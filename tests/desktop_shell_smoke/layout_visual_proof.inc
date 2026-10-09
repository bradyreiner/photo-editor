    const auto photo_path = runtime_root.filePath(QStringLiteral("layout-photo.png"));
    if (!write_overlay_visual_fixture(photo_path, QStringLiteral("saturated"))) {
      return fail("Layout proof should create its photo fixture");
    }
    catalog_assets.replaceAssetsForTesting({make_asset(QStringLiteral("layout-photo"), photo_path)});
    catalog_assets.selectIndex(0);
    engine.load(QUrl::fromLocalFile(main_qml_path));
    process_layout_events(10);
    if (engine.rootObjects().isEmpty()) {
      return fail("Layout proof should load Main.qml");
    }
    auto* visual_root = engine.rootObjects().constFirst();
    auto* visual_window = qobject_cast<QQuickWindow*>(visual_root);
    if (visual_window == nullptr) {
      return fail("Layout proof requires a real QQuickWindow");
    }
    const auto proof_dir = qEnvironmentVariableIsSet("PHOTO_EDITOR_VISUAL_PROOF_DIR")
        ? QString::fromUtf8(qgetenv("PHOTO_EDITOR_VISUAL_PROOF_DIR"))
        : runtime_root.filePath(QStringLiteral("layout-visual-proof"));
    if (!QDir().mkpath(proof_dir)) {
      return fail("Layout proof should create its artifact directory");
    }
    QJsonArray manifest;
    const auto capture = [&](const QString& state, const QStringList& required) {
      return capture_visual_proof(visual_window, visual_root, proof_dir, state,
                                  1280, 800, required, &manifest);
    };
    const auto show_workspace = [&](const char* index_property, const QString& object_name) {
      visual_root->setProperty("currentWorkspace", visual_root->property(index_property));
      const auto ready_control = object_name == QStringLiteral("editStudioWorkspace")
          ? QStringLiteral("editWorkspaceLayoutButton")
          : object_name == QStringLiteral("libraryStudioWorkspace")
              ? QStringLiteral("libraryWorkspaceLayoutButton")
              : QStringLiteral("exportWorkspaceLayoutButton");
      return wait_for_condition([&]() {
        auto* workspace = qobject_cast<QQuickItem*>(find_named_object(visual_root, object_name));
        auto* control = find_named_item(visual_root, ready_control);
        return visual_root->property("currentWorkspace") == visual_root->property(index_property) &&
               !visual_root->property("workspaceTransitionActive").toBool() &&
               workspace != nullptr && item_visible(workspace) && control != nullptr &&
               item_visible(control) && control->window() == visual_window &&
               (object_name != QStringLiteral("editStudioWorkspace") ||
                workspace->property("pageContentReady").toBool());
      }, 15000);
    };
    const auto settle_compact = [&]() {
      visual_root->setProperty("width", 900);
      visual_root->setProperty("height", 700);
      QElapsedTimer timer;
      timer.start();
      while (timer.elapsed() < 150) {
        process_layout_events(1);
        QThread::msleep(2);
      }
      visual_window->update();
      process_layout_events(8);
    };
    if (!show_workspace("libraryWorkspaceIndex", QStringLiteral("libraryStudioWorkspace"))) {
      return fail("Layout proof should load Library");
    }
    auto* library = find_named_object(visual_root, QStringLiteral("libraryStudioWorkspace"));
    process_layout_events(10);
    if (!capture(QStringLiteral("library-default"),
                 {QStringLiteral("libraryStudioWorkspace"), QStringLiteral("libraryWorkspaceLayoutButton")})) {
      return fail("Layout proof should capture Library");
    }
    library->setProperty("importModeOpen", true);
    if (!wait_for_condition([&]() {
          auto* import_workspace = find_named_object(visual_root, QStringLiteral("importWorkspace"));
          return import_workspace != nullptr &&
                 !import_workspace->property("currentFolderPath").toString().isEmpty();
        }, 3000)) {
      return fail("Import should open its default source folder before capture");
    }
    process_layout_events(8);
    if (!capture(QStringLiteral("import-empty"),
                 {QStringLiteral("libraryStudioWorkspace"), QStringLiteral("importWorkspace")})) {
      return fail("Layout proof should capture Import at desktop size");
    }
    settle_compact();
    if (!capture_visual_proof(visual_window, visual_root, proof_dir,
                              QStringLiteral("import-compact"), 900, 700,
                              {QStringLiteral("importWorkspace")}, &manifest)) {
      return fail("Layout proof should capture compact Import");
    }
    library->setProperty("importModeOpen", false);
    process_layout_events(6);
    if (!QMetaObject::invokeMethod(library, "handleWorkspaceLayoutMenu", Q_ARG(QVariant, QVariant(QStringLiteral("focus")))) ||
        library->property("librarySourcesPanelVisible").toBool() ||
        library->property("libraryInspectorPanelVisible").toBool()) {
      return fail("Library focus layout should hide optional panels");
    }
    visual_window->update();
    QElapsedTimer library_focus_settle;
    library_focus_settle.start();
    while (library_focus_settle.elapsed() < 150) {
      process_layout_events(1);
      QThread::msleep(2);
    }
    if (!capture(QStringLiteral("library-focus"),
                 {QStringLiteral("libraryStudioWorkspace"),
                  QStringLiteral("libraryWorkspaceLayoutButton")})) {
      return fail("Layout proof should capture focused Library");
    }
    if (!show_workspace("editWorkspaceIndex", QStringLiteral("editStudioWorkspace"))) {
      return fail("Layout proof should load Edit");
    }
    auto* edit = find_named_object(visual_root, QStringLiteral("editStudioWorkspace"));
    process_layout_events(12);
    if (!capture(QStringLiteral("edit-default"),
                 {QStringLiteral("editStudioWorkspace"), QStringLiteral("editWorkspaceLayoutButton")})) {
      return fail("Layout proof should capture Edit");
    }
    edit->setProperty("editContextPanelVisible", false);
    edit->setProperty("editInspectorPanelVisible", false);
    edit->setProperty("editFilmstripCollapsed", true);
    app_shell.setInterfaceDensity(QStringLiteral("compact"));
    if (!QMetaObject::invokeMethod(edit, "handleWorkspaceLayoutMenu",
                                   Q_ARG(QVariant, QVariant(QStringLiteral("save-layout"))))) {
      return fail("Edit Layout menu should open the save-layout dialog");
    }
    auto* layout_dialog = find_named_object(edit, QStringLiteral("workspaceLayoutSaveDialog"));
    auto* layout_name = find_named_object(edit, QStringLiteral("workspaceLayoutNameField"));
    if (layout_dialog == nullptr || !layout_dialog->property("visible").toBool() ||
        layout_name == nullptr) {
      return fail("Edit should expose a named layout save dialog");
    }
    if (!capture(QStringLiteral("edit-save-layout"),
                 {QStringLiteral("editStudioWorkspace")})) {
      return fail("Layout proof should capture the save-layout dialog");
    }
    layout_name->setProperty("text", QStringLiteral("Proof layout"));
    if (!QMetaObject::invokeMethod(layout_dialog, "accept")) {
      return fail("Edit should save the named layout from its dialog");
    }
    auto* layout_menu_button = find_named_object(edit, QStringLiteral("editWorkspaceLayoutButton"));
    if (layout_menu_button == nullptr ||
        !QMetaObject::invokeMethod(layout_menu_button, "openMenu")) {
      return fail("Edit should open its Layout menu after saving a layout");
    }
    process_layout_events(4);
    if (!capture(QStringLiteral("edit-layout-menu"),
                 {QStringLiteral("editStudioWorkspace"),
                  QStringLiteral("editWorkspaceLayoutButtonPopup")})) {
      return fail("Layout proof should capture the named-layout menu");
    }
    QMetaObject::invokeMethod(layout_menu_button, "closeMenu");
    edit->setProperty("editContextPanelVisible", true);
    edit->setProperty("editInspectorPanelVisible", true);
    edit->setProperty("editFilmstripCollapsed", false);
    app_shell.setInterfaceDensity(QStringLiteral("balanced"));
    if (!QMetaObject::invokeMethod(edit, "handleWorkspaceLayoutMenu",
                                   Q_ARG(QVariant, QVariant(QStringLiteral("layout:Proof layout")))) ||
        edit->property("editContextPanelVisible").toBool() ||
        edit->property("editInspectorPanelVisible").toBool() ||
        !edit->property("editFilmstripCollapsed").toBool() ||
        app_shell.interfaceDensity() != QStringLiteral("balanced")) {
      return fail("Edit Layout menu should restore saved panels without changing density");
    }
    if (!QMetaObject::invokeMethod(edit, "handleWorkspaceLayoutMenu",
                                   Q_ARG(QVariant, QVariant(QStringLiteral("delete-layout:Proof layout"))))) {
      return fail("Edit Layout menu should remove a saved layout");
    }
    QVariant deleted_layout_result;
    if (!QMetaObject::invokeMethod(edit, "applyWorkspaceLayout",
                                   Q_RETURN_ARG(QVariant, deleted_layout_result),
                                   Q_ARG(QVariant, QVariant(QStringLiteral("Proof layout")))) ||
        deleted_layout_result.toBool()) {
      return fail("Deleted Edit layouts should no longer be available");
    }
    edit->setProperty("editContextPanelVisible", true);
    edit->setProperty("editInspectorPanelVisible", true);
    edit->setProperty("editFilmstripCollapsed", false);
    app_shell.setInterfaceDensity(QStringLiteral("balanced"));
    if (!QMetaObject::invokeMethod(edit, "handleWorkspaceLayoutMenu", Q_ARG(QVariant, QVariant(QStringLiteral("focus")))) ||
        edit->property("editContextPanelVisible").toBool() ||
        edit->property("editInspectorPanelVisible").toBool()) {
      return fail("Edit focus layout should hide optional panels");
    }
    process_layout_events(8);
    if (!capture(QStringLiteral("edit-focus"), {QStringLiteral("editStudioWorkspace")})) {
      return fail("Layout proof should capture focused Edit");
    }
    if (!show_workspace("exportWorkspaceIndex", QStringLiteral("exportStudioWorkspaceContent"))) {
      return fail("Layout proof should load Export");
    }
    auto* export_view = find_named_object(visual_root, QStringLiteral("exportStudioWorkspaceContent"));
    process_layout_events(12);
    QVariant selection_summary;
    if (!QMetaObject::invokeMethod(export_view, "selectionSummaryText",
                                   Q_RETURN_ARG(QVariant, selection_summary)) ||
        !selection_summary.toString().contains(QStringLiteral("Current photo"))) {
      return fail("Export should identify the active photo when no batch is selected");
    }
    auto* export_preflight = find_named_object(
        visual_root, QStringLiteral("exportPreflightSection"));
    auto* export_summary = find_named_object(
        visual_root, QStringLiteral("exportDeliveryReviewSummaryCard"));
    auto* export_action = find_named_object(
        visual_root, QStringLiteral("exportCurrentSplitButton"));
    auto* export_guidance = find_named_object(
        visual_root, QStringLiteral("exportActionGuidanceLabel"));
    auto* export_guidance_row = find_named_object(
        visual_root, QStringLiteral("exportActionGuidanceRow"));
    auto* export_destination_repair = find_named_object(
        visual_root, QStringLiteral("exportBrowseBlockedDestinationButton"));
    if (!wait_for_condition([&]() {
          return !export_jobs.preflightBusy() &&
                 export_jobs.singlePreflight().contains(QStringLiteral("ok")) &&
                 export_summary != nullptr && export_summary->property("visible").toBool();
        }, 10000) ||
        export_preflight == nullptr || !export_preflight->property("visible").toBool() ||
        export_action == nullptr || export_guidance == nullptr ||
        export_guidance_row == nullptr || export_destination_repair == nullptr ||
        (!export_action->property("primaryEnabled").toBool() &&
         (!export_guidance_row->property("visible").toBool() ||
          export_guidance->property("text").toString().trimmed().isEmpty() ||
          (export_guidance->property("text").toString().contains(
               QStringLiteral("destination"), Qt::CaseInsensitive) &&
           !export_destination_repair->property("visible").toBool())))) {
      return fail("Export should expose current-photo preflight and explain a blocked primary action");
    }
    if (!capture(QStringLiteral("export-default"),
                 {QStringLiteral("exportStudioWorkspaceContent"),
                  QStringLiteral("exportWorkspaceLayoutButton"),
                  QStringLiteral("exportDeliveryReviewSummaryCard")})) {
      return fail("Layout proof should capture Export");
    }
    if (!export_view->setProperty("deliveryDestinationPath", runtime_root.path()) ||
        !wait_for_condition([&]() {
          return !export_jobs.preflightBusy() &&
                 export_jobs.singlePreflight().value(QStringLiteral("ok")).toBool() &&
                 export_action->property("primaryEnabled").toBool() &&
                 !export_guidance_row->property("visible").toBool();
        }, 10000)) {
      return fail("Export should clear blocking guidance and enable delivery for a writable destination");
    }
    auto* export_filename_preview = find_named_object(
        visual_root, QStringLiteral("exportFilenamePreviewLabel"));
    const auto resolved_output_name = QFileInfo(
        export_jobs.singlePreflight().value(QStringLiteral("outputPath")).toString()).fileName();
    if (export_filename_preview == nullptr || resolved_output_name.isEmpty() ||
        !export_filename_preview->property("text").toString().contains(resolved_output_name) ||
        export_filename_preview->property("text").toString().contains(runtime_root.path())) {
      return fail("Export filename preview should show the output name without repeating the destination path");
    }
    QElapsedTimer export_ready_settle;
    export_ready_settle.start();
    while (export_ready_settle.elapsed() < 180) {
      process_layout_events(1);
      QThread::msleep(2);
    }
    if (!capture(QStringLiteral("export-ready"),
                 {QStringLiteral("exportStudioWorkspaceContent"),
                  QStringLiteral("exportDeliveryReviewSummaryCard"),
                  QStringLiteral("exportCurrentSplitButton")})) {
      return fail("Layout proof should capture ready Export");
    }
    if (!QMetaObject::invokeMethod(export_view, "handleExportLayoutMenu", Q_ARG(QVariant, QVariant(QStringLiteral("focus-settings")))) ||
        export_view->property("exportPreviewVisible").toBool() ||
        export_view->property("exportActivityVisible").toBool()) {
      return fail("Export settings layout should hide optional panels");
    }
    process_layout_events(8);
    if (!capture(QStringLiteral("export-settings"), {QStringLiteral("exportStudioWorkspaceContent")})) {
      return fail("Layout proof should capture focused Export");
    }
    if (!QMetaObject::invokeMethod(export_view, "handleExportLayoutMenu", Q_ARG(QVariant, QVariant(QStringLiteral("show-main"))))) {
      return fail("Export review should restore its main panels");
    }
    settle_compact();
    if (!capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("export-compact"),
                              900, 700, {QStringLiteral("exportStudioWorkspaceContent"),
                                         QStringLiteral("exportWorkspaceLayoutButton")}, &manifest)) {
      return fail("Layout proof should capture compact Export");
    }
    if (!show_workspace("editWorkspaceIndex", QStringLiteral("editStudioWorkspace")) ||
        !QMetaObject::invokeMethod(edit, "handleWorkspaceLayoutMenu", Q_ARG(QVariant, QVariant(QStringLiteral("show-all"))))) {
      return fail("Layout proof should restore Edit");
    }
    settle_compact();
    if (!capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("edit-compact"),
                              900, 700, {QStringLiteral("editStudioWorkspace"),
                                         QStringLiteral("editWorkspaceLayoutButton")}, &manifest)) {
      return fail("Layout proof should capture compact Edit");
    }
    auto* compact_viewport = find_named_item(edit, QStringLiteral("editImageViewport"));
    auto* compact_context = find_named_item(edit, QStringLiteral("editContextPanel"));
    auto* compact_inspector = find_named_item(edit, QStringLiteral("editInspectorPanel"));
    if (!compact_viewport || !item_visible(compact_context) || !item_visible(compact_inspector) ||
        compact_viewport->width() < 359) {
      return fail("Compact Edit should retain both panels and at least 360 px of photo working space");
    }
    const auto compact_canvas_width = compact_viewport->width();
    const auto compact_density = edit->property("interfaceMode");
    for (const auto resize_width : {950, 1280, 900}) {
      visual_root->setProperty("width", resize_width);
      process_layout_events(8);
      if (!item_visible(compact_context) || !item_visible(compact_inspector) ||
          edit->property("interfaceMode") != compact_density || compact_viewport->width() < 359) {
        return fail("Resizing Edit should preserve selected panels and density while protecting photo space");
      }
    }
    auto* sidebar_toggle = find_named_object(edit, QStringLiteral("editSidebarToggleButton"));
    if (!sidebar_toggle || !QMetaObject::invokeMethod(sidebar_toggle, "clicked")) {
      return fail("Compact Edit should expose its Library panel toggle");
    }
    process_layout_events(8);
    wait_for_condition([&] {
      visual_window->update();
      return !item_visible(compact_context) && item_visible(compact_inspector) &&
          compact_viewport->width() >= compact_canvas_width + 80;
    });
    if (item_visible(compact_context) || !item_visible(compact_inspector) ||
        compact_viewport->width() < compact_canvas_width + 80 ||
        !capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("edit-compact-inspector-only"),
                              900, 700, {QStringLiteral("editImageViewport"), QStringLiteral("editInspectorPanel")}, &manifest)) {
      std::cerr << "Compact Edit toggle: context=" << item_visible(compact_context)
                << " inspector=" << item_visible(compact_inspector)
                << " collapsed=" << edit->property("editSidebarCollapsed").toBool()
                << " canvas=" << compact_viewport->width() << " previous=" << compact_canvas_width
                << " inspector-width=" << compact_inspector->width() << '\n';
      return fail("Hiding the compact Library panel should reclaim usable photo space");
    }
    QMetaObject::invokeMethod(sidebar_toggle, "clicked");
    process_layout_events(8);
    for (const auto& tab : {QStringLiteral("presets"), QStringLiteral("filters"),
                           QStringLiteral("layers"), QStringLiteral("history")}) {
      auto* button = find_named_object(edit, tab + QStringLiteral("InspectorTabButton"));
      if (!button || !QMetaObject::invokeMethod(button, "clicked")) {
        return fail("Compact Edit should keep every inspector tab reachable");
      }
      process_layout_events(8);
      if (!wait_for_condition([&] {
            return edit->property("editInspectorTab").toString() == tab &&
                !edit->property("editToolTransitionActive").toBool() && compact_viewport->width() >= 359;
          }) ||
          !capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("edit-compact-") + tab,
                                900, 700, {QStringLiteral("editImageViewport"), QStringLiteral("editInspectorPanel")}, &manifest)) {
        return fail("Compact Edit inspector sections should keep the photograph visible");
      }
      if (tab == QStringLiteral("layers")) {
        auto* header = find_named_item(edit, QStringLiteral("photoLayerSparseHeader"));
        auto* count_label = find_named_item(edit, QStringLiteral("photoLayerStackCountLabel"));
        auto* add_layer = find_named_item(edit, QStringLiteral("photoLayerAddSplitButton"));
        auto* layer_menu = find_named_item(edit, QStringLiteral("layerVisibilityStateMenuButton"));
        auto* base_row = find_named_item(edit, QStringLiteral("baseImageLayerRow"));
        auto* base_title = find_named_item(edit, QStringLiteral("baseImageLayerNameLabel"));
        if (!header || !count_label || !add_layer || !layer_menu || !base_row || !base_title ||
            !scene_rect(header).contains(scene_rect(add_layer)) ||
            !scene_rect(header).contains(scene_rect(layer_menu)) ||
            scene_rect(add_layer).intersects(scene_rect(layer_menu)) ||
            !scene_rect(base_row).contains(scene_rect(base_title)) ||
            base_title->property("contentWidth").toDouble() > base_title->width() + 1 ||
            count_label->property("contentWidth").toDouble() > count_label->width() + 1) {
          return fail("Compact layer controls and base image identity should fit without overlapping");
        }
        const auto recipe_before_layer = editor_preview.currentAdjustmentPatchJson();
        if (!QMetaObject::invokeMethod(add_layer, "clicked") ||
            !wait_for_condition([&] {
              return editor_preview.photoLayerCount() == 1 &&
                  edit->property("editInspectorTab").toString() == QStringLiteral("adjust") &&
                  !edit->property("editToolTransitionActive").toBool();
            }) ||
            !QMetaObject::invokeMethod(button, "clicked") ||
            !wait_for_condition([&] {
              return edit->property("editInspectorTab").toString() == QStringLiteral("layers") &&
                  !edit->property("editToolTransitionActive").toBool();
            }) ||
            !capture_visual_proof(visual_window, visual_root, proof_dir,
                                  QStringLiteral("edit-compact-layers-populated"), 900, 700,
                                  {QStringLiteral("photoLayerRow"), QStringLiteral("photoLayerPropertiesPanel")},
                                  &manifest)) {
          return fail("Compact Layers should expose the added layer and its properties");
        }
        auto* layer_row = find_named_item(edit, QStringLiteral("photoLayerRow"));
        auto* layer_name = find_named_item(edit, QStringLiteral("photoLayerNameLabel"));
        auto* properties = find_named_item(edit, QStringLiteral("photoLayerPropertiesPanel"));
        auto* properties_name = find_named_item(edit, QStringLiteral("photoLayerPropertiesNameLabel"));
        auto* opacity = find_named_item(edit, QStringLiteral("photoLayerOpacitySlider"));
        auto* opacity_caption = find_named_item(edit, QStringLiteral("photoLayerOpacitySliderCaption"));
        auto* blend = find_named_item(edit, QStringLiteral("photoLayerBlendModeComboBox"));
        auto* blend_caption = find_named_item(edit, QStringLiteral("photoLayerBlendModeCaption"));
        if (!layer_row || !layer_name || !properties || !properties_name || !opacity || !blend ||
            !item_visible(opacity_caption) || !item_visible(blend_caption) ||
            !scene_rect(layer_row).contains(scene_rect(layer_name)) ||
            layer_name->property("contentWidth").toDouble() > layer_name->width() + 1 ||
            properties_name->property("contentWidth").toDouble() > properties_name->width() + 1 ||
            !scene_rect(opacity).contains(scene_rect(opacity_caption)) ||
            !scene_rect(properties).contains(scene_rect(opacity)) ||
            !scene_rect(properties).contains(scene_rect(blend)) ||
            scene_rect(opacity).intersects(scene_rect(blend))) {
          return fail("Compact Layers should keep added layer identity, opacity, and blend controls readable");
        }
        opacity->forceActiveFocus();
        if (!send_window_key(visual_window, Qt::Key_Left) || !wait_for_condition([&] {
              return opacity->property("valueText").toString() == QStringLiteral("99%");
            })) {
          return fail("The labeled layer opacity control should retain keyboard editing and visible value feedback");
        }
        editor_preview.undo();
        if (!wait_for_condition([&] {
              return opacity->property("valueText").toString() == QStringLiteral("100%");
            })) {
          return fail("Undo should restore the layer opacity and its visible caption");
        }
        editor_preview.undo();
        if (!wait_for_condition([&] { return editor_preview.photoLayerCount() == 0; }) ||
            editor_preview.currentAdjustmentPatchJson() != recipe_before_layer) {
          return fail("Undo should restore the recipe after checking the populated compact stack");
        }
      }
    }
    QMetaObject::invokeMethod(find_named_object(edit, QStringLiteral("adjustInspectorTabButton")), "clicked");
    auto* zoom_step = find_named_object(edit, QStringLiteral("zoomStepButton"));
    const auto expected_zoom_text = QStringLiteral("%1%").arg(qRound(
        compact_viewport->property("zoom").toDouble() /
        compact_viewport->property("oneToOneZoom").toDouble() * 100));
    if (!zoom_step || !zoom_step->property("labelVisible").toBool() ||
        zoom_step->property("text").toString() != expected_zoom_text) {
      return fail("Preview zoom should show its current percentage of source pixels");
    }
    const auto recipe_before_fit = editor_preview.currentAdjustmentPatchJson();
    if (!QMetaObject::invokeMethod(compact_viewport, "setZoom", Q_ARG(QVariant, QVariant(2.0)),
                                  Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant())) ||
        compact_viewport->property("zoom").toDouble() != 2.0) {
      return fail("Fit proof should first magnify the preview");
    }
    QMetaObject::invokeMethod(find_named_object(edit, QStringLiteral("fitToViewButton")), "clicked");
    if (compact_viewport->property("zoom").toDouble() != 1.0 ||
        compact_viewport->property("panX").toDouble() != 0.0 ||
        compact_viewport->property("panY").toDouble() != 0.0 ||
        editor_preview.currentAdjustmentPatchJson() != recipe_before_fit) {
      return fail("Fit should reset preview framing without changing the photo edits");
    }
    if (!layout_menu_button || !QMetaObject::invokeMethod(layout_menu_button, "openMenu")) {
      return fail("Edit should expose compact density through its Layout menu");
    }
    QObject* density_item = nullptr;
    const auto density_before_menu = edit->property("compactMode").toBool();
    const auto density_menu_key = density_before_menu ? QStringLiteral("density:balanced") : QStringLiteral("density:compact");
    wait_for_condition([&] {
      for (int index = 0; index < 32; ++index) {
        auto* item = find_named_object(visual_root,
            QStringLiteral("editWorkspaceLayoutButtonPopupItem%1").arg(index));
        if (item &&
            item->property("itemKey").toString() == density_menu_key) {
          density_item = item;
          return true;
        }
      }
      return false;
    });
    if (!density_item || density_item->property("itemChecked").toBool() ||
        !capture_visual_proof(visual_window, visual_root, proof_dir,
          QStringLiteral("edit-compact-layout-menu"), 900, 700,
          {QStringLiteral("editWorkspaceLayoutButtonPopup")}, &manifest) ||
        !QMetaObject::invokeMethod(density_item, "clicked") ||
        edit->property("compactMode").toBool() == density_before_menu || !item_visible(compact_context) ||
        !item_visible(compact_inspector)) {
      return fail("Layout menu should change density while preserving selected panels");
    }
    auto* file_menu = find_named_object(visual_root, QStringLiteral("applicationFileMenu"));
    const bool native_application_menu = QGuiApplication::platformName() == QStringLiteral("cocoa") &&
        !QCoreApplication::testAttribute(Qt::AA_DontUseNativeMenuBar);
    if (native_application_menu) {
      // Cocoa places this menu in the system menu bar, outside grabWindow().
      // Keep validating the populated menu and the actual workspace; an OS-level
      // menu screenshot requires a separate desktop capture, not this fixture.
      if (!file_menu || file_menu->property("count").toInt() < 1 ||
          !file_menu->property("title").toString().contains(QStringLiteral("File")))
        return fail("The native application File menu should retain its actions");
      std::cout << "Native macOS File menu is populated; system menu rendering is outside app-window capture\n";
    } else {
      if (!file_menu || !QMetaObject::invokeMethod(file_menu, "open") ||
          !capture_visual_proof(visual_window, visual_root, proof_dir,
            QStringLiteral("edit-compact-file-menu"), 900, 700,
            {QStringLiteral("applicationMenuBar"), QStringLiteral("applicationFileMenu")}, &manifest))
        return fail("The application menu should use the same restrained surface as the editor");
      QMetaObject::invokeMethod(file_menu, "close");
    }
    if (!show_workspace("libraryWorkspaceIndex", QStringLiteral("libraryStudioWorkspace")) ||
        !QMetaObject::invokeMethod(library, "handleWorkspaceLayoutMenu", Q_ARG(QVariant, QVariant(QStringLiteral("show-all"))))) {
      return fail("Layout proof should restore Library");
    }
    settle_compact();
    if (!capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("library-compact"),
                              900, 700, {QStringLiteral("libraryStudioWorkspace"),
                                         QStringLiteral("libraryWorkspaceLayoutButton")}, &manifest)) {
      return fail("Layout proof should capture compact Library");
    }
    edit->setProperty("editContextPanelVisible", false);
    edit->setProperty("editInspectorPanelVisible", false);
    edit->setProperty("editFilmstripCollapsed", true);
    app_shell.setInterfaceDensity(QStringLiteral("compact"));
    QVariant reopened_layout_saved;
    if (!QMetaObject::invokeMethod(edit, "saveWorkspaceLayout",
                                   Q_RETURN_ARG(QVariant, reopened_layout_saved),
                                   Q_ARG(QVariant, QVariant(QStringLiteral("Reopen proof")))) ||
        !reopened_layout_saved.toBool()) {
      return fail("Layout proof should save an arrangement before relaunch");
    }
    if (!wait_for_condition([&]() {
          QSettings settings;
          settings.sync();
          return settings.value(QStringLiteral("StudioWorkspace-edit/savedLayoutsJson"))
              .toString().contains(QStringLiteral("Reopen proof"));
        }, 3000)) {
      return fail("Named Edit layout should reach isolated persistent settings");
    }
    delete visual_root;
    visual_root = nullptr;
    visual_window = nullptr;
    process_layout_events(4);
    engine.load(QUrl::fromLocalFile(main_qml_path));
    process_layout_events(10);
    if (engine.rootObjects().isEmpty()) {
      return fail("Layout proof should reload Main.qml with saved settings");
    }
    visual_root = engine.rootObjects().constLast();
    visual_window = qobject_cast<QQuickWindow*>(visual_root);
    if (visual_window == nullptr ||
        !show_workspace("editWorkspaceIndex", QStringLiteral("editStudioWorkspace"))) {
      return fail("Layout proof should reopen Edit after a window restart");
    }
    edit = find_named_object(visual_root, QStringLiteral("editStudioWorkspace"));
    QVariant reopened_layout_applied;
    if (edit == nullptr ||
        !QMetaObject::invokeMethod(edit, "applyWorkspaceLayout",
                                   Q_RETURN_ARG(QVariant, reopened_layout_applied),
                                   Q_ARG(QVariant, QVariant(QStringLiteral("Reopen proof")))) ||
        !reopened_layout_applied.toBool() ||
        edit->property("editContextPanelVisible").toBool() ||
        edit->property("editInspectorPanelVisible").toBool() ||
        !edit->property("editFilmstripCollapsed").toBool() ||
        edit->property("interfaceMode").toString() != QStringLiteral("compact")) {
      return fail("Named Edit layout should restore after reloading Main.qml");
    }
    if (!capture(QStringLiteral("edit-reopened-layout"),
                 {QStringLiteral("editStudioWorkspace"),
                  QStringLiteral("editWorkspaceLayoutButton")})) {
      return fail("Layout proof should capture the restored Edit layout after reload");
    }
    QFile manifest_file(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
    if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        manifest_file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("Layout proof should persist its manifest");
    }
    return EXIT_SUCCESS;

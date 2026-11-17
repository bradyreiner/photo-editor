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
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER")) {
      activate_native_test_window(visual_window);
      if (!wait_for_condition([&] { return visual_window->isActive() && visual_window->isExposed(); }, 5000))
        return fail("Native layout review requires an active exposed window");
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
          ? QStringLiteral("editViewMoreButton")
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
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_VIEWER_TOOLS_PROOF_ONLY")) {
#include "viewer_tools_proof.inc"
    }
    if (!show_workspace("libraryWorkspaceIndex", QStringLiteral("libraryStudioWorkspace"))) {
      return fail("Layout proof should load Library");
    }
    auto* library = find_named_object(visual_root, QStringLiteral("libraryStudioWorkspace"));
    process_layout_events(10);
    if (!capture(QStringLiteral("library-default"),
                 {QStringLiteral("libraryStudioWorkspace"), QStringLiteral("libraryWorkspaceLayoutButton")})) {
      return fail("Layout proof should capture Library");
    }
    const auto previous_scale = app_shell.interfaceScale();
    if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_BOARD_CREATION_ONLY")) {
    // Exercise the actual lazy-loaded Settings window, including enlarged text,
    // rather than inferring its density from the shared control specimens.
    if (!QMetaObject::invokeMethod(visual_root, "openPreferencesDialog", Q_ARG(QVariant, QVariant(0))))
      return fail("Settings should open from the application shell");
    QObject* preferences = nullptr;
    if (!wait_for_condition([&]() {
          preferences = find_named_object(visual_root, QStringLiteral("preferencesDialog"));
          return preferences && preferences->property("visible").toBool();
        }, 5000))
      return fail("Settings should finish loading before layout review");
    for (int page = 0; page < 7; ++page) {
      preferences->setProperty("currentPage", page);
      process_layout_events(8);
      if (!capture(QStringLiteral("settings-page-%1").arg(page),
                   {QStringLiteral("preferencesNavigationPanel"), QStringLiteral("preferencesPageHeader")}))
        return fail("Every Settings page should render with shared styling");
    }
    preferences->setProperty("searchQuery", QStringLiteral("cache"));
    if (!wait_for_condition([&] {
          return preferences->property("currentPage").toInt() == 6 &&
                 item_visible(find_named_item(visual_root, QStringLiteral("preferencesSystemDetailsPage")));
        })) return fail("Settings search should route cache queries to System Details");
    if (!capture(QStringLiteral("settings-search-system"),
                 {QStringLiteral("preferencesSearchField"), QStringLiteral("preferencesSystemDetailsPage")}))
      return fail("Settings search should render matching technical settings");
    const auto visible_preference = [&](const QString& name) -> QQuickItem* {
      for (auto* item : find_named_items(preferences, name)) {
        if (item_visible(item)) return item;
      }
      return nullptr;
    };
    auto* storage_details = visible_preference(QStringLiteral("storagePathDetailsButton"));
    if (!storage_details || !QMetaObject::invokeMethod(storage_details, "clicked"))
      return fail("Storage paths need an explicit Details control");
    process_layout_events(6);
    auto* storage_path = visible_preference(QStringLiteral("configuredStoragePathField"));
    auto* copy_storage_path = visible_preference(QStringLiteral("copyStoragePathButton"));
    if (!storage_path || !copy_storage_path || !item_visible(storage_path) ||
        !item_visible(copy_storage_path) || !storage_path->property("readOnly").toBool() ||
        storage_path->property("text").toString() != app_shell.appDataLocation())
      return fail("Storage Details must preserve a selectable full path and Copy action");
    auto previous_clipboard = std::make_unique<QMimeData>();
    if (const auto* mime = QGuiApplication::clipboard()->mimeData()) {
      for (const auto& format : mime->formats()) previous_clipboard->setData(format, mime->data(format));
    }
    const auto path_copied = QMetaObject::invokeMethod(copy_storage_path, "clicked") &&
        QGuiApplication::clipboard()->text() == app_shell.appDataLocation();
    QGuiApplication::clipboard()->setMimeData(previous_clipboard.release());
    if (!path_copied) return fail("Copy path should copy the actual storage location");
    if (!capture(QStringLiteral("settings-storage-details"),
          {QStringLiteral("configuredStoragePathField"), QStringLiteral("copyStoragePathButton")}))
      return fail("Storage path details should render when requested");
    QMetaObject::invokeMethod(storage_details, "clicked");
    preferences->setProperty("searchQuery", QStringLiteral("nothing-matches-this-query"));
    if (!wait_for_condition([&] {
          return item_visible(find_named_item(visual_root, QStringLiteral("preferencesNoSearchResults"))) &&
                 item_visible(find_named_item(visual_root, QStringLiteral("preferencesCloseButton")));
        })) return fail("An empty Settings search must retain Clear and Close");
    auto* search_field = find_named_item(visual_root, QStringLiteral("preferencesSearchField"));
    if (!search_field) return fail("Settings search needs an accessible input");
    search_field->forceActiveFocus(Qt::TabFocusReason);
    if (!send_window_key(visual_window, Qt::Key_Escape) ||
        !preferences->property("visible").toBool() ||
        !preferences->property("searchQuery").toString().isEmpty())
      return fail("Escape in Settings search should clear the query before closing the dialog");
    preferences->setProperty("searchQuery", QStringLiteral("reduce motion"));
    if (!wait_for_condition([&] {
          return preferences->property("currentPage").toInt() == 0 &&
                 item_visible(find_named_item(visual_root, QStringLiteral("preferencesPageContent")));
        })) return fail("Settings search must match a setting name across words");
    preferences->setProperty("searchQuery", QString{});
    preferences->setProperty("currentPage", 0);
    app_shell.setInterfaceScale(2.0);
    process_layout_events(12);
    if (!capture(QStringLiteral("settings-enlarged"),
                 {QStringLiteral("preferencesNavigationPanel"), QStringLiteral("preferencesPageHeader")}))
      return fail("Settings navigation and content should remain reachable with enlarged text");
    send_window_key(visual_window, Qt::Key_Escape);
    if (!wait_for_condition([&]() { return !preferences->property("visible").toBool(); }, 2000))
      return fail("Escape should close Settings without changing the workspace");
    app_shell.setInterfaceScale(previous_scale);
    process_layout_events(8);
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_PREFERENCES_ONLY"))
      return EXIT_SUCCESS;
    visual_root->setProperty("currentWorkspace", visual_root->property("timelineWorkspaceIndex"));
    if (!wait_for_condition([&] {
          return item_visible(find_named_item(visual_root, QStringLiteral("timelineSetupSurface"))) &&
                 !visual_root->property("workspaceTransitionActive").toBool();
        }, 15000)) return fail("The empty Timeline workspace should show its creation page");
    auto* timeline_setup = find_named_object(visual_root, QStringLiteral("timelineWorkspace"));
    auto* timeline_advanced = find_named_item(visual_root, QStringLiteral("timelineSetupAdvancedButton"));
    auto* timeline_options = find_named_item(visual_root, QStringLiteral("timelineSetupAdvancedOptions"));
    auto* timeline_create = find_named_item(visual_root, QStringLiteral("timelineCreateButton"));
    if (!timeline_setup || !timeline_advanced || !timeline_options || !timeline_create ||
        item_visible(timeline_options) || !timeline_create->isEnabled())
      return fail("Timeline creation should lead with name, format, and Create while Advanced stays closed");
    for (const auto width : {1280, 900}) {
      if (!capture_visual_proof(visual_window, visual_root, proof_dir,
            QStringLiteral("timeline-create-%1").arg(width), width, 700,
            {QStringLiteral("timelineSetupNameField"), QStringLiteral("timelineSetupFormatCombo"),
             QStringLiteral("timelineCreateButton")}, &manifest) ||
          !QRectF(0, 0, width, 700).contains(scene_rect(timeline_create)))
        return fail("Timeline creation actions must remain visible at desktop and compact widths");
    }
    QMetaObject::invokeMethod(timeline_advanced, "clicked");
    timeline_setup->setProperty("setupFrameRate", QStringLiteral("24"));
    timeline_setup->setProperty("setupProjectBinding", QStringLiteral("Proof project"));
    QMetaObject::invokeMethod(timeline_advanced, "clicked");
    QMetaObject::invokeMethod(timeline_advanced, "clicked");
    if (!timeline_setup->property("setupAdvancedExpanded").toBool() ||
        timeline_setup->property("setupFrameRate").toString() != QStringLiteral("24") ||
        timeline_setup->property("setupProjectBinding").toString() != QStringLiteral("Proof project") ||
        !capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("timeline-create-advanced"),
            900, 700, {QStringLiteral("timelineSetupAdvancedOptions"), QStringLiteral("timelineCreateButton")}, &manifest))
      return fail("Advanced timeline settings should survive disclosure and keep Create reachable");
    if (!show_workspace("libraryWorkspaceIndex", QStringLiteral("libraryStudioWorkspace")))
      return fail("Creation-page review should return to Library without creating a document");
    // Check each clipping ancestor, not just the control's own visible flag.
    // A RowLayout may leave a live AX action outside the gallery's clip.
    const auto library_action_exposed = [&](const QString& name) {
      auto* item = find_named_item(visual_root, name);
      if (!item || !item_visible(item) || item->width() <= 0 || item->height() <= 0) return false;
      const auto bounds = item->mapRectToScene(item->boundingRect());
      if (!QRectF(0, 0, visual_window->width(), visual_window->height()).adjusted(-1, -1, 1, 1).contains(bounds)) return false;
      for (auto* parent = item->parentItem(); parent; parent = parent->parentItem()) {
        if (parent->clip() && !parent->mapRectToScene(parent->boundingRect()).adjusted(-1, -1, 1, 1).contains(bounds)) return false;
      }
      return true;
    };
    for (const auto width : {1280, 1100, 900}) {
      visual_window->resize(width, 800);
      process_layout_events(12);
      for (const auto* action : {"studioLibrarySearchInput", "studioLibraryImportButton",
                                 "libraryWorkspaceLayoutButtonPrimaryAction", "studioLibrarySelectButton"}) {
        if (!library_action_exposed(QString::fromLatin1(action)))
          return fail("Library search, Import, Layout and Select must remain inside every clipping ancestor");
      }
      auto* compact_view = find_named_item(visual_root, QStringLiteral("libraryReviewViewMenuButtonPrimaryAction"));
      auto* cull_view = find_named_item(visual_root, QStringLiteral("studioLibraryCullButton"));
      if ((!compact_view || !item_visible(compact_view)) && (!cull_view || !item_visible(cull_view)))
        return fail("Compact Library must retain a reachable culling/review view action");
      if (!capture_visual_proof(visual_window, visual_root, proof_dir,
                                QStringLiteral("library-toolbar-%1").arg(width), width, 800,
                                {QStringLiteral("studioLibraryImportButton"), QStringLiteral("studioLibrarySelectButton")}, &manifest))
        return fail("Library toolbar proof should capture each supported width");
    }
    visual_window->resize(1280, 800);
    process_layout_events(8);
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
    }
    if (!show_workspace("editWorkspaceIndex", QStringLiteral("editStudioWorkspace"))) {
      return fail("Layout proof should load Edit");
    }
    auto* edit = find_named_object(visual_root, QStringLiteral("editStudioWorkspace"));
    process_layout_events(12);
    auto* rail = find_named_object(edit, QStringLiteral("studioEditToolRail"));
    if (!rail) return fail("Edit should retain a visible tool rail");
    for (const auto& entry : rail->property("tools").toList()) {
      const auto id = entry.toMap().value("id").toString();
      if (id == QStringLiteral("presets") || id == QStringLiteral("export"))
        return fail("Inspector and delivery navigation should not duplicate editing tools");
    }
    const auto recipe_before_presets = editor_preview.currentAdjustmentPatchJson();
    auto* presets_tab = find_named_object(edit, QStringLiteral("presetsInspectorTabButton"));
    if (!presets_tab || !QMetaObject::invokeMethod(presets_tab, "clicked") ||
        edit->property("editInspectorTab").toString() != QStringLiteral("presets") ||
        !edit->property("editInspectorPanelVisible").toBool() ||
        editor_preview.currentAdjustmentPatchJson() != recipe_before_presets)
      return fail("Presets must remain directly reachable without editing the photo");
    QMetaObject::invokeMethod(find_named_object(edit, QStringLiteral("adjustInspectorTabButton")), "clicked");
    process_layout_events(6);
    if (!capture(QStringLiteral("edit-default"),
                 {QStringLiteral("editStudioWorkspace"), QStringLiteral("editViewMoreButton")})) {
      return fail("Layout proof should capture Edit");
    }
    // Occasional edit commands share one compact opener beside the inspector
    // tabs. Exercise the actual menu rows, including their deferred dispatch.
    auto* adjustment_actions = find_named_object(edit, QStringLiteral("editInspectorActionsButton"));
    auto* edit_viewport = find_named_object(edit, QStringLiteral("editImageViewport"));
    if (!adjustment_actions || !edit_viewport ||
        find_named_object(edit, QStringLiteral("editInspectorFooter")) ||
        find_named_object(edit, QStringLiteral("fillViewButton")) ||
        !QMetaObject::invokeMethod(adjustment_actions, "openMenu"))
      return fail("Edit should consolidate occasional actions without duplicate canvas controls");
    if (!capture(QStringLiteral("edit-adjustment-actions"),
                 {QStringLiteral("editInspectorActionsButtonPopup")}))
      return fail("Adjustment actions should render inside the shared overflow menu");
    auto* save_preset_row = find_named_object(edit, QStringLiteral("editInspectorActionsButtonPopupItem0"));
    if (!save_preset_row || !QMetaObject::invokeMethod(save_preset_row, "clicked") ||
        !wait_for_condition([&] {
          auto* dialog = find_named_object(edit, QStringLiteral("studioPresetCreateDialog"));
          return dialog && dialog->property("visible").toBool();
        })) return fail("Save preset must still open its workflow from the inspector menu");
    QMetaObject::invokeMethod(find_named_object(edit, QStringLiteral("studioPresetCreateDialog")), "close");
    editor_preview.setExposure(0.37);
    if (!QMetaObject::invokeMethod(adjustment_actions, "openMenu") ||
        !wait_for_condition([&] {
          return item_visible(find_named_item(edit, QStringLiteral("editInspectorActionsButtonPopupItem1")));
        })) return fail("The reset command should remain reachable through adjustment actions");
    if (!QMetaObject::invokeMethod(find_named_object(edit, QStringLiteral("editInspectorActionsButtonPopupItem1")), "clicked") ||
        !wait_for_condition([&] { return std::abs(editor_preview.exposure()) < 0.0001; }))
      return fail("Reset from the menu should reset the photo");
    editor_preview.undo();
    if (std::abs(editor_preview.exposure() - 0.37) > 0.0001)
      return fail("Reset from the menu must preserve Undo");
    editor_preview.undo();
    const auto recipe_before_view_commands = editor_preview.currentAdjustmentPatchJson();
    for (const auto* mode : {"fill", "fit"}) {
      if (!QMetaObject::invokeMethod(edit, "handleEditViewMenuAction", Q_ARG(QVariant, QString::fromLatin1(mode))) ||
          edit_viewport->property("fitMode").toString() != QString::fromLatin1(mode) ||
          editor_preview.currentAdjustmentPatchJson() != recipe_before_view_commands)
        return fail("Consolidated view commands must change framing without editing the photo");
    }
    if (!show_workspace("libraryWorkspaceIndex", QStringLiteral("libraryStudioWorkspace")))
      return fail("Canvas creation starts in Library");
    auto* creation_library = find_named_object(visual_root, QStringLiteral("libraryStudioWorkspace"));
    const auto project_creation = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_BOARD_CREATION_ONLY");
    if (project_creation) {
      catalog_assets.clearFilters();
      catalog_assets.setMediaCollectionFilter(QStringLiteral("all"));
      catalog_assets.replaceAssetsForTesting({make_asset(QStringLiteral("layout-photo"), photo_path)});
      catalog_assets.selectIndexWithModifiers(0, 0, false);
      if (catalog_assets.selectedAssetCount() != 1) return fail("Select the project photo fixture");
      catalog_assets.createProjectWithSelectedAssets(QStringLiteral("Canvas project"));
      catalog_assets.setProjectFilter(QStringLiteral("Canvas project"));
      auto project_photo = make_asset(QStringLiteral("layout-photo"), photo_path);
      project_photo.project_names = {QStringLiteral("Canvas project")};
      catalog_assets.replaceAssetsForTesting({project_photo});
    }
    creation_library->setProperty("libraryCollection", project_creation ? QStringLiteral("images") : QStringLiteral("boards"));
    const auto creation_name = project_creation ? QStringLiteral("libraryProjectNewCanvasButton") : QStringLiteral("libraryNewBoardsButton");
    if (!wait_for_condition([&] { return item_visible(find_named_item(visual_root, creation_name)); }, 5000))
      return fail("Library should expose creation within the selected project");
    auto* new_board = find_named_object(visual_root, creation_name);
    if (!new_board || !QMetaObject::invokeMethod(new_board, "clicked"))
      return fail("Library New Boards should open the size chooser");
    auto* canvas_dialog = find_named_object(visual_root, QStringLiteral("newCanvasDialog"));
    if (!wait_for_condition([&] { return canvas_dialog && canvas_dialog->property("opened").toBool(); }))
      return fail("New Canvas should finish opening before visual review");
    auto* canvas_advanced = find_named_item(visual_root, QStringLiteral("newCanvasAdvancedButton"));
    auto* canvas_dimensions = find_named_item(visual_root, QStringLiteral("newCanvasDimensionsGroup"));
    if (!canvas_advanced || !canvas_dimensions || !item_visible(canvas_dimensions))
      return fail("New Canvas should show dimensions immediately");
    for (const auto scale : {1.0, 2.0}) {
      app_shell.setInterfaceScale(scale);
      if (!capture(QStringLiteral("new-canvas-basic-%1x").arg(scale),
            {QStringLiteral("newCanvasNameField"), QStringLiteral("newCanvasPresetField"),
             QStringLiteral("newCanvasDimensionsGroup"), QStringLiteral("newCanvasFooter"), QStringLiteral("newCanvasCreateButton")}))
        return fail("New Canvas default actions must fit at normal and enlarged text sizes");
      QMetaObject::invokeMethod(canvas_advanced, "clicked");
      if (!capture(QStringLiteral("new-canvas-advanced-%1x").arg(scale),
            {QStringLiteral("newCanvasDimensionsGroup"), QStringLiteral("newCanvasFooter")}))
        return fail("Advanced canvas options should scroll above a persistent action footer");
      QMetaObject::invokeMethod(canvas_advanced, "clicked");
    }
    app_shell.setInterfaceScale(previous_scale);
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_BOARD_CREATION_ONLY")) {
      const bool two_video_import = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_BOARD_TWO_VIDEO_IMPORT");
      const int creation_width = two_video_import ? 3840 : 640, creation_height = two_video_import ? 3072 : 800;
      find_named_object(visual_root, QStringLiteral("newCanvasWidthField"))->setProperty("text", QString::number(creation_width));
      find_named_object(visual_root, QStringLiteral("newCanvasHeightField"))->setProperty("text", QString::number(creation_height));
      auto* create = find_named_object(visual_root, QStringLiteral("newCanvasCreateButton"));
      if (!create || !QMetaObject::invokeMethod(create, "clicked") ||
          !wait_for_condition([&] {
            return visual_root->property("editBoardsActive").toBool() && editor_preview.sourceImageWidth() == creation_width &&
                   editor_preview.sourceImageHeight() == creation_height && !editor_preview.processing() &&
                   !visual_root->property("workspaceTransitionActive").toBool();
          }, 20000)) return fail("Library creates a blank board at the chosen independent size");
      if (two_video_import) {
#include "board_two_video_import_proof.inc"
      }
      auto* board_layout_group = find_named_object(visual_root, QStringLiteral("boardLayoutInspectorGroup"));
      if (!board_layout_group || board_layout_group->property("expanded").toBool() ||
          item_visible(find_named_item(visual_root, QStringLiteral("boardPhotoControls"))) ||
          !item_visible(find_named_item(visual_root, QStringLiteral("photoLayersInspectorGroup"))) ||
          !board_layout_group->setProperty("expanded", true) ||
          !wait_for_condition([&] {
            return item_visible(find_named_item(visual_root, QStringLiteral("boardPhotoControls"))) &&
                   item_visible(find_named_item(visual_root, QStringLiteral("photoLayersInspectorGroup")));
          }, 5000))
        return fail("Board Layout expands above the visible Layers group in the stacked inspector");
      if (!capture(QStringLiteral("blank-board-add-images"),
          {QStringLiteral("boardAddImagesButton"), QStringLiteral("boardPhotoControls"), QStringLiteral("editCanvasAddBoardButton")}))
        return fail("A blank board visibly offers Add media in the sidebar and layout controls");
      if (find_named_object(visual_root, QStringLiteral("blankBoardAddImagesButton")))
        return fail("Add media must not float over the canvas");
      if (carousel_document.projectName() != QStringLiteral("Canvas project")) return fail("Creation retains the selected project");
      const auto canvas_document_id = carousel_document.savedDocumentId();
      const auto initial_canvas_recipe = editor_preview.currentAssetBoundAdjustmentPatchJson();
      if (!QMetaObject::invokeMethod(edit, "openBoardImagePicker") || !wait_for_condition([&] {
            return visual_root->property("currentWorkspace") == visual_root->property("libraryWorkspaceIndex") &&
                   !visual_root->property("workspaceTransitionActive").toBool(); }, 15000))
        return fail("Add images returns to the full Library");
      if (!capture(QStringLiteral("canvas-library-selection"), {QStringLiteral("libraryCanvasMediaSelectionBar"), QStringLiteral("studioLibrarySearchInput")}))
        return fail("Canvas selection retains normal Library search and filters");
      if (!QMetaObject::invokeMethod(visual_root, "cancelCanvasMediaSelection") || !wait_for_condition([&] {
            return visual_root->property("currentWorkspace") == visual_root->property("editWorkspaceIndex") &&
                   !visual_root->property("workspaceTransitionActive").toBool(); }, 15000) ||
          carousel_document.savedDocumentId() != canvas_document_id || editor_preview.currentAssetBoundAdjustmentPatchJson() != initial_canvas_recipe)
        return fail("Cancel returns to the original canvas without changing its recipe");
      auto edited_photo = make_asset(QStringLiteral("placed-edited-photo"), photo_path);
      photo_editor::desktop::EditRecipeState edited_photo_recipe; edited_photo_recipe.exposure = -0.5;
      edited_photo.adjustment_patch_json = photo_editor::desktop::serialize_adjustment_patch_json(edited_photo_recipe,
          photo_editor::desktop::all_adjustment_groups(), photo_editor::desktop::AdjustmentPatchScope::AssetBound);
      edited_photo.project_names = {QStringLiteral("Canvas project")};
      catalog_assets.replaceAssetsForTesting({edited_photo});
      catalog_assets.addAssetToProject(QStringLiteral("placed-edited-photo"), QStringLiteral("Canvas project"));
      if (!wait_for_condition([&] { return catalog_assets.assetCountForProject(QStringLiteral("Canvas project")) == 1; }, 5000))
        return fail("The edited placement fixture belongs to the selected project");
      for (int image = 0; image < 2; ++image) {
        if (!QMetaObject::invokeMethod(edit, "openBoardImagePicker") || !wait_for_condition([&] {
              return visual_root->property("currentWorkspace") == visual_root->property("libraryWorkspaceIndex") &&
                     !visual_root->property("workspaceTransitionActive").toBool(); }, 15000)) return fail("Open full Library for canvas selection");
        catalog_assets.setSearchFilter(QStringLiteral("layout"));
        catalog_assets.selectIndexWithModifiers(0, 0, false);
        auto* add = find_named_object(visual_root, QStringLiteral("libraryAddPhotosToCanvasButton"));
        if (!add || !add->property("enabled").toBool() || !QMetaObject::invokeMethod(add, "clicked") ||
            !wait_for_condition([&] { return editor_preview.photoLayerCount() == (image == 0 ? 1 : 3) && !editor_preview.processing() &&
                visual_root->property("currentWorkspace") == visual_root->property("editWorkspaceIndex") &&
                !visual_root->property("workspaceTransitionActive").toBool(); }, 20000))
        {
          std::cerr << "Canvas selection count=" << catalog_assets.selectedAssetCount()
                    << " assets=" << catalog_assets.assetCount() << " total=" << catalog_assets.totalAssetCount()
                    << " project=" << catalog_assets.projectFilter().toStdString()
                    << " workspace=" << visual_root->property("currentWorkspace").toInt()
                    << " pending=" << visual_root->property("pendingCanvasMediaPaths").toList().size()
                    << " layers=" << editor_preview.photoLayerCount()
                    << " status=" << carousel_document.status().toStdString() << '\n';
          return fail("Add the filtered Library selection to the original canvas as full image layers");
        }
      }
      QQuickItem* apply = nullptr;
      for (const auto& value : editor_preview.photoLayers()) {
        const auto layer = value.toMap();
        if (layer.value("kind") != QStringLiteral("source")) continue;
        const auto recipe = photo_editor::desktop::parse_adjustment_payload_or_legacy_recipe(layer.value("sourceRecipeJson").toString());
        if (!recipe.ok() || recipe.state->exposure != -0.5 || layer.value("sourceAssetId") != QStringLiteral("placed-edited-photo")) {
          std::cerr << "Placed source=" << layer.value("sourceAssetId").toString().toStdString()
                    << " recipe=" << layer.value("sourceRecipeJson").toString().toStdString() << '\n';
          return fail("Library placement retains each selected photo's exact saved recipe and source identity");
        }
      }
      if (!board_layout_group->setProperty("expanded", true) ||
          !wait_for_condition([&] { return item_visible(find_named_item(visual_root, QStringLiteral("boardPhotoControls"))); }, 5000))
        return fail("Board layout controls stay one selection away after adding media");
      for (auto* candidate : find_named_items(visual_root, QStringLiteral("boardApplyPhotoLayoutButton")))
        if (item_visible(candidate)) { apply = candidate; break; }
      if (!apply || !QMetaObject::invokeMethod(apply, "clicked") ||
          !wait_for_condition([&] { return !editor_preview.processing() && !editor_preview.fullRenderRunning(); }, 15000) ||
          !editor_preview.photoLayerStyles(0).isEmpty() || !editor_preview.photoLayerStyles(1).isEmpty() ||
          editor_preview.photoLayers().last().toMap().value("kind") != QStringLiteral("fill") ||
          !capture(QStringLiteral("board-vertical-layout-white-borders"), {QStringLiteral("boardPhotoControls"), QStringLiteral("editImageViewport"), QStringLiteral("boardPhotoReframeButton")}))
        return fail("Visible layout controls stack independent photos with editable white borders");
      auto* reframe_button = find_named_object(visual_root, QStringLiteral("boardPhotoReframeButton"));
      for (auto* candidate : find_named_items(visual_root, QStringLiteral("boardPhotoReframeButton")))
        if (item_visible(candidate)) { reframe_button = candidate; break; }
      if (!reframe_button || !QMetaObject::invokeMethod(reframe_button, "clicked")) return fail("Expose non-destructive photo reframing controls");
      auto* vertical_frame = find_named_object(visual_root, QStringLiteral("boardPhotoReframeVertical"));
      for (auto* candidate : find_named_items(visual_root, QStringLiteral("boardPhotoReframeVertical")))
        if (item_visible(candidate)) { vertical_frame = candidate; break; }
      if (!vertical_frame) return fail("The selected photo exposes vertical framing");
      vertical_frame->setProperty("value", 0);
      if (!QMetaObject::invokeMethod(vertical_frame, "valueModified") || !wait_for_condition([&] {
            return !editor_preview.processing() && !editor_preview.fullRenderRunning(); }, 15000) ||
          !capture(QStringLiteral("board-photo-reframe-controls"), {QStringLiteral("boardPhotoReframeVertical"), QStringLiteral("editImageViewport")}))
        return fail("Reframe photo content without modifying the linked original");
      QMetaObject::invokeMethod(reframe_button, "clicked");
      if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_BOARD_VIDEO_ONLY")) {
        const auto source_video = runtime_root.filePath(QStringLiteral("board-sequence-source.mp4"));
        QProcess fixture;
        fixture.start(QStandardPaths::findExecutable(QStringLiteral("ffmpeg")),
            {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("testsrc2=duration=2:size=120x80:rate=30"), QStringLiteral("-c:v"), QStringLiteral("libx264"),
             QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), source_video});
        if (!fixture.waitForFinished(15000) || fixture.exitCode() != 0) return fail("Create native board video fixture");
        catalog_assets.clearFilters();
        catalog_assets.setProjectFilter(QStringLiteral("all"));
        auto video_asset = make_asset(QStringLiteral("board-video"), source_video);
        video_asset.media_type = QStringLiteral("video"); video_asset.duration_seconds = 2.0;
        catalog_assets.replaceAssetsForTesting({make_asset(QStringLiteral("layout-photo"), photo_path), video_asset});
        int video_index = -1;
        for (int row = 0; row < catalog_assets.assetCount(); ++row)
          if (catalog_assets.assetAt(row).value("assetId") == QStringLiteral("board-video")) video_index = row;
        if (video_index < 0) return fail("Find the Library video fixture");
        catalog_assets.selectIndexWithModifiers(video_index, 0, false);
        catalog_assets.addSelectedAssetsToProject(QStringLiteral("Canvas project"));
        if (!QMetaObject::invokeMethod(edit, "openBoardImagePicker") || !wait_for_condition([&] {
              return visual_root->property("currentWorkspace") == visual_root->property("libraryWorkspaceIndex") &&
                     !visual_root->property("workspaceTransitionActive").toBool(); }, 15000)) return fail("Choose video through the full Library");
        catalog_assets.setMediaTypeFilter(QStringLiteral("video"));
        catalog_assets.selectIndexWithModifiers(0, 0, false);
        auto* add_video = find_named_object(visual_root, QStringLiteral("libraryAddPhotosToCanvasButton"));
        if (!add_video || !QMetaObject::invokeMethod(add_video, "clicked") || !wait_for_condition([&] {
              return !timeline_document.carouselBoardSession().isEmpty() &&
                     visual_root->property("currentWorkspace") == visual_root->property("timelineWorkspaceIndex") &&
                     !visual_root->property("workspaceTransitionActive").toBool() && !timeline_document.mediaAnalysisBusy(); }, 25000))
          return fail("Library video opens the owning board's Timeline sequence");
        timeline_document.setSelectedTrimStartSeconds(0.25);
        timeline_document.setSelectedTrimEndSeconds(1.25);
        if (!timeline_document.setSelectedClipTransform(0.2, 0.1, 0.5, 0)) return fail("Position board video in Timeline");
        process_layout_events(12);
        if (!capture(QStringLiteral("carousel-board-timeline-trim-placement"), {QStringLiteral("carouselBoardSequenceBar"), QStringLiteral("carouselSequenceApplyButton")}))
          return fail("The board sequence exposes the native Timeline controls and Apply action");
        auto* apply_video = find_named_object(visual_root, QStringLiteral("carouselSequenceApplyButton"));
        if (!apply_video || !QMetaObject::invokeMethod(apply_video, "clicked") || !wait_for_condition([&] {
              return timeline_document.carouselBoardSession().isEmpty() && !carousel_document.boardSequence(0).isEmpty() &&
                     visual_root->property("currentWorkspace") == visual_root->property("editWorkspaceIndex") &&
                     !visual_root->property("workspaceTransitionActive").toBool(); }, 60000)) {
          std::cerr << timeline_document.status().toStdString() << '\n';
          return fail("Apply video to the board and return to its editable photo canvas");
        }
        if (editor_preview.photoLayerCount() != 4 || carousel_document.savedDocumentId() != canvas_document_id ||
            !board_layout_group->setProperty("expanded", true) ||
            !wait_for_condition([&] { return item_visible(find_named_item(visual_root, QStringLiteral("boardPhotoControls"))); }, 5000) ||
            !capture(QStringLiteral("carousel-board-video-layout"), {QStringLiteral("boardApplyPhotoLayoutButton"), QStringLiteral("boardEditVideoSequenceButton"), QStringLiteral("editCanvasAddBoardButton")}))
          return fail("The board retains editable photo and video layers, shared layout controls and the plus action");
        if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER")) {
          QQuickItem* handle = nullptr;
          for (auto* candidate : find_named_items(visual_root, QStringLiteral("photoLayerTransformBottomRightHandle")))
            if (item_visible(candidate)) { handle = candidate; break; }
          if (!handle || editor_preview.photoLayers().front().toMap().value("boardVideoSource").toMap().isEmpty())
            return fail("The selected video exposes the same native resize handles as a photo");
          const auto original_recipe = editor_preview.currentAssetBoundAdjustmentPatchJson();
          const auto original_scale = editor_preview.photoLayers().front().toMap().value("scale").toDouble();
          const auto start = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
          const auto finish = start - QPoint(32, 20);
          QTest::mousePress(visual_window, Qt::LeftButton, Qt::NoModifier, start, 10);
          for (int step = 1; step <= 8; ++step) {
            QTest::mouseMove(visual_window, start + (finish - start) * step / 8, 10);
            process_layout_events(1);
          }
          QTest::mouseRelease(visual_window, Qt::LeftButton, Qt::NoModifier, finish, 10);
          if (!wait_for_condition([&] { return editor_preview.photoLayers().front().toMap().value("scale").toDouble() < original_scale &&
                  !editor_preview.processing() && !editor_preview.fullRenderRunning(); }, 15000) ||
              !capture(QStringLiteral("carousel-board-video-native-resize"), {QStringLiteral("photoLayerTransformFrame"), QStringLiteral("boardApplyPhotoLayoutButton")}))
            return fail("Native pointer resizing changes the video's editable board geometry");
          editor_preview.undo();
          if (!wait_for_condition([&] { return editor_preview.currentAssetBoundAdjustmentPatchJson() == original_recipe &&
                  !carousel_document.boardSequence(0).value("needsUpdate").toBool(); }, 15000))
            return fail("Undo video resizing restores the exact applied geometry and playable sequence");
        }
        auto* play = find_named_object(visual_root, QStringLiteral("boardSequencePlayButton"));
        for (auto* candidate : find_named_items(visual_root, QStringLiteral("boardSequencePlayButton")))
          if (item_visible(candidate)) { play = candidate; break; }
        if (!play || !play->property("enabled").toBool() || !QMetaObject::invokeMethod(play, "clicked")) return fail("Play the freshly applied board sequence in the carousel without a spurious stale-layout warning");
        process_layout_events(25);
        if (!capture(QStringLiteral("carousel-board-video-playing"), {QStringLiteral("boardVideoSequencePreview")})) return fail("Capture board playback");
        QMetaObject::invokeMethod(play, "clicked");
        const auto click_board_control = [&](const QString& name) {
          for (auto* candidate : find_named_items(visual_root, name)) {
            if (!item_visible(candidate) || !candidate->isEnabled()) continue;
            const auto point = scene_rect(candidate).center().toPoint();
            if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER"))
              QCursor::setPos(visual_window->mapToGlobal(point));
            QTest::mouseClick(visual_window, Qt::LeftButton, Qt::NoModifier, point);
            process_layout_events(5);
            return true;
          }
          return false;
        };
        if (!click_board_control(QStringLiteral("editCanvasAddBoardButtonPrimaryAction")) ||
            !wait_for_condition([&] { return item_visible(find_named_item(visual_root, QStringLiteral("editCanvasAddBoardButtonPopupItem0"))); }, 5000) ||
            !capture(QStringLiteral("carousel-add-board-menu"), {QStringLiteral("editCanvasAddBoardButtonPopupItem0")} ) ||
            !click_board_control(QStringLiteral("editCanvasAddBoardButtonPopupItem0")) ||
            !wait_for_condition([&] { return carousel_document.slideCount() == 2 && carousel_document.currentIndex() == 1 && !editor_preview.processing(); }, 15000) ||
            !carousel_document.boardSequence(1).isEmpty()) return fail("The restored plus adds a separate board beside the video");
        if (!wait_for_condition([&] {
              auto* previous = find_named_item(visual_root, QStringLiteral("editCanvasBoardPreview0"));
              return item_visible(previous) && previous->property("status").toInt() == 1 &&
                  previous->property("source").toUrl().toLocalFile() == carousel_document.boardSequence(0).value("posterPath").toString();
            }, 5000)) return fail("The previous video board retains its rendered poster while the new board is active");
        if (!capture(QStringLiteral("carousel-video-and-new-blank-board"), {QStringLiteral("editCanvasAddBoardButton"), QStringLiteral("editImageViewport")}))
          return fail("Capture restored Add board workflow after video authoring");
        if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_BOARD_LIBRARY_ONLY")) {
          if (!show_workspace("libraryWorkspaceIndex", QStringLiteral("libraryStudioWorkspace")))
            return fail("Return to the project's Library after saving Boards");
          catalog_assets.setProjectFilter(QStringLiteral("Canvas project"));
          auto* library_workspace = find_named_object(visual_root, QStringLiteral("libraryStudioWorkspace"));
          library_workspace->setProperty("libraryCollection", QStringLiteral("boards"));
          const auto open_name = QStringLiteral("libraryOpenBoardsButton") + canvas_document_id;
          if (!wait_for_condition([&] { return !carousel_document.recentBoardDocumentsLoading() &&
                item_visible(find_named_item(visual_root, open_name)); }, 15000))
            return fail("The saved board appears under its project through the asynchronous Boards list");
          if (!wait_for_condition([&] {
                auto* cover = find_named_item(visual_root, QStringLiteral("savedBoardThumbnail"));
                return item_visible(cover) && cover->property("status").toInt() == 1 && !cover->property("source").toUrl().isEmpty();
              }, 15000)) return fail("The visible project board receives its cover without another list query");
          if (!capture(QStringLiteral("project-saved-boards-library"), {QStringLiteral("savedBoardsLibrary"), open_name}))
            return fail("Capture the saved project board and its Open in Edit action");
          catalog_assets.setProjectFilter(QStringLiteral("unassigned"));
          if (!wait_for_condition([&] { return !item_visible(find_named_item(visual_root, open_name)); }, 5000))
            return fail("Project Boards do not leak into Unassigned");
          catalog_assets.setProjectFilter(QStringLiteral("Canvas project"));
          if (!wait_for_condition([&] { return item_visible(find_named_item(visual_root, open_name)); }, 5000))
            return fail("Switching back to the project reuses the cached board listing");
        }
        return EXIT_SUCCESS;
      }
      auto* size_button = find_named_item(visual_root, QStringLiteral("boardCanvasSizeButton"));
      for (auto* candidate : find_named_items(visual_root, QStringLiteral("boardCanvasSizeButton")))
        if (item_visible(candidate)) { size_button = candidate; break; }
      if (!size_button || !QMetaObject::invokeMethod(size_button, "clicked") ||
          !wait_for_condition([&] { return canvas_dialog->property("opened").toBool(); }, 5000)) return fail("Open canvas size after placing photos");
      auto* resolution = find_named_object(visual_root, QStringLiteral("newCanvasResolutionField"));
      resolution->setProperty("currentIndex", 2);
      QMetaObject::invokeMethod(resolution, "activated", Q_ARG(int, 2));
      auto* apply_size = find_named_object(visual_root, QStringLiteral("newCanvasCreateButton"));
      if (!apply_size || !QMetaObject::invokeMethod(apply_size, "clicked") || !wait_for_condition([&] {
            return editor_preview.sourceImageWidth() == 3072 && editor_preview.sourceImageHeight() == 3840 && !editor_preview.processing(); }, 25000) ||
          carousel_document.savedDocumentId() != canvas_document_id || carousel_document.projectName() != QStringLiteral("Canvas project") ||
          editor_preview.photoLayerCount() != 3) return fail("Resize the same project canvas to 4K while retaining placed images");
      if (!board_layout_group->setProperty("expanded", true) ||
          !wait_for_condition([&] { return item_visible(find_named_item(visual_root, QStringLiteral("boardPhotoControls"))); }, 5000) ||
          !capture(QStringLiteral("project-canvas-4k"), {QStringLiteral("boardPhotoControls"), QStringLiteral("editImageViewport")}))
        return fail("Render resized project canvas");
      return EXIT_SUCCESS;
    }
    QMetaObject::invokeMethod(canvas_dialog, "close");
    process_layout_events(8);
    if (!show_workspace("editWorkspaceIndex", QStringLiteral("editStudioWorkspace")))
      return fail("Return to Edit after reviewing Library canvas creation");
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
    auto* layout_menu_button = find_named_object(edit, QStringLiteral("editViewMoreButton"));
    if (layout_menu_button == nullptr ||
        !QMetaObject::invokeMethod(layout_menu_button, "openMenu")) {
      return fail("Edit should open its View menu after saving a layout");
    }
    process_layout_events(4);
    if (!capture(QStringLiteral("edit-layout-menu"),
                 {QStringLiteral("editStudioWorkspace"),
                  QStringLiteral("editViewMoreButtonPopup")})) {
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
    if (!show_workspace("exportWorkspaceIndex", QStringLiteral("exportStudioWorkspace"))) {
      return fail("Layout proof should load Export");
    }
    auto* export_host = find_named_object(visual_root, QStringLiteral("exportStudioWorkspace"));
    auto* export_view = find_named_object(export_host, QStringLiteral("exportStudioWorkspaceContent"));
    auto* export_view_item = qobject_cast<QQuickItem*>(export_view);
    process_layout_events(12);
    if (export_host == nullptr || export_view == nullptr || export_view_item == nullptr) {
      return fail("Export should route through ExportWorkspaceHost to the active ExportView");
    }
    const bool original_export_activity_preference =
        export_view->property("exportActivityVisible").toBool();
    const auto restore_export_activity_preference = qScopeGuard(
        [view = QPointer<QObject>(export_view), original_export_activity_preference]() {
      if (view && view->property("exportActivityVisible").toBool() !=
          original_export_activity_preference) {
        view->setProperty("exportActivityVisible", original_export_activity_preference);
      }
    });
    if (original_export_activity_preference &&
        !QMetaObject::invokeMethod(export_view, "handleExportLayoutMenu",
                                   Q_ARG(QVariant, QVariant(QStringLiteral("activity"))))) {
      return fail("Export layout proof should normalize its idle activity preference");
    }
    photo_editor::desktop::JobQueueModelTestAccess::setHistoryFixture(job_queue, {});
    job_queue.refresh();
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
                 !job_queue.loading() && job_queue.jobCount() == 0 &&
                 export_summary != nullptr && export_summary->property("visible").toBool();
        }, 10000) ||
        export_preflight == nullptr || !export_preflight->property("visible").toBool() ||
        export_action == nullptr || export_guidance == nullptr ||
        export_guidance_row == nullptr || export_destination_repair == nullptr ||
        (!export_action->property("enabled").toBool() &&
         (!export_guidance_row->property("visible").toBool() ||
          export_guidance->property("text").toString().trimmed().isEmpty() ||
          (export_guidance->property("text").toString().contains(
               QStringLiteral("destination"), Qt::CaseInsensitive) &&
           !export_destination_repair->property("visible").toBool())))) {
      return fail("Export should expose current-photo preflight and explain a blocked primary action");
    }
    auto* export_queue = find_named_item(export_view, QStringLiteral("exportQueuePanel"));
    if (!export_queue || export_view->property("exportActivityVisible").toBool() ||
        export_view->property("deliveryActivityNeedsAttention").toBool() ||
        export_view->property("exportActivityOpen").toBool() || item_visible(export_queue)) {
      return fail("Idle Export should keep Delivery activity collapsed when the saved preference is off");
    }
    if (!capture(QStringLiteral("export-default"),
                 {QStringLiteral("exportStudioWorkspace"),
                  QStringLiteral("exportStudioWorkspaceContent"),
                  QStringLiteral("exportConfigurationPanel"),
                  QStringLiteral("exportPresetSection"),
                  QStringLiteral("exportDestinationNameLabel"),
                  QStringLiteral("exportDestinationChangeButton"),
                  QStringLiteral("exportDestinationMoreMenuButton"),
                  QStringLiteral("exportPresetSummaryLine"),
                  QStringLiteral("exportWorkspaceLayoutButton"),
                  QStringLiteral("exportDeliveryReviewSummaryCard")})) {
      return fail("Layout proof should capture Export");
    }
    auto* export_configuration = find_named_item(export_view, QStringLiteral("exportConfigurationPanel"));
    auto* export_preset = find_named_item(export_view, QStringLiteral("exportPresetCombo"));
    auto* export_preset_summary = find_named_item(export_view, QStringLiteral("exportPresetSummaryLine"));
    auto* destination_name = find_named_item(export_view, QStringLiteral("exportDestinationNameLabel"));
    auto* destination_change = find_named_item(export_view, QStringLiteral("exportDestinationChangeButton"));
    auto* destination_edit = find_named_item(export_view, QStringLiteral("exportDestinationMoreMenuButton"));
    auto* destination_field = find_named_item(export_view, QStringLiteral("exportDestinationField"));
    if (export_configuration == nullptr || export_queue == nullptr || export_preset == nullptr ||
        export_preset_summary == nullptr || destination_name == nullptr || destination_change == nullptr ||
        destination_edit == nullptr || destination_field == nullptr ||
        (item_visible(export_queue) && export_configuration->mapToItem(export_view_item, 0, 0).x() <=
            export_queue->mapToItem(export_view_item, 0, 0).x()) ||
        export_preset->property("currentValue").toString() != QStringLiteral("Full Resolution") ||
        export_preset->property("currentText").toString() != QStringLiteral("Full size PNG") ||
        !item_visible(export_preset_summary) ||
        export_preset_summary->property("text").toString().contains(QChar('\n')) ||
        export_preset_summary->property("wrapMode").toInt() != 0 ||
        export_preset_summary->height() > export_preset_summary->implicitHeight() + 1.0 ||
        destination_change->property("text").toString() != QStringLiteral("Change") ||
        !item_visible(destination_change)) {
      return fail("Active Export should lead with settings and preserve the full preset value behind its short display label");
    }
    const auto saved_destination = export_view->property("deliveryDestinationPath").toString();
    const auto expected_destination_name = QFileInfo(QDir::cleanPath(saved_destination)).fileName();
    if (saved_destination.isEmpty() || expected_destination_name.isEmpty() ||
        destination_name->property("text").toString() != expected_destination_name ||
        !item_visible(destination_name) || !item_visible(destination_change) || !item_visible(destination_edit) ||
        item_visible(destination_field)) {
      return fail("Export should show the destination folder name at rest and keep the full path in an explicit edit control");
    }
    if (!QMetaObject::invokeMethod(destination_edit, "openMenu"))
      return fail("Export destination overflow should open");
    process_layout_events(4);
    auto* edit_path_action = find_named_item(export_view, QStringLiteral("exportDestinationMoreMenuButtonPopupItem0"));
    if (!edit_path_action || !QMetaObject::invokeMethod(edit_path_action, "activated", Q_ARG(QString, QStringLiteral("edit-path"))))
      return fail("Export should open its full-path editor from the destination menu");
    process_layout_events(4);
    if (!item_visible(destination_field) || !destination_field->property("focus").toBool() ||
        destination_field->property("text").toString() != saved_destination) {
      return fail("Destination edit should begin with the full saved path");
    }
    destination_field->setProperty("text", expected_destination_name);
    destination_field->setProperty("focus", false);
    process_layout_events(2);
    if (destination_field->property("focus").toBool() ||
        destination_field->property("activeFocus").toBool() ||
        export_view->property("deliveryDestinationPath").toString() != saved_destination ||
        !export_view->property("destinationEditing").toBool()) {
      return fail("Losing focus must not commit the folder label as a destination path");
    }
    if (!QMetaObject::invokeMethod(export_view, "cancelDestinationEdit") ||
        export_view->property("deliveryDestinationPath").toString() != saved_destination ||
        item_visible(destination_field)) {
      return fail("Canceling destination editing should restore the unchanged full path");
    }
    if (!QMetaObject::invokeMethod(export_view, "beginDestinationEdit", Q_ARG(QVariant, QVariant{}))) {
      return fail("Export should reopen the saved destination for editing");
    }
    process_layout_events(3);
    if (destination_field->property("text").toString() != saved_destination ||
        !QMetaObject::invokeMethod(export_view, "commitDestinationEdit") ||
        export_view->property("deliveryDestinationPath").toString() != saved_destination) {
      return fail("A no-change destination edit should round-trip the exact saved full path");
    }
    if (!export_view->setProperty("deliveryDestinationPath", runtime_root.path()) ||
        !wait_for_condition([&]() {
          return !export_jobs.preflightBusy() &&
                 export_jobs.singlePreflight().value(QStringLiteral("ok")).toBool() &&
                 export_action->property("enabled").toBool() &&
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
                 {QStringLiteral("exportStudioWorkspace"),
                  QStringLiteral("exportStudioWorkspaceContent"),
                  QStringLiteral("exportDeliveryReviewSummaryCard"),
                  QStringLiteral("exportCurrentSplitButton")})) {
      return fail("Layout proof should capture ready Export");
    }
    auto failed_activity_job = make_persistent_job(
        QStringLiteral("layout-export-error-context"), QStringLiteral("asset_export"),
        QStringLiteral("Failed"));
    failed_activity_job.display_label = QStringLiteral("Layout error context");
    failed_activity_job.error_message = QStringLiteral("Synthetic layout proof failure.");
    photo_editor::desktop::JobQueueModelTestAccess::setHistoryFixture(
        job_queue, {failed_activity_job});
    job_queue.refresh();
    auto* export_review_failed = find_named_item(export_view, QStringLiteral("exportReviewFailedJobsButton"));
    if (!wait_for_condition([&]() {
          return !job_queue.loading() && job_queue.jobCount() == 1 &&
              export_view->property("deliveryActivityNeedsAttention").toBool() &&
              export_view->property("exportActivityContextOpen").toBool() &&
              export_view->property("exportActivityOpen").toBool() &&
              !export_view->property("exportActivityVisible").toBool() &&
              item_visible(export_queue) && item_visible(export_review_failed);
        }, 5000)) {
      return fail("A failed export should open Delivery activity without changing its idle preference");
    }
    if (!capture(QStringLiteral("export-error-auto-open"),
                 {QStringLiteral("exportQueuePanel"),
                  QStringLiteral("exportReviewFailedJobsButton")})) {
      return fail("Layout proof should capture the failed export context that opened Delivery activity");
    }
    photo_editor::desktop::JobQueueModelTestAccess::setHistoryFixture(job_queue, {});
    job_queue.refresh();
    if (!wait_for_condition([&]() {
          return !job_queue.loading() && job_queue.jobCount() == 0 &&
              !export_view->property("deliveryActivityNeedsAttention").toBool() &&
              !export_view->property("exportActivityContextOpen").toBool() &&
              !export_view->property("exportActivityOpen").toBool() &&
              !export_view->property("exportActivityVisible").toBool() &&
              !item_visible(export_queue);
        }, 5000)) {
      return fail("Delivery activity should collapse again after the synthetic failed job is removed");
    }
    if (!QMetaObject::invokeMethod(export_view, "handleExportLayoutMenu", Q_ARG(QVariant, QVariant(QStringLiteral("focus-settings")))) ||
        export_view->property("exportPreviewVisible").toBool() ||
        export_view->property("exportActivityVisible").toBool()) {
      return fail("Export settings layout should hide optional panels");
    }
    process_layout_events(8);
    if (!capture(QStringLiteral("export-settings"), {QStringLiteral("exportStudioWorkspace"),
                                                      QStringLiteral("exportStudioWorkspaceContent")})) {
      return fail("Layout proof should capture focused Export");
    }
    if (!QMetaObject::invokeMethod(export_view, "handleExportLayoutMenu", Q_ARG(QVariant, QVariant(QStringLiteral("show-main"))))) {
      return fail("Export review should restore its main panels");
    }
    settle_compact();
    if (!capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("export-compact"),
                              900, 700, {QStringLiteral("exportStudioWorkspace"),
                                         QStringLiteral("exportStudioWorkspaceContent"),
                                         QStringLiteral("exportWorkspaceLayoutButton"),
                                         QStringLiteral("exportDestinationNameLabel"),
                                         QStringLiteral("exportDestinationMoreMenuButton")}, &manifest)) {
      return fail("Layout proof should capture compact Export");
    }
    if (export_configuration->mapToItem(export_view_item, 0, 0).x() <=
        export_queue->mapToItem(export_view_item, 0, 0).x()) {
      return fail("Compact Export should retain settings to the right of preview and activity");
    }
    if (export_view->property("exportActivityVisible").toBool() !=
        original_export_activity_preference) {
      export_view->setProperty("exportActivityVisible", original_export_activity_preference);
      process_layout_events(3);
    }
    if (export_view->property("exportActivityVisible").toBool() !=
        original_export_activity_preference) {
      return fail("Export layout proof should restore its original Delivery activity preference");
    }
    if (!show_workspace("editWorkspaceIndex", QStringLiteral("editStudioWorkspace")) ||
        !QMetaObject::invokeMethod(edit, "handleWorkspaceLayoutMenu", Q_ARG(QVariant, QVariant(QStringLiteral("show-all"))))) {
      return fail("Layout proof should restore Edit");
    }
    settle_compact();
    if (!capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("edit-compact"),
                              900, 700, {QStringLiteral("editStudioWorkspace"),
                                         QStringLiteral("editViewMoreButton")}, &manifest)) {
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
                  edit->property("editInspectorTab").toString() == QStringLiteral("layers") &&
                  !edit->property("editToolTransitionActive").toBool();
            }))
          return fail("Adding an adjustment layer should retain its selected row in Layers");
        auto* edit_layer = find_named_item(edit, QStringLiteral("photoLayerEditButton"));
        if (!edit_layer || !item_visible(edit_layer) ||
            !QMetaObject::invokeMethod(edit_layer, "clicked") ||
            !wait_for_condition([&] {
              return edit->property("editInspectorTab").toString() == QStringLiteral("adjust") &&
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
        auto* properties_toggle = find_named_item(edit, QStringLiteral("photoLayerPropertiesDisclosureHeader"));
        if (!properties || !properties_toggle ||
            (!properties->property("detailsExpanded").toBool() &&
             !(properties_toggle->forceActiveFocus(Qt::TabFocusReason), send_window_key(visual_window, Qt::Key_Return))) ||
            !wait_for_condition([&] { return item_visible(opacity_caption) && item_visible(blend_caption); }))
          return fail("Layer Properties must disclose the editable opacity and blend controls");
        if (item_visible(properties_name))
          return fail("Expanded layer properties should replace the static name with one editable name");
        properties_name = find_named_item(edit, QStringLiteral("photoLayerNameFieldInput"));
        if (!wait_for_condition([&] {
              return layer_row && layer_name && properties && properties_name && opacity && blend &&
                  item_visible(opacity_caption) && item_visible(blend_caption) &&
                  scene_rect(layer_row).contains(scene_rect(layer_name)) &&
                  layer_name->property("contentWidth").toDouble() <= layer_name->width() + 1 &&
                  item_visible(properties_name) &&
                  properties_name->property("text").toString() == layer_name->property("text").toString() &&
                  properties_name->property("contentWidth").toDouble() <= properties_name->width() + 1 &&
                  scene_rect(properties).contains(scene_rect(properties_name)) &&
                  scene_rect(opacity).contains(scene_rect(opacity_caption)) &&
                  scene_rect(properties).contains(scene_rect(opacity)) &&
                  scene_rect(properties).contains(scene_rect(blend)) &&
                  !scene_rect(opacity).intersects(scene_rect(blend));
            })) {
          const auto bounds_json = [](QQuickItem* item) {
            const auto rect = scene_rect(item);
            return QJsonObject{{"name", item ? item->objectName() : QString{}},
                {"visible", item_visible(item)}, {"x", rect.x()}, {"y", rect.y()},
                {"width", rect.width()}, {"height", rect.height()},
                {"contentWidth", item ? item->property("contentWidth").toDouble() : 0}};
          };
          std::cerr << QJsonDocument(QJsonObject{{"layerRow", bounds_json(layer_row)},
              {"layerName", bounds_json(layer_name)}, {"properties", bounds_json(properties)},
              {"propertiesName", bounds_json(properties_name)}, {"opacity", bounds_json(opacity)},
              {"opacityCaption", bounds_json(opacity_caption)}, {"blend", bounds_json(blend)},
              {"blendCaption", bounds_json(blend_caption)}}).toJson().constData();
          return fail("Compact Layers should keep added layer identity, opacity, and blend controls readable");
        }
        auto* target_bar = find_named_item(edit, QStringLiteral("activeEditTargetBar"));
        auto* target_name = find_named_item(edit, QStringLiteral("activeEditTargetModeLabel"));
        auto* return_to_base = find_named_item(edit, QStringLiteral("activeEditTargetReturnToBaseButton"));
        if (!target_bar || !target_name || !return_to_base ||
            !scene_rect(target_bar).contains(scene_rect(target_name)) ||
            !scene_rect(target_bar).contains(scene_rect(return_to_base)) ||
            scene_rect(target_name).intersects(scene_rect(return_to_base)))
          return fail("Compact layer target mode must fit beside the Base action without overlapping it");
        if (!capture_visual_proof(visual_window, visual_root, proof_dir,
                                  QStringLiteral("edit-compact-layers-properties-expanded"), 900, 700,
                                  {QStringLiteral("photoLayerPropertiesPanel"),
                                   QStringLiteral("photoLayerNameFieldInput"),
                                   QStringLiteral("photoLayerPropertiesPrimaryControls"),
                                   QStringLiteral("photoLayerOpacitySliderCaption"),
                                   QStringLiteral("photoLayerBlendModeComboBox")},
                                  &manifest)) {
          return fail("Expanded Compact Layers properties should capture the readable opacity and blend controls");
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
    QVariant layout_items_value;
    if (!QMetaObject::invokeMethod(edit, "workspaceLayoutMenuItems",
                                   Q_RETURN_ARG(QVariant, layout_items_value))) {
      return fail("Edit should expose its panel and saved-layout choices");
    }
    bool layout_has_view_options = false;
    for (const auto& item_value : layout_items_value.toList()) {
      const auto key = item_value.toMap().value(QStringLiteral("key")).toString();
      if (key == QStringLiteral("snap-layers") || key == QStringLiteral("safe-margins") ||
          key.startsWith(QStringLiteral("density:")))
        layout_has_view_options = true;
    }
    auto* view_options_button = find_named_object(edit, QStringLiteral("editViewMoreButton"));
    if (!layout_menu_button || layout_has_view_options || !view_options_button ||
        !view_options_button->property("visible").toBool() ||
        !QMetaObject::invokeMethod(layout_menu_button, "openMenu") ||
        !capture_visual_proof(visual_window, visual_root, proof_dir,
          QStringLiteral("edit-panel-layout-menu"), 900, 700,
          {QStringLiteral("editViewMoreButtonPopup")}, &manifest)) {
      return fail("The View menu should expose panel layouts alongside snap and safe guides");
    }

    const auto density_before_view_options = edit->property("compactMode").toBool();
    const auto toggle_view_option = [&](const QString& key, const QString& state_property) {
      if (!QMetaObject::invokeMethod(view_options_button, "openMenu")) return false;
      QObject* popup = find_named_object(edit, QStringLiteral("editViewMoreButtonPopup"));
      if (!popup || !wait_for_condition([&] {
            return popup->property("opened").toBool();
          }, 3000)) return false;
      QObject* row = nullptr;
      for (int index = 0; index < 32; ++index) {
        auto* candidate = find_named_object(edit,
            QStringLiteral("editViewMoreButtonPopupItem%1").arg(index));
        if (candidate && candidate->property("itemKey").toString() == key) {
          row = candidate;
          break;
        }
      }
      if (!row) return false;
      const bool before = edit->property(state_property.toUtf8().constData()).toBool();
      if (!QMetaObject::invokeMethod(row, "clicked")) return false;
      return wait_for_condition([&] {
        return edit->property(state_property.toUtf8().constData()).toBool() != before &&
               !popup->property("opened").toBool() &&
               edit->property("compactMode").toBool() == density_before_view_options;
      }, 5000);
    };
    if (!toggle_view_option(QStringLiteral("snap-layers"), QStringLiteral("layerSnappingEnabled")) ||
        !toggle_view_option(QStringLiteral("safe-margins"), QStringLiteral("layerSafeMarginsEnabled"))) {
      return fail("View options should toggle snap and safe guides without changing workspace density");
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
    // Canvas creation leaves Library on Canvases; the following assertions
    // exercise the photo toolbar and therefore need the Photos collection.
    library->setProperty("libraryCollection", QStringLiteral("images"));
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
                  QStringLiteral("editViewMoreButton")})) {
      return fail("Layout proof should capture the restored Edit layout after reload");
    }
    // Submit the simplified creation form through its real action, retaining
    // the hidden Advanced values rather than silently replacing them.
    visual_root->setProperty("currentWorkspace", visual_root->property("timelineWorkspaceIndex"));
    if (!wait_for_condition([&] {
          return item_visible(find_named_item(visual_root, QStringLiteral("timelineSetupSurface"))) &&
                 !visual_root->property("workspaceTransitionActive").toBool();
        }, 15000))
      return fail("Timeline creation should remain reachable after reopening the shell");
    auto* final_setup = find_named_object(visual_root, QStringLiteral("timelineWorkspace"));
    auto* final_create = find_named_item(visual_root, QStringLiteral("timelineCreateButton"));
    if (!final_setup || !final_create)
      return fail("Timeline creation requires its live form and action");
    final_setup->setProperty("setupTimelineName", QString{});
    process_layout_events(3);
    if (final_create->isEnabled())
      return fail("Timeline Create must require a nonblank document name");
    final_setup->setProperty("setupTimelineName", QStringLiteral("Page polish timeline"));
    final_setup->setProperty("setupResolutionPreset", QStringLiteral("1920 x 1080"));
    QMetaObject::invokeMethod(final_setup, "applyResolutionPreset");
    final_setup->setProperty("setupFrameRate", QStringLiteral("24"));
    final_setup->setProperty("setupProjectBinding", QStringLiteral("Proof project"));
    final_setup->setProperty("setupAdvancedExpanded", false);
    process_layout_events(3);
    if (!final_create->isEnabled() || !QMetaObject::invokeMethod(final_create, "clicked") ||
        !wait_for_condition([&] { return timeline_document.hasActiveTimeline(); }, 15000) ||
        timeline_document.timelineName() != QStringLiteral("Page polish timeline") ||
        timeline_document.sequenceWidth() != 1920 || timeline_document.sequenceHeight() != 1080 ||
        timeline_document.framesPerSecond() != 24 ||
        timeline_document.projectBinding() != QStringLiteral("Proof project"))
      return fail("Timeline Create should preserve the named format and hidden Advanced choices");
    if (!timeline_document.saveTimeline() || !wait_for_condition([&] {
          return !timeline_document.timelineSaveInProgress() &&
                 !timeline_document.savedDocumentPath().isEmpty() &&
                 QFileInfo::exists(timeline_document.savedDocumentPath());
        }, 10000))
      return fail("The timeline created from the compact form must save in the isolated catalog");
    const auto created_timeline_path = timeline_document.savedDocumentPath();
    timeline_document.closeTimeline();
    if (!timeline_document.loadTimeline(created_timeline_path) ||
        timeline_document.timelineName() != QStringLiteral("Page polish timeline") ||
        timeline_document.sequenceWidth() != 1920 || timeline_document.sequenceHeight() != 1080 ||
        timeline_document.framesPerSecond() != 24 ||
        timeline_document.projectBinding() != QStringLiteral("Proof project"))
      return fail("The created timeline must reopen with its basic and Advanced settings intact");
    QFile manifest_file(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
    if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        manifest_file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("Layout proof should persist its manifest");
    }
    return EXIT_SUCCESS;

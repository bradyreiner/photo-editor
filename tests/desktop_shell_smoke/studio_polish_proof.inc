    const auto surface_only = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_SURFACE_ONLY");
    const auto roto_only = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_ROTO_ONLY");
    const auto scene_only = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_SCENE_ONLY") || surface_only || roto_only;
    const auto photo_path = runtime_root.filePath(QStringLiteral("studio-photo.png"));
    const auto video_path = runtime_root.filePath(QStringLiteral("studio-video.mp4"));
    if (!scene_only) {
      if (!write_overlay_visual_fixture(photo_path, QStringLiteral("saturated"))) {
        return fail("Studio polish should create its isolated photo fixture");
      }
      const auto photographic_source_path = qEnvironmentVariable("PHOTO_EDITOR_STUDIO_PHOTO_SOURCE");
      if (!photographic_source_path.isEmpty()) {
        const QImage photographic_source(photographic_source_path);
        if (photographic_source.isNull() || !photographic_source.save(photo_path))
          return fail("Studio interactions should load a real photograph into their isolated fixture");
      }
      if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_BLANK_CANVAS")) {
        // Register the initial photo too: a synthetic asset ID cannot satisfy
        // the catalog foreign key when eraser edits are saved.
        const auto fixture_manifest_path = runtime_root.filePath(QStringLiteral("canvas-photo-fixture.txt"));
        QFile fixture_manifest(fixture_manifest_path);
        if (!fixture_manifest.open(QIODevice::WriteOnly) || fixture_manifest.write(photo_path.toUtf8() + '\n') <= 0)
          return fail("Write the isolated canvas photo import manifest");
        fixture_manifest.close();
        QProcess fixture_import;
        fixture_import.start(photo_editor::desktop::AppBootstrap::helperProgram(QStringLiteral("import_catalog")),
            photo_editor::desktop::AppBootstrap::helperArguments(QStringLiteral("import_catalog"),
                {runtime_root.path(), QStringLiteral("canvas-fixture"), QStringLiteral("Canvas proof"),
                 QStringLiteral("referenced"), fixture_manifest_path}));
        if (!fixture_import.waitForFinished(15000) || fixture_import.exitCode() != 0) {
          std::cerr << fixture_import.readAllStandardError().toStdString() << '\n';
          return fail("Register the initial canvas proof photo in the disposable catalog");
        }
        catalog_assets.refresh();
        if (!wait_for_condition([&] { return !catalog_assets.loading() && !catalog_assets.assetIdForFilePath(photo_path).isEmpty(); }, 15000))
          return fail("Load the registered canvas photo fixture");
        catalog_assets.selectAssetById(catalog_assets.assetIdForFilePath(photo_path));
    } else {
      catalog_assets.replaceAssetsForTesting({make_asset(QStringLiteral("studio-photo"), photo_path)});
      catalog_assets.selectIndex(0);
    }
    QProcess fixture_encoder;
    QStringList fixture_arguments{QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error")};
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_TIMELINE_WORKFLOW")) {
      fixture_arguments.append({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                QStringLiteral("testsrc2=size=640x480:rate=30")});
    } else {
      fixture_arguments.append({QStringLiteral("-loop"), QStringLiteral("1"), QStringLiteral("-i"), photo_path});
    }
    fixture_arguments.append({
         QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         // Silent AAC retains linked-audio/media paths without audible UI-test tones.
         QStringLiteral("anullsrc=channel_layout=stereo:sample_rate=48000"),
         QStringLiteral("-t"), QStringLiteral("2"), QStringLiteral("-vf"), QStringLiteral("scale=640:480"),
         QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
         QStringLiteral("-c:a"), QStringLiteral("aac"), video_path});
    fixture_encoder.start(QStringLiteral("ffmpeg"), fixture_arguments);
    if (!fixture_encoder.waitForFinished(30000) || fixture_encoder.exitCode() != 0) {
      std::cerr << fixture_encoder.readAllStandardError().toStdString() << '\n';
      return fail("Studio polish should generate real video and audio for its Timeline fixture");
    }
    timeline_document.createTimeline(QStringLiteral("Studio layout review"));
    timeline_document.addClipFromGallery(
        QStringLiteral("studio-shot"), video_path, QStringLiteral("Opening shot"), 2.0,
        QUrl::fromLocalFile(photo_path).toString());
    timeline_document.selectClip(0);
    if (!timeline_document.saveTimeline() || !wait_for_condition([&]() {
          return !timeline_document.timelineSaveInProgress() &&
                 !timeline_document.savedDocumentPath().isEmpty() &&
                 QFileInfo::exists(timeline_document.savedDocumentPath());
        }, 10000)) {
      return fail("Studio polish should save its isolated Timeline fixture");
    }
    const auto studio_document_path = timeline_document.savedDocumentPath();
    timeline_document.closeTimeline();
    if (!timeline_document.loadTimeline(studio_document_path)) {
      return fail("Studio polish should reopen its isolated Timeline fixture");
    }
    timeline_document.selectClip(0);
    } else {
      const auto scene_fixture_path = roto_only
          ? qEnvironmentVariable("PHOTO_EDITOR_ROTO_TIMELINE_FIXTURE")
          : surface_only ? qEnvironmentVariable("PHOTO_EDITOR_SURFACE_TIMELINE_FIXTURE")
          : qEnvironmentVariable("PHOTO_EDITOR_SCENE_TIMELINE_FIXTURE");
      const auto fixture_file = QFileInfo(scene_fixture_path).absoluteFilePath();
      if (scene_fixture_path.isEmpty() || !QFileInfo::exists(fixture_file))
        return roto_only ? fail("PHOTO_EDITOR_SMOKE_ROTO_ONLY requires an editable PHOTO_EDITOR_ROTO_TIMELINE_FIXTURE") : surface_only
            ? fail("PHOTO_EDITOR_SMOKE_SURFACE_ONLY requires an editable PHOTO_EDITOR_SURFACE_TIMELINE_FIXTURE")
            : fail("PHOTO_EDITOR_SMOKE_SCENE_ONLY requires an editable solved PHOTO_EDITOR_SCENE_TIMELINE_FIXTURE");
      if (!timeline_document.loadTimeline(fixture_file) ||
          !wait_for_condition([&] { return timeline_document.hasActiveTimeline(); }, 10000))
        return fail(surface_only
            ? "Surface-only proof should load its analyzed Timeline before the editor route initializes"
            : "Scene-only proof should load its solved Timeline before the editor route initializes");
      // Host pressure may deliberately defer hydration. This proof qualifies
      // editing behavior under normal resources, rather than pressure recovery.
      timeline_document.setCachePressureLevelForTesting(QStringLiteral("normal"));
    }
    engine.load(QUrl::fromLocalFile(main_qml_path));
    if (engine.rootObjects().isEmpty()) {
      return fail("Studio polish should load the application QML");
    }
    auto* studio_root = engine.rootObjects().constFirst();
    auto* studio_window = qobject_cast<QQuickWindow*>(studio_root);
    if (!studio_window) {
      return fail("Studio polish requires a rendered window");
    }
    const auto proof_dir = qEnvironmentVariableIsSet("PHOTO_EDITOR_VISUAL_PROOF_DIR")
        ? QString::fromUtf8(qgetenv("PHOTO_EDITOR_VISUAL_PROOF_DIR"))
        : runtime_root.filePath(QStringLiteral("studio-polish-proof"));
    if (!QDir().mkpath(proof_dir)) {
      return fail("Studio polish should create its capture directory");
    }
    QJsonArray studio_manifest;
    QJsonArray creative_interaction_metrics;
    const auto show_studio_workspace = [&](const char* index_property, const QString& object_name) {
      studio_root->setProperty("currentWorkspace", studio_root->property(index_property));
      return wait_for_condition([&]() {
        auto* workspace = find_named_item(studio_root, object_name);
        auto* timeline_loading = find_named_object(studio_root, QStringLiteral("timelineEditorLoadingSurface"));
        auto* shell_loading = find_named_object(studio_root, QStringLiteral("timelineWorkspaceLoadingSurface"));
        return !studio_root->property("workspaceTransitionActive").toBool() &&
               workspace && item_visible(workspace) &&
               (object_name != QStringLiteral("editStudioWorkspace") ||
                workspace->property("pageContentReady").toBool()) &&
               (object_name != QStringLiteral("timelineWorkspace") ||
                (workspace->property("workspacePanelSettingsInitialized").toBool() &&
                 timeline_loading && !timeline_loading->property("visible").toBool() &&
                 shell_loading && !shell_loading->property("visible").toBool() &&
                 item_visible(find_named_item(workspace, QStringLiteral("timelineViewport")))));
      }, 25000);
    };
    if (scene_only) {
#include "timeline_workflow_proof.inc"
      return EXIT_SUCCESS;
    }
    if (!show_studio_workspace("editWorkspaceIndex", QStringLiteral("editStudioWorkspace"))) {
      return fail("Studio polish should reach the Edit workspace");
    }
    auto* edit = find_named_object(studio_root, QStringLiteral("editStudioWorkspace"));
    if (!edit || !wait_for_condition([&] {
          return editor_preview.hasAsset() && !editor_preview.processing();
        }, 15000)) {
      return fail("Studio polish should load its photo");
    }
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_TIMELINE_WORKFLOW")) {
#include "timeline_workflow_proof.inc"
    }
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_OUTLINE_REVIEW")) {
#include "mask_outline_visual_proof.inc"
    }
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_BLANK_CANVAS")) {
#include "blank_canvas_interactions.inc"
    }
    const QStringList densities{QStringLiteral("comfortable"), QStringLiteral("balanced"),
                                QStringLiteral("compact")};
    const auto native_proof = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER");
    const QList<QSize> studio_sizes{QSize(1180, 768), QSize(1440, 900),
                                  native_proof ? QSize(1920, 900) : QSize(1920, 1080)};
    for (const auto& density : densities) {
      const auto context_before = edit->property("editContextPanelVisible");
      const auto inspector_before = edit->property("editInspectorPanelVisible");
      const auto filmstrip_before = edit->property("editFilmstripCollapsed");
      if (!QMetaObject::invokeMethod(edit, "setInterfaceDensity",
                                    Q_ARG(QVariant, QVariant(density)))) {
        return fail("Edit should expose its density selection");
      }
      process_layout_events(8);
      if (app_shell.interfaceDensity() != density ||
          edit->property("interfaceDensity").toString() != density ||
          edit->property("editContextPanelVisible") != context_before ||
          edit->property("editInspectorPanelVisible") != inspector_before ||
          edit->property("editFilmstripCollapsed") != filmstrip_before) {
        return fail("Density selection should persist globally while preserving Edit panels");
      }
      for (const auto& size : studio_sizes) {
        if (!capture_visual_proof(studio_window, studio_root, proof_dir,
              QStringLiteral("edit-%1-%2").arg(density).arg(size.width()),
              size.width(), size.height(),
              {QStringLiteral("editImageViewport"), QStringLiteral("editWorkspaceLayoutButton"),
               QStringLiteral("activeEditTargetModeLabel")}, &studio_manifest)) {
          return fail("Edit density should render its canvas, layout control, and editing target");
        }
        auto* target = find_named_object(studio_root, QStringLiteral("activeEditTargetModeLabel"));
        if (!target || target->property("text").toString() != QStringLiteral("Editing: Entire image")) {
          return fail("Edit should identify the adjustment target in plain language");
        }
        auto* viewport = find_named_item(studio_root, QStringLiteral("editImageViewport"));
        const QRectF window_bounds(0, 0, studio_window->width(), studio_window->height());
        if (!viewport || viewport->width() < 350 || viewport->height() < 250 ||
            !window_bounds.contains(scene_rect(viewport))) {
          return fail("Edit density should retain a usable canvas inside the window");
        }
      }
      auto* color_intent = find_named_object(studio_root, QStringLiteral("adjustmentIntentColorButton"));
      if (!color_intent || !QMetaObject::invokeMethod(color_intent, "clicked") ||
          edit->property("activeAdjustmentIntent").toString() != QStringLiteral("color")) {
        return fail("Edit should select its Color focus through the visible control");
      }
      process_layout_events(8);
      auto* light_group = find_named_object(studio_root, QStringLiteral("lightInspectorGroup"));
      auto* white_balance = find_named_object(studio_root, QStringLiteral("whiteBalanceInspectorGroup"));
      if (!light_group || light_group->property("visible").toBool() || !white_balance ||
          !white_balance->property("visible").toBool() ||
          !capture_visual_proof(studio_window, studio_root, proof_dir,
              QStringLiteral("edit-color-%1").arg(density), 1440, 900,
              {QStringLiteral("adjustmentIntentNavigation"), QStringLiteral("whiteBalanceInspectorGroup")},
              &studio_manifest)) {
        return fail("Color focus should reveal existing color controls and hide unrelated Light controls");
      }
      edit->setProperty("editAdjustmentFilterText", QStringLiteral("temperature"));
      process_layout_events(4);
      auto* color_group = find_named_object(studio_root, QStringLiteral("colorInspectorGroup"));
      if (!white_balance->property("visible").toBool() || !color_group ||
          color_group->property("visible").toBool()) {
        return fail("Adjustment search should compose with Color focus");
      }
      edit->setProperty("editAdjustmentFilterText", QString());
      auto* all_intent = find_named_object(studio_root, QStringLiteral("adjustmentIntentAllButton"));
      if (!all_intent || !QMetaObject::invokeMethod(all_intent, "clicked")) {
        return fail("Edit should return to all adjustment tools");
      }
      QVariant lens_matches;
      if (!QMetaObject::invokeMethod(edit, "adjustmentGroupMatches", Q_RETURN_ARG(QVariant, lens_matches),
            Q_ARG(QVariant, QVariant(QStringLiteral("lens")))) || !lens_matches.toBool()) {
        return fail("All adjustments should retain Lens controls");
      }
    }

    // Exercise Paint against an isolated native fixture layer. This keeps the
    // real photo untouched while proving that tool entry follows the selected
    // pixel target rather than leaving the base image active.
    const int paint_layer_count_before = editor_preview.photoLayerCount();
    if (!editor_preview.addPaintLayer() ||
        editor_preview.photoLayerCount() != paint_layer_count_before + 1) {
      return fail("Studio polish should add one isolated pixel paint layer");
    }
    QVariant paint_layer_selected;
    if (!QMetaObject::invokeMethod(edit, "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, paint_layer_selected),
          Q_ARG(QVariant, QVariant(0)), Q_ARG(QVariant, QVariant(false))) ||
        !paint_layer_selected.toBool()) {
      return fail("Studio polish should select the isolated pixel paint layer");
    }
    const auto paint_layer_data = editor_preview.photoLayers().value(0).toMap();
    const auto paint_layer_id = paint_layer_data.value(QStringLiteral("id")).toString();
    if (paint_layer_id.isEmpty() ||
        paint_layer_data.value(QStringLiteral("kind")).toString() != QStringLiteral("pixel")) {
      return fail("Studio polish should create a real pixel layer in its fixture stack");
    }

    quint64 paint_pointer_timestamp = 1000;
    const auto send_pointer_to_window = [&](QQuickWindow* target_window, QEvent::Type type, const QPointF& point) {
      const auto global = QPointF(target_window->mapToGlobal(point.toPoint()));
      QMouseEvent event(type, point, global,
                        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                        Qt::NoModifier);
      event.setTimestamp(paint_pointer_timestamp);
      paint_pointer_timestamp += 16;
      QCoreApplication::sendEvent(target_window, &event);
      process_layout_events(2);
    };
    const auto send_paint_pointer = [&](QEvent::Type type, const QPointF& point) {
      send_pointer_to_window(studio_window, type, point);
    };
    const auto click_studio_item = [&](QQuickItem* item) {
      auto* target_window = item ? item->window() : nullptr;
      if (!item_visible(item) || !target_window || !item->property("enabled").toBool()) {
        return false;
      }
      // Dock/float reparenting publishes window ownership before the next
      // layout polish. Read a rendered frame before targeting its new bounds.
      target_window->grabWindow();
      paint_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
      const auto point = scene_rect(item).center();
      send_pointer_to_window(target_window, QEvent::MouseButtonPress, point);
      if (item->objectName() == QStringLiteral("maskCreationDoneButton")) {
        auto* grabber = target_window->mouseGrabberItem();
        std::cerr << "Selection Done press: pressed=" << item->property("pressed").toBool()
                  << " grabber=" << (grabber ? grabber->objectName().toStdString() : "none") << '\n';
      }
      send_pointer_to_window(target_window, QEvent::MouseButtonRelease, point);
      return true;
    };
    const auto invoke_edit_tool = [&](const QString& tool_name) {
      if (!QMetaObject::invokeMethod(edit, "activateEditTool",
                                    Q_ARG(QVariant, QVariant(tool_name))) ||
          !wait_for_condition([&] { return !edit->property("editToolTransitionActive").toBool(); }, 5000))
        return false;
      // The transition overlay intentionally intercepts input until the tool
      // is ready. Pointer checks must begin after that real readiness signal.
      process_layout_events(8);
      return true;
    };
    auto* tool_viewport = find_named_item(studio_root, QStringLiteral("editImageViewport"));
    auto* paint_overlay = find_named_item(studio_root, QStringLiteral("photoPaintOverlay"));
    auto* paint_options = find_named_item(studio_root, QStringLiteral("photoPaintToolOptions"));
    auto* paint_controls = find_named_item(studio_root, QStringLiteral("photoPaintControls"));
    auto* creative_tools_panel = find_named_item(studio_root, QStringLiteral("creativeToolsPanel"));
    auto* creative_tools_surface = find_named_item(studio_root, QStringLiteral("creativeToolsSurface"));
    auto* creative_tools_content = find_named_item(studio_root, QStringLiteral("creativeToolsContent"));
    auto* creative_paint_host = find_named_item(studio_root, QStringLiteral("creativePaintControlsHost"));
    auto* creative_tools_close = find_named_item(studio_root, QStringLiteral("creativeToolsCloseButton"));
    auto* creative_tools_float = find_named_item(studio_root, QStringLiteral("creativeToolsFloatButton"));
    auto* creative_tools_window_object = find_named_object(studio_root, QStringLiteral("creativeToolsWindow"));
    auto* paint_mode = find_named_item(studio_root, QStringLiteral("photoPaintMode"));
    auto* erase_mode = find_named_item(studio_root, QStringLiteral("photoPaintEraseMode"));
    auto* paint_size = find_named_item(studio_root, QStringLiteral("photoPaintSize"));
    auto* paint_color = find_named_item(studio_root, QStringLiteral("photoPaintColorButton"));
    auto* paint_advanced = find_named_item(studio_root, QStringLiteral("photoPaintAdvancedButton"));
    auto* paint_rail_button = find_named_item(studio_root, QStringLiteral("editRailPaintButton"));
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_TYPOGRAPHY_ONLY")) {
#include "typography_inspector_proof.inc"
    }
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_TOOL_SIDEBARS")) {
#include "creative_tool_sidebar_interactions.inc"
#include "creative_tool_shape_creation_interactions.inc"
      QFile manifest_file(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
      QFile metrics_file(QDir(proof_dir).filePath(QStringLiteral("interaction-metrics.json")));
      if (!manifest_file.open(QIODevice::WriteOnly) ||
          manifest_file.write(QJsonDocument(studio_manifest).toJson(QJsonDocument::Indented)) <= 0 ||
          !metrics_file.open(QIODevice::WriteOnly) ||
          metrics_file.write(QJsonDocument(creative_interaction_metrics).toJson(QJsonDocument::Indented)) <= 0)
        return fail("Tool sidebar proof should save native screenshots and interaction gates");
      return EXIT_SUCCESS;
    }
    if (!tool_viewport || !paint_overlay || !paint_options || !paint_controls || !creative_tools_panel ||
        !creative_tools_surface || !creative_tools_content ||
        !creative_paint_host || !creative_tools_close || !creative_tools_float ||
        !creative_tools_window_object || !paint_mode || !erase_mode ||
        !paint_size || !paint_color || !paint_advanced || !paint_rail_button) {
      return fail("Studio polish should expose named Paint HUD controls for native proof");
    }
    auto* base_image_layer_row = find_named_item(studio_root, QStringLiteral("baseImageLayerRow"));
    auto* layer_properties = find_named_item(studio_root, QStringLiteral("photoLayerPropertiesPanel"));
    if (!base_image_layer_row || !layer_properties ||
        !QMetaObject::invokeMethod(edit, "activateBaseImageLayerTarget") ||
        !wait_for_condition([&] {
          return edit->property("selectedEditTarget").toMap()
                     .value(QStringLiteral("scope")).toString() == QStringLiteral("base-image");
        }) ||
        !base_image_layer_row->property("baseLayerSelected").toBool() ||
        layer_properties->property("visible").toBool()) {
      return fail("Base targeting should clear stale pixel-layer properties before Paint entry");
    }
    if (!invoke_edit_tool(QStringLiteral("paint")) ||
        !wait_for_condition([&] {
          return edit->property("editTool").toString() == QStringLiteral("paint") &&
                 paint_overlay->property("active").toBool() &&
                 paint_options->isVisible() && layer_properties->isVisible() &&
                 edit->property("creativeToolsActive").toBool() &&
                 edit->property("editCreativeToolsVisible").toBool() &&
                 !edit->property("editCreativeToolsFloating").toBool() &&
                 creative_tools_panel->isVisible();
        }, 5000)) {
      return fail("Paint entry should reveal its HUD and the existing pixel-layer properties");
    }
    const auto selected_target = edit->property("selectedEditTarget").toMap();
    if (base_image_layer_row->property("baseLayerSelected").toBool() ||
        selected_target.value(QStringLiteral("scope")).toString() == QStringLiteral("base-image") ||
        selected_target.value(QStringLiteral("layerId")).toString() != paint_layer_id ||
        paint_overlay->property("targetId").toString() != paint_layer_id ||
        editor_preview.photoLayerCount() != paint_layer_count_before + 1 ||
        layer_properties->property("layerData").toMap().value(QStringLiteral("kind")).toString() !=
            QStringLiteral("pixel")) {
      return fail("Paint entry should select the existing pixel layer without adding another layer");
    }

    if (!QMetaObject::invokeMethod(edit, "activateBaseImageLayerTarget") ||
        !wait_for_condition([&] {
          return base_image_layer_row->property("baseLayerSelected").toBool() &&
                 !paint_overlay->property("editable").toBool() &&
                 paint_overlay->property("targetId").toString().isEmpty() &&
                 paint_overlay->property("layerIndex").toInt() == -1 &&
                 !layer_properties->property("visible").toBool();
        }) || !invoke_edit_tool(QStringLiteral("paint")) ||
        !wait_for_condition([&] {
          return paint_overlay->property("editable").toBool() &&
                 paint_overlay->property("targetId").toString() == paint_layer_id &&
                 !base_image_layer_row->property("baseLayerSelected").toBool();
        })) {
      return fail("Selecting Base Image during Paint should disable painting until a pixel target is selected");
    }

    const QStringList paint_control_names{
        QStringLiteral("photoPaintNewLayerButton"), QStringLiteral("photoPaintMode"),
        QStringLiteral("photoPaintEraseMode"), QStringLiteral("photoPaintEraseSelection"),
        QStringLiteral("photoPaintColorButton"),
        QStringLiteral("photoPaintSize"), QStringLiteral("photoPaintHardness"),
        QStringLiteral("photoPaintOpacity"), QStringLiteral("photoPaintFlow"),
        QStringLiteral("photoPaintAdvancedButton")};
    // Reactivating a target schedules the HUD's Flow/Layout polish. Wait for
    // that geometry before computing coordinates for a real pointer drag.
    process_layout_events(8);
    const auto initial_brush_size = paint_overlay->property("brushSize").toDouble();
    const auto size_bounds = scene_rect(paint_size);
    if (!item_visible(creative_tools_panel) || !item_visible(creative_paint_host) ||
        !item_visible(paint_options) || !item_visible(paint_controls) ||
        creative_tools_surface->window() != studio_window ||
        !visible_named_items_fit_parent(studio_root, QStringLiteral("creativePaintControlsHost"),
                                        paint_control_names) ||
        !item_visible(paint_size) || size_bounds.width() < 18.0 || size_bounds.height() < 24.0) {
      return fail("Paint HUD controls should remain visible with usable bounds inside their surface");
    }
    const auto size_start = size_bounds.center();
    const auto size_end = QPointF(size_bounds.right() - 2.0, size_start.y());
    send_paint_pointer(QEvent::MouseButtonPress, size_start);
    for (int step = 1; step <= 5; ++step) {
      send_paint_pointer(QEvent::MouseMove,
                         size_start + (size_end - size_start) * (static_cast<double>(step) / 5.0));
    }
    send_paint_pointer(QEvent::MouseButtonRelease, size_end);
    if (!wait_for_condition([&] {
          return std::abs(paint_overlay->property("brushSize").toDouble() - initial_brush_size) > 1.0;
        }, 3000)) {
      const auto actual_bounds = scene_rect(paint_size);
      std::cerr << "Paint size drag: initial=" << initial_brush_size
                << " actual=" << paint_overlay->property("brushSize").toDouble()
                << " enabled=" << paint_size->property("enabled").toBool()
                << " from=" << size_bounds.x() << ',' << size_bounds.y()
                << ',' << size_bounds.width() << ',' << size_bounds.height()
                << " current=" << actual_bounds.x() << ',' << actual_bounds.y()
                << ',' << actual_bounds.width() << ',' << actual_bounds.height() << '\n';
      studio_window->grabWindow().save(QDir(proof_dir).filePath(QStringLiteral("paint-size-failure.png")));
      return fail("The visible Paint size control should change brush size without painting pixels");
    }
    paint_overlay->setProperty("brushSize", initial_brush_size);
    if (!click_studio_item(erase_mode) || !paint_overlay->property("erase").toBool() ||
        paint_color->property("enabled").toBool() || !click_studio_item(paint_mode) ||
        paint_overlay->property("erase").toBool() || !paint_color->property("enabled").toBool()) {
      return fail("Paint and erase modes should switch through the visible HUD control");
    }

    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_ERASER_SHAPE_FILLS")) {
#include "creative_eraser_shape_fill_interactions.inc"
    }

#include "creative_tool_paint_interactions.inc"

    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_PAINT_INTERACTIONS")) {
      QFile manifest_file(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
      QFile metrics_file(QDir(proof_dir).filePath(QStringLiteral("interaction-metrics.json")));
      if (!manifest_file.open(QIODevice::WriteOnly) ||
          manifest_file.write(QJsonDocument(studio_manifest).toJson(QJsonDocument::Indented)) <= 0 ||
          !metrics_file.open(QIODevice::WriteOnly) ||
          metrics_file.write(QJsonDocument(creative_interaction_metrics).toJson(QJsonDocument::Indented)) <= 0)
        return fail("Paint interactions should save their native evidence");
      return EXIT_SUCCESS;
    }

    for (const auto& density : densities) {
      if (!QMetaObject::invokeMethod(edit, "setInterfaceDensity",
                                     Q_ARG(QVariant, QVariant(density)))) {
        return fail("Paint proof should set each interface density");
      }
      if (!invoke_edit_tool(QStringLiteral("paint")) ||
          !wait_for_condition([&] {
            return edit->property("editTool").toString() == QStringLiteral("paint") &&
                   paint_overlay->property("active").toBool() && paint_options->isVisible();
          }, 5000)) {
        return fail("Each density should reopen Paint before checking its HUD geometry");
      }
      process_layout_events(8);
      for (const auto& size : {QSize(1180, 768), QSize(1440, 900)}) {
        studio_window->resize(size);
        process_layout_events(8);
        auto* viewport = find_named_item(studio_root, QStringLiteral("editImageViewport"));
        auto* edit_inspector = find_named_item(studio_root, QStringLiteral("editInspectorPanel"));
        const QRectF window_bounds(0, 0, studio_window->width(), studio_window->height());
        const auto tools_bounds = scene_rect(creative_tools_panel);
        if (!item_visible(viewport) || !item_visible(paint_overlay) || !item_visible(paint_options) ||
            !item_visible(creative_tools_panel) || !item_visible(creative_paint_host) ||
            creative_tools_surface->window() != studio_window ||
            !window_bounds.contains(tools_bounds) ||
            !tools_bounds.contains(scene_rect(creative_paint_host)) ||
            !scene_rect(creative_paint_host).contains(scene_rect(paint_controls)) ||
            (item_visible(edit_inspector) && tools_bounds.bottom() > scene_rect(edit_inspector).top() + 2.0) ||
            overlaps_with_tolerance(tools_bounds, scene_rect(viewport)) ||
            !capture_visual_proof(studio_window, studio_root, proof_dir,
                QStringLiteral("paint-%1-%2").arg(density).arg(size.width()),
                size.width(), size.height(),
                {QStringLiteral("editImageViewport"), QStringLiteral("creativeToolsPanel"),
                 QStringLiteral("creativePaintControlsHost"), QStringLiteral("photoPaintControls"),
                 QStringLiteral("photoPaintOverlay"), QStringLiteral("photoPaintMode"),
                 QStringLiteral("photoPaintSize")}, &studio_manifest)) {
          return fail("Paint controls should fit in the docked Creative Tools panel above the normal inspector");
        }
      }

      if (!click_studio_item(paint_advanced) ||
          !wait_for_condition([&] {
            return named_object_visible_for_proof(studio_root, QStringLiteral("photoPaintAdvancedControls"));
          }) ||
          !capture_visual_proof(studio_window, studio_root, proof_dir,
              QStringLiteral("paint-advanced-%1").arg(density),
              studio_window->width(), studio_window->height(),
              {QStringLiteral("creativeToolsPanel"), QStringLiteral("creativeToolsSurface"),
               QStringLiteral("creativePaintControlsHost"),
               QStringLiteral("photoPaintToolOptions"), QStringLiteral("photoPaintAdvancedControls"),
               QStringLiteral("photoPaintMaterialChooser"), QStringLiteral("photoPaintSpacing")},
              &studio_manifest) || !click_studio_item(paint_advanced)) {
        return fail("Paint advanced controls should expand separately and return to the compact bar");
      }

      if (!invoke_edit_tool(QStringLiteral("layers")) ||
          !wait_for_condition([&] {
            return edit->property("editTool").toString() == QStringLiteral("layers") &&
                   layer_properties->property("visible").toBool() &&
                   layer_properties->property("layerSelected").toBool();
          }, 5000)) {
        return fail("The layer stack should reveal its selected pixel-layer properties");
      }
      auto* details_button = find_named_item(studio_root, QStringLiteral("photoLayerPropertiesDetailsButton"));
      auto* lock_button = find_named_item(studio_root, QStringLiteral("photoLayerLockButton"));
      auto* clipping_button = find_named_item(studio_root, QStringLiteral("photoLayerClippingMaskButton"));
      auto* inspector_scroll = find_named_item(studio_root, QStringLiteral("editInspectorScroll"));
      if (!details_button || !lock_button || !clipping_button || !inspector_scroll ||
          !layer_properties->property("layerSelected").toBool() ||
          layer_properties->property("layerData").toMap().value(QStringLiteral("kind")).toString() !=
              QStringLiteral("pixel") || !click_studio_item(details_button) ||
          !wait_for_condition([&] {
            return layer_properties->property("detailsExpanded").toBool() && item_visible(clipping_button) && item_visible(lock_button);
          })) {
        return fail("Selected pixel-layer details should expose clipping while retaining common actions");
      }

      QObject* inspector_content_object = inspector_scroll->property("contentItem").value<QObject*>();
      auto* inspector_content = qobject_cast<QQuickItem*>(inspector_content_object);
      if (!inspector_content) {
        return fail("The Edit inspector should expose its native scroll content for layer proof");
      }
      for (const auto& size : {QSize(1180, 768), QSize(1440, 900)}) {
        studio_window->resize(size);
        process_layout_events(8);
        auto inspector_view = scene_rect(inspector_scroll).adjusted(2.0, 2.0, -2.0, -2.0);
        auto lock_bounds = scene_rect(lock_button);
        const auto current_content_y = inspector_content->property("contentY").toDouble();
        double scroll_delta = 0.0;
        if (lock_bounds.bottom() > inspector_view.bottom())
          scroll_delta = lock_bounds.bottom() - inspector_view.bottom() + 8.0;
        else if (lock_bounds.top() < inspector_view.top())
          scroll_delta = lock_bounds.top() - inspector_view.top() - 8.0;
        if (std::abs(scroll_delta) > 1.0) {
          inspector_content->setProperty("contentY", std::max(0.0, current_content_y + scroll_delta));
          process_layout_events(6);
          inspector_view = scene_rect(inspector_scroll).adjusted(2.0, 2.0, -2.0, -2.0);
          lock_bounds = scene_rect(lock_button);
        }
        if (!item_visible(layer_properties) || !item_visible(lock_button) ||
            !inspector_view.contains(lock_bounds) ||
            !capture_visual_proof(studio_window, studio_root, proof_dir,
                QStringLiteral("pixel-layer-%1-%2").arg(density).arg(size.width()),
                size.width(), size.height(),
                {QStringLiteral("photoLayersInspectorGroup"), QStringLiteral("photoLayerRow"),
                 QStringLiteral("photoLayerPropertiesPanel"), QStringLiteral("photoLayerPropertiesHeader"),
                 QStringLiteral("photoLayerPropertiesDetailsButton"), QStringLiteral("photoLayerLockButton")},
                &studio_manifest)) {
          return fail("Pixel-layer stack and expanded properties should stay visible and scroll-contained");
        }
      }
      if (!click_studio_item(details_button) || layer_properties->property("detailsExpanded").toBool() ||
          item_visible(clipping_button) || !item_visible(lock_button)) {
        return fail("Pixel-layer details should collapse while retaining the common Lock action");
      }
    }

    // Exercise the selection brush HUD on the same isolated fixture. This
    // edits mask parameters only; pointer input stays on HUD controls.
    QVariant brush_mode_result;
    if (!QMetaObject::invokeMethod(edit, "setBrushPaintMode",
          Q_RETURN_ARG(QVariant, brush_mode_result), Q_ARG(QVariant, QVariant(QStringLiteral("add")))) ||
        brush_mode_result.toString() != QStringLiteral("add")) {
      return fail("Selection brush proof should enter through the Add workflow");
    }
    const auto brush_tool_ready = [&] {
      QVariant ready;
      return QMetaObject::invokeMethod(edit, "brushToolReady", Q_RETURN_ARG(QVariant, ready)) &&
             ready.toBool();
    };
    auto* brush_hud = find_named_item(studio_root, QStringLiteral("brushToolHud"));
    auto* brush_hud_surface = find_named_item(studio_root, QStringLiteral("brushHudSurface"));
    auto* brush_add = find_named_item(studio_root, QStringLiteral("brushHudAddButton"));
    auto* brush_erase = find_named_item(studio_root, QStringLiteral("brushHudEraseButton"));
    auto* brush_size_field = find_named_item(studio_root, QStringLiteral("brushHudSizeField"));
    auto* brush_reset = find_named_item(studio_root, QStringLiteral("brushHudResetDefaultsButton"));
    auto* brush_hardness = find_named_item(studio_root, QStringLiteral("brushHudHardnessVerticalSlider"));
    auto* brush_flow = find_named_item(studio_root, QStringLiteral("brushHudFlowVerticalSlider"));
    auto* brush_done = find_named_item(studio_root, QStringLiteral("brushHudDoneButton"));
    auto* brush_rail = find_named_item(studio_root, QStringLiteral("maskToolRail"));
    auto* brush_layer_header = find_named_item(studio_root, QStringLiteral("brushLayerInspectorHeader"));
    auto* brush_target_name = find_named_item(studio_root, QStringLiteral("brushLayerInspectorTargetNameLabel"));
    auto* brush_isolate = find_named_item(studio_root, QStringLiteral("brushLayerInspectorIsolationButton"));
    if (!brush_hud || !brush_hud_surface || !brush_add || !brush_erase || !brush_size_field ||
        !brush_reset || !brush_hardness ||
        !brush_flow || !brush_done || !brush_rail || !brush_layer_header ||
        !brush_target_name || !brush_isolate ||
        !wait_for_condition([&] {
          return brush_tool_ready() && brush_hud->isVisible() && edit->property("creativeToolsActive").toBool();
        }, 5000)) {
      return fail("Selection brush mode should expose its named native HUD and layer target header");
    }
    edit->setProperty("editInspectorTab", QStringLiteral("layers"));
    process_layout_events(8);
    auto* brush_inspector_scroll = find_named_item(studio_root, QStringLiteral("editInspectorScroll"));
    auto* brush_inspector_content = brush_inspector_scroll
        ? brush_inspector_scroll->property("contentItem").value<QObject*>() : nullptr;
    if (!brush_inspector_content) {
      return fail("Brush inspector should expose its scroll content");
    }
    brush_inspector_content->setProperty("contentY", 0.0);
    process_layout_events(6);
    auto expected_pixel_layer_name = paint_layer_data.value(QStringLiteral("displayName")).toString();
    if (expected_pixel_layer_name.isEmpty())
      expected_pixel_layer_name = paint_layer_data.value(QStringLiteral("name")).toString();
    if (expected_pixel_layer_name.isEmpty())
      expected_pixel_layer_name = QStringLiteral("Paint Layer");
    if (!brush_layer_header->isVisible() || brush_layer_header->width() < 200 ||
        !brush_target_name->isVisible() ||
        brush_target_name->property("text").toString() != expected_pixel_layer_name) {
      return fail("Brush layer inspector should identify the selected pixel-layer target");
    }

    if (!click_studio_item(brush_erase) ||
        editor_preview.activeMaskOperation() != QStringLiteral("subtract") ||
        !click_studio_item(brush_add) ||
        editor_preview.activeMaskOperation() != QStringLiteral("add")) {
      return fail("Visible selection brush Add and Erase controls should switch the active operation");
    }

    // The target card must follow Base Image selection and restore the pixel
    // layer target without leaving the selection-brush workflow.
    if (!QMetaObject::invokeMethod(edit, "activateBaseImageLayerTarget") ||
        !wait_for_condition([&] { return !brush_tool_ready(); }) ||
        !QMetaObject::invokeMethod(edit, "setBrushPaintMode",
              Q_ARG(QVariant, QVariant(QStringLiteral("add"))))) {
      return fail("Base Image should leave the current brush selection before a new Add workflow");
    }
    edit->setProperty("editInspectorTab", QStringLiteral("layers"));
    process_layout_events(6);
    if (!wait_for_condition([&] {
          return brush_tool_ready() && brush_layer_header->isVisible() &&
                 brush_target_name->property("text").toString() == QStringLiteral("Base image");
        }) || brush_isolate->property("enabled").toBool()) {
      return fail("Brush target header should identify Base Image and disable layer isolation");
    }
    QVariant brush_layer_selected_again;
    QVariant brush_pixel_layer_index;
    if (!QMetaObject::invokeMethod(edit, "photoLayerIndexForIdentity",
          Q_RETURN_ARG(QVariant, brush_pixel_layer_index), Q_ARG(QVariant, QVariant(paint_layer_id))) ||
        brush_pixel_layer_index.toInt() < 0 ||
        !QMetaObject::invokeMethod(edit, "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, brush_layer_selected_again), Q_ARG(QVariant, brush_pixel_layer_index),
          Q_ARG(QVariant, QVariant(false))) || !brush_layer_selected_again.toBool() ||
        !QMetaObject::invokeMethod(edit, "setBrushPaintMode", Q_ARG(QVariant, QVariant(QStringLiteral("add"))))) {
      return fail("Brush selection workflow should restore its selected pixel-layer target");
    }
    edit->setProperty("editInspectorTab", QStringLiteral("layers"));
    process_layout_events(6);
    if (!wait_for_condition([&] {
          return brush_tool_ready() && brush_layer_header->isVisible() &&
                 brush_target_name->property("text").toString() == expected_pixel_layer_name;
        })) {
      return fail("Brush selection workflow should restore its selected pixel-layer target");
    }

    const auto pixel_solo_matches_target = [&] {
      const auto layers = editor_preview.photoLayers();
      for (const auto& value : layers) {
        const auto layer = value.toMap();
        const auto expected_solo = layer.value(QStringLiteral("id")).toString() == paint_layer_id;
        if (layer.value(QStringLiteral("solo")).toBool() != expected_solo)
          return false;
      }
      return editor_preview.photoLayerSoloActive();
    };
    brush_inspector_content->setProperty("contentY", 0.0);
    process_layout_events(6);
    if (!click_studio_item(brush_isolate) || !wait_for_condition(pixel_solo_matches_target) ||
        !click_studio_item(brush_isolate) ||
        !wait_for_condition([&] { return !editor_preview.photoLayerSoloActive(); })) {
      std::cerr << "Brush isolation target=" << brush_layer_header->property("targetLayerIndex").toInt()
                << " bounds=" << scene_rect(brush_isolate).y()
                << " enabled=" << brush_isolate->property("enabled").toBool()
                << " solo=" << editor_preview.photoLayerSoloActive() << '\n';
      studio_window->grabWindow().save(QDir(proof_dir).filePath(QStringLiteral("brush-isolation-failure.png")));
      return fail("Brush isolation should solo its selected pixel layer and Clear should restore the stack");
    }

    const auto type_brush_size = [&](const QString& text) {
      brush_size_field->forceActiveFocus(Qt::MouseFocusReason);
      if (!send_window_key(studio_window, Qt::Key_A,
                           QKeySequence(QKeySequence::SelectAll)[0].keyboardModifiers()) ||
          !send_window_key(studio_window, Qt::Key_Backspace)) {
        return false;
      }
      for (const auto character : text) {
        const auto key = character.isDigit() ? static_cast<int>(character.unicode()) : 0;
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, QString(character));
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, QString(character));
        QCoreApplication::sendEvent(studio_window, &press);
        QCoreApplication::sendEvent(studio_window, &release);
        process_layout_events(2);
        if (!brush_size_field->hasActiveFocus())
          return false;
      }
      return send_window_key(studio_window, Qt::Key_Return);
    };
    QVariant brush_size_percent_set;
    if (!QMetaObject::invokeMethod(edit, "setActiveBrushSizePercent",
          Q_RETURN_ARG(QVariant, brush_size_percent_set), Q_ARG(QVariant, QVariant(9))) ||
        !brush_size_percent_set.toBool() ||
        !wait_for_condition([&] {
          return brush_size_field->property("text").toString() == QStringLiteral("9");
        })) {
      return fail("Brush size field should follow controller-side size changes before typed entry");
    }
    if (!type_brush_size(QStringLiteral("18")) ||
        !wait_for_condition([&] {
          return std::abs(editor_preview.activeMaskBrushSize() - 0.18) < 0.006 &&
                 brush_hud->property("sizePercentText").toString() == QStringLiteral("18");
        })) {
      return fail("Typed brush size should commit to the selection controller value");
    }
    if (!QMetaObject::invokeMethod(edit, "setActiveBrushSizePercent",
          Q_RETURN_ARG(QVariant, brush_size_percent_set), Q_ARG(QVariant, QVariant(12))) ||
        !brush_size_percent_set.toBool() ||
        !wait_for_condition([&] {
          return brush_size_field->property("text").toString() == QStringLiteral("12");
        }) || !QMetaObject::invokeMethod(edit, "setActiveBrushSizePercent",
          Q_RETURN_ARG(QVariant, brush_size_percent_set), Q_ARG(QVariant, QVariant(18))) ||
        !brush_size_percent_set.toBool() ||
        !wait_for_condition([&] {
          return brush_size_field->property("text").toString() == QStringLiteral("18") &&
                 std::abs(editor_preview.activeMaskBrushSize() - 0.18) < 0.006;
        })) {
      return fail("Typed brush size should keep following external controller changes after commit");
    }

    const auto drag_slider_edge = [&](QQuickItem* slider, bool toward_high) {
      if (!item_visible(slider) || !slider->property("enabled").toBool())
        return false;
      const auto bounds = scene_rect(slider);
      const auto start = bounds.center();
      const auto end = bounds.width() >= bounds.height()
          ? QPointF(toward_high ? bounds.right() - 2 : bounds.left() + 2, start.y())
          : QPointF(start.x(), toward_high ? bounds.top() + 2 : bounds.bottom() - 2);
      send_paint_pointer(QEvent::MouseButtonPress, start);
      for (int step = 1; step <= 5; ++step) {
        send_paint_pointer(QEvent::MouseMove,
                           start + (end - start) * (static_cast<double>(step) / 5.0));
      }
      send_paint_pointer(QEvent::MouseButtonRelease, end);
      return true;
    };
    const auto initial_hardness = editor_preview.activeMaskBrushHardness();
    const auto initial_flow = editor_preview.activeMaskBrushFlow();
    if (!drag_slider_edge(brush_hardness, true) ||
        !wait_for_condition([&] {
          return std::abs(editor_preview.activeMaskBrushHardness() - initial_hardness) > 0.02;
        }) || !drag_slider_edge(brush_flow, false) ||
        !wait_for_condition([&] {
          return std::abs(editor_preview.activeMaskBrushFlow() - initial_flow) > 0.02;
        })) {
      return fail("Selection brush hardness and flow sliders should change their matching controller values");
    }

    const QStringList brush_hud_control_names{
        QStringLiteral("brushHudSurface"), QStringLiteral("brushHudAddButton"),
        QStringLiteral("brushHudEraseButton"), QStringLiteral("brushHudResetDefaultsButton"),
        QStringLiteral("brushHudSizeField"), QStringLiteral("brushHudHardnessVerticalSlider"),
        QStringLiteral("brushHudFlowVerticalSlider"), QStringLiteral("brushHudDoneButton")};
    for (const auto& density : densities) {
      if (!QMetaObject::invokeMethod(edit, "setInterfaceDensity",
                                     Q_ARG(QVariant, QVariant(density)))) {
        return fail("Brush HUD proof should set each interface density");
      }
      process_layout_events(8);
      for (const auto& size : {QSize(1180, 768), QSize(1440, 900)}) {
        studio_window->resize(size);
        process_layout_events(8);
        const QRectF window_bounds(0, 0, studio_window->width(), studio_window->height());
        const auto hud_bounds = scene_rect(brush_hud);
        if (tool_viewport->width() < 350 || !brush_tool_ready() || !item_visible(brush_hud) || !item_visible(brush_rail) ||
            !item_visible(brush_layer_header) || !item_visible(creative_tools_panel) ||
            !edit->property("editCreativeToolsVisible").toBool() ||
            !item_visible(brush_hud_surface) || !item_visible(brush_add) ||
            !item_visible(brush_erase) || !item_visible(brush_reset) ||
            creative_tools_surface->window() != studio_window ||
            !window_bounds.contains(scene_rect(creative_tools_panel)) ||
            !scene_rect(creative_tools_panel).contains(hud_bounds) ||
            !visible_named_items_fit_parent(studio_root, QStringLiteral("brushToolHud"),
                                            brush_hud_control_names) ||
            brush_target_name->property("text").toString() != expected_pixel_layer_name ||
            !capture_visual_proof(studio_window, studio_root, proof_dir,
                QStringLiteral("selection-brush-%1-%2").arg(density).arg(size.width()),
                size.width(), size.height(),
                {QStringLiteral("editImageViewport"), QStringLiteral("creativeToolsPanel"),
                 QStringLiteral("brushToolHud"),
                 QStringLiteral("brushHudSurface"),
                 QStringLiteral("brushHudAddButton"), QStringLiteral("brushHudEraseButton"),
                 QStringLiteral("brushHudResetDefaultsButton"),
                 QStringLiteral("brushHudSizeField"), QStringLiteral("brushHudHardnessVerticalSlider"),
                 QStringLiteral("brushHudFlowVerticalSlider"), QStringLiteral("brushHudDoneButton"),
                 QStringLiteral("brushLayerInspectorHeader"), QStringLiteral("brushLayerInspectorTargetNameLabel"),
                 QStringLiteral("maskToolRail")}, &studio_manifest)) {
          return fail("Selection brush HUD should fit below its rail without colliding with canvas chrome");
        }
      }
    }
    QVariant tool_layout_state;
    QVariant tool_layout_result;
    if (!edit->property("editCreativeToolsVisible").toBool() ||
        edit->property("editCreativeToolsFloating").toBool() ||
        !QMetaObject::invokeMethod(edit, "currentWorkspaceLayoutState",
          Q_RETURN_ARG(QVariant, tool_layout_state),
          Q_ARG(QVariant, QVariant(QStringLiteral("Creative tools proof")))) ||
        !tool_layout_state.toMap().contains(QStringLiteral("editCreativeToolsHeight")) ||
        tool_layout_state.toMap().value(QStringLiteral("editCreativeToolsHeight")).toDouble() <= 0.0 ||
        !tool_layout_state.toMap().value(QStringLiteral("editCreativeToolsVisible")).toBool() ||
        tool_layout_state.toMap().value(QStringLiteral("editCreativeToolsFloating")).toBool() ||
        !QMetaObject::invokeMethod(edit, "saveWorkspaceLayout", Q_RETURN_ARG(QVariant, tool_layout_result),
          Q_ARG(QVariant, QVariant(QStringLiteral("Creative tools proof")))) || !tool_layout_result.toBool()) {
      return fail("Creative Tools visibility and dock state should be included in saved workspace layouts");
    }
    const auto retained_brush_size = editor_preview.activeMaskBrushSize();
    const auto retained_brush_hardness = editor_preview.activeMaskBrushHardness();
    const auto retained_brush_flow = editor_preview.activeMaskBrushFlow();
    const auto retained_brush_operation = editor_preview.activeMaskOperation();
    const auto retained_layer_count = editor_preview.photoLayerCount();
    auto* creative_tools_window = qobject_cast<QQuickWindow*>(creative_tools_window_object);
    if (!creative_tools_window || !click_studio_item(creative_tools_float) ||
        !wait_for_condition([&] {
          return edit->property("editCreativeToolsFloating").toBool() &&
                 creative_tools_window->isVisible() && creative_tools_content->window() == creative_tools_window &&
                 creative_tools_surface->window() == creative_tools_window &&
                 brush_hud->window() == creative_tools_window;
        }, 5000)) {
      return fail("Creative Tools should float its current brush panel into the native tool window");
    }
    const QRectF floating_bounds(0, 0, creative_tools_window->width(), creative_tools_window->height());
    if (!floating_bounds.contains(scene_rect(creative_tools_content)) ||
        !floating_bounds.contains(scene_rect(brush_hud)) ||
        !floating_bounds.contains(scene_rect(creative_tools_surface))) {
      return fail("Floating Creative Tools controls should fit inside their separate window");
    }
    const auto floating_capture = creative_tools_window->grabWindow();
    if (floating_capture.isNull() ||
        !floating_capture.save(QDir(proof_dir).filePath(QStringLiteral("creative-tools-brush-floating.png"))) ||
        !click_studio_item(creative_tools_float) ||
        !wait_for_condition([&] {
          return !edit->property("editCreativeToolsFloating").toBool() &&
                 creative_tools_content->window() == studio_window &&
                 creative_tools_surface->window() == studio_window;
        }, 5000) || !click_studio_item(creative_tools_close) ||
        !wait_for_condition([&] {
          return !edit->property("editCreativeToolsVisible").toBool() &&
                 !creative_tools_surface->isVisible() && !edit->property("creativeToolsActive").toBool() &&
                 brush_tool_ready();
        }, 5000) ||
        std::abs(editor_preview.activeMaskBrushSize() - retained_brush_size) > 1e-6 ||
        std::abs(editor_preview.activeMaskBrushHardness() - retained_brush_hardness) > 1e-6 ||
        std::abs(editor_preview.activeMaskBrushFlow() - retained_brush_flow) > 1e-6 ||
        editor_preview.activeMaskOperation() != retained_brush_operation ||
        editor_preview.photoLayerCount() != retained_layer_count) {
      std::cerr << "Tools close: visible=" << edit->property("editCreativeToolsVisible").toBool()
                << " surface=" << creative_tools_surface->isVisible()
                << " ready=" << brush_tool_ready() << " floating=" << edit->property("editCreativeToolsFloating").toBool()
                << " size=" << editor_preview.activeMaskBrushSize() << '/' << retained_brush_size
                << " hardness=" << editor_preview.activeMaskBrushHardness() << '/' << retained_brush_hardness
                << " flow=" << editor_preview.activeMaskBrushFlow() << '/' << retained_brush_flow
                << " layers=" << editor_preview.photoLayerCount() << '/' << retained_layer_count << '\n';
      studio_window->grabWindow().save(QDir(proof_dir).filePath(QStringLiteral("tools-close-failure.png")));
      return fail("Closing Creative Tools should hide its controls without changing the active brush or layer stack");
    }
    QVariant layout_menu_items_value;
    if (!QMetaObject::invokeMethod(edit, "workspaceLayoutMenuItems",
          Q_RETURN_ARG(QVariant, layout_menu_items_value))) {
      return fail("The Edit Layout menu should expose its Tool controls entry");
    }
    const auto layout_menu_items = layout_menu_items_value.toList();
    int tools_menu_index = -1;
    for (int index = 0; index < layout_menu_items.size(); ++index) {
      if (layout_menu_items.at(index).toMap().value(QStringLiteral("key")).toString() == QStringLiteral("tools")) {
        tools_menu_index = index;
        break;
      }
    }
    auto* layout_button = find_named_item(studio_root, QStringLiteral("editWorkspaceLayoutButton"));
    if (!layout_button || tools_menu_index < 0 ||
        !QMetaObject::invokeMethod(layout_button, "openMenu")) {
      return fail("The Layout menu should offer a direct way to reopen Creative Tools");
    }
    const auto tools_menu_row_name = QStringLiteral("editWorkspaceLayoutButtonPopupItem%1").arg(tools_menu_index);
    auto* tools_menu_row = find_named_item(studio_root, tools_menu_row_name);
    auto* tools_menu_popup = find_named_object(studio_root, QStringLiteral("editWorkspaceLayoutButtonPopup"));
    if (!tools_menu_row || !tools_menu_popup || !wait_for_condition([&] {
          return tools_menu_popup->property("opened").toBool();
        }, 3000) || !click_studio_item(tools_menu_row) ||
        !wait_for_condition([&] {
          return edit->property("editCreativeToolsVisible").toBool() &&
                 creative_tools_panel->isVisible() && edit->property("creativeToolsActive").toBool() &&
                 creative_tools_surface->isVisible() && creative_tools_content->window() == studio_window &&
                 brush_tool_ready();
        }, 5000) ||
        std::abs(editor_preview.activeMaskBrushSize() - retained_brush_size) > 1e-6 ||
        std::abs(editor_preview.activeMaskBrushHardness() - retained_brush_hardness) > 1e-6 ||
        std::abs(editor_preview.activeMaskBrushFlow() - retained_brush_flow) > 1e-6 ||
        editor_preview.photoLayerCount() != retained_layer_count) {
      std::cerr << "Tools reopen: row=" << bool(tools_menu_row)
                << " popup=" << bool(tools_menu_popup)
                << " toolsVisible=" << edit->property("editCreativeToolsVisible").toBool()
                << " ready=" << brush_tool_ready() << " panel=" << creative_tools_panel->isVisible() << '\n';
      studio_window->grabWindow().save(QDir(proof_dir).filePath(QStringLiteral("tools-reopen-failure.png")));
      return fail("Layout > Tool controls should reopen the current brush state without recreating layers");
    }
    if (!click_studio_item(creative_tools_float) ||
        !wait_for_condition([&] { return edit->property("editCreativeToolsFloating").toBool(); }, 5000) ||
        !QMetaObject::invokeMethod(edit, "applyWorkspaceLayout",
          Q_RETURN_ARG(QVariant, tool_layout_result), Q_ARG(QVariant, QVariant(QStringLiteral("Creative tools proof")))) ||
        !tool_layout_result.toBool() || !wait_for_condition([&] {
          return edit->property("editCreativeToolsVisible").toBool() &&
                 !edit->property("editCreativeToolsFloating").toBool() &&
                 creative_tools_content->window() == studio_window &&
                 creative_tools_surface->window() == studio_window;
        }, 5000)) {
      return fail("Applying a saved workspace layout should restore the Creative Tools dock and visibility state");
    }
    if (!click_studio_item(brush_done) ||
        !wait_for_condition([&] { return !brush_tool_ready() && !brush_hud->isVisible(); }, 5000)) {
      return fail("Done should exit the selection brush workflow cleanly");
    }

#include "creative_tool_sidebar_interactions.inc"

#include "creative_tool_shape_creation_interactions.inc"

    // Add shape and text layer fixtures through the controller API, then prove
    // their real properties editors in the shared docked Creative Tools panel.
    const auto shape_fixture_count = editor_preview.photoLayerCount();
    const auto image_width = editor_preview.sourceImageWidth();
    const auto image_height = editor_preview.sourceImageHeight();
    if (image_width < 80 || image_height < 80 ||
        !editor_preview.addPhotoShape(QStringLiteral("rectangle"), image_width / 8, image_height / 8,
            image_width / 2, image_height / 2, image_width, image_height,
            QStringLiteral("#E06A56"), QStringLiteral("#F2E8D9"), 6.0) ||
        !editor_preview.addTextLayer(QStringLiteral("Creative tools proof"), QStringLiteral("Proof Text")) ||
        editor_preview.photoLayerCount() != shape_fixture_count + 2 ||
        !invoke_edit_tool(QStringLiteral("layers"))) {
      return fail("Creative Tools proof should create isolated shape and text layers on the fixture");
    }
    const auto fixture_layers = editor_preview.photoLayers();
    int shape_layer_index = -1;
    int text_layer_index = -1;
    for (int index = 0; index < fixture_layers.size(); ++index) {
      const auto layer = fixture_layers.at(index).toMap();
      if (!layer.value(QStringLiteral("shapeDefinition")).toMap().isEmpty())
        shape_layer_index = index;
      if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("text"))
        text_layer_index = index;
    }
    if (shape_layer_index < 0 || text_layer_index < 0) {
      return fail("Creative Tools fixture layers should retain their native Shape and Text identities");
    }
    auto* shape_editor = find_named_item(studio_root, QStringLiteral("photoLayerShapeEditor"));
    auto* shape_no_fill = find_named_item(studio_root, QStringLiteral("photoShapeLayerNoFillButton"));
    auto* shape_width_field = find_named_item(studio_root, QStringLiteral("photoShapeLayerStrokeWidthField"));
    auto* creative_tools_scroll = find_named_item(studio_root, QStringLiteral("creativeToolsScroll"));
    const auto select_layer_for_tools = [&](int layer_index) {
      QVariant selected;
      return QMetaObject::invokeMethod(edit, "setActivePhotoLayerSelection",
                 Q_RETURN_ARG(QVariant, selected), Q_ARG(QVariant, QVariant(layer_index)),
                 Q_ARG(QVariant, QVariant(false))) && selected.toBool() &&
             wait_for_condition([&] { return edit->property("creativeToolsActive").toBool(); }, 5000);
    };
    edit->setProperty("editInspectorTab", QStringLiteral("layers"));
    if (!select_layer_for_tools(shape_layer_index) || !shape_editor || !shape_no_fill ||
        !shape_width_field || !creative_tools_scroll || !shape_editor->isVisible() ||
        !click_studio_item(shape_no_fill) ||
        !wait_for_condition([&] {
          return shape_editor->property("definition").toMap().value(QStringLiteral("fillColor")).toString().isEmpty();
        }, 3000)) {
      return fail("Selected shape properties should appear in Creative Tools and respond to No Fill");
    }
    for (const auto& density : densities) {
      if (!QMetaObject::invokeMethod(edit, "setInterfaceDensity", Q_ARG(QVariant, QVariant(density))))
        return fail("Shape properties proof should apply every interface density");
      process_layout_events(8);
      for (const auto& size : {QSize(1180, 768), QSize(1440, 900)}) {
        studio_window->resize(size);
        process_layout_events(8);
        const auto panel_bounds = scene_rect(creative_tools_surface);
        const auto scroll_bounds = scene_rect(creative_tools_scroll);
        if (!item_visible(shape_no_fill) || !panel_bounds.contains(scene_rect(shape_editor)) ||
            !scroll_bounds.contains(scene_rect(shape_no_fill)) ||
            !capture_visual_proof(studio_window, studio_root, proof_dir,
                QStringLiteral("creative-shape-%1-%2").arg(density).arg(size.width()),
                size.width(), size.height(),
                {QStringLiteral("creativeToolsPanel"), QStringLiteral("creativeToolsSurface"),
                 QStringLiteral("photoLayerShapeEditor"), QStringLiteral("photoShapeLayerNoFillButton"),
                 QStringLiteral("photoShapeLayerStrokeWidthField")}, &studio_manifest)) {
          return fail("Selected shape controls should remain visible inside the tool panel at each density and width");
        }
      }
    }
    auto* text_editor = find_named_item(studio_root, QStringLiteral("photoLayerTextEditor"));
    auto* text_content_field = find_named_item(studio_root, QStringLiteral("photoLayerTextContentField"));
    auto* text_bold_button = find_named_item(studio_root, QStringLiteral("photoLayerTextBoldButton"));
    if (!select_layer_for_tools(text_layer_index) || !text_editor || !text_content_field ||
        !text_bold_button || !wait_for_condition([&] { return item_visible(text_editor); }, 3000) ||
        !click_studio_item(text_content_field) ||
        !QMetaObject::invokeMethod(text_content_field, "forceInputFocus") ||
        !wait_for_condition([&] { return text_content_field->property("userEditing").toBool(); }, 3000)) {
      std::cerr << "Text focus: field=" << bool(text_content_field)
                << " visible=" << (text_content_field && item_visible(text_content_field))
                << " enabled=" << (text_content_field && text_content_field->isEnabled())
                << " userEditing=" << (text_content_field && text_content_field->property("userEditing").toBool())
                << " panel=" << creative_tools_panel->isVisible()
                << " mainActive=" << studio_window->isActive()
                << " layerIndex=" << layer_properties->property("layerIndex").toInt()
                << " kind=" << layer_properties->property("layerData").toMap().value(QStringLiteral("kind")).toString().toStdString() << '\n';
      studio_window->grabWindow().save(QDir(proof_dir).filePath(QStringLiteral("text-focus-failure.png")));
      return fail("Selected text layer should show its live typography controls in Creative Tools");
    }
    text_content_field->setProperty("text", QStringLiteral("Native tool text"));
    process_layout_events(6);
    QMetaObject::invokeMethod(text_content_field, "clearInputFocus");
    if (!wait_for_condition([&] {
          return editor_preview.photoLayers().at(text_layer_index).toMap()
              .value(QStringLiteral("text")).toString() == QStringLiteral("Native tool text");
        }, 3000) || !click_studio_item(text_bold_button) ||
        !wait_for_condition([&] {
          return editor_preview.photoLayers().at(text_layer_index).toMap()
              .value(QStringLiteral("fontWeight")).toString() == QStringLiteral("bold");
        }, 3000)) {
      std::cerr << "Text controls: text=" << editor_preview.photoLayers().at(text_layer_index).toMap().value(QStringLiteral("text")).toString().toStdString()
                << " weight=" << editor_preview.photoLayers().at(text_layer_index).toMap().value(QStringLiteral("fontWeight")).toString().toStdString()
                << " fieldFocus=" << text_content_field->property("userEditing").toBool()
                << " enabled=" << text_content_field->isEnabled() << '\n';
      studio_window->grabWindow().save(QDir(proof_dir).filePath(QStringLiteral("text-controls-failure.png")));
      return fail("Typography text input and Bold control should update the selected text layer");
    }
    for (const auto& density : densities) {
      if (!QMetaObject::invokeMethod(edit, "setInterfaceDensity", Q_ARG(QVariant, QVariant(density))))
        return fail("Text properties proof should apply every interface density");
      process_layout_events(8);
      for (const auto& size : {QSize(1180, 768), QSize(1440, 900)}) {
        studio_window->resize(size);
        process_layout_events(8);
        const auto panel_bounds = scene_rect(creative_tools_surface);
        const auto scroll_bounds = scene_rect(creative_tools_scroll);
        const auto editor_bounds = scene_rect(text_editor);
        const auto field_bounds = scene_rect(text_content_field);
        const auto bold_bounds = scene_rect(text_bold_button);
        if (!item_visible(text_content_field) || !item_visible(text_bold_button) ||
            editor_bounds.left() < panel_bounds.left() || editor_bounds.right() > panel_bounds.right() ||
            !scroll_bounds.contains(field_bounds) || !scroll_bounds.contains(bold_bounds) ||
            !capture_visual_proof(studio_window, studio_root, proof_dir,
                QStringLiteral("creative-text-%1-%2").arg(density).arg(size.width()),
                size.width(), size.height(),
                {QStringLiteral("creativeToolsPanel"), QStringLiteral("creativeToolsSurface"),
                 QStringLiteral("photoLayerTextEditor"), QStringLiteral("photoLayerTextContentField"),
                 QStringLiteral("photoLayerTextBoldButton")}, &studio_manifest)) {
          return fail("Selected typography controls should remain visible in Creative Tools at each density and width");
        }
      }
    }

#include "creative_tool_transform_interactions.inc"
    QFile interaction_metrics_file(QDir(proof_dir).filePath(QStringLiteral("interaction-metrics.json")));
    if (!interaction_metrics_file.open(QIODevice::WriteOnly) ||
        interaction_metrics_file.write(QJsonDocument(creative_interaction_metrics).toJson(QJsonDocument::Indented)) <= 0)
      return fail("Creative tools should save their measured interaction evidence");
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_CREATIVE_INTERACTIONS")) {
      QFile manifest_file(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
      if (!manifest_file.open(QIODevice::WriteOnly) ||
          manifest_file.write(QJsonDocument(studio_manifest).toJson(QJsonDocument::Indented)) <= 0)
        return fail("Creative tools should save their native screenshot manifest");
      return EXIT_SUCCESS;
    }

    // Native Edit inspector proof for the reorganized tonal/color workflow.
    // All edits use the isolated fixture and reset before the next section.
    const auto wait_for_adjustment_layout = [&] {
      return wait_for_condition([&] {
        return !edit->property("editToolTransitionActive").toBool() &&
               !edit->property("workspaceTransitionActive").toBool();
      }, 5000);
    };
    auto* inspector_scroll = find_named_item(studio_root, QStringLiteral("editInspectorScroll"));
    auto* inspector_content = inspector_scroll
        ? inspector_scroll->property("contentItem").value<QObject*>() : nullptr;
    auto* white_balance_group = find_named_item(studio_root, QStringLiteral("whiteBalanceInspectorGroup"));
    auto* white_balance_sample = find_named_item(studio_root, QStringLiteral("whiteBalanceSampleNeutralButton"));
    auto* levels_group = find_named_item(studio_root, QStringLiteral("levelsInspectorGroup"));
    auto* levels_histogram = find_named_item(studio_root, QStringLiteral("toneCurveInputLevelsHistogram"));
    auto* levels_strip = find_named_item(studio_root, QStringLiteral("toneCurveInputLevelsStrip"));
    auto* levels_black = find_named_item(studio_root, QStringLiteral("toneCurveInputBlackSlider"));
    auto* levels_gamma = find_named_item(studio_root, QStringLiteral("toneCurveInputGammaSlider"));
    auto* levels_white = find_named_item(studio_root, QStringLiteral("toneCurveInputWhiteSlider"));
    auto* levels_reset = find_named_item(studio_root, QStringLiteral("toneCurveInputLevelsResetButton"));
    auto* curve_group = find_named_item(studio_root, QStringLiteral("toneCurveInspectorGroup"));
    auto* curve_editor = find_named_item(studio_root, QStringLiteral("studioToneCurveEditor"));
    auto* curve_graph = find_named_item(curve_editor, QStringLiteral("toneCurveGraph"));
    auto* curve_presets_toggle = find_named_item(studio_root, QStringLiteral("studioToneCurveEditorPresetsToggleButton"));
    auto* grading_group = find_named_item(studio_root, QStringLiteral("colorGradingInspectorGroup"));
    auto* grading_layout = find_named_item(studio_root, QStringLiteral("colorGradingEditorLayout"));
    auto* grading_wheel = find_named_item(studio_root, QStringLiteral("splitToningFocusedWheel"));
    auto* grading_shadows = find_named_item(studio_root, QStringLiteral("splitToningShadowsWheel"));
    auto* grading_midtones = find_named_item(studio_root, QStringLiteral("splitToningMidtonesWheel"));
    auto* grading_highlights = find_named_item(studio_root, QStringLiteral("splitToningHighlightsWheel"));
    auto* grading_hue = find_named_item(studio_root, QStringLiteral("selectedToneWheelHueSlider"));
    auto* grading_reset = find_named_item(studio_root, QStringLiteral("colorGradingPanelResetButton"));
    if (!inspector_scroll || !inspector_content || !white_balance_group || !white_balance_sample ||
        !levels_group || !levels_histogram || !levels_strip || !levels_black || !levels_gamma ||
        !levels_white || !levels_reset || !curve_group || !curve_editor || !curve_graph ||
        !curve_presets_toggle || !grading_group || !grading_layout || !grading_wheel ||
        !grading_shadows || !grading_midtones || !grading_highlights || !grading_hue || !grading_reset) {
      return fail("Edit inspector should expose named White Balance, Levels, Tone Curve, and Grading controls");
    }
    auto* removed_white_balance_pad = find_named_item(studio_root, QStringLiteral("whiteBalancePad"));
    if (removed_white_balance_pad ||
        !QMetaObject::invokeMethod(edit, "activateBaseImageLayerTarget") ||
        !invoke_edit_tool(QStringLiteral("adjust"))) {
      return fail("White Balance should use direct Temperature/Tint controls, and Levels must remain target-aware");
    }
    edit->setProperty("activeAdjustmentIntent", QStringLiteral("all"));
    edit->setProperty("editInspectorTab", QStringLiteral("adjust"));
    edit->setProperty("editAdjustmentFilterText", QStringLiteral("levels"));
    process_layout_events(6);
    if (!wait_for_adjustment_layout() ||
        !QMetaObject::invokeMethod(edit, "setPrimaryAdjustmentValue",
          Q_ARG(QVariant, QVariant(QStringLiteral("levelsBlackPoint"))), Q_ARG(QVariant, QVariant(20.0))) ||
        !QMetaObject::invokeMethod(edit, "setPrimaryAdjustmentValue",
          Q_ARG(QVariant, QVariant(QStringLiteral("levelsWhitePoint"))), Q_ARG(QVariant, QVariant(20.0)))) {
      return fail("Standalone Levels should accept target-aware black and white endpoint changes");
    }
    QVariant accepted_gamma;
    if (!QMetaObject::invokeMethod(levels_gamma, "testCommitInputText",
          Q_RETURN_ARG(QVariant, accepted_gamma), Q_ARG(QVariant, QStringLiteral("2.00"))) ||
        std::abs(accepted_gamma.toDouble() -
            (-100.0 * std::log2(2.0) / 1.45)) > 0.02 || std::abs(editor_preview.levelsGamma() -
            (-100.0 * std::log2(2.0) / 1.45)) > 0.02 ||
        levels_gamma->property("valueText").toString() != QStringLiteral("2.00")) {
      return fail("Levels gamma entry should accept a ratio and map it to the engine's inverse gamma parameter");
    }
    const auto gamma_after_valid_entry = editor_preview.levelsGamma();
    QVariant rejected_gamma;
    QMetaObject::invokeMethod(levels_gamma, "testCommitInputText",
          Q_RETURN_ARG(QVariant, rejected_gamma), Q_ARG(QVariant, QStringLiteral("0")));
    if (std::abs(editor_preview.levelsGamma() - gamma_after_valid_entry) > 1e-6) {
      return fail("Levels gamma should reject a nonpositive ratio without changing the recipe");
    }
    if (!QMetaObject::invokeMethod(levels_reset, "clicked") ||
        !wait_for_condition([&] {
          return std::abs(editor_preview.levelsBlackPoint()) < 1e-6 &&
                 std::abs(editor_preview.levelsGamma()) < 1e-6 &&
                 std::abs(editor_preview.levelsWhitePoint()) < 1e-6;
        }, 3000)) {
      return fail("Levels reset should restore black, gamma, and white input endpoints together");
    }

    edit->setProperty("activeAdjustmentIntent", QStringLiteral("all"));
    edit->setProperty("editAdjustmentFilterText", QStringLiteral("white balance"));
    edit->setProperty("editInspectorTab", QStringLiteral("adjust"));
    if (!wait_for_adjustment_layout() || !white_balance_group->property("visible").toBool()) {
      return fail("White Balance should remain available as direct temperature and tint adjustments");
    }
    const QStringList inspector_densities{QStringLiteral("comfortable"), QStringLiteral("balanced"),
                                          QStringLiteral("compact")};
    const QList<QSize> inspector_sizes{QSize(1180, 768), QSize(1440, 900)};
    for (const auto& density : inspector_densities) {
      edit->setProperty("editAdjustmentFilterText", QStringLiteral("white balance"));
      process_layout_events(6);
      if (!QMetaObject::invokeMethod(edit, "setInterfaceDensity",
            Q_ARG(QVariant, QVariant(density))) || !wait_for_adjustment_layout()) {
        return fail("Edit inspector proof should settle each interface density before capture");
      }
      for (const auto& size : inspector_sizes) {
        studio_window->resize(size);
        inspector_content->setProperty("contentY", 0.0);
        process_layout_events(8);
        if (!capture_visual_proof(studio_window, studio_root, proof_dir,
              QStringLiteral("adjustment-white-balance-%1-%2").arg(density).arg(size.width()),
              size.width(), size.height(),
              {QStringLiteral("whiteBalanceInspectorGroup"), QStringLiteral("temperatureSlider"),
               QStringLiteral("tintSlider"), QStringLiteral("whiteBalanceSampleNeutralButton")},
              &studio_manifest)) {
          return fail("White Balance sliders and neutral sampling should remain aligned at each density");
        }
      }

      const auto capture_section = [&](const QString& filter, const QString& proof_name,
                                       const QStringList& required, QQuickItem* graph_item) {
        edit->setProperty("editAdjustmentFilterText", filter);
        if (!wait_for_adjustment_layout())
          return false;
        for (const auto& size : inspector_sizes) {
          studio_window->resize(size);
          inspector_content->setProperty("contentY", 0.0);
          process_layout_events(8);
          if (graph_item == grading_wheel) {
            // The wheel has transparent space around its painted disc. Keep
            // the disc and drag handle visible while allowing its lower
            // precision controls to scroll in short desktop windows.
            const auto bounds = scene_rect(graph_item);
            const auto viewport = scene_rect(inspector_scroll).adjusted(2.0, 2.0, -2.0, -2.0);
            const auto visible_side = graph_item->property("wheelSize").toDouble() * 0.94;
            const auto painted = QRectF(bounds.center().x() - visible_side / 2.0,
                                        bounds.center().y() - visible_side / 2.0,
                                        visible_side, visible_side);
            if (painted.bottom() > viewport.bottom()) {
              inspector_content->setProperty("contentY", painted.bottom() - viewport.bottom() + 2.0);
              process_layout_events(8);
            }
          }
          if (!capture_visual_proof(studio_window, studio_root, proof_dir,
                QStringLiteral("%1-%2-%3").arg(proof_name, density).arg(size.width()),
                size.width(), size.height(), required, &studio_manifest))
            return false;
          const QRectF window_bounds(0, 0, studio_window->width(), studio_window->height());
          if (graph_item) {
            auto graph_bounds = scene_rect(graph_item);
            if (graph_item == grading_wheel) {
              const auto side = graph_item->property("wheelSize").toDouble() * 0.94;
              graph_bounds = QRectF(graph_bounds.center().x() - side / 2.0,
                                    graph_bounds.center().y() - side / 2.0, side, side);
            }
            const auto scroll_bounds = scene_rect(inspector_scroll).adjusted(2.0, 2.0, -2.0, -2.0);
            if (!item_visible(graph_item) || graph_bounds.width() < 250.0 ||
                graph_bounds.height() < 90.0 || !window_bounds.contains(graph_bounds) ||
                !scroll_bounds.contains(graph_bounds)) {
              qWarning() << "Inspector graph bounds" << proof_name << density << size
                         << graph_bounds << "scroll" << scroll_bounds
                         << "visible" << item_visible(graph_item);
              return false;
            }
          }
        }
        return true;
      };

      edit->setProperty("editAdjustmentFilterText", QStringLiteral("levels"));
      if (!wait_for_adjustment_layout() || !levels_group->property("visible").toBool() ||
          !item_visible(levels_strip) || !item_visible(levels_histogram) ||
          !capture_section(QStringLiteral("levels"), QStringLiteral("adjustment-levels"),
              {QStringLiteral("levelsInspectorGroup"), QStringLiteral("toneCurveInputLevelsStrip"),
               QStringLiteral("toneCurveInputLevelsHistogram"), QStringLiteral("toneCurveInputGammaSlider"),
               QStringLiteral("toneCurveInputLevelsResetButton")}, nullptr)) {
        return fail("Standalone Levels histogram, inverse gamma, and reset should fit at narrow and wide widths");
      }
      edit->setProperty("editAdjustmentFilterText", QStringLiteral("tone curve"));
      if (!wait_for_adjustment_layout() || !curve_group->property("visible").toBool() ||
          curve_editor->property("presetsExpanded").toBool() ||
          !capture_section(QStringLiteral("tone curve"), QStringLiteral("adjustment-tone-curve"),
              {QStringLiteral("toneCurveInspectorGroup"), QStringLiteral("studioToneCurveEditor"),
               QStringLiteral("toneCurveGraph"), QStringLiteral("studioToneCurveEditorPresetsToggleButton")},
              curve_graph)) {
        return fail("Tone Curve graph should stay bounded in the inspector with presets collapsed by default");
      }
      edit->setProperty("editAdjustmentFilterText", QStringLiteral("grading"));
      if (!wait_for_adjustment_layout() || !grading_group->property("visible").toBool() ||
          grading_wheel->property("wheelSize").toDouble() < 250.0 ||
          grading_wheel->property("title").toString().size() > 0 ||
          grading_wheel->property("showValueReadouts").toBool() ||
          !scene_rect(grading_layout).contains(scene_rect(grading_wheel))) {
        return fail("Color Grading should give its hue/saturation wheel at least 250px in the responsive editor");
      }
      if (!capture_section(QStringLiteral("grading"), QStringLiteral("adjustment-grading-wheel"),
              {QStringLiteral("colorGradingInspectorGroup"), QStringLiteral("colorGradingEditorLayout"),
               QStringLiteral("splitToningShadowsWheel"), QStringLiteral("splitToningMidtonesWheel"),
               QStringLiteral("splitToningHighlightsWheel"), QStringLiteral("splitToningFocusedWheel")}, grading_wheel) ||
          !click_studio_item(grading_highlights) || edit->property("selectedToneWheel").toInt() != 2) {
        return fail("Grading captures should show a large wheel, three target tabs, and precision controls");
      }
      const auto initial_highlight_hue = editor_preview.gradingHighlightsHue();
      const auto initial_highlight_saturation = editor_preview.gradingHighlightsSaturation();
      const auto wheel_bounds = scene_rect(grading_wheel);
      const auto wheel_center = wheel_bounds.center();
      const auto wheel_start = QPointF(wheel_center.x(), wheel_center.y() - wheel_bounds.height() * 0.20);
      const auto wheel_target = QPointF(wheel_center.x() + wheel_bounds.width() * 0.30,
                                        wheel_center.y());
      send_paint_pointer(QEvent::MouseButtonPress, wheel_start);
      send_paint_pointer(QEvent::MouseMove, wheel_target);
      send_paint_pointer(QEvent::MouseButtonRelease, wheel_target);
      if (!wait_for_condition([&] {
            return std::abs(editor_preview.gradingHighlightsHue() - initial_highlight_hue) > 0.02 &&
                   std::abs(editor_preview.gradingHighlightsSaturation() - initial_highlight_saturation) > 2.0;
          }, 3000)) {
        return fail("Dragging the large grading wheel should edit the selected target hue and saturation together");
      }
      if (!QMetaObject::invokeMethod(grading_reset, "clicked") ||
          !wait_for_condition([&] {
            return std::abs(editor_preview.gradingHighlightsHue()) < 1e-6 &&
                   std::abs(editor_preview.gradingHighlightsSaturation()) < 1e-6;
          }, 3000)) {
        return fail("Color Grading reset should restore the selected Highlights target after wheel proof");
      }
      inspector_content->setProperty("contentY", 0.0);
      process_layout_events(8);
      if (!click_studio_item(grading_midtones) || edit->property("selectedToneWheel").toInt() != 1 ||
          !click_studio_item(grading_shadows) || edit->property("selectedToneWheel").toInt() != 0 ||
          !click_studio_item(grading_midtones)) {
        return fail("Grading tabs should address Shadows, Midtones, and Highlights independently");
      }
      auto* grading_balance = find_named_item(studio_root, QStringLiteral("colorGradingBalanceSlider"));
      if (!grading_balance)
        return fail("Grading should retain its global Balance control below the precision fields");
      const auto controls_viewport = scene_rect(inspector_scroll).adjusted(2.0, 2.0, -2.0, -2.0);
      inspector_content->setProperty("contentY", std::max(0.0,
          scene_rect(grading_balance).bottom() - controls_viewport.bottom() + 8.0));
      process_layout_events(8);
      if (!controls_viewport.contains(scene_rect(grading_hue)) ||
          !controls_viewport.contains(scene_rect(grading_balance)) ||
          !capture_visual_proof(studio_window, studio_root, proof_dir,
              QStringLiteral("adjustment-grading-values-%1").arg(density), 1440, 900,
              {QStringLiteral("selectedToneWheelHueSlider"), QStringLiteral("selectedToneWheelSaturationSlider"),
               QStringLiteral("selectedToneWheelLuminanceSlider"), QStringLiteral("colorGradingAmountSlider"),
               QStringLiteral("colorGradingBalanceSlider")}, &studio_manifest)) {
        return fail("Grading precision fields and Amount/Balance should fit together when scrolled into view");
      }
      inspector_content->setProperty("contentY", 0.0);
      process_layout_events(8);
    }
    edit->setProperty("editAdjustmentFilterText", QString());
    edit->setProperty("activeAdjustmentIntent", QStringLiteral("all"));
    if (!wait_for_adjustment_layout()) {
      return fail("Edit inspector should restore its full adjustment stack after native proof");
    }

    QVariant layout_saved;
    if (!QMetaObject::invokeMethod(edit, "saveWorkspaceLayout", Q_RETURN_ARG(QVariant, layout_saved),
          Q_ARG(QVariant, QVariant(QStringLiteral("Independent density")))) || !layout_saved.toBool()) {
      return fail("Edit should save its panel arrangement");
    }
    app_shell.setInterfaceDensity(QStringLiteral("comfortable"));
    QVariant layout_applied;
    if (!QMetaObject::invokeMethod(edit, "applyWorkspaceLayout", Q_RETURN_ARG(QVariant, layout_applied),
          Q_ARG(QVariant, QVariant(QStringLiteral("Independent density")))) || !layout_applied.toBool() ||
        app_shell.interfaceDensity() != QStringLiteral("comfortable")) {
      return fail("Restoring a panel layout should preserve the global density choice");
    }
    if (!show_studio_workspace("timelineWorkspaceIndex", QStringLiteral("timelineWorkspace"))) {
      return fail("Studio polish should reach the populated Timeline workspace");
    }
    if (auto* recovery = find_named_object(studio_root, QStringLiteral("timelineRecoveryPopup"));
        recovery && recovery->property("visible").toBool()) {
      QMetaObject::invokeMethod(recovery, "close");
      process_layout_events(4);
    }
    if (!wait_for_condition([&] {
          auto* recovery = find_named_object(studio_root, QStringLiteral("timelineRecoveryPopup"));
          if (recovery && recovery->property("visible").toBool()) {
            QMetaObject::invokeMethod(recovery, "close");
            return false;
          }
          return true;
        }, 3000)) {
      return fail("Studio polish should dismiss its fixture recovery dialog before captures");
    }
    auto* timeline = find_named_object(studio_root, QStringLiteral("timelineWorkspace"));
    const QStringList studio_presets{QStringLiteral("edit"), QStringLiteral("color"),
                                     QStringLiteral("audio"), QStringLiteral("finish")};
    bool finish_tools_pointer_verified = false;
    for (const auto& density : densities) {
      app_shell.setInterfaceDensity(density);
      for (const auto& preset : studio_presets) {
        if (!QMetaObject::invokeMethod(timeline, "applyTimelineLayoutPreset",
                                      Q_ARG(QVariant, QVariant(preset)))) {
          return fail("Timeline should expose its task layout selection");
        }
        process_layout_events(8);
        if (timeline->property("timelineLayoutPreset").toString() != preset ||
            app_shell.interfaceDensity() != density) {
          return fail("Timeline task layouts should preserve global density");
        }
        QStringList required{QStringLiteral("timelineProgramMonitor"), QStringLiteral("timelineViewport"),
                             QStringLiteral("timelineLayoutPresetButton")};
        if (preset == QStringLiteral("audio")) {
          required.append(QStringLiteral("timelineStudioAudioDock"));
          required.append(QStringLiteral("timelineStudioAudioMixerTitle"));
        } else if (preset == QStringLiteral("color")) {
          required.append(QStringLiteral("timelineVideoColorInspector"));
          required.append(QStringLiteral("timelineDockedProgramScopes"));
        } else if (preset == QStringLiteral("finish")) {
          required.append(QStringLiteral("timelineEffectsInspector"));
          required.append(QStringLiteral("timelineWorkspaceToolsButton"));
        }
        if (!wait_for_condition([&] {
              return std::all_of(required.cbegin(), required.cend(), [&](const QString& name) {
                return named_object_visible_for_proof(studio_root, name);
              });
            }, 15000)) {
          std::cerr << "Studio loading: preset=" << preset.toStdString()
                    << " density=" << density.toStdString() << '\n';
          for (const auto& name : required) {
            auto* object = find_named_object(studio_root, name);
            std::cerr << name.toStdString() << " exists=" << (object != nullptr)
                      << " visible=" << named_object_visible_for_proof(studio_root, name) << '\n';
            for (auto* item = qobject_cast<QQuickItem*>(object); item; item = item->parentItem()) {
              std::cerr << "  ancestor=" << item->objectName().toStdString()
                        << " visible=" << item->isVisible() << " opacity=" << item->opacity()
                        << " size=" << item->width() << 'x' << item->height() << '\n';
            }
          }
          capture_visual_proof(studio_window, studio_root, proof_dir,
              QStringLiteral("failed-%1-%2").arg(preset, density),
              studio_window->width(), studio_window->height(), {}, &studio_manifest);
          return fail("Timeline task layout should finish loading its visible tools before capture");
        }
        if (preset == QStringLiteral("finish") && !finish_tools_pointer_verified) {
          auto* tools_button = find_named_item(
              studio_root, QStringLiteral("timelineWorkspaceToolsButton"));
          if (!click_studio_item(tools_button) ||
              !wait_for_condition([&] {
                auto* popup = find_named_object(
                    studio_root, QStringLiteral("timelineWorkspaceToolsButtonPopup"));
                return popup && popup->property("opened").toBool() &&
                       named_object_visible_for_proof(
                           studio_root, QStringLiteral("timelineWorkspaceToolsButtonPopupItem0"));
              }, 3000) ||
              !capture_visual_proof(
                  studio_window, studio_root, proof_dir,
                  QStringLiteral("timeline-finish-tools-menu"),
                  studio_window->width(), studio_window->height(),
                  required + QStringList{QStringLiteral("timelineWorkspaceToolsButtonPopupItem0")},
                  &studio_manifest) ||
              !click_studio_item(find_named_item(
                  studio_root, QStringLiteral("timelineWorkspaceToolsButtonPopupItem0"))) ||
              !wait_for_condition([&] {
                auto* captions = find_named_object(
                    studio_root, QStringLiteral("timelineCaptionGeneratorPopup"));
                return captions && captions->property("visible").toBool();
              }, 5000) ||
              !send_window_key(studio_window, Qt::Key_Escape) ||
              !wait_for_condition([&] {
                auto* captions = find_named_object(
                    studio_root, QStringLiteral("timelineCaptionGeneratorPopup"));
                return captions && !captions->property("visible").toBool();
              }, 3000)) {
            return fail("Finish workspace should open caption tools through its visible Tools menu");
          }
          finish_tools_pointer_verified = true;
        }
        for (const auto& size : studio_sizes) {
          if (!capture_visual_proof(studio_window, studio_root, proof_dir,
                QStringLiteral("timeline-%1-%2-%3").arg(preset, density).arg(size.width()),
                size.width(), size.height(), required, &studio_manifest)) {
            return fail("Timeline task layouts should render without hiding their editing surfaces");
          }
          const QRectF window_bounds(0, 0, studio_window->width(), studio_window->height());
          auto* mode = find_named_item(studio_root, QStringLiteral("timelineEditModeMenuButton"));
          auto* select_tool = find_named_item(studio_root, QStringLiteral("timelineToolMenuButton"));
          if (!item_visible(mode) || !item_visible(select_tool) ||
              !window_bounds.contains(scene_rect(mode)) ||
              std::abs(scene_rect(mode).top() - scene_rect(select_tool).top()) > 2) {
            return fail("Edit mode should remain readable beside Select at every density and size");
          }
          for (const auto& name : required) {
            auto* surface = find_named_item(studio_root, name);
            if (!surface || !window_bounds.contains(scene_rect(surface))) {
              const auto bounds = surface ? scene_rect(surface) : QRectF();
              std::cerr << "Studio bounds: " << name.toStdString() << " preset=" << preset.toStdString()
                        << " density=" << density.toStdString() << " window=" << size.width()
                        << " rect=" << bounds.x() << ',' << bounds.y() << ','
                        << bounds.width() << ',' << bounds.height() << '\n';
              return fail("Timeline studio surfaces should fit inside the window at each density");
            }
          }
          auto* viewport = find_named_item(studio_root, QStringLiteral("timelineViewport"));
          if (!viewport || viewport->width() < 350 || viewport->height() < 100) {
            return fail("Timeline profiles should retain a usable editing timeline");
          }
        }
      }
    }
    app_shell.setReducedMotion(true);
    app_shell.setHighContrast(true);
    if (!capture_visual_proof(studio_window, studio_root, proof_dir,
          QStringLiteral("timeline-accessible"), 1440, 900,
          {QStringLiteral("timelineProgramMonitor"), QStringLiteral("timelineViewport")}, &studio_manifest)) {
      return fail("Studio polish should preserve high contrast and reduced motion");
    }
    if (!wait_for_condition([&]() {
          QSettings settings;
          settings.sync();
          return settings.value(QStringLiteral("TimelineWorkspace/timelineLayoutPreset")).toString() ==
                 QStringLiteral("finish");
        }, 3000)) {
      return fail("Timeline task layout should persist before relaunch");
    }
    delete studio_root;
    studio_root = nullptr;
    studio_window = nullptr;
    process_layout_events(4);
    engine.load(QUrl::fromLocalFile(main_qml_path));
    if (engine.rootObjects().isEmpty()) {
      return fail("Studio polish should reload the application");
    }
    studio_root = engine.rootObjects().constLast();
    studio_window = qobject_cast<QQuickWindow*>(studio_root);
    if (!studio_window || !show_studio_workspace("timelineWorkspaceIndex", QStringLiteral("timelineWorkspace"))) {
      return fail("Studio polish should reopen the Timeline workspace");
    }
    timeline = find_named_object(studio_root, QStringLiteral("timelineWorkspace"));
    if (!timeline || timeline->property("timelineLayoutPreset").toString() != QStringLiteral("finish") ||
        app_shell.interfaceDensity() != QStringLiteral("compact")) {
      std::cerr << "Restarted studio preset="
                << (timeline ? timeline->property("timelineLayoutPreset").toString().toStdString() : "missing")
                << " density=" << app_shell.interfaceDensity().toStdString() << '\n';
      return fail("Timeline profile and global density should survive a window restart");
    }
    QFile manifest_file(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
    if (!manifest_file.open(QIODevice::WriteOnly) ||
        manifest_file.write(QJsonDocument(studio_manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("Studio polish should save its screenshot manifest");
    }
    return EXIT_SUCCESS;

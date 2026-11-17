  {
    app_shell.completeFirstRunExperience();
    const auto source_path = runtime_root.filePath(QStringLiteral("component-workspace-photo.png"));
    QImage source(720, 480, QImage::Format_RGB32);
    for (int y = 0; y < source.height(); ++y) {
      for (int x = 0; x < source.width(); ++x) {
        source.setPixelColor(x, y, QColor(28 + (x * 90 / source.width()),
                                         58 + (y * 105 / source.height()),
                                         92 + ((x + y) * 70 / (source.width() + source.height()))));
      }
    }
    source.setColorSpace(QColorSpace(QColorSpace::SRgb));
    if (!source.save(source_path))
      return fail("Component workspace proof should create its source photograph");
    catalog_assets.replaceAssetsForTesting(
        {make_asset(QStringLiteral("component-workspace-photo"), source_path,
                    QUrl::fromLocalFile(source_path).toString())});
    catalog_assets.selectIndex(0);

    QStringList qml_routing_warnings;
    // The engine outlives this proof block, so the warnings connection must
    // not retain a lambda that captures the local list by reference. Using a
    // block-local context disconnects it before the captured list is destroyed.
    QObject qml_warning_context;
    QObject::connect(&engine, &QQmlEngine::warnings, &qml_warning_context,
                     [&qml_routing_warnings](const QList<QQmlError>& errors) {
      for (const auto& error : errors) {
        const auto description = error.description();
        const auto full_error = error.toString();
        if (description.contains(QStringLiteral("componentWorkspace"), Qt::CaseInsensitive) ||
            description.contains(QStringLiteral("ReferenceError"), Qt::CaseInsensitive) ||
            description.contains(QStringLiteral("TypeError"), Qt::CaseInsensitive) ||
            full_error.contains(QStringLiteral("PhotoComponentWorkspaceBar.qml"), Qt::CaseInsensitive) ||
            full_error.contains(QStringLiteral("componentWorkspace"), Qt::CaseInsensitive))
          qml_routing_warnings.append(full_error);
      }
    });
    engine.load(QUrl::fromLocalFile(main_qml_path));
    if (engine.rootObjects().isEmpty())
      return fail("Component workspace proof should load the actual Main.qml tree");
    auto* proof_root = engine.rootObjects().constFirst();
    auto* proof_window = qobject_cast<QQuickWindow*>(proof_root);
    if (!proof_window)
      return fail("Component workspace proof requires Main.qml's Qt Quick window");
    proof_window->resize(1440, 900);
    proof_window->show();
    proof_root->setProperty("currentWorkspace", proof_root->property("editWorkspaceIndex"));
    if (!wait_for_condition([&] {
          process_layout_events(3);
          return editor_preview.hasAsset() && editor_preview.sourceImageWidth() == 720 &&
                 !editor_preview.processing() && !editor_preview.fullRenderRunning();
        }, 30000))
      return fail("Component workspace proof should load its photographic parent into Edit");

    auto* edit = find_named_object(proof_root, QStringLiteral("editStudioWorkspace"));
    if (!edit || !edit->property("pageContentReady").toBool())
      return fail("Component workspace proof should find the ready Main Studio workspace");
    const auto settle_parent = [&] {
      return wait_for_condition([&] {
        process_layout_events(2);
        return photo_editor::desktop::EditorPreviewControllerTestAccess::preview_idle(editor_preview) &&
               !editor_preview.professionalToolsBusy();
      }, 20000);
    };
    if (!settle_parent())
      return fail("Component workspace parent should settle before the proof edits it");

    const int canvas_width = editor_preview.sourceImageWidth();
    const int canvas_height = editor_preview.sourceImageHeight();
    if (!editor_preview.addPhotoShape(QStringLiteral("rectangle"), 48, 244, 250, 418,
                                      canvas_width, canvas_height,
                                      QStringLiteral("#3f9f85"), QStringLiteral("#f4d38a"), 6.0) ||
        !editor_preview.addPointTextLayer(QStringLiteral("Component title"), 332, 72,
                                          canvas_width, canvas_height) ||
        !settle_parent())
      return fail("Component workspace parent should create editable shape and text contents");
    QVariantList group_indices;
    const auto find_layer_index = [](photo_editor::desktop::EditorPreviewController* controller,
                                     const QString& id) {
      const auto layers = controller->photoLayers();
      for (int index = 0; index < layers.size(); ++index)
        if (layers.at(index).toMap().value(QStringLiteral("id")).toString() == id)
          return index;
      return -1;
    };
    QString shape_id;
    QString text_id;
    for (const auto& value : editor_preview.photoLayers()) {
      const auto layer = value.toMap();
      const auto kind = layer.value(QStringLiteral("kind")).toString();
      if (kind == QStringLiteral("pixel") && !layer.value(QStringLiteral("shapeDefinition")).toMap().isEmpty())
        shape_id = layer.value(QStringLiteral("id")).toString();
      if (kind == QStringLiteral("text")) text_id = layer.value(QStringLiteral("id")).toString();
    }
    const int shape_index = find_layer_index(&editor_preview, shape_id);
    const int text_index = find_layer_index(&editor_preview, text_id);
    if (shape_id.isEmpty() || text_id.isEmpty() || shape_index < 0 || text_index < 0 ||
        std::abs(shape_index - text_index) != 1)
      return fail("Component workspace fixture should create adjacent shape and text rows");
    group_indices.append(shape_index);
    group_indices.append(text_index);
    if (!editor_preview.groupPhotoLayers(group_indices, QStringLiteral("Workflow component")) ||
        !settle_parent())
      return fail("Component workspace proof should group and convert its real parent layers");
    int group_index = -1;
    QString component_layer_id;
    for (int index = 0; index < editor_preview.photoLayers().size(); ++index) {
      const auto layer = editor_preview.photoLayers().at(index).toMap();
      if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("group")) {
        group_index = index;
        component_layer_id = layer.value(QStringLiteral("id")).toString();
        break;
      }
    }
    if (group_index < 0 || component_layer_id.isEmpty() ||
        !editor_preview.convertPhotoLayerToComponent(group_index) || !settle_parent())
      return fail("Component workspace proof should convert its selected source group");
    int component_layer_index = find_layer_index(&editor_preview, component_layer_id);
    if (component_layer_index < 0 ||
        editor_preview.photoLayers().at(component_layer_index).toMap()
            .value(QStringLiteral("smartObjectDocument")).toMap().isEmpty())
      return fail("Converted parent group should retain a component document");

    QVariant selection_result;
    const auto set_active_layer = [&](int index) {
      selection_result.clear();
      return QMetaObject::invokeMethod(edit, "setActivePhotoLayer",
                                       Q_RETURN_ARG(QVariant, selection_result),
                                       Q_ARG(QVariant, QVariant(index))) && selection_result.toBool();
    };
    if (!set_active_layer(component_layer_index))
      return fail("The Main Studio should select the converted group in its existing Layers workspace");
    edit->setProperty("editInspectorTab", QStringLiteral("layers"));
    edit->setProperty("photoLayerDetailsExpanded", true);
    if (!settle_parent())
      return fail("The selected component's Properties inspector should settle");

    const int original_active_layer = edit->property("activePhotoLayerIndex").toInt();
    const auto original_selected_layers = edit->property("selectedPhotoLayerIndices").toList();
    const auto original_selected_target = edit->property("selectedEditTarget").toMap();
    const auto original_tool = edit->property("editTool").toString();
    const auto original_tab = edit->property("editInspectorTab").toString();
    auto* canvas = find_named_item(edit, QStringLiteral("editImageViewport"));
    auto* rows = find_named_object(edit, QStringLiteral("photoLayerRowsRepeater"));
    auto* component_edit_button = find_named_item(edit, QStringLiteral("photoComponentEditButton"));
    if (!canvas || !rows || !component_edit_button || !item_visible(component_edit_button) ||
        !component_edit_button->isEnabled())
      return fail("The actual Studio Properties inspector should expose Edit Contents for the selected component");

    quint64 pointer_timestamp = 1000;
    const auto process_layout = [&] { process_layout_events(3); };
    const auto click_item = [&](QQuickItem* item) {
      auto* target_window = item ? item->window() : nullptr;
      if (!item_visible(item) || !item->isEnabled() || !target_window)
        return false;
      const auto click_point = scene_rect(item).center();
      if (!QRectF(0.0, 0.0, target_window->width(), target_window->height())
               .contains(click_point))
        return false;
      QTest::mouseClick(target_window, Qt::LeftButton, Qt::NoModifier,
                        click_point.toPoint());
      process_layout();
      return true;
    };
    const auto restore_parent_selection = [&] {
      return edit->property("activePhotoLayerIndex").toInt() == original_active_layer &&
          edit->property("selectedPhotoLayerIndices").toList() == original_selected_layers &&
          edit->property("editTool").toString() == original_tool &&
          edit->property("editInspectorTab").toString() == original_tab;
    };
    const auto root_layers_before_child = editor_preview.photoLayers();
    const int root_history_before_child = editor_preview.editHistoryCurrentIndex();
    const auto root_recipe_before_child = photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview);
    const auto parsed_root_patch_before_child = photo_editor::desktop::parse_adjustment_patch_json(
        editor_preview.currentAssetBoundAdjustmentPatchJson());
    if (!parsed_root_patch_before_child.ok())
      return fail("Component workspace proof should parse the parent's canonical layers before opening a child");
    const auto root_authored_layers_before_child = parsed_root_patch_before_child.state->layers;
    const auto parent_authored_layers_match = [&](const QVariantList& expected_layers) {
      const auto parsed = photo_editor::desktop::parse_adjustment_patch_json(
          editor_preview.currentAssetBoundAdjustmentPatchJson());
      return parsed.ok() && parsed.state->layers == expected_layers;
    };
    const auto summarize_authored_layers = [](const QVariantList& layers) {
      QStringList summaries;
      for (int index = 0; index < layers.size() && index < 8; ++index) {
        const auto layer = layers.at(index).toMap();
        summaries.append(QStringLiteral("%1/%2/%3")
            .arg(layer.value(QStringLiteral("id")).toString(),
                 layer.value(QStringLiteral("kind")).toString(),
                 layer.value(QStringLiteral("name")).toString()));
      }
      if (layers.size() > 8) summaries.append(QStringLiteral("..."));
      return summaries.join(QLatin1Char(','));
    };
    const auto* original_viewport = canvas;
    const auto* original_rows = rows;

    if (!click_item(component_edit_button))
      return fail("Edit Contents should open from its real Studio pointer button");
    if (!wait_for_condition([&] {
          process_layout();
          return component_workspace.active() && !component_workspace.busy() &&
                 component_workspace.controller() != &editor_preview &&
                 component_workspace.controller() != nullptr;
        }, 30000))
      return fail("Edit Contents should finish opening an isolated component controller");
    auto* child = component_workspace.controller();
    if (!wait_for_condition([&] {
          process_layout();
          return child->hasAsset() && child->sourceImageWidth() == canvas_width &&
                 photo_editor::desktop::EditorPreviewControllerTestAccess::preview_idle(*child);
        }, 30000))
      return fail("The isolated child document should render before receiving canvas input");
    if (edit->property("editorPreviewController").value<QObject*>() != child ||
        find_named_item(edit, QStringLiteral("editImageViewport")) != original_viewport ||
        find_named_object(edit, QStringLiteral("photoLayerRowsRepeater")) != original_rows)
      return fail("Main should retarget its existing Studio canvas and Layers stack to the child controller");
    auto* shared_mask_overlay = find_named_object(
        edit, QStringLiteral("editImageViewportSharedMaskOverlay"));
    if (!shared_mask_overlay ||
        shared_mask_overlay->property("previewController").value<QObject*>() != child)
      return fail("The shared mask overlay should route its geometry and pointer context to the active child controller");
    if (!parent_authored_layers_match(root_authored_layers_before_child) ||
        editor_preview.editHistoryCurrentIndex() != root_history_before_child ||
        photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview) != root_recipe_before_child)
      return fail("Opening a component should leave the parent document and history unchanged");

    if (!QMetaObject::invokeMethod(edit, "activateEditTool",
                                   Q_ARG(QVariant, QVariant(QStringLiteral("layers")))))
      return fail("The component retouch routing check should start from the Layers tool");
    edit->setProperty("editInspectorTab", QStringLiteral("layers"));
    if (!wait_for_condition([&] {
          process_layout();
          return edit->property("editTool").toString() == QStringLiteral("layers") &&
              edit->property("editInspectorTab").toString() == QStringLiteral("layers") &&
              !edit->property("editToolTransitionActive").toBool() &&
              photo_editor::desktop::EditorPreviewControllerTestAccess::preview_idle(*child);
        }, 10000))
      return fail("The component retouch routing check should settle in Layers before opening a draft");

    const auto child_recipe_before_retouch_route = child->currentAssetBoundAdjustmentPatchJson();
    const auto child_masks_before_retouch_route = child->maskStack();
    const int child_active_mask_before_retouch_route = child->activeMaskIndex();
    const bool child_had_active_mask_before_retouch_route = child->hasActiveMask();
    QVariant retouch_menu_value;
    if (!QMetaObject::invokeMethod(edit, "retouchHudModeMenuItems",
                                   Q_RETURN_ARG(QVariant, retouch_menu_value)))
      return fail("The Main retouch menu should expose its per-mode capability state");
    const auto retouch_menu_items = retouch_menu_value.metaType() == QMetaType::fromType<QJSValue>()
        ? retouch_menu_value.value<QJSValue>().toVariant().toList()
        : retouch_menu_value.toList();
    const auto retouch_menu_mode_enabled = [&](const QString& key, bool* enabled) {
      if (!enabled) return false;
      for (const auto& value : retouch_menu_items) {
        const auto item = value.toMap();
        if (item.value(QStringLiteral("key")).toString() != key) continue;
        *enabled = item.value(QStringLiteral("enabled")).toBool();
        return true;
      }
      return false;
    };
    bool clone_menu_enabled = false;
    bool dust_menu_enabled = true;
    bool red_eye_menu_enabled = true;
    if (!retouch_menu_mode_enabled(QStringLiteral("retouch-clone"), &clone_menu_enabled) ||
        !retouch_menu_mode_enabled(QStringLiteral("retouch-dust"), &dust_menu_enabled) ||
        !retouch_menu_mode_enabled(QStringLiteral("retouch-red-eye"), &red_eye_menu_enabled) ||
        !clone_menu_enabled || dust_menu_enabled || red_eye_menu_enabled)
      return fail("A valid component should enable Clone Stamp and disable source-photo Dust and Red Eye menu items");

    if (!QMetaObject::invokeMethod(edit, "handleMaskToolMenuAction",
                                   Q_ARG(QVariant, QVariant(QStringLiteral("retouch-clone")))) ||
        !wait_for_condition([&] {
          process_layout();
          if (!child->hasActiveMask() || child->activeMaskToolRole() != QStringLiteral("retouch") ||
              child->activeMaskRetouchMode() != QStringLiteral("clone") ||
              child->activeMaskCommitted() || child->activeMaskIndex() < 0)
            return false;
          const auto draft = child->maskStack().value(child->activeMaskIndex()).toMap();
          return draft.value(QStringLiteral("componentPostCompositeRetouchVersion")).toInt() == 1 &&
              draft.value(QStringLiteral("componentRetouchCanvasWidth")).toInt() == child->sourceImageWidth() &&
              draft.value(QStringLiteral("componentRetouchCanvasHeight")).toInt() == child->sourceImageHeight() &&
              edit->property("editTool").toString() == QStringLiteral("retouch") &&
              !edit->property("editToolTransitionActive").toBool() &&
              photo_editor::desktop::EditorPreviewControllerTestAccess::preview_idle(*child) &&
              !child->professionalToolsBusy();
        }, 10000))
      return fail("The real Main mask-menu route should start a versioned editable Clone draft on the child");
    const auto clone_draft_masks = child->maskStack();
    const auto clone_draft_recipe = child->currentAssetBoundAdjustmentPatchJson();
    const int clone_draft_history = child->editHistoryCurrentIndex();
    for (const auto& unsupported_key : {QStringLiteral("retouch-red-eye"),
                                        QStringLiteral("retouch-dust")}) {
      if (!QMetaObject::invokeMethod(edit, "handleMaskToolMenuAction",
                                     Q_ARG(QVariant, QVariant(unsupported_key))))
        return fail("The Main mask-menu route should handle source-photo-only retouch keys");
      process_layout();
      if (child->maskStack() != clone_draft_masks ||
          child->currentAssetBoundAdjustmentPatchJson() != clone_draft_recipe ||
          child->editHistoryCurrentIndex() != clone_draft_history ||
          child->activeMaskToolRole() != QStringLiteral("retouch") ||
          child->activeMaskRetouchMode() != QStringLiteral("clone") ||
          edit->property("editTool").toString() != QStringLiteral("retouch"))
        return fail("Rejected Red Eye and Dust menu routes must preserve the active Clone draft and child history exactly");
    }
    child->discardActiveRetouch();
    if (!wait_for_condition([&] {
          process_layout();
          return child->maskStack() == child_masks_before_retouch_route &&
              child->currentAssetBoundAdjustmentPatchJson() == child_recipe_before_retouch_route &&
              child->activeMaskIndex() == child_active_mask_before_retouch_route &&
              child->hasActiveMask() == child_had_active_mask_before_retouch_route &&
              photo_editor::desktop::EditorPreviewControllerTestAccess::preview_idle(*child);
        }, 10000))
      return fail("Discarding the routed Clone draft should restore the exact original child recipe and masks");
    if (!QMetaObject::invokeMethod(edit, "activateEditTool",
                                   Q_ARG(QVariant, QVariant(QStringLiteral("layers")))))
      return fail("The Main Studio should restore the Layers tool after discarding the routed retouch draft");
    edit->setProperty("editInspectorTab", QStringLiteral("layers"));
    if (!wait_for_condition([&] {
          process_layout();
          return edit->property("editTool").toString() == QStringLiteral("layers") &&
              edit->property("editInspectorTab").toString() == QStringLiteral("layers") &&
              !edit->property("editToolTransitionActive").toBool() &&
              child->maskStack() == child_masks_before_retouch_route &&
              child->currentAssetBoundAdjustmentPatchJson() == child_recipe_before_retouch_route &&
              child->activeMaskIndex() == child_active_mask_before_retouch_route &&
              child->hasActiveMask() == child_had_active_mask_before_retouch_route &&
              editor_preview.editHistoryCurrentIndex() == root_history_before_child &&
              photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview) ==
                  root_recipe_before_child &&
              photo_editor::desktop::EditorPreviewControllerTestAccess::preview_idle(*child);
        }, 10000))
      return fail("Discarding the routed retouch draft should restore Layers and leave the parent history and recipe unchanged");

    proof_window->resize(960, 700);
    if (!wait_for_condition([&] {
          process_layout();
          auto* bar = find_named_item(proof_root, QStringLiteral("photoComponentWorkspaceBar"));
          auto* apply = find_named_item(proof_root, QStringLiteral("photoComponentWorkspaceApplyButton"));
          auto* cancel = find_named_item(proof_root, QStringLiteral("photoComponentWorkspaceCancelButton"));
          return item_visible(bar) && item_visible(apply) && item_visible(cancel) && apply->isEnabled();
        }, 10000))
      return fail("The compact Main window should show an actionable component breadcrumb bar");
    auto* bar = find_named_item(proof_root, QStringLiteral("photoComponentWorkspaceBar"));
    auto* apply_button = find_named_item(proof_root, QStringLiteral("photoComponentWorkspaceApplyButton"));
    auto* cancel_button = find_named_item(proof_root, QStringLiteral("photoComponentWorkspaceCancelButton"));
    const auto apply_rect = scene_rect(apply_button);
    const auto cancel_rect = scene_rect(cancel_button);
    const auto proof_dir = qEnvironmentVariableIsSet("PHOTO_EDITOR_VISUAL_PROOF_DIR")
        ? QString::fromUtf8(qgetenv("PHOTO_EDITOR_VISUAL_PROOF_DIR"))
        : runtime_root.filePath(QStringLiteral("component-workspace-proof"));
    if (!QDir().mkpath(proof_dir))
      return fail("Component workspace proof should create its screenshot directory");
    if (!bar || scene_rect(canvas).top() < scene_rect(bar).bottom() ||
        apply_rect.intersects(cancel_rect) ||
        !proof_window->grabWindow().save(QDir(proof_dir).filePath(
            QStringLiteral("01-main-component-workspace-compact.png")))) {
      proof_window->grabWindow().save(QDir(proof_dir).filePath(QStringLiteral("component-workspace-layout-failure.png")));
      return fail("Compact component controls should fit above the unchanged Main canvas and produce a PNG capture");
    }

    const auto rect_summary = [](const QRectF& rect) {
      return QStringLiteral("%1,%2 %3x%4")
          .arg(rect.x(), 0, 'f', 1).arg(rect.y(), 0, 'f', 1)
          .arg(rect.width(), 0, 'f', 1).arg(rect.height(), 0, 'f', 1);
    };
    const auto record_pointer_failure = [&](const QString& reason,
                                            const QPointF& gesture_target_start = QPointF(),
                                            const QPointF& gesture_target_end = QPointF()) {
      const auto failure_path = QDir(proof_dir).filePath(
          QStringLiteral("failure.png"));
      const auto failure_frame = proof_window->grabWindow();
      if (failure_frame.isNull() || !failure_frame.save(failure_path))
        std::cerr << "Could not save pointer failure capture to "
                  << failure_path.toStdString() << '\n';
      const QRectF painted_local(
          canvas->property("paintedContentX").toDouble(),
          canvas->property("paintedContentY").toDouble(),
          canvas->property("paintedContentWidth").toDouble(),
          canvas->property("paintedContentHeight").toDouble());
      const auto painted_scene = canvas->mapRectToScene(painted_local);
      auto* frame_item = find_named_item(edit, QStringLiteral("photoLayerTransformFrame"));
      auto* overlay_item = find_named_item(edit, QStringLiteral("photoLayerTransformOverlay"));
      const int active_index = edit->property("activePhotoLayerIndex").toInt();
      const auto child_layers = child->photoLayers();
      const auto active_layer = active_index >= 0 && active_index < child_layers.size()
          ? child_layers.at(active_index).toMap() : QVariantMap{};
      std::cerr << "Component canvas pointer failure: " << reason.toStdString()
                << " window=" << rect_summary(QRectF(0, 0, proof_window->width(), proof_window->height())).toStdString()
                << " canvas=" << rect_summary(scene_rect(canvas)).toStdString()
                << " paintedLocal=" << rect_summary(painted_local).toStdString()
                << " paintedScene=" << rect_summary(painted_scene).toStdString()
                << " frame=" << rect_summary(frame_item ? scene_rect(frame_item) : QRectF()).toStdString()
                << " transformOverlay=" << rect_summary(overlay_item ? scene_rect(overlay_item) : QRectF()).toStdString()
                << " gestureTargetStart=" << gesture_target_start.x() << ',' << gesture_target_start.y()
                << " gestureTargetEnd=" << gesture_target_end.x() << ',' << gesture_target_end.y()
                << " activeIndex=" << active_index
                << " activeLayer={id:" << active_layer.value(QStringLiteral("id")).toString().toStdString()
                << ",kind:" << active_layer.value(QStringLiteral("kind")).toString().toStdString()
                << ",x:" << active_layer.value(QStringLiteral("x")).toDouble()
                << ",y:" << active_layer.value(QStringLiteral("y")).toDouble()
                << ",width:" << active_layer.value(QStringLiteral("pixelWidth")).toDouble()
                << ",height:" << active_layer.value(QStringLiteral("pixelHeight")).toDouble()
                << ",scale:" << active_layer.value(QStringLiteral("scale")).toDouble()
                << ",rotation:" << active_layer.value(QStringLiteral("rotation")).toDouble()
                << "} mask={tool:" << edit->property("editTool").toString().toStdString()
                << ",kind:" << child->activeMaskKind().toStdString()
                << ",hasWorkingSelection:" << child->hasWorkingSelection()
                << ",sessionActive:" << child->selectionEditSessionActive()
                << ",brushPoints:" << child->activeMaskBrushPointCount()
                << ",maskCount:" << child->maskCount()
                << "} capture=" << failure_path.toStdString() << '\n';
      for (int index = 0; index < child_layers.size(); ++index) {
        const auto layer = child_layers.at(index).toMap();
        std::cerr << "  childLayer[" << index << "] id="
                  << layer.value(QStringLiteral("id")).toString().toStdString()
                  << " kind=" << layer.value(QStringLiteral("kind")).toString().toStdString()
                  << " x=" << layer.value(QStringLiteral("x")).toDouble()
                  << " y=" << layer.value(QStringLiteral("y")).toDouble()
                  << " size=" << layer.value(QStringLiteral("pixelWidth")).toDouble()
                  << 'x' << layer.value(QStringLiteral("pixelHeight")).toDouble()
                  << " scale=" << layer.value(QStringLiteral("scale")).toDouble()
                  << " rotation=" << layer.value(QStringLiteral("rotation")).toDouble()
                  << " text=" << layer.value(QStringLiteral("text")).toString().toStdString()
                  << '\n';
      }
      return false;
    };

    const auto find_layer_row = [&](photo_editor::desktop::EditorPreviewController* controller,
                                    const QString& id) -> QQuickItem* {
      (void)controller;
      for (auto* candidate : find_named_items(edit, QStringLiteral("photoLayerRow"))) {
        if (candidate->property("layerData").toMap().value(QStringLiteral("id")).toString() == id)
          return candidate;
      }
      return nullptr;
    };
    const auto find_child_text = [&]() -> QVariantMap {
      for (int index = 0; index < child->photoLayers().size(); ++index) {
        const auto layer = child->photoLayers().at(index).toMap();
        if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("text"))
          return layer;
      }
      return {};
    };
    const auto move_child_text = [&]() {
      const auto text_layer = find_child_text();
      const auto text_id = text_layer.value(QStringLiteral("id")).toString();
      if (text_id.isEmpty())
        return record_pointer_failure(QStringLiteral("child text layer not found"));
      auto* row = find_layer_row(child, text_id);
      auto* name = row ? find_named_item(row, QStringLiteral("photoLayerNameLabel")) : nullptr;
      if (!name || !click_item(name) ||
          !wait_for_condition([&] {
            process_layout();
            return edit->property("activePhotoLayerIndex").toInt() >= 0 &&
                child->photoLayers().value(edit->property("activePhotoLayerIndex").toInt()).toMap()
                    .value(QStringLiteral("id")).toString() == text_id;
          }, 5000))
        return record_pointer_failure(QStringLiteral("child text row could not be selected"));
      auto* transform_frame = find_named_item(edit, QStringLiteral("photoLayerTransformFrame"));
      auto* transform_overlay = find_named_item(edit, QStringLiteral("photoLayerTransformOverlay"));
      if (!transform_frame || !transform_overlay ||
          transform_overlay->property("layerIndex").toInt() != edit->property("activePhotoLayerIndex").toInt())
        return record_pointer_failure(QStringLiteral("selected text transform frame is unavailable"));
      const auto start = scene_rect(transform_frame).center();
      const auto end = start + QPointF(42, 24);
      const auto window_bounds = QRectF(0.0, 0.0, proof_window->width(), proof_window->height());
      if (!window_bounds.contains(start) || !window_bounds.contains(end))
        return record_pointer_failure(QStringLiteral("text transform gesture target is outside the visible window"),
                                      start, end);
      const auto send_pointer = [&](QEvent::Type type, const QPointF& point) {
        const auto global = QPointF(proof_window->mapToGlobal(point.toPoint()));
        QMouseEvent event(type, point, global,
                          type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                          Qt::NoModifier);
        event.setTimestamp(pointer_timestamp++);
        QCoreApplication::sendEvent(proof_window, &event);
        process_layout();
      };
      const double before_x = text_layer.value(QStringLiteral("x")).toDouble();
      send_pointer(QEvent::MouseButtonPress, start);
      for (int step = 1; step <= 8; ++step)
        send_pointer(QEvent::MouseMove, start + (end - start) * (static_cast<double>(step) / 8.0));
      send_pointer(QEvent::MouseButtonRelease, end);
      if (!wait_for_condition([&] {
        process_layout();
        const auto after = find_child_text();
        return !after.isEmpty() && std::abs(after.value(QStringLiteral("x")).toDouble() - before_x) > 8.0 &&
            !child->processing() && !child->fullRenderRunning();
      }, 15000))
        return record_pointer_failure(QStringLiteral("child text transform drag did not move its layer"),
                                      start, end);
      return true;
    };

    const auto parent_recipe_before_mask_gesture =
        photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview);
    const auto parent_masks_before_mask_gesture = editor_preview.maskStack();
    const auto parent_working_masks_before_gesture = editor_preview.workingSelectionMasks();
    const bool parent_had_working_selection_before_gesture = editor_preview.hasWorkingSelection();
    const int parent_history_before_mask_gesture = editor_preview.editHistoryCurrentIndex();
    const auto parent_preview_source_for_mask_gesture = QUrl(editor_preview.displayPreviewSource());
    auto parent_preview_key_for_mask_gesture = parent_preview_source_for_mask_gesture.path();
    if (parent_preview_key_for_mask_gesture.startsWith(QLatin1Char('/')))
      parent_preview_key_for_mask_gesture.remove(0, 1);
    const auto parent_pixels_before_mask_gesture =
        editor_preview_image_provider->image(parent_preview_key_for_mask_gesture);
    if (parent_pixels_before_mask_gesture.isNull()) {
      record_pointer_failure(QStringLiteral("parent provider pixels unavailable before mask gesture"));
      return fail("The parent preview provider should expose exact pixels before child mask authoring");
    }

    if (!QMetaObject::invokeMethod(edit, "activateEditTool",
                                   Q_ARG(QVariant, QVariant(QStringLiteral("masks"))))) {
      record_pointer_failure(QStringLiteral("could not invoke child Masks tool"));
      return fail("The actual Studio should activate its Masks tool for child selection authoring");
    }
    if (!wait_for_condition([&] {
          process_layout();
          return edit->property("editTool").toString() == QStringLiteral("masks");
        }, 5000) ||
        !QMetaObject::invokeMethod(edit, "createMaskSource",
                                   Q_ARG(QVariant, QVariant(QStringLiteral("rectangle"))))) {
      record_pointer_failure(QStringLiteral("could not create child rectangle selection source"));
      return fail("The actual Studio Masks tool should create a child rectangle selection source");
    }
    if (!wait_for_condition([&] {
          process_layout();
          return child->hasWorkingSelection() && child->selectionEditSessionActive() &&
              child->activeMaskKind() == QStringLiteral("rectangle") &&
              child->activeMaskIndex() >= 0;
        }, 5000)) {
      record_pointer_failure(QStringLiteral("rectangle source did not create a child working selection"));
      return fail("Creating a rectangle source should begin an editable child working selection");
    }
    if (!QMetaObject::invokeMethod(edit, "startMaskBrushSource",
                                   Q_ARG(QVariant, QVariant(QStringLiteral("add"))))) {
      record_pointer_failure(QStringLiteral("could not add child selection brush source"));
      return fail("The actual Studio should add a selection brush source to the child working selection");
    }
    if (!wait_for_condition([&] {
          process_layout();
          return edit->property("editTool").toString() == QStringLiteral("brush") &&
              child->activeMaskKind() == QStringLiteral("brush") &&
              child->hasWorkingSelection();
        }, 5000)) {
      record_pointer_failure(QStringLiteral("child selection brush source did not activate"));
      return fail("The child should expose the newly added selection brush as the active working selection");
    }

    const auto local_canvas_bounds = QRectF(0.0, 0.0, canvas->width(), canvas->height());
    const QRectF painted_content(
        canvas->property("paintedContentX").toDouble(),
        canvas->property("paintedContentY").toDouble(),
        canvas->property("paintedContentWidth").toDouble(),
        canvas->property("paintedContentHeight").toDouble());
    const auto window_bounds = QRectF(0.0, 0.0, proof_window->width(), proof_window->height());
    const auto painted_content_scene = canvas->mapRectToScene(painted_content);
    const auto selection_start_local = painted_content.topLeft() +
        QPointF(painted_content.width() * 0.34, painted_content.height() * 0.54);
    const auto selection_end_local = painted_content.topLeft() +
        QPointF(painted_content.width() * 0.46, painted_content.height() * 0.55);
    const auto selection_start = canvas->mapToScene(selection_start_local);
    const auto selection_end = canvas->mapToScene(selection_end_local);
    if (painted_content.width() <= 20.0 || painted_content.height() <= 20.0 ||
        !local_canvas_bounds.contains(selection_start_local) ||
        !local_canvas_bounds.contains(selection_end_local) ||
        !painted_content_scene.intersects(window_bounds) ||
        !window_bounds.contains(selection_start) || !window_bounds.contains(selection_end)) {
      record_pointer_failure(QStringLiteral("painted-content target mapped outside the visible canvas"),
                             selection_start, selection_end);
      return fail("Child selection brush pointer targets should map inside the visible painted image and window");
    }
    const auto send_selection_pointer = [&](QEvent::Type type, const QPointF& point) {
      const auto global = QPointF(proof_window->mapToGlobal(point.toPoint()));
      QMouseEvent event(type, point, global,
                        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                        Qt::NoModifier);
      event.setTimestamp(pointer_timestamp++);
      QCoreApplication::sendEvent(proof_window, &event);
      process_layout();
    };
    send_selection_pointer(QEvent::MouseButtonPress, selection_start);
    for (int step = 1; step <= 8; ++step) {
      const auto fraction = static_cast<double>(step) / 8.0;
      send_selection_pointer(QEvent::MouseMove,
                             selection_start + (selection_end - selection_start) * fraction);
    }
    const auto live_selection_brush_points = child->activeMaskLiveBrushPoints();
    const auto first_live_brush_point = live_selection_brush_points.isEmpty()
        ? QVariantMap{} : live_selection_brush_points.first().toMap();
    const auto last_live_brush_point = live_selection_brush_points.isEmpty()
        ? QVariantMap{} : live_selection_brush_points.last().toMap();
    const auto brush_geometry_matches_pointer = live_selection_brush_points.size() >= 3 &&
        std::abs(first_live_brush_point.value(QStringLiteral("x")).toDouble() - 0.34) < 0.08 &&
        std::abs(first_live_brush_point.value(QStringLiteral("y")).toDouble() - 0.54) < 0.08 &&
        std::abs(last_live_brush_point.value(QStringLiteral("x")).toDouble() - 0.46) < 0.08 &&
        std::abs(last_live_brush_point.value(QStringLiteral("y")).toDouble() - 0.55) < 0.08 &&
        last_live_brush_point.value(QStringLiteral("x")).toDouble() >
            first_live_brush_point.value(QStringLiteral("x")).toDouble();
    if (!brush_geometry_matches_pointer) {
      record_pointer_failure(QStringLiteral("live child brush geometry did not match mapped pointer coordinates"),
                             selection_start, selection_end);
      return fail("The child working-selection brush should expose real geometry at the mapped pointer path");
    }
    send_selection_pointer(QEvent::MouseButtonRelease, selection_end);
    if (!wait_for_condition([&] {
          process_layout();
          return !child->brushStrokeActive() && !child->photoPaintBusy() &&
              !child->selectionWorkBusy() && child->activeMaskBrushPointCount() >= 3 &&
              child->selectionEditSessionDirty();
        }, 15000) || !child->hasWorkingSelection() ||
        child->workingSelectionMasks().isEmpty()) {
      record_pointer_failure(QStringLiteral("real canvas drag did not create editable child selection coverage"),
                             selection_start, selection_end);
      return fail("A real child canvas drag should paint editable coverage into the working selection");
    }
    if (editor_preview.hasWorkingSelection() != parent_had_working_selection_before_gesture ||
        !parent_authored_layers_match(root_authored_layers_before_child) ||
        editor_preview.maskStack() != parent_masks_before_mask_gesture ||
        editor_preview.workingSelectionMasks() != parent_working_masks_before_gesture ||
        editor_preview.editHistoryCurrentIndex() != parent_history_before_mask_gesture ||
        photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview) !=
            parent_recipe_before_mask_gesture ||
        editor_preview_image_provider->image(parent_preview_key_for_mask_gesture) !=
            parent_pixels_before_mask_gesture) {
      record_pointer_failure(QStringLiteral("child mask gesture changed parent state"),
                             selection_start, selection_end);
      return fail("Pointer-authored child selection should leave parent selection, layers, masks, history, recipe, and pixels unchanged");
    }
    if (!child->finishSelectionEditSession() &&
        !wait_for_condition([&] {
          process_layout();
          return !child->selectionEditSessionActive();
        }, 15000)) {
      record_pointer_failure(QStringLiteral("child selection session did not finish"),
                             selection_start, selection_end);
      return fail("The child selection edit session should finish after its real canvas gesture");
    }
    if (!QMetaObject::invokeMethod(edit, "activateEditTool",
                                   Q_ARG(QVariant, QVariant(QStringLiteral("layers"))))) {
      record_pointer_failure(QStringLiteral("could not activate child Layers tool after selection"),
                             selection_start, selection_end);
      return fail("The child Studio should return to Layers after selection authoring");
    }
    edit->setProperty("editInspectorTab", QStringLiteral("layers"));
    if (!wait_for_condition([&] {
          process_layout();
          return edit->property("editTool").toString() == QStringLiteral("layers") &&
              edit->property("editInspectorTab").toString() == QStringLiteral("layers") &&
              !child->selectionEditSessionActive() &&
              photo_editor::desktop::EditorPreviewControllerTestAccess::preview_idle(*child);
        }, 15000)) {
      record_pointer_failure(QStringLiteral("child selection did not settle in Layers"),
                             selection_start, selection_end);
      return fail("The child selection should be finished and the Studio Layers workspace ready for layer gestures");
    }
    if (editor_preview.hasWorkingSelection() != parent_had_working_selection_before_gesture ||
        !parent_authored_layers_match(root_authored_layers_before_child) ||
        editor_preview.maskStack() != parent_masks_before_mask_gesture ||
        editor_preview.workingSelectionMasks() != parent_working_masks_before_gesture ||
        editor_preview.editHistoryCurrentIndex() != parent_history_before_mask_gesture ||
        photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview) !=
            parent_recipe_before_mask_gesture ||
        editor_preview_image_provider->image(parent_preview_key_for_mask_gesture) !=
            parent_pixels_before_mask_gesture) {
      record_pointer_failure(QStringLiteral("finishing child selection changed parent state"),
                             selection_start, selection_end);
      return fail("Finishing child selection authoring should still leave every parent document state unchanged");
    }

    if (!move_child_text())
      return fail("A real canvas pointer drag should move the selected child text layer");
    const auto child_text_after_move = find_child_text();
    const int child_history_after_move = child->editHistoryCurrentIndex();
    if (child_history_after_move <= 0 ||
        !parent_authored_layers_match(root_authored_layers_before_child) ||
        editor_preview.editHistoryCurrentIndex() != root_history_before_child ||
        photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview) != root_recipe_before_child)
      return fail("Canvas edits should affect only the child document and its isolated history");
    auto* undo_action = find_named_object(proof_root, QStringLiteral("mainPhotoUndoAction"));
    auto* redo_action = find_named_object(proof_root, QStringLiteral("mainPhotoRedoAction"));
    // Main uses Qt's portable Ctrl shortcut token, which maps to Command on macOS.
    const auto primary_modifier = Qt::ControlModifier;
    if (!undo_action || !undo_action->property("enabled").toBool())
      return fail("Main Undo should become enabled from the child's edit history");
    QTest::keyClick(proof_window, Qt::Key_Z, primary_modifier);
    if (!wait_for_condition([&] {
          process_layout();
          return child->editHistoryCurrentIndex() < child_history_after_move && !child->processing();
        }, 10000))
      return fail("Main keyboard Undo should target the active component child");
    const auto child_text_after_undo = find_child_text();
    if (std::abs(child_text_after_undo.value(QStringLiteral("x")).toDouble() -
                 child_text_after_move.value(QStringLiteral("x")).toDouble()) < 8.0 ||
        !parent_authored_layers_match(root_authored_layers_before_child) ||
        editor_preview.editHistoryCurrentIndex() != root_history_before_child)
      return fail("Main Undo should restore child pixels and leave the parent history untouched");
    if (!redo_action || !redo_action->property("enabled").toBool())
      return fail("Main Redo should be available for the child pointer edit");
    QTest::keyClick(proof_window, Qt::Key_Z, primary_modifier | Qt::ShiftModifier);
    if (!wait_for_condition([&] {
          process_layout();
          const auto current = find_child_text();
          return std::abs(current.value(QStringLiteral("x")).toDouble() -
                          child_text_after_move.value(QStringLiteral("x")).toDouble()) < 1.0 &&
              !child->processing();
        }, 10000))
      return fail("Main keyboard Redo should restore the child pointer edit");
    if (!component_workspace.canApply() || !click_item(apply_button))
      return fail("Apply should publish the ready child through the real Main bar pointer action");
    if (!wait_for_condition([&] {
          process_layout();
          return !component_workspace.active() && !component_workspace.busy() &&
              !editor_preview.processing() && !editor_preview.professionalToolsBusy();
        }, 30000))
      return fail("Apply should finish the isolated component publish and return to its parent");
    if (editor_preview.editHistoryCurrentIndex() != root_history_before_child + 1 ||
        edit->property("editorPreviewController").value<QObject*>() != &editor_preview ||
        find_named_item(edit, QStringLiteral("editImageViewport")) != original_viewport ||
        find_named_object(edit, QStringLiteral("photoLayerRowsRepeater")) != original_rows ||
        !restore_parent_selection()) {
      const int history_after_apply = editor_preview.editHistoryCurrentIndex();
      auto* current_controller = edit->property("editorPreviewController").value<QObject*>();
      auto* current_viewport = find_named_item(edit, QStringLiteral("editImageViewport"));
      auto* current_rows = find_named_object(edit, QStringLiteral("photoLayerRowsRepeater"));
      const int current_active_layer = edit->property("activePhotoLayerIndex").toInt();
      const auto current_selected_layers = edit->property("selectedPhotoLayerIndices").toList();
      const auto current_selected_target = edit->property("selectedEditTarget").toMap();
      const auto current_tool = edit->property("editTool").toString();
      const auto current_tab = edit->property("editInspectorTab").toString();
      const bool active_matches = current_active_layer == original_active_layer;
      const bool selected_matches = current_selected_layers == original_selected_layers;
      const bool tool_matches = current_tool == original_tool;
      const bool tab_matches = current_tab == original_tab;
      const auto format_indices = [](const QVariantList& indices) {
        QStringList formatted;
        for (const auto& index : indices)
          formatted.append(index.toString());
        return QStringLiteral("[%1]").arg(formatted.join(QLatin1Char(',')));
      };
      const auto format_target = [](const QVariantMap& target) {
        return QStringLiteral("{scope:%1,layerId:%2,layerIndex:%3,componentId:%4,maskSetId:%5,sourceIndex:%6}")
            .arg(target.value(QStringLiteral("scope")).toString(),
                 target.value(QStringLiteral("layerId")).toString())
            .arg(target.value(QStringLiteral("layerIndex")).toInt())
            .arg(target.value(QStringLiteral("componentId")).toString(),
                 target.value(QStringLiteral("maskSetId")).toString())
            .arg(target.value(QStringLiteral("sourceIndex")).toInt());
      };
      std::cerr << "Apply restoration diagnostics: history=" << history_after_apply
                << " expectedHistory=" << (root_history_before_child + 1)
                << " historyMatches=" << (history_after_apply == root_history_before_child + 1)
                << " controllerMatches=" << (current_controller == &editor_preview)
                << " viewportMatches=" << (current_viewport == original_viewport)
                << " rowsMatches=" << (current_rows == original_rows)
                << " activeMatches=" << active_matches
                << " selectedMatches=" << selected_matches
                << " toolMatches=" << tool_matches
                << " tabMatches=" << tab_matches
                << " savedSelection={active:" << original_active_layer
                << ",selected:" << format_indices(original_selected_layers).toStdString()
                << ",tool:" << original_tool.toStdString()
                << ",tab:" << original_tab.toStdString()
                << ",target:" << format_target(original_selected_target).toStdString()
                << "} currentSelection={active:" << current_active_layer
                << ",selected:" << format_indices(current_selected_layers).toStdString()
                << ",tool:" << current_tool.toStdString()
                << ",tab:" << current_tab.toStdString()
                << ",target:" << format_target(current_selected_target).toStdString()
                << "}\n";
      return fail("Apply should create one parent history entry and restore the same Studio view and selection");
    }
    shared_mask_overlay = find_named_object(
        edit, QStringLiteral("editImageViewportSharedMaskOverlay"));
    if (!shared_mask_overlay ||
        shared_mask_overlay->property("previewController").value<QObject*>() != &editor_preview)
      return fail("Apply should retarget the shared mask overlay to the restored parent controller");
    const int parent_history_after_apply = editor_preview.editHistoryCurrentIndex();
    const auto parsed_parent_patch_after_apply = photo_editor::desktop::parse_adjustment_patch_json(
        editor_preview.currentAssetBoundAdjustmentPatchJson());
    if (!parsed_parent_patch_after_apply.ok())
      return fail("Apply should leave a parseable canonical parent recipe for the second-draft isolation check");
    const auto parent_authored_layers_after_apply = parsed_parent_patch_after_apply.state->layers;
    const auto parent_recipe_after_apply = photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview);
    const auto parent_view_layers_after_apply = editor_preview.photoLayers();
    component_layer_index = find_layer_index(&editor_preview, component_layer_id);
    const auto applied_component = component_layer_index < 0 ? QVariantMap{} :
        editor_preview.photoLayers().at(component_layer_index).toMap()
            .value(QStringLiteral("smartObjectDocument")).toMap();
    const int original_component_index = find_layer_index(&editor_preview, component_layer_id);
    const QVariantMap original_component = original_component_index < 0 ? QVariantMap{} :
        root_layers_before_child.at(original_component_index).toMap()
            .value(QStringLiteral("smartObjectDocument")).toMap();
    if (applied_component.isEmpty() ||
        applied_component == original_component)
      return fail("Apply should update the parent's editable component contents");

    component_edit_button = find_named_item(edit, QStringLiteral("photoComponentEditButton"));
    if (!component_edit_button || !item_visible(component_edit_button) || !click_item(component_edit_button) ||
        !wait_for_condition([&] {
          process_layout();
          return component_workspace.active() && !component_workspace.busy() &&
              component_workspace.controller() != &editor_preview;
        }, 30000))
      return fail("The component should reopen through the same Edit Contents control after Apply");
    child = component_workspace.controller();
    if (!wait_for_condition([&] {
          process_layout();
          return child->hasAsset() && photo_editor::desktop::EditorPreviewControllerTestAccess::preview_idle(*child);
        }, 30000))
      return fail("The reopened component child should finish rendering its isolated canvas");
    shared_mask_overlay = find_named_object(
        edit, QStringLiteral("editImageViewportSharedMaskOverlay"));
    if (!shared_mask_overlay ||
        shared_mask_overlay->property("previewController").value<QObject*>() != child ||
        !move_child_text())
      return fail("The reopened component child should accept a second real canvas gesture");
    const auto parsed_parent_patch_before_cancel = photo_editor::desktop::parse_adjustment_patch_json(
        editor_preview.currentAssetBoundAdjustmentPatchJson());
    const bool authored_layers_unchanged_before_cancel = parsed_parent_patch_before_cancel.ok() &&
        parsed_parent_patch_before_cancel.state->layers == parent_authored_layers_after_apply;
    const bool parent_recipe_unchanged_before_cancel =
        photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview) ==
            parent_recipe_after_apply;
    if (!authored_layers_unchanged_before_cancel || !parent_recipe_unchanged_before_cancel ||
        editor_preview.editHistoryCurrentIndex() != parent_history_after_apply) {
      const auto current_layers = parsed_parent_patch_before_cancel.ok()
          ? parsed_parent_patch_before_cancel.state->layers : QVariantList{};
      std::cerr << "Second component draft parent isolation: canonicalParse="
                << parsed_parent_patch_before_cancel.ok()
                << " authoredLayersMatch=" << authored_layers_unchanged_before_cancel
                << " recipeMatches=" << parent_recipe_unchanged_before_cancel
                << " history=" << editor_preview.editHistoryCurrentIndex()
                << " expectedHistory=" << parent_history_after_apply
                << " viewLayerListDiffers=" << (editor_preview.photoLayers() != parent_view_layers_after_apply)
                << " expectedLayers=[" << summarize_authored_layers(parent_authored_layers_after_apply).toStdString()
                << "] currentLayers=[" << summarize_authored_layers(current_layers).toStdString() << "]\n";
      return fail("A second isolated draft should not change parent layers before Cancel");
    }
    cancel_button = find_named_item(proof_root, QStringLiteral("photoComponentWorkspaceCancelButton"));
    if (!cancel_button || !click_item(cancel_button) ||
        !wait_for_condition([&] {
          process_layout();
          return !component_workspace.active() && !component_workspace.busy();
        }, 10000) || !settle_parent())
      return fail("Cancel should discard the second draft and return through the Main bar");
    const auto parsed_parent_patch_after_cancel = photo_editor::desktop::parse_adjustment_patch_json(
        editor_preview.currentAssetBoundAdjustmentPatchJson());
    if (!parsed_parent_patch_after_cancel.ok() ||
        parsed_parent_patch_after_cancel.state->layers != parent_authored_layers_after_apply ||
        photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview) !=
            parent_recipe_after_apply ||
        editor_preview.editHistoryCurrentIndex() != parent_history_after_apply ||
        edit->property("editorPreviewController").value<QObject*>() != &editor_preview ||
        !restore_parent_selection())
      return fail("Cancel should preserve the applied parent while restoring its original Studio selection");
    if (!qml_routing_warnings.isEmpty()) {
      std::cerr << qml_routing_warnings.join(QLatin1Char('\n')).toStdString() << '\n';
      return fail("Component workspace startup should not emit QML routing or component bar errors");
    }
    // Leave a live child for the scope guards to close. Views must release
    // their controller references before the component service is destroyed.
    component_edit_button = find_named_item(edit, QStringLiteral("photoComponentEditButton"));
    if (!component_edit_button || !click_item(component_edit_button) ||
        !wait_for_condition([&] {
          process_layout();
          return component_workspace.active() && !component_workspace.busy() &&
              component_workspace.controller() != nullptr &&
              component_workspace.controller() != &editor_preview &&
              photo_editor::desktop::EditorPreviewControllerTestAccess::preview_idle(
                  *component_workspace.controller());
        }, 30000))
      return fail("The Main fixture should exercise shutdown with an open component session");
    if (!qml_routing_warnings.isEmpty())
      return fail("Reopening for active-session shutdown should not emit QML routing errors");
    std::cout << "Full Main component workspace proof passed: canvas routing, child undo/redo, one-entry Apply, Cancel, restored Studio state, and active-session shutdown\n";
    return EXIT_SUCCESS;
  }

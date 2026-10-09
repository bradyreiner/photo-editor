  if (arguments.contains("--layer-drop-indicator-proof")) {
    using namespace photo_editor::desktop;
    using Access = EditorPreviewControllerTestAccess;
    const QString id = QStringLiteral("layer-drop-proof");
    catalog_assets.replaceAssetsForTesting({make_asset(id, source_path)});
    catalog_assets.selectIndexWithModifiers(0, 0);
    if (!QMetaObject::invokeMethod(root, "openSelectedPhotoInEdit") ||
        !wait_for_condition([&] { return editor_preview.assetId() == id &&
            Access::all_idle(editor_preview) && active_workspace(root) &&
            active_workspace(root)->objectName() == "editStudioWorkspace"; }, timeout_ms))
      return fail("Layer drop proof could not open the isolated photo.");
    auto* workspace = active_workspace(root);
    workspace->setProperty("editInspectorTab", "layers");
    if (!editor_preview.addFillLayer("#253648", "Bottom") ||
        !editor_preview.addLayerGroup("Drop Group") ||
        !editor_preview.addFillLayer("#526475", "Group Child") ||
        !editor_preview.movePhotoLayerIntoGroup(0, 1) ||
        !editor_preview.addFillLayer("#83929f", "Top"))
      return fail("Could not create the expanded group and sibling fixture.");
    static_cast<void>(editor_preview.setPhotoLayerGroupExpanded(1, true));
    workspace->setProperty("editInspectorTab", "layers");
    workspace->setProperty("editInspectorPanelVisible", true);
    process_layout_events(8);
    const auto row_at = [&](int index) -> QQuickItem* {
      const auto visit = [&](const auto& self, QQuickItem* item) -> QQuickItem* {
        if (!item) return nullptr;
        if (item->objectName() == "photoLayerRow" &&
            item->property("layerIndex").toInt() == index && item_visible(item)) return item;
        for (auto* child : item->childItems())
          if (auto* row = self(self, child)) return row;
        return nullptr;
      };
      return visit(visit, qobject_cast<QQuickItem*>(workspace));
    };
    const auto layer_id = [&](int index) {
      return editor_preview.photoLayers().value(index).toMap().value("id").toString();
    };
    const auto top_id = layer_id(0), group_id = layer_id(1), bottom_id = layer_id(3);
    QQuickItem* marker = nullptr;
    if (!wait_for_condition([&] {
          marker = find_named_item(workspace, "photoLayerInsertionMarker");
          return marker && row_at(2);
        }, timeout_ms)) {
      QJsonArray setup_capture;
      save_capture_if_requested(root_window, capture_dir, "fixture-setup", &setup_capture);
      std::cerr << "marker_present=" << (marker != nullptr) << " child_present=" << (row_at(2) != nullptr) << '\n';
      return fail("Insertion marker and expanded child must be instantiated.");
    }
    if (!wait_for_condition([&] { return Access::all_idle(editor_preview); }, timeout_ms))
      return fail("Layer fixture rendering must settle before pointer interaction.");
    const auto select_source = [&](int index) {
      QMetaObject::invokeMethod(workspace, "selectPhotoLayerWithModifiers",
          Q_ARG(QVariant, QVariant(index)), Q_ARG(QVariant, QVariant(0)));
      process_layout_events(5);
    };
    const auto begin_drag = [&](int index, bool grip, QPoint destination) {
      auto* row = row_at(index);
      if (!row) return false;
      auto* origin_item = find_named_item(row, grip ? "photoLayerReorderHandle" : "photoLayerThumbnail");
      if (!origin_item || !item_visible(origin_item)) return false;
      const auto origin = origin_item->mapToScene(QPointF(origin_item->width() / 2, origin_item->height() / 2)).toPoint();
      QTest::mousePress(root_window, Qt::LeftButton, Qt::NoModifier, origin);
      for (int step = 1; step <= 12; ++step) {
        const auto point = origin + (destination - origin) * step / 12;
        QTest::mouseMove(root_window, point, 20);
        QTest::qWait(5);
      }
      process_layout_events(4);
      return true;
    };
    const auto pointer_at = [&](int index, double fraction) {
      auto* row = row_at(index);
      return row->mapToScene(QPointF(row->width() * 0.3, row->height() * fraction)).toPoint();
    };
    QJsonArray captures;
    auto* click_thumbnail = find_named_item(row_at(3), "photoLayerThumbnail");
    const auto click_point = click_thumbnail->mapToScene(
        QPointF(click_thumbnail->width() / 2, click_thumbnail->height() / 2)).toPoint();
    QTest::mouseClick(root_window, Qt::LeftButton, Qt::NoModifier, click_point);
    process_layout_events(5);
    if (layer_id(3) != bottom_id || item_visible(marker))
      return fail("Selecting a row must not reorder it or show a drop marker.");
    select_source(0);
    auto destination = pointer_at(3, 0.9);
    if (!begin_drag(0, false, destination) || !item_visible(marker) ||
        workspace->property("photoLayerReorderDropTargetIndex").toInt() != 3 ||
        !workspace->property("photoLayerReorderDropAfter").toBool()) {
      save_capture_if_requested(root_window, capture_dir, "body-drag-failure", &captures);
      auto* area = find_named_item(row_at(0), "photoLayerRowEditTargetArea");
      std::cerr << "body_pressed=" << area->property("pressed").toBool()
                << " reorder_active=" << row_at(0)->property("reorderDragActive").toBool()
                << " marker_visible=" << item_visible(marker)
                << " marker_index=" << workspace->property("photoLayerReorderDropTargetIndex").toInt()
                << " group_index=" << workspace->property("photoLayerHierarchyDropTargetIndex").toInt()
                << " drop_after=" << workspace->property("photoLayerReorderDropAfter").toBool() << '\n';
      return fail("Body drag must show the insertion line below the last sibling.");
    }
    const auto expected_bottom = row_at(3)->mapToScene(QPointF(0, row_at(3)->height())).y();
    if (std::abs(marker->mapToScene(QPointF(0, marker->height() / 2)).y() - expected_bottom) > 1)
      return fail("Insertion line must align with the actual row boundary.");
    save_capture_if_requested(root_window, capture_dir, "drop-after-sibling", &captures);
    QTest::mouseRelease(root_window, Qt::LeftButton, Qt::NoModifier, destination);
    process_layout_events(5);
    if (layer_id(3) != top_id || item_visible(marker))
      return fail("Release must move to the indicated sibling slot and clear the line.");
    select_source(3);
    destination = pointer_at(0, 0.1);
    if (!begin_drag(3, true, destination) || !item_visible(marker) ||
        workspace->property("photoLayerReorderDropTargetIndex").toInt() != 0 ||
        workspace->property("photoLayerReorderDropAfter").toBool())
      return fail("Grip drag must show insertion before the expanded group.");
    save_capture_if_requested(root_window, capture_dir, "drop-before-group", &captures);
    QTest::mouseRelease(root_window, Qt::LeftButton, Qt::NoModifier, destination);
    process_layout_events(5);
    if (layer_id(0) != top_id || layer_id(1) != group_id || item_visible(marker))
      return fail("Release must insert before the group without entering it.");
    select_source(0);
    destination = pointer_at(2, 0.9);
    if (!begin_drag(0, true, destination) || !item_visible(marker) ||
        workspace->property("photoLayerReorderDropTargetIndex").toInt() != 2 ||
        !workspace->property("photoLayerReorderDropAfter").toBool())
      return fail("Insertion after an expanded group must mark below its last child.");
    save_capture_if_requested(root_window, capture_dir, "drop-after-expanded-group", &captures);
    QTest::mouseRelease(root_window, Qt::LeftButton, Qt::NoModifier, destination);
    process_layout_events(5);
    if (layer_id(2) != top_id || layer_id(0) != group_id || item_visible(marker))
      return fail("Release must insert after the whole group subtree.");
    if (!editor_preview.movePhotoLayer(2, -1)) return fail("Restore the group-entry fixture.");
    process_layout_events(5);
    select_source(0);
    destination = pointer_at(1, 0.5);
    if (!begin_drag(0, true, destination) || item_visible(marker) ||
        workspace->property("photoLayerHierarchyDropTargetIndex").toInt() != 1)
      return fail("Group center must show the group highlight instead of an insertion line.");
    auto* group_highlight = find_named_item(row_at(1), "photoLayerHierarchyDropHighlight");
    if (!group_highlight || !item_visible(group_highlight)) return fail("Group drop highlight must be visible.");
    save_capture_if_requested(root_window, capture_dir, "drop-into-group", &captures);
    QTest::mouseRelease(root_window, Qt::LeftButton, Qt::NoModifier, destination);
    process_layout_events(5);
    bool entered_group = false;
    for (const auto& value : editor_preview.photoLayers()) {
      const auto layer = value.toMap();
      if (layer.value("id").toString() == top_id)
        entered_group = layer.value("parentId").toString() == group_id;
    }
    if (!entered_group || workspace->property("photoLayerHierarchyDropTargetIndex").toInt() != -1)
      return fail("Release must enter the indicated group and clear feedback.");
    select_source(3);
    const auto before_cancel = Access::recipe_json(editor_preview);
    destination = pointer_at(0, 0.1);
    if (!begin_drag(3, false, destination) || !item_visible(marker))
      return fail("Cancellation fixture must show an active insertion line.");
    auto* cancel_area = find_named_item(row_at(3), "photoLayerRowEditTargetArea");
    cancel_area->ungrabMouse();
    QTest::mouseRelease(root_window, Qt::LeftButton, Qt::NoModifier, destination);
    process_layout_events(4);
    if (item_visible(marker) || Access::recipe_json(editor_preview) != before_cancel)
      return fail("Cancel must clear the line without moving a layer.");
    if (!editor_preview.setPhotoLayerLocked(3, true)) return fail("Lock the final sibling.");
    process_layout_events(4);
    if (!begin_drag(3, false, destination)) return fail("Attempt the locked-row drag.");
    QTest::mouseRelease(root_window, Qt::LeftButton, Qt::NoModifier, destination);
    process_layout_events(4);
    if (item_visible(marker) || layer_id(3) != bottom_id)
      return fail("Locked rows must not advertise or commit a drop.");
    std::cout << QJsonDocument(QJsonObject{{"schema", "layer-drop-indicator:v1"}, {"ok", true},
        {"platform", QGuiApplication::platformName()}, {"body_and_grip", true},
        {"before_and_after", true}, {"expanded_group", true}, {"group_center", true},
        {"click_does_not_reorder", true}, {"after_group_subtree", true},
        {"cancel_clears", true}, {"locked_blocked", true}, {"captures", captures}}).toJson().constData();
    return EXIT_SUCCESS;
  }

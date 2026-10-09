  if (arguments.contains("--layer-move-proof")) {
    using Access = photo_editor::desktop::EditorPreviewControllerTestAccess;
    const QString id = QStringLiteral("layer-move-proof");
    catalog_assets.replaceAssetsForTesting({make_asset(id, source_path)});
    catalog_assets.selectIndexWithModifiers(0, 0);
    QMetaObject::invokeMethod(root, "openSelectedPhotoInEdit");
    if (!wait_for_condition([&] { return editor_preview.assetId() == id &&
        !root->property("workspaceTransitionActive").toBool() &&
        Access::all_idle(editor_preview) && active_workspace(root) &&
        active_workspace(root)->objectName() == "editStudioWorkspace"; }, timeout_ms))
      return fail("Move proof must open its isolated document.");
    auto* workspace = active_workspace(root);
    const auto first_path = fixture_dir.filePath("move-first.png");
    const auto second_path = fixture_dir.filePath("move-second.png");
    QImage object(260, 140, QImage::Format_ARGB32);
    object.setColorSpace(QColorSpace(QColorSpace::SRgb));
    object.fill(QColor("#f6a44e"));
    if (!object.save(first_path)) return fail("First object fixture must save.");
    object.fill(QColor("#50d4e8"));
    if (!object.save(second_path)) return fail("Second object fixture must save.");
    if (!editor_preview.addSourceLayer(first_path, "First") ||
        !editor_preview.setPhotoLayerTransform(0, 350, 400, 1, 0) ||
        !editor_preview.addSourceLayer(second_path, "Second") ||
        !editor_preview.setPhotoLayerTransform(0, 1000, 400, 1, 0))
      return fail("Move proof must create two separated objects.");
    const auto select = [&](int index, int modifiers) {
      QMetaObject::invokeMethod(workspace, "selectPhotoLayerWithModifiers",
          Q_ARG(QVariant, QVariant(index)), Q_ARG(QVariant, QVariant(modifiers)));
    };
    select(0, 0);
    select(1, static_cast<int>(Qt::ControlModifier));
    workspace->setProperty("editTool", "layers");
    workspace->setProperty("editInspectorTab", "layers");
    workspace->setProperty("layerSnappingEnabled", false);
    if (!wait_for_condition([&] { return Access::all_idle(editor_preview); }, timeout_ms))
      return fail("Move fixture must settle.");
    process_layout_events(6);
    auto* viewport = workspace->property("editViewportHandle").value<QQuickItem*>();
    auto* overlay = find_named_item(workspace, "photoLayerTransformOverlay");
    if (!viewport || !overlay) return fail("Move proof must locate its canvas.");
    const auto position = [&](int index) {
      const auto layer = editor_preview.photoLayers().at(index).toMap();
      return QPoint(layer.value("x").toInt(), layer.value("y").toInt());
    };
    const auto selected_count = [&] {
      return workspace->property("selectedPhotoLayerIndices").value<QJSValue>().toVariant().toList().size();
    };
    const auto point_for = [&](int index) {
      const auto p = position(index);
      return viewport->mapToScene(QPointF(
          viewport->property("paintedContentX").toDouble() + (p.x() + 130) *
              viewport->property("paintedContentWidth").toDouble() / editor_preview.sourceImageWidth(),
          viewport->property("paintedContentY").toDouble() + (p.y() + 70) *
              viewport->property("paintedContentHeight").toDouble() / editor_preview.sourceImageHeight())).toPoint();
    };
    const auto grab_canvas = [&] {
      overlay->setOpacity(0);
      const auto image = root_window->grabWindow();
      overlay->setOpacity(1);
      const auto origin = viewport->mapToScene(QPointF(viewport->property("paintedContentX").toDouble(),
          viewport->property("paintedContentY").toDouble()));
      const double factor = static_cast<double>(image.width()) / root_window->width();
      return image.copy(QRect(qRound(origin.x() * factor), qRound(origin.y() * factor),
          qRound(viewport->property("paintedContentWidth").toDouble() * factor),
          qRound(viewport->property("paintedContentHeight").toDouble() * factor)));
    };
    const auto before0 = position(0), before1 = position(1);
    QJsonArray captures;
    save_capture_if_requested(root_window, capture_dir, "01-selected", &captures);
    // Drag the non-active selected object through the canvas picker.
    auto start = point_for(0);
    auto end = start + QPoint(35, 22);
    QTest::mousePress(root_window, Qt::LeftButton, Qt::NoModifier, start);
    for (int step = 1; step <= 6; ++step)
      QTest::mouseMove(root_window, start + (end - start) * step / 6, 16);
    QTest::mouseRelease(root_window, Qt::LeftButton, Qt::NoModifier, end);
    process_layout_events(4);
    const auto drag_delta = position(0) - before0;
    if (drag_delta.isNull() || position(1) - before1 != drag_delta || selected_count() != 2)
      return fail("Dragging a selected non-active layer must move both and retain selection.");
    editor_preview.undo();
    if (position(0) != before0 || position(1) != before1)
      return fail("A multi-layer drag must undo in one step.");
    editor_preview.redo();
    process_layout_events(5);
    // Drag the active frame through LayerTransformOverlay too.
    const auto overlay_before0 = position(0), overlay_before1 = position(1);
    start = point_for(workspace->property("activePhotoLayerIndex").toInt());
    end = start + QPoint(24, 18);
    QTest::mousePress(root_window, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(root_window, end, 20);
    QTest::mouseRelease(root_window, Qt::LeftButton, Qt::NoModifier, end);
    process_layout_events(4);
    if (position(0) == overlay_before0 || position(0) - overlay_before0 != position(1) - overlay_before1)
      return fail("Dragging the active transform frame must move both selected layers.");
    // Focus the canvas without collapsing the selection.
    QTest::mouseClick(root_window, Qt::LeftButton, Qt::NoModifier, point_for(0));
    process_layout_events(4);
    const auto tap0 = position(0), tap1 = position(1);
    QTest::keyClick(root_window, Qt::Key_Right);
    if (position(0) != tap0 + QPoint(1, 0) || position(1) != tap1 + QPoint(1, 0))
      return fail("One arrow tap must move every selected layer exactly one source pixel.");
    const auto hold0 = position(0), hold1 = position(1);
    if (!wait_for_condition([&] { return Access::all_idle(editor_preview); }, timeout_ms))
      return fail("The arrow tap must settle before held-pixel readback.");
    const auto painted_before = grab_canvas();
    QTest::keyPress(root_window, Qt::Key_Down);
    QTest::qWait(100);
    if (position(0) != hold0 + QPoint(0, 1))
      return fail("A hold must start with one pixel before its repeat delay.");
    QTest::qWait(350);
    const auto held_delta = position(0) - hold0;
    if (held_delta.x() != 0 || held_delta.y() < 5 || position(1) - hold1 != held_delta) {
      std::cerr << "held_delta=" << held_delta.x() << ',' << held_delta.y()
                << " second_delta=" << (position(1) - hold1).x() << ',' << (position(1) - hold1).y()
                << " context=" << workspace->property("layerNudgeContextActive").toBool()
                << " keys=" << workspace->property("layerNudgeKeys").value<QJSValue>().toVariant().toList().size() << '\n';
      return fail("Holding an arrow must continue moving both objects by pixels.");
    }
    if (!wait_for_condition([&] { return grab_canvas() != painted_before; }, timeout_ms))
      return fail("Held movement must change the painted canvas before release.");
    save_capture_if_requested(root_window, capture_dir, "02-moving-held", &captures);
    QTest::keyRelease(root_window, Qt::Key_Down);
    const auto released0 = position(0), released1 = position(1);
    QTest::qWait(100);
    if (position(0) != released0 || position(1) != released1)
      return fail("Releasing an arrow must stop movement immediately.");
    editor_preview.undo();
    if (position(0) != hold0 || position(1) != hold1)
      return fail("An entire arrow hold must undo in one step.");
    editor_preview.redo();
    process_layout_events(5);
    auto* layer_row = find_visible_named_item(workspace, "photoLayerRow");
    if (!layer_row) return fail("Movement must also be available from a focused layer row.");
    layer_row->forceActiveFocus();
    const auto row_before0 = position(0), row_before1 = position(1);
    QTest::keyPress(root_window, Qt::Key_Up);
    QTest::qWait(450);
    QTest::keyRelease(root_window, Qt::Key_Up);
    if (position(0).y() >= row_before0.y() - 4 || position(1) - row_before1 != position(0) - row_before0)
      return fail("Arrow holds from a focused layer row must survive preview-driven row refreshes.");
    editor_preview.setPhotoLayerLocked(1, true);
    const auto locked = position(1), unlocked = position(0);
    QTest::keyClick(root_window, Qt::Key_Left);
    if (position(1) != locked || position(0) != unlocked - QPoint(1, 0))
      return fail("Locked layers must stay put while other selected layers move.");
    QTest::keyPress(root_window, Qt::Key_Right);
    workspace->setProperty("workspaceActive", false);
    const auto suspended = position(0);
    QTest::qWait(350);
    QTest::keyRelease(root_window, Qt::Key_Right);
    if (position(0) != suspended || !workspace->property("layerNudgeKeys").value<QJSValue>().toVariant().toList().isEmpty())
      return fail("Leaving the workspace must stop held movement and close the gesture.");
    // Parent and child selections must never translate descendants twice.
    workspace->setProperty("workspaceActive", true);
    if (!editor_preview.addLayerGroup("Group") || !editor_preview.movePhotoLayerIntoGroup(1, 0))
      return fail("Move proof must create a group fixture.");
    const auto child_before = position(1), group_before = position(0);
    if (!editor_preview.movePhotoLayersByPixels({0, 1, 0, -1, 999}, 1, 0) ||
        position(0) != group_before + QPoint(1, 0) || position(1) != child_before)
      return fail("Selected groups must move children once and ignore duplicate or invalid indices.");
    const QJsonObject result{{"proof", "layer-move:v1"}, {"multi_layer_drag", true},
        {"one_pixel_tap", true}, {"held_pixels", held_delta.y()}, {"release_stops", true},
        {"gesture_undo", true}, {"locks", true}, {"group_descendants_once", true}, {"captures", captures}};
    std::cout << QJsonDocument(result).toJson().constData();
    return EXIT_SUCCESS;
  }

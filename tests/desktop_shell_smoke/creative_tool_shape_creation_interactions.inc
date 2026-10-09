    // Capture the shape-placement HUD itself before adding fixture layers below.
    auto* shape_create_controls = find_named_item(studio_root, QStringLiteral("photoShapeCreateControls"));
    auto* shape_create_no_fill = find_named_item(studio_root, QStringLiteral("photoShapeNoFillButton"));
    if (!QMetaObject::invokeMethod(edit, "setInterfaceDensity", Q_ARG(QVariant, QVariant(QStringLiteral("comfortable")))) ||
        !invoke_edit_tool(QStringLiteral("shapes")) || !shape_create_controls || !shape_create_no_fill ||
        !wait_for_condition([&] { return item_visible(shape_create_controls); }, 5000) ||
        !capture_visual_proof(studio_window, studio_root, proof_dir,
            QStringLiteral("creative-shape-create-1440"), 1440, 900,
            {QStringLiteral("creativeToolsPanel"), QStringLiteral("creativeToolsSurface"),
             QStringLiteral("photoShapeCreateControls"), QStringLiteral("photoShapeTypeChooser"),
             QStringLiteral("photoShapeNoFillButton"), QStringLiteral("photoShapeStrokeWidthSlider")},
            &studio_manifest)) {
      return fail("Shapes placement controls should render inside the shared Creative Tools panel");
    }
    for (const auto& density : densities) {
      if (!QMetaObject::invokeMethod(edit, "setInterfaceDensity", Q_ARG(QVariant, QVariant(density))) ||
          !capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("shape-create-sidebar-%1").arg(density),
            1180, 768, {QStringLiteral("photoShapeCreateControls"), QStringLiteral("photoShapeTypeChooser"),
              QStringLiteral("photoShapeNoFillButton"), QStringLiteral("photoShapeStrokeWidthSlider")}, &studio_manifest))
        return fail("Shape choices should remain usable in a smaller desktop window at each density");
    }
    auto* shape_chooser = find_named_item(studio_root, QStringLiteral("photoShapeTypeChooser"));
    auto* shape_popup = find_named_object(studio_root, QStringLiteral("photoShapeTypePopup"));
    auto* shape_type = find_named_item(studio_root, QStringLiteral("photoShapeTypeControl"));
    if (!shape_chooser || !shape_popup || !shape_type || !click_studio_item(shape_chooser) ||
        !wait_for_condition([&] { return shape_popup->property("opened").toBool(); }, 3000) ||
        !capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("shape-visual-chooser"), 1440, 900,
          {QStringLiteral("photoShapeTypeOption0"), QStringLiteral("photoShapeTypeOption2"),
           QStringLiteral("photoShapeTypeOption4")}, &studio_manifest) ||
        !click_studio_item(find_named_item(studio_root, QStringLiteral("photoShapeTypeOption2"))) ||
        shape_type->property("currentIndex").toInt() != 2 || !click_studio_item(shape_chooser) ||
        !click_studio_item(find_named_item(studio_root, QStringLiteral("photoShapeTypeOption0"))) ||
        shape_type->property("currentIndex").toInt() != 0 || !invoke_edit_tool(QStringLiteral("layers")))
      return fail("The shape preview chooser should select and restore actual drawing types");


    if (!invoke_edit_tool(QStringLiteral("shapes")) || !click_studio_item(shape_create_no_fill))
      return fail("Shape fill choices should be usable before drawing");
    const auto layers_before_shape_draw = editor_preview.photoLayerCount();
    const auto shape_origin = tool_viewport->mapToScene(QPointF(tool_viewport->property("paintedContentX").toDouble(),
        tool_viewport->property("paintedContentY").toDouble()));
    const auto shape_start = shape_origin + QPointF(tool_viewport->property("paintedContentWidth").toDouble() * 0.2,
        tool_viewport->property("paintedContentHeight").toDouble() * 0.2);
    const auto shape_end = shape_origin + QPointF(tool_viewport->property("paintedContentWidth").toDouble() * 0.6,
        tool_viewport->property("paintedContentHeight").toDouble() * 0.55);
    paint_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
    send_paint_pointer(QEvent::MouseButtonPress, shape_start);
    send_paint_pointer(QEvent::MouseMove, shape_end);
    send_paint_pointer(QEvent::MouseButtonRelease, shape_end);
    if (!wait_for_condition([&] { return editor_preview.photoLayerCount() == layers_before_shape_draw + 1 &&
        !editor_preview.processing(); }, 15000)) return fail("Shape drawing should create a layer from sidebar choices");
    const auto drawn_shape = editor_preview.photoLayers().at(0).toMap().value(QStringLiteral("shapeDefinition")).toMap();
    if (drawn_shape.value(QStringLiteral("type")).toString() != QStringLiteral("rectangle") ||
        !drawn_shape.value(QStringLiteral("fillColor")).toString().isEmpty())
      return fail("The drawn shape should preserve its chosen type and no-fill style");
    editor_preview.undo();
    if (editor_preview.photoLayerCount() != layers_before_shape_draw)
      return fail("One undo should remove the newly drawn shape");


  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYER_WORKSPACE")) {
    const auto source_path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath(
        QStringLiteral("tests/fixtures/creative-tools/landscape-mountain-source.jpg"));
    QImage photograph(source_path);
    if (photograph.isNull())
      return fail("Layer workspace proof requires the photographic mountain fixture");
    if (photograph.width() < 1600)
      photograph = photograph.scaledToWidth(1600, Qt::SmoothTransformation);
    const auto photo_path = runtime_root.filePath(QStringLiteral("layer-workspace-photo.png"));
    if (!photograph.save(photo_path))
      return fail("Layer workspace proof should copy the photographic fixture");
    catalog_assets.replaceAssetsForTesting(
        {make_asset(QStringLiteral("layer-workspace-photo"), photo_path,
                    QUrl::fromLocalFile(photo_path).toString())});
    catalog_assets.selectIndex(0);
    engine.load(QUrl::fromLocalFile(main_qml_path));
    if (engine.rootObjects().isEmpty())
      return fail("Layer workspace proof should load the application QML");
    auto* proof_root = engine.rootObjects().constFirst();
    auto* proof_window = qobject_cast<QQuickWindow*>(proof_root);
    if (!proof_window)
      return fail("Layer workspace proof requires a rendered Qt Quick window");
    proof_root->setProperty("width", 1440);
    proof_root->setProperty("height", 900);
    proof_root->setProperty("currentWorkspace", proof_root->property("editWorkspaceIndex"));
    QObject* edit = nullptr;
    if (!wait_for_condition([&] {
          edit = find_named_object(proof_root, QStringLiteral("editStudioWorkspace"));
          return edit && edit->property("pageContentReady").toBool() &&
                 !proof_root->property("workspaceTransitionActive").toBool() &&
                 editor_preview.hasAsset() && !editor_preview.processing();
        }, 20000))
      return fail("Layer workspace proof should reach the ready Edit workspace");

    const auto proof_dir = qEnvironmentVariableIsSet("PHOTO_EDITOR_VISUAL_PROOF_DIR")
        ? QString::fromUtf8(qgetenv("PHOTO_EDITOR_VISUAL_PROOF_DIR"))
        : runtime_root.filePath(QStringLiteral("layer-workspace-proof"));
    if (!QDir().mkpath(proof_dir))
      return fail("Layer workspace proof should create its capture directory");
    QJsonArray captures;
    quint64 pointer_timestamp = 1000;
    const auto process_layout = [&] { process_layout_events(5); };
    const auto settle = [&] {
      QElapsedTimer elapsed;
      elapsed.start();
      return wait_for_condition([&] {
        process_layout();
        return elapsed.elapsed() >= 350 && !editor_preview.processing();
      }, 10000);
    };
    const auto click_item = [&](QQuickItem* item) {
      auto* target_window = item ? item->window() : nullptr;
      if (!item_visible(item) || !target_window || !item->property("enabled").toBool())
        return false;
      target_window->grabWindow();
      pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
      const auto point = scene_rect(item).center();
      const auto global = QPointF(target_window->mapToGlobal(point.toPoint()));
      QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
      press.setTimestamp(pointer_timestamp++);
      QCoreApplication::sendEvent(target_window, &press);
      process_layout();
      QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton,
                          Qt::NoButton, Qt::NoModifier);
      release.setTimestamp(pointer_timestamp++);
      QCoreApplication::sendEvent(target_window, &release);
      process_layout();
      return true;
    };
    const auto invoke_bool = [&](const char* method, const QVariant& first = QVariant()) {
      QVariant result;
      const auto invoked = first.isValid()
          ? QMetaObject::invokeMethod(edit, method, Q_RETURN_ARG(QVariant, result),
                                      Q_ARG(QVariant, first))
          : QMetaObject::invokeMethod(edit, method, Q_RETURN_ARG(QVariant, result));
      return invoked && result.toBool();
    };
    const auto capture = [&](const QString& state, int width, int height) {
      proof_root->setProperty("width", width);
      proof_root->setProperty("height", height);
      if (!settle()) return false;
      auto* canvas = find_named_item(edit, QStringLiteral("editImageViewport"));
      if (!canvas || !proof_window) return false;
      const auto content_x = canvas->property("paintedContentX").toDouble();
      const auto content_y = canvas->property("paintedContentY").toDouble();
      const auto content_width = canvas->property("paintedContentWidth").toDouble();
      const auto content_height = canvas->property("paintedContentHeight").toDouble();
      if (content_width <= 0 || content_height <= 0) return false;
      const auto clear_tooltip_point = QPointF(
          content_x + content_width * 0.03,
          content_y + content_height * 0.94);
      const auto clear_tooltip_scene_point = canvas->mapToScene(clear_tooltip_point);
      const auto clear_tooltip_global_point =
          QPointF(proof_window->mapToGlobal(clear_tooltip_scene_point.toPoint()));
      QCursor::setPos(clear_tooltip_global_point.toPoint());
      QMouseEvent move(QEvent::MouseMove, clear_tooltip_scene_point, clear_tooltip_global_point,
                       Qt::NoButton, Qt::NoButton, Qt::NoModifier);
      move.setTimestamp(pointer_timestamp++);
      QCoreApplication::sendEvent(proof_window, &move);
      process_layout_events(4);
      return capture_visual_proof(
          proof_window, proof_root, proof_dir, state, width, height,
          {QStringLiteral("editImageViewport"), QStringLiteral("photoLayersInspectorGroup"),
           QStringLiteral("photoLayerPropertiesPanel")}, &captures);
    };
    auto* viewport = find_named_item(edit, QStringLiteral("editImageViewport"));
    auto* layer_rows = find_named_object(edit, QStringLiteral("photoLayerRowsRepeater"));
    if (!viewport || !layer_rows)
      return fail("Layer workspace should expose its image canvas and layer stack");

    const auto canvas_width = editor_preview.sourceImageWidth();
    const auto canvas_height = editor_preview.sourceImageHeight();
    if (canvas_width <= 0 || canvas_height <= 0 ||
        !editor_preview.addPhotoShape(
            QStringLiteral("rectangle"), canvas_width / 5, canvas_height / 5,
            canvas_width / 2, canvas_height / 2, canvas_width, canvas_height,
            QStringLiteral("#65c5aa"), QStringLiteral("#e8f1e9"), 6.0) ||
        !editor_preview.addPointTextLayer(
            QStringLiteral("Foreground object"), canvas_width / 10, canvas_height / 8,
            canvas_width, canvas_height) ||
        !editor_preview.addPointTextLayer(
            QStringLiteral("Background object"), canvas_width * 7 / 10, canvas_height * 7 / 10,
            canvas_width, canvas_height) || !settle())
      return fail("Layer workspace should create a shape and two independent canvas objects");
    const auto content_target_id = editor_preview.photoLayers().value(1).toMap()
                                       .value(QStringLiteral("id")).toString();
    if (content_target_id.isEmpty())
      return fail("Layer workspace should identify the content layer that will own the mask");

    // Creating a selection source should make one linked adjustment row. The
    // mask itself remains a child target and does not become a second stack row.
    editor_preview.deselectActiveMask();
    const auto layer_count_before_mask = editor_preview.photoLayerCount();
    const auto mask_count_before = editor_preview.maskCount();
    if (!QMetaObject::invokeMethod(edit, "createMaskSource",
                                   Q_ARG(QVariant, QVariant(QStringLiteral("radial")))) || !settle())
      return fail("Layer workspace should create a radial source through the normal UI path");
    const auto layers_after_mask = editor_preview.photoLayers();
    const auto temporary_mask_layer = layers_after_mask.isEmpty() ? QVariantMap{} : layers_after_mask.first().toMap();
    if (editor_preview.maskCount() != mask_count_before + 1 ||
        editor_preview.photoLayerCount() != layer_count_before_mask + 1 ||
        temporary_mask_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("adjustment") ||
        !temporary_mask_layer.value(QStringLiteral("selectionScoped")).toBool() ||
        edit->property("editTool").toString() != QStringLiteral("masks") ||
        edit->property("editInspectorTab").toString() != QStringLiteral("layers"))
      return fail("Adding a selection source should create one temporary linked row and leave Layers open");
    int content_target_index = -1;
    for (int index = 0; index < editor_preview.photoLayerCount(); ++index) {
      if (editor_preview.photoLayers().at(index).toMap().value(QStringLiteral("id")).toString() == content_target_id) {
        content_target_index = index;
        break;
      }
    }
    QVariant linked_selection;
    if (content_target_index < 0 ||
        !QMetaObject::invokeMethod(edit, "useActiveSelectionAsLayerMask",
                                   Q_RETURN_ARG(QVariant, linked_selection),
                                   Q_ARG(QVariant, QVariant(content_target_index)),
                                   Q_ARG(QVariant, QVariant(false))) ||
        !linked_selection.toBool() || !settle())
      return fail("Layer workspace should link the new selection directly to a content layer");
    QVariant linked_content_index;
    if (!QMetaObject::invokeMethod(edit, "photoLayerIndexForId",
                                   Q_RETURN_ARG(QVariant, linked_content_index),
                                   Q_ARG(QVariant, QVariant(content_target_id))) ||
        linked_content_index.toInt() < 0 ||
        !invoke_bool("setActivePhotoLayer", linked_content_index) || !settle())
      return fail("The content layer should remain selected after linking its mask");
    const auto linked_content_layer = editor_preview.photoLayers().at(linked_content_index.toInt()).toMap();
    QMetaObject::invokeMethod(edit, "setPhotoLayerMaskSectionCollapsed",
                              Q_ARG(QVariant, linked_content_index),
                              Q_ARG(QVariant, QVariant(linked_content_layer)),
                              Q_ARG(QVariant, QVariant(false)));
    process_layout();
    const auto linked_mask_rows = find_named_items(edit, QStringLiteral("photoLayerNestedMaskRow"));
    const auto standalone_mask_rows = find_named_items(edit, QStringLiteral("photoLayerMaskSetRow"));
    int visible_linked_mask_rows = 0;
    for (auto* candidate : linked_mask_rows)
      if (item_visible(qobject_cast<QQuickItem*>(candidate))) ++visible_linked_mask_rows;
    int visible_standalone_mask_rows = 0;
    for (auto* candidate : standalone_mask_rows)
      if (item_visible(qobject_cast<QQuickItem*>(candidate))) ++visible_standalone_mask_rows;
    QVariant unlinked_mask_sets;
    if (!QMetaObject::invokeMethod(edit, "unlinkedMaskSets",
                                   Q_RETURN_ARG(QVariant, unlinked_mask_sets)) ||
        editor_preview.photoLayerCount() != layer_count_before_mask ||
        visible_linked_mask_rows != 1 || visible_standalone_mask_rows != 0 ||
        !unlinked_mask_sets.toList().isEmpty())
      return fail("A content-linked mask should appear once under its layer and never as a duplicate standalone row");
    auto* nested_mask_row = qobject_cast<QQuickItem*>(linked_mask_rows.first());
    auto* nested_mask_name = find_named_item(edit, QStringLiteral("photoLayerNestedMaskNameLabel"));
    if (!nested_mask_row || nested_mask_row->property("combineOrderIndex").toInt() != 0 ||
        nested_mask_row->property("combineOrderCount").toInt() != 1 || !nested_mask_name ||
        nested_mask_name->property("text").toString().trimmed().isEmpty())
      return fail("A single nested mask should publish stable combine order and a readable name");
    proof_root->setProperty("width", 1100);
    proof_root->setProperty("height", 780);
    if (!settle() || nested_mask_name->width() < 100.0)
      return fail("A nested mask should retain at least 100 pixels for its label at native 1100-pixel width");
    if (!capture(QStringLiteral("layers-with-radial-mask"), 1440, 900) ||
        !capture(QStringLiteral("layers-with-radial-mask-narrow"), 1100, 780))
      return fail("Layer workspace should capture the polished stack at desktop and narrow sizes");

    // New adjustment layers remain in the layer workflow. Their Edit control
    // is the deliberate entry point to adjustment controls.
    if (!invoke_bool("activateBaseImageLayerTarget"))
      return fail("Layer workspace should leave selection editing before creating a global layer");
    const auto layer_count_before_create = editor_preview.photoLayerCount();
    if (!invoke_bool("addAdjustmentLayerForCurrentTarget") || !settle() ||
        editor_preview.photoLayerCount() != layer_count_before_create + 1 ||
        edit->property("editTool").toString() != QStringLiteral("layers") ||
        edit->property("editInspectorTab").toString() != QStringLiteral("layers"))
      return fail("Creating an adjustment layer should keep the Layers inspector active");
    const auto row_targets = find_named_items(edit, QStringLiteral("photoLayerRowEditTargetArea"));
    QQuickItem* visible_layer_row_target = nullptr;
    for (auto* candidate : row_targets) {
      auto* item = qobject_cast<QQuickItem*>(candidate);
      if (item_visible(item)) {
        visible_layer_row_target = item;
        break;
      }
    }
    const auto created_row_index = visible_layer_row_target && visible_layer_row_target->parent()
        ? visible_layer_row_target->parent()->property("layerIndex").toInt() : -1;
    if (!visible_layer_row_target || created_row_index < 0 ||
        !click_item(visible_layer_row_target) || !settle() ||
        edit->property("activePhotoLayerIndex").toInt() != created_row_index ||
        edit->property("editTool").toString() != QStringLiteral("layers") ||
        edit->property("editInspectorTab").toString() != QStringLiteral("layers"))
      return fail("A single layer-row click should select the layer and keep its properties visible");
    auto* edit_button = find_named_item(edit, QStringLiteral("photoLayerEditButton"));
    if (!edit_button || !click_item(edit_button) || !settle() ||
        edit->property("editTool").toString() != QStringLiteral("adjust") ||
        edit->property("editInspectorTab").toString() != QStringLiteral("adjust"))
      return fail("A visible layer Edit button should explicitly open adjustment controls");

    // Add two text objects at separated canvas positions, then click the first
    // object's painted bounds. The active layer must match the object under the
    // pointer and reveal its object properties in the Layers inspector.
    const auto foreground_layer_index = 2;
    QVariant activate_layers;
    if (!QMetaObject::invokeMethod(edit, "activateEditTool", Q_RETURN_ARG(QVariant, activate_layers),
                                   Q_ARG(QVariant, QVariant(QStringLiteral("layers")))) ||
        !invoke_bool("setActivePhotoLayer", 0) || !settle())
      return fail("Layer workspace should select a created canvas object");
    auto* canvas = find_named_item(edit, QStringLiteral("editImageViewport"));
    const auto object_bounds = editor_preview.photoLayerBounds(foreground_layer_index);
    const auto image_width = static_cast<double>(editor_preview.sourceImageWidth());
    const auto image_height = static_cast<double>(editor_preview.sourceImageHeight());
    const auto content_x = canvas->property("paintedContentX").toDouble();
    const auto content_y = canvas->property("paintedContentY").toDouble();
    const auto content_width = canvas->property("paintedContentWidth").toDouble();
    const auto content_height = canvas->property("paintedContentHeight").toDouble();
    const auto object_x = object_bounds.value(QStringLiteral("x")).toDouble() +
                          object_bounds.value(QStringLiteral("width")).toDouble() / 2.0;
    const auto object_y = object_bounds.value(QStringLiteral("y")).toDouble() +
                          object_bounds.value(QStringLiteral("height")).toDouble() / 2.0;
    if (image_width <= 0 || image_height <= 0 || content_width <= 0 || content_height <= 0 ||
        object_bounds.isEmpty())
      return fail("Canvas picking proof should read valid object and painted-image bounds");
    const auto local_canvas_point = QPointF(
        content_x + object_x * content_width / image_width,
        content_y + object_y * content_height / image_height);
    const auto canvas_scene_point = canvas->mapToScene(local_canvas_point);
    const auto canvas_global_point = QPointF(proof_window->mapToGlobal(canvas_scene_point.toPoint()));
    QMouseEvent canvas_press(QEvent::MouseButtonPress, canvas_scene_point, canvas_global_point,
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    canvas_press.setTimestamp(pointer_timestamp++);
    QCoreApplication::sendEvent(proof_window, &canvas_press);
    process_layout();
    QMouseEvent canvas_release(QEvent::MouseButtonRelease, canvas_scene_point, canvas_global_point,
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    canvas_release.setTimestamp(pointer_timestamp++);
    QCoreApplication::sendEvent(proof_window, &canvas_release);
    if (!wait_for_condition([&] {
          process_layout();
          return edit->property("activePhotoLayerIndex").toInt() == foreground_layer_index &&
                 edit->property("editTool").toString() == QStringLiteral("layers") &&
                 edit->property("editInspectorTab").toString() == QStringLiteral("layers");
        }, 3000))
      return fail("Clicking a canvas object should select its layer and open its properties");
    auto* layer_properties = find_named_object(edit, QStringLiteral("photoLayerPropertiesPanel"));
    auto* text_editor = find_named_object(edit, QStringLiteral("photoLayerTextEditor"));
    auto* creative_panel = find_named_object(edit, QStringLiteral("creativeToolsPanel"));
    const auto foreground_layer = editor_preview.photoLayers().at(foreground_layer_index).toMap();
    const auto foreground_name = foreground_layer.value(QStringLiteral("displayName")).toString().isEmpty()
        ? foreground_layer.value(QStringLiteral("name")).toString()
        : foreground_layer.value(QStringLiteral("displayName")).toString();
    if (!layer_properties || layer_properties->property("layerIndex").toInt() != foreground_layer_index ||
        !text_editor || !item_visible(qobject_cast<QQuickItem*>(text_editor)) ||
        !creative_panel || creative_panel->property("targetText").toString() != foreground_name)
      return fail("Selecting a text object should reveal its matching typography panel and layer properties");
    if (!capture(QStringLiteral("canvas-object-properties"), 1440, 900))
      return fail("Canvas object properties should capture with their layer selected");

    const auto trigger_add_menu = [&](const QString& key) {
      auto* menu_action = find_named_item(edit, QStringLiteral("photoLayerAddSplitButtonMenuAction"));
      if (!menu_action || !click_item(menu_action)) {
        std::cerr << "Layer add menu action could not be clicked: " << key.toStdString() << '\n';
        return false;
      }
      auto* popup = find_named_object(edit, QStringLiteral("photoLayerAddSplitButtonPopup"));
      if (!popup || !wait_for_condition([&] { process_layout(); return popup->property("visible").toBool(); }, 3000)) {
        std::cerr << "Layer add popup did not open for " << key.toStdString() << '\n';
        return false;
      }
      auto* add_split = find_named_object(edit, QStringLiteral("photoLayerAddSplitButton"));
      if (!add_split) {
        std::cerr << "Layer add split control disappeared for " << key.toStdString() << '\n';
        return false;
      }
      QVariant menu_data_value;
      if (!QMetaObject::invokeMethod(edit, "layerAddMenuItems",
                                     Q_RETURN_ARG(QVariant, menu_data_value))) {
        std::cerr << "Layer add menu model could not be read for " << key.toStdString() << '\n';
        return false;
      }
      const auto menu_data = menu_data_value.metaType() == QMetaType::fromType<QJSValue>()
          ? menu_data_value.value<QJSValue>().toVariant().toList()
          : menu_data_value.toList();
      if (menu_data.isEmpty()) {
        std::cerr << "Layer add menu model is empty for " << key.toStdString()
                  << " type=" << menu_data_value.metaType().name() << '\n';
        return false;
      }
      for (int index = 0; index < menu_data.size(); ++index) {
        if (menu_data.at(index).toMap().value(QStringLiteral("key")).toString() != key)
          continue;
        auto* item = find_named_item(popup, QStringLiteral("photoLayerAddSplitButtonPopupItem") +
                                         QString::number(index));
        if (!item || !click_item(item)) {
          std::cerr << "Layer add menu row could not be clicked: " << key.toStdString()
                    << " index=" << index << '\n';
          return false;
        }
        return true;
      }
      std::cerr << "Layer add menu key missing: " << key.toStdString()
                << " items=" << menu_data.size() << '\n';
      return false;
    };
    const auto add_layer_stays_in_layers = [&](const QString& key, const QString& kind) {
      const auto previous_layers = editor_preview.photoLayers();
      QStringList previous_ids;
      for (const auto& value : previous_layers)
        previous_ids.append(value.toMap().value(QStringLiteral("id")).toString());
      if (!trigger_add_menu(key) || !settle() ||
          editor_preview.photoLayerCount() != previous_layers.size() + 1)
        return false;
      QString added_id;
      for (const auto& value : editor_preview.photoLayers()) {
        const auto layer = value.toMap();
        const auto identity = layer.value(QStringLiteral("id")).toString();
        if (!previous_ids.contains(identity) && layer.value(QStringLiteral("kind")).toString() == kind) {
          added_id = identity;
          break;
        }
      }
      QVariant added_index;
      if (added_id.isEmpty() ||
          !QMetaObject::invokeMethod(edit, "photoLayerIndexForId",
                                     Q_RETURN_ARG(QVariant, added_index),
                                     Q_ARG(QVariant, QVariant(added_id))) ||
          added_index.toInt() < 0 ||
          edit->property("activePhotoLayerIndex").toInt() != added_index.toInt() ||
          edit->property("editTool").toString() != QStringLiteral("layers") ||
          edit->property("editInspectorTab").toString() != QStringLiteral("layers"))
        return false;
      return true;
    };
    if (!add_layer_stays_in_layers(QStringLiteral("fill"), QStringLiteral("fill")))
      return fail("Fill add-menu action should select its new layer in the Layers inspector");
    if (!add_layer_stays_in_layers(QStringLiteral("text"), QStringLiteral("text")))
      return fail("Text add-menu action should select its new layer in the Layers inspector");
    edit->setProperty("selectedPhotoLayerIndices", QVariantList{});
    process_layout();
    if (!add_layer_stays_in_layers(QStringLiteral("group"), QStringLiteral("group")))
      return fail("The Group add-menu action should create and select an organizational layer in Layers");

    QFile manifest(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
    if (!manifest.open(QIODevice::WriteOnly) ||
        manifest.write(QJsonDocument(captures).toJson()) < 0)
      return fail("Layer workspace proof should save its screenshot manifest");
    std::cout << "Layer workspace native interaction proof passed; captures="
              << captures.size() << "\n";
    editor_preview.setImageProvider(nullptr);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return EXIT_SUCCESS;
  }

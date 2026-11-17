  const bool layer_row_interaction_only =
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYER_ROW_INTERACTIONS");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYER_WORKSPACE") ||
      layer_row_interaction_only) {
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
    const auto move_pointer_to_item = [&](QQuickItem* item) {
      auto* target_window = item ? item->window() : nullptr;
      if (!item_visible(item) || !target_window) return false;
      QTest::mouseMove(target_window, scene_rect(item).center().toPoint());
      process_layout();
      return true;
    };
    const auto reveal_layer_row_actions = [&](QQuickItem* item) {
      for (auto* ancestor = item; ancestor; ancestor = ancestor->parentItem()) {
        const auto name = ancestor->objectName();
        if (name != QStringLiteral("photoLayerRow") &&
            name != QStringLiteral("baseImageLayerRow") &&
            name != QStringLiteral("photoLayerNestedMaskRow")) continue;
        if (!move_pointer_to_item(ancestor)) return false;
        return wait_for_condition([&] {
          process_layout();
          return item_visible(item) && item->isEnabled();
        }, 2000);
      }
      return false;
    };
    const auto click_item = [&](QQuickItem* item,
                                Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
      auto* target_window = item ? item->window() : nullptr;
      if (!item_visible(item) || !target_window)
        return false;
      if (!item->isEnabled() && !reveal_layer_row_actions(item))
        return false;
      if (!item_visible(item) || !item->isEnabled())
        return false;
      target_window->grabWindow();
      const auto center = scene_rect(item).center();
      const auto point = center.toPoint();
      if (!QRectF(0.0, 0.0, target_window->width(), target_window->height()).contains(center))
        return false;
      QTest::mouseClick(target_window, Qt::LeftButton, modifiers, point);
      process_layout();
      return true;
    };
    const auto double_click_item = [&](QQuickItem* item) {
      auto* target_window = item ? item->window() : nullptr;
      if (!item_visible(item) || !target_window || !item->property("enabled").toBool())
        return false;
      target_window->grabWindow();
      const auto point = scene_rect(item).center();
      QTest::mouseDClick(target_window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
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
    const auto hit_path_at_window_point = [](QQuickWindow* window, const QPointF& scene_point) {
      QStringList path;
      auto* current = window ? window->contentItem() : nullptr;
      QPointF local_point = current ? current->mapFromScene(scene_point) : scene_point;
      for (int depth = 0; current && depth < 14; ++depth) {
        path.push_back(QString::fromLatin1(current->metaObject()->className()) +
                       QStringLiteral("#") + current->objectName());
        auto* child = current->childAt(local_point.x(), local_point.y());
        if (!child) break;
        local_point = child->mapFromItem(current, local_point);
        current = child;
      }
      return path.join(QStringLiteral(" > "));
    };
    const auto mask_set_ids = [](const QVariantList& rows, const QString& id_key) {
      QStringList ids;
      for (const auto& value : rows) {
        const auto id = value.toMap().value(id_key).toString().trimmed();
        if (!id.isEmpty() && !ids.contains(id)) ids.append(id);
      }
      ids.sort(Qt::CaseSensitive);
      return ids;
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
    if (layer_row_interaction_only) {
      proof_root->setProperty("width", 1900);
      proof_root->setProperty("height", 980);
      if (!invoke_bool("setInterfaceDensity", QVariant(QStringLiteral("comfortable"))) || !settle() ||
          !invoke_bool("activateBaseImageLayerTarget") ||
          !invoke_bool("addAdjustmentLayerForCurrentTarget") || !settle())
        return fail("The focused row proof should provide a spacious inspector and add an editable adjustment layer");
      const auto first_layer_index = edit->property("activePhotoLayerIndex").toInt();
      const auto first_layer = editor_preview.photoLayers().value(first_layer_index).toMap();
      const auto first_layer_id = first_layer.value(QStringLiteral("id")).toString();
      if (first_layer_index < 0 || first_layer_id.isEmpty() ||
          !invoke_bool("addAdjustmentLayerForCurrentTarget") || !settle())
        return fail("The focused row proof should create two independently addressable layers");
      const auto second_layer_index = edit->property("activePhotoLayerIndex").toInt();
      const auto second_layer = editor_preview.photoLayers().value(second_layer_index).toMap();
      const auto second_layer_id = second_layer.value(QStringLiteral("id")).toString();
      if (second_layer_index < 0 || second_layer_id.isEmpty() || second_layer_id == first_layer_id ||
          edit->property("editTool").toString() != QStringLiteral("layers"))
        return fail("Adding a second layer should keep the Layers inspector active");
      const auto find_photo_row = [&](int index) -> QQuickItem* {
        for (auto* candidate : find_named_items(edit, QStringLiteral("photoLayerRow")))
          if (candidate->property("layerIndex").toInt() == index)
            return candidate;
        return nullptr;
      };
      const auto layer_index_for_id = [&](const QString& id) {
        QVariant index;
        if (!QMetaObject::invokeMethod(edit, "photoLayerIndexForId",
                                       Q_RETURN_ARG(QVariant, index),
                                       Q_ARG(QVariant, QVariant(id))))
          return -1;
        return index.toInt();
      };
      const auto find_photo_row_by_id = [&](const QString& id) {
        return find_photo_row(layer_index_for_id(id));
      };
      const auto layer_data_by_id = [&](const QString& id) {
        return editor_preview.photoLayers().value(layer_index_for_id(id)).toMap();
      };
      const auto activate_layers_tool = [&] {
        return QMetaObject::invokeMethod(edit, "activateEditTool",
                                         Q_ARG(QVariant, QVariant(QStringLiteral("layers")))) &&
            settle() && edit->property("editTool").toString() == QStringLiteral("layers") &&
            edit->property("editInspectorTab").toString() == QStringLiteral("layers");
      };
      const auto selected_adjustment_target = [&](const QString& id, int index) {
        const auto target = edit->property("selectedEditTarget").toMap();
        return edit->property("activePhotoLayerIndex").toInt() == index &&
            edit->property("selectedPhotoLayerIndices").toList() == QVariantList{QVariant(index)} &&
            target.value(QStringLiteral("scope")).toString() == QStringLiteral("adjustment-layer") &&
            target.value(QStringLiteral("layerId")).toString() == id &&
            target.value(QStringLiteral("layerIndex")).toInt() == index &&
            edit->property("editTool").toString() == QStringLiteral("adjust") &&
            edit->property("editInspectorTab").toString() == QStringLiteral("adjust");
      };
      const auto selected_adjustment_row = [&](const QString& id, int index) {
        const auto target = edit->property("selectedEditTarget").toMap();
        return edit->property("activePhotoLayerIndex").toInt() == index &&
            edit->property("selectedPhotoLayerIndices").toList() == QVariantList{QVariant(index)} &&
            target.value(QStringLiteral("scope")).toString() == QStringLiteral("adjustment-layer") &&
            target.value(QStringLiteral("layerId")).toString() == id &&
            target.value(QStringLiteral("layerIndex")).toInt() == index &&
            edit->property("editTool").toString() == QStringLiteral("layers") &&
            edit->property("editInspectorTab").toString() == QStringLiteral("layers");
      };
      auto first_row_index = layer_index_for_id(first_layer_id);
      auto second_row_index = layer_index_for_id(second_layer_id);
      auto* first_row = find_photo_row_by_id(first_layer_id);
      auto* first_name = first_row == nullptr
          ? nullptr : find_named_item(first_row, QStringLiteral("photoLayerNameLabel"));
      auto* first_name_window = first_name ? first_name->window() : nullptr;
      const auto first_row_bounds = scene_rect(first_row);
      const auto first_name_bounds = scene_rect(first_name);
      const auto first_name_point = first_name_bounds.center();
      const auto first_name_window_bounds = first_name_window
          ? QRectF(0.0, 0.0, first_name_window->width(), first_name_window->height())
          : QRectF{};
      const bool first_name_point_in_window = first_name_window_bounds.contains(first_name_point);
      const bool first_name_visible_before_click = item_visible(first_name);
      const bool first_name_enabled_before_click = first_name && first_name->isEnabled();
      const int active_layer_before_name_click = edit->property("activePhotoLayerIndex").toInt();
      const auto selected_layers_before_name_click = edit->property("selectedPhotoLayerIndices").toList();
      const auto selected_target_before_name_click = edit->property("selectedEditTarget").toMap();
      const auto first_name_hit_path = hit_path_at_window_point(first_name_window, first_name_point);
      QPointer<QQuickItem> guarded_first_name(first_name);
      const bool name_click_sent = first_name && click_item(first_name);
      const bool name_click_settled = name_click_sent && settle();
      const int active_layer_after_name_click = edit->property("activePhotoLayerIndex").toInt();
      const auto target_after_name_click = edit->property("selectedEditTarget").toMap();
      const bool first_layer_is_selected_target =
          target_after_name_click.value(QStringLiteral("layerId")).toString() == first_layer_id &&
          target_after_name_click.value(QStringLiteral("scope")).toString() == QStringLiteral("adjustment-layer") &&
          target_after_name_click.value(QStringLiteral("layerIndex")).toInt() == first_row_index;
      const bool layers_inspector_is_open =
          edit->property("editTool").toString() == QStringLiteral("layers") &&
          edit->property("editInspectorTab").toString() == QStringLiteral("layers");
      if (first_row_index < 0 || second_row_index < 0 || !first_name ||
          !name_click_sent || !name_click_settled ||
          active_layer_after_name_click != first_row_index ||
          !first_layer_is_selected_target || !layers_inspector_is_open) {
        const auto failure_capture_path = QDir(proof_dir).filePath(
            QStringLiteral("layer-row-name-selection-failure.png"));
        if (!proof_window->grabWindow().save(failure_capture_path))
          std::cerr << "Could not save layer row selection capture: "
                    << failure_capture_path.toStdString() << '\n';
        auto* current_first_row = find_photo_row_by_id(first_layer_id);
        auto* current_first_name = current_first_row == nullptr
            ? nullptr : find_named_item(current_first_row, QStringLiteral("photoLayerNameLabel"));
        const auto current_name_bounds = scene_rect(current_first_name);
        const int active_index = edit->property("activePhotoLayerIndex").toInt();
        const auto current_layers = editor_preview.photoLayers();
        const auto active_layer = active_index >= 0 && active_index < current_layers.size()
            ? current_layers.at(active_index).toMap() : QVariantMap{};
        const auto selected_layers_after_name_click = edit->property("selectedPhotoLayerIndices").toList();
        QStringList before_selected_summary;
        for (const auto& value : selected_layers_before_name_click)
          before_selected_summary.append(value.toString());
        QStringList after_selected_summary;
        for (const auto& value : selected_layers_after_name_click)
          after_selected_summary.append(value.toString());
        std::cerr << "Layer name pointer selection diagnostic: firstId="
                  << first_layer_id.toStdString() << " firstExpectedIndex=" << first_row_index
                  << " secondId=" << second_layer_id.toStdString()
                  << " secondIndex=" << second_row_index
                  << " itemVisibleBefore=" << first_name_visible_before_click
                  << " itemEnabledBefore=" << first_name_enabled_before_click
                  << " itemAliveAfterClick=" << !guarded_first_name.isNull()
                  << " clickSent=" << name_click_sent << " settled=" << name_click_settled
                  << " window=" << first_name_window_bounds.x() << ',' << first_name_window_bounds.y()
                  << ' ' << first_name_window_bounds.width() << 'x' << first_name_window_bounds.height()
                  << " rowBounds=" << first_row_bounds.x() << ',' << first_row_bounds.y()
                  << ' ' << first_row_bounds.width() << 'x' << first_row_bounds.height()
                  << " nameBounds=" << first_name_bounds.x() << ',' << first_name_bounds.y()
                  << ' ' << first_name_bounds.width() << 'x' << first_name_bounds.height()
                  << " currentNameBounds=" << current_name_bounds.x() << ',' << current_name_bounds.y()
                  << ' ' << current_name_bounds.width() << 'x' << current_name_bounds.height()
                  << " clickPoint=" << first_name_point.x() << ',' << first_name_point.y()
                  << " targetInsideWindow=" << first_name_point_in_window
                  << " hitPath=" << first_name_hit_path.toStdString()
                  << " activeBefore=" << active_layer_before_name_click
                  << " activeAfter=" << active_layer_after_name_click
                  << " activeIdAfter=" << active_layer.value(QStringLiteral("id")).toString().toStdString()
                  << " selectedBefore=[" << before_selected_summary.join(QLatin1Char(',')).toStdString()
                  << "] selectedAfter=[" << after_selected_summary.join(QLatin1Char(',')).toStdString()
                  << " targetBefore={scope:" << selected_target_before_name_click.value(QStringLiteral("scope")).toString().toStdString()
                  << ",layerId:" << selected_target_before_name_click.value(QStringLiteral("layerId")).toString().toStdString()
                  << ",layerIndex:" << selected_target_before_name_click.value(QStringLiteral("layerIndex")).toInt() << "}"
                  << " editTool=" << edit->property("editTool").toString().toStdString()
                  << " inspectorTab=" << edit->property("editInspectorTab").toString().toStdString()
                  << " targetScope=" << target_after_name_click.value(QStringLiteral("scope")).toString().toStdString()
                  << " targetLayerId=" << target_after_name_click.value(QStringLiteral("layerId")).toString().toStdString()
                  << " targetLayerIndex=" << target_after_name_click.value(QStringLiteral("layerIndex")).toInt()
                  << " targetMatches=" << first_layer_is_selected_target
                  << " layersInspectorExpected=" << layers_inspector_is_open
                  << " capture=" << failure_capture_path.toStdString() << '\n';
        return fail("Clicking an adjustment row name should select it and keep the Layers inspector active");
      }

      auto* first_pixels_target = find_named_item(first_row, QStringLiteral("photoLayerPixelsTarget"));
      if (!first_pixels_target || !item_visible(first_pixels_target) ||
          !click_item(first_pixels_target) || !settle() ||
          !selected_adjustment_target(first_layer_id, first_row_index))
        return fail("Clicking an adjustment layer thumbnail should open that layer's adjustment controls");
      if (!activate_layers_tool() || !item_visible(find_photo_row_by_id(first_layer_id)))
        return fail("The thumbnail target proof should return to Layers before rename and reorder checks");

      // Layer delegates can be destroyed and recreated when selection or layer
      // state changes. Resolve each target again from its stable model identity.
      first_row = find_photo_row_by_id(first_layer_id);
      first_name = first_row == nullptr
          ? nullptr : find_named_item(first_row, QStringLiteral("photoLayerNameLabel"));
      if (!double_click_item(first_name) || !settle())
        return fail("Double-clicking a layer name should start inline rename");
      if (!activate_layers_tool())
        return fail("The row interaction proof should return to Layers to verify the inline rename editor");
      QQuickItem* visible_rename_field = nullptr;
      for (auto* candidate : find_named_items(edit, QStringLiteral("photoLayerInlineNameField")))
        if (item_visible(candidate)) {
          visible_rename_field = candidate;
          break;
        }
      first_row_index = layer_index_for_id(first_layer_id);
      if (!visible_rename_field ||
          !invoke_bool("cancelInlinePhotoLayerRename", QVariant(first_row_index)) ||
          !settle())
        return fail("Inline rename should target the first row and close cleanly on cancel");

      second_row_index = layer_index_for_id(second_layer_id);
      auto* second_row = find_photo_row_by_id(second_layer_id);
      auto* second_name = second_row == nullptr
          ? nullptr : find_named_item(second_row, QStringLiteral("photoLayerNameLabel"));
      if (!second_name || !click_item(second_name) || !settle() ||
          !selected_adjustment_row(second_layer_id, second_row_index))
        return fail("Clicking the second adjustment row name should select it and keep Layers active");

      first_row_index = layer_index_for_id(first_layer_id);
      first_row = find_photo_row_by_id(first_layer_id);
      first_name = first_row == nullptr
          ? nullptr : find_named_item(first_row, QStringLiteral("photoLayerNameLabel"));
      if (!first_name || !click_item(first_name) || !settle() ||
          !selected_adjustment_row(first_layer_id, first_row_index))
        return fail("A plain click on A after selecting B should leave only A selected");
      second_row_index = layer_index_for_id(second_layer_id);
      second_row = find_photo_row_by_id(second_layer_id);
      second_name = second_row == nullptr
          ? nullptr : find_named_item(second_row, QStringLiteral("photoLayerNameLabel"));
      if (!second_name || !click_item(second_name) || !settle() ||
          !selected_adjustment_row(second_layer_id, second_row_index))
        return fail("A plain click back on B should restore a sole selection before modifier testing");

      first_row = find_photo_row_by_id(first_layer_id);
      auto* first_name_for_modifier_click = first_row == nullptr
          ? nullptr : find_named_item(first_row, QStringLiteral("photoLayerNameLabel"));
      auto* modifier_click_window = first_name_for_modifier_click
          ? first_name_for_modifier_click->window() : nullptr;
      if (!first_name_for_modifier_click || !modifier_click_window)
        return fail("A Control-click on the first layer name should reach the row selection handler");
      QTest::keyPress(modifier_click_window, Qt::Key_Control);
      process_layout();
      const bool control_click_sent = click_item(first_name_for_modifier_click, Qt::ControlModifier);
      // Mirror physical key-up before the next ordinary click.
      QTest::keyRelease(modifier_click_window, Qt::Key_Control);
      process_layout();
      if (!control_click_sent || !settle())
        return fail("A Control-click on the first layer name should reach the row selection handler");
      first_row_index = layer_index_for_id(first_layer_id);
      second_row_index = layer_index_for_id(second_layer_id);
      const auto modifier_selection = edit->property("selectedPhotoLayerIndices").toList();
      int first_selected_count = 0;
      int second_selected_count = 0;
      for (const auto& value : modifier_selection) {
        if (value.toInt() == first_row_index) ++first_selected_count;
        if (value.toInt() == second_row_index) ++second_selected_count;
      }
      if (modifier_selection.size() != 2 || first_selected_count != 1 || second_selected_count != 1 ||
          edit->property("editTool").toString() != QStringLiteral("layers") ||
          edit->property("editInspectorTab").toString() != QStringLiteral("layers")) {
        std::cerr << "Layer modifier selection probe: selection-size=" << modifier_selection.size()
                  << " first-count=" << first_selected_count
                  << " second-count=" << second_selected_count
                  << " editTool=" << edit->property("editTool").toString().toStdString()
                  << " inspectorTab=" << edit->property("editInspectorTab").toString().toStdString() << '\n';
        return fail("A Control-click should toggle the first row into the selection exactly once");
      }
      auto* legacy_command_bar = find_named_item(edit, QStringLiteral("photoLayerAdjustmentCommandBar"));
      QQuickItem* clear_selection_button = nullptr;
      for (auto* candidate : find_named_items(edit, QStringLiteral("clearPhotoLayerSelectionButton"))) {
        auto* item = qobject_cast<QQuickItem*>(candidate);
        if (item_visible(item) && item->isEnabled()) {
          clear_selection_button = item;
          break;
        }
      }
      edit->setProperty("photoLayerDetailsExpanded", true);
      if (item_visible(legacy_command_bar) || !clear_selection_button || !settle() ||
          !capture(QStringLiteral("layer-row-multiselection"), 1440, 900))
        return fail("Multi-layer selection should show one compact action header and capture cleanly");
      proof_root->setProperty("width", 1900);
      proof_root->setProperty("height", 980);
      if (!settle())
        return fail("The row interaction proof should restore its wide inspector after capture");
      clear_selection_button = nullptr;
      for (auto* candidate : find_named_items(edit, QStringLiteral("clearPhotoLayerSelectionButton"))) {
        auto* item = qobject_cast<QQuickItem*>(candidate);
        if (item_visible(item) && item->isEnabled()) {
          clear_selection_button = item;
          break;
        }
      }
      const auto active_index_before_clear = edit->property("activePhotoLayerIndex").toInt();
      if (!clear_selection_button || !click_item(clear_selection_button) || !settle() ||
          edit->property("activePhotoLayerIndex").toInt() != active_index_before_clear ||
          edit->property("selectedPhotoLayerIndices").toList().size() != 1 ||
          edit->property("selectedPhotoLayerIndices").toList().first().toInt() != active_index_before_clear ||
          edit->property("editTool").toString() != QStringLiteral("layers"))
        return fail("Clear selection should keep the active row and remove only its multi-selection peers");
      if (!invoke_bool("setActivePhotoLayer", QVariant(layer_index_for_id(second_layer_id))) ||
          !settle() ||
          edit->property("activePhotoLayerIndex").toInt() != layer_index_for_id(second_layer_id) ||
          edit->property("selectedPhotoLayerIndices").toList().size() != 1)
        return fail("The second row should be restored as the sole target for visibility and More checks");

      const auto click_empty_canvas_to_clear_row_focus = [&] {
        auto* canvas_picker = find_named_item(edit, QStringLiteral("photoLayerCanvasPicker"));
        const auto content_x = viewport->property("paintedContentX").toDouble();
        const auto content_y = viewport->property("paintedContentY").toDouble();
        const auto content_width = viewport->property("paintedContentWidth").toDouble();
        const auto content_height = viewport->property("paintedContentHeight").toDouble();
        if (!canvas_picker || !item_visible(canvas_picker) || !canvas_picker->isEnabled() ||
            content_width <= 0 || content_height <= 0)
          return false;

        // This point is clear of the fixture's centered rectangle and both
        // text layers. The real canvas picker gives the editor focus while
        // leaving the active layer selection untouched.
        const QPointF canvas_local_point(content_x + content_width * 0.92,
                                         content_y + content_height * 0.08);
        const auto canvas_scene_point = viewport->mapToScene(canvas_local_point);
        QTest::mouseMove(proof_window, canvas_scene_point.toPoint(), 10);
        process_layout();
        QTest::mouseClick(proof_window, Qt::LeftButton, Qt::NoModifier,
                          canvas_scene_point.toPoint());
        process_layout();
        auto* edit_item = qobject_cast<QQuickItem*>(edit);
        return edit_item && proof_window->activeFocusItem() == edit_item &&
               edit->property("activePhotoLayerIndex").toInt() == layer_index_for_id(second_layer_id) &&
               edit->property("selectedPhotoLayerIndices").toList().size() == 1;
      };

      first_row_index = layer_index_for_id(first_layer_id);
      second_row_index = layer_index_for_id(second_layer_id);
      first_row = find_photo_row_by_id(first_layer_id);
      auto* visibility_button = find_named_item(first_row, QStringLiteral("photoLayerVisibilityButton"));
      auto* action_zone = find_named_item(first_row, QStringLiteral("photoLayerFixedStateActionZone"));
      if (!click_empty_canvas_to_clear_row_focus())
        return fail("An empty canvas click should return keyboard focus to Edit without changing layer selection");
      if (!first_row || !action_zone || !visibility_button || !move_pointer_to_item(viewport) ||
          !wait_for_condition([&] {
            process_layout();
            return !action_zone->isEnabled() && !visibility_button->isEnabled() &&
                   action_zone->property("opacity").toDouble() < 0.01;
          }, 2000))
        return fail("Secondary row actions should start hidden and disabled when the pointer is outside the row");
      const auto row_geometry_before_hover = scene_rect(first_row);
      const auto action_geometry_before_hover = scene_rect(action_zone);
      const bool visibility_item_found = visibility_button != nullptr;
      const auto visibility_item_width = visibility_button ? visibility_button->width() : 0.0;
      const auto visibility_row_width = first_row ? first_row->width() : 0.0;
      const auto visibility_action_budget = first_row
          ? first_row->property("layerActionWidthBudget").toInt() : -1;
      if (!move_pointer_to_item(first_row) ||
          !wait_for_condition([&] {
            process_layout();
            return action_zone->property("actionsRevealed").toBool() &&
                   action_zone->isEnabled() && visibility_button->isEnabled() &&
                   action_zone->property("opacity").toDouble() > 0.99;
          }, 2000))
        return fail("Hovering a layer row should reveal its secondary actions before interaction");
      const auto row_geometry_after_hover = scene_rect(first_row);
      const auto action_geometry_after_hover = scene_rect(action_zone);
      const auto same_geometry = [](const QRectF& first, const QRectF& second) {
        return qAbs(first.x() - second.x()) <= 0.5 &&
               qAbs(first.y() - second.y()) <= 0.5 &&
               qAbs(first.width() - second.width()) <= 0.5 &&
               qAbs(first.height() - second.height()) <= 0.5;
      };
      if (!same_geometry(row_geometry_before_hover, row_geometry_after_hover) ||
          !same_geometry(action_geometry_before_hover, action_geometry_after_hover))
        return fail("Revealing layer row actions should not shift the row or its reserved action zone");
      const bool visibility_item_visible = item_visible(visibility_button);
      const bool visibility_item_enabled = visibility_button && visibility_button->isEnabled();
      const auto visibility_before_layer = editor_preview.photoLayers().value(first_row_index).toMap();
      const bool visibility_before_enabled = visibility_before_layer.value(QStringLiteral("enabled")).toBool();
      const bool visibility_click_sent = visibility_item_visible && click_item(visibility_button);
      const bool visibility_settled = visibility_click_sent && settle();
      const int visibility_active_index_after = edit->property("activePhotoLayerIndex").toInt();
      const QString visibility_edit_tool_after = edit->property("editTool").toString();
      first_row_index = layer_index_for_id(first_layer_id);
      second_row_index = layer_index_for_id(second_layer_id);
      const auto visibility_after_layer = editor_preview.photoLayers().value(first_row_index).toMap();
      const bool visibility_after_enabled = visibility_after_layer.value(QStringLiteral("enabled")).toBool();
      if (!visibility_item_visible || !visibility_item_enabled || !visibility_settled ||
          visibility_active_index_after != second_row_index ||
          visibility_edit_tool_after != QStringLiteral("layers") ||
          visibility_before_enabled == visibility_after_enabled) {
        std::cerr << "Layer visibility probe: found=" << visibility_item_found
                  << " visible=" << visibility_item_visible
                  << " enabled=" << visibility_item_enabled
                  << " button-width=" << visibility_item_width
                  << " row-width=" << visibility_row_width
                  << " action-budget=" << visibility_action_budget
                  << " click-sent=" << visibility_click_sent
                  << " settled=" << visibility_settled
                  << " active-after=" << visibility_active_index_after
                  << " expected-active=" << second_row_index
                  << " tool-after=" << visibility_edit_tool_after.toStdString()
                  << " layer-enabled-before=" << visibility_before_enabled
                  << " layer-enabled-after=" << visibility_after_enabled << '\n';
        return fail("Visibility on the first row should not replace the second row selection");
      }
      first_row = find_photo_row_by_id(first_layer_id);
      visibility_button = find_named_item(first_row, QStringLiteral("photoLayerVisibilityButton"));
      const bool visibility_second_click = click_item(visibility_button);
      const bool visibility_second_settled = visibility_second_click && settle();
      const bool visibility_restored = layer_data_by_id(first_layer_id)
                                           .value(QStringLiteral("enabled")).toBool() == visibility_before_enabled;
      if (!visibility_second_settled || !visibility_restored ||
          edit->property("activePhotoLayerIndex").toInt() != layer_index_for_id(second_layer_id) ||
          edit->property("editTool").toString() != QStringLiteral("layers"))
        return fail("A second eye click should restore the first row without changing the second selection");

      first_row = find_photo_row_by_id(first_layer_id);
      auto* more_button = find_named_item(first_row, QStringLiteral("photoLayerRowMoreMenuButton"));
      if (!more_button || !click_item(more_button) ||
          !wait_for_condition([&] {
            process_layout();
            auto* item = find_named_item(edit, QStringLiteral("layerAction_toggle-lock"));
            return item && item_visible(item);
          }, 5000) || edit->property("editTool").toString() != QStringLiteral("layers") ||
          edit->property("activePhotoLayerIndex").toInt() != second_row_index)
        return fail("Opening the first row's More menu should preserve the second row selection");
      auto* lock_menu_item = find_named_item(edit, QStringLiteral("layerAction_toggle-lock"));
      if (!lock_menu_item || !click_item(lock_menu_item) || !settle() ||
          edit->property("editTool").toString() != QStringLiteral("layers") ||
          edit->property("activePhotoLayerIndex").toInt() != layer_index_for_id(second_layer_id) ||
          !layer_data_by_id(first_layer_id).value(QStringLiteral("locked")).toBool())
        return fail("Lock from More should not activate the first row");
      first_row_index = layer_index_for_id(first_layer_id);
      second_row_index = layer_index_for_id(second_layer_id);
      first_row = find_photo_row_by_id(first_layer_id);
      more_button = find_named_item(first_row, QStringLiteral("photoLayerRowMoreMenuButton"));
      if (!click_item(more_button) ||
          !wait_for_condition([&] {
            process_layout();
            auto* item = find_named_item(edit, QStringLiteral("layerAction_toggle-lock"));
            return item && item_visible(item) &&
                   item->property("text").toString().contains(QStringLiteral("Unlock"));
          }, 5000))
        return fail("More should refresh the lock label after the layer state changes");
      auto* unlock_menu_item = find_named_item(edit, QStringLiteral("layerAction_toggle-lock"));
      if (!unlock_menu_item || !click_item(unlock_menu_item) || !settle() ||
          edit->property("editTool").toString() != QStringLiteral("layers") ||
          edit->property("activePhotoLayerIndex").toInt() != layer_index_for_id(second_layer_id) ||
          layer_data_by_id(first_layer_id).value(QStringLiteral("locked")).toBool())
        return fail("Unlock should act on the first row and preserve the second row selection");

      first_row_index = layer_index_for_id(first_layer_id);
      second_row_index = layer_index_for_id(second_layer_id);
      first_row = find_photo_row_by_id(first_layer_id);
      auto* target_row = find_photo_row(first_row_index + 1);
      const auto first_layer_model = layer_data_by_id(first_layer_id);
      const auto target_layer_model = target_row ? target_row->property("layerData").toMap() : QVariantMap{};
      auto* source_name = first_row == nullptr
          ? nullptr : find_named_item(first_row, QStringLiteral("photoLayerNameLabel"));
      auto* source_edit_area = first_row == nullptr
          ? nullptr : find_named_item(first_row, QStringLiteral("photoLayerRowEditTargetArea"));
      if (!first_row || !target_row ||
          !first_layer_model.value(QStringLiteral("parentId")).toString().isEmpty() ||
          first_layer_model.value(QStringLiteral("kind")).toString() == QStringLiteral("group") ||
          target_layer_model.value(QStringLiteral("baseImageEdit")).toBool() ||
          target_layer_model.value(QStringLiteral("locked")).toBool() ||
          !target_layer_model.value(QStringLiteral("parentId")).toString().isEmpty() ||
          target_layer_model.value(QStringLiteral("kind")).toString() == QStringLiteral("group") ||
          !source_name || !source_edit_area || !item_visible(source_name) || !item_visible(target_row))
        return fail("The first row should expose an adjacent unlocked row for name dragging");
      auto* drag_window = source_name->window();
      const auto source_pointer = source_name->mapToScene(
          QPointF(source_name->width() * 0.5, source_name->height() * 0.5));
      const auto target_pointer = target_row->mapToScene(
          QPointF(target_row->width() * 0.5, target_row->height() * 0.5));
      const auto source_row_scene_rect = scene_rect(first_row);
      const auto target_row_scene_rect = scene_rect(target_row);
      const auto source_name_scene_rect = scene_rect(source_name);
      QPointer<QQuickItem> guarded_source_edit_area(source_edit_area);
      const auto source_row_data_before_drag = first_row->property("layerData").toMap();
      const auto target_row_data_before_drag = target_row->property("layerData").toMap();
      const auto source_name_text = source_name->property("text").toString();
      if (!drag_window || QLineF(source_pointer, target_pointer).length() <
              QGuiApplication::styleHints()->startDragDistance() + 2.0)
        return fail("Adjacent layer names should allow a pointer drag");
      const auto source_point = source_pointer.toPoint();
      const auto target_point = target_pointer.toPoint();
      QTest::mousePress(drag_window, Qt::LeftButton, Qt::NoModifier, source_point, 10);
      process_layout();
      const bool edit_area_alive_after_press = !guarded_source_edit_area.isNull();
      const bool edit_area_pressed_after_press = guarded_source_edit_area &&
          guarded_source_edit_area->property("pressed").toBool();
      const auto mouse_buttons_after_press = QGuiApplication::mouseButtons();
      constexpr int drag_steps = 10;
      for (int step = 1; step <= drag_steps; ++step) {
        const auto point = source_point + (target_point - source_point) * step / drag_steps;
        QTest::mouseMove(drag_window, point, 10);
        process_layout();
        QTest::qWait(10);
      }
      const bool edit_area_alive_after_move = !guarded_source_edit_area.isNull();
      const bool edit_area_pressed_after_move = guarded_source_edit_area &&
          guarded_source_edit_area->property("pressed").toBool();
      const auto mouse_buttons_after_move = QGuiApplication::mouseButtons();
      const auto edit_area_mouse_x_after_move = guarded_source_edit_area
          ? guarded_source_edit_area->property("mouseX").toDouble() : -1.0;
      const auto edit_area_mouse_y_after_move = guarded_source_edit_area
          ? guarded_source_edit_area->property("mouseY").toDouble() : -1.0;
      const auto edit_area_contains_mouse_after_move = guarded_source_edit_area
          ? guarded_source_edit_area->property("containsMouse").toBool() : false;
      auto* window_mouse_grabber_after_move = drag_window ? drag_window->mouseGrabberItem() : nullptr;
      auto* source_row_after_move = find_photo_row_by_id(first_layer_id);
      const bool row_drag_active_after_move = source_row_after_move &&
          source_row_after_move->property("reorderDragActive").toBool();
      auto* reorder_handle_after_move = source_row_after_move
          ? find_named_item(source_row_after_move, QStringLiteral("photoLayerReorderHandle")) : nullptr;
      const bool handle_drag_active_after_move = reorder_handle_after_move &&
          reorder_handle_after_move->property("dragActive").toBool();
      auto* reorder_proxy_after_move = find_named_item(edit, QStringLiteral("photoLayerReorderDragProxy"));
      const auto reorder_proxy_y_after_move = reorder_proxy_after_move
          ? reorder_proxy_after_move->y() : 0.0;
      const int reorder_drop_target_after_move =
          edit->property("photoLayerReorderDropTargetIndex").toInt();
      const int hierarchy_drop_target_after_move =
          edit->property("photoLayerHierarchyDropTargetIndex").toInt();
      const int active_index_after_move = edit->property("activePhotoLayerIndex").toInt();
      QTest::mouseRelease(drag_window, Qt::LeftButton, Qt::NoModifier, target_point, 10);
      process_layout();
      if (!wait_for_condition([&] {
            process_layout();
            QVariant moved_index;
            return QMetaObject::invokeMethod(edit, "photoLayerIndexForId",
                                             Q_RETURN_ARG(QVariant, moved_index),
                                             Q_ARG(QVariant, QVariant(first_layer_id))) &&
                   moved_index.toInt() > first_row_index;
          }, 5000)) {
        if (proof_window)
          proof_window->grabWindow().save(QStringLiteral("/tmp/looksy-layer-name-drag-failure.png"));
        std::cerr << "Layer name drag probe: source-index=" << first_row_index
                  << " source-after=" << layer_index_for_id(first_layer_id)
                  << " target-index=" << first_row_index + 1
                  << " source-row-id=" << source_row_data_before_drag.value(QStringLiteral("id")).toString().toStdString()
                  << " target-row-id=" << target_row_data_before_drag.value(QStringLiteral("id")).toString().toStdString()
                  << " source-row-index=" << first_row_index
                  << " target-row-index=" << first_row_index + 1
                  << " source-row-rect=" << source_row_scene_rect.x() << ',' << source_row_scene_rect.y()
                  << ',' << source_row_scene_rect.width() << ',' << source_row_scene_rect.height()
                  << " target-row-rect=" << target_row_scene_rect.x() << ',' << target_row_scene_rect.y()
                  << ',' << target_row_scene_rect.width() << ',' << target_row_scene_rect.height()
                  << " source-name=" << source_name_text.toStdString()
                  << " source-name-rect=" << source_name_scene_rect.x() << ',' << source_name_scene_rect.y()
                  << ',' << source_name_scene_rect.width() << ',' << source_name_scene_rect.height()
                  << " source-hit-path=" << hit_path_at_window_point(drag_window, source_pointer).toStdString()
                  << " edit-area-alive-after-press=" << edit_area_alive_after_press
                  << " edit-area-pressed-after-press=" << edit_area_pressed_after_press
                  << " mouse-buttons-after-press=" << static_cast<int>(mouse_buttons_after_press)
                  << " edit-area-alive-after-move=" << edit_area_alive_after_move
                  << " edit-area-pressed-after-move=" << edit_area_pressed_after_move
                  << " mouse-buttons-after-move=" << static_cast<int>(mouse_buttons_after_move)
                  << " edit-area-mouse-after-move=" << edit_area_mouse_x_after_move << ','
                  << edit_area_mouse_y_after_move
                  << " edit-area-contains-mouse-after-move=" << edit_area_contains_mouse_after_move
                  << " window-grabber-after-move="
                  << (window_mouse_grabber_after_move
                          ? (QString::fromLatin1(window_mouse_grabber_after_move->metaObject()->className()) +
                             QStringLiteral("#") + window_mouse_grabber_after_move->objectName()).toStdString()
                          : std::string("<none>"))
                  << " active-after-move=" << active_index_after_move
                  << " source-row-after-move=" << (source_row_after_move != nullptr)
                  << " row-drag-active=" << row_drag_active_after_move
                  << " handle-drag-active=" << handle_drag_active_after_move
                  << " proxy-y=" << reorder_proxy_y_after_move
                  << " reorder-target=" << reorder_drop_target_after_move
                  << " hierarchy-target=" << hierarchy_drop_target_after_move
                  << " source-point=" << source_pointer.x() << ',' << source_pointer.y()
                  << " target-point=" << target_pointer.x() << ',' << target_pointer.y() << '\n';
        return fail("Dragging the first row by its name should reorder it after its adjacent sibling");
      }

      QString mask_content_target_id;
      for (const auto& value : editor_preview.photoLayers()) {
        const auto layer = value.toMap();
        const auto layer_id = layer.value(QStringLiteral("id")).toString();
        const auto kind = layer.value(QStringLiteral("kind")).toString();
        if (layer_id.isEmpty() || layer_id == first_layer_id || layer_id == second_layer_id ||
            layer.value(QStringLiteral("baseImageEdit")).toBool() ||
            layer.value(QStringLiteral("locked")).toBool() || kind == QStringLiteral("adjustment") ||
            kind == QStringLiteral("group"))
          continue;
        mask_content_target_id = layer_id;
        break;
      }
      if (mask_content_target_id.isEmpty())
        return fail("The focused layer proof should find an unlocked content layer for a linked mask");
      editor_preview.deselectActiveMask();
      const auto focused_layer_count_before_mask = editor_preview.photoLayerCount();
      const auto focused_mask_count_before = editor_preview.maskCount();
      const auto focused_recipe_before_mask = photo_editor::desktop::parse_adjustment_patch_json(
          editor_preview.currentAssetBoundAdjustmentPatchJson());
      if (!focused_recipe_before_mask.ok())
        return fail("The focused row proof should parse the authored recipe before selection creation");
      const auto focused_authored_layers_before_mask = focused_recipe_before_mask.state->layers;
      if (!QMetaObject::invokeMethod(edit, "createMaskSource",
                                     Q_ARG(QVariant, QVariant(QStringLiteral("radial")))) || !settle())
        return fail("The focused layer proof should create a radial source through the normal UI path");
      const auto focused_recipe_after_mask = photo_editor::desktop::parse_adjustment_patch_json(
          editor_preview.currentAssetBoundAdjustmentPatchJson());
      const bool focused_authored_layers_unchanged = focused_recipe_after_mask.ok() &&
          focused_recipe_after_mask.state->layers == focused_authored_layers_before_mask;
      const bool focused_tool_is_masks =
          edit->property("editTool").toString() == QStringLiteral("masks");
      const bool focused_tab_is_layers =
          edit->property("editInspectorTab").toString() == QStringLiteral("layers");
      if (editor_preview.maskCount() != focused_mask_count_before + 1 ||
          editor_preview.photoLayerCount() != focused_layer_count_before_mask ||
          !focused_authored_layers_unchanged ||
          !editor_preview.hasWorkingSelection() || !editor_preview.hasActiveMask() ||
          editor_preview.activeMaskKind() != QStringLiteral("radial") ||
          !focused_tool_is_masks || !focused_tab_is_layers) {
        const auto selected_target = edit->property("selectedEditTarget").toMap();
        std::cerr << "Focused radial selection source diagnostic: maskCount="
                  << editor_preview.maskCount() << " expectedMaskCount="
                  << (focused_mask_count_before + 1) << " layerCount="
                  << editor_preview.photoLayerCount() << " expectedLayerCount="
                  << focused_layer_count_before_mask << " authoredLayersUnchanged="
                  << focused_authored_layers_unchanged
                  << " hasWorkingSelection=" << editor_preview.hasWorkingSelection()
                  << " hasActiveMask=" << editor_preview.hasActiveMask()
                  << " activeMaskKind=" << editor_preview.activeMaskKind().toStdString()
                  << " editTool=" << edit->property("editTool").toString().toStdString()
                  << " editToolMatches=" << focused_tool_is_masks
                  << " inspectorTab=" << edit->property("editInspectorTab").toString().toStdString()
                  << " inspectorTabMatches=" << focused_tab_is_layers
                  << " targetScope=" << selected_target.value(QStringLiteral("scope")).toString().toStdString()
                  << " targetLayerId=" << selected_target.value(QStringLiteral("layerId")).toString().toStdString()
                  << " targetLayerIndex=" << selected_target.value(QStringLiteral("layerIndex")).toInt()
                  << '\n';
        return fail("The focused layer proof should create an editable radial selection without changing the layer stack");
      }
      const auto focused_working_set_id_before_attach = editor_preview.workingSelectionSetId();
      const auto focused_working_masks_before_attach = editor_preview.workingSelectionMasks();
      const int focused_mask_count_before_attach = editor_preview.maskCount();
      QVariant focused_unlinked_before_attach_value;
      if (!QMetaObject::invokeMethod(edit, "unlinkedMaskSets",
                                     Q_RETURN_ARG(QVariant, focused_unlinked_before_attach_value)))
        return fail("The focused row proof should read the independent working selection before attaching it");
      const auto focused_unlinked_before_attach = focused_unlinked_before_attach_value.toList();
      const auto focused_unlinked_before_attach_ids = mask_set_ids(focused_unlinked_before_attach,
                                                                   QStringLiteral("id"));
      if (focused_working_set_id_before_attach.isEmpty() || focused_working_masks_before_attach.isEmpty() ||
          mask_set_ids(focused_working_masks_before_attach, QStringLiteral("maskSetId")) !=
              QStringList{focused_working_set_id_before_attach} ||
          !focused_unlinked_before_attach_ids.contains(focused_working_set_id_before_attach))
        return fail("The focused radial source should exist as its own unlinked working selection");
      const auto mask_content_target_index = layer_index_for_id(mask_content_target_id);
      QVariant focused_linked_sets_before_value;
      if (mask_content_target_index < 0 ||
          !QMetaObject::invokeMethod(edit, "maskSetsForAdjustmentLayer",
                                     Q_RETURN_ARG(QVariant, focused_linked_sets_before_value),
                                     Q_ARG(QVariant, QVariant(mask_content_target_index))))
        return fail("The focused row proof should read the content layer's existing linked masks");
      const auto focused_linked_set_ids_before = mask_set_ids(
          focused_linked_sets_before_value.toList(), QStringLiteral("id"));
      QVariant focused_linked_selection;
      if (mask_content_target_index < 0 ||
          !QMetaObject::invokeMethod(edit, "useActiveSelectionAsLayerMask",
                                     Q_RETURN_ARG(QVariant, focused_linked_selection),
                                     Q_ARG(QVariant, QVariant(mask_content_target_index)),
                                     Q_ARG(QVariant, QVariant(false))) ||
          !focused_linked_selection.toBool() || !settle())
        return fail("The focused layer proof should link the new selection directly to a content layer");
      const auto linked_content_index = layer_index_for_id(mask_content_target_id);
      const auto focused_selected_layers = edit->property("selectedPhotoLayerIndices").toList();
      QVariant focused_linked_sets_after_value;
      QVariant focused_unlinked_sets_after_value;
      const bool focused_linked_sets_read_after =
          QMetaObject::invokeMethod(edit, "maskSetsForAdjustmentLayer",
                                    Q_RETURN_ARG(QVariant, focused_linked_sets_after_value),
                                    Q_ARG(QVariant, QVariant(linked_content_index)));
      const bool focused_unlinked_sets_read_after =
          QMetaObject::invokeMethod(edit, "unlinkedMaskSets",
                                    Q_RETURN_ARG(QVariant, focused_unlinked_sets_after_value));
      const auto focused_linked_set_ids_after = mask_set_ids(
          focused_linked_sets_after_value.toList(), QStringLiteral("id"));
      const auto focused_unlinked_after_ids = mask_set_ids(
          focused_unlinked_sets_after_value.toList(), QStringLiteral("id"));
      QStringList focused_new_linked_set_ids;
      for (const auto& id : focused_linked_set_ids_after)
        if (!focused_linked_set_ids_before.contains(id)) focused_new_linked_set_ids.append(id);
      const auto focused_new_layer_mask_id = focused_new_linked_set_ids.size() == 1
          ? focused_new_linked_set_ids.first() : QString();
      const auto focused_mask_stack_set_ids = mask_set_ids(
          editor_preview.maskStack(), QStringLiteral("maskSetId"));
      const auto focused_working_masks_after_attach = editor_preview.workingSelectionMasks();
      const bool focused_working_selection_retained = editor_preview.hasWorkingSelection() &&
          editor_preview.workingSelectionSetId() == focused_working_set_id_before_attach &&
          mask_set_ids(focused_working_masks_after_attach, QStringLiteral("maskSetId")) ==
              QStringList{focused_working_set_id_before_attach};
      const bool focused_independent_layer_mask_added =
          !focused_new_layer_mask_id.isEmpty() &&
          focused_new_layer_mask_id != focused_working_set_id_before_attach &&
          focused_mask_stack_set_ids.contains(focused_working_set_id_before_attach) &&
          focused_mask_stack_set_ids.contains(focused_new_layer_mask_id) &&
          focused_unlinked_after_ids == focused_unlinked_before_attach_ids &&
          focused_unlinked_after_ids.contains(focused_working_set_id_before_attach);
      const int expected_focused_mask_count_after_attach = focused_mask_count_before_attach +
          static_cast<int>(focused_working_masks_before_attach.size());
      if (linked_content_index < 0 ||
          edit->property("activePhotoLayerIndex").toInt() != linked_content_index ||
          !focused_selected_layers.contains(QVariant(linked_content_index)) ||
          editor_preview.photoLayerCount() != focused_layer_count_before_mask ||
          editor_preview.maskCount() != expected_focused_mask_count_after_attach ||
          !focused_linked_sets_read_after || !focused_unlinked_sets_read_after ||
          focused_linked_set_ids_after.size() != focused_linked_set_ids_before.size() + 1 ||
          !focused_working_selection_retained || !focused_independent_layer_mask_added ||
          edit->property("editTool").toString() != QStringLiteral("masks") ||
          edit->property("editInspectorTab").toString() != QStringLiteral("layers") || !settle()) {
        const auto selected_target = edit->property("selectedEditTarget").toMap();
        std::cerr << "Focused selection-to-layer-mask diagnostic: expectedLayerId="
                  << mask_content_target_id.toStdString() << " resolvedIndex=" << linked_content_index
                  << " activeLayerIndex=" << edit->property("activePhotoLayerIndex").toInt()
                  << " selectedCount=" << focused_selected_layers.size()
                  << " maskCount=" << editor_preview.maskCount()
                  << " expectedMaskCount=" << expected_focused_mask_count_after_attach
                  << " layerCount=" << editor_preview.photoLayerCount()
                  << " workingSetId=" << editor_preview.workingSelectionSetId().toStdString()
                  << " expectedWorkingSetId=" << focused_working_set_id_before_attach.toStdString()
                  << " linkedSetIds=" << focused_linked_set_ids_after.join(QLatin1Char(',')).toStdString()
                  << " newLayerMaskId=" << focused_new_layer_mask_id.toStdString()
                  << " unlinkedSetIds=" << focused_unlinked_after_ids.join(QLatin1Char(',')).toStdString()
                  << " workingRetained=" << focused_working_selection_retained
                  << " independentLayerMaskAdded=" << focused_independent_layer_mask_added
                  << " editTool=" << edit->property("editTool").toString().toStdString()
                  << " inspectorTab=" << edit->property("editInspectorTab").toString().toStdString()
                  << " targetScope=" << selected_target.value(QStringLiteral("scope")).toString().toStdString()
                  << " targetLayerId=" << selected_target.value(QStringLiteral("layerId")).toString().toStdString()
                  << '\n';
        return fail("The focused layer proof should explicitly attach the working selection to the selected content layer");
      }
      const auto linked_content_layer = layer_data_by_id(mask_content_target_id);
      QMetaObject::invokeMethod(edit, "setPhotoLayerMaskSectionCollapsed",
                                Q_ARG(QVariant, QVariant(linked_content_index)),
                                Q_ARG(QVariant, QVariant(linked_content_layer)),
                                Q_ARG(QVariant, QVariant(false)));
      edit->setProperty("photoLayerDetailsExpanded", false);
      process_layout();
      auto* focused_creative_close = find_named_item(edit, QStringLiteral("creativeToolsCloseButton"));
      if (item_visible(focused_creative_close) && !click_item(focused_creative_close))
        return fail("The linked-mask hierarchy proof should close the auxiliary tool panel through its normal action");
      if (!settle())
        return fail("The Layers inspector should reclaim space after closing its auxiliary tool panel");

      const auto focused_mask_rows = find_named_items(edit, QStringLiteral("photoLayerNestedMaskRow"));
      const auto focused_standalone_mask_rows = find_named_items(edit, QStringLiteral("photoLayerMaskSetRow"));
      int focused_visible_mask_rows = 0;
      for (auto* candidate : focused_mask_rows)
        if (item_visible(qobject_cast<QQuickItem*>(candidate))) ++focused_visible_mask_rows;
      int focused_visible_standalone_mask_rows = 0;
      for (auto* candidate : focused_standalone_mask_rows)
        if (item_visible(qobject_cast<QQuickItem*>(candidate))) ++focused_visible_standalone_mask_rows;
      QVariant focused_unlinked_mask_sets;
      if (!QMetaObject::invokeMethod(edit, "unlinkedMaskSets",
                                     Q_RETURN_ARG(QVariant, focused_unlinked_mask_sets)) ||
          editor_preview.photoLayerCount() != focused_layer_count_before_mask ||
          focused_visible_mask_rows != 1 ||
          focused_visible_standalone_mask_rows !=
              static_cast<int>(focused_unlinked_before_attach_ids.size()) ||
          mask_set_ids(focused_unlinked_mask_sets.toList(), QStringLiteral("id")) !=
              focused_unlinked_before_attach_ids)
        return fail("The focused layer mask should appear under its content layer while the original selection stays unlinked");

      const auto focused_mask_layout_is_valid = [&] {
        auto* nested_row = focused_mask_rows.isEmpty()
            ? nullptr : qobject_cast<QQuickItem*>(focused_mask_rows.first());
        auto* parent_row = find_photo_row_by_id(mask_content_target_id);
        auto* nested_name = nested_row == nullptr
            ? nullptr : find_named_item(nested_row, QStringLiteral("photoLayerNestedMaskNameLabel"));
        auto* properties_panel = find_named_item(edit, QStringLiteral("photoLayerPropertiesPanel"));
        auto* properties_name = properties_panel == nullptr
            ? nullptr : find_named_item(properties_panel, QStringLiteral("photoLayerPropertiesNameLabel"));
        const auto target_layer = layer_data_by_id(mask_content_target_id);
        const auto target_name = target_layer.value(QStringLiteral("displayName")).toString().isEmpty()
            ? target_layer.value(QStringLiteral("name")).toString()
            : target_layer.value(QStringLiteral("displayName")).toString();
        if (!nested_row || !parent_row || !nested_name || !properties_panel || !properties_name ||
            !item_visible(nested_row) || !item_visible(parent_row) || !item_visible(nested_name) ||
            !item_visible(properties_panel) || !item_visible(properties_name) ||
            nested_row->property("parentLayerIndex").toInt() != layer_index_for_id(mask_content_target_id) ||
            nested_row->property("modelData").toMap().value(QStringLiteral("id")).toString() !=
                focused_new_layer_mask_id ||
            nested_row->property("maskChildIndent").toInt() <= 0 ||
            scene_rect(nested_row).left() <= scene_rect(parent_row).left() + 1.0 ||
            target_name.trimmed().isEmpty() || properties_name->property("text").toString() != target_name ||
            properties_panel->property("layerIndex").toInt() != layer_index_for_id(mask_content_target_id))
          return false;
        int matching_visible_names = 0;
        for (auto* object : properties_panel->findChildren<QObject*>()) {
          auto* item = qobject_cast<QQuickItem*>(object);
          if (item && item_visible(item) && item->property("text").toString() == target_name)
            ++matching_visible_names;
        }
        return matching_visible_names == 1;
      };
      if (!settle() || !focused_mask_layout_is_valid() ||
          !capture(QStringLiteral("layer-row-nested-mask-properties"), 1440, 900))
        return fail("The focused Layers view should indent its linked mask and show the selected layer name once in Properties");
      if (!focused_mask_layout_is_valid() ||
          !capture(QStringLiteral("layer-row-nested-mask-properties-narrow"), 1100, 780) ||
          !focused_mask_layout_is_valid())
        return fail("Nested mask indentation and the single selected Properties name should persist at the narrow layout");

      std::cout << "Layer row interactions: selection, rename, hover actions, reorder, and linked mask hierarchy passed\n";
      editor_preview.setImageProvider(nullptr);
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
      return EXIT_SUCCESS;
    }
    const auto content_target_id = editor_preview.photoLayers().value(1).toMap()
                                       .value(QStringLiteral("id")).toString();
    if (content_target_id.isEmpty())
      return fail("Layer workspace should identify the content layer that will own the mask");

    // Creating a selection source edits only the working selection. Attaching
    // it to content is a separate action and does not add a stack row.
    editor_preview.deselectActiveMask();
    const auto layer_count_before_mask = editor_preview.photoLayerCount();
    const auto mask_count_before = editor_preview.maskCount();
    const auto recipe_before_mask = photo_editor::desktop::parse_adjustment_patch_json(
        editor_preview.currentAssetBoundAdjustmentPatchJson());
    if (!recipe_before_mask.ok())
      return fail("Layer workspace should parse the authored recipe before selection creation");
    const auto authored_layers_before_mask = recipe_before_mask.state->layers;
    if (!QMetaObject::invokeMethod(edit, "createMaskSource",
                                   Q_ARG(QVariant, QVariant(QStringLiteral("radial")))) || !settle())
      return fail("Layer workspace should create a radial source through the normal UI path");
    const auto recipe_after_mask = photo_editor::desktop::parse_adjustment_patch_json(
        editor_preview.currentAssetBoundAdjustmentPatchJson());
    const bool authored_layers_unchanged = recipe_after_mask.ok() &&
        recipe_after_mask.state->layers == authored_layers_before_mask;
    const bool source_tool_is_masks = edit->property("editTool").toString() == QStringLiteral("masks");
    const bool source_tab_is_layers = edit->property("editInspectorTab").toString() == QStringLiteral("layers");
    if (editor_preview.maskCount() != mask_count_before + 1 ||
        editor_preview.photoLayerCount() != layer_count_before_mask ||
        !authored_layers_unchanged ||
        !editor_preview.hasWorkingSelection() || !editor_preview.hasActiveMask() ||
        editor_preview.activeMaskKind() != QStringLiteral("radial") ||
        !source_tool_is_masks || !source_tab_is_layers) {
      const auto selected_target = edit->property("selectedEditTarget").toMap();
      std::cerr << "Radial selection source diagnostic: maskCount=" << editor_preview.maskCount()
                << " expectedMaskCount=" << (mask_count_before + 1)
                << " layerCount=" << editor_preview.photoLayerCount()
                << " expectedLayerCount=" << layer_count_before_mask
                << " authoredLayersUnchanged=" << authored_layers_unchanged
                << " hasWorkingSelection=" << editor_preview.hasWorkingSelection()
                << " hasActiveMask=" << editor_preview.hasActiveMask()
                << " activeMaskKind=" << editor_preview.activeMaskKind().toStdString()
                << " editTool=" << edit->property("editTool").toString().toStdString()
                << " editToolMatches=" << source_tool_is_masks
                << " inspectorTab=" << edit->property("editInspectorTab").toString().toStdString()
                << " inspectorTabMatches=" << source_tab_is_layers
                << " targetScope=" << selected_target.value(QStringLiteral("scope")).toString().toStdString()
                << " targetLayerId=" << selected_target.value(QStringLiteral("layerId")).toString().toStdString()
                << " targetLayerIndex=" << selected_target.value(QStringLiteral("layerIndex")).toInt()
                << '\n';
      return fail("Creating a radial selection should preserve authored layers and leave Masks open in the Layers inspector");
    }
    int content_target_index = -1;
    for (int index = 0; index < editor_preview.photoLayerCount(); ++index) {
      if (editor_preview.photoLayers().at(index).toMap().value(QStringLiteral("id")).toString() == content_target_id) {
        content_target_index = index;
        break;
      }
    }
    const auto working_set_id_before_attach = editor_preview.workingSelectionSetId();
    const auto working_masks_before_attach = editor_preview.workingSelectionMasks();
    const int mask_count_before_attach = editor_preview.maskCount();
    QVariant unlinked_sets_before_attach_value;
    QVariant linked_sets_before_attach_value;
    if (content_target_index < 0 ||
        !QMetaObject::invokeMethod(edit, "unlinkedMaskSets",
                                   Q_RETURN_ARG(QVariant, unlinked_sets_before_attach_value)) ||
        !QMetaObject::invokeMethod(edit, "maskSetsForAdjustmentLayer",
                                   Q_RETURN_ARG(QVariant, linked_sets_before_attach_value),
                                   Q_ARG(QVariant, QVariant(content_target_index))))
      return fail("Layer workspace should read independent and linked mask identities before attachment");
    const auto unlinked_sets_before_attach = unlinked_sets_before_attach_value.toList();
    const auto unlinked_set_ids_before_attach = mask_set_ids(unlinked_sets_before_attach,
                                                             QStringLiteral("id"));
    const auto linked_set_ids_before_attach = mask_set_ids(linked_sets_before_attach_value.toList(),
                                                           QStringLiteral("id"));
    if (working_set_id_before_attach.isEmpty() || working_masks_before_attach.isEmpty() ||
        mask_set_ids(working_masks_before_attach, QStringLiteral("maskSetId")) !=
            QStringList{working_set_id_before_attach} ||
        !unlinked_set_ids_before_attach.contains(working_set_id_before_attach))
      return fail("The radial source should exist as its own unlinked working selection before attachment");
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
        linked_content_index.toInt() < 0 || !settle())
      return fail("The content layer should remain addressable after linking its mask");
    const auto selected_layers_after_link = edit->property("selectedPhotoLayerIndices").toList();
    const int active_layer_after_link = edit->property("activePhotoLayerIndex").toInt();
    const bool link_tool_is_masks = edit->property("editTool").toString() == QStringLiteral("masks");
    const bool link_tab_is_layers = edit->property("editInspectorTab").toString() == QStringLiteral("layers");
    QVariant linked_sets_after_attach_value;
    QVariant unlinked_sets_after_attach_value;
    const bool linked_sets_read_after_attach =
        QMetaObject::invokeMethod(edit, "maskSetsForAdjustmentLayer",
                                  Q_RETURN_ARG(QVariant, linked_sets_after_attach_value),
                                  Q_ARG(QVariant, QVariant(linked_content_index.toInt())));
    const bool unlinked_sets_read_after_attach =
        QMetaObject::invokeMethod(edit, "unlinkedMaskSets",
                                  Q_RETURN_ARG(QVariant, unlinked_sets_after_attach_value));
    const auto linked_set_ids_after_attach = mask_set_ids(linked_sets_after_attach_value.toList(),
                                                          QStringLiteral("id"));
    const auto unlinked_set_ids_after_attach = mask_set_ids(unlinked_sets_after_attach_value.toList(),
                                                            QStringLiteral("id"));
    QStringList new_linked_set_ids;
    for (const auto& id : linked_set_ids_after_attach)
      if (!linked_set_ids_before_attach.contains(id)) new_linked_set_ids.append(id);
    const auto new_layer_mask_id = new_linked_set_ids.size() == 1
        ? new_linked_set_ids.first() : QString();
    const auto mask_stack_set_ids_after_attach = mask_set_ids(editor_preview.maskStack(),
                                                              QStringLiteral("maskSetId"));
    const auto working_masks_after_attach = editor_preview.workingSelectionMasks();
    const bool working_selection_retained = editor_preview.hasWorkingSelection() &&
        editor_preview.workingSelectionSetId() == working_set_id_before_attach &&
        mask_set_ids(working_masks_after_attach, QStringLiteral("maskSetId")) ==
            QStringList{working_set_id_before_attach};
    const bool independent_layer_mask_added = !new_layer_mask_id.isEmpty() &&
        new_layer_mask_id != working_set_id_before_attach &&
        mask_stack_set_ids_after_attach.contains(working_set_id_before_attach) &&
        mask_stack_set_ids_after_attach.contains(new_layer_mask_id) &&
        unlinked_set_ids_after_attach == unlinked_set_ids_before_attach &&
        unlinked_set_ids_after_attach.contains(working_set_id_before_attach);
    const int expected_mask_count_after_attach = mask_count_before_attach +
        static_cast<int>(working_masks_before_attach.size());
    if (active_layer_after_link != linked_content_index.toInt() ||
        !selected_layers_after_link.contains(QVariant(linked_content_index.toInt())) ||
        editor_preview.photoLayerCount() != layer_count_before_mask ||
        editor_preview.maskCount() != expected_mask_count_after_attach ||
        !linked_sets_read_after_attach || !unlinked_sets_read_after_attach ||
        linked_set_ids_after_attach.size() != linked_set_ids_before_attach.size() + 1 ||
        !working_selection_retained || !independent_layer_mask_added ||
        !link_tool_is_masks || !link_tab_is_layers) {
      const auto selected_target = edit->property("selectedEditTarget").toMap();
      std::cerr << "Selection-to-layer-mask diagnostic: targetId=" << content_target_id.toStdString()
                << " targetIndex=" << linked_content_index.toInt()
                << " activeIndex=" << active_layer_after_link
                << " selectedCount=" << selected_layers_after_link.size()
                << " layerCount=" << editor_preview.photoLayerCount()
                << " workingSetId=" << editor_preview.workingSelectionSetId().toStdString()
                << " expectedWorkingSetId=" << working_set_id_before_attach.toStdString()
                << " maskCount=" << editor_preview.maskCount()
                << " expectedMaskCount=" << expected_mask_count_after_attach
                << " linkedSetIds=" << linked_set_ids_after_attach.join(QLatin1Char(',')).toStdString()
                << " newLayerMaskId=" << new_layer_mask_id.toStdString()
                << " unlinkedSetIds=" << unlinked_set_ids_after_attach.join(QLatin1Char(',')).toStdString()
                << " workingRetained=" << working_selection_retained
                << " independentLayerMaskAdded=" << independent_layer_mask_added
                << " editTool=" << edit->property("editTool").toString().toStdString()
                << " editToolMatches=" << link_tool_is_masks
                << " inspectorTab=" << edit->property("editInspectorTab").toString().toStdString()
                << " inspectorTabMatches=" << link_tab_is_layers
                << " targetScope=" << selected_target.value(QStringLiteral("scope")).toString().toStdString()
                << " targetLayerId=" << selected_target.value(QStringLiteral("layerId")).toString().toStdString()
                << '\n';
      return fail("Applying the working selection should attach it to the content layer and keep that layer selected");
    }
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
        visible_linked_mask_rows != 1 ||
        visible_standalone_mask_rows != static_cast<int>(unlinked_set_ids_before_attach.size()) ||
        mask_set_ids(unlinked_mask_sets.toList(), QStringLiteral("id")) !=
            unlinked_set_ids_before_attach)
      return fail("The copied layer mask should appear under its content layer while the original working selection remains standalone");
    const auto find_new_linked_mask_row = [&]() -> QQuickItem* {
      for (auto* candidate : find_named_items(edit, QStringLiteral("photoLayerNestedMaskRow"))) {
        auto* row = qobject_cast<QQuickItem*>(candidate);
        if (row && row->property("parentLayerIndex").toInt() == linked_content_index.toInt() &&
            row->property("modelData").toMap().value(QStringLiteral("id")).toString() == new_layer_mask_id)
          return row;
      }
      return nullptr;
    };
    auto* nested_mask_row = find_new_linked_mask_row();
    auto* nested_mask_name = nested_mask_row
        ? find_named_item(nested_mask_row, QStringLiteral("photoLayerNestedMaskNameLabel")) : nullptr;
    if (!nested_mask_row ||
        nested_mask_row->property("parentLayerIndex").toInt() != linked_content_index.toInt() ||
        nested_mask_row->property("modelData").toMap().value(QStringLiteral("id")).toString() !=
            new_layer_mask_id ||
        nested_mask_row->property("combineOrderIndex").toInt() != 0 ||
        nested_mask_row->property("combineOrderCount").toInt() != 1 || !nested_mask_name ||
        nested_mask_name->property("text").toString().trimmed().isEmpty())
      return fail("A single nested mask should publish stable combine order and a readable name");
    proof_root->setProperty("width", 1100);
    proof_root->setProperty("height", 780);
    if (!settle())
      return fail("The nested mask label should settle after resizing the layer inspector");
    nested_mask_row = find_new_linked_mask_row();
    nested_mask_name = nested_mask_row
        ? find_named_item(nested_mask_row, QStringLiteral("photoLayerNestedMaskNameLabel")) : nullptr;
    if (!nested_mask_row || !nested_mask_name || !item_visible(nested_mask_name) ||
        nested_mask_name->width() < 100.0) {
      const auto current_row_model = nested_mask_row
          ? nested_mask_row->property("modelData").toMap() : QVariantMap{};
      std::cerr << "Nested mask label layout diagnostic: rowFound=" << (nested_mask_row != nullptr)
                << " rowVisible=" << item_visible(nested_mask_row)
                << " parentLayerIndex=" << (nested_mask_row
                    ? nested_mask_row->property("parentLayerIndex").toInt() : -1)
                << " expectedParentLayerIndex=" << linked_content_index.toInt()
                << " modelId=" << current_row_model.value(QStringLiteral("id")).toString().toStdString()
                << " expectedModelId=" << new_layer_mask_id.toStdString()
                << " nameFound=" << (nested_mask_name != nullptr)
                << " nameVisible=" << item_visible(nested_mask_name)
                << " nameText=" << (nested_mask_name
                    ? nested_mask_name->property("text").toString().toStdString() : std::string{})
                << " nameWidth=" << (nested_mask_name ? nested_mask_name->width() : -1.0)
                << " nameImplicitWidth=" << (nested_mask_name ? nested_mask_name->implicitWidth() : -1.0)
                << " labels=[";
      const auto all_mask_names = find_named_items(edit, QStringLiteral("photoLayerNestedMaskNameLabel"));
      for (int index = 0; index < all_mask_names.size() && index < 12; ++index) {
        auto* label = qobject_cast<QQuickItem*>(all_mask_names.at(index));
        if (index > 0) std::cerr << "; ";
        std::cerr << "visible=" << item_visible(label)
                  << ",width=" << (label ? label->width() : -1.0)
                  << ",text=" << (label
                      ? label->property("text").toString().toStdString() : std::string{});
      }
      std::cerr << "]\n";
      return fail("A nested mask should retain at least 100 pixels for its label at native 1100-pixel width");
    }
    edit->setProperty("photoLayerDetailsExpanded", true);
    auto* expanded_layer_properties = find_named_item(edit, QStringLiteral("photoLayerPropertiesPanel"));
    if (!settle() || !item_visible(expanded_layer_properties))
      return fail("The layer proof should explicitly expand properties before capturing them beside creative tools");
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
    proof_root->setProperty("width", 1900);
    proof_root->setProperty("height", 980);
    if (!settle())
      return fail("Layer Edit action proof should settle after widening the inspector");
    auto* inspector_scroll = find_named_item(edit, QStringLiteral("editInspectorScroll"));
    auto* inspector_scroll_handle = edit->property("editInspectorScrollHandle").value<QQuickItem*>();
    auto* inspector_flickable = inspector_scroll_handle
        ? qobject_cast<QQuickItem*>(inspector_scroll_handle->property("contentItem").value<QObject*>())
        : nullptr;
    const auto find_visible_edit_button = [&]() -> QQuickItem* {
      if (!inspector_scroll || !proof_window) return nullptr;
      const auto viewport_bounds = scene_rect(inspector_scroll);
      const QRectF window_bounds(0.0, 0.0, proof_window->width(), proof_window->height());
      for (auto* candidate : find_named_items(edit, QStringLiteral("photoLayerEditButton"))) {
        auto* item = qobject_cast<QQuickItem*>(candidate);
        if (!item_visible(item) || !item->isEnabled()) continue;
        const auto target_bounds = scene_rect(item);
        if (viewport_bounds.adjusted(0, 4, 0, -4).contains(target_bounds) &&
            window_bounds.adjusted(0, 4, 0, -4).contains(target_bounds))
          return item;
      }
      return nullptr;
    };
    const auto inspector_viewport_bounds = scene_rect(inspector_scroll);
    const auto wheel_position = inspector_viewport_bounds.center();
    const auto wheel_global = QPointF(proof_window->mapToGlobal(wheel_position.toPoint()));
    int edit_button_scroll_events = 0;
    QQuickItem* edit_button = find_visible_edit_button();
    while (!edit_button && inspector_scroll && inspector_flickable && edit_button_scroll_events < 24) {
      QWheelEvent wheel(wheel_position, wheel_global, QPoint(0, -180), QPoint(0, -120),
                        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
      QCoreApplication::sendEvent(proof_window, &wheel);
      process_layout();
      ++edit_button_scroll_events;
      edit_button = find_visible_edit_button();
    }
    if (edit_button && !wait_for_condition([&] {
          process_layout();
          return !inspector_flickable ||
              (!inspector_flickable->property("moving").toBool() &&
               !inspector_flickable->property("flicking").toBool());
        }, 5000))
      return fail("The layer inspector should finish wheel scrolling before clicking Edit");
    edit_button = find_visible_edit_button();
    const auto edit_button_clicks = edit_button
        ? std::make_unique<QSignalSpy>(edit_button, SIGNAL(clicked())) : nullptr;
    if (!edit_button || !click_item(edit_button) || !settle() ||
        edit->property("editTool").toString() != QStringLiteral("adjust") ||
        edit->property("editInspectorTab").toString() != QStringLiteral("adjust")) {
      auto* button_candidate = edit_button ? edit_button
          : find_named_item(edit, QStringLiteral("photoLayerEditButton"));
      const auto button_bounds = scene_rect(button_candidate);
      const auto selected_target = edit->property("selectedEditTarget").toMap();
      auto* properties_panel = find_named_item(edit, QStringLiteral("photoLayerPropertiesPanel"));
      const auto panel_layer = properties_panel
          ? properties_panel->property("layerData").toMap() : QVariantMap{};
      const auto button_center = button_bounds.center();
      proof_window->grabWindow().save(QDir(proof_dir).filePath(
          QStringLiteral("layer-edit-button-failure.png")));
      std::cerr << "Layer Edit button pointer diagnostic: foundInViewport=" << (edit_button != nullptr)
                << " clickedSignals=" << (edit_button_clicks ? edit_button_clicks->count() : -1)
                << " candidateVisible=" << item_visible(button_candidate)
                << " enabled=" << (button_candidate && button_candidate->isEnabled())
                << " scrollEvents=" << edit_button_scroll_events
                << " scrollBounds=" << inspector_viewport_bounds.x() << ',' << inspector_viewport_bounds.y()
                << ' ' << inspector_viewport_bounds.width() << 'x' << inspector_viewport_bounds.height()
                << " buttonBounds=" << button_bounds.x() << ',' << button_bounds.y()
                << ' ' << button_bounds.width() << 'x' << button_bounds.height()
                << " buttonCenterInScroll=" << inspector_viewport_bounds.contains(button_center)
                << " hitPath=" << hit_path_at_window_point(proof_window, button_center).toStdString()
                << " buttonCenterInWindow=" << QRectF(0.0, 0.0, proof_window->width(), proof_window->height())
                    .contains(button_center)
                << " contentY=" << (inspector_flickable
                    ? inspector_flickable->property("contentY").toDouble() : -1.0)
                << " contentHeight=" << (inspector_flickable
                    ? inspector_flickable->property("contentHeight").toDouble() : -1.0)
                << " flickableHeight=" << (inspector_flickable ? inspector_flickable->height() : -1.0)
                << " activeLayer=" << edit->property("activePhotoLayerIndex").toInt()
                << " propertiesLayerIndex=" << (properties_panel
                    ? properties_panel->property("layerIndex").toInt() : -1)
                << " propertiesLayerId=" << panel_layer.value(QStringLiteral("id")).toString().toStdString()
                << " selectedTargetScope=" << selected_target.value(QStringLiteral("scope")).toString().toStdString()
                << " selectedTargetLayerId=" << selected_target.value(QStringLiteral("layerId")).toString().toStdString()
                << " editTool=" << edit->property("editTool").toString().toStdString()
                << " pendingSelectionAction=" << edit->property("pendingSelectionAction").toString().toStdString()
                << " selectionSessionActive=" << editor_preview.selectionEditSessionActive()
                << " inspectorTab=" << edit->property("editInspectorTab").toString().toStdString() << '\n';
      return fail("The visible layer Edit button should open adjustment controls after scrolling into view");
    }

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

    // Reordering must remain available from the layer name itself. The
    // name's double-click recognizer is passive once a drag crosses the
    // platform threshold, allowing the row target to keep the drag grab.
    proof_root->setProperty("width", 1900);
    proof_root->setProperty("height", 980);
    if (!settle())
      return fail("Layer name drag proof should settle after resizing the stack");
    QQuickItem* drag_source_row = nullptr;
    QQuickItem* drag_target_row = nullptr;
    int drag_source_index = -1;
    QString drag_source_id;
    QString drag_target_id;
    for (auto* candidate : find_named_items(edit, QStringLiteral("photoLayerRow"))) {
      if (!item_visible(candidate))
        continue;
      const auto layer = candidate->property("layerData").toMap();
      const auto index = candidate->property("layerIndex").toInt();
      const auto kind = layer.value(QStringLiteral("kind")).toString();
      if (layer.value(QStringLiteral("baseImageEdit")).toBool() ||
          layer.value(QStringLiteral("locked")).toBool() ||
          !layer.value(QStringLiteral("parentId")).toString().isEmpty() || kind == QStringLiteral("group"))
        continue;
      if (drag_source_row == nullptr) {
        drag_source_row = candidate;
        drag_source_index = index;
        drag_source_id = layer.value(QStringLiteral("id")).toString();
      } else if (index == drag_source_index + 1) {
        drag_target_row = candidate;
        drag_target_id = layer.value(QStringLiteral("id")).toString();
        break;
      } else {
        drag_source_row = candidate;
        drag_source_index = index;
        drag_source_id = layer.value(QStringLiteral("id")).toString();
      }
    }
    const auto row_by_layer_id = [&](const QString& layer_id) -> QQuickItem* {
      QVariant resolved_index;
      if (layer_id.isEmpty() ||
          !QMetaObject::invokeMethod(edit, "photoLayerIndexForId",
                                     Q_RETURN_ARG(QVariant, resolved_index),
                                     Q_ARG(QVariant, QVariant(layer_id))))
        return nullptr;
      for (auto* candidate : find_named_items(edit, QStringLiteral("photoLayerRow")))
        if (candidate->property("layerIndex").toInt() == resolved_index.toInt())
          return candidate;
      return nullptr;
    };
    const auto name_by_layer_id = [&](const QString& layer_id) -> QQuickItem* {
      auto* row = row_by_layer_id(layer_id);
      return row ? find_named_item(row, QStringLiteral("photoLayerNameLabel")) : nullptr;
    };
    auto* drag_source_name = name_by_layer_id(drag_source_id);
    auto* drag_target_name = name_by_layer_id(drag_target_id);
    if (!drag_source_row || !drag_target_row || drag_source_id.isEmpty() ||
        drag_target_id.isEmpty() || !drag_source_name || !drag_target_name ||
        !item_visible(drag_source_name) || !item_visible(drag_target_name))
      return fail("Layer reorder proof should expose two adjacent unlocked rows and a source name");
    auto* reorder_inspector_scroll = find_named_item(edit, QStringLiteral("editInspectorScroll"));
    auto* reorder_scroll_handle = edit->property("editInspectorScrollHandle").value<QQuickItem*>();
    auto* reorder_flickable = reorder_scroll_handle
        ? qobject_cast<QQuickItem*>(reorder_scroll_handle->property("contentItem").value<QObject*>())
        : nullptr;
    if (!reorder_inspector_scroll || !reorder_flickable)
      return fail("Layer reorder proof should find the real inspector scroll viewport");
    const auto row_names_visible_in_viewport = [&] {
      if (!drag_source_name || !drag_target_name || !item_visible(drag_source_name) ||
          !item_visible(drag_target_name))
        return false;
      const QRectF viewport_bounds = scene_rect(reorder_inspector_scroll);
      const QRectF window_bounds(0.0, 0.0, proof_window->width(), proof_window->height());
      return viewport_bounds.contains(scene_rect(drag_source_name).center()) &&
          viewport_bounds.contains(scene_rect(drag_target_name).center()) &&
          window_bounds.contains(scene_rect(drag_source_name).center()) &&
          window_bounds.contains(scene_rect(drag_target_name).center());
    };
    const auto reorder_viewport_bounds = scene_rect(reorder_inspector_scroll);
    const auto reorder_wheel_position = reorder_viewport_bounds.center();
    const auto reorder_wheel_global = QPointF(proof_window->mapToGlobal(reorder_wheel_position.toPoint()));
    int reorder_scroll_events = 0;
    while (!row_names_visible_in_viewport() && reorder_scroll_events < 48) {
      const auto source_center = scene_rect(drag_source_name).center();
      const auto target_center = scene_rect(drag_target_name).center();
      const auto viewport_now = scene_rect(reorder_inspector_scroll);
      const bool scroll_down = source_center.y() > viewport_now.bottom() ||
          target_center.y() > viewport_now.bottom();
      const QPoint pixel_delta(0, scroll_down ? -180 : 180);
      const QPoint angle_delta(0, scroll_down ? -120 : 120);
      const auto content_y_before = reorder_flickable->property("contentY").toDouble();
      QWheelEvent wheel(reorder_wheel_position, reorder_wheel_global, pixel_delta, angle_delta,
                        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
      QCoreApplication::sendEvent(proof_window, &wheel);
      ++reorder_scroll_events;
      if (!wait_for_condition([&] {
            process_layout();
            return !reorder_flickable->property("moving").toBool() &&
                !reorder_flickable->property("flicking").toBool();
          }, 5000))
        return fail("The layer reorder inspector should finish scrolling before pointer input");
      drag_source_name = name_by_layer_id(drag_source_id);
      drag_target_name = name_by_layer_id(drag_target_id);
      if (!row_names_visible_in_viewport() &&
          std::abs(reorder_flickable->property("contentY").toDouble() - content_y_before) < 0.5)
        break;
    }
    drag_source_row = row_by_layer_id(drag_source_id);
    drag_target_row = row_by_layer_id(drag_target_id);
    drag_source_name = name_by_layer_id(drag_source_id);
    drag_target_name = name_by_layer_id(drag_target_id);
    if (!row_names_visible_in_viewport()) {
      const auto source_bounds = scene_rect(drag_source_name);
      const auto target_bounds = scene_rect(drag_target_name);
      const auto viewport_bounds = scene_rect(reorder_inspector_scroll);
      std::cerr << "Layer reorder target visibility diagnostic: sourceId=" << drag_source_id.toStdString()
                << " sourceIndex=" << drag_source_index
                << " targetId=" << drag_target_id.toStdString()
                << " targetIndex=" << (drag_target_row ? drag_target_row->property("layerIndex").toInt() : -1)
                << " scrollEvents=" << reorder_scroll_events
                << " contentY=" << reorder_flickable->property("contentY").toDouble()
                << " contentHeight=" << reorder_flickable->property("contentHeight").toDouble()
                << " viewport=" << viewport_bounds.x() << ',' << viewport_bounds.y() << ' '
                << viewport_bounds.width() << 'x' << viewport_bounds.height()
                << " sourceNameBounds=" << source_bounds.x() << ',' << source_bounds.y() << ' '
                << source_bounds.width() << 'x' << source_bounds.height()
                << " targetNameBounds=" << target_bounds.x() << ',' << target_bounds.y() << ' '
                << target_bounds.width() << 'x' << target_bounds.height() << '\n';
      return fail("Layer reorder proof should scroll both exact sibling names into the visible inspector window");
    }
    auto* drag_window = drag_source_name->window();
    const auto source_pointer = drag_source_name->mapToScene(
        QPointF(drag_source_name->width() * 0.5, drag_source_name->height() * 0.5));
    const auto target_pointer = drag_target_name->mapToScene(
        QPointF(drag_target_name->width() * 0.5,
                drag_target_name->mapFromItem(drag_target_row, 0,
                    drag_target_row->height() * 0.85).y()));
    const QRectF drag_window_bounds(0.0, 0.0, drag_window ? drag_window->width() : 0.0,
                                    drag_window ? drag_window->height() : 0.0);
    if (!drag_window || QLineF(source_pointer, target_pointer).length() <
            QGuiApplication::styleHints()->startDragDistance() + 2.0 ||
        !drag_window_bounds.contains(source_pointer) || !drag_window_bounds.contains(target_pointer))
      return fail("Adjacent layer names should be far enough apart for a reorder drag");
    const auto source_global = QPointF(drag_window->mapToGlobal(source_pointer.toPoint()));
    const auto target_global = QPointF(drag_window->mapToGlobal(target_pointer.toPoint()));
    pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
    QMouseEvent reorder_press(QEvent::MouseButtonPress, source_pointer, source_global,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    reorder_press.setTimestamp(pointer_timestamp++);
    QCoreApplication::sendEvent(drag_window, &reorder_press);
    const auto reorder_move = [&](const QPointF& point, const QPointF& global) {
      QMouseEvent move(QEvent::MouseMove, point, global, Qt::NoButton,
                       Qt::LeftButton, Qt::NoModifier);
      move.setTimestamp(pointer_timestamp++);
      QCoreApplication::sendEvent(drag_window, &move);
      process_layout();
    };
    // A coalesced move must still arm the drag before release; a fast pointer
    // gesture cannot rely on a second movement event to commit its drop.
    reorder_move(target_pointer, target_global);
    const bool drag_armed_before_release = drag_source_row->property("reorderDragActive").toBool();
    QMouseEvent reorder_release(QEvent::MouseButtonRelease, target_pointer, target_global,
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    reorder_release.setTimestamp(pointer_timestamp++);
    QCoreApplication::sendEvent(drag_window, &reorder_release);
    const bool reordered = wait_for_condition([&] {
          process_layout();
          QVariant moved_index;
          return QMetaObject::invokeMethod(edit, "photoLayerIndexForId",
                                           Q_RETURN_ARG(QVariant, moved_index),
                                           Q_ARG(QVariant, QVariant(drag_source_id))) &&
                 moved_index.toInt() > drag_source_index;
        }, 5000);
    if (!reordered) {
      QVariant current_source_index;
      QVariant current_target_index;
      QMetaObject::invokeMethod(edit, "photoLayerIndexForId",
                                Q_RETURN_ARG(QVariant, current_source_index),
                                Q_ARG(QVariant, QVariant(drag_source_id)));
      QMetaObject::invokeMethod(edit, "photoLayerIndexForId",
                                Q_RETURN_ARG(QVariant, current_target_index),
                                Q_ARG(QVariant, QVariant(drag_target_id)));
      auto* current_source_row = row_by_layer_id(drag_source_id);
      auto* current_target_row = row_by_layer_id(drag_target_id);
      auto* current_source_name = name_by_layer_id(drag_source_id);
      auto* current_target_name = name_by_layer_id(drag_target_id);
      const auto current_viewport_bounds = scene_rect(reorder_inspector_scroll);
      std::cerr << "Layer reorder gesture diagnostic: sourceId=" << drag_source_id.toStdString()
                << " initialSourceIndex=" << drag_source_index
                << " currentSourceIndex=" << current_source_index.toInt()
                << " targetId=" << drag_target_id.toStdString()
                << " currentTargetIndex=" << current_target_index.toInt()
                << " scrollEvents=" << reorder_scroll_events
                << " contentY=" << reorder_flickable->property("contentY").toDouble()
                << " viewport=" << current_viewport_bounds.x() << ',' << current_viewport_bounds.y() << ' '
                << current_viewport_bounds.width() << 'x' << current_viewport_bounds.height()
                << " sourcePointer=" << source_pointer.x() << ',' << source_pointer.y()
                << " targetPointer=" << target_pointer.x() << ',' << target_pointer.y()
                << " sourcePointerInWindow=" << drag_window_bounds.contains(source_pointer)
                << " targetPointerInWindow=" << drag_window_bounds.contains(target_pointer)
                << " sourceRowAlive=" << (current_source_row != nullptr)
                << " targetRowAlive=" << (current_target_row != nullptr)
                << " sourceDragActive=" << (current_source_row &&
                    current_source_row->property("reorderDragActive").toBool())
                << " armedBeforeRelease=" << drag_armed_before_release
                << " hierarchyTarget=" << edit->property("photoLayerHierarchyDropTargetIndex").toInt()
                << " sourceName=" << (current_source_name
                    ? current_source_name->property("text").toString().toStdString() : std::string{})
                << " targetName=" << (current_target_name
                    ? current_target_name->property("text").toString().toStdString() : std::string{}) << '\n';
      return fail("Dragging from a layer name should reorder that layer after its sibling");
    }

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

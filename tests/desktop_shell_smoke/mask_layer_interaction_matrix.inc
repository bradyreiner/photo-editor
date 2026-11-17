{
  const auto matrix_photo_path = runtime_root.filePath(QStringLiteral("mask-layer-matrix-photo.png"));
  QImage matrix_photo(640, 480, QImage::Format_RGB32);
  for (int y = 0; y < matrix_photo.height(); ++y) {
    for (int x = 0; x < matrix_photo.width(); ++x) {
      const auto band = ((x / 32) + (y / 32)) % 2 == 0;
      matrix_photo.setPixelColor(x, y, band
          ? QColor(52 + (x * 120 / matrix_photo.width()), 94 + (y * 96 / matrix_photo.height()), 138)
          : QColor(166, 104 + (x * 70 / matrix_photo.width()), 62 + (y * 72 / matrix_photo.height())));
    }
  }
  if (!matrix_photo.save(matrix_photo_path))
    return fail("Mask and layer interaction matrix should write its synthetic photo");
  auto* matrix_qml_warning_sink = new QObject(&engine);
  QObject::connect(&engine, &QQmlEngine::warnings, matrix_qml_warning_sink,
      [matrix_qml_warning_sink](const QList<QQmlError>& warnings) {
        auto script_errors = matrix_qml_warning_sink->property("scriptErrors").toStringList();
        for (const auto& warning : warnings) {
          const auto text = warning.toString();
          const auto actionable_qml_error =
              text.contains(QStringLiteral("ReferenceError")) ||
              text.contains(QStringLiteral("TypeError")) ||
              text.contains(QStringLiteral("Binding loop detected"), Qt::CaseInsensitive) ||
              text.contains(QStringLiteral("Unable to assign"), Qt::CaseInsensitive);
          if (actionable_qml_error && !script_errors.contains(text))
            script_errors.append(text);
        }
        matrix_qml_warning_sink->setProperty("scriptErrors", script_errors);
      });
  catalog_assets.replaceAssetsForTesting({make_asset(
      QStringLiteral("mask-layer-matrix-photo"), matrix_photo_path,
      QUrl::fromLocalFile(matrix_photo_path).toString())});
  catalog_assets.selectIndex(0);
  app_shell.completeFirstRunExperience();
  engine.load(QUrl::fromLocalFile(main_qml_path));
  if (engine.rootObjects().isEmpty())
    return fail("Mask and layer interaction matrix should load Main.qml");
  auto* matrix_root = engine.rootObjects().constFirst();
  auto* matrix_window = qobject_cast<QQuickWindow*>(matrix_root);
  if (!matrix_window)
    return fail("Mask and layer interaction matrix requires the actual Main QQuickWindow");
  matrix_root->setProperty("width", 1440);
  matrix_root->setProperty("height", 900);
  matrix_root->setProperty("currentWorkspace", matrix_root->property("editWorkspaceIndex"));
  QObject* matrix_edit = nullptr;
  if (!wait_for_condition([&] {
        process_layout_events(2);
        matrix_edit = find_named_object(matrix_root, QStringLiteral("editStudioWorkspace"));
        return matrix_edit && matrix_edit->property("pageContentReady").toBool() &&
            !matrix_root->property("workspaceTransitionActive").toBool() &&
            editor_preview.hasAsset() && !editor_preview.processing() &&
            !editor_preview.fullRenderRunning();
      }, 20000))
    return fail("Mask and layer interaction matrix should reach a ready Edit workspace");

  const auto matrix_proof_dir = qEnvironmentVariableIsSet("PHOTO_EDITOR_VISUAL_PROOF_DIR")
      ? QString::fromUtf8(qgetenv("PHOTO_EDITOR_VISUAL_PROOF_DIR"))
      : runtime_root.filePath(QStringLiteral("mask-layer-interaction-matrix"));
  if (!QDir().mkpath(matrix_proof_dir))
    return fail("Mask and layer interaction matrix should create its evidence directory");
  QJsonArray matrix_captures;
  const auto settle_matrix = [&] {
    return wait_for_condition([&] {
      process_layout_events(2);
      return photo_editor::desktop::EditorPreviewControllerTestAccess::preview_idle(editor_preview) &&
          !editor_preview.professionalToolsBusy() && !editor_preview.photoPaintBusy();
    }, 15000);
  };
  const auto visible_matrix_rect = [&](QQuickItem* item) {
    QPointer<QQuickItem> live_item(item);
    if (!live_item || !item_visible(live_item) || live_item->window() != matrix_window) return QRectF{};
    auto rect = scene_rect(live_item).intersected(
        QRectF(0, 0, matrix_window->width(), matrix_window->height()));
    for (auto* parent = live_item->parentItem(); parent; parent = parent->parentItem()) {
      if (!parent->isVisible()) return QRectF{};
      if (parent->clip()) rect = rect.intersected(scene_rect(parent));
    }
    return rect;
  };
  const auto reveal_matrix_item = [&](QQuickItem* input_item) {
    QPointer<QQuickItem> item(input_item);
    if (!item || item->window() != matrix_window) return false;
    QVector<QPointer<QQuickItem>> scroll_ancestors;
    for (auto* ancestor = item->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
      if (ancestor->metaObject()->indexOfProperty("contentY") < 0 ||
          ancestor->metaObject()->indexOfProperty("contentHeight") < 0) continue;
      scroll_ancestors.append(QPointer<QQuickItem>(ancestor));
    }
    for (const auto& ancestor : scroll_ancestors) {
      if (!item || !ancestor) return false;
      const auto bounds = item->mapRectToItem(ancestor, item->boundingRect());
      const auto delta = bounds.top() < 3 ? bounds.top() - 3
          : bounds.bottom() > ancestor->height() - 3 ? bounds.bottom() - ancestor->height() + 3 : 0.0;
      if (!qFuzzyIsNull(delta)) {
        const auto maximum = qMax(0.0, ancestor->property("contentHeight").toDouble() - ancestor->height());
        ancestor->setProperty("contentY", qBound(0.0,
            ancestor->property("contentY").toDouble() + delta, maximum));
      }
    }
    process_layout_events(3);
    if (!item) return false;
    QVector<QPointer<QQuickItem>> row_ancestors;
    for (auto* ancestor = item.data(); ancestor; ancestor = ancestor->parentItem())
      row_ancestors.append(QPointer<QQuickItem>(ancestor));
    for (const auto& ancestor : row_ancestors) {
      if (!item) return false;
      if (!ancestor) continue;
      const auto name = ancestor->objectName();
      if (name != QStringLiteral("photoLayerRow") &&
          name != QStringLiteral("photoLayerNestedMaskRow") &&
          name != QStringLiteral("photoLayerMaskSetRow")) continue;
      const auto rect = visible_matrix_rect(ancestor);
      if (rect.width() < 2 || rect.height() < 2) return false;
      QTest::mouseMove(matrix_window, rect.center().toPoint());
      process_layout_events(3);
      if (!item || !ancestor) return false;
      break;
    }
    return !visible_matrix_rect(item).isEmpty();
  };
  const auto reveal_matrix_pair = [&](QQuickItem* first_input, QQuickItem* second_input) {
    QPointer<QQuickItem> first(first_input);
    QPointer<QQuickItem> second(second_input);
    if (!first || !second || !reveal_matrix_item(first) || !first || !second ||
        !reveal_matrix_item(second) || !first || !second) return false;
    QPointer<QQuickItem> shared_scroll;
    for (auto* ancestor = first->parentItem(); ancestor && !shared_scroll; ancestor = ancestor->parentItem()) {
      if (ancestor->metaObject()->indexOfProperty("contentY") < 0 ||
          ancestor->metaObject()->indexOfProperty("contentHeight") < 0) continue;
      for (auto* other = second->parentItem(); other; other = other->parentItem()) {
        if (other == ancestor) { shared_scroll = QPointer<QQuickItem>(ancestor); break; }
      }
    }
    if (shared_scroll) {
      if (!first || !second) return false;
      const auto first_rect = first->mapRectToItem(shared_scroll, first->boundingRect());
      const auto second_rect = second->mapRectToItem(shared_scroll, second->boundingRect());
      const auto combined_top = qMin(first_rect.top(), second_rect.top());
      const auto combined_bottom = qMax(first_rect.bottom(), second_rect.bottom());
      const auto current_y = shared_scroll->property("contentY").toDouble();
      const auto desired_y = current_y +
          ((combined_top + combined_bottom) * 0.5 - shared_scroll->height() * 0.5);
      const auto maximum = qMax(0.0, shared_scroll->property("contentHeight").toDouble() - shared_scroll->height());
      shared_scroll->setProperty("contentY", qBound(0.0, desired_y, maximum));
      process_layout_events(3);
    }
    if (!first || !second) return false;
    return !visible_matrix_rect(first).isEmpty() && !visible_matrix_rect(second).isEmpty();
  };
  const auto click_matrix_item = [&](QQuickItem* input_item,
                                     Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QPointer<QQuickItem> item(input_item);
    if (!item || !reveal_matrix_item(item) || !item || !item->isEnabled()) return false;
    const auto rect = visible_matrix_rect(item);
    if (rect.width() < 2 || rect.height() < 2) return false;
    QTest::mouseClick(matrix_window, Qt::LeftButton, modifiers, rect.center().toPoint());
    process_layout_events(4);
    return true;
  };
  const auto invoke_matrix_bool = [&](const char* method, const QVariant& value = QVariant()) {
    QVariant result;
    const auto invoked = value.isValid()
        ? QMetaObject::invokeMethod(matrix_edit, method, Q_RETURN_ARG(QVariant, result),
                                    Q_ARG(QVariant, value))
        : QMetaObject::invokeMethod(matrix_edit, method, Q_RETURN_ARG(QVariant, result));
    return invoked && result.toBool();
  };
  const auto canonical_matrix_layers = [&]() -> QVariantList {
    const auto parsed = photo_editor::desktop::parse_adjustment_patch_json(
        editor_preview.currentAssetBoundAdjustmentPatchJson());
    return parsed.ok() ? parsed.state->layers : QVariantList{};
  };
  const auto matrix_recipe = [&] {
    return photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview);
  };
  const auto matrix_layer_index = [&](const QString& id) {
    QVariant result;
    return QMetaObject::invokeMethod(matrix_edit, "photoLayerIndexForId",
        Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QVariant(id))) ? result.toInt() : -1;
  };
  const auto matrix_layer = [&](const QString& id) {
    const auto index = matrix_layer_index(id);
    return index >= 0 ? editor_preview.photoLayers().value(index).toMap() : QVariantMap{};
  };
  const auto matrix_layer_row = [&](const QString& id) -> QQuickItem* {
    const auto index = matrix_layer_index(id);
    for (auto* row : find_named_items(matrix_edit, QStringLiteral("photoLayerRow")))
      if (row->property("layerIndex").toInt() == index) return row;
    return nullptr;
  };
  const auto item_descendant_named = [](QQuickItem* root, const QString& name) -> QQuickItem* {
    if (!root) return nullptr;
    QList<QQuickItem*> pending{root};
    while (!pending.isEmpty()) {
      auto* item = pending.takeLast();
      if (item->objectName() == name) return item;
      pending.append(item->childItems());
    }
    return nullptr;
  };
  const auto matrix_mask_linked = [&](const QString& layer_id, const QString& set_id) {
    const auto ids = matrix_layer(layer_id).value(QStringLiteral("selectionMaskSetIds")).toStringList();
    if (ids.contains(set_id)) return true;
    for (const auto& value : matrix_layer(layer_id).value(QStringLiteral("selectionMaskSetIds")).toList())
      if (value.toString() == set_id) return true;
    return matrix_layer(layer_id).value(QStringLiteral("selectionMaskSetId")).toString() == set_id;
  };
  const auto matrix_mask_present = [&](const QString& set_id) {
    for (const auto& value : editor_preview.maskStack()) {
      const auto mask = value.toMap();
      if (mask.value(QStringLiteral("maskSetId")).toString() == set_id ||
          mask.value(QStringLiteral("id")).toString() == set_id) return true;
    }
    return false;
  };
  const auto matrix_mask_id_list = [&] {
    QStringList ids;
    for (const auto& value : editor_preview.maskStack()) {
      const auto mask = value.toMap();
      auto id = mask.value(QStringLiteral("maskSetId")).toString();
      if (id.isEmpty()) id = mask.value(QStringLiteral("id")).toString();
      if (!id.isEmpty() && !ids.contains(id)) ids.append(id);
    }
    ids.sort(Qt::CaseSensitive);
    return ids;
  };
  const auto nested_mask_thumbnail = [&](const QString& layer_id, const QString& set_id) -> QQuickItem* {
    for (auto* thumbnail_frame : find_named_items(matrix_edit, QStringLiteral("photoLayerNestedMaskThumbnail"))) {
      auto* row = thumbnail_frame;
      while (row && row->objectName() != QStringLiteral("photoLayerNestedMaskRow")) row = row->parentItem();
      if (!row) continue;
      const auto data = row->property("modelData").toMap();
      if (data.value(QStringLiteral("id")).toString() == set_id &&
          row->property("parentLayerIndex").toInt() == matrix_layer_index(layer_id)) {
        QList<QQuickItem*> pending{thumbnail_frame};
        while (!pending.isEmpty()) {
          auto* candidate = pending.takeLast();
          const auto* meta = candidate->metaObject();
          if (meta->indexOfProperty("dragging") >= 0 &&
              candidate->property("maskSetId").toString() == set_id &&
              candidate->property("sourceLayerId").toString() == layer_id)
            return candidate;
          pending.append(candidate->childItems());
        }
      }
    }
    return nullptr;
  };
  const auto expand_nested_mask_section = [&](const QString& layer_id) {
    auto* row = matrix_layer_row(layer_id);
    auto* section = row ? item_descendant_named(row, QStringLiteral("photoLayerNestedMaskSection")) : nullptr;
    if (!section) return false;
    if (section->property("expanded").toBool()) return true;
    QPointer<QQuickItem> header(item_descendant_named(section,
        QStringLiteral("photoLayerNestedMaskSectionHeader")));
    if (!header || !click_matrix_item(header.data())) return false;
    return wait_for_condition([&] {
      process_layout_events(2);
      auto* current_row = matrix_layer_row(layer_id);
      auto* current_section = current_row
          ? item_descendant_named(current_row, QStringLiteral("photoLayerNestedMaskSection")) : nullptr;
      return current_section && current_section->property("expanded").toBool();
    }, 3000);
  };
  const auto mask_menu_button = [&](const QString& layer_id, const QString& set_id,
                                    bool linked) -> QQuickItem* {
    const auto name = linked ? QStringLiteral("photoLayerNestedMaskRowMoreMenuButton")
                             : QStringLiteral("photoLayerMaskSetRowMoreMenuButton");
    QQuickItem* fallback = nullptr;
    for (auto* button : find_named_items(matrix_edit, name)) {
      auto* row = button;
      const auto expected_row_name = linked ? QStringLiteral("photoLayerNestedMaskRow")
                                            : QStringLiteral("photoLayerMaskSetRow");
      while (row && row->objectName() != expected_row_name) row = row->parentItem();
      if (!row || row->property("modelData").toMap().value(QStringLiteral("id")).toString() != set_id)
        continue;
      if (linked && row->property("parentLayerIndex").toInt() != matrix_layer_index(layer_id)) continue;
      if (!fallback) fallback = button;
      if (!visible_matrix_rect(button).isEmpty()) return button;
    }
    return fallback;
  };
  const auto click_matrix_menu_opener = [&](QQuickItem* button_input) {
    QPointer<QQuickItem> button(button_input);
    if (!button) return false;
    auto* action = find_named_item(button, button->objectName() + QStringLiteral("MenuAction"));
    if (!action || !item_visible(action) || !action->isEnabled()) action = button;
    return click_matrix_item(action);
  };
  const auto trigger_matrix_menu = [&](QQuickItem* button_input, const QString& key) {
    QPointer<QQuickItem> button(button_input);
    if (!button) {
      std::cerr << "mask-menu-trigger key=" << key.toStdString() << " stage=no-button\n";
      return false;
    }
    const auto owner_name = button->objectName();
    const auto opener_clicked = click_matrix_menu_opener(button);
    if (!opener_clicked || !button) {
      std::cerr << "mask-menu-trigger key=" << key.toStdString()
                << " stage=opener clicked=" << opener_clicked
                << " buttonAlive=" << !button.isNull()
                << " visible=" << (!button.isNull() && item_visible(button))
                << " enabled=" << (!button.isNull() && button->isEnabled())
                << " object=" << owner_name.toStdString() << '\n';
      return false;
    }
    QPointer<QObject> popup(find_named_object(button, owner_name + QStringLiteral("Popup")));
    const auto popup_opened = popup && wait_for_condition([&] {
      return popup && popup->property("opened").toBool();
    }, 3000);
    if (!popup_opened || !popup) {
      std::cerr << "mask-menu-trigger key=" << key.toStdString()
                << " stage=popup opened=" << popup_opened
                << " popupAlive=" << !popup.isNull()
                << " object=" << owner_name.toStdString() << '\n';
      return false;
    }
    const auto items = popup->property("menuItems").toList();
    int item_index = -1;
    for (int index = 0; index < items.size(); ++index)
      if (items.at(index).toMap().value(QStringLiteral("key")).toString() == key) item_index = index;
    if (item_index < 0) {
      std::cerr << "mask-menu-trigger key=" << key.toStdString() << " stage=missing-item items=";
      for (const auto& value : items)
        std::cerr << value.toMap().value(QStringLiteral("key")).toString().toStdString() << ',';
      std::cerr << '\n';
      return false;
    }
    QPointer<QQuickItem> menu_item(find_named_item(popup,
        owner_name + QStringLiteral("PopupItem%1").arg(item_index)));
    const auto item_clicked = menu_item && menu_item->isEnabled() && click_matrix_item(menu_item);
    if (!item_clicked) {
      std::cerr << "mask-menu-trigger key=" << key.toStdString()
                << " stage=menu-item itemAlive=" << !menu_item.isNull()
                << " itemEnabled=" << (!menu_item.isNull() && menu_item->isEnabled())
                << " itemVisible=" << (!menu_item.isNull() && item_visible(menu_item))
                << " itemIndex=" << item_index
                << " modelItem=" << QJsonDocument::fromVariant(items.at(item_index)).toJson(QJsonDocument::Compact).toStdString()
                << '\n';
      return false;
    }
    const auto closed = wait_for_condition([&] { return !popup || !popup->property("visible").toBool(); }, 3000);
    if (!closed)
      std::cerr << "mask-menu-trigger key=" << key.toStdString() << " stage=popup-close visible="
                << (!popup.isNull() && popup->property("visible").toBool()) << '\n';
    return closed;
  };
  const auto matrix_pixels = [&] {
    return editor_preview_image_provider->image(QStringLiteral("edited"));
  };
  const auto matrix_cpu_render = [&](const QString& recipe) {
    const auto state = photo_editor::desktop::parse_edit_recipe_json(recipe);
    return photo_editor::desktop::render_flattened_output_file(
        QStringLiteral("mask-layer-matrix-photo"), matrix_photo_path, state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32);
  };
  const auto matrix_max_channel_delta = [](const QImage& left_input, const QImage& right_input) {
    if (left_input.isNull() || right_input.isNull() || left_input.size() != right_input.size()) return 256;
    const auto left = left_input.convertToFormat(QImage::Format_ARGB32);
    const auto right = right_input.convertToFormat(QImage::Format_ARGB32);
    int maximum_delta = 0;
    for (int y = 0; y < left.height(); ++y) {
      for (int x = 0; x < left.width(); ++x) {
        const auto a = left.pixel(x, y);
        const auto b = right.pixel(x, y);
        maximum_delta = qMax(maximum_delta, qAbs(qRed(a) - qRed(b)));
        maximum_delta = qMax(maximum_delta, qAbs(qGreen(a) - qGreen(b)));
        maximum_delta = qMax(maximum_delta, qAbs(qBlue(a) - qBlue(b)));
        maximum_delta = qMax(maximum_delta, qAbs(qAlpha(a) - qAlpha(b)));
      }
    }
    return maximum_delta;
  };
  const auto matrix_pixels_match_cpu = [&](const QImage& pixels, const QImage& expected) {
    // The interactive provider is ARGB32 while the canonical CPU renderer can
    // round from higher precision. Allow only two 8-bit levels per channel.
    return matrix_max_channel_delta(pixels, expected) <= 2;
  };
  const auto matrix_preview_view_state = [&] {
    const auto target = matrix_edit->property("selectedEditTarget").toMap();
    const auto active_index = editor_preview.activeMaskIndex();
    const auto active_mask = active_index >= 0 && active_index < editor_preview.maskStack().size()
        ? editor_preview.maskStack().at(active_index).toMap() : QVariantMap{};
    return QJsonObject{{QStringLiteral("maskPreviewMode"), editor_preview.maskPreviewMode()},
        {QStringLiteral("maskPreviewDisplayMode"), editor_preview.maskPreviewDisplayMode()},
        {QStringLiteral("maskPreviewShowAll"), editor_preview.maskPreviewShowAll()},
        {QStringLiteral("maskPreviewTemporarilyHidden"), editor_preview.maskPreviewTemporarilyHidden()},
        {QStringLiteral("maskPreviewOverlayOpacity"), editor_preview.maskPreviewOverlayOpacity()},
        {QStringLiteral("displayPreviewOverlaySource"), editor_preview.displayPreviewOverlaySource()},
        {QStringLiteral("activeMaskIndex"), active_index},
        {QStringLiteral("activeMaskId"), active_mask.value(QStringLiteral("id")).toString()},
        {QStringLiteral("activeMaskSetId"), active_mask.value(QStringLiteral("maskSetId")).toString()},
        {QStringLiteral("activeMaskKind"), active_mask.value(QStringLiteral("kind")).toString()},
        {QStringLiteral("activeMaskSetSelectionInvert"), active_mask.value(QStringLiteral("maskSetSelectionInvert")).toBool()},
        {QStringLiteral("editingLayerMask"), editor_preview.editingLayerMask()},
        {QStringLiteral("maskEditLayerId"), editor_preview.maskEditLayerId()},
        {QStringLiteral("selectedTargetScope"), target.value(QStringLiteral("scope")).toString()},
        {QStringLiteral("selectedTargetLayerId"), target.value(QStringLiteral("layerId")).toString()},
        {QStringLiteral("selectedTargetMaskSetId"), target.value(QStringLiteral("maskSetId")).toString()},
        {QStringLiteral("editTool"), matrix_edit->property("editTool").toString()},
        {QStringLiteral("editInspectorTab"), matrix_edit->property("editInspectorTab").toString()}};
  };
  QJsonObject matrix_history_before_action_view;
  QJsonObject matrix_history_after_action_view;
  QJsonObject matrix_history_after_undo_view;
  QJsonObject matrix_history_after_redo_view;
  const auto send_matrix_mouse = [&](QEvent::Type type, const QPointF& point,
                                     Qt::MouseButton button, Qt::MouseButtons buttons,
                                     Qt::KeyboardModifiers modifiers) {
    const QPointF global(matrix_window->mapToGlobal(point.toPoint()));
    QMouseEvent event(type, point, global, button, buttons, modifiers);
    QCoreApplication::sendEvent(matrix_window, &event);
    process_layout_events(2);
  };
  QString drag_mask_stage;
  const auto drag_mask = [&](QQuickItem* source_input, QQuickItem* destination_input,
                             Qt::KeyboardModifiers modifiers) {
    QPointer<QQuickItem> source(source_input);
    QPointer<QQuickItem> destination(destination_input);
    drag_mask_stage = QStringLiteral("reveal-pair");
    if (!reveal_matrix_pair(source, destination)) return false;
    if (!source || !destination) {
      drag_mask_stage = QStringLiteral("delegate-removed-during-reveal");
      return false;
    }
    const auto source_rect = visible_matrix_rect(source);
    const auto target_rect = visible_matrix_rect(destination);
    if (source_rect.width() < 8 || source_rect.height() < 8 ||
        target_rect.width() < 8 || target_rect.height() < 8) {
      drag_mask_stage = QStringLiteral("insufficient-visible-geometry");
      return false;
    }
    const auto start = source_rect.center();
    const auto end = QPointF(target_rect.center().x(), target_rect.top() + target_rect.height() * 0.72);
    drag_mask_stage = QStringLiteral("press");
    send_matrix_mouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, modifiers);
    if (!source) {
      drag_mask_stage = QStringLiteral("source-removed-on-press");
      send_matrix_mouse(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, modifiers);
      return false;
    }
    drag_mask_stage = QStringLiteral("first-move");
    send_matrix_mouse(QEvent::MouseMove, start + QPointF(12, 4), Qt::NoButton, Qt::LeftButton, modifiers);
    if (!source) {
      drag_mask_stage = QStringLiteral("source-removed-on-first-move");
      send_matrix_mouse(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, modifiers);
      return false;
    }
    drag_mask_stage = QStringLiteral("middle-move");
    send_matrix_mouse(QEvent::MouseMove, (start + end) * 0.5, Qt::NoButton, Qt::LeftButton, modifiers);
    if (!source) {
      drag_mask_stage = QStringLiteral("source-removed-on-middle-move");
      send_matrix_mouse(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, modifiers);
      return false;
    }
    drag_mask_stage = QStringLiteral("target-move");
    send_matrix_mouse(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, modifiers);
    if (!source) {
      drag_mask_stage = QStringLiteral("source-removed-on-target-move");
      send_matrix_mouse(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, modifiers);
      return false;
    }
    const auto drag_started = source->property("dragging").toBool();
    drag_mask_stage = drag_started ? QStringLiteral("release-started-drag") : QStringLiteral("release-without-drag");
    send_matrix_mouse(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, modifiers);
    if (!drag_started) return false;
    drag_mask_stage = QStringLiteral("settle");
    const auto settled = settle_matrix();
    if (settled) drag_mask_stage = QStringLiteral("completed");
    return settled;
  };
  const auto undo_redo_matrix_recipe = [&](const QString& before_recipe, const QImage& before_pixels,
                                           const QString& after_recipe, const QImage& after_pixels) {
    auto* edit_item = qobject_cast<QQuickItem*>(matrix_edit);
    if (!edit_item || !matrix_window || !settle_matrix()) return false;
    edit_item->forceActiveFocus();
    const auto undo_dispatched = send_window_key(matrix_window, Qt::Key_Z, Qt::ControlModifier);
    const auto undo_settled = undo_dispatched && wait_for_condition([&] {
      return settle_matrix() && matrix_recipe() == before_recipe && matrix_pixels() == before_pixels;
    }, 10000);
    const auto undo_recipe_matches = matrix_recipe() == before_recipe;
    const auto undo_pixels = matrix_pixels();
    const auto undo_pixels_match = !undo_pixels.isNull() && undo_pixels == before_pixels;
    matrix_history_after_undo_view = matrix_preview_view_state();
    if (!undo_settled || !undo_recipe_matches || !undo_pixels_match) {
      const auto render_cpu = [&](const QString& recipe) {
        const auto state = photo_editor::desktop::parse_edit_recipe_json(recipe);
        return photo_editor::desktop::render_flattened_output_file(
            QStringLiteral("mask-layer-matrix-photo"), matrix_photo_path, state,
            QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64);
      };
      const auto before_cpu = render_cpu(before_recipe);
      const auto after_cpu = render_cpu(after_recipe);
      std::cerr << "mask-history stage=undo dispatched=" << undo_dispatched
                << " settled=" << undo_settled << " recipeMatches=" << undo_recipe_matches
                << " providerPixelsMatch=" << undo_pixels_match
                << " currentHistory=" << editor_preview.editHistoryCurrentIndex()
                << " currentRecipeBytes=" << matrix_recipe().size()
                << " expectedRecipeBytes=" << before_recipe.size()
                << " providerBeforeSize=" << before_pixels.width() << 'x' << before_pixels.height()
                << " providerAfterSize=" << undo_pixels.width() << 'x' << undo_pixels.height()
                << " cpuImagesValid=" << !before_cpu.isNull() << ',' << !after_cpu.isNull()
                << " cpuChange=" << (before_cpu != after_cpu)
                << " providerMatchesExpectedBefore=" << undo_pixels_match << '\n';
      if (!before_cpu.isNull()) before_cpu.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-history-before-cpu.png")));
      if (!after_cpu.isNull()) after_cpu.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-history-after-cpu.png")));
      if (!before_pixels.isNull()) before_pixels.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-history-undo-expected-before.png")));
      if (!undo_pixels.isNull()) undo_pixels.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-history-undo-provider-actual.png")));
      if (!after_pixels.isNull()) after_pixels.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-history-undo-expected-after.png")));
      const auto failure_image = matrix_window->grabWindow();
      failure_image.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-history-undo-failure.png")));
      return false;
    }
    edit_item->forceActiveFocus();
    const auto redo_dispatched = send_window_key(
        matrix_window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    const auto redo_settled = redo_dispatched && wait_for_condition([&] {
      return settle_matrix() && matrix_recipe() == after_recipe && matrix_pixels() == after_pixels;
    }, 10000);
    const auto redo_recipe_matches = matrix_recipe() == after_recipe;
    const auto redo_pixels = matrix_pixels();
    const auto redo_pixels_match = !redo_pixels.isNull() && redo_pixels == after_pixels;
    matrix_history_after_redo_view = matrix_preview_view_state();
    if (!redo_settled || !redo_recipe_matches || !redo_pixels_match) {
      const auto render_cpu = [&](const QString& recipe) {
        const auto state = photo_editor::desktop::parse_edit_recipe_json(recipe);
        return photo_editor::desktop::render_flattened_output_file(
            QStringLiteral("mask-layer-matrix-photo"), matrix_photo_path, state,
            QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64);
      };
      const auto before_cpu = render_cpu(before_recipe);
      const auto after_cpu = render_cpu(after_recipe);
      const auto current_matches_cpu_after = !redo_pixels.isNull() && !after_cpu.isNull() &&
          after_cpu.convertToFormat(redo_pixels.format()) == redo_pixels;
      const auto image_metadata = [](const QImage& image) {
        return QJsonObject{{QStringLiteral("width"), image.width()},
            {QStringLiteral("height"), image.height()},
            {QStringLiteral("format"), static_cast<int>(image.format())},
            {QStringLiteral("devicePixelRatio"), image.devicePixelRatio()},
            {QStringLiteral("hasAlpha"), image.hasAlphaChannel()},
            {QStringLiteral("colorSpaceValid"), image.colorSpace().isValid()},
            {QStringLiteral("colorSpace"), image.colorSpace().description()}};
      };
      std::cerr << "mask-history stage=redo dispatched=" << redo_dispatched
                << " settled=" << redo_settled << " recipeMatches=" << redo_recipe_matches
                << " providerPixelsMatch=" << redo_pixels_match
                << " providerPixelsMatchBefore=" << (redo_pixels == before_pixels)
                << " currentHistory=" << editor_preview.editHistoryCurrentIndex()
                << " currentRecipeBytes=" << matrix_recipe().size()
                << " expectedRecipeBytes=" << after_recipe.size()
                << " providerExpectedSize=" << after_pixels.width() << 'x' << after_pixels.height()
                << " providerActualSize=" << redo_pixels.width() << 'x' << redo_pixels.height()
                << " cpuImagesValid=" << !before_cpu.isNull() << ',' << !after_cpu.isNull()
                << " cpuChange=" << (before_cpu != after_cpu)
                << " providerMatchesCpuAfter=" << current_matches_cpu_after
                << " previewIdle=" << photo_editor::desktop::EditorPreviewControllerTestAccess::preview_idle(editor_preview)
                << " professionalBusy=" << editor_preview.professionalToolsBusy()
                << " processing=" << editor_preview.processing()
                << " fullRender=" << editor_preview.fullRenderRunning()
                << " nativeRevision=" << editor_preview.displayPreviewNativeRevision()
                << " previewSource=" << editor_preview.displayPreviewSource().toStdString()
                << " failureStage=" << editor_preview.previewFailureStage().toStdString()
                << " status=" << editor_preview.status().toStdString() << '\n';
      if (!before_pixels.isNull()) before_pixels.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-history-redo-expected-before.png")));
      if (!after_pixels.isNull()) after_pixels.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-history-redo-expected-after.png")));
      if (!redo_pixels.isNull()) redo_pixels.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-history-redo-provider-actual.png")));
      if (!before_cpu.isNull()) before_cpu.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-history-before-cpu.png")));
      if (!after_cpu.isNull()) after_cpu.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-history-after-cpu.png")));
      QJsonObject diagnostic{{QStringLiteral("action"), QStringLiteral("invert-mask")},
          {QStringLiteral("sequence"), QStringLiteral("nested mask thumbnail; More/Invert Mask; Ctrl+Z; Ctrl+Shift+Z")},
          {QStringLiteral("redoRecipeMatches"), redo_recipe_matches},
          {QStringLiteral("providerMatchesExpectedAfter"), redo_pixels_match},
          {QStringLiteral("providerMatchesExpectedBefore"), redo_pixels == before_pixels},
          {QStringLiteral("providerMatchesCpuAfter"), current_matches_cpu_after},
          {QStringLiteral("previewIdle"), photo_editor::desktop::EditorPreviewControllerTestAccess::preview_idle(editor_preview)},
          {QStringLiteral("professionalToolsBusy"), editor_preview.professionalToolsBusy()},
          {QStringLiteral("processing"), editor_preview.processing()},
          {QStringLiteral("fullRenderRunning"), editor_preview.fullRenderRunning()},
          {QStringLiteral("historyIndex"), editor_preview.editHistoryCurrentIndex()},
          {QStringLiteral("displayPreviewNativeRevision"), editor_preview.displayPreviewNativeRevision()},
          {QStringLiteral("displayPreviewSource"), editor_preview.displayPreviewSource()},
          {QStringLiteral("previewFailureStage"), editor_preview.previewFailureStage()},
          {QStringLiteral("status"), editor_preview.status()},
          {QStringLiteral("previewViewBeforeAction"), matrix_history_before_action_view},
          {QStringLiteral("previewViewAfterAction"), matrix_history_after_action_view},
          {QStringLiteral("previewViewAfterUndo"), matrix_history_after_undo_view},
          {QStringLiteral("previewViewAfterRedo"), matrix_history_after_redo_view},
          {QStringLiteral("images"), QJsonObject{
              {QStringLiteral("expectedBefore"), image_metadata(before_pixels)},
              {QStringLiteral("expectedAfter"), image_metadata(after_pixels)},
              {QStringLiteral("providerActualAfterRedo"), image_metadata(redo_pixels)},
              {QStringLiteral("cpuBefore"), image_metadata(before_cpu)},
              {QStringLiteral("cpuAfter"), image_metadata(after_cpu)}}}};
      QFile metadata_file(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-history-redo-diagnostic.json")));
      if (metadata_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        metadata_file.write(QJsonDocument(diagnostic).toJson(QJsonDocument::Indented));
      const auto failure_image = matrix_window->grabWindow();
      failure_image.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-history-redo-failure.png")));
      return false;
    }
    return true;
  };

  if (!settle_matrix() || !invoke_matrix_bool("setInterfaceDensity", QVariant(QStringLiteral("balanced"))) ||
      !QMetaObject::invokeMethod(matrix_edit, "activateEditTool",
          Q_ARG(QVariant, QVariant(QStringLiteral("layers")))) ||
      !wait_for_condition([&] { return matrix_edit->property("editTool").toString() == QStringLiteral("layers"); }, 5000))
    return fail("Mask and layer matrix should use the real balanced Layers workspace");
  const auto canvas_width = editor_preview.sourceImageWidth();
  const auto canvas_height = editor_preview.sourceImageHeight();
  if (canvas_width != matrix_photo.width() || canvas_height != matrix_photo.height() ||
      !editor_preview.addPhotoShape(QStringLiteral("rectangle"), 70, 60, 280, 260,
          canvas_width, canvas_height, QStringLiteral("#f04454"), QStringLiteral("#fff0dc"), 5.0) ||
      !editor_preview.addPhotoShape(QStringLiteral("ellipse"), 300, 190, 560, 440,
          canvas_width, canvas_height, QStringLiteral("#286ce0"), QStringLiteral("#e6f0ff"), 4.0) ||
      !editor_preview.addPhotoShape(QStringLiteral("rounded-rectangle"), 320, 45, 535, 180,
          canvas_width, canvas_height, QStringLiteral("#2eac80"), QStringLiteral("#e6fff5"), 4.0) ||
      !editor_preview.addParagraphTextLayer(QStringLiteral("Focus gate"), 30, 320, 360, 100,
          canvas_width, canvas_height) || !settle_matrix())
    return fail("Mask and layer matrix fixture should create three editable pixel layers and a text layer");
  auto authored = canonical_matrix_layers();
  if (authored.size() != 4)
    return fail("Mask and layer matrix fixture should expose four canonical authored layers");
  const auto text_id = authored.at(0).toMap().value(QStringLiteral("id")).toString();
  const auto green_id = authored.at(1).toMap().value(QStringLiteral("id")).toString();
  const auto blue_id = authored.at(2).toMap().value(QStringLiteral("id")).toString();
  const auto red_id = authored.at(3).toMap().value(QStringLiteral("id")).toString();
  if (text_id.isEmpty() || green_id.isEmpty() || blue_id.isEmpty() || red_id.isEmpty() ||
      matrix_layer_index(text_id) < 0 || matrix_layer_index(green_id) < 0 ||
      matrix_layer_index(blue_id) < 0 || matrix_layer_index(red_id) < 0)
    return fail("Mask and layer matrix fixture needs stable layer IDs before pointer work");
  trace_smoke_progress("mask-layer matrix: four-layer fixture ready");

  if (!capture_visual_proof(matrix_window, matrix_root, matrix_proof_dir,
          QStringLiteral("mask-layer-matrix-standard"), 1440, 900,
          {QStringLiteral("editImageViewport"), QStringLiteral("photoLayersInspectorGroup")},
          &matrix_captures))
    return fail("Mask and layer matrix should capture its standard Edit workspace state");

  auto* red_row = matrix_layer_row(red_id);
  auto* blue_row = matrix_layer_row(blue_id);
  auto* green_row = matrix_layer_row(green_id);
  auto* red_name = red_row ? item_descendant_named(red_row, QStringLiteral("photoLayerNameLabel")) : nullptr;
  auto* blue_name = blue_row ? item_descendant_named(blue_row, QStringLiteral("photoLayerNameLabel")) : nullptr;
  auto* green_name = green_row ? item_descendant_named(green_row, QStringLiteral("photoLayerNameLabel")) : nullptr;
  const auto state_before_row_selection = canonical_matrix_layers();
  const auto history_before_row_selection = editor_preview.editHistoryCurrentIndex();
  const auto pixels_before_row_selection = matrix_pixels();
  if (!click_matrix_item(red_name) || !settle_matrix() ||
      matrix_layer_index(red_id) < 0 ||
      editor_preview.editHistoryCurrentIndex() != history_before_row_selection ||
      canonical_matrix_layers() != state_before_row_selection || matrix_pixels() != pixels_before_row_selection)
    return fail("Selecting a layer by its name should preserve authored pixels and history");
  const auto red_index = matrix_layer_index(red_id);
  const auto active_target = matrix_edit->property("selectedEditTarget").toMap();
  if (active_target.value(QStringLiteral("scope")).toString() != QStringLiteral("photo-layer") ||
      active_target.value(QStringLiteral("layerId")).toString() != red_id ||
      matrix_edit->property("activePhotoLayerIndex").toInt() != red_index)
    return fail("A layer row click should select its stable pixel-layer target");

  red_row = matrix_layer_row(red_id);
  blue_row = matrix_layer_row(blue_id);
  green_row = matrix_layer_row(green_id);
  auto* red_pixels_target = red_row ? item_descendant_named(red_row, QStringLiteral("photoLayerPixelsTarget")) : nullptr;
  auto* blue_pixels_target = blue_row ? item_descendant_named(blue_row, QStringLiteral("photoLayerPixelsTarget")) : nullptr;
  auto* green_pixels_target = green_row ? item_descendant_named(green_row, QStringLiteral("photoLayerPixelsTarget")) : nullptr;
  if (!red_pixels_target || !blue_pixels_target || !green_pixels_target ||
      !click_matrix_item(blue_pixels_target) || !settle_matrix())
    return fail("Pixel thumbnails should be real targets in the Layers inspector");
  auto selected = matrix_edit->property("selectedEditTarget").toMap();
  if (selected.value(QStringLiteral("scope")).toString() != QStringLiteral("photo-layer") ||
      selected.value(QStringLiteral("layerId")).toString() != blue_id ||
      !selected.value(QStringLiteral("maskSetId")).toString().isEmpty())
    return fail("Clicking a pixel thumbnail should leave mask scope and select that layer by ID");

  // Pixel thumbnails open the layer editor. Return to the Layers inspector by
  // its visible tab before exercising row modifiers, as a user would.
  QPointer<QQuickItem> layers_inspector_tab(
      find_named_item(matrix_edit, QStringLiteral("layersInspectorTabButton")));
  const auto layers_inspector_tab_rect = visible_matrix_rect(layers_inspector_tab);
  QScopedPointer<QSignalSpy> layers_inspector_tab_clicks(
      layers_inspector_tab ? new QSignalSpy(layers_inspector_tab, SIGNAL(clicked())) : nullptr);
  const auto return_to_layers_dispatched = layers_inspector_tab && click_matrix_item(layers_inspector_tab);
  const auto return_to_layers_settled = return_to_layers_dispatched &&
      wait_for_condition([&] {
        process_layout_events(2);
        return matrix_edit->property("editTool").toString() == QStringLiteral("layers") &&
            matrix_edit->property("editInspectorTab").toString() == QStringLiteral("layers") &&
            !matrix_edit->property("editToolTransitionActive").toBool();
      }, 5000);
  if (!return_to_layers_settled || !settle_matrix()) {
    const auto failure_capture = QDir(matrix_proof_dir).filePath(QStringLiteral("mask-layer-matrix-return-to-layers-failure.png"));
    const auto failure_image = matrix_window->grabWindow();
    std::cerr << "return-to-layers tabAlive=" << !layers_inspector_tab.isNull()
              << " dispatched=" << return_to_layers_dispatched
              << " signalValid=" << (layers_inspector_tab_clicks && layers_inspector_tab_clicks->isValid())
              << " clickSignalCount=" << (layers_inspector_tab_clicks ? layers_inspector_tab_clicks->count() : -1)
              << " rect=" << layers_inspector_tab_rect.x() << ',' << layers_inspector_tab_rect.y() << ','
              << layers_inspector_tab_rect.width() << ',' << layers_inspector_tab_rect.height()
              << " enabled=" << (layers_inspector_tab && layers_inspector_tab->isEnabled())
              << " editTool=" << matrix_edit->property("editTool").toString().toStdString()
              << " inspectorTab=" << matrix_edit->property("editInspectorTab").toString().toStdString()
              << " toolTransition=" << matrix_edit->property("editToolTransitionActive").toBool()
              << " pendingTool=" << matrix_edit->property("pendingEditToolTransitionName").toString().toStdString()
              << " selectedLayer=" << matrix_edit->property("selectedEditTarget").toMap()
                     .value(QStringLiteral("layerId")).toString().toStdString()
              << " capture=" << (!failure_image.isNull() && failure_image.save(failure_capture))
              << " path=" << failure_capture.toStdString() << '\n';
    return fail("Matrix should return from the pixel layer editor to the Layers inspector by pointer");
  }

  const auto before_multi_select = canonical_matrix_layers();
  const auto history_before_multi_select = editor_preview.editHistoryCurrentIndex();
  red_row = matrix_layer_row(red_id);
  red_name = red_row ? item_descendant_named(red_row, QStringLiteral("photoLayerNameLabel")) : nullptr;
  QPointer<QQuickItem> red_name_before_multi_select(red_name);
  const auto red_name_rect_before_multi_select = visible_matrix_rect(red_name_before_multi_select);
  const auto control_click_dispatched = click_matrix_item(red_name_before_multi_select, Qt::ControlModifier);
  if (!control_click_dispatched || !settle_matrix()) {
    auto* current_red_row = matrix_layer_row(red_id);
    auto* current_red_name = current_red_row
        ? item_descendant_named(current_red_row, QStringLiteral("photoLayerNameLabel")) : nullptr;
    const auto current_red_rect = visible_matrix_rect(current_red_name);
    const auto selected_target = matrix_edit->property("selectedEditTarget").toMap();
    const auto selected_rows = matrix_edit->property("selectedPhotoLayerIndices").toList();
    auto describe_item = [&](const char* label, QQuickItem* item, const QRectF& rect) {
      std::cerr << label << " alive=" << (item != nullptr);
      if (item) {
        const auto local = item->boundingRect();
        std::cerr << " object=" << item->objectName().toStdString()
                  << " enabled=" << item->isEnabled() << " visible=" << item_visible(item)
                  << " sameWindow=" << (item->window() == matrix_window)
                  << " local=" << local.x() << ',' << local.y() << ',' << local.width() << ',' << local.height();
      }
      std::cerr << " clipped=" << rect.x() << ',' << rect.y() << ',' << rect.width() << ',' << rect.height() << '\n';
    };
    std::cerr << "control-click dispatched=" << control_click_dispatched
              << " oldNameAlive=" << !red_name_before_multi_select.isNull()
              << " stableRedId=" << red_id.toStdString()
              << " redIndex=" << matrix_layer_index(red_id)
              << " blueIndex=" << matrix_layer_index(blue_id)
              << " selectedTarget=" << selected_target.value(QStringLiteral("scope")).toString().toStdString()
              << ':' << selected_target.value(QStringLiteral("layerId")).toString().toStdString()
              << " selectedRows=";
    for (const auto& selected_row : selected_rows) std::cerr << selected_row.toInt() << ',';
    std::cerr << '\n';
    describe_item("oldRedName", red_name_before_multi_select, red_name_rect_before_multi_select);
    describe_item("currentRedName", current_red_name, current_red_rect);
    if (current_red_row) {
      const auto row_rect = visible_matrix_rect(current_red_row);
      std::cerr << "currentRedRow id=" << current_red_row->property("layerId").toString().toStdString()
                << " index=" << current_red_row->property("layerIndex").toInt()
                << " rect=" << row_rect.x() << ',' << row_rect.y() << ','
                << row_rect.width() << ',' << row_rect.height() << '\n';
    }
    const auto failure_capture = QDir(matrix_proof_dir).filePath(QStringLiteral("mask-layer-matrix-control-click-failure.png"));
    const auto failure_image = matrix_window->grabWindow();
    std::cerr << "control-click failure capture=" << (!failure_image.isNull() && failure_image.save(failure_capture))
              << " path=" << failure_capture.toStdString() << '\n';
    return fail("Control-click should reach a second row through the production stack");
  }
  auto selected_indices = matrix_edit->property("selectedPhotoLayerIndices").toList();
  if (selected_indices.size() != 2 || !selected_indices.contains(matrix_layer_index(blue_id)) ||
      !selected_indices.contains(matrix_layer_index(red_id)) ||
      canonical_matrix_layers() != before_multi_select ||
      editor_preview.editHistoryCurrentIndex() != history_before_multi_select)
    return fail("Control-click should select two stable layer IDs without changing the recipe or history");
  green_row = matrix_layer_row(green_id);
  green_name = green_row ? item_descendant_named(green_row, QStringLiteral("photoLayerNameLabel")) : nullptr;
  if (!click_matrix_item(green_name, Qt::ShiftModifier) || !settle_matrix())
    return fail("Shift-click should reach a range endpoint through the production layer row");
  selected_indices = matrix_edit->property("selectedPhotoLayerIndices").toList();
  if (selected_indices.size() < 2 || !selected_indices.contains(matrix_layer_index(green_id)) ||
      canonical_matrix_layers() != before_multi_select ||
      editor_preview.editHistoryCurrentIndex() != history_before_multi_select)
    return fail("Shift-click should extend the row range without editing authored layers or masks");
  trace_smoke_progress("mask-layer matrix: row targeting and modifier selection passed");
  QVariant active_layer_result;
  if (!QMetaObject::invokeMethod(matrix_edit, "setActivePhotoLayerSelection",
        Q_RETURN_ARG(QVariant, active_layer_result),
        Q_ARG(QVariant, QVariant(matrix_layer_index(red_id))), Q_ARG(QVariant, QVariant(false))) ||
      !active_layer_result.toBool() ||
      !QMetaObject::invokeMethod(matrix_edit, "activateEditTool",
        Q_ARG(QVariant, QVariant(QStringLiteral("layers")))) || !settle_matrix())
    return fail("Matrix should return to Layers before mask authoring");

  if (!QMetaObject::invokeMethod(matrix_edit, "createMaskSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("radial"))) ) ||
      !wait_for_condition([&] { return editor_preview.hasActiveMask() && editor_preview.selectionEditSessionActive(); }, 10000))
    return fail("Mask fixture should enter the real selection workflow before attaching an editable mask");
  auto* creation_done = find_named_item(matrix_edit, QStringLiteral("maskCreationDoneButton"));
  if (!click_matrix_item(creation_done) ||
      !wait_for_condition([&] { return !editor_preview.selectionEditSessionActive() && editor_preview.hasWorkingSelection(); }, 10000) ||
      !editor_preview.saveWorkingSelection(QStringLiteral("Matrix selection")) || !settle_matrix())
    return fail("The mask fixture should commit and save one editable selection set");
  if (!QMetaObject::invokeMethod(matrix_edit, "activateEditTool",
        Q_ARG(QVariant, QVariant(QStringLiteral("layers")))) || !settle_matrix())
    return fail("The saved selection fixture should return to the Layers inspector");
  auto* matrix_layers_tab = find_named_item(matrix_edit, QStringLiteral("layersInspectorTabButton"));
  if (matrix_layers_tab && !click_matrix_item(matrix_layers_tab))
    return fail("The saved selection fixture should expose Layers through its actual inspector tab");
  if (!wait_for_condition([&] { return matrix_edit->property("editInspectorTab").toString() == QStringLiteral("layers"); }, 5000))
    return fail("The mask matrix should show the Layers inspector before attaching the saved set");
  QString saved_set_id;
  for (const auto& value : editor_preview.maskStack()) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("maskSetName")).toString() == QStringLiteral("Matrix selection")) {
      saved_set_id = mask.value(QStringLiteral("maskSetId")).toString();
      if (saved_set_id.isEmpty()) saved_set_id = mask.value(QStringLiteral("id")).toString();
    }
  }
  if (saved_set_id.isEmpty())
    return fail("Saved selection should retain a stable mask-set identity");

  red_row = matrix_layer_row(red_id);
  red_name = red_row ? item_descendant_named(red_row, QStringLiteral("photoLayerNameLabel")) : nullptr;
  if (!click_matrix_item(red_name) || !settle_matrix())
    return fail("Mask attach should begin from the selected red pixel layer");
  auto* saved_more = mask_menu_button(QString(), saved_set_id, false);
  if (!saved_more || !trigger_matrix_menu(saved_more, QStringLiteral("apply-selection")) || !settle_matrix())
    return fail("The standalone selection More menu should attach a duplicate to the selected layer");
  const auto red_masks_after_attach = matrix_layer(red_id).value(QStringLiteral("selectionMaskSetIds")).toList();
  QString red_mask_id;
  for (const auto& value : red_masks_after_attach) {
    const auto candidate = value.toString();
    if (!candidate.isEmpty() && candidate != saved_set_id) red_mask_id = candidate;
  }
  if (red_mask_id.isEmpty() || !matrix_mask_linked(red_id, red_mask_id) ||
      !matrix_mask_present(saved_set_id))
    return fail("Attaching a saved selection should preserve its standalone source and add a linked mask ID");

  auto* red_mask_thumb = nested_mask_thumbnail(red_id, red_mask_id);
  if (!red_mask_thumb || !click_matrix_item(red_mask_thumb) || !settle_matrix())
    return fail("A nested mask thumbnail should receive a real selection-target click");
  selected = matrix_edit->property("selectedEditTarget").toMap();
  if (selected.value(QStringLiteral("scope")).toString() != QStringLiteral("mask") ||
      selected.value(QStringLiteral("layerId")).toString() != red_id ||
      selected.value(QStringLiteral("maskSetId")).toString() != red_mask_id)
    return fail("A mask thumbnail should select its exact owner and mask-set IDs");
  QPointer<QQuickItem> mask_layers_tab(
      find_named_item(matrix_edit, QStringLiteral("layersInspectorTabButton")));
  QScopedPointer<QSignalSpy> mask_layers_tab_clicks(
      mask_layers_tab ? new QSignalSpy(mask_layers_tab, SIGNAL(clicked())) : nullptr);
  const auto mask_return_dispatched = mask_layers_tab && click_matrix_item(mask_layers_tab);
  const auto mask_return_settled = mask_return_dispatched &&
      wait_for_condition([&] {
        process_layout_events(2);
        return matrix_edit->property("editTool").toString() == QStringLiteral("layers") &&
            matrix_edit->property("editInspectorTab").toString() == QStringLiteral("layers") &&
            !matrix_edit->property("editToolTransitionActive").toBool();
      }, 5000);
  if (!mask_return_settled || !settle_matrix()) {
    const auto failure_capture = QDir(matrix_proof_dir).filePath(QStringLiteral("mask-layer-matrix-mask-return-to-layers-failure.png"));
    const auto failure_image = matrix_window->grabWindow();
    std::cerr << "mask-return-to-layers tabAlive=" << !mask_layers_tab.isNull()
              << " dispatched=" << mask_return_dispatched
              << " signalValid=" << (mask_layers_tab_clicks && mask_layers_tab_clicks->isValid())
              << " clickSignalCount=" << (mask_layers_tab_clicks ? mask_layers_tab_clicks->count() : -1)
              << " editTool=" << matrix_edit->property("editTool").toString().toStdString()
              << " inspectorTab=" << matrix_edit->property("editInspectorTab").toString().toStdString()
              << " toolTransition=" << matrix_edit->property("editToolTransitionActive").toBool()
              << " pendingTool=" << matrix_edit->property("pendingEditToolTransitionName").toString().toStdString()
              << " capture=" << (!failure_image.isNull() && failure_image.save(failure_capture))
              << " path=" << failure_capture.toStdString() << '\n';
    return fail("Matrix should return from a selected mask to the Layers inspector by pointer");
  }
  red_row = matrix_layer_row(red_id);
  red_pixels_target = red_row ? item_descendant_named(red_row, QStringLiteral("photoLayerPixelsTarget")) : nullptr;
  if (!red_pixels_target || !click_matrix_item(red_pixels_target) || !settle_matrix())
    return fail("The owning pixel thumbnail should switch from the mask target back to its pixels");
  selected = matrix_edit->property("selectedEditTarget").toMap();
  if (selected.value(QStringLiteral("scope")).toString() != QStringLiteral("photo-layer") ||
      selected.value(QStringLiteral("layerId")).toString() != red_id ||
      !selected.value(QStringLiteral("maskSetId")).toString().isEmpty())
    return fail("Pixel target switching should clear the previous mask identity");

  QPointer<QQuickItem> compact_layers_tab(
      find_named_item(matrix_edit, QStringLiteral("layersInspectorTabButton")));
  QScopedPointer<QSignalSpy> compact_layers_tab_clicks(
      compact_layers_tab ? new QSignalSpy(compact_layers_tab, SIGNAL(clicked())) : nullptr);
  const auto compact_return_dispatched = compact_layers_tab && click_matrix_item(compact_layers_tab);
  const auto compact_return_settled = compact_return_dispatched &&
      wait_for_condition([&] {
        process_layout_events(2);
        return matrix_edit->property("editTool").toString() == QStringLiteral("layers") &&
            matrix_edit->property("editInspectorTab").toString() == QStringLiteral("layers") &&
            !matrix_edit->property("editToolTransitionActive").toBool();
      }, 5000);
  if (!compact_return_settled || !settle_matrix()) {
    const auto failure_capture = QDir(matrix_proof_dir).filePath(QStringLiteral("mask-layer-matrix-compact-return-to-layers-failure.png"));
    const auto failure_image = matrix_window->grabWindow();
    std::cerr << "compact-return-to-layers tabAlive=" << !compact_layers_tab.isNull()
              << " dispatched=" << compact_return_dispatched
              << " signalValid=" << (compact_layers_tab_clicks && compact_layers_tab_clicks->isValid())
              << " clickSignalCount=" << (compact_layers_tab_clicks ? compact_layers_tab_clicks->count() : -1)
              << " editTool=" << matrix_edit->property("editTool").toString().toStdString()
              << " inspectorTab=" << matrix_edit->property("editInspectorTab").toString().toStdString()
              << " capture=" << (!failure_image.isNull() && failure_image.save(failure_capture))
              << " path=" << failure_capture.toStdString() << '\n';
    return fail("Compact Layers evidence should return from the pixel editor by pointer");
  }

  if (!capture_visual_proof(matrix_window, matrix_root, matrix_proof_dir,
          QStringLiteral("mask-layer-matrix-compact"), 1024, 700,
          {QStringLiteral("editImageViewport"), QStringLiteral("photoLayersInspectorGroup"),
           QStringLiteral("photoLayerNestedMaskRow")}, &matrix_captures))
    return fail("Mask and layer matrix should capture its compact attached-mask state");
  matrix_root->setProperty("width", 1440);
  matrix_root->setProperty("height", 900);
  if (!settle_matrix()) return fail("Matrix should restore its standard layout after the compact capture");

  const auto run_mask_menu_undo_redo = [&](const QString& action_key,
                                           const std::function<bool(const QVariantMap&)>& expected_after) {
    const auto before_recipe = matrix_recipe();
    if (before_recipe.isEmpty()) return false;
    const auto expected_before_pixels = matrix_cpu_render(before_recipe);
    if (expected_before_pixels.isNull() || !wait_for_condition([&] {
          return settle_matrix() && matrix_recipe() == before_recipe &&
              matrix_pixels_match_cpu(matrix_pixels(), expected_before_pixels);
        }, 10000)) {
      std::cerr << "mask-menu action=" << action_key.toStdString()
                << " before preview did not match its canonical recipe; maxChannelDelta="
                << matrix_max_channel_delta(matrix_pixels(), expected_before_pixels) << '\n';
      return false;
    }
    const auto before_pixels = matrix_pixels();
    matrix_history_before_action_view = matrix_preview_view_state();
    const auto before_history = editor_preview.editHistoryCurrentIndex();
    QPointer<QQuickItem> button(mask_menu_button(red_id, red_mask_id, true));
    const auto button_identity = reinterpret_cast<quintptr>(button.data());
    auto* nested_row = matrix_layer_row(red_id);
    auto* nested_list = nested_row
        ? item_descendant_named(nested_row, QStringLiteral("photoLayerNestedMaskList")) : nullptr;
    const auto linked_count_before = nested_list
        ? nested_list->property("linkedMaskSets").toList().size() : -1;
    const auto menu_triggered = button && trigger_matrix_menu(button.data(), action_key);
    const auto button_identity_preserved = button &&
        reinterpret_cast<quintptr>(button.data()) == button_identity;
    const auto after_recipe = matrix_recipe();
    nested_row = matrix_layer_row(red_id);
    nested_list = nested_row
        ? item_descendant_named(nested_row, QStringLiteral("photoLayerNestedMaskList")) : nullptr;
    const auto linked_count_after = nested_list
        ? nested_list->property("linkedMaskSets").toList().size() : -1;
    const auto expected_after_pixels = matrix_cpu_render(after_recipe);
    const auto preview_settled = menu_triggered && !expected_after_pixels.isNull() &&
        wait_for_condition([&] {
          return settle_matrix() && matrix_recipe() == after_recipe &&
              matrix_pixels_match_cpu(matrix_pixels(), expected_after_pixels);
        }, 10000);
    const auto after_pixels = matrix_pixels();
    matrix_history_after_action_view = matrix_preview_view_state();
    const auto recipe_changed = after_recipe != before_recipe;
    const auto pixels_changed = !after_pixels.isNull() && after_pixels != before_pixels;
    const auto history_advanced = editor_preview.editHistoryCurrentIndex() > before_history;
    const auto expected_state = expected_after(matrix_layer(red_id));
    if (!menu_triggered || !button_identity_preserved ||
        linked_count_before != linked_count_after || !preview_settled || !recipe_changed || !pixels_changed ||
        !history_advanced || !expected_state) {
      std::cerr << "mask-menu action=" << action_key.toStdString()
                << " button=" << !button.isNull()
                << " openerIdentityPreserved=" << button_identity_preserved
                << " linkedCount=" << linked_count_before << "->" << linked_count_after
                << " triggered=" << menu_triggered << " settled=" << preview_settled
                << " recipeChanged=" << recipe_changed
                << " beforeRecipeBytes=" << before_recipe.size()
                << " afterRecipeBytes=" << after_recipe.size()
                << " pixelsBeforeNull=" << before_pixels.isNull()
                << " pixelsAfterNull=" << after_pixels.isNull()
                << " pixelsChanged=" << pixels_changed
                << " canonicalAfterReady=" << preview_settled
                << " canonicalAfterDelta=" << matrix_max_channel_delta(after_pixels, expected_after_pixels)
                << " history=" << before_history << "->" << editor_preview.editHistoryCurrentIndex()
                << " expectedState=" << expected_state
                << " maskLinked=" << matrix_mask_linked(red_id, red_mask_id)
                << " layerMaskEnabled=" << matrix_layer(red_id).value(QStringLiteral("maskEnabled")).toBool()
                << " selectedScope=" << matrix_edit->property("selectedEditTarget").toMap()
                       .value(QStringLiteral("scope")).toString().toStdString() << '\n';
      std::cerr << "mask-menu previewSource=" << editor_preview.displayPreviewSource().toStdString()
                << " nativeRevision=" << editor_preview.displayPreviewNativeRevision()
                << " previewIdle=" << photo_editor::desktop::EditorPreviewControllerTestAccess::preview_idle(editor_preview)
                << " professionalBusy=" << editor_preview.professionalToolsBusy()
                << " maskMode=" << editor_preview.maskPreviewMode().toStdString()
                << " maskDisplay=" << editor_preview.maskPreviewDisplayMode().toStdString()
                << " overlaySource=" << editor_preview.displayPreviewOverlaySource().toStdString()
                << " beforeView=" << QJsonDocument(matrix_history_before_action_view).toJson(QJsonDocument::Compact).toStdString()
                << " afterView=" << QJsonDocument(matrix_history_after_action_view).toJson(QJsonDocument::Compact).toStdString()
                << '\n';
      for (const auto& value : editor_preview.maskStack()) {
        const auto mask = value.toMap();
        if (mask.value(QStringLiteral("maskSetId")).toString() == red_mask_id ||
            mask.value(QStringLiteral("id")).toString() == red_mask_id)
          std::cerr << "target mask id=" << mask.value(QStringLiteral("id")).toString().toStdString()
                    << " set=" << mask.value(QStringLiteral("maskSetId")).toString().toStdString()
                    << " invert=" << mask.value(QStringLiteral("maskSetSelectionInvert")).toBool()
                    << " sourceInvert=" << mask.value(QStringLiteral("invert")).toBool()
                    << " locked=" << mask.value(QStringLiteral("locked")).toBool() << '\n';
      }
      if (!before_pixels.isNull())
        before_pixels.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-menu-before.png")));
      if (!after_pixels.isNull())
        after_pixels.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-menu-after.png")));
      if (!expected_before_pixels.isNull())
        expected_before_pixels.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-menu-before-cpu.png")));
      if (!expected_after_pixels.isNull())
        expected_after_pixels.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-menu-after-cpu.png")));
      const auto failure_capture = matrix_window->grabWindow();
      failure_capture.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-menu-failure.png")));
      return false;
    }
    const auto history_roundtrip = undo_redo_matrix_recipe(before_recipe, before_pixels, after_recipe, after_pixels);
    if (!history_roundtrip)
      std::cerr << "mask-menu action=" << action_key.toStdString()
                << " failed exact Undo/Redo roundtrip\n";
    return history_roundtrip;
  };
  if (!run_mask_menu_undo_redo(QStringLiteral("invert-mask"), [&](const QVariantMap&) {
        bool found_inverted = false;
        for (const auto& value : editor_preview.maskStack()) {
          const auto mask = value.toMap();
          if (mask.value(QStringLiteral("maskSetId")).toString() == red_mask_id &&
              mask.value(QStringLiteral("maskSetSelectionInvert")).toBool()) found_inverted = true;
        }
        return found_inverted;
      }))
    return fail("Invert Mask through its menu should change rendered pixels and Undo/Redo exactly");
  if (!run_mask_menu_undo_redo(QStringLiteral("toggle-mask"), [&](const QVariantMap& layer) {
        return layer.value(QStringLiteral("maskEnabled")).isValid() &&
            !layer.value(QStringLiteral("maskEnabled")).toBool();
      }))
    return fail("Disable Mask through its menu should change rendered pixels and Undo/Redo exactly");
  trace_smoke_progress("mask-layer matrix: mask invert and disable menu Undo/Redo passed");

  const auto before_remove_recipe = matrix_recipe();
  const auto before_remove_layers = canonical_matrix_layers();
  const auto before_remove_pixels = matrix_pixels();
  const auto mask_stack_before_remove = editor_preview.maskStack();
  const auto remove_history_before = editor_preview.editHistoryCurrentIndex();
  if (!trigger_matrix_menu(mask_menu_button(red_id, red_mask_id, true), QStringLiteral("remove-mask")) ||
      !settle_matrix() || matrix_mask_linked(red_id, red_mask_id) ||
      editor_preview.maskStack() != mask_stack_before_remove ||
      editor_preview.editHistoryCurrentIndex() <= remove_history_before)
    return fail("Remove from Layer should unlink the stable mask ID while retaining its saved source set");
  const auto after_remove_pixels = matrix_pixels();
  if (!undo_redo_matrix_recipe(before_remove_recipe, before_remove_pixels,
                               matrix_recipe(), after_remove_pixels) ||
      matrix_mask_linked(red_id, red_mask_id) || editor_preview.maskStack() != mask_stack_before_remove)
    return fail("Remove from Layer should Undo and Redo the exact owner link and pixels");
  auto* matrix_edit_item = qobject_cast<QQuickItem*>(matrix_edit);
  if (!matrix_edit_item) return fail("Mask remove Undo should retain the workspace key focus owner");
  matrix_edit_item->forceActiveFocus();
  if (!send_window_key(matrix_window, Qt::Key_Z, Qt::ControlModifier) ||
      !wait_for_condition([&] { return settle_matrix() && canonical_matrix_layers() == before_remove_layers; }, 10000) ||
      !matrix_mask_linked(red_id, red_mask_id) || matrix_pixels() != before_remove_pixels)
    return fail("Mask move fixture should restore the original owner after verifying Remove Undo/Redo");

  red_row = matrix_layer_row(red_id);
  red_name = red_row ? item_descendant_named(red_row, QStringLiteral("photoLayerNameLabel")) : nullptr;
  if (!click_matrix_item(red_name) || !settle_matrix())
    return fail("Mask transfer should start from its source layer row");
  QPointer<QQuickItem> source_mask_thumb(nested_mask_thumbnail(red_id, red_mask_id));
  QPointer<QQuickItem> blue_drop(matrix_layer_row(blue_id));
  const auto source_mask_drag_dispatched = source_mask_thumb && blue_drop &&
      drag_mask(source_mask_thumb.data(), blue_drop.data(), Qt::NoModifier);
  const auto source_mask_moved = matrix_mask_linked(blue_id, red_mask_id) &&
      !matrix_mask_linked(red_id, red_mask_id);
  if (!source_mask_drag_dispatched || !source_mask_moved) {
    const auto failure_capture = QDir(matrix_proof_dir).filePath(QStringLiteral("mask-layer-matrix-transfer-failure.png"));
    const auto failure_image = matrix_window->grabWindow();
    std::cerr << "mask-transfer stage=" << drag_mask_stage.toStdString()
              << " sourceFound=" << !source_mask_thumb.isNull()
              << " destinationFound=" << !blue_drop.isNull()
              << " sourceDragStarted=" << (!source_mask_thumb.isNull() && source_mask_thumb->property("dragging").toBool())
              << " sourceMoved=" << source_mask_moved
              << " sourceStillLinked=" << matrix_mask_linked(red_id, red_mask_id)
              << " destinationLinked=" << matrix_mask_linked(blue_id, red_mask_id)
              << " selectedTarget=" << QJsonDocument(matrix_edit->property("selectedEditTarget").toMap().isEmpty()
                      ? QJsonObject{} : QJsonObject::fromVariantMap(matrix_edit->property("selectedEditTarget").toMap()))
                     .toJson(QJsonDocument::Compact).toStdString()
              << " capture=" << (!failure_image.isNull() && failure_image.save(failure_capture))
              << " path=" << failure_capture.toStdString() << '\n';
    return fail("A pointer drag should move the same linked mask ID from one unlocked layer to another");
  }
  if (!expand_nested_mask_section(blue_id))
    return fail("A moved mask should remain reachable after expanding its destination layer's actual Masks section");
  QPointer<QQuickItem> blue_mask_thumb(nested_mask_thumbnail(blue_id, red_mask_id));
  QPointer<QQuickItem> green_drop(matrix_layer_row(green_id));
  const auto alt_drag_dispatched = blue_mask_thumb && green_drop &&
      drag_mask(blue_mask_thumb.data(), green_drop.data(), Qt::AltModifier);
  if (!alt_drag_dispatched) {
    const auto failure_capture = QDir(matrix_proof_dir).filePath(QStringLiteral("mask-layer-matrix-alt-copy-failure.png"));
    const auto failure_image = matrix_window->grabWindow();
    std::cerr << "mask-alt-copy stage=" << drag_mask_stage.toStdString()
              << " sourceFound=" << !blue_mask_thumb.isNull()
              << " destinationFound=" << !green_drop.isNull()
              << " sourceDragging=" << (!blue_mask_thumb.isNull() && blue_mask_thumb->property("dragging").toBool())
              << " capture=" << (!failure_image.isNull() && failure_image.save(failure_capture))
              << " path=" << failure_capture.toStdString() << '\n';
    return fail("Alt-drag should begin at the linked mask thumbnail and reach its destination row");
  }
  QString green_copy_id;
  for (const auto& value : matrix_layer(green_id).value(QStringLiteral("selectionMaskSetIds")).toList()) {
    const auto candidate = value.toString();
    if (!candidate.isEmpty() && candidate != red_mask_id) green_copy_id = candidate;
  }
  const auto original_stayed_linked = matrix_mask_linked(blue_id, red_mask_id);
  const auto copied_to_new_id = !green_copy_id.isEmpty() && green_copy_id != red_mask_id &&
      matrix_mask_linked(green_id, green_copy_id);
  if (!original_stayed_linked || !copied_to_new_id) {
    const auto failure_capture = QDir(matrix_proof_dir).filePath(QStringLiteral("mask-layer-matrix-alt-copy-identity-failure.png"));
    const auto failure_image = matrix_window->grabWindow();
    std::cerr << "mask-alt-copy stage=verify sourceLinked=" << original_stayed_linked
              << " copiedId=" << green_copy_id.toStdString()
              << " copiedToNewId=" << copied_to_new_id
              << " greenIds=";
    for (const auto& value : matrix_layer(green_id).value(QStringLiteral("selectionMaskSetIds")).toList())
      std::cerr << value.toString().toStdString() << ',';
    std::cerr << " capture=" << (!failure_image.isNull() && failure_image.save(failure_capture))
              << " path=" << failure_capture.toStdString() << '\n';
    return fail("Alt-drag should copy to a new stable mask ID and leave its original owner linked");
  }

  green_row = matrix_layer_row(green_id);
  green_name = green_row ? item_descendant_named(green_row, QStringLiteral("photoLayerNameLabel")) : nullptr;
  if (!green_name || !click_matrix_item(green_name) || !settle_matrix())
    return fail("Mask transfer rejection should first select its destination layer by pointer");
  auto* layer_actions_button = find_named_item(matrix_edit, QStringLiteral("photoLayerPropertiesLayerActionsMenuButton"));
  if (!layer_actions_button || !click_matrix_item(layer_actions_button))
    return fail("Mask transfer rejection should open the selected layer's production actions menu");
  auto* toggle_lock_action = find_named_item(matrix_edit, QStringLiteral("layerAction_toggle-lock"));
  if (!toggle_lock_action || !click_matrix_item(toggle_lock_action) || !settle_matrix() ||
      !matrix_layer(green_id).value(QStringLiteral("locked")).toBool())
    return fail("Mask transfer rejection should lock the destination through its visible layer actions menu");
  QPointer<QQuickItem> locked_mask_menu(mask_menu_button(green_id, green_copy_id, true));
  if (!locked_mask_menu) return fail("A locked layer should retain a discoverable linked mask menu");
  if (!click_matrix_menu_opener(locked_mask_menu))
    return fail("The locked layer's mask menu should remain openable for inspection");
  if (!locked_mask_menu) return fail("The locked mask menu should survive opening its popup");
  const auto locked_popup_name = locked_mask_menu->objectName() + QStringLiteral("Popup");
  QPointer<QObject> locked_popup(find_named_object(locked_mask_menu, locked_popup_name));
  if (!locked_popup || !wait_for_condition([&] { return locked_popup && locked_popup->property("opened").toBool(); }, 3000))
    return fail("The locked layer's nested mask menu should open from its real More control");
  const auto locked_menu_items = locked_popup->property("menuItems").toList();
  bool locked_edit_enabled = false;
  int locked_edit_index = -1;
  bool locked_invert_disabled = false;
  bool locked_toggle_disabled = false;
  bool locked_remove_disabled = false;
  for (int index = 0; index < locked_menu_items.size(); ++index) {
    const auto item = locked_menu_items.at(index).toMap();
    const auto key = item.value(QStringLiteral("key")).toString();
    if (key == QStringLiteral("edit-mask")) {
      locked_edit_index = index;
      locked_edit_enabled = !item.contains(QStringLiteral("enabled")) ||
          item.value(QStringLiteral("enabled")).toBool();
    }
    if (key == QStringLiteral("invert-mask")) locked_invert_disabled = item.value(QStringLiteral("enabled")).toBool() == false;
    if (key == QStringLiteral("toggle-mask")) locked_toggle_disabled = item.value(QStringLiteral("enabled")).toBool() == false;
    if (key == QStringLiteral("remove-mask")) locked_remove_disabled = item.value(QStringLiteral("enabled")).toBool() == false;
  }
  QPointer<QQuickItem> locked_edit_menu_item;
  if (locked_mask_menu && locked_edit_index >= 0)
    locked_edit_menu_item = find_named_item(locked_popup,
        locked_mask_menu->objectName() + QStringLiteral("PopupItem%1").arg(locked_edit_index));
  const auto locked_edit_item_interactive = locked_edit_menu_item &&
      locked_edit_menu_item->isEnabled() && !visible_matrix_rect(locked_edit_menu_item).isEmpty();
  const auto locked_edit_activated = locked_edit_menu_item &&
      click_matrix_item(locked_edit_menu_item.data());
  const auto locked_menu_dismissed = locked_edit_activated && wait_for_condition([&] {
    return !locked_popup || !locked_popup->property("visible").toBool();
  }, 3000);
  const auto locked_inspection_target = matrix_edit->property("selectedEditTarget").toMap();
  const auto locked_inspection_selected = locked_inspection_target.value(QStringLiteral("scope")).toString() == QStringLiteral("mask") &&
      locked_inspection_target.value(QStringLiteral("layerId")).toString() == green_id &&
      locked_inspection_target.value(QStringLiteral("maskSetId")).toString() == green_copy_id;
  if (!locked_edit_enabled || !locked_edit_item_interactive || !locked_invert_disabled ||
      !locked_toggle_disabled || !locked_remove_disabled ||
      !locked_menu_dismissed || !locked_inspection_selected) {
    const auto failure_capture = QDir(matrix_proof_dir).filePath(QStringLiteral("mask-layer-matrix-locked-mask-menu-failure.png"));
    const auto failure_image = matrix_window->grabWindow();
    std::cerr << "locked-mask-menu editEnabled=" << locked_edit_enabled
              << " editItemInteractive=" << locked_edit_item_interactive
              << " invertDisabled=" << locked_invert_disabled
              << " toggleDisabled=" << locked_toggle_disabled
              << " removeDisabled=" << locked_remove_disabled
              << " editMaskActivated=" << locked_edit_activated
              << " editMaskClosedPopup=" << locked_menu_dismissed
              << " inspectionTargetSelected=" << locked_inspection_selected
              << " menuItems=" << QJsonDocument::fromVariant(locked_menu_items).toJson(QJsonDocument::Compact).toStdString()
              << " layerLocked=" << matrix_layer(green_id).value(QStringLiteral("locked")).toBool()
              << " sourceLocked=";
    for (const auto& value : editor_preview.maskStack()) {
      const auto mask = value.toMap();
      if (mask.value(QStringLiteral("maskSetId")).toString() == green_copy_id ||
          mask.value(QStringLiteral("id")).toString() == green_copy_id)
        std::cerr << mask.value(QStringLiteral("locked")).toBool();
    }
    std::cerr << " capture=" << (!failure_image.isNull() && failure_image.save(failure_capture))
              << " path=" << failure_capture.toStdString() << '\n';
    return fail("Locked masks should remain inspectable while destructive mask actions are disabled");
  }
  QPointer<QQuickItem> locked_return_layers_tab(
      find_named_item(matrix_edit, QStringLiteral("layersInspectorTabButton")));
  if (!locked_return_layers_tab || !click_matrix_item(locked_return_layers_tab.data()) ||
      !wait_for_condition([&] {
        process_layout_events(2);
        return matrix_edit->property("editInspectorTab").toString() == QStringLiteral("layers") &&
            !matrix_edit->property("editToolTransitionActive").toBool();
      }, 5000) || !settle_matrix())
    return fail("Locked-mask inspection should return to Layers through its actual inspector tab before drag rejection");
  process_layout_events(3);
  const auto green_layers_locked = canonical_matrix_layers();
  const auto locked_target_mask_ids = matrix_layer(green_id).value(QStringLiteral("selectionMaskSetIds")).toList();
  blue_mask_thumb = nested_mask_thumbnail(blue_id, red_mask_id);
  green_drop = matrix_layer_row(green_id);
  const auto locked_drop_history_before = editor_preview.editHistoryCurrentIndex();
  const auto locked_drop_layers_before = canonical_matrix_layers();
  const auto locked_drop_dispatched = blue_mask_thumb && green_drop &&
      drag_mask(blue_mask_thumb.data(), green_drop.data(), Qt::NoModifier);
  const auto locked_drop_layers_after = canonical_matrix_layers();
  const auto locked_drop_target_ids_after = matrix_layer(green_id).value(QStringLiteral("selectionMaskSetIds")).toList();
  const auto locked_drop_source_linked_after = matrix_mask_linked(blue_id, red_mask_id);
  if (!locked_drop_dispatched || locked_drop_layers_after != green_layers_locked ||
      locked_drop_layers_before != locked_drop_layers_after ||
      locked_drop_target_ids_after != locked_target_mask_ids || !locked_drop_source_linked_after ||
      editor_preview.editHistoryCurrentIndex() != locked_drop_history_before) {
    const auto failure_capture = QDir(matrix_proof_dir).filePath(QStringLiteral("mask-layer-matrix-locked-drop-failure.png"));
    const auto failure_image = matrix_window->grabWindow();
    std::cerr << "locked-mask-drop stage=" << drag_mask_stage.toStdString()
              << " dispatched=" << locked_drop_dispatched
              << " sourceAlive=" << !blue_mask_thumb.isNull()
              << " destinationAlive=" << !green_drop.isNull()
              << " sourceStillLinked=" << locked_drop_source_linked_after
              << " destinationIdsBefore=";
    for (const auto& value : locked_target_mask_ids) std::cerr << value.toString().toStdString() << ',';
    std::cerr << " destinationIdsAfter=";
    for (const auto& value : locked_drop_target_ids_after) std::cerr << value.toString().toStdString() << ',';
    std::cerr << " recipeStable=" << (locked_drop_layers_before == locked_drop_layers_after)
              << " expectedLockedLayersStable=" << (locked_drop_layers_after == green_layers_locked)
              << " history=" << locked_drop_history_before << "->" << editor_preview.editHistoryCurrentIndex()
              << " sourceIds=";
    for (const auto& value : matrix_layer(blue_id).value(QStringLiteral("selectionMaskSetIds")).toList())
      std::cerr << value.toString().toStdString() << ',';
    std::cerr << " capture=" << (!failure_image.isNull() && failure_image.save(failure_capture))
              << " path=" << failure_capture.toStdString() << '\n';
    return fail("Dropping a mask on a locked layer should be rejected without changing either link");
  }
  green_row = matrix_layer_row(green_id);
  green_name = green_row ? item_descendant_named(green_row, QStringLiteral("photoLayerNameLabel")) : nullptr;
  if (!click_matrix_item(green_name))
    return fail("Mask matrix should reselect the destination before unlocking it");
  layer_actions_button = find_named_item(matrix_edit, QStringLiteral("photoLayerPropertiesLayerActionsMenuButton"));
  if (!click_matrix_item(layer_actions_button))
    return fail("Mask matrix should reopen layer actions before unlocking the destination");
  toggle_lock_action = find_named_item(matrix_edit, QStringLiteral("layerAction_toggle-lock"));
  if (!toggle_lock_action || !click_matrix_item(toggle_lock_action) || !settle_matrix() ||
      matrix_layer(green_id).value(QStringLiteral("locked")).toBool())
    return fail("Mask matrix should unlock the destination through the same layer actions menu");

  blue_mask_thumb = nested_mask_thumbnail(blue_id, red_mask_id);
  green_drop = matrix_layer_row(green_id);
  QPointer<QQuickItem> stale_source(blue_mask_thumb);
  QPointer<QQuickItem> stale_target(green_drop);
  if (!reveal_matrix_pair(stale_source, stale_target))
    return fail("Stale mask drag should begin with its source and destination rows exposed");
  if (!stale_source || !stale_target)
    return fail("Stale mask drag items should survive their scroll exposure");
  auto stale_source_rect = visible_matrix_rect(stale_source);
  auto stale_target_rect = visible_matrix_rect(stale_target);
  if (stale_source_rect.width() < 8 || stale_target_rect.width() < 8)
    return fail("Stale mask drag should use actual visible thumbnail and layer row hit targets");
  const auto stale_start = stale_source_rect.center();
  const auto stale_end = QPointF(stale_target_rect.center().x(),
      stale_target_rect.top() + stale_target_rect.height() * 0.72);
  send_matrix_mouse(QEvent::MouseButtonPress, stale_start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  if (!stale_source || !stale_target) {
    send_matrix_mouse(QEvent::MouseButtonRelease, stale_end, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    return fail("Stale source or destination should not disappear before the drag threshold is crossed");
  }
  send_matrix_mouse(QEvent::MouseMove, stale_start + QPointF(12, 4), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  if (!stale_source) {
    send_matrix_mouse(QEvent::MouseButtonRelease, stale_end, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    return fail("Stale source should survive the drag-start event before deliberate invalidation");
  }
  send_matrix_mouse(QEvent::MouseMove, (stale_start + stale_end) * 0.5, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  const auto stale_drag_started = stale_source && stale_source->property("dragging").toBool();
  const auto green_masks_before_stale_drop = matrix_layer(green_id).value(QStringLiteral("selectionMaskSetIds")).toList();
  if (!stale_drag_started || !editor_preview.removeLayerMask(matrix_layer_index(blue_id), red_mask_id)) {
    send_matrix_mouse(QEvent::MouseButtonRelease, stale_end, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    return fail("Stale drag fixture should invalidate the source link while a real pointer drag is held");
  }
  send_matrix_mouse(QEvent::MouseMove, stale_end, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  send_matrix_mouse(QEvent::MouseButtonRelease, stale_end, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  if (!settle_matrix() || matrix_layer(green_id).value(QStringLiteral("selectionMaskSetIds")).toList() != green_masks_before_stale_drop ||
      matrix_mask_linked(green_id, red_mask_id) || !matrix_mask_linked(green_id, green_copy_id))
    return fail("A stale drag whose source link disappears before drop should not mutate the destination");
  trace_smoke_progress("mask-layer matrix: move, Alt-copy, lock rejection and stale drag passed");

  const auto before_copy_remove_recipe = matrix_recipe();
  const auto before_copy_remove_pixels = matrix_pixels();
  const auto before_copy_remove_masks = editor_preview.maskStack();
  if (!trigger_matrix_menu(mask_menu_button(green_id, green_copy_id, true), QStringLiteral("remove-mask")) ||
      !settle_matrix() || matrix_mask_linked(green_id, green_copy_id) ||
      editor_preview.maskStack() != before_copy_remove_masks)
    return fail("The copied nested mask should also expose its own real Remove from Layer menu action");
  const auto after_copy_remove_pixels = matrix_pixels();
  if (!undo_redo_matrix_recipe(before_copy_remove_recipe, before_copy_remove_pixels,
                        matrix_recipe(), after_copy_remove_pixels) ||
      matrix_mask_linked(green_id, green_copy_id))
    return fail("Undo and Redo should restore and remove the copied mask link without touching its source");

  blue_row = matrix_layer_row(blue_id);
  blue_name = blue_row ? item_descendant_named(blue_row, QStringLiteral("photoLayerNameLabel")) : nullptr;
  if (!click_matrix_item(blue_name) || !settle_matrix())
    return fail("Layer-switch preservation should use a real click on the blue layer name");
  const auto mask_stack_before_switch = editor_preview.maskStack();
  const auto history_before_switch = editor_preview.editHistoryCurrentIndex();
  green_row = matrix_layer_row(green_id);
  green_name = green_row ? item_descendant_named(green_row, QStringLiteral("photoLayerNameLabel")) : nullptr;
  if (!click_matrix_item(green_name) || !settle_matrix() ||
      editor_preview.maskStack() != mask_stack_before_switch ||
      editor_preview.editHistoryCurrentIndex() != history_before_switch ||
      !matrix_mask_present(red_mask_id) || !matrix_mask_present(green_copy_id) ||
      !editor_preview.hasWorkingSelection())
    return fail("Switching the selected layer should preserve the active selection stack and linked mask IDs");

  auto* text_row = matrix_layer_row(text_id);
  auto* text_pixels = text_row ? item_descendant_named(text_row, QStringLiteral("photoLayerPixelsTarget")) : nullptr;
  auto* text_name = text_row ? item_descendant_named(text_row, QStringLiteral("photoLayerNameLabel")) : nullptr;
  if (!text_pixels || !click_matrix_item(text_pixels) || !settle_matrix())
    return fail("Shortcut-focus test should open the real text layer inspector by its thumbnail");
  auto* text_field = find_named_item(matrix_edit, QStringLiteral("photoLayerTextContentField"));
  if (!text_field || !click_matrix_item(text_field))
    return fail("Shortcut-focus test should give the actual text content field keyboard focus");
  QPointer<QQuickItem> text_field_guard(text_field);
  const auto original_tool = matrix_edit->property("editTool").toString();
  const auto send_text_key = [&](int key, const QString& text) {
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
    QCoreApplication::sendEvent(matrix_window, &press);
    QCoreApplication::sendEvent(matrix_window, &release);
    process_layout_events(3);
  };
  send_text_key(Qt::Key_B, QStringLiteral("b"));
  send_text_key(Qt::Key_M, QStringLiteral("m"));
  if (!text_field_guard || !text_field_guard->hasActiveFocus() ||
      matrix_edit->property("editTool").toString() != original_tool ||
      !text_field_guard->property("text").toString().endsWith(QStringLiteral("bm")))
    return fail("B and M should be inserted into a focused text field without triggering tool shortcuts");
  if (!send_window_key(matrix_window, Qt::Key_B, Qt::ControlModifier | Qt::AltModifier) ||
      !send_window_key(matrix_window, Qt::Key_M, Qt::ControlModifier | Qt::AltModifier) ||
      matrix_edit->property("editTool").toString() != original_tool ||
      !text_field_guard || !text_field_guard->property("text").toString().endsWith(QStringLiteral("bm")))
    return fail("Configured Ctrl+Alt+B and Ctrl+Alt+M tool shortcuts should be suppressed while text input owns focus");
  if (!wait_for_condition([&] {
        return text_field_guard && !text_field_guard->property("pendingEdit").toBool() && settle_matrix();
      }, 5000))
    return fail("Focused text shortcut proof should let its ordinary text edit settle before checking selection state");
  const auto focused_mask_stack = editor_preview.maskStack();
  const auto focused_working_masks = editor_preview.workingSelectionMasks();
  const auto focused_mask_ids = matrix_mask_id_list();
  const auto focused_has_working_selection = editor_preview.hasWorkingSelection();
  const auto focused_active_mask_index = editor_preview.activeMaskIndex();
  const auto focused_history = editor_preview.editHistoryCurrentIndex();
  const auto focused_recipe = matrix_recipe();
  const auto focused_pixels = matrix_pixels();
  if (!send_window_key(matrix_window, Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier) ||
      !send_window_key(matrix_window, Qt::Key_D, Qt::ControlModifier) ||
      !text_field_guard || !text_field_guard->hasActiveFocus() ||
      matrix_edit->property("editTool").toString() != original_tool ||
      editor_preview.maskStack() != focused_mask_stack ||
      editor_preview.workingSelectionMasks() != focused_working_masks ||
      matrix_mask_id_list() != focused_mask_ids ||
      editor_preview.hasWorkingSelection() != focused_has_working_selection ||
      editor_preview.activeMaskIndex() != focused_active_mask_index ||
      editor_preview.editHistoryCurrentIndex() != focused_history ||
      matrix_recipe() != focused_recipe || matrix_pixels() != focused_pixels ||
      !text_field_guard->property("text").toString().endsWith(QStringLiteral("bm")))
    return fail("Ctrl+Shift+I and Ctrl+D should leave the selection, mask identities, recipe and history unchanged while text input owns focus");
  text_row = matrix_layer_row(text_id);
  text_name = text_row ? item_descendant_named(text_row, QStringLiteral("photoLayerNameLabel")) : nullptr;
  if (!click_matrix_item(text_name) || !wait_for_condition([&] {
        process_layout_events(2);
        QVariant result;
        return (!text_field_guard || !text_field_guard->hasActiveFocus()) &&
            QMetaObject::invokeMethod(matrix_edit, "shortcutTextInputActive", Q_RETURN_ARG(QVariant, result)) &&
            !result.toBool();
      }, 3000) || !send_window_key(matrix_window, Qt::Key_M, Qt::ControlModifier | Qt::AltModifier) ||
      !wait_for_condition([&] { return matrix_edit->property("editTool").toString() == QStringLiteral("masks"); }, 3000))
    return fail("After text focus leaves the field, M should reach the Selection tool shortcut");

  const auto before_unfocused_invert_recipe = matrix_recipe();
  const auto before_unfocused_invert_stack = editor_preview.maskStack();
  const auto before_unfocused_invert_ids = matrix_mask_id_list();
  const auto before_unfocused_invert_working = editor_preview.workingSelectionMasks();
  const auto before_unfocused_invert_pixels = matrix_pixels();
  const auto before_unfocused_invert_history = editor_preview.editHistoryCurrentIndex();
  QString working_set_id;
  if (!before_unfocused_invert_working.isEmpty()) {
    const auto first_working = before_unfocused_invert_working.first().toMap();
    working_set_id = first_working.value(QStringLiteral("maskSetId")).toString();
    if (working_set_id.isEmpty()) working_set_id = first_working.value(QStringLiteral("id")).toString();
  }
  const auto working_set_inverted = [&](const QVariantList& stack) {
    for (const auto& value : stack) {
      const auto mask = value.toMap();
      if (mask.value(QStringLiteral("maskSetId")).toString() == working_set_id)
        return mask.value(QStringLiteral("maskSetSelectionInvert")).toBool();
    }
    return false;
  };
  const auto working_inverted_before = working_set_inverted(before_unfocused_invert_stack);
  if (working_set_id.isEmpty() ||
      !send_window_key(matrix_window, Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier) ||
      !wait_for_condition([&] {
        return settle_matrix() && matrix_recipe() != before_unfocused_invert_recipe &&
            editor_preview.editHistoryCurrentIndex() > before_unfocused_invert_history;
      }, 5000) || editor_preview.maskStack() == before_unfocused_invert_stack ||
      matrix_mask_id_list() != before_unfocused_invert_ids ||
      !editor_preview.hasWorkingSelection() ||
      working_set_inverted(editor_preview.maskStack()) == working_inverted_before)
    return fail("After text focus leaves, Ctrl+Shift+I should invert the selected working mask without changing its identity");
  auto* matrix_workspace_focus = qobject_cast<QQuickItem*>(matrix_edit);
  if (!matrix_workspace_focus) return fail("Selection shortcut proof should retain a workspace focus owner for Undo");
  matrix_workspace_focus->forceActiveFocus();
  if (!send_window_key(matrix_window, Qt::Key_Z, Qt::ControlModifier) ||
      !wait_for_condition([&] {
        return settle_matrix() && matrix_recipe() == before_unfocused_invert_recipe &&
            editor_preview.maskStack() == before_unfocused_invert_stack &&
            editor_preview.workingSelectionMasks() == before_unfocused_invert_working &&
            editor_preview.editHistoryCurrentIndex() == before_unfocused_invert_history;
      }, 10000) || matrix_pixels() != before_unfocused_invert_pixels ||
      !editor_preview.hasWorkingSelection())
    return fail("Undo after the unfocused invert shortcut should restore the exact working selection and pixels");

  const auto before_unfocused_deselect_recipe = matrix_recipe();
  const auto before_unfocused_deselect_stack = editor_preview.maskStack();
  const auto before_unfocused_deselect_working = editor_preview.workingSelectionMasks();
  const auto before_unfocused_deselect_ids = matrix_mask_id_list();
  const auto before_unfocused_deselect_pixels = matrix_pixels();
  const auto before_unfocused_deselect_history = editor_preview.editHistoryCurrentIndex();
  matrix_workspace_focus->forceActiveFocus();
  if (!send_window_key(matrix_window, Qt::Key_D, Qt::ControlModifier) ||
      !wait_for_condition([&] {
        return settle_matrix() && !editor_preview.hasWorkingSelection() &&
            matrix_recipe() != before_unfocused_deselect_recipe;
      }, 5000) || editor_preview.maskStack() != before_unfocused_deselect_stack ||
      matrix_mask_id_list() != before_unfocused_deselect_ids ||
      editor_preview.editHistoryCurrentIndex() <= before_unfocused_deselect_history)
    return fail("After text focus leaves, Ctrl+D should deselect the working selection while preserving saved mask identities");
  matrix_workspace_focus->forceActiveFocus();
  if (!send_window_key(matrix_window, Qt::Key_Z, Qt::ControlModifier) ||
      !wait_for_condition([&] {
        return settle_matrix() && matrix_recipe() == before_unfocused_deselect_recipe &&
            editor_preview.maskStack() == before_unfocused_deselect_stack &&
            editor_preview.workingSelectionMasks() == before_unfocused_deselect_working &&
            editor_preview.editHistoryCurrentIndex() == before_unfocused_deselect_history &&
            editor_preview.hasWorkingSelection();
      }, 10000) || matrix_pixels() != before_unfocused_deselect_pixels)
    return fail("Undo after the unfocused deselect shortcut should restore the exact working selection and pixels");
  trace_smoke_progress("mask-layer matrix: text focus shortcut guards and unfocused selection shortcuts passed");

  QPointer<QQuickItem> paint_layers_tab(
      find_named_item(matrix_edit, QStringLiteral("layersInspectorTabButton")));
  if (!paint_layers_tab || !click_matrix_item(paint_layers_tab.data()) ||
      !wait_for_condition([&] {
        process_layout_events(2);
        return matrix_edit->property("editTool").toString() == QStringLiteral("layers") &&
            matrix_edit->property("editInspectorTab").toString() == QStringLiteral("layers") &&
            !matrix_edit->property("editToolTransitionActive").toBool();
      }, 5000) || !settle_matrix())
    return fail("Paint target proof should return to Layers by its actual inspector tab after selection shortcuts");

  const auto paint_before_layers = canonical_matrix_layers();
  const auto paint_before_pixels = matrix_pixels();
  const auto paint_before_history = editor_preview.editHistoryCurrentIndex();
  red_row = matrix_layer_row(red_id);
  QPointer<QQuickItem> red_name_target(red_row
      ? item_descendant_named(red_row, QStringLiteral("photoLayerNameLabel")) : nullptr);
  const auto paint_row_click = red_name_target && click_matrix_item(red_name_target.data());
  const auto paint_row_settled = paint_row_click && settle_matrix();
  const auto paint_selected_target = matrix_edit->property("selectedEditTarget").toMap();
  if (!paint_row_settled || paint_selected_target.value(QStringLiteral("scope")).toString() != QStringLiteral("photo-layer") ||
      paint_selected_target.value(QStringLiteral("layerId")).toString() != red_id) {
    const auto failure_capture = QDir(matrix_proof_dir).filePath(QStringLiteral("mask-layer-matrix-paint-target-failure.png"));
    const auto failure_image = matrix_window->grabWindow();
    const auto name_rect = red_name_target ? visible_matrix_rect(red_name_target) : QRectF{};
    std::cerr << "paint-target rowFound=" << (red_row != nullptr)
              << " nameFound=" << !red_name_target.isNull()
              << " rowClick=" << paint_row_click
              << " settled=" << paint_row_settled
              << " rowIndex=" << matrix_layer_index(red_id)
              << " rowVisible=" << (red_row && item_visible(red_row))
              << " nameRect=" << name_rect.x() << ',' << name_rect.y() << ' '
              << name_rect.width() << 'x' << name_rect.height()
              << " target=" << QJsonDocument(QJsonObject::fromVariantMap(paint_selected_target))
                     .toJson(QJsonDocument::Compact).toStdString()
              << " editTool=" << matrix_edit->property("editTool").toString().toStdString()
              << " inspectorTab=" << matrix_edit->property("editInspectorTab").toString().toStdString()
              << " history=" << paint_before_history << "->" << editor_preview.editHistoryCurrentIndex()
              << " capture=" << (!failure_image.isNull() && failure_image.save(failure_capture))
              << " path=" << failure_capture.toStdString() << '\n';
    return fail("Paint target proof should select its stable pixel layer through the row name");
  }
  auto* paint_button = find_named_item(matrix_edit, QStringLiteral("editRailPaintButton"));
  if (!click_matrix_item(paint_button) ||
      !wait_for_condition([&] { return matrix_edit->property("editTool").toString() == QStringLiteral("paint"); }, 5000))
    return fail("Paint clipping proof should enter Paint through the actual tool rail button");
  auto* paint_overlay = find_named_item(matrix_edit, QStringLiteral("photoPaintOverlay"));
  if (!paint_overlay || !paint_overlay->property("active").toBool() ||
      paint_overlay->property("layerData").toMap().value(QStringLiteral("id")).toString() != red_id ||
      paint_overlay->property("layerIndex").toInt() != matrix_layer_index(red_id))
    return fail("Paint overlay should bind to the selected stable layer before the pointer stroke");
  paint_overlay->setProperty("brushSize", 34.0);
  paint_overlay->setProperty("hardness", 0.9);
  paint_overlay->setProperty("brushOpacity", 1.0);
  paint_overlay->setProperty("flow", 1.0);
  paint_overlay->setProperty("ink", QColor(QStringLiteral("#ff29b4")));
  const auto image_x = paint_overlay->property("imageX").toDouble();
  const auto image_y = paint_overlay->property("imageY").toDouble();
  const auto image_w = paint_overlay->property("imageWidth").toDouble();
  const auto image_h = paint_overlay->property("imageHeight").toDouble();
  const auto paint_start = paint_overlay->mapToScene(QPointF(image_x + image_w * 0.26, image_y + image_h * 0.30));
  const auto paint_end = paint_overlay->mapToScene(QPointF(image_x + image_w * 0.39, image_y + image_h * 0.34));
  if (!visible_matrix_rect(paint_overlay).contains(paint_start) ||
      !visible_matrix_rect(paint_overlay).contains(paint_end))
    return fail("Paint proof stroke should stay inside the rendered canvas hit region");
  send_matrix_mouse(QEvent::MouseButtonPress, paint_start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_matrix_mouse(QEvent::MouseMove, (paint_start + paint_end) * 0.5,
      Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  send_matrix_mouse(QEvent::MouseButtonRelease, paint_end, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  if (!wait_for_condition([&] {
        process_layout_events(2);
        return !editor_preview.photoPaintBusy() && !editor_preview.processing() && !editor_preview.fullRenderRunning();
      }, 15000))
    return fail("The selected-layer paint gesture should commit and settle");
  const auto paint_after_layers = canonical_matrix_layers();
  const auto paint_after_pixels = matrix_pixels();
  bool target_layer_changed = false;
  bool non_target_layers_unchanged = paint_before_layers.size() == paint_after_layers.size();
  for (const auto& before_value : paint_before_layers) {
    const auto before_layer = before_value.toMap();
    const auto id = before_layer.value(QStringLiteral("id")).toString();
    QVariantMap after_layer;
    for (const auto& after_value : paint_after_layers)
      if (after_value.toMap().value(QStringLiteral("id")).toString() == id) after_layer = after_value.toMap();
    if (after_layer.isEmpty()) { non_target_layers_unchanged = false; continue; }
    if (id == red_id) target_layer_changed = after_layer != before_layer;
    else if (after_layer != before_layer) non_target_layers_unchanged = false;
  }
  if (!target_layer_changed || !non_target_layers_unchanged || paint_after_pixels.isNull() ||
      paint_before_pixels == paint_after_pixels || editor_preview.editHistoryCurrentIndex() <= paint_before_history)
    return fail("A real brush stroke should change only the selected layer's canonical authored content and rendered pixels");
  trace_smoke_progress("mask-layer matrix: selected-layer paint changed only its authored target");

  matrix_edit_item = qobject_cast<QQuickItem*>(matrix_edit);
  if (!matrix_edit_item)
    return fail("Paint Undo/Redo should retain the workspace keyboard focus owner");
  matrix_edit_item->forceActiveFocus();
  if (!send_window_key(matrix_window, Qt::Key_Z, Qt::ControlModifier) ||
      !wait_for_condition([&] { return settle_matrix() && canonical_matrix_layers() == paint_before_layers; }, 10000) ||
      matrix_pixels() != paint_before_pixels || !send_window_key(matrix_window, Qt::Key_Z,
          Qt::ControlModifier | Qt::ShiftModifier) ||
      !wait_for_condition([&] { return settle_matrix() && canonical_matrix_layers() == paint_after_layers; }, 10000) ||
      matrix_pixels() != paint_after_pixels)
    return fail("Undo and Redo should restore the exact selected-layer paint tiles and rendered pixels");

  // Exercise the requested adjustment path as a full QML interaction: the
  // working selection is attached by Add layer, the layer's pixel target opens
  // Adjust, then an actual window pointer drag changes its Exposure slider.
  QPointer<QQuickItem> adjustment_layers_tab(
      find_named_item(matrix_edit, QStringLiteral("layersInspectorTabButton")));
  if (!adjustment_layers_tab || !click_matrix_item(adjustment_layers_tab) ||
      !wait_for_condition([&] {
        process_layout_events(2);
        return matrix_edit->property("editTool").toString() == QStringLiteral("layers") &&
            matrix_edit->property("editInspectorTab").toString() == QStringLiteral("layers") &&
            !matrix_edit->property("editToolTransitionActive").toBool();
      }, 5000) || !settle_matrix() || !editor_preview.hasWorkingSelection())
    return fail("The masked adjustment proof should return to Layers with the working selection intact");
  QPointer<QQuickItem> add_adjustment_action(find_named_item(matrix_edit,
      QStringLiteral("photoLayerAddSplitButtonPrimaryAction")));
  const auto pre_adjustment_working_set_id = editor_preview.workingSelectionSetId();
  const auto pre_adjustment_layers = canonical_matrix_layers();
  const auto pre_adjustment_recipe = matrix_recipe();
  const auto pre_adjustment_pixels = matrix_pixels();
  const auto pre_adjustment_mask_stack = editor_preview.maskStack();
  const auto pre_adjustment_working_selection = editor_preview.workingSelectionMasks();
  const auto pre_adjustment_history = editor_preview.editHistoryCurrentIndex();
  if (pre_adjustment_working_set_id.isEmpty() || pre_adjustment_recipe.isEmpty() || pre_adjustment_pixels.isNull() ||
      !matrix_pixels_match_cpu(pre_adjustment_pixels, matrix_cpu_render(pre_adjustment_recipe)) ||
      !add_adjustment_action || !click_matrix_item(add_adjustment_action) || !settle_matrix() ||
      canonical_matrix_layers().size() != pre_adjustment_layers.size() + 1)
    return fail("The real Add layer primary action should create an adjustment layer for the active selection");
  const auto adjustment_layers_before = canonical_matrix_layers();
  const auto adjustment_owner_before = adjustment_layers_before.first().toMap();
  const auto adjustment_owner_id = adjustment_owner_before.value(QStringLiteral("id")).toString();
  const auto adjustment_mask_ids = adjustment_owner_before.value(QStringLiteral("selectionMaskSetIds")).toList();
  const auto adjustment_fallback_mask_id = adjustment_owner_before.value(QStringLiteral("selectionMaskSetId")).toString();
  const auto adjustment_has_mask = !adjustment_mask_ids.isEmpty() ||
      !adjustment_fallback_mask_id.isEmpty();
  const auto adjustment_after_recipe = matrix_recipe();
  const auto adjustment_before_mask_stack = editor_preview.maskStack();
  const auto adjustment_before_pixels = matrix_pixels();
  const auto adjustment_add_settled = settle_matrix();
  const auto adjustment_cpu_pixels = matrix_cpu_render(adjustment_after_recipe);
  const auto adjustment_pixels_match_cpu = matrix_pixels_match_cpu(adjustment_before_pixels, adjustment_cpu_pixels);
  const auto adjustment_history_is_one = editor_preview.editHistoryCurrentIndex() == pre_adjustment_history + 1;
  if (adjustment_owner_id.isEmpty() ||
      adjustment_owner_before.value(QStringLiteral("kind")).toString() != QStringLiteral("adjustment") ||
      !adjustment_has_mask || adjustment_before_pixels.isNull() ||
      !adjustment_history_is_one || !adjustment_add_settled || adjustment_after_recipe == pre_adjustment_recipe ||
      !adjustment_pixels_match_cpu) {
    std::cerr << "masked-adjustment-setup id=" << adjustment_owner_id.toStdString()
              << " kind=" << adjustment_owner_before.value(QStringLiteral("kind")).toString().toStdString()
              << " savedSet=" << saved_set_id.toStdString() << " selectionMaskSetId="
              << adjustment_owner_before.value(QStringLiteral("selectionMaskSetId")).toString().toStdString()
              << " selectionMaskSetIds=" << QJsonDocument::fromVariant(adjustment_mask_ids).toJson(QJsonDocument::Compact).toStdString()
              << " layers=" << pre_adjustment_layers.size() << "->" << adjustment_layers_before.size()
              << " history=" << pre_adjustment_history << "->" << editor_preview.editHistoryCurrentIndex()
              << " oneHistory=" << adjustment_history_is_one
              << " addSettled=" << adjustment_add_settled
              << " hasWorkingSelection=" << editor_preview.hasWorkingSelection()
              << " pixelsValid=" << !adjustment_before_pixels.isNull()
              << " cpuValid=" << !adjustment_cpu_pixels.isNull()
              << " pixelsMatchCpu=" << adjustment_pixels_match_cpu
              << " cpuDelta=" << matrix_max_channel_delta(adjustment_before_pixels, adjustment_cpu_pixels)
              << " target=" << matrix_edit->property("selectedEditTarget").toMap()
                     .value(QStringLiteral("layerId")).toString().toStdString() << '\n';
    return fail("The new adjustment layer should own the active editable mask and publish its canonical composite");
  }
  const auto attached_adjustment_mask_id = !adjustment_mask_ids.isEmpty()
      ? adjustment_mask_ids.first().toString() : adjustment_fallback_mask_id;
  QStringList canonical_adjustment_mask_ids;
  for (const auto& value : adjustment_mask_ids) {
    const auto id = value.toString().trimmed();
    if (!id.isEmpty() && !canonical_adjustment_mask_ids.contains(id))
      canonical_adjustment_mask_ids.append(id);
  }
  if (!adjustment_fallback_mask_id.trimmed().isEmpty() &&
      !canonical_adjustment_mask_ids.contains(adjustment_fallback_mask_id.trimmed()))
    canonical_adjustment_mask_ids.append(adjustment_fallback_mask_id.trimmed());
  const auto read_adjustment_projection_ids = [&](QStringList* ids) {
    if (!ids) return false;
    const auto owner_index = matrix_layer_index(adjustment_owner_id);
    QVariant result;
    if (owner_index < 0 || !QMetaObject::invokeMethod(matrix_edit,
            "maskSetsForAdjustmentLayer", Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, QVariant(owner_index))))
      return false;
    ids->clear();
    for (const auto& value : result.toList()) {
      const auto id = value.toMap().value(QStringLiteral("id")).toString().trimmed();
      if (!id.isEmpty()) ids->append(id);
    }
    return true;
  };
  const auto read_mask_set_owner_index = [&](const QVariantMap& mask_set,
                                              const char* method, int* index) {
    if (!index) return false;
    QVariant result;
    const auto invoked = QMetaObject::invokeMethod(matrix_edit, method,
        Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QVariant(mask_set)));
    if (invoked) *index = result.toInt();
    return invoked;
  };
  const auto current_mask_set_rows = matrix_edit->property("maskSetRows").toList();
  QVariantMap original_working_set;
  QVariantMap attached_adjustment_set;
  QString saved_selection_name;
  for (const auto& value : current_mask_set_rows) {
    const auto row = value.toMap();
    const auto id = row.value(QStringLiteral("id")).toString();
    if (id == pre_adjustment_working_set_id) original_working_set = row;
    if (id == saved_set_id) saved_selection_name = row.value(QStringLiteral("name")).toString().trimmed();
    if (id == attached_adjustment_mask_id) attached_adjustment_set = row;
  }
  QStringList projected_adjustment_mask_ids;
  const auto adjustment_projection_invoked =
      read_adjustment_projection_ids(&projected_adjustment_mask_ids);
  const auto original_working_name = original_working_set.value(QStringLiteral("name")).toString().trimmed();
  const auto attached_adjustment_name = attached_adjustment_set.value(QStringLiteral("name")).toString().trimmed();
  int original_linked_layer_index = -2;
  int original_adjustment_layer_index = -2;
  const auto original_link_query_invoked = read_mask_set_owner_index(
      original_working_set, "maskSetLinkedLayerIndex", &original_linked_layer_index);
  const auto original_adjustment_query_invoked = read_mask_set_owner_index(
      original_working_set, "adjustmentLayerIndexForMaskSet", &original_adjustment_layer_index);
  const auto original_same_name_unowned = !original_working_set.isEmpty() &&
      !original_working_name.isEmpty() && original_working_name == attached_adjustment_name &&
      original_link_query_invoked && original_adjustment_query_invoked &&
      original_linked_layer_index == -1 && original_adjustment_layer_index == -1;
  const auto canonical_and_production_associations_exact =
      canonical_adjustment_mask_ids.size() == 1 &&
      canonical_adjustment_mask_ids.first() == attached_adjustment_mask_id &&
      attached_adjustment_mask_id != saved_set_id &&
      attached_adjustment_mask_id != pre_adjustment_working_set_id &&
      adjustment_projection_invoked &&
      projected_adjustment_mask_ids == canonical_adjustment_mask_ids &&
      original_same_name_unowned;
  if (!canonical_and_production_associations_exact) {
    std::cerr << "masked-adjustment-associations owner=" << adjustment_owner_id.toStdString()
              << " canonicalIds=" << QJsonDocument::fromVariant(canonical_adjustment_mask_ids).toJson(QJsonDocument::Compact).toStdString()
              << " projectedIds=" << QJsonDocument::fromVariant(projected_adjustment_mask_ids).toJson(QJsonDocument::Compact).toStdString()
              << " projectionInvoked=" << adjustment_projection_invoked
              << " originalId=" << pre_adjustment_working_set_id.toStdString()
              << " originalName=" << original_working_name.toStdString()
              << " attachedId=" << attached_adjustment_mask_id.toStdString()
              << " attachedName=" << attached_adjustment_name.toStdString()
              << " originalLinkedIndex=" << original_linked_layer_index
              << " originalAdjustmentIndex=" << original_adjustment_layer_index
              << " ownerIndex=" << matrix_layer_index(adjustment_owner_id) << '\n';
    return fail("The adjustment must own exactly its canonical mask ID while the same-name working selection stays standalone");
  }
  const auto adjustment_associations_survive_redo = [&] {
    QStringList projected_ids;
    int linked_index = -2;
    int adjustment_index = -2;
    return read_adjustment_projection_ids(&projected_ids) &&
        projected_ids == canonical_adjustment_mask_ids &&
        read_mask_set_owner_index(original_working_set, "maskSetLinkedLayerIndex", &linked_index) &&
        read_mask_set_owner_index(original_working_set, "adjustmentLayerIndexForMaskSet", &adjustment_index) &&
        linked_index == -1 && adjustment_index == -1;
  };
  const auto adjustment_history_roundtrip = undo_redo_matrix_recipe(pre_adjustment_recipe,
      pre_adjustment_pixels, adjustment_after_recipe, adjustment_before_pixels);
  const auto adjustment_associations_survived_redo = adjustment_associations_survive_redo();
  if (attached_adjustment_mask_id.isEmpty() || attached_adjustment_mask_id == saved_set_id ||
      attached_adjustment_mask_id == pre_adjustment_working_set_id ||
      !matrix_mask_present(attached_adjustment_mask_id) ||
      editor_preview.maskStack() == pre_adjustment_mask_stack ||
      !adjustment_history_roundtrip ||
      canonical_matrix_layers() != adjustment_layers_before ||
      editor_preview.maskStack() != adjustment_before_mask_stack ||
      editor_preview.workingSelectionMasks() != pre_adjustment_working_selection ||
      editor_preview.editHistoryCurrentIndex() != pre_adjustment_history + 1 ||
      !adjustment_associations_survived_redo) {
    std::cerr << "masked-adjustment-history owner=" << adjustment_owner_id.toStdString()
              << " attachedMask=" << attached_adjustment_mask_id.toStdString()
              << " savedMask=" << saved_set_id.toStdString()
              << " maskPresent=" << matrix_mask_present(attached_adjustment_mask_id)
              << " history=" << pre_adjustment_history << "->" << editor_preview.editHistoryCurrentIndex()
              << " layerCount=" << pre_adjustment_layers.size() << "->" << canonical_matrix_layers().size()
              << " maskStackCount=" << pre_adjustment_mask_stack.size() << "->" << editor_preview.maskStack().size()
              << " associationsSurvivedRedo=" << adjustment_associations_survived_redo
              << '\n';
    return fail("One Add layer undo and redo should remove and restore the adjustment layer with its independent mask copy");
  }

  auto* adjustment_row = matrix_layer_row(adjustment_owner_id);
  auto* adjustment_pixels_target = adjustment_row
      ? item_descendant_named(adjustment_row, QStringLiteral("photoLayerPixelsTarget")) : nullptr;
  if (!adjustment_pixels_target || !click_matrix_item(adjustment_pixels_target) || !settle_matrix())
    return fail("The masked adjustment proof should select its owner through the real pixel thumbnail");
  auto adjustment_target = matrix_edit->property("selectedEditTarget").toMap();
  if (adjustment_target.value(QStringLiteral("layerId")).toString() != adjustment_owner_id ||
      adjustment_target.value(QStringLiteral("scope")).toString() != QStringLiteral("adjustment-layer"))
    return fail("The adjustment layer pixel thumbnail should target the exact adjustment owner");
  QPointer<QQuickItem> adjustment_tab(find_named_item(matrix_edit,
      QStringLiteral("adjustInspectorTabButton")));
  if (adjustment_tab && matrix_edit->property("editInspectorTab").toString() != QStringLiteral("adjust") &&
      !click_matrix_item(adjustment_tab))
    return fail("The selected adjustment layer should open its Adjust inspector by the visible tab");
  if (!wait_for_condition([&] {
        process_layout_events(2);
        auto* group = find_named_item(matrix_edit, QStringLiteral("lightInspectorGroup"));
        auto* slider = find_named_item(matrix_edit, QStringLiteral("exposureSlider"));
        return matrix_edit->property("editInspectorTab").toString() == QStringLiteral("adjust") &&
            group && item_visible(group) && slider && item_visible(slider);
      }, 5000))
    return fail("The selected adjustment owner should expose the Light Exposure slider");
  QPointer<QQuickItem> exposure_slider(find_named_item(matrix_edit, QStringLiteral("exposureSlider")));
  QPointer<QQuickItem> exposure_control(find_named_item(matrix_edit, QStringLiteral("exposureSliderControl")));
  QPointer<QQuickItem> exposure_pointer(find_named_item(matrix_edit,
      QStringLiteral("exposureSliderPrecisionPointer")));
  if (!exposure_slider || !exposure_control || !exposure_pointer || !reveal_matrix_item(exposure_slider) ||
      !exposure_slider || !exposure_control || !exposure_pointer || !exposure_control->isEnabled())
    return fail("The enabled Exposure slider should be revealed for the actual masked adjustment drag");

  const auto slider_before_recipe = matrix_recipe();
  const auto slider_before_cpu = matrix_cpu_render(slider_before_recipe);
  const auto slider_before_pixels = matrix_pixels();
  const auto slider_before_layers = canonical_matrix_layers();
  const auto slider_before_mask_stack = editor_preview.maskStack();
  const auto slider_before_working_selection = editor_preview.workingSelectionMasks();
  const auto slider_before_history = editor_preview.editHistoryCurrentIndex();
  const auto slider_bounds = exposure_pointer->boundingRect();
  const auto slider_rect = visible_matrix_rect(exposure_pointer);
  if (slider_rect.width() < 80 || slider_rect.height() < 8 || slider_before_recipe.isEmpty() ||
      slider_before_cpu.isNull() || !matrix_pixels_match_cpu(slider_before_pixels, slider_before_cpu))
    return fail("The slider proof should start from a visible, canonically published masked adjustment baseline");
  const auto slider_start = exposure_pointer->mapToScene(QPointF(slider_bounds.width() * 0.5,
      slider_bounds.height() * 0.5));
  const auto slider_end = exposure_pointer->mapToScene(QPointF(slider_bounds.width() * 0.75,
      slider_bounds.height() * 0.5));
  const auto slider_window_bounds = QRectF(0, 0, matrix_window->width(), matrix_window->height());
  if (!slider_window_bounds.contains(slider_start) || !slider_window_bounds.contains(slider_end))
    return fail("The Exposure drag should stay inside the visible application window");
  send_matrix_mouse(QEvent::MouseButtonPress, slider_start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_matrix_mouse(QEvent::MouseMove, (slider_start + slider_end) * 0.5,
      Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  send_matrix_mouse(QEvent::MouseMove, slider_end, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  send_matrix_mouse(QEvent::MouseButtonRelease, slider_end, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  const auto slider_settled = wait_for_condition([&] {
    return settle_matrix() && matrix_recipe() != slider_before_recipe &&
        editor_preview.editHistoryCurrentIndex() > slider_before_history;
  }, 15000);
  const auto slider_after_recipe = matrix_recipe();
  const auto slider_after_cpu = matrix_cpu_render(slider_after_recipe);
  const auto slider_after_pixels = matrix_pixels();
  const auto slider_after_layers = canonical_matrix_layers();
  const auto slider_after_mask_stack = editor_preview.maskStack();
  const auto slider_after_working_selection = editor_preview.workingSelectionMasks();
  QVariantMap slider_owner_before;
  for (const auto& layer_value : slider_before_layers) {
    const auto candidate = layer_value.toMap();
    if (candidate.value(QStringLiteral("id")).toString() == adjustment_owner_id)
      slider_owner_before = candidate;
  }
  QVariantMap slider_owner_after;
  for (const auto& layer_value : slider_after_layers) {
    const auto candidate = layer_value.toMap();
    if (candidate.value(QStringLiteral("id")).toString() == adjustment_owner_id)
      slider_owner_after = candidate;
  }
  bool sibling_layers_unchanged = slider_before_layers.size() == slider_after_layers.size();
  for (const auto& before_layer_value : slider_before_layers) {
    const auto before_layer = before_layer_value.toMap();
    const auto id = before_layer.value(QStringLiteral("id")).toString();
    if (id == adjustment_owner_id) continue;
    const auto match = std::find_if(slider_after_layers.cbegin(), slider_after_layers.cend(), [&](const QVariant& value) {
      return value.toMap().value(QStringLiteral("id")).toString() == id;
    });
    if (match == slider_after_layers.cend() || match->toMap() != before_layer) sibling_layers_unchanged = false;
  }
  auto slider_owner_before_without_exposure = slider_owner_before;
  slider_owner_before_without_exposure.remove(QStringLiteral("exposure"));
  for (const auto* derived_key : {"changedAdjustmentGroupLabel", "changedAdjustmentGroups",
                                  "hasNonNeutralAdjustments", "layerAuditLabel"})
    slider_owner_before_without_exposure.remove(QString::fromLatin1(derived_key));
  auto slider_owner_after_without_exposure = slider_owner_after;
  slider_owner_after_without_exposure.remove(QStringLiteral("exposure"));
  for (const auto* derived_key : {"changedAdjustmentGroupLabel", "changedAdjustmentGroups",
                                  "hasNonNeutralAdjustments", "layerAuditLabel"})
    slider_owner_after_without_exposure.remove(QString::fromLatin1(derived_key));
  const auto changed_groups = slider_owner_after.value(QStringLiteral("changedAdjustmentGroups")).toList();
  const auto one_light_group = changed_groups.size() == 1 &&
      changed_groups.first().toMap().value(QStringLiteral("key")).toString() == QStringLiteral("light") &&
      changed_groups.first().toMap().value(QStringLiteral("label")).toString() == QStringLiteral("Light");
  const auto adjustment_summary_matches =
      slider_owner_after.value(QStringLiteral("changedAdjustmentGroupLabel")).toString() == QStringLiteral("Light") &&
      slider_owner_after.value(QStringLiteral("hasNonNeutralAdjustments")).toBool() && one_light_group &&
      slider_owner_after.value(QStringLiteral("layerAuditLabel")).toString().contains(QStringLiteral("edited: Light"));
  const auto exposure_before = slider_owner_before.value(QStringLiteral("exposure")).toDouble();
  const auto exposure_after = slider_owner_after.value(QStringLiteral("exposure")).toDouble();
  if (!slider_settled || slider_owner_after.isEmpty() || exposure_after <= exposure_before + 0.15 ||
      slider_owner_after_without_exposure != slider_owner_before_without_exposure ||
      !adjustment_summary_matches || !sibling_layers_unchanged || slider_after_mask_stack != slider_before_mask_stack ||
      slider_after_working_selection != slider_before_working_selection ||
      slider_after_pixels.isNull() || slider_after_pixels == slider_before_pixels || slider_after_cpu.isNull() ||
      !matrix_pixels_match_cpu(slider_after_pixels, slider_after_cpu)) {
    QStringList changed_owner_keys;
    auto owner_keys = slider_owner_before.keys();
    for (const auto& key : slider_owner_after.keys())
      if (!owner_keys.contains(key)) owner_keys.append(key);
    for (const auto& key : owner_keys)
      if (slider_owner_before.value(key) != slider_owner_after.value(key)) changed_owner_keys.append(key);
    const auto failure_image = matrix_window->grabWindow();
    failure_image.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-layer-matrix-adjustment-slider-failure.png")));
    std::cerr << "masked-adjustment owner=" << adjustment_owner_id.toStdString()
              << " exposure=" << exposure_before << "->" << exposure_after
              << " settled=" << slider_settled << " siblingLayersUnchanged=" << sibling_layers_unchanged
              << " maskStackUnchanged=" << (slider_after_mask_stack == slider_before_mask_stack)
              << " workingSelectionUnchanged=" << (slider_after_working_selection == slider_before_working_selection)
              << " adjustmentSummaryMatches=" << adjustment_summary_matches
              << " ownerChangedKeys=" << changed_owner_keys.join(QLatin1Char(',')).toStdString()
              << " ownerBefore=" << QJsonDocument::fromVariant(slider_owner_before).toJson(QJsonDocument::Compact).toStdString()
              << " ownerAfter=" << QJsonDocument::fromVariant(slider_owner_after).toJson(QJsonDocument::Compact).toStdString()
              << " pixelsChanged=" << (slider_after_pixels != slider_before_pixels)
              << " previewCpuDelta=" << matrix_max_channel_delta(slider_after_pixels, slider_after_cpu)
              << " history=" << slider_before_history << "->" << editor_preview.editHistoryCurrentIndex()
              << " target=" << matrix_edit->property("selectedEditTarget").toMap()
                     .value(QStringLiteral("layerId")).toString().toStdString() << '\n';
    return fail("A real Exposure slider drag should edit only the masked adjustment owner and publish its exact recipe");
  }

  QStringList alpha_mask_ids;
  for (const auto& value : slider_owner_after.value(QStringLiteral("selectionMaskSetIds")).toList())
    if (!value.toString().isEmpty() && !alpha_mask_ids.contains(value.toString()))
      alpha_mask_ids.append(value.toString());
  if (alpha_mask_ids.isEmpty() && !slider_owner_after.value(QStringLiteral("selectionMaskSetId")).toString().isEmpty())
    alpha_mask_ids.append(slider_owner_after.value(QStringLiteral("selectionMaskSetId")).toString());
  if (alpha_mask_ids.isEmpty())
    return fail("The adjustment pixel test should have the editable selection mask IDs on its owner");
  auto raster_mask_ids = alpha_mask_ids;
  if (!raster_mask_ids.contains(saved_set_id)) raster_mask_ids.append(saved_set_id);
  std::vector<std::vector<std::uint8_t>> adjustment_mask_alpha;
  for (const auto& mask_id : raster_mask_ids) {
    photo_editor::pipeline::LocalMaskSet mask_set;
    mask_set.id = mask_id.toStdString();
    bool has_definition = false;
    for (const auto& mask_value : slider_after_mask_stack) {
      const auto mask = mask_value.toMap();
      if (mask.value(QStringLiteral("maskSetId")).toString() != mask_id) continue;
      if (!has_definition) {
        mask_set.name = mask.value(QStringLiteral("maskSetName")).toString().toStdString();
        mask_set.enabled = mask.value(QStringLiteral("enabled"), true).toBool();
        mask_set.selection_invert = mask.value(QStringLiteral("maskSetSelectionInvert")).toBool();
        mask_set.selection_expand = mask.value(QStringLiteral("maskSetSelectionExpand")).toDouble();
        mask_set.selection_smooth = mask.value(QStringLiteral("maskSetSelectionSmooth")).toDouble();
        mask_set.selection_fine_edge = mask.value(QStringLiteral("maskSetSelectionFineEdge")).toDouble();
        mask_set.selection_edge_contrast = mask.value(QStringLiteral("maskSetSelectionEdgeContrast")).toDouble();
        mask_set.opacity = mask.value(QStringLiteral("maskSetOpacity"), 1.0).toDouble();
        mask_set.density = mask.value(QStringLiteral("maskSetDensity"), 1.0).toDouble();
        has_definition = true;
      }
      mask_set.sources.push_back(photo_editor::desktop::compile_pipeline_local_mask(
          mask, photo_editor::desktop::LocalMaskRasterPolicy::RebuildEditableAtOutputResolution,
          matrix_photo.size()));
    }
    if (!has_definition || mask_set.sources.empty())
      return fail("Every adjustment mask ID should retain its complete editable source definitions");
    const auto alpha = photo_editor::pipeline::rasterize_local_mask_set_alpha(
        mask_set, matrix_photo.width(), matrix_photo.height(), {});
    if (alpha.size() != static_cast<std::size_t>(matrix_photo.width() * matrix_photo.height()))
      return fail("The adjustment mask should rasterize at the authored photo resolution");
    adjustment_mask_alpha.push_back(alpha);
  }
  std::vector<std::uint8_t> combined_adjustment_alpha(
      static_cast<std::size_t>(matrix_photo.width() * matrix_photo.height()), 0);
  const auto saved_alpha_index = raster_mask_ids.indexOf(saved_set_id);
  if (saved_alpha_index < 0 || static_cast<std::size_t>(saved_alpha_index) >= adjustment_mask_alpha.size())
    return fail("The original saved selection should remain available as a coverage reference");
  for (const auto& mask_id : alpha_mask_ids) {
    const auto attached_alpha_index = raster_mask_ids.indexOf(mask_id);
    if (attached_alpha_index < 0 || static_cast<std::size_t>(attached_alpha_index) >= adjustment_mask_alpha.size() ||
        adjustment_mask_alpha[static_cast<std::size_t>(attached_alpha_index)] !=
            adjustment_mask_alpha[static_cast<std::size_t>(saved_alpha_index)])
      return fail("The independently attached mask should preserve the saved selection coverage exactly");
    const auto& alpha = adjustment_mask_alpha[static_cast<std::size_t>(attached_alpha_index)];
    for (std::size_t index = 0; index < alpha.size(); ++index)
      combined_adjustment_alpha[index] = std::max(combined_adjustment_alpha[index], alpha[index]);
  }
  int changed_inside_mask = 0;
  int changed_outside_mask = 0;
  const auto slider_before_argb = slider_before_cpu.convertToFormat(QImage::Format_ARGB32);
  const auto slider_after_argb = slider_after_cpu.convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < slider_after_argb.height(); ++y) {
    for (int x = 0; x < slider_after_argb.width(); ++x) {
      const auto before = slider_before_argb.pixel(x, y);
      const auto after = slider_after_argb.pixel(x, y);
      const auto delta = qMax(qAbs(qRed(before) - qRed(after)),
          qMax(qAbs(qGreen(before) - qGreen(after)), qAbs(qBlue(before) - qBlue(after))));
      if (delta < 3) continue;
      const auto mask_index = static_cast<std::size_t>(y * slider_after_argb.width() + x);
      if (mask_index < combined_adjustment_alpha.size() && combined_adjustment_alpha[mask_index] > 0)
        ++changed_inside_mask;
      else
        ++changed_outside_mask;
    }
  }
  if (changed_inside_mask <= 0 || changed_outside_mask != 0) {
    std::cerr << "masked-adjustment pixel-scope inside=" << changed_inside_mask
              << " outside=" << changed_outside_mask << " maskIds=";
    for (const auto& id : alpha_mask_ids) std::cerr << id.toStdString() << ',';
    std::cerr << '\n';
    return fail("Exposure should change composite pixels inside its attached selection only");
  }
  const auto slider_history_roundtrip = undo_redo_matrix_recipe(slider_before_recipe, slider_before_pixels,
      slider_after_recipe, slider_after_pixels);
  const auto slider_redo_state_restored = matrix_recipe() == slider_after_recipe &&
      editor_preview.maskStack() == slider_after_mask_stack &&
      editor_preview.workingSelectionMasks() == slider_after_working_selection &&
      canonical_matrix_layers() == slider_after_layers &&
      matrix_pixels_match_cpu(matrix_pixels(), slider_after_cpu);
  if (!slider_history_roundtrip || !slider_redo_state_restored) {
    std::cerr << "masked-adjustment-history roundtrip=" << slider_history_roundtrip
              << " redoState=" << slider_redo_state_restored
              << " recipeMatches=" << (matrix_recipe() == slider_after_recipe)
              << " layersMatch=" << (canonical_matrix_layers() == slider_after_layers)
              << " masksMatch=" << (editor_preview.maskStack() == slider_after_mask_stack)
              << " workingSelectionMatch=" << (editor_preview.workingSelectionMasks() == slider_after_working_selection)
              << " previewCpuDelta=" << matrix_max_channel_delta(matrix_pixels(), slider_after_cpu)
              << '\n';
    return fail("Undo and Redo should restore the exact masked adjustment, masks and published pixels");
  }
  trace_smoke_progress("mask-layer matrix: masked adjustment slider changed only covered pixels with exact undo/redo");

  const auto adjustment_export_history = editor_preview.editHistoryCurrentIndex();
  const auto adjustment_export_path = QDir(matrix_proof_dir).filePath(
      QStringLiteral("mask-layer-matrix-masked-adjustment-export.png"));
  if (!editor_preview.exportFlattenedPhoto(adjustment_export_path))
    return fail("The masked adjustment proof should export its flattened PNG from the actual editor controller");
  const QImage adjustment_exported_pixels(adjustment_export_path);
  if (adjustment_exported_pixels.isNull() || adjustment_exported_pixels.size() != matrix_photo.size() ||
      matrix_max_channel_delta(adjustment_exported_pixels, slider_after_cpu) > 2 ||
      editor_preview.editHistoryCurrentIndex() != adjustment_export_history ||
      matrix_recipe() != slider_after_recipe)
    return fail("Flattened export should match the masked Exposure preview without changing recipe or history");
  if (!capture_visual_proof(matrix_window, matrix_root, matrix_proof_dir,
          QStringLiteral("mask-layer-matrix-masked-adjustment"), 1440, 900,
          {QStringLiteral("editImageViewport"), QStringLiteral("lightInspectorGroup")}, &matrix_captures))
    return fail("The masked adjustment proof should capture the real Light inspector and composite viewport");

  QPointer<QQuickItem> escape_layers_tab(find_named_item(matrix_edit,
      QStringLiteral("layersInspectorTabButton")));
  if (!escape_layers_tab || !click_matrix_item(escape_layers_tab) ||
      !wait_for_condition([&] {
        process_layout_events(2);
        return matrix_edit->property("editTool").toString() == QStringLiteral("layers") &&
            matrix_edit->property("editInspectorTab").toString() == QStringLiteral("layers") &&
            !matrix_edit->property("editToolTransitionActive").toBool();
      }, 5000) || !settle_matrix())
    return fail("Escape proof should return to the Layers inspector using its actual tab");
  auto* escape_menu_button = mask_menu_button(QString(), saved_set_id, false);
  if (!escape_menu_button || !click_matrix_menu_opener(escape_menu_button))
    return fail("The standalone mask menu should open from its real More button before testing Escape");
  QPointer<QQuickItem> escape_opener(escape_menu_button);
  QPointer<QObject> escape_popup(find_named_object(escape_menu_button,
      escape_menu_button->objectName() + QStringLiteral("Popup")));
  if (!escape_popup || !wait_for_condition([&] {
        return escape_popup && escape_popup->property("opened").toBool();
      }, 3000))
    return fail("The mask menu should become the active popup before Escape is sent to the QQuickWindow");
  QPointer<QQuickItem> escape_restore_target(
      qvariant_cast<QQuickItem*>(escape_popup->property("transientFocusTarget")));
  if (!escape_restore_target)
    escape_restore_target = qvariant_cast<QQuickItem*>(escape_popup->property("transientOpener"));
  if (!escape_restore_target || !escape_restore_target->isVisible())
    return fail("The open mask menu should retain a visible opener focus target for restoration");
  QQuickItem* escape_restore_owner = escape_restore_target;
  while (escape_restore_owner && escape_restore_owner != escape_opener)
    escape_restore_owner = escape_restore_owner->parentItem();
  if (escape_restore_owner != escape_opener)
    return fail("The menu's restore focus target should be a visible child of its More button");
  // Resolve the row inside this exact popup. The Main workspace hosts many
  // hidden More menus with the same generated PopupItem0 object name.
  QPointer<QQuickItem> escape_focus_item(find_named_item(escape_popup,
      escape_menu_button->objectName() + QStringLiteral("PopupItem0")));
  if (!escape_focus_item || !escape_focus_item->isVisible())
    return fail("The open mask menu should expose its first keyboard focus target");
  QPointer<QQuickItem> escape_popup_content(qvariant_cast<QQuickItem*>(escape_popup->property("contentItem")));
  QQuickItem* escape_focus_owner = escape_focus_item;
  while (escape_focus_owner && escape_focus_owner != escape_popup_content)
    escape_focus_owner = escape_focus_owner->parentItem();
  if (!escape_popup_content || escape_focus_owner != escape_popup_content)
    return fail("Escape focus should belong to the exact open mask menu, not another hidden More menu");
  escape_focus_item->forceActiveFocus(Qt::TabFocusReason);
  if (!wait_for_condition([&] {
        process_layout_events(1);
        return matrix_window->activeFocusItem() == escape_focus_item;
      }, 2000))
    return fail("The open mask menu should own keyboard focus before the Escape input");
  QSignalSpy escape_dismiss_requested(escape_focus_item, SIGNAL(dismissRequested()));
  const auto escape_recipe_before = matrix_recipe();
  const auto escape_history_before = editor_preview.editHistoryCurrentIndex();
  const auto escape_tool_before = matrix_edit->property("editTool").toString();
  QTest::keyClick(escape_focus_item->window(), Qt::Key_Escape);
  if (!wait_for_condition([&] {
        process_layout_events(2);
        return !escape_popup || !escape_popup->property("opened").toBool();
      }, 3000)) {
    const auto failure_image = matrix_window->grabWindow();
    failure_image.save(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-layer-matrix-escape-failure.png")));
    std::cerr << "escape-debug popupOpened=" << (!escape_popup.isNull() && escape_popup->property("opened").toBool())
              << " popupVisible=" << (!escape_popup.isNull() && escape_popup->property("visible").toBool())
              << " transient=" << (!escape_popup.isNull() && escape_popup->property("transientMenu").toBool())
              << " popupFocus=" << (!escape_popup.isNull() && escape_popup->property("focus").toBool())
              << " mainWindow=" << matrix_window << " itemWindow=" << (escape_focus_item ? escape_focus_item->window() : nullptr)
              << " activeWindow=" << QGuiApplication::focusWindow()
              << " mainIsActive=" << matrix_window->isActive()
              << " activeFocus=" << (matrix_window->activeFocusItem() ? matrix_window->activeFocusItem()->objectName().toStdString() : "<none>")
              << " dismissRequestedCount=" << escape_dismiss_requested.count()
              << " opener=" << (escape_opener ? escape_opener->objectName().toStdString() : "<destroyed>")
              << '\n';
    QList<QObject*> shortcut_pending{matrix_root};
    QSet<QObject*> shortcut_visited;
    while (!shortcut_pending.isEmpty()) {
      auto* object = shortcut_pending.takeLast();
      if (!object || shortcut_visited.contains(object)) continue;
      shortcut_visited.insert(object);
      const auto sequence_index = object->metaObject()->indexOfProperty("sequence");
      if (sequence_index >= 0) {
        const auto sequence = object->property("sequence").toString();
        if (sequence.contains(QStringLiteral("Escape"), Qt::CaseInsensitive)) {
          const auto enabled = object->property("enabled");
          std::cerr << "escape-shortcut object=" << object->objectName().toStdString()
                    << " class=" << object->metaObject()->className()
                    << " sequence=" << sequence.toStdString()
                    << " enabled=" << (enabled.isValid() ? enabled.toString().toStdString() : "<missing>")
                    << " context=" << object->property("context").toString().toStdString()
                    << " parent=" << (object->parent() ? object->parent()->objectName().toStdString() : "<none>")
                    << " parentClass=" << (object->parent() ? object->parent()->metaObject()->className() : "<none>")
                    << " parentVisible=" << (object->parent() ? object->parent()->property("visible").toString().toStdString() : "<none>")
                    << " parentEnabled=" << (object->parent() ? object->parent()->property("enabled").toString().toStdString() : "<none>")
                    << '\n';
        }
      }
      for (auto* child : object->children()) shortcut_pending.append(child);
    }
    return fail("Escape should close an open mask More menu through the actual QQuickWindow keyboard path");
  }
  const auto escape_focus_restored = wait_for_condition([&] {
    return matrix_window->activeFocusItem() == escape_restore_target;
  }, 2000);
  const auto escape_recipe_unchanged = matrix_recipe() == escape_recipe_before;
  const auto escape_history_unchanged = editor_preview.editHistoryCurrentIndex() == escape_history_before;
  const auto escape_tool_unchanged = matrix_edit->property("editTool").toString() == escape_tool_before;
  if (!escape_focus_restored || !escape_recipe_unchanged || !escape_history_unchanged ||
      !escape_tool_unchanged) {
    std::cerr << "escape-restore-debug popupAlive=" << !escape_popup.isNull()
              << " popupOpened=" << (!escape_popup.isNull() && escape_popup->property("opened").toBool())
              << " popupVisible=" << (!escape_popup.isNull() && escape_popup->property("visible").toBool())
              << " dismissRequestedCount=" << escape_dismiss_requested.count()
              << " activeFocus=" << (matrix_window->activeFocusItem()
                  ? matrix_window->activeFocusItem()->objectName().toStdString() : "<none>")
              << " restoreTarget=" << (escape_restore_target
                  ? escape_restore_target->objectName().toStdString() : "<destroyed>")
              << " restoreTargetVisible=" << (!escape_restore_target.isNull() && escape_restore_target->isVisible())
              << " menuWrapper=" << (escape_opener
                  ? escape_opener->objectName().toStdString() : "<destroyed>")
              << " toolBefore=" << escape_tool_before.toStdString()
              << " toolAfter=" << matrix_edit->property("editTool").toString().toStdString()
              << " recipeUnchanged=" << escape_recipe_unchanged
              << " historyBefore=" << escape_history_before
              << " historyAfter=" << editor_preview.editHistoryCurrentIndex()
              << " focusRestored=" << escape_focus_restored << '\n';
    return fail("Escape should restore More focus without changing the active tool, recipe or history");
  }
  trace_smoke_progress("mask-layer matrix: Escape closed the mask menu and restored its opener focus");

  const auto reopen_recipe = matrix_recipe();
  const auto reopen_provider_pixels = matrix_pixels();
  photo_editor::desktop::EditorPreviewController reopened_matrix;
  reopened_matrix.loadAsset(QStringLiteral("mask-layer-matrix-reopened"), matrix_photo_path,
      {}, {}, {}, {}, reopen_recipe);
  if (!wait_for_condition([&] {
        process_layout_events(2);
        return reopened_matrix.hasAsset() &&
            photo_editor::desktop::EditorPreviewControllerTestAccess::preview_idle(reopened_matrix) &&
            !reopened_matrix.professionalToolsBusy() && !reopened_matrix.selectionWorkBusy() &&
            !reopened_matrix.photoExportBusy();
      }, 60000))
    return fail("A fresh controller should reopen the authored mask and layer recipe to an idle preview");
  const auto reopened_recipe =
      photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(reopened_matrix);
  const auto reopened_export_path = QDir(matrix_proof_dir).filePath(
      QStringLiteral("mask-layer-matrix-fresh-controller-export.png"));
  if (reopened_recipe != reopen_recipe || !reopened_matrix.exportFlattenedPhoto(reopened_export_path))
    return fail("Fresh-controller reopen should preserve the exact saved recipe and export it");
  const QImage reopened_pixels(reopened_export_path);
  if (reopened_pixels.isNull() || reopened_pixels.size() != reopen_provider_pixels.size() ||
      matrix_max_channel_delta(reopened_pixels, reopen_provider_pixels) > 2)
    return fail("Fresh-controller reopened export should match the authored editor preview pixels");
  trace_smoke_progress("mask-layer matrix: fresh controller reopened the exact recipe and matched provider pixels");

  // Seed a separate asset-bound state with one primary and one explicit
  // intersection ID on the same adjustment. The UI proof below then routes to
  // that exact nested row and removes only the intersection attachment.
  const auto intersection_baseline_recipe = matrix_recipe();
  const auto intersection_baseline_pixels = matrix_pixels();
  const auto intersection_baseline_history = editor_preview.editHistoryCurrentIndex();
  auto intersection_state = photo_editor::desktop::parse_edit_recipe_json(intersection_baseline_recipe);
  bool intersection_owner_found = false;
  for (auto& value : intersection_state.layers) {
    auto layer = value.toMap();
    if (layer.value(QStringLiteral("id")).toString() != adjustment_owner_id) continue;
    QStringList primary_ids = layer.value(QStringLiteral("selectionMaskSetIds")).toStringList();
    if (primary_ids.isEmpty()) {
      for (const auto& id_value : layer.value(QStringLiteral("selectionMaskSetIds")).toList())
        primary_ids.append(id_value.toString());
    }
    if (primary_ids.size() != 1 || primary_ids.first() != attached_adjustment_mask_id ||
        !layer.value(QStringLiteral("selectionIntersectionMaskSetIds")).toStringList().isEmpty() ||
        !layer.value(QStringLiteral("selectionIntersectionMaskSetIds")).toList().isEmpty())
      break;
    layer.insert(QStringLiteral("selectionIntersectionMaskSetIds"), QStringList{saved_set_id});
    layer.insert(QStringLiteral("selectionIntersectionMaskSetNames"), QStringList{saved_selection_name});
    value = layer;
    intersection_owner_found = true;
    break;
  }
  const auto intersection_seed_recipe = photo_editor::desktop::serialize_edit_recipe_json(intersection_state);
  const auto intersection_previous_patch = editor_preview.currentAssetBoundAdjustmentPatchJson();
  if (!intersection_owner_found || saved_selection_name.isEmpty() || intersection_seed_recipe.isEmpty() ||
      intersection_baseline_pixels.isNull() ||
      !matrix_pixels_match_cpu(intersection_baseline_pixels,
          matrix_cpu_render(intersection_baseline_recipe)))
    return fail("The intersection fixture should start from the exact primary-mask adjustment and published composite");
  editor_preview.applyCatalogRecipeChange(editor_preview.assetId(), matrix_photo_path,
      intersection_previous_patch, intersection_seed_recipe, false);
  if (!settle_matrix() || matrix_recipe() == intersection_baseline_recipe ||
      editor_preview.editHistoryCurrentIndex() != intersection_baseline_history + 1)
    return fail("The isolated asset-bound recipe should seed one explicit intersection attachment history entry");
  const auto seeded_intersection_layers = canonical_matrix_layers();
  QVariantMap seeded_intersection_owner;
  for (const auto& value : seeded_intersection_layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("id")).toString() == adjustment_owner_id)
      seeded_intersection_owner = layer;
  }
  QStringList expected_intersection_ids{attached_adjustment_mask_id, saved_set_id};
  QStringList seeded_primary_ids = seeded_intersection_owner.value(QStringLiteral("selectionMaskSetIds")).toStringList();
  if (seeded_primary_ids.isEmpty()) {
    for (const auto& value : seeded_intersection_owner.value(QStringLiteral("selectionMaskSetIds")).toList())
      seeded_primary_ids.append(value.toString());
  }
  QStringList seeded_intersection_ids = seeded_intersection_owner.value(QStringLiteral("selectionIntersectionMaskSetIds")).toStringList();
  if (seeded_intersection_ids.isEmpty()) {
    for (const auto& value : seeded_intersection_owner.value(QStringLiteral("selectionIntersectionMaskSetIds")).toList())
      seeded_intersection_ids.append(value.toString());
  }
  QStringList seeded_projection_ids;
  const auto seeded_projection_exact = read_adjustment_projection_ids(&seeded_projection_ids) &&
      seeded_projection_ids == expected_intersection_ids;
  if (seeded_primary_ids != QStringList{attached_adjustment_mask_id} ||
      seeded_intersection_ids != QStringList{saved_set_id} || !seeded_projection_exact) {
    std::cerr << "intersection-seed primary=" << QJsonDocument::fromVariant(seeded_primary_ids).toJson(QJsonDocument::Compact).toStdString()
              << " intersection=" << QJsonDocument::fromVariant(seeded_intersection_ids).toJson(QJsonDocument::Compact).toStdString()
              << " projection=" << QJsonDocument::fromVariant(seeded_projection_ids).toJson(QJsonDocument::Compact).toStdString()
              << " expected=" << QJsonDocument::fromVariant(expected_intersection_ids).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
    return fail("The production adjustment projection should expose exactly its primary and explicit intersection mask IDs");
  }
  QPointer<QQuickItem> intersection_layers_tab(
      find_named_item(matrix_edit, QStringLiteral("layersInspectorTabButton")));
  if (!intersection_layers_tab || !click_matrix_item(intersection_layers_tab) ||
      !wait_for_condition([&] {
        process_layout_events(2);
        return matrix_edit->property("editTool").toString() == QStringLiteral("layers") &&
            matrix_edit->property("editInspectorTab").toString() == QStringLiteral("layers") &&
            !matrix_edit->property("editToolTransitionActive").toBool();
      }, 5000) || !settle_matrix())
    return fail("The intersection proof should show the Layers inspector before expanding nested mask rows");
  if (!expand_nested_mask_section(adjustment_owner_id))
    return fail("The two-mask adjustment row should expand through its real nested mask section control");
  auto* primary_intersection_thumbnail = nested_mask_thumbnail(adjustment_owner_id, attached_adjustment_mask_id);
  auto* explicit_intersection_thumbnail = nested_mask_thumbnail(adjustment_owner_id, saved_set_id);
  if (!primary_intersection_thumbnail || !explicit_intersection_thumbnail ||
      visible_matrix_rect(primary_intersection_thumbnail).isEmpty() ||
      visible_matrix_rect(explicit_intersection_thumbnail).isEmpty())
    return fail("The rendered adjustment row should expose visible nested thumbnails for both exact mask IDs");
  if (!capture_visual_proof(matrix_window, matrix_root, matrix_proof_dir,
          QStringLiteral("mask-layer-matrix-intersection-two-mask-owner"), 1440, 900,
          {QStringLiteral("editImageViewport"), QStringLiteral("photoLayersInspectorGroup")}, &matrix_captures))
    return fail("The two-mask intersection fixture should capture its real nested rows and canvas");
  trace_smoke_progress("mask-layer matrix: exact primary and intersection mask IDs appear in nested rows");
  if (!click_matrix_item(explicit_intersection_thumbnail) || !settle_matrix())
    return fail("The explicit intersection thumbnail should receive an actual pointer click");
  const auto intersection_target = matrix_edit->property("selectedEditTarget").toMap();
  if (intersection_target.value(QStringLiteral("scope")).toString() != QStringLiteral("mask") ||
      intersection_target.value(QStringLiteral("layerId")).toString() != adjustment_owner_id ||
      intersection_target.value(QStringLiteral("maskSetId")).toString() != saved_set_id)
    return fail("Clicking the intersection row should select its exact mask ID and adjustment owner");
  QPointer<QQuickItem> intersection_menu_layers_tab(
      find_named_item(matrix_edit, QStringLiteral("layersInspectorTabButton")));
  if (!intersection_menu_layers_tab || !click_matrix_item(intersection_menu_layers_tab) ||
      !wait_for_condition([&] {
        process_layout_events(2);
        return matrix_edit->property("editTool").toString() == QStringLiteral("layers") &&
            matrix_edit->property("editInspectorTab").toString() == QStringLiteral("layers");
      }, 5000) || !settle_matrix())
    return fail("Return to Layers through its tab before opening the intersection row menu");
  const auto before_intersection_remove_recipe = matrix_recipe();
  const auto before_intersection_remove_pixels = matrix_pixels();
  const auto before_intersection_remove_history = editor_preview.editHistoryCurrentIndex();
  auto* explicit_intersection_menu = mask_menu_button(adjustment_owner_id, saved_set_id, true);
  if (!explicit_intersection_menu ||
      !trigger_matrix_menu(explicit_intersection_menu, QStringLiteral("remove-mask")) ||
      !settle_matrix())
    return fail("The explicit intersection row should remove through its real nested More menu");
  const auto after_intersection_remove_recipe = matrix_recipe();
  const auto after_intersection_remove_pixels = matrix_pixels();
  const auto after_intersection_remove_layer = matrix_layer(adjustment_owner_id);
  QStringList after_remove_primary_ids = after_intersection_remove_layer.value(QStringLiteral("selectionMaskSetIds")).toStringList();
  if (after_remove_primary_ids.isEmpty()) {
    for (const auto& value : after_intersection_remove_layer.value(QStringLiteral("selectionMaskSetIds")).toList())
      after_remove_primary_ids.append(value.toString());
  }
  QStringList after_remove_intersection_projection;
  const auto remove_left_primary_only = after_intersection_remove_recipe != before_intersection_remove_recipe &&
      after_remove_primary_ids == QStringList{attached_adjustment_mask_id} &&
      read_adjustment_projection_ids(&after_remove_intersection_projection) &&
      after_remove_intersection_projection == QStringList{attached_adjustment_mask_id} &&
      matrix_mask_present(saved_set_id) &&
      editor_preview.editHistoryCurrentIndex() == before_intersection_remove_history + 1;
  if (!remove_left_primary_only ||
      !undo_redo_matrix_recipe(before_intersection_remove_recipe, before_intersection_remove_pixels,
          after_intersection_remove_recipe, after_intersection_remove_pixels))
    return fail("Removing the intersection should preserve the primary mask and roundtrip exactly through UI Undo/Redo");
  QStringList redone_intersection_projection;
  if (!read_adjustment_projection_ids(&redone_intersection_projection) ||
      redone_intersection_projection != QStringList{attached_adjustment_mask_id} ||
      matrix_recipe() != after_intersection_remove_recipe)
    return fail("Redo should restore the removed state with only the primary mask projection");
  trace_smoke_progress("mask-layer matrix: intersection target and nested Remove roundtrip passed");
  editor_preview.undo();
  QStringList undone_intersection_projection;
  if (!settle_matrix() || matrix_recipe() != before_intersection_remove_recipe ||
      !read_adjustment_projection_ids(&undone_intersection_projection) ||
      undone_intersection_projection != expected_intersection_ids)
    return fail("Undo should restore the seeded two-mask intersection state before fixture cleanup");
  editor_preview.undo();
  if (!settle_matrix() || matrix_recipe() != intersection_baseline_recipe ||
      matrix_pixels() != intersection_baseline_pixels ||
      editor_preview.editHistoryCurrentIndex() != intersection_baseline_history)
    return fail("Intersection proof cleanup should restore the original recipe, pixels and history index");
  QPointer<QQuickItem> intersection_return_layers_tab(
      find_named_item(matrix_edit, QStringLiteral("layersInspectorTabButton")));
  if (!intersection_return_layers_tab || !click_matrix_item(intersection_return_layers_tab) ||
      !wait_for_condition([&] {
        process_layout_events(2);
        return matrix_edit->property("editTool").toString() == QStringLiteral("layers") &&
            matrix_edit->property("editInspectorTab").toString() == QStringLiteral("layers") &&
            !matrix_edit->property("editToolTransitionActive").toBool();
      }, 5000) || !settle_matrix())
    return fail("Intersection proof cleanup should return the real editor to Layers");

  auto* inactive_paint_overlay = find_named_item(matrix_edit, QStringLiteral("photoPaintOverlay"));
  if (!inactive_paint_overlay || item_visible(inactive_paint_overlay))
    return fail("The paint overlay must be hidden after returning to the Layers tool");
  QElapsedTimer final_transition_settle;
  final_transition_settle.start();
  if (!wait_for_condition([&] {
        process_layout_events(2);
        return !matrix_edit->property("editToolTransitionActive").toBool() &&
            final_transition_settle.elapsed() >= matrix_edit->property("studioMotionFast").toInt() + 32;
      }, 3000))
    return fail("The final capture should wait for the tool-switch fade to settle");
  if (!capture_visual_proof(matrix_window, matrix_root, matrix_proof_dir,
          QStringLiteral("mask-layer-matrix-standard-after"), 1440, 900,
          {QStringLiteral("editImageViewport"), QStringLiteral("photoLayersInspectorGroup"),
           QStringLiteral("workingSelectionContextBar"), QStringLiteral("layersInspectorTabButton")}, &matrix_captures))
    return fail("Mask and layer matrix should capture its standard post-interaction workspace");
  const auto matrix_qml_script_errors = matrix_qml_warning_sink->property("scriptErrors").toStringList();
  if (!matrix_qml_script_errors.isEmpty()) {
    std::cerr << "mask-layer-matrix QML script errors:";
    for (const auto& error : matrix_qml_script_errors)
      std::cerr << "\n  " << error.toStdString();
    std::cerr << '\n';
    return fail("Mask and layer interaction matrix should not emit QML runtime, binding-loop or assignment warnings");
  }
  QJsonObject receipt{
      {QStringLiteral("schema"), QStringLiteral("looksy-mask-layer-interaction-matrix:v1")},
      {QStringLiteral("status"), QStringLiteral("passed")},
      {QStringLiteral("input"), QStringLiteral("injected Qt Quick window pointer and key events")},
      {QStringLiteral("physicalNativeInputQualified"), false},
      {QStringLiteral("layerIds"), QJsonObject{{QStringLiteral("red"), red_id},
          {QStringLiteral("blue"), blue_id}, {QStringLiteral("green"), green_id},
          {QStringLiteral("text"), text_id}, {QStringLiteral("maskedAdjustment"), adjustment_owner_id}}},
      {QStringLiteral("maskIds"), QJsonObject{{QStringLiteral("saved"), saved_set_id},
          {QStringLiteral("moved"), red_mask_id}, {QStringLiteral("altCopy"), green_copy_id},
          {QStringLiteral("maskedAdjustmentCopy"), attached_adjustment_mask_id},
          {QStringLiteral("explicitIntersection"), saved_set_id}}},
      {QStringLiteral("cases"), QJsonArray{
          QStringLiteral("row and pixel thumbnail target routing"),
          QStringLiteral("Ctrl and Shift multi-select preserve canonical recipe/history"),
          QStringLiteral("mask thumbnail and pixel thumbnail target switching"),
          QStringLiteral("standalone selection attach menu"),
          QStringLiteral("invert, disable, remove menus with exact undo and redo pixels"),
          QStringLiteral("mask move, Alt-copy, locked target rejection, stale source rejection"),
          QStringLiteral("B/M shortcut suppression while text input has focus"),
          QStringLiteral("selected-layer paint changes only the target canonical layer"),
          QStringLiteral("masked adjustment add creates one undoable layer and independent editable mask copy"),
          QStringLiteral("same-name mask copies show only canonical attachments and preserve standalone ownership through undo and redo"),
          QStringLiteral("actual Exposure pointer drag changes only mask-covered pixels and preserves every other authored field"),
          QStringLiteral("masked adjustment exact Undo/Redo, flattened PNG export and preview parity"),
          QStringLiteral("Escape closes the mask menu, restores More focus and leaves the active tool unchanged"),
          QStringLiteral("primary and explicit intersection attachments expose exact nested IDs, select the intersection target, and remove with exact Undo/Redo"),
          QStringLiteral("fresh controller reloads the exact serialized recipe and exports matching preview pixels"),
          QStringLiteral("QML runtime, binding-loop and assignment warnings are captured and rejected")}},
      {QStringLiteral("captures"), matrix_captures}};
  QFile receipt_file(QDir(matrix_proof_dir).filePath(QStringLiteral("mask-layer-interaction-matrix.json")));
  if (!receipt_file.open(QIODevice::WriteOnly) ||
      receipt_file.write(QJsonDocument(receipt).toJson(QJsonDocument::Indented)) <= 0)
    return fail("Mask and layer interaction matrix should write its verification receipt");
  std::cout << "mask-layer-interaction-matrix=passed; QtQuick-pointer+keyboard; layers=5; linked-mask-menu+transfer+paint+masked-adjustment-undo-redo-export\n";
  return EXIT_SUCCESS;
}

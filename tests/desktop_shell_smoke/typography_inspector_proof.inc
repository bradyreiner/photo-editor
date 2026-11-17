      using photo_editor::desktop::EditorPreviewControllerTestAccess;
      const bool text_perf_only = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_TEXT_PERF_ONLY");
      const bool text_layer_menu_proof = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_LAYER_MENU_PROOF");
      const bool keep_paint_layer_for_text_perf =
          qEnvironmentVariable("PHOTO_EDITOR_SMOKE_TEXT_PERF_KEEP_PAINT_LAYER").trimmed() == QStringLiteral("1");
      bool empty_paint_layer_removed_for_text_perf = false;
      if (text_perf_only && !qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_TEXT_DRAG_PERF") && !text_layer_menu_proof)
        return fail("Text performance-only proof requires a text-drag or layer-menu proof route");
      if (text_perf_only && !keep_paint_layer_for_text_perf) {
        const auto paint_tiles = paint_layer_data.value(QStringLiteral("paintTiles")).toMap();
        if (paint_layer_id.isEmpty() || paint_layer_data.value(QStringLiteral("kind")).toString() != QStringLiteral("pixel") ||
            !paint_tiles.contains(QStringLiteral("tiles")) ||
            !paint_tiles.value(QStringLiteral("tiles")).toMap().isEmpty())
          return fail("Isolated text performance proof can remove only its empty fixture Paint layer");
        if (!editor_preview.deletePhotoLayer(0) || editor_preview.photoLayerCount() != paint_layer_count_before ||
            !wait_for_condition([&] { return !editor_preview.processing() && !editor_preview.fullRenderRunning(); }, 20000))
          return fail("Isolated text performance proof should remove and settle its empty fixture Paint layer before text creation");
        const auto remaining_layers = editor_preview.photoLayers();
        if (std::any_of(remaining_layers.cbegin(), remaining_layers.cend(), [&](const QVariant& value) {
              return value.toMap().value(QStringLiteral("id")).toString() == paint_layer_id;
            }))
          return fail("Isolated text performance proof should remove its fixture Paint layer before creating text");
        studio_window->update();
        if (studio_window->grabWindow().isNull())
          return fail("Isolated text performance proof should render the stack without Paint before text creation");
        empty_paint_layer_removed_for_text_perf = true;
      }
      if (!text_perf_only) {
        // T01/T03/T07: use the production rail, viewport, input, popup and commands.
        const auto before_authoring = editor_preview.currentAssetBoundAdjustmentPatchJson();
        const auto layer_count_before_authoring = editor_preview.photoLayerCount();
        auto* text_rail = find_named_item(studio_root, QStringLiteral("editRailTextButton"));
        auto* authoring_viewport = find_named_item(studio_root, QStringLiteral("editImageViewport"));
        const auto visible_text_item = [&](const QString& name) -> QQuickItem* {
          QList<QQuickItem*> pending{studio_window->contentItem()};
          while (!pending.isEmpty()) {
            auto* item = pending.takeLast();
            if (item_visible(item) && item->objectName() == name) return item;
            pending.append(item->childItems());
          }
          return nullptr;
        };
        const auto type_copy = [&](const QString& copy) {
          QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, copy);
          QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, copy);
          QCoreApplication::sendEvent(studio_window, &press);
          QCoreApplication::sendEvent(studio_window, &release);
          process_layout_events(4);
        };
        // Qt's portable Control modifier maps to the physical Command key on
        // Cocoa, matching Main.primaryShortcut and the production Edit actions.
        const auto primary = Qt::ControlModifier;
        if (!text_rail || !authoring_viewport || !click_studio_item(text_rail) ||
            !wait_for_condition([&] { return edit->property("editTool").toString() == QStringLiteral("text") &&
                !edit->property("editToolTransitionActive").toBool(); }, 5000))
          return fail("Text authoring should start through the visible Text rail control");
        const auto origin = authoring_viewport->mapToScene(QPointF(
            authoring_viewport->property("paintedContentX").toDouble(),
            authoring_viewport->property("paintedContentY").toDouble()));
        const auto point = origin + QPointF(authoring_viewport->property("paintedContentWidth").toDouble() * 0.62,
            authoring_viewport->property("paintedContentHeight").toDouble() * 0.3);
        paint_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
        send_paint_pointer(QEvent::MouseButtonPress, point);
        send_paint_pointer(QEvent::MouseButtonRelease, point);
        auto* canvas_input = visible_text_item(QStringLiteral("photoCanvasTextInput"));
        auto* done = visible_text_item(QStringLiteral("photoTextDoneButton"));
        auto* cancel = visible_text_item(QStringLiteral("photoTextCancelButton"));
        auto* outline = visible_text_item(QStringLiteral("photoTextEditingOutline"));
        auto* session = find_named_object(studio_root, QStringLiteral("photoCanvasTextOverlay"));
        if (!canvas_input || !canvas_input->hasActiveFocus() || !done || !cancel || !outline ||
            outline->property("color").value<QColor>().alpha() != 0 || !session ||
            editor_preview.photoLayerCount() != layer_count_before_authoring ||
            editor_preview.beginAgentTask(QStringLiteral("text-draft-conflict")).value("ok").toBool())
          return fail("Point placement must focus one transparent draft and block conflicting agent execution");
        int done_count = 0, cancel_count = 0, command_group_count = 0;
        QList<QQuickItem*> visible_items{studio_window->contentItem()};
        while (!visible_items.isEmpty()) {
          auto* item = visible_items.takeLast();
          if (item_visible(item)) {
            if (item->objectName() == QStringLiteral("photoTextDoneButton") ||
                item->objectName() == QStringLiteral("photoTextCreationDone")) ++done_count;
            if (item->objectName() == QStringLiteral("photoTextCancelButton") ||
                item->objectName() == QStringLiteral("photoTextCreationCancel")) ++cancel_count;
            if (item->objectName() == QStringLiteral("photoTextEditActions")) ++command_group_count;
          }
          visible_items.append(item->childItems());
        }
        if (done_count != 1 || cancel_count != 1 || command_group_count != 1 ||
            scene_rect(authoring_viewport).intersects(scene_rect(done)) ||
            scene_rect(authoring_viewport).intersects(scene_rect(cancel)))
          return fail("Text must expose exactly one action group outside the photograph");
        if (!wait_for_condition([&] { return !editor_preview.processing() && !editor_preview.fullRenderRunning(); }, 20000))
          return fail("Direct-text pixel proof needs a settled photographic background");
        const auto empty_draft_frame = studio_window->grabWindow();
        const auto empty_caret_scene = canvas_input->mapRectToScene(canvas_input->property("cursorRectangle").toRectF());
        if (empty_draft_frame.isNull()) return fail("The empty draft must expose its native baseline pixels");
        type_copy(QStringLiteral("Direct text"));
        send_window_key(studio_window, Qt::Key_Return);
        type_copy(QStringLiteral("Second line"));
        const auto typed = QStringLiteral("Direct text\nSecond line");
        const auto display_caret = canvas_input->mapRectToScene(canvas_input->property("cursorRectangle").toRectF());
        if (display_caret.height() < 20.0 || display_caret.height() > 80.0)
          return fail("First-use text must provide a readable caret at Fit instead of tiny source-pixel defaults");
        QImage visible_draft_frame;
        int visible_draft_ink = 0;
        const auto fill = canvas_input->property("color").value<QColor>();
        if (!wait_for_condition([&] {
              studio_window->update();
              const auto frame = studio_window->grabWindow();
              if (frame.isNull() || frame.size() != empty_draft_frame.size()) return false;
              const auto scale = static_cast<qreal>(frame.width()) / studio_window->width();
              const auto bounds = canvas_input->mapRectToScene(canvas_input->boundingRect())
                  .intersected(scene_rect(authoring_viewport));
              const QRect pixels(static_cast<int>(std::floor(bounds.left() * scale)), static_cast<int>(std::floor(bounds.top() * scale)),
                  static_cast<int>(std::ceil(bounds.width() * scale)), static_cast<int>(std::ceil(bounds.height() * scale)));
              const auto current_caret = canvas_input->mapRectToScene(canvas_input->property("cursorRectangle").toRectF());
              visible_draft_ink = 0;
              const auto clipped = pixels.intersected(frame.rect());
              for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
                for (int x = clipped.left(); x <= clipped.right(); ++x) {
                  const QPointF logical((x + 0.5) / scale, (y + 0.5) / scale);
                  if (empty_caret_scene.adjusted(-3, -3, 3, 3).contains(logical) ||
                      current_caret.adjusted(-3, -3, 3, 3).contains(logical)) continue;
                  const auto before = empty_draft_frame.pixelColor(x, y);
                  const auto after = frame.pixelColor(x, y);
                  const auto delta = std::abs(before.red() - after.red()) + std::abs(before.green() - after.green()) +
                      std::abs(before.blue() - after.blue());
                  if (delta > 48 && std::abs(after.red() - fill.red()) < 64 &&
                      std::abs(after.green() - fill.green()) < 64 && std::abs(after.blue() - fill.blue()) < 64)
                    ++visible_draft_ink;
                }
              }
              if (visible_draft_ink < 80 * scale * scale) return false;
              visible_draft_frame = frame;
              return true;
            }, 5000))
          return fail("Typed text must produce native visible fill pixels beyond its caret before capture");
        if (!visible_draft_frame.save(QDir(proof_dir).filePath(QStringLiteral("text-authoring-draft.png"))))
          return fail("The visible direct-creation draft must have photographic-window evidence");
        if (canvas_input->property("text").toString() != typed)
          return fail("Canvas Return must insert a plain-text newline");
        auto* draft_font = visible_text_item(QStringLiteral("photoLayerTextFontField"));
        if (!draft_font || !click_studio_item(draft_font) ||
            !wait_for_condition([&] { return visible_text_item(QStringLiteral("photoTypographyFamilyPickerSearch")); }, 3000) ||
            !send_window_key(studio_window, Qt::Key_Escape) ||
            !wait_for_condition([&] { return !visible_text_item(QStringLiteral("photoTypographyFamilyPickerSearch")); }, 3000) ||
            !session->property("editing").toBool() || canvas_input->property("text").toString() != typed)
          return fail("Font-popup Escape must preserve the underlying authoring draft");
        auto* menu_undo = find_named_object(studio_root, QStringLiteral("mainPhotoUndoAction"));
        auto* menu_redo = find_named_object(studio_root, QStringLiteral("mainPhotoRedoAction"));
        if (!menu_undo || !menu_redo || !QMetaObject::invokeMethod(menu_undo, "trigger") ||
            canvas_input->property("text").toString() != QStringLiteral("Direct text\n") ||
            editor_preview.currentAssetBoundAdjustmentPatchJson() != before_authoring ||
            !QMetaObject::invokeMethod(menu_redo, "trigger") || canvas_input->property("text").toString() != typed)
          return fail("The production Edit-menu actions must stay inside text history after font-control focus");
        if (!click_studio_item(done) || !wait_for_condition([&] {
              return !session->property("editing").toBool() &&
                  editor_preview.photoLayerCount() == layer_count_before_authoring + 1;
            }, 5000) || editor_preview.photoLayers().at(0).toMap().value("text").toString() != typed ||
            edit->property("editTool").toString() != QStringLiteral("text"))
          return fail("Done must create one multiline object and retain Text context");
        const auto after_authoring = editor_preview.currentAssetBoundAdjustmentPatchJson();
        if (!send_window_key(studio_window, Qt::Key_Z, primary) ||
            !wait_for_condition([&] { return editor_preview.currentAssetBoundAdjustmentPatchJson() == before_authoring; }, 5000) ||
            !send_window_key(studio_window, Qt::Key_Z, primary | Qt::ShiftModifier) ||
            !wait_for_condition([&] { return editor_preview.currentAssetBoundAdjustmentPatchJson() == after_authoring; }, 5000))
          return fail("Main Undo/Redo must restore the exact pre/post text recipes");
        auto* inspector_copy = visible_text_item(QStringLiteral("photoLayerTextContentField"));
        if (!inspector_copy || !click_studio_item(inspector_copy) ||
            !send_window_key(studio_window, Qt::Key_A, primary))
          return fail("The optional Inspector copy field must enter the same text session");
        type_copy(QStringLiteral("Inspector draft"));
        if (!QMetaObject::invokeMethod(menu_undo, "trigger") || canvas_input->property("text").toString() != typed ||
            !QMetaObject::invokeMethod(menu_redo, "trigger") ||
            canvas_input->property("text").toString() != QStringLiteral("Inspector draft"))
          return fail("Edit-menu Undo/Redo must share Inspector text history without traversing document history");
        if (!session->property("editing").toBool() ||
            canvas_input->property("text").toString() != QStringLiteral("Inspector draft") ||
            !send_window_key(studio_window, Qt::Key_Escape) || session->property("editing").toBool() ||
            editor_preview.currentAssetBoundAdjustmentPatchJson() != after_authoring)
          return fail("Inspector typing shares the draft; authoring Escape restores the complete session baseline");
        if (!send_window_key(studio_window, Qt::Key_Z, primary) ||
            !wait_for_condition([&] { return editor_preview.currentAssetBoundAdjustmentPatchJson() == before_authoring; }, 5000))
          return fail("Cancelled Inspector edits must add no global history");
        const auto authoring_platform = QGuiApplication::platformName();
        const auto authoring_foreground = native_window_foreground_sample(studio_window);
        const auto authoring_exposed = studio_window->isExposed();
        if (authoring_platform == QStringLiteral("cocoa") &&
            (!authoring_exposed || !authoring_foreground.value(QStringLiteral("qualified")).toBool()))
          return fail("Native text authoring requires an exposed foreground-qualified Cocoa window");
        QSaveFile receipt(QDir(proof_dir).filePath(QStringLiteral("text-authoring-workflow.json")));
        const QJsonObject result{{"schema", "looksy-text-authoring-workflow:v1"}, {"status", "passed"},
            {"platform", authoring_platform}, {"windowExposed", authoring_exposed},
            {"foreground", QJsonObject::fromVariantMap(authoring_foreground)},
            {"directCreation", true}, {"multiline", true}, {"oneActionGroup", true},
            {"inspectorSharesDraft", true}, {"popupEscapePreservesDraft", true},
            {"authoringEscapeCancels", true}, {"undoExact", true}, {"redoExact", true},
            {"defaultTextLegible", true}, {"draftCaptured", true}, {"editMenuSharesSession", true},
            {"visibleDraftInkPixels", visible_draft_ink}, {"draftPixelProof", true}};
        if (!receipt.open(QIODevice::WriteOnly) || receipt.write(QJsonDocument(result).toJson()) < 0 || !receipt.commit())
          return fail("Text authoring acceptance must publish its verified receipt");
      }
      const auto typography_canvas_width = editor_preview.sourceImageWidth();
      const auto typography_canvas_height = editor_preview.sourceImageHeight();
      const auto initial_text = text_perf_only
          ? QStringLiteral("CITY LIGHT\nAn evening edition")
          : QStringLiteral("City light\nEvening edition");
      if (!editor_preview.addParagraphTextLayer(initial_text,
              60, 60, 460, 180, typography_canvas_width, typography_canvas_height) ||
          !QMetaObject::invokeMethod(edit, "editPhotoLayerInspectorTarget", Q_ARG(QVariant, QVariant(0))) ||
          !invoke_edit_tool(QStringLiteral("layers")))
        return fail("Typography proof should open the selected text in its real inspector");
      auto* text_editor = find_named_item(studio_root, QStringLiteral("photoLayerTextEditor"));
      auto* content = find_named_item(studio_root, QStringLiteral("photoLayerTextContentField"));
      auto* bold = find_named_item(studio_root, QStringLiteral("photoLayerTextBoldButton"));
      auto* geometry = find_named_item(studio_root, QStringLiteral("photoTypographyGeometryToggle"));
      const auto wait_rendered_frame = [&] {
        const auto rendered = std::make_shared<int>(0);
        const auto connection = QObject::connect(studio_window, &QQuickWindow::frameSwapped, &engine,
            [rendered, studio_window] {
              ++*rendered;
              if (*rendered == 1) studio_window->update();
            }, Qt::QueuedConnection);
        studio_window->update();
        const auto ready = wait_for_condition([&] { return *rendered >= 2; }, 3000);
        QObject::disconnect(connection);
        return ready;
      };
      const auto reveal_control = [&](QQuickItem* item) {
        auto* scroll = find_named_item(studio_root, QStringLiteral("creativeToolsScroll"));
        auto* viewport = scroll ? qobject_cast<QQuickItem*>(scroll->property("contentItem").value<QObject*>()) : nullptr;
        if (!viewport) return false;
        const auto position = item->mapToItem(viewport, QPointF(item->width()/2, item->height()/2));
        const auto maximum = std::max(0.0, viewport->property("contentHeight").toDouble() - viewport->height());
        viewport->setProperty("contentY", std::clamp(viewport->property("contentY").toDouble() +
            position.y() - viewport->height()/2, 0.0, maximum));
        process_layout_events(8);
        return scene_rect(scroll).contains(scene_rect(item));
      };
      if (text_perf_only) {
        const QVariantMap base_style{
            {QStringLiteral("family"), QStringLiteral("Georgia")},
            {QStringLiteral("face"), QString()},
            {QStringLiteral("size"), 48.0},
            {QStringLiteral("weight"), 700},
            {QStringLiteral("italic"), true},
            {QStringLiteral("underline"), true},
            {QStringLiteral("tracking"), 0.0},
            {QStringLiteral("lineHeight"), 1.2},
            {QStringLiteral("baselineOffset"), 0.0},
            {QStringLiteral("color"), QStringLiteral("#ffffffff")},
            {QStringLiteral("mode"), QStringLiteral("paragraph")},
            {QStringLiteral("paragraphSpacing"), 0.0},
            {QStringLiteral("strokeWidth"), 0.0},
            {QStringLiteral("strokeColor"), QStringLiteral("#ff000000")},
            {QStringLiteral("shadowX"), 0.0},
            {QStringLiteral("shadowY"), 0.0},
            {QStringLiteral("shadowColor"), QStringLiteral("#a0000000")},
            {QStringLiteral("axes"), QVariantMap{}}};
        const QVariantList runs{QVariantMap{
            {QStringLiteral("start"), 0},
            {QStringLiteral("length"), 4},
            {QStringLiteral("family"), QStringLiteral("Georgia")},
            {QStringLiteral("color"), QStringLiteral("#ffff347a")},
            {QStringLiteral("strokeWidth"), 2.5},
            {QStringLiteral("strokeColor"), QStringLiteral("#ff102030")}}};
        if (!editor_preview.setPhotoLayerTypography(0, base_style) ||
            !editor_preview.setPhotoLayerTypography(0, {{QStringLiteral("runs"), runs}}))
          return fail("Isolated text performance fixture should persist the same v10 mixed-format run as the inspector proof");
        studio_window->resize(1440, 900);
        process_layout_events(8);
        auto* layers_group = find_named_item(studio_root, QStringLiteral("photoLayersInspectorGroup"));
        auto* text_row = find_named_item(studio_root, QStringLiteral("photoLayerRow"));
        auto* viewport = find_named_item(studio_root, QStringLiteral("editImageViewport"));
        if (!layers_group || !text_row || !viewport ||
            !wait_for_condition([&] {
              return item_visible(layers_group) && item_visible(text_row) && viewport->width() > 0.0 &&
                  viewport->height() > 0.0 && !editor_preview.processing() && !editor_preview.fullRenderRunning();
            }, 20000) || !wait_rendered_frame() || studio_window->grabWindow().isNull())
          return fail("Isolated text performance fixture should settle the real Layers inspector and 1440x900 viewport");
        const auto performance_style = editor_preview.photoLayerTypography(0);
        const auto performance_runs = performance_style.value(QStringLiteral("runs")).toList();
        if (editor_preview.photoLayers().at(0).toMap().value(QStringLiteral("text")).toString() !=
                QStringLiteral("CITY LIGHT\nAn evening edition") ||
            performance_style.value(QStringLiteral("family")).toString() != QStringLiteral("Georgia") ||
            performance_style.value(QStringLiteral("size")).toDouble() != 48.0 ||
            performance_style.value(QStringLiteral("weight")).toInt() != 700 ||
            performance_style.value(QStringLiteral("version")).toInt() != 10 ||
            performance_style.value(QStringLiteral("color")).toString() != QStringLiteral("#ffffffff") ||
            !performance_style.value(QStringLiteral("italic")).toBool() ||
            !performance_style.value(QStringLiteral("underline")).toBool() || performance_runs.size() != 1 ||
            performance_runs.front().toMap().value(QStringLiteral("start")).toInt() != 0 ||
            performance_runs.front().toMap().value(QStringLiteral("length")).toInt() != 4 ||
            performance_runs.front().toMap().value(QStringLiteral("family")).toString() != QStringLiteral("Georgia") ||
            performance_runs.front().toMap().value(QStringLiteral("color")).toString() != QStringLiteral("#ffff347a") ||
            performance_runs.front().toMap().value(QStringLiteral("strokeWidth")).toDouble() != 2.5 ||
            performance_runs.front().toMap().value(QStringLiteral("strokeColor")).toString() != QStringLiteral("#ff102030"))
          return fail("Isolated text performance fixture must match the inspected Georgia mixed-format typography");
      } else {
        if (!text_editor || !content || !bold || !geometry ||
            !wait_for_condition([&] { return item_visible(content) && item_visible(bold); }, 5000))
          return fail("Typography proof requires visible multiline copy and primary format controls");
        if (!click_studio_item(content) || !QMetaObject::invokeMethod(content, "forceInputFocus"))
          return fail("Typography copy should accept focus");
        content->setProperty("text", QStringLiteral("CITY LIGHT\nAn evening edition"));
        QMetaObject::invokeMethod(content, "clearInputFocus");
        if (editor_preview.photoLayers().at(0).toMap().value("text").toString() !=
            QStringLiteral("CITY LIGHT\nAn evening edition"))
          return fail("Inspector copy should persist both lines");
        const auto base_style_before_selection = editor_preview.photoLayerTypography(0);
        auto* selection_notice = find_named_item(studio_root, QStringLiteral("photoTypographySelectionNotice"));
        auto* whole_layer_scope = find_named_item(studio_root, QStringLiteral("photoTypographyWholeLayerScopeButton"));
        auto* fill_color = find_named_item(studio_root, QStringLiteral("photoLayerTextColorField"));
        auto* selection_stroke_width = find_named_item(studio_root, QStringLiteral("photoTypographySelectionStrokeWidth"));
        auto* selection_stroke_color = find_named_item(studio_root, QStringLiteral("photoTypographySelectionStrokeColor"));
        if (!selection_notice || !whole_layer_scope || !fill_color ||
            !selection_stroke_width || !selection_stroke_color ||
            !QMetaObject::invokeMethod(content, "select", Q_ARG(int, 0), Q_ARG(int, 4)) ||
            !wait_for_condition([&] {
              return content->property("selectionStart").toInt() == 0 &&
                  content->property("selectionEnd").toInt() == 4 &&
                  selection_notice->property("visible").toBool();
            }, 3000))
          return fail("Real inspector text selection should expose the selected-word formatting scope");
        auto* font_trigger = find_named_item(studio_root, QStringLiteral("photoLayerTextFontField"));
        auto* font_search = find_named_item(studio_root, QStringLiteral("photoTypographyFamilyPickerSearch"));
        auto* font_popup = find_named_object(studio_root, QStringLiteral("photoTypographyFamilyPickerPopup"));
        if (!font_trigger || !font_search || !font_popup || !click_studio_item(font_trigger) ||
            !wait_for_condition([&] { return font_search->hasActiveFocus(); }, 3000))
          return fail("Selected-word font control should open while the inspector selection remains active");
        QKeyEvent selected_search_font(QEvent::KeyPress, Qt::Key_G, Qt::NoModifier, QStringLiteral("Georgia"));
        QCoreApplication::sendEvent(studio_window, &selected_search_font);
        process_layout_events(8);
        QKeyEvent selected_choose_font(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(studio_window, &selected_choose_font);
        if (!wait_for_condition([&] {
              return content->property("selectionStart").toInt() == 0 &&
                  content->property("selectionEnd").toInt() == 4 &&
                  editor_preview.photoLayerTypography(0).value("runs").toList().size() == 1 &&
                  !font_popup->property("opened").toBool() && !font_popup->property("visible").toBool();
            }, 5000))
          return fail("Choosing a font must retain the selected word, write one persisted run, and finish closing the picker popup");
        const auto set_selected_field = [&](QObject* field, const QString& value) {
          field->setProperty("text", value);
          return QMetaObject::invokeMethod(field, "editingFinished");
        };
        if (!set_selected_field(fill_color, QStringLiteral("#ffff347a")) ||
            !set_selected_field(selection_stroke_width, QStringLiteral("2.5")) ||
            !set_selected_field(selection_stroke_color, QStringLiteral("#ff102030")))
          return fail("Selected-word fill and outline controls should apply through the visible Inspector fields");
        process_layout_events(8);
        if (!wait_rendered_frame())
          return fail("Selected-word style changes should settle through a rendered Inspector frame before the scope control is clicked");
        const auto selected_style = editor_preview.photoLayerTypography(0);
        const auto selected_runs = selected_style.value("runs").toList();
        if (content->property("selectionStart").toInt() != 0 ||
            content->property("selectionEnd").toInt() != 4 || selected_runs.size() != 1 ||
            selected_runs.front().toMap().value("start").toInt() != 0 ||
            selected_runs.front().toMap().value("length").toInt() != 4 ||
            selected_runs.front().toMap().value("family").toString() != QStringLiteral("Georgia") ||
            selected_runs.front().toMap().value("color").toString() != QStringLiteral("#ffff347a") ||
            selected_runs.front().toMap().value("strokeWidth").toDouble() != 2.5 ||
            selected_runs.front().toMap().value("strokeColor").toString() != QStringLiteral("#ff102030") ||
            selected_style.value("family") != base_style_before_selection.value("family") ||
            selected_style.value("color") != base_style_before_selection.value("color"))
          return fail("Font, fill, and outline must affect only the selected word and leave the layer base unchanged");
        auto* scope_scroll = find_named_item(studio_root, QStringLiteral("creativeToolsScroll"));
        auto* scope_panel = whole_layer_scope;
        while (scope_panel && scope_panel->metaObject()->indexOfProperty("forceWholeLayerScope") < 0)
          scope_panel = scope_panel->parentItem();
        const auto report_scope_failure = [&] {
          studio_window->grabWindow().save(QDir(proof_dir).filePath(QStringLiteral("scope-failure.png")));
          qWarning() << "Typography whole-layer scope click failed"
                     << "buttonRect=" << scene_rect(whole_layer_scope)
                     << "selection=" << content->property("selectionStart").toInt()
                     << content->property("selectionEnd").toInt()
                     << "noticeVisible=" << selection_notice->property("visible").toBool()
                     << "buttonText=" << whole_layer_scope->property("text").toString()
                     << "forceWholeLayerScope=" << (scope_panel && scope_panel->property("forceWholeLayerScope").toBool())
                     << "fontPopupOpened=" << font_popup->property("opened").toBool()
                     << "fontPopupVisible=" << font_popup->property("visible").toBool()
                     << "scrollRect=" << scene_rect(scope_scroll)
                     << "scrollVisible=" << item_visible(scope_scroll);
        };
        if (!reveal_control(whole_layer_scope) ||
            !scene_rect(scope_scroll).contains(scene_rect(whole_layer_scope)) ||
            !click_studio_item(whole_layer_scope) ||
            !wait_for_condition([&] { return whole_layer_scope->property("text").toString() == QStringLiteral("Selection"); }, 3000)) {
          report_scope_failure();
          return fail("The visible scope control should switch to whole-layer formatting without losing the saved run");
        }
        if (!QMetaObject::invokeMethod(content, "deselect") ||
            !wait_for_condition([&] { return !selection_notice->property("visible").toBool(); }, 3000))
          return fail("Clearing the text selection should restore whole-layer control behavior");
        if (!click_studio_item(bold) || !wait_for_condition([&] {
              const auto style = editor_preview.photoLayerTypography(0);
              const auto runs = style.value("runs").toList();
              return editor_preview.photoLayers().at(0).toMap().value("fontWeight").toString() == "bold" &&
                  runs.size() == 1 && runs.front().toMap().value("start").toInt() == 0 &&
                  runs.front().toMap().value("length").toInt() == 4 &&
                  runs.front().toMap().value("family").toString() == QStringLiteral("Georgia");
            }, 3000))
          return fail("Whole-layer Bold should update the base while preserving the selected word's font, fill, and outline run");
        for (const auto& name : {QStringLiteral("photoTypographyItalicButton"),
                                QStringLiteral("photoTypographyUnderlineButton")}) {
          auto* button = find_named_item(studio_root, name);
          if (!button || !click_studio_item(button) || !wait_for_condition([&] {
                return button->property("active").toBool();
              }, 3000))
            return fail("Primary italic and underline controls should apply without opening advanced sections");
        }
        if (!font_trigger || !font_search || !click_studio_item(font_trigger) ||
            !wait_for_condition([&] { return font_search->hasActiveFocus(); }, 3000))
          return fail("The real inspector font picker should focus its search field");
        QKeyEvent search_font(QEvent::KeyPress, Qt::Key_G, Qt::NoModifier, QStringLiteral("Georgia"));
        QCoreApplication::sendEvent(studio_window, &search_font);
        process_layout_events(8);
        if (!studio_window->grabWindow().save(QDir(proof_dir).filePath("typography-font-search.png")))
          return fail("The font search should render previews");
        QKeyEvent choose_font(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(studio_window, &choose_font);
        if (!wait_for_condition([&] {
              return editor_preview.photoLayerTypography(0).value("family").toString() == "Georgia" &&
                  !font_popup->property("opened").toBool() && !font_popup->property("visible").toBool();
            }, 5000))
          return fail("Keyboard font selection should update the base style and finish closing the picker popup");
      }
      if (!text_perf_only) {
        auto* finish_text = find_named_item(studio_root, QStringLiteral("photoTextDoneButton"));
        if (!finish_text || !click_studio_item(finish_text))
          return fail("Inspector typography changes should finish through the shared text Done action");
      }
      const auto capture = [&](const QString& name, QSize size) {
        return capture_visual_proof(studio_window, studio_root, proof_dir, name, size.width(), size.height(),
            {QStringLiteral("creativeToolsSurface"), QStringLiteral("photoLayerTextEditor"),
             QStringLiteral("photoLayerTextContentField"), QStringLiteral("photoLayerTextBoldButton")}, &studio_manifest);
      };
      const auto run_text_layer_menu_proof = [&] {
        auto* more_button = find_named_item(studio_root, QStringLiteral("photoLayerRowMoreMenuButton"));
        auto* action_menu = find_named_object(studio_root, QStringLiteral("photoLayerRowActionsPopup"));
        if (!more_button || !action_menu || editor_preview.photoLayers().isEmpty())
          return false;
        const auto original_layer = editor_preview.photoLayers().at(0).toMap();
        const bool initially_locked = original_layer.value(QStringLiteral("locked")).toBool();
        const auto original_style = editor_preview.photoLayerTypography(0);
        const auto original_recipe = editor_preview.currentAssetBoundAdjustmentPatchJson();
        const auto visible_menu_item = [&](const QString& object_name) -> QQuickItem* {
          QList<QQuickItem*> pending{studio_window->contentItem()};
          while (!pending.isEmpty()) {
            auto* item = pending.takeLast();
            if (item_visible(item) && item->objectName() == object_name)
              return item;
            pending.append(item->childItems());
          }
          return nullptr;
        };
        const auto open_menu_and_check_lock = [&](bool locked) {
          const auto expected_label = locked ? QStringLiteral("Unlock Layer") : QStringLiteral("Lock Layer");
          return click_studio_item(more_button) && wait_for_condition([&] {
            auto* lock_item = visible_menu_item(QStringLiteral("layerAction_toggle-lock"));
            return action_menu->property("visible").toBool() && lock_item &&
                lock_item->property("text").toString() == expected_label &&
                lock_item->property("enabled").toBool() &&
                lock_item->property("checkable").toBool() &&
                lock_item->property("checked").toBool() == locked;
          }, 5000);
        };
        const auto close_menu_with_escape = [&] {
          return send_window_key(studio_window, Qt::Key_Escape) && wait_for_condition([&] {
            return !action_menu->property("visible").toBool();
          }, 3000);
        };
        const auto click_lock_item = [&](bool locked) {
          auto* lock_item = visible_menu_item(QStringLiteral("layerAction_toggle-lock"));
          if (!lock_item || !click_studio_item(lock_item))
            return false;
          return wait_for_condition([&] {
            const auto layers = editor_preview.photoLayers();
            return layers.size() > 0 &&
                layers.at(0).toMap().value(QStringLiteral("locked")).toBool() == locked &&
                !action_menu->property("visible").toBool();
          }, 5000);
        };

        if (!item_visible(more_button) || !open_menu_and_check_lock(initially_locked) ||
            !close_menu_with_escape())
          return false;
        more_button->forceActiveFocus();
        const auto initial_lock_label = initially_locked ? QStringLiteral("Unlock Layer") : QStringLiteral("Lock Layer");
        if (!wait_for_condition([&] { return more_button->hasActiveFocus(); }, 3000) ||
            !send_window_key(studio_window, Qt::Key_Space) || !wait_for_condition([&] {
              auto* lock_item = visible_menu_item(QStringLiteral("layerAction_toggle-lock"));
              return action_menu->property("visible").toBool() && lock_item &&
                  lock_item->property("text").toString() == initial_lock_label &&
                  lock_item->property("enabled").toBool() &&
                  lock_item->property("checked").toBool() == initially_locked;
            }, 3000) || !close_menu_with_escape() || !open_menu_and_check_lock(initially_locked) ||
            !click_lock_item(!initially_locked) || !open_menu_and_check_lock(!initially_locked) ||
            !click_lock_item(initially_locked) || !open_menu_and_check_lock(initially_locked) ||
            !close_menu_with_escape())
          return false;
        const bool recipe_restored = editor_preview.photoLayers().at(0).toMap()
                .value(QStringLiteral("locked")).toBool() == initially_locked &&
            editor_preview.photoLayerTypography(0) == original_style &&
            editor_preview.currentAssetBoundAdjustmentPatchJson() == original_recipe;
        if (recipe_restored)
          std::cout << "Text layer menu proof: pointer and keyboard open, fresh lock label/check state, exact recipe restore\n";
        return recipe_restored;
      };
      if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_TEXT_DRAG_PERF")) {
        const auto label = qEnvironmentVariable("PHOTO_EDITOR_SMOKE_TEXT_DRAG_PERF_LABEL");
        if (label != "baseline" && label != "candidate")
          return fail("Text-drag performance proof requires PHOTO_EDITOR_SMOKE_TEXT_DRAG_PERF_LABEL=baseline or candidate");
        auto* transform_overlay = find_named_item(studio_root, QStringLiteral("photoLayerTransformOverlay"));
        auto* move_handle = find_named_item(studio_root, QStringLiteral("photoLayerTransformMoveHandle"));
        activate_native_test_window(studio_window);
        if (!transform_overlay || !move_handle || !item_visible(move_handle) ||
            !wait_for_condition([&] { return !editor_preview.processing() && !editor_preview.fullRenderRunning(); }, 20000) ||
            !wait_rendered_frame())
          return fail("Text-drag performance proof requires a settled visible canvas move handle");
        auto* image_viewport = find_named_item(studio_root, QStringLiteral("editImageViewport"));
        const auto render_configuration = QJsonObject::fromVariantMap(
            EditorPreviewControllerTestAccess::render_configuration(editor_preview));
        if (!image_viewport || image_viewport->width() <= 0.0 || image_viewport->height() <= 0.0 ||
            render_configuration.value(QStringLiteral("actual_photographic_rendering_model")).toString().isEmpty() ||
            render_configuration.value(QStringLiteral("working_color_profile")).toString().isEmpty())
          return fail("Text-drag performance proof must record the loaded rendering model and viewport dimensions");

        photo_editor::desktop::observability::initialize();
        const auto send_perf_pointer = [&](QEvent::Type type, const QPointF& point) {
          const auto global = QPointF(studio_window->mapToGlobal(point.toPoint()));
          QMouseEvent event(type, point, global,
              type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
              type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
              Qt::NoModifier);
          event.setTimestamp(paint_pointer_timestamp);
          paint_pointer_timestamp += 16;
          QCoreApplication::sendEvent(studio_window, &event);
          QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        };
        const auto source_path = qEnvironmentVariable("PHOTO_EDITOR_STUDIO_PHOTO_SOURCE");
        QFile source_file(source_path.isEmpty() ? photo_path : source_path);
        if (!source_file.open(QIODevice::ReadOnly))
          return fail("Text-drag performance proof could not read its fixed source fixture");
        const auto source_sha256 = QCryptographicHash::hash(source_file.readAll(), QCryptographicHash::Sha256).toHex();
        const auto text_layout_metrics = [](const QVariantMap& layer) {
          QJsonObject metrics;
          const auto width = qBound(16, layer.value(QStringLiteral("pixelWidth"), 512).toInt(), 8192);
          const auto height = qBound(16, layer.value(QStringLiteral("pixelHeight"), 128).toInt(), 8192);
          const auto text = layer.value(QStringLiteral("text"), QStringLiteral("Text")).toString();
          QString style_error;
          const auto style = photo_editor::desktop::decode_photo_text_style(
              QJsonObject::fromVariantMap(layer.value(QStringLiteral("textTypography")).toMap()), &style_error);
          if (!style) {
            metrics.insert(QStringLiteral("valid"), false);
            metrics.insert(QStringLiteral("error"), style_error);
            return metrics;
          }
          const auto layout = photo_editor::desktop::rasterize_photo_text(text, *style,
              QSizeF(width, height), QSize(width, height),
              photo_editor::desktop::legacy_photo_text_alignment(layer), 0,
              style->point_text ? photo_editor::desktop::photo_text_origin(layer) : QPointF{}, true);
          metrics.insert(QStringLiteral("valid"), layout.error.isEmpty());
          metrics.insert(QStringLiteral("error"), layout.error);
          metrics.insert(QStringLiteral("line_count"), layout.line_count);
          metrics.insert(QStringLiteral("content_height"), layout.content_height);
          metrics.insert(QStringLiteral("content_width"), layout.content_width);
          metrics.insert(QStringLiteral("overflow"), layout.overflow);
          return metrics;
        };

        const auto layer_before = editor_preview.photoLayers().at(0).toMap();
        const auto style_before = editor_preview.photoLayerTypography(0);
        const auto recipe_before = EditorPreviewControllerTestAccess::recipe_json(editor_preview);
        const auto recipe_before_hash = QCryptographicHash::hash(recipe_before.toUtf8(), QCryptographicHash::Sha256).toHex();
        const auto start = scene_rect(move_handle).center();
        const auto overlay_opacity = transform_overlay->opacity();
        const auto capture_canvas_without_guides = [&] {
          transform_overlay->setOpacity(0.0);
          studio_window->update();
          const auto frame = studio_window->grabWindow();
          transform_overlay->setOpacity(overlay_opacity);
          if (frame.isNull() || !tool_viewport) return QImage{};
          const auto bounds = scene_rect(tool_viewport);
          const qreal factor = static_cast<qreal>(frame.width()) / studio_window->width();
          const QRect crop(qRound(bounds.x() * factor), qRound(bounds.y() * factor),
              qRound(bounds.width() * factor), qRound(bounds.height() * factor));
          return frame.copy(crop.intersected(frame.rect()));
        };
        const auto canvas_before = capture_canvas_without_guides();
        if (canvas_before.isNull()) return fail("Text-drag performance proof could not capture its baseline canvas");
        const auto native_window_before = native_window_foreground_sample(studio_window);

        struct FrameSignalSamples final {
          QElapsedTimer timer;
          QMutex mutex;
          QVector<qint64> timestamps_ms;
        };
        // frameSwapped is emitted from Qt Quick's scene graph render thread.
        // Capture only a shared, mutex-protected timestamp sink there; keeping
        // it shared also protects an in-flight direct callback after disconnect.
        const auto frame_samples = std::make_shared<FrameSignalSamples>();
        auto& gesture_timer = frame_samples->timer;
        QJsonArray preview_updates;
        QJsonArray native_revision_updates;
        QJsonArray render_results;
        QSet<int> render_generations_recorded;
        const auto initial_native_revision = editor_preview.displayPreviewNativeRevision();
        const auto initial_render_generation = EditorPreviewControllerTestAccess::render_generation(editor_preview);
        auto last_native_revision = initial_native_revision;
        const auto initial_provider_image = editor_preview_image_provider->image(QStringLiteral("edited"));
        const auto initial_provider_cache_key = initial_provider_image.isNull()
            ? QStringLiteral("null") : QString::number(initial_provider_image.cacheKey());
        gesture_timer.start();
        const auto frame_connection = QObject::connect(studio_window, &QQuickWindow::frameSwapped, &engine,
            [frame_samples] {
              const auto at_ms = frame_samples->timer.nsecsElapsed() / 1000000;
              frame_samples->mutex.lock();
              frame_samples->timestamps_ms.append(at_ms);
              frame_samples->mutex.unlock();
            }, Qt::DirectConnection);
        const auto preview_connection = QObject::connect(&editor_preview, &photo_editor::desktop::EditorPreviewController::previewChanged,
            &engine, [&] {
              const auto at_ms = gesture_timer.nsecsElapsed() / 1000000.0;
              const auto native_revision = editor_preview.displayPreviewNativeRevision();
              const auto preview = editor_preview_image_provider->image(QStringLiteral("edited"));
              preview_updates.append(QJsonObject{
                  {QStringLiteral("at_ms"), at_ms},
                  {QStringLiteral("native_revision"), native_revision},
                  {QStringLiteral("provider_cache_key"), preview.isNull()
                      ? QStringLiteral("null") : QString::number(preview.cacheKey())}});
              if (native_revision > last_native_revision) {
                native_revision_updates.append(QJsonObject{
                    {QStringLiteral("at_ms"), at_ms},
                    {QStringLiteral("from_revision"), last_native_revision},
                    {QStringLiteral("to_revision"), native_revision},
                    {QStringLiteral("advance_count"), native_revision - last_native_revision}});
                last_native_revision = native_revision;
              }
              const auto ready_result = EditorPreviewControllerTestAccess::ready_interactive_render_result(editor_preview);
              if (ready_result && ready_result->generation > initial_render_generation &&
                  !render_generations_recorded.contains(ready_result->generation)) {
                render_generations_recorded.insert(ready_result->generation);
                render_results.append(QJsonObject{
                    {QStringLiteral("observed_at_ms"), at_ms},
                    {QStringLiteral("generation"), ready_result->generation},
                    {QStringLiteral("edit_transaction_generation"),
                     QString::number(static_cast<qulonglong>(ready_result->edit_transaction_generation))},
                    {QStringLiteral("async_job_generation"), ready_result->async_job.generation},
                    {QStringLiteral("backend"), ready_result->backend},
                    {QStringLiteral("max_edge"), ready_result->max_edge},
                    {QStringLiteral("output_width"), ready_result->output_width},
                    {QStringLiteral("output_height"), ready_result->output_height},
                    {QStringLiteral("full_resolution"), ready_result->full_resolution},
                    {QStringLiteral("draft_interactive"), ready_result->draft_interactive},
                    {QStringLiteral("render_reason"), ready_result->render_reason},
                    {QStringLiteral("render_mode"), ready_result->render_mode},
                    {QStringLiteral("interaction_mode"), ready_result->interaction_mode},
                    {QStringLiteral("completed_render_stages"), QJsonArray::fromStringList(ready_result->completed_render_stages)},
                    {QStringLiteral("fallback_reason"), ready_result->fallback_reason},
                    {QStringLiteral("failed_render_stage"), ready_result->failed_render_stage},
                    {QStringLiteral("failure_stage"), ready_result->failure_stage},
                    {QStringLiteral("failure_detail"), ready_result->failure_detail},
                    {QStringLiteral("total_elapsed_ms"), ready_result->total_elapsed_ms},
                    {QStringLiteral("output_elapsed_ms"), ready_result->adjustment_output_elapsed_ms},
                    {QStringLiteral("output_status"), ready_result->output_lut.status},
                    {QStringLiteral("output_lut_kernel_dispatch_count"), ready_result->output_lut.kernel_dispatch_count},
                    {QStringLiteral("adjustment_gpu_kernel_dispatch_count"), ready_result->adjustment_gpu_kernel_dispatch_count},
                    {QStringLiteral("adjustment_gpu_command_buffer_count"), ready_result->adjustment_gpu_command_buffer_count},
                    {QStringLiteral("adjustment_gpu_blit_count"), ready_result->adjustment_gpu_blit_count},
                    {QStringLiteral("adjustment_gpu_cpu_to_gpu_bytes"), ready_result->adjustment_gpu_cpu_to_gpu_bytes},
                    {QStringLiteral("adjustment_gpu_gpu_to_cpu_bytes"), ready_result->adjustment_gpu_gpu_to_cpu_bytes},
                    {QStringLiteral("adjustment_gpu_spatial_texture_blur_dispatch_count"), ready_result->adjustment_gpu_spatial_texture_blur_dispatch_count},
                    {QStringLiteral("adjustment_gpu_spatial_clarity_blur_dispatch_count"), ready_result->adjustment_gpu_spatial_clarity_blur_dispatch_count},
                    {QStringLiteral("adjustment_gpu_spatial_haze_blur_dispatch_count"), ready_result->adjustment_gpu_spatial_haze_blur_dispatch_count},
                    {QStringLiteral("gpu_to_cpu_bytes"), ready_result->output_lut.gpu_to_cpu_bytes},
                    {QStringLiteral("cpu_display_image"), !ready_result->preview_image.isNull()},
                    {QStringLiteral("native_surface"), ready_result->texture_upload_surface != nullptr},
                    {QStringLiteral("queue_wait_ms"), ready_result->queue_wait_ms},
                    {QStringLiteral("render_plan_overhead_ms"), ready_result->render_plan_overhead_ms},
                    {QStringLiteral("in_flight_render_stage_elapsed_ms"), ready_result->in_flight_render_stage_elapsed_ms},
                    {QStringLiteral("render_executor_cache_bypassed"), ready_result->render_executor_cache_bypassed},
                    {QStringLiteral("render_executor_cache_writes_skipped"), ready_result->render_executor_cache_writes_skipped},
                    {QStringLiteral("render_stage_elapsed_ms"),
                     QJsonObject::fromVariantMap(ready_result->render_stage_elapsed_ms)},
                    {QStringLiteral("render_stage_backends"),
                     QJsonObject::fromVariantMap(ready_result->render_stage_backends)},
                    {QStringLiteral("render_stage_cache_hits"),
                     QJsonObject::fromVariantMap(ready_result->render_stage_cache_hits)}});
              }
            }, Qt::DirectConnection);

        paint_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
        send_perf_pointer(QEvent::MouseButtonPress, start);
        QJsonArray dispatched_moves;
        const QPointF drag_delta(84.0, 42.0);
        constexpr int move_count = 60;
        constexpr qint64 cadence_ns = 16000000;
        for (int step = 1; step <= move_count; ++step) {
          const auto target_ns = cadence_ns * step;
          while (gesture_timer.nsecsElapsed() < target_ns) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
            QThread::msleep(1);
          }
          const auto dispatch_start_ms = gesture_timer.nsecsElapsed() / 1000000.0;
          const auto point = start + drag_delta * (static_cast<qreal>(step) / move_count);
          send_perf_pointer(QEvent::MouseMove, point);
          dispatched_moves.append(QJsonObject{
              {QStringLiteral("step"), step},
              {QStringLiteral("target_ms"), static_cast<double>(target_ns) / 1000000.0},
              {QStringLiteral("dispatch_start_ms"), dispatch_start_ms},
              {QStringLiteral("dispatch_end_ms"), gesture_timer.nsecsElapsed() / 1000000.0},
              {QStringLiteral("photo_layer_stack_performance"),
               QJsonObject::fromVariantMap(
                   EditorPreviewControllerTestAccess::photo_layer_stack_performance(editor_preview))}});
        }

        // Keep the native button held while the asynchronous draft render has
        // a chance to publish. The screenshot below reads the presented canvas,
        // not the moving transform outline.
        const auto final_move_ms = gesture_timer.nsecsElapsed() / 1000000.0;
        const auto minimum_hold_ms = final_move_ms + 1000.0;
        const auto hold_deadline_ms = final_move_ms + 2500.0;
        while (gesture_timer.nsecsElapsed() / 1000000.0 < hold_deadline_ms &&
               (gesture_timer.nsecsElapsed() / 1000000.0 < minimum_hold_ms ||
                (editor_preview.displayPreviewNativeRevision() == initial_native_revision && preview_updates.isEmpty()))) {
          QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
          QThread::msleep(2);
        }
        const auto held_canvas = capture_canvas_without_guides();
        const auto held_capture_ms = gesture_timer.nsecsElapsed() / 1000000.0;
        const auto native_revision_at_capture = editor_preview.displayPreviewNativeRevision();
        const auto native_window_held = native_window_foreground_sample(studio_window);
        const auto held_capture_path = QDir(proof_dir).filePath(QStringLiteral("text-drag-%1-held.png").arg(label));
        const bool held_capture_saved = !held_canvas.isNull() && held_canvas.save(held_capture_path);
        send_perf_pointer(QEvent::MouseButtonRelease, start + drag_delta);
        const auto interaction_finished = wait_for_condition([&] {
          return !transform_overlay->property("interactionActive").toBool() &&
              !EditorPreviewControllerTestAccess::edit_transaction_active(editor_preview);
        }, 3000);
        const auto recipe_after = EditorPreviewControllerTestAccess::recipe_json(editor_preview);
        const auto recipe_after_hash = QCryptographicHash::hash(recipe_after.toUtf8(), QCryptographicHash::Sha256).toHex();
        const auto layer_after = editor_preview.photoLayers().at(0).toMap();
        const auto revision_after_release = editor_preview.displayPreviewNativeRevision();
        QObject::disconnect(preview_connection);
        QObject::disconnect(frame_connection);
        QVector<qint64> captured_frame_signal_ms;
        frame_samples->mutex.lock();
        captured_frame_signal_ms = frame_samples->timestamps_ms;
        frame_samples->mutex.unlock();
        const bool final_render_settled = wait_for_condition([&] {
          return !editor_preview.processing() && !editor_preview.fullRenderRunning();
        }, 20000) && wait_rendered_frame();
        const auto final_native_size = editor_preview.displayPreviewNativeSize();
        const auto final_native_revision = editor_preview.displayPreviewNativeRevision();
        const auto final_canvas = final_render_settled ? capture_canvas_without_guides() : QImage{};
        const bool final_capture_saved = !final_canvas.isNull() && final_canvas.save(
            QDir(proof_dir).filePath(QStringLiteral("text-drag-%1-final.png").arg(label)));

        int changed_canvas_pixels = -1;
        if (!canvas_before.isNull() && !held_canvas.isNull() && canvas_before.size() == held_canvas.size()) {
          changed_canvas_pixels = 0;
          for (int y = 0; y < canvas_before.height(); ++y) {
            const auto* before_line = reinterpret_cast<const QRgb*>(canvas_before.constScanLine(y));
            const auto* held_line = reinterpret_cast<const QRgb*>(held_canvas.constScanLine(y));
            for (int x = 0; x < canvas_before.width(); ++x)
              if (before_line[x] != held_line[x]) ++changed_canvas_pixels;
          }
        }

        const auto recipe_after_hash_text = QString::fromLatin1(recipe_after_hash);
        editor_preview.undo();
        const auto undo_exact = EditorPreviewControllerTestAccess::recipe_json(editor_preview) == recipe_before;
        const bool undo_settled = wait_for_condition([&] {
          return !editor_preview.processing() && !editor_preview.fullRenderRunning();
        }, 20000);
        editor_preview.redo();
        const bool redo_exact = EditorPreviewControllerTestAccess::recipe_json(editor_preview) == recipe_after;
        editor_preview.undo();
        const bool undo_again_exact = EditorPreviewControllerTestAccess::recipe_json(editor_preview) == recipe_before;
        const bool move_changed_layer = layer_after.value(QStringLiteral("x")) != layer_before.value(QStringLiteral("x")) ||
            layer_after.value(QStringLiteral("y")) != layer_before.value(QStringLiteral("y"));

        const auto run_held_resize_probe = [&](const QString& kind, const QString& handle_name,
                                               const QPointF& drag_delta) {
          activate_native_test_window(studio_window);
          auto* handle = find_named_item(studio_root, handle_name);
          if (!handle || !item_visible(handle) ||
              !wait_for_condition([&] {
                return !editor_preview.processing() && !editor_preview.fullRenderRunning() &&
                    !transform_overlay->property("interactionActive").toBool() &&
                    !EditorPreviewControllerTestAccess::edit_transaction_active(editor_preview);
              }, 20000) || !wait_rendered_frame())
            return false;

          const auto resize_layer_before = editor_preview.photoLayers().at(0).toMap();
          const auto resize_style_before = editor_preview.photoLayerTypography(0);
          const auto resize_layout_before = text_layout_metrics(resize_layer_before);
          const auto resize_recipe_before = EditorPreviewControllerTestAccess::recipe_json(editor_preview);
          const auto resize_recipe_before_hash = QCryptographicHash::hash(
              resize_recipe_before.toUtf8(), QCryptographicHash::Sha256).toHex();
          const auto resize_canvas_before = capture_canvas_without_guides();
          if (resize_canvas_before.isNull()) return false;

          const auto resize_start = scene_rect(handle).center();
          const auto resize_native_before = native_window_foreground_sample(studio_window);
          const auto resize_initial_native_revision = editor_preview.displayPreviewNativeRevision();
          const auto resize_initial_generation = EditorPreviewControllerTestAccess::render_generation(editor_preview);
          struct ResizeFrameSamples final {
            QElapsedTimer timer;
            QMutex mutex;
            QVector<qint64> timestamps_ms;
          };
          const auto resize_frame_samples = std::make_shared<ResizeFrameSamples>();
          auto& resize_timer = resize_frame_samples->timer;
          resize_timer.start();
          const auto resize_frame_connection = QObject::connect(studio_window, &QQuickWindow::frameSwapped, &engine,
              [resize_frame_samples] {
                const auto at_ms = resize_frame_samples->timer.nsecsElapsed() / 1000000;
                QMutexLocker locker(&resize_frame_samples->mutex);
                resize_frame_samples->timestamps_ms.append(at_ms);
              }, Qt::DirectConnection);
          QJsonArray resize_preview_updates;
          QJsonArray resize_native_revision_updates;
          QJsonArray resize_render_results;
          QSet<int> resize_generations_recorded;
          auto resize_last_native_revision = resize_initial_native_revision;
          const auto resize_preview_connection = QObject::connect(&editor_preview,
              &photo_editor::desktop::EditorPreviewController::previewChanged, &engine, [&] {
                const auto at_ms = resize_timer.nsecsElapsed() / 1000000.0;
                const auto native_revision = editor_preview.displayPreviewNativeRevision();
                resize_preview_updates.append(QJsonObject{
                    {QStringLiteral("at_ms"), at_ms},
                    {QStringLiteral("native_revision"), native_revision}});
                if (native_revision > resize_last_native_revision) {
                  resize_native_revision_updates.append(QJsonObject{
                      {QStringLiteral("at_ms"), at_ms},
                      {QStringLiteral("from_revision"), resize_last_native_revision},
                      {QStringLiteral("to_revision"), native_revision},
                      {QStringLiteral("advance_count"), native_revision - resize_last_native_revision}});
                  resize_last_native_revision = native_revision;
                }
                const auto ready_result = EditorPreviewControllerTestAccess::ready_interactive_render_result(editor_preview);
                if (ready_result && ready_result->generation > resize_initial_generation &&
                    !resize_generations_recorded.contains(ready_result->generation)) {
                  resize_generations_recorded.insert(ready_result->generation);
                  resize_render_results.append(QJsonObject{
                      {QStringLiteral("observed_at_ms"), at_ms},
                      {QStringLiteral("generation"), ready_result->generation},
                      {QStringLiteral("backend"), ready_result->backend},
                      {QStringLiteral("output_width"), ready_result->output_width},
                      {QStringLiteral("output_height"), ready_result->output_height},
                      {QStringLiteral("full_resolution"), ready_result->full_resolution},
                      {QStringLiteral("draft_interactive"), ready_result->draft_interactive},
                      {QStringLiteral("render_reason"), ready_result->render_reason},
                      {QStringLiteral("render_mode"), ready_result->render_mode},
                      {QStringLiteral("interaction_mode"), ready_result->interaction_mode},
                      {QStringLiteral("completed_render_stages"), QJsonArray::fromStringList(ready_result->completed_render_stages)},
                      {QStringLiteral("total_elapsed_ms"), ready_result->total_elapsed_ms},
                      {QStringLiteral("output_elapsed_ms"), ready_result->adjustment_output_elapsed_ms},
                      {QStringLiteral("fallback_reason"), ready_result->fallback_reason},
                      {QStringLiteral("failed_render_stage"), ready_result->failed_render_stage},
                      {QStringLiteral("failure_stage"), ready_result->failure_stage},
                      {QStringLiteral("failure_detail"), ready_result->failure_detail},
                      {QStringLiteral("output_status"), ready_result->output_lut.status},
                      {QStringLiteral("output_lut_kernel_dispatch_count"), ready_result->output_lut.kernel_dispatch_count},
                      {QStringLiteral("adjustment_gpu_command_buffer_count"), ready_result->adjustment_gpu_command_buffer_count},
                      {QStringLiteral("adjustment_gpu_kernel_dispatch_count"), ready_result->adjustment_gpu_kernel_dispatch_count},
                      {QStringLiteral("adjustment_gpu_blit_count"), ready_result->adjustment_gpu_blit_count},
                      {QStringLiteral("adjustment_gpu_cpu_to_gpu_bytes"), ready_result->adjustment_gpu_cpu_to_gpu_bytes},
                      {QStringLiteral("adjustment_gpu_gpu_to_cpu_bytes"), ready_result->adjustment_gpu_gpu_to_cpu_bytes},
                      {QStringLiteral("adjustment_gpu_spatial_texture_blur_dispatch_count"), ready_result->adjustment_gpu_spatial_texture_blur_dispatch_count},
                      {QStringLiteral("adjustment_gpu_spatial_clarity_blur_dispatch_count"), ready_result->adjustment_gpu_spatial_clarity_blur_dispatch_count},
                      {QStringLiteral("adjustment_gpu_spatial_haze_blur_dispatch_count"), ready_result->adjustment_gpu_spatial_haze_blur_dispatch_count},
                      {QStringLiteral("native_surface"), ready_result->texture_upload_surface != nullptr},
                      {QStringLiteral("render_stage_elapsed_ms"), QJsonObject::fromVariantMap(ready_result->render_stage_elapsed_ms)},
                      {QStringLiteral("render_stage_backends"), QJsonObject::fromVariantMap(ready_result->render_stage_backends)},
                      {QStringLiteral("render_stage_cache_hits"), QJsonObject::fromVariantMap(ready_result->render_stage_cache_hits)}});
                }
              }, Qt::DirectConnection);

          paint_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
          send_perf_pointer(QEvent::MouseButtonPress, resize_start);
          QJsonArray resize_dispatched_moves;
          constexpr int resize_move_count = 60;
          constexpr qint64 resize_cadence_ns = 16000000;
          for (int step = 1; step <= resize_move_count; ++step) {
            const auto target_ns = resize_cadence_ns * step;
            while (resize_timer.nsecsElapsed() < target_ns) {
              QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
              QThread::msleep(1);
            }
            const auto dispatch_start_ms = resize_timer.nsecsElapsed() / 1000000.0;
            const auto point = resize_start + drag_delta * (static_cast<qreal>(step) / resize_move_count);
            send_perf_pointer(QEvent::MouseMove, point);
            resize_dispatched_moves.append(QJsonObject{
                {QStringLiteral("step"), step},
                {QStringLiteral("target_ms"), static_cast<double>(target_ns) / 1000000.0},
                {QStringLiteral("dispatch_start_ms"), dispatch_start_ms},
                {QStringLiteral("dispatch_end_ms"), resize_timer.nsecsElapsed() / 1000000.0},
                {QStringLiteral("photo_layer_stack_performance"),
                 QJsonObject::fromVariantMap(
                     EditorPreviewControllerTestAccess::photo_layer_stack_performance(editor_preview))}});
          }

          const auto resize_final_move_ms = resize_timer.nsecsElapsed() / 1000000.0;
          const auto resize_minimum_hold_ms = resize_final_move_ms + 1000.0;
          const auto resize_hold_deadline_ms = resize_final_move_ms + 2500.0;
          while (resize_timer.nsecsElapsed() / 1000000.0 < resize_hold_deadline_ms &&
                 (resize_timer.nsecsElapsed() / 1000000.0 < resize_minimum_hold_ms ||
                  (editor_preview.displayPreviewNativeRevision() == resize_initial_native_revision &&
                   resize_preview_updates.isEmpty()))) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            QThread::msleep(2);
          }
          const auto resize_held_canvas = capture_canvas_without_guides();
          const auto resize_held_capture_ms = resize_timer.nsecsElapsed() / 1000000.0;
          const auto resize_native_revision_at_capture = editor_preview.displayPreviewNativeRevision();
          const auto resize_native_held = native_window_foreground_sample(studio_window);
          const auto resize_held_capture_path = QDir(proof_dir).filePath(
              QStringLiteral("text-resize-%1-%2-held.png").arg(kind, label));
          const bool resize_held_capture_saved = !resize_held_canvas.isNull() &&
              resize_held_canvas.save(resize_held_capture_path);
          send_perf_pointer(QEvent::MouseButtonRelease, resize_start + drag_delta);
          const bool resize_interaction_finished = wait_for_condition([&] {
            return !transform_overlay->property("interactionActive").toBool() &&
                !EditorPreviewControllerTestAccess::edit_transaction_active(editor_preview);
          }, 3000);
          const auto resize_recipe_after = EditorPreviewControllerTestAccess::recipe_json(editor_preview);
          const auto resize_recipe_after_hash = QCryptographicHash::hash(
              resize_recipe_after.toUtf8(), QCryptographicHash::Sha256).toHex();
          const auto resize_layer_after = editor_preview.photoLayers().at(0).toMap();
          const bool resize_final_render_settled = wait_for_condition([&] {
            return !editor_preview.processing() && !editor_preview.fullRenderRunning();
          }, 20000) && wait_rendered_frame();
          const auto resize_final_canvas = resize_final_render_settled ? capture_canvas_without_guides() : QImage{};
          const bool resize_final_capture_saved = !resize_final_canvas.isNull() && resize_final_canvas.save(
              QDir(proof_dir).filePath(QStringLiteral("text-resize-%1-%2-final.png").arg(kind, label)));
          QObject::disconnect(resize_preview_connection);
          QObject::disconnect(resize_frame_connection);
          QVector<qint64> resize_frame_times;
          {
            QMutexLocker locker(&resize_frame_samples->mutex);
            resize_frame_times = resize_frame_samples->timestamps_ms;
          }
          QJsonArray resize_frame_intervals_ms;
          QJsonArray resize_frame_swapped_during_movement_ms;
          QJsonArray resize_frame_intervals_during_movement_ms;
          qint64 previous_resize_frame_ms = -1;
          for (qsizetype index = 0; index < resize_frame_times.size(); ++index) {
            const auto frame_ms = resize_frame_times.at(index);
            if (index > 0)
              resize_frame_intervals_ms.append(static_cast<double>(frame_ms - resize_frame_times.at(index - 1)));
            if (static_cast<double>(frame_ms) > resize_final_move_ms) continue;
            resize_frame_swapped_during_movement_ms.append(static_cast<double>(frame_ms));
            if (previous_resize_frame_ms >= 0)
              resize_frame_intervals_during_movement_ms.append(static_cast<double>(frame_ms - previous_resize_frame_ms));
            previous_resize_frame_ms = frame_ms;
          }
          int resize_changed_pixels = -1;
          if (!resize_canvas_before.isNull() && !resize_held_canvas.isNull() &&
              resize_canvas_before.size() == resize_held_canvas.size()) {
            resize_changed_pixels = 0;
            for (int y = 0; y < resize_canvas_before.height(); ++y) {
              const auto* before_line = reinterpret_cast<const QRgb*>(resize_canvas_before.constScanLine(y));
              const auto* held_line = reinterpret_cast<const QRgb*>(resize_held_canvas.constScanLine(y));
              for (int x = 0; x < resize_canvas_before.width(); ++x)
                if (before_line[x] != held_line[x]) ++resize_changed_pixels;
            }
          }

          double first_resize_native_update_ms = -1.0;
          int resize_native_updates_during_movement = 0;
          for (const auto& value : resize_native_revision_updates) {
            const auto update = value.toObject();
            const auto at_ms = update.value(QStringLiteral("at_ms")).toDouble();
            if (first_resize_native_update_ms < 0.0) first_resize_native_update_ms = at_ms;
            if (at_ms <= resize_final_move_ms)
              resize_native_updates_during_movement += update.value(QStringLiteral("advance_count")).toInt();
          }
          const bool resize_width_changed = resize_layer_after.value(QStringLiteral("pixelWidth")) !=
              resize_layer_before.value(QStringLiteral("pixelWidth"));
          const bool resize_height_changed = resize_layer_after.value(QStringLiteral("pixelHeight")) !=
              resize_layer_before.value(QStringLiteral("pixelHeight"));
          const auto resize_layout_after = text_layout_metrics(resize_layer_after);
          const bool resize_layout_metrics_valid = resize_layout_before.value(QStringLiteral("valid")).toBool() &&
              resize_layout_after.value(QStringLiteral("valid")).toBool();
          const auto resize_content_height_before = resize_layout_before.value(QStringLiteral("content_height")).toDouble();
          const auto resize_content_height_after = resize_layout_after.value(QStringLiteral("content_height")).toDouble();
          const bool resize_text_reflow_proven = resize_layout_metrics_valid &&
              (resize_layout_before.value(QStringLiteral("line_count")).toInt() !=
                   resize_layout_after.value(QStringLiteral("line_count")).toInt() ||
               std::abs(resize_content_height_after - resize_content_height_before) > 0.5);
          const bool resize_height_behavior_matches_kind =
              (kind == QStringLiteral("corner") && resize_height_changed) ||
              (kind == QStringLiteral("right-edge") && !resize_height_changed);
          const bool resize_geometry_contract = resize_width_changed && resize_height_behavior_matches_kind;
          const bool resize_style_unchanged = editor_preview.photoLayerTypography(0) == resize_style_before;
          const bool resize_scale_unchanged = resize_layer_after.value(QStringLiteral("scale")) ==
              resize_layer_before.value(QStringLiteral("scale"));
          const bool resize_rotation_unchanged = resize_layer_after.value(QStringLiteral("rotation")) ==
              resize_layer_before.value(QStringLiteral("rotation"));
          const bool resize_foreground_any_sample_available =
              resize_native_before.value(QStringLiteral("sample_available")).toBool() ||
              resize_native_held.value(QStringLiteral("sample_available")).toBool();
          const bool resize_foreground_qualified =
              resize_native_before.value(QStringLiteral("sample_available")).toBool() &&
              resize_native_held.value(QStringLiteral("sample_available")).toBool() &&
              resize_native_before.value(QStringLiteral("qualified")).toBool() &&
              resize_native_held.value(QStringLiteral("qualified")).toBool();
          const QJsonObject resize_fixture{
              {QStringLiteral("source_sha256"), QString::fromLatin1(source_sha256)},
              {QStringLiteral("text_perf_only"), text_perf_only},
              {QStringLiteral("empty_paint_layer_removed"), empty_paint_layer_removed_for_text_perf},
              {QStringLiteral("paint_layer_kept_by_override"), text_perf_only && keep_paint_layer_for_text_perf},
              {QStringLiteral("window_width"), studio_window->width()},
              {QStringLiteral("window_height"), studio_window->height()},
              {QStringLiteral("image_viewport_width"), image_viewport->width()},
              {QStringLiteral("image_viewport_height"), image_viewport->height()},
              {QStringLiteral("render_configuration"), render_configuration},
              {QStringLiteral("text"), QJsonValue::fromVariant(resize_layer_before.value(QStringLiteral("text")))},
              {QStringLiteral("typography"), QJsonObject::fromVariantMap(resize_style_before)},
              {QStringLiteral("pixel_width"), QJsonValue::fromVariant(resize_layer_before.value(QStringLiteral("pixelWidth")))},
              {QStringLiteral("pixel_height"), QJsonValue::fromVariant(resize_layer_before.value(QStringLiteral("pixelHeight")))},
              {QStringLiteral("scale"), QJsonValue::fromVariant(resize_layer_before.value(QStringLiteral("scale")))},
              {QStringLiteral("rotation"), QJsonValue::fromVariant(resize_layer_before.value(QStringLiteral("rotation")))}};
          const auto resize_fixture_hash = QCryptographicHash::hash(
              QJsonDocument(resize_fixture).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
          const auto resize_recipe_before_hash_text = QString::fromLatin1(resize_recipe_before_hash);
          const auto resize_recipe_after_hash_text = QString::fromLatin1(resize_recipe_after_hash);
          editor_preview.undo();
          const bool resize_undo_exact = EditorPreviewControllerTestAccess::recipe_json(editor_preview) == resize_recipe_before;
          const bool resize_undo_settled = wait_for_condition([&] {
            return !editor_preview.processing() && !editor_preview.fullRenderRunning();
          }, 20000);
          editor_preview.redo();
          const bool resize_redo_exact = EditorPreviewControllerTestAccess::recipe_json(editor_preview) == resize_recipe_after;
          editor_preview.undo();
          const bool resize_undo_again_exact = EditorPreviewControllerTestAccess::recipe_json(editor_preview) == resize_recipe_before;
          const QJsonObject resize_result{
              {QStringLiteral("label"), label},
              {QStringLiteral("fixture"), resize_fixture},
              {QStringLiteral("text_perf_only"), text_perf_only},
              {QStringLiteral("empty_paint_layer_removed"), empty_paint_layer_removed_for_text_perf},
              {QStringLiteral("paint_layer_kept_by_override"), text_perf_only && keep_paint_layer_for_text_perf},
              {QStringLiteral("fixture_sha256"), QString::fromLatin1(resize_fixture_hash)},
              {QStringLiteral("gesture"), QStringLiteral("native-held-text-box-resize")},
              {QStringLiteral("handle"), handle_name},
              {QStringLiteral("move_count"), static_cast<int>(resize_dispatched_moves.size())},
              {QStringLiteral("target_cadence_ms"), 16},
              {QStringLiteral("drag_delta_viewport_pixels"), QJsonArray{drag_delta.x(), drag_delta.y()}},
              {QStringLiteral("moves"), resize_dispatched_moves},
              {QStringLiteral("preview_updates"), resize_preview_updates},
              {QStringLiteral("native_revision_updates"), resize_native_revision_updates},
              {QStringLiteral("render_results"), resize_render_results},
              {QStringLiteral("native_updates_during_movement"), resize_native_updates_during_movement},
              {QStringLiteral("first_native_update_ms"), first_resize_native_update_ms},
              {QStringLiteral("frame_intervals_ms"), resize_frame_intervals_ms},
              {QStringLiteral("frame_swapped_during_movement_ms"), resize_frame_swapped_during_movement_ms},
              {QStringLiteral("frame_intervals_during_movement_ms"), resize_frame_intervals_during_movement_ms},
              {QStringLiteral("frame_swapped_semantics"), QStringLiteral("scene_graph_frame_queued_for_present")},
              {QStringLiteral("native_window_before"), QJsonObject::fromVariantMap(resize_native_before)},
              {QStringLiteral("native_window_held"), QJsonObject::fromVariantMap(resize_native_held)},
              {QStringLiteral("native_window_samples_qualified"), resize_foreground_qualified},
              {QStringLiteral("native_revision_before"), resize_initial_native_revision},
              {QStringLiteral("native_revision_at_held_capture"), resize_native_revision_at_capture},
              {QStringLiteral("held_capture_ms"), resize_held_capture_ms},
              {QStringLiteral("held_canvas_capture"), QFileInfo(resize_held_capture_path).fileName()},
              {QStringLiteral("held_canvas_changed_pixels"), resize_changed_pixels},
              {QStringLiteral("presented_canvas_changed_while_held"), resize_changed_pixels > 0},
              {QStringLiteral("held_capture_saved"), resize_held_capture_saved},
              {QStringLiteral("interaction_finished"), resize_interaction_finished},
              {QStringLiteral("pixel_width_before"), QJsonValue::fromVariant(resize_layer_before.value(QStringLiteral("pixelWidth")))},
              {QStringLiteral("pixel_width_after"), QJsonValue::fromVariant(resize_layer_after.value(QStringLiteral("pixelWidth")))},
              {QStringLiteral("pixel_height_before"), QJsonValue::fromVariant(resize_layer_before.value(QStringLiteral("pixelHeight")))},
              {QStringLiteral("pixel_height_after"), QJsonValue::fromVariant(resize_layer_after.value(QStringLiteral("pixelHeight")))},
              {QStringLiteral("width_changed"), resize_width_changed},
              {QStringLiteral("height_changed"), resize_height_changed},
              {QStringLiteral("height_behavior_matches_kind"), resize_height_behavior_matches_kind},
              {QStringLiteral("geometry_contract"), resize_geometry_contract},
              {QStringLiteral("text_layout_before"), resize_layout_before},
              {QStringLiteral("text_layout_after"), resize_layout_after},
              {QStringLiteral("text_reflow_proven"), resize_text_reflow_proven},
              {QStringLiteral("style_unchanged"), resize_style_unchanged},
              {QStringLiteral("scale_unchanged"), resize_scale_unchanged},
              {QStringLiteral("rotation_unchanged"), resize_rotation_unchanged},
              {QStringLiteral("recipe_before_sha256"), resize_recipe_before_hash_text},
              {QStringLiteral("recipe_after_sha256"), resize_recipe_after_hash_text},
              {QStringLiteral("undo_exact"), resize_undo_exact},
              {QStringLiteral("redo_exact"), resize_redo_exact},
              {QStringLiteral("undo_again_exact"), resize_undo_again_exact},
              {QStringLiteral("final_render_settled"), resize_final_render_settled},
              {QStringLiteral("final_capture_saved"), resize_final_capture_saved}};
          const auto resize_metrics_path = QDir(proof_dir).filePath(
              QStringLiteral("text-resize-perf-%1-%2.json").arg(kind, label));
          QFile resize_metrics_file(resize_metrics_path);
          const bool resize_metrics_saved = resize_metrics_file.open(QIODevice::WriteOnly) &&
              resize_metrics_file.write(QJsonDocument(resize_result).toJson(QJsonDocument::Indented)) > 0;
          const bool foreground_ok = !resize_foreground_any_sample_available || resize_foreground_qualified;
          if (!resize_metrics_saved || !resize_held_capture_saved || !resize_final_capture_saved ||
              !resize_interaction_finished || !resize_geometry_contract || !resize_text_reflow_proven ||
              !resize_style_unchanged || !resize_scale_unchanged || !resize_rotation_unchanged ||
              !foreground_ok || resize_changed_pixels <= 0 || !resize_undo_exact ||
              !resize_undo_settled || !resize_redo_exact || !resize_undo_again_exact ||
              !resize_final_render_settled) {
            qWarning() << "Held text resize perf proof failed" << kind << label
                       << "dimensions" << resize_layer_before.value("pixelWidth") << resize_layer_before.value("pixelHeight")
                       << "->" << resize_layer_after.value("pixelWidth") << resize_layer_after.value("pixelHeight")
                       << "layout before" << resize_layout_before << "layout after" << resize_layout_after
                       << "geometry contract" << resize_geometry_contract
                       << "held pixels" << resize_changed_pixels << "native updates" << resize_native_updates_during_movement
                       << "foreground" << resize_foreground_qualified << "undo" << resize_undo_exact;
            return false;
          }
          std::cout << "Held text resize baseline: kind=" << kind.toStdString()
                    << " moves=" << resize_dispatched_moves.size()
                    << " nativeUpdatesDuringMovement=" << resize_native_updates_during_movement
                    << " heldChangedPixels=" << resize_changed_pixels
                    << " metrics=" << resize_metrics_path.toStdString() << '\n';
          return true;
        };
        const auto run_resize_probe = [&](const QString& kind, const QString& handle_name,
                                          qreal width_fraction, qreal height_fraction) {
          if (!wait_for_condition([&] {
                return !editor_preview.processing() && !editor_preview.fullRenderRunning() &&
                    !transform_overlay->property("interactionActive").toBool() &&
                    !EditorPreviewControllerTestAccess::edit_transaction_active(editor_preview);
              }, 20000) || !wait_rendered_frame())
            return false;
          const auto frame_width = transform_overlay->property("frameWidth").toDouble();
          const auto frame_height = transform_overlay->property("frameHeight").toDouble();
          const QPointF resize_delta(-frame_width * width_fraction, frame_height * height_fraction);
          return frame_width > 0.0 && frame_height > 0.0 &&
              run_held_resize_probe(kind, handle_name, resize_delta);
        };
        QJsonArray frame_swapped_ms;
        QJsonArray frame_intervals_ms;
        for (int i = 0; i < captured_frame_signal_ms.size(); ++i) {
          frame_swapped_ms.append(static_cast<double>(captured_frame_signal_ms.at(i)));
          if (i > 0)
            frame_intervals_ms.append(static_cast<double>(captured_frame_signal_ms.at(i) - captured_frame_signal_ms.at(i - 1)));
        }
        QJsonArray preview_intervals_ms;
        QJsonArray native_revision_intervals_ms;
        double first_preview_ms = -1.0;
        double previous_preview_ms = -1.0;
        int preview_updates_during_movement = 0;
        for (const auto& value : preview_updates) {
          const auto at_ms = value.toObject().value(QStringLiteral("at_ms")).toDouble();
          if (at_ms <= final_move_ms) ++preview_updates_during_movement;
          if (first_preview_ms < 0.0) first_preview_ms = at_ms;
          if (previous_preview_ms >= 0.0) preview_intervals_ms.append(at_ms - previous_preview_ms);
          previous_preview_ms = at_ms;
        }
        double first_native_update_ms = -1.0;
        double previous_native_update_ms = -1.0;
        double first_native_update_during_movement_ms = -1.0;
        int native_updates_during_movement = 0;
        for (const auto& value : native_revision_updates) {
          const auto update = value.toObject();
          const auto at_ms = update.value(QStringLiteral("at_ms")).toDouble();
          const auto advance_count = update.value(QStringLiteral("advance_count")).toInt();
          if (first_native_update_ms < 0.0) first_native_update_ms = at_ms;
          if (previous_native_update_ms >= 0.0)
            native_revision_intervals_ms.append(at_ms - previous_native_update_ms);
          previous_native_update_ms = at_ms;
          if (at_ms <= final_move_ms) {
            native_updates_during_movement += advance_count;
            if (first_native_update_during_movement_ms < 0.0)
              first_native_update_during_movement_ms = at_ms;
          }
        }

        QJsonArray frame_swapped_during_movement_ms;
        QJsonArray frame_intervals_during_movement_ms;
        qint64 previous_movement_frame_ms = -1;
        for (const auto frame_ms : captured_frame_signal_ms) {
          if (static_cast<double>(frame_ms) > final_move_ms) continue;
          frame_swapped_during_movement_ms.append(static_cast<double>(frame_ms));
          if (previous_movement_frame_ms >= 0)
            frame_intervals_during_movement_ms.append(static_cast<double>(frame_ms - previous_movement_frame_ms));
          previous_movement_frame_ms = frame_ms;
        }
        const bool native_texture_enabled = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_TEXTURE");
        const bool native_candidate_gate = label == QStringLiteral("candidate") && native_texture_enabled;
        QJsonObject fixture{
            {QStringLiteral("source_path"), source_path.isEmpty() ? photo_path : source_path},
            {QStringLiteral("source_sha256"), QString::fromLatin1(source_sha256)},
            {QStringLiteral("text_perf_only"), text_perf_only},
            {QStringLiteral("empty_paint_layer_removed"), empty_paint_layer_removed_for_text_perf},
            {QStringLiteral("paint_layer_kept_by_override"), text_perf_only && keep_paint_layer_for_text_perf},
            {QStringLiteral("canvas_width"), typography_canvas_width},
            {QStringLiteral("canvas_height"), typography_canvas_height},
            {QStringLiteral("window_width"), studio_window->width()},
            {QStringLiteral("window_height"), studio_window->height()},
            {QStringLiteral("image_viewport_width"), image_viewport->width()},
            {QStringLiteral("image_viewport_height"), image_viewport->height()},
            {QStringLiteral("render_configuration"), render_configuration},
            {QStringLiteral("text"), QJsonValue::fromVariant(layer_before.value(QStringLiteral("text")))},
            {QStringLiteral("typography"), QJsonObject::fromVariantMap(style_before)},
            {QStringLiteral("x"), QJsonValue::fromVariant(layer_before.value(QStringLiteral("x")))},
            {QStringLiteral("y"), QJsonValue::fromVariant(layer_before.value(QStringLiteral("y")))},
            {QStringLiteral("pixel_width"), QJsonValue::fromVariant(layer_before.value(QStringLiteral("pixelWidth")))},
            {QStringLiteral("pixel_height"), QJsonValue::fromVariant(layer_before.value(QStringLiteral("pixelHeight")))},
            {QStringLiteral("scale"), QJsonValue::fromVariant(layer_before.value(QStringLiteral("scale")))},
            {QStringLiteral("rotation"), QJsonValue::fromVariant(layer_before.value(QStringLiteral("rotation")))}};
        const auto fixture_sha256 = QCryptographicHash::hash(
            QJsonDocument(fixture).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
        const QJsonObject result{
            {QStringLiteral("label"), label},
            {QStringLiteral("fixture"), fixture},
            {QStringLiteral("text_perf_only"), text_perf_only},
            {QStringLiteral("empty_paint_layer_removed"), empty_paint_layer_removed_for_text_perf},
            {QStringLiteral("paint_layer_kept_by_override"), text_perf_only && keep_paint_layer_for_text_perf},
            {QStringLiteral("fixture_sha256"), QString::fromLatin1(fixture_sha256)},
            {QStringLiteral("gesture"), QStringLiteral("native-held-text-move")},
            {QStringLiteral("move_count"), move_count},
            {QStringLiteral("target_cadence_ms"), 16},
            {QStringLiteral("moves"), dispatched_moves},
            {QStringLiteral("preview_updates"), preview_updates},
            {QStringLiteral("preview_updates_during_movement"), preview_updates_during_movement},
            {QStringLiteral("native_revision_updates"), native_revision_updates},
            {QStringLiteral("render_results"), render_results},
            {QStringLiteral("native_updates_during_movement"), native_updates_during_movement},
            {QStringLiteral("first_native_update_ms"), first_native_update_ms},
            {QStringLiteral("first_native_update_during_movement_ms"), first_native_update_during_movement_ms},
            {QStringLiteral("native_revision_intervals_ms"), native_revision_intervals_ms},
            {QStringLiteral("final_move_ms"), final_move_ms},
            {QStringLiteral("preview_intervals_ms"), preview_intervals_ms},
            {QStringLiteral("first_preview_update_ms"), first_preview_ms},
            {QStringLiteral("frame_swapped_ms"), frame_swapped_ms},
            {QStringLiteral("frame_intervals_ms"), frame_intervals_ms},
            {QStringLiteral("frame_swapped_during_movement_ms"), frame_swapped_during_movement_ms},
            {QStringLiteral("frame_intervals_during_movement_ms"), frame_intervals_during_movement_ms},
            {QStringLiteral("frame_swapped_semantics"), QStringLiteral("scene_graph_frame_queued_for_present")},
            {QStringLiteral("native_revision_before"), initial_native_revision},
            {QStringLiteral("native_window_before"), QJsonObject::fromVariantMap(native_window_before)},
            {QStringLiteral("native_window_held"), QJsonObject::fromVariantMap(native_window_held)},
            {QStringLiteral("native_window_samples_qualified"),
             native_window_before.value(QStringLiteral("qualified")).toBool() &&
                 native_window_held.value(QStringLiteral("qualified")).toBool()},
            {QStringLiteral("initial_render_generation"), initial_render_generation},
            {QStringLiteral("native_texture_path_enabled"), native_texture_enabled},
            {QStringLiteral("native_candidate_gate_enabled"), native_candidate_gate},
            {QStringLiteral("native_revision_at_held_capture"), native_revision_at_capture},
            {QStringLiteral("native_revision_after_release"), revision_after_release},
            {QStringLiteral("provider_cache_key_before"), initial_provider_cache_key},
            {QStringLiteral("held_capture_ms"), held_capture_ms},
            {QStringLiteral("held_canvas_capture"), QFileInfo(held_capture_path).fileName()},
            {QStringLiteral("held_canvas_changed_pixels"), changed_canvas_pixels},
            {QStringLiteral("native_revision_advanced_while_held"), native_revision_at_capture > initial_native_revision},
            {QStringLiteral("presented_canvas_changed_while_held"), changed_canvas_pixels > 0},
            {QStringLiteral("held_capture_saved"), held_capture_saved},
            {QStringLiteral("interaction_finished"), interaction_finished},
            {QStringLiteral("layer_position_changed"), move_changed_layer},
            {QStringLiteral("style_unchanged"), editor_preview.photoLayerTypography(0) == style_before},
            {QStringLiteral("scale_unchanged"), layer_after.value(QStringLiteral("scale")) == layer_before.value(QStringLiteral("scale"))},
            {QStringLiteral("rotation_unchanged"), layer_after.value(QStringLiteral("rotation")) == layer_before.value(QStringLiteral("rotation"))},
            {QStringLiteral("recipe_before_sha256"), QString::fromLatin1(recipe_before_hash)},
            {QStringLiteral("recipe_after_sha256"), recipe_after_hash_text},
            {QStringLiteral("undo_exact"), undo_exact},
            {QStringLiteral("redo_exact"), redo_exact},
            {QStringLiteral("undo_again_exact"), undo_again_exact},
            {QStringLiteral("final_render_settled"), final_render_settled},
            {QStringLiteral("final_native_revision"), final_native_revision},
            {QStringLiteral("final_native_width"), final_native_size.width()},
            {QStringLiteral("final_native_height"), final_native_size.height()},
            {QStringLiteral("final_capture_saved"), final_capture_saved},
            {QStringLiteral("undo_settled"), undo_settled},
            {QStringLiteral("observability_run_directory"), photo_editor::desktop::observability::runDirectory()}};
        const auto metrics_path = QDir(proof_dir).filePath(QStringLiteral("text-drag-perf-%1.json").arg(label));
        QFile metrics_file(metrics_path);
        const bool metrics_saved = metrics_file.open(QIODevice::WriteOnly) &&
            metrics_file.write(QJsonDocument(result).toJson(QJsonDocument::Indented)) > 0;
        const bool observability_flushed = photo_editor::desktop::observability::flush();
        if (!held_capture_saved || !metrics_saved || !observability_flushed || !move_changed_layer || !interaction_finished ||
            editor_preview.photoLayerTypography(0) != style_before ||
            layer_after.value(QStringLiteral("scale")) != layer_before.value(QStringLiteral("scale")) ||
            layer_after.value(QStringLiteral("rotation")) != layer_before.value(QStringLiteral("rotation")) ||
            changed_canvas_pixels <= 0 || !undo_exact || !undo_settled || !redo_exact || !undo_again_exact ||
            !final_render_settled || !final_capture_saved ||
            (native_candidate_gate && native_updates_during_movement < 2)) {
          qWarning() << "Text drag perf proof failed" << label << "moves" << dispatched_moves.size()
                     << "preview updates" << preview_updates.size() << "native revision"
                     << initial_native_revision << "->" << revision_after_release << "native updates during movement"
                     << native_updates_during_movement << "changed pixels"
                     << changed_canvas_pixels << "undo" << undo_exact << "settled" << undo_settled;
          return fail("Held text-drag performance proof must update presented text pixels and undo exactly");
        }
        std::cout << "Text-drag performance baseline: label=" << label.toStdString()
                  << " moves=" << dispatched_moves.size() << " previewUpdates=" << preview_updates.size()
                  << " nativeUpdatesDuringMovement=" << native_updates_during_movement
                  << " firstPreviewMs=" << first_preview_ms << " changedCanvasPixels=" << changed_canvas_pixels
                  << " metrics=" << metrics_path.toStdString() << '\n';
        if (!run_resize_probe(QStringLiteral("corner"), QStringLiteral("photoLayerTransformBottomRightHandle"),
                0.45, 0.45) ||
            !run_resize_probe(QStringLiteral("right-edge"), QStringLiteral("photoLayerTransformRightHandle"),
                0.50, 0.0))
          return fail("Held corner and edge text-box resize must reflow rendered pixels and undo exactly");
        if (text_layer_menu_proof && !run_text_layer_menu_proof())
          return fail("The text layer actions menu should populate on pointer and keyboard open, refresh lock state, and restore its recipe");
        return EXIT_SUCCESS;
      }
      if (text_layer_menu_proof) {
        if (!run_text_layer_menu_proof())
          return fail("The text layer actions menu should populate on pointer and keyboard open, refresh lock state, and restore its recipe");
        return EXIT_SUCCESS;
      }
      for (const auto& density : densities) {
        if (!QMetaObject::invokeMethod(edit, "setInterfaceDensity", Q_ARG(QVariant, QVariant(density))))
          return fail("Typography proof should set each supported density");
        for (const auto& size : {QSize(1180, 768), QSize(1440, 900)}) {
          studio_window->resize(size);
          process_layout_events(8);
          auto* scroll = find_named_item(studio_root, QStringLiteral("creativeToolsScroll"));
          if (!capture(QStringLiteral("typography-%1-%2").arg(density).arg(size.width()), size))
            return fail("Typography inspector should render at each density");
          if (!scroll || !scene_rect(scroll).contains(scene_rect(content)) ||
              !scene_rect(scroll).contains(scene_rect(bold))) {
            qWarning() << "Typography fit" << density << size << "scroll" << scene_rect(scroll)
                       << "content" << scene_rect(content) << "bold" << scene_rect(bold);
            return fail("Essential typography must remain visible inside the inspector at all densities");
          }
        }
      }
      if (!QMetaObject::invokeMethod(edit, "setInterfaceDensity", Q_ARG(QVariant, QVariant(QStringLiteral("balanced")))))
        return fail("Typography proof should restore balanced density");
      studio_window->resize(1440, 900);
      process_layout_events(8);
      auto* geometry_scroll = find_named_item(studio_root, QStringLiteral("creativeToolsScroll"));
      auto* geometry_viewport = geometry_scroll
          ? qobject_cast<QQuickItem*>(geometry_scroll->property("contentItem").value<QObject*>()) : nullptr;
      if (!geometry_viewport)
        return fail("Expanded geometry proof should access its scroll viewport");
      const auto collapsed_geometry_content_height = geometry_viewport->property("contentHeight").toDouble();
      if (!reveal_control(geometry) || !click_studio_item(geometry) || !wait_for_condition([&] {
            return geometry->property("active").toBool();
          }, 3000))
        return fail("Optional box and perspective controls should open");
      process_layout_events(8);
      if (!wait_for_condition([&] {
            return geometry_viewport->property("contentHeight").toDouble() > collapsed_geometry_content_height + 1.0;
          }, 3000))
        return fail("Expanded geometry should update the inspector scroll extent");
      auto* box_width = find_named_item(studio_root, QStringLiteral("photoLayerTextBoxWidthSlider"));
      if (!box_width || !item_visible(box_width))
        return fail("Expanded geometry should expose text box dimensions");
      if (!reveal_control(box_width))
        return fail("Expanded geometry should scroll inside its viewport");
      if (!wait_rendered_frame())
        return fail("Expanded geometry should finish rendering");
      if (!capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("typography-geometry"),
            1440, 900, {QStringLiteral("creativeToolsSurface"), QStringLiteral("photoLayerTextBoxWidthSlider"),
                        QStringLiteral("photoLayerTextBoxHeightSlider")}, &studio_manifest))
        return fail("Expanded geometry should render inside its scroll viewport");
      if (!reveal_control(geometry) || !click_studio_item(geometry) || !wait_for_condition([&] {
            return !geometry->property("active").toBool();
          }, 3000))
        return fail("Geometry should collapse again");
      reveal_control(content);
      if (!click_studio_item(creative_tools_float) ||
          !wait_for_condition([&] { return content->window() != studio_window && item_visible(content); }, 5000))
        return fail("Typography controls should work when floated");
      auto* floating = content->window();
      if (floating->grabWindow().isNull() ||
          !floating->grabWindow().save(QDir(proof_dir).filePath("typography-floating.png")))
        return fail("Typography floating panel should render");
      if (!click_studio_item(creative_tools_float) ||
          !wait_for_condition([&] { return content->window() == studio_window; }, 5000))
        return fail("Typography should dock again");
      const auto drag_text_box = [&](const QString& screenshot) {
        auto* handle = find_named_item(studio_root, QStringLiteral("photoLayerTransformBottomRightHandle"));
        if (!handle || !item_visible(handle) || !wait_rendered_frame()) {
          qWarning() << "Text resize handle unavailable" << handle;
          return false;
        }
        const auto before_layer = editor_preview.photoLayers().at(0).toMap();
        const auto before_style = editor_preview.photoLayerTypography(0);
        const auto before_recipe = EditorPreviewControllerTestAccess::recipe_json(editor_preview);
        const auto start = scene_rect(handle).center();
        const QPointF delta(100, 70);
        paint_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
        send_pointer_to_window(studio_window, QEvent::MouseButtonPress, start);
        for (int step = 1; step <= 3; ++step)
          send_pointer_to_window(studio_window, QEvent::MouseMove, start + delta * (step/3.0));
        send_pointer_to_window(studio_window, QEvent::MouseButtonRelease, start + delta);
        if (!wait_for_condition([&] {
              return editor_preview.photoLayers().at(0).toMap().value("pixelWidth").toInt() >
                  before_layer.value("pixelWidth").toInt();
            }, 3000)) {
          qWarning() << "Text resize did not grow the box" << before_layer.value("pixelWidth")
                     << editor_preview.photoLayers().at(0).toMap().value("pixelWidth");
          qWarning() << "Drag bounds" << start << "delta" << delta << "handle" << scene_rect(handle);
          studio_window->grabWindow().save(QDir(proof_dir).filePath("typography-box-resize-failure.png"));
          return false;
        }
        const auto after_layer = editor_preview.photoLayers().at(0).toMap();
        auto expected_style = before_style;
        if (before_style.value("mode").toString() == QStringLiteral("point")) {
          expected_style.insert("mode", QStringLiteral("paragraph"));
          expected_style.insert("version", qMax(2, before_style.value("version").toInt()));
          if (!expected_style.contains("paragraphSpacing"))
            expected_style.insert("paragraphSpacing", 0);
        }
        if (editor_preview.photoLayerTypography(0) != expected_style ||
            after_layer.value("pixelHeight").toInt() <= before_layer.value("pixelHeight").toInt() ||
            after_layer.value("fontSize") != before_layer.value("fontSize") ||
            after_layer.value("scale") != before_layer.value("scale") ||
            after_layer.value("rotation") != before_layer.value("rotation")) {
          qWarning() << "Text resize changed styling" << before_style << editor_preview.photoLayerTypography(0)
                     << "font size" << before_layer.value("fontSize") << after_layer.value("fontSize")
                     << "scale" << before_layer.value("scale") << after_layer.value("scale")
                     << "dimensions" << after_layer.value("pixelWidth") << after_layer.value("pixelHeight");
          studio_window->grabWindow().save(QDir(proof_dir).filePath("typography-box-resize-failure.png"));
          return false;
        }
        const auto after_recipe = EditorPreviewControllerTestAccess::recipe_json(editor_preview);
        if (!wait_for_condition([&] { return !editor_preview.processing() && !editor_preview.fullRenderRunning(); }, 20000) ||
            !wait_rendered_frame() || !studio_window->grabWindow().save(QDir(proof_dir).filePath(screenshot))) return false;
        editor_preview.undo();
        if (EditorPreviewControllerTestAccess::recipe_json(editor_preview) != before_recipe) {
          qWarning() << "Text resize undo did not restore recipe";
          return false;
        }
        editor_preview.redo();
        if (EditorPreviewControllerTestAccess::recipe_json(editor_preview) != after_recipe) {
          qWarning() << "Text resize redo did not restore recipe";
          return false;
        }
        editor_preview.undo();
        return EditorPreviewControllerTestAccess::recipe_json(editor_preview) == before_recipe;
      };
      if (!drag_text_box(QStringLiteral("typography-paragraph-box-resize.png")))
        return fail("Native paragraph handles must resize the box while preserving type size and one-step undo");
      const auto paragraph_recipe = EditorPreviewControllerTestAccess::recipe_json(editor_preview);
      if (!editor_preview.addPointTextLayer(QStringLiteral("Point text"), 80, 80,
              typography_canvas_width, typography_canvas_height) ||
          !QMetaObject::invokeMethod(edit, "editPhotoLayerInspectorTarget", Q_ARG(QVariant, QVariant(0))) ||
          !wait_for_condition([&] { return editor_preview.photoLayerTypography(0).value("mode").toString() == "point"; }, 3000) ||
          !drag_text_box(QStringLiteral("typography-point-box-resize.png")))
        return fail("Native point-text handles must enable paragraph wrapping without scaling the letters");
      editor_preview.undo();
      if (EditorPreviewControllerTestAccess::recipe_json(editor_preview) != paragraph_recipe)
        return fail("Point-text resize proof must restore the preceding document exactly");
      QFile manifest_file(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
      if (!manifest_file.open(QIODevice::WriteOnly) ||
          manifest_file.write(QJsonDocument(studio_manifest).toJson(QJsonDocument::Indented)) <= 0)
        return fail("Typography proof should save its capture manifest");
      std::cout << "Real Studio typography: multiline copy, Bold/Italic/Underline, keyboard font search, densities, geometry, float/dock, paragraph/point box resize with fixed font size and grouped undo passed\n";
      return EXIT_SUCCESS;

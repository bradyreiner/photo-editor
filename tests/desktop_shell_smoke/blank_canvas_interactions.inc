      // This Canvas prerequisite explicitly qualifies the native Metal preview,
      // not only a Cocoa window displaying the provider-image fallback. The
      // test runtime installs that texture sink only through this profile.
      if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_TEXTURE"))
        return fail("Canvas/Board native proof requires PHOTO_EDITOR_SMOKE_NATIVE_TEXTURE=1 before controller construction");
      studio_window->resize(1440, 900);
      if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER")) {
        activate_native_test_window(studio_window);
        if (!wait_for_condition([&] { return studio_window->isActive() && studio_window->isExposed(); }, 5000))
          return fail("Native canvas pointer proof requires an active exposed window");
      }
      auto* canvas_viewport = find_named_item(studio_root, QStringLiteral("editImageViewport"));
      if (!canvas_viewport) return fail("Canvas proof requires the Edit viewport");
      canvas_viewport->setProperty("zoom", 0.5);
      process_layout_events(8);
      const auto native_pointer_proof = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER");
      quint64 canvas_pointer_timestamp = 1000;
      const auto canvas_pointer = [&](QEvent::Type type, const QPointF& point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QMouseEvent event(type, point, QPointF(studio_window->mapToGlobal(point.toPoint())),
            type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
            type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick ? Qt::LeftButton : Qt::NoButton, modifiers);
        event.setTimestamp(canvas_pointer_timestamp);
        canvas_pointer_timestamp += 16;
        QCoreApplication::sendEvent(studio_window, &event);
        process_layout_events(3);
      };
      const auto move_canvas_pointer = [&](const QPointF& point) {
        if (native_pointer_proof)
          QCursor::setPos(studio_window->mapToGlobal(point.toPoint()));
        canvas_pointer(QEvent::MouseMove, point);
      };
      const auto find_visible_canvas_item = [&](const QString& name) -> QQuickItem* {
        for (auto* candidate : find_named_items(studio_root, name))
          if (item_visible(candidate)) return candidate;
        return nullptr;
      };
      const auto describe_canvas_item_candidates = [&](const QString& name) {
        const auto candidates = find_named_items(studio_root, name);
        QStringList descriptions;
        descriptions.reserve(candidates.size());
        for (auto* candidate : candidates) {
          const auto rect = scene_rect(candidate);
          descriptions.push_back(QStringLiteral("visible=%1 enabled=%2 rect=%3,%4 %5x%6")
              .arg(item_visible(candidate))
              .arg(candidate->isEnabled())
              .arg(rect.x(), 0, 'f', 1)
              .arg(rect.y(), 0, 'f', 1)
              .arg(rect.width(), 0, 'f', 1)
              .arg(rect.height(), 0, 'f', 1));
          for (auto* ancestor = candidate->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
            if (ancestor->objectName() != QStringLiteral("baseImageLayerRow") &&
                ancestor->objectName() != QStringLiteral("photoLayerRow")) continue;
            descriptions.append(QStringLiteral("row=%1 enabled=%2 hovered=%3 contextualActionsVisible=%4 lockButtonEnabled=%5")
                .arg(ancestor->objectName()).arg(ancestor->isEnabled()).arg(ancestor->property("hovered").toBool())
                .arg(ancestor->property("contextualActionsVisible").toBool()).arg(ancestor->property("lockButtonEnabled").toBool()));
            break;
          }
        }
        return QStringLiteral("%1 candidate(s): %2")
            .arg(candidates.size())
            .arg(descriptions.join(QStringLiteral("; ")));
      };
      const auto reveal_layer_row_action = [&](QQuickItem* item) {
        for (auto* ancestor = item; ancestor; ancestor = ancestor->parentItem()) {
          const auto row_name = ancestor->objectName();
          if (row_name != QStringLiteral("photoLayerRow") &&
              row_name != QStringLiteral("baseImageLayerRow") &&
              row_name != QStringLiteral("photoLayerNestedMaskRow"))
            continue;
          if (!item_visible(ancestor)) return false;
          // Hover the row first so its contextual action zone becomes enabled.
          move_canvas_pointer(scene_rect(ancestor).center());
          return wait_for_condition([&] {
            process_layout_events(2);
            return item_visible(item) && item->isEnabled();
          }, 2500);
        }
        return false;
      };
      const auto canvas_click_item = [&](QQuickItem* item) {
        if (!item) return false;
        if (!item->isEnabled() && !reveal_layer_row_action(item)) return false;
        if (!item_visible(item) || !item->isEnabled()) return false;
        studio_window->grabWindow();
        canvas_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
        const auto point = scene_rect(item).center();
        // Keep the real OS pointer inside the row while moving from its hover
        // target to the newly revealed secondary action.
        move_canvas_pointer(point);
        if (!wait_for_condition([&] {
              process_layout_events(2);
              return item_visible(item) && item->isEnabled();
            }, 1500)) return false;
        canvas_pointer(QEvent::MouseButtonPress, point);
        canvas_pointer(QEvent::MouseButtonRelease, point);
        return true;
      };
      const auto canvas_click = [&](const QString& name) {
        // Several editing states retain QML controls with the same object
        // name. Always act on a rendered instance, never the first QObject
        // match, which may belong to a hidden retained row.
        return canvas_click_item(find_visible_canvas_item(name));
      };
      const auto visible_canvas_menu_item = [&](const QString& menu_name, const QString& key) -> QQuickItem* {
        auto* menu = find_named_object(studio_root, menu_name);
        if (!menu) return nullptr;
        const auto options = menu->property("menuItems").toList();
        // Enumerate rendered rows, then identify the visible semantic action.
        // Conditional/hidden actions may retain a delegate or shift row indices.
        for (qsizetype row = 0; row < options.size(); ++row) {
          const auto name = menu_name + QStringLiteral("PopupItem") + QString::number(row);
          for (auto* candidate : find_named_items(studio_root, name))
            if (item_visible(candidate) && candidate->property("itemKey").toString() == key) return candidate;
        }
        return nullptr;
      };
      const auto canvas_type = [&](const QString& name, const QString& text) {
        if (!canvas_click(name)) return false;
        auto* focus = studio_window->activeFocusItem();
        if (!focus) return false;
        const auto select_all = QKeySequence(QKeySequence::SelectAll)[0];
        QKeyEvent select_all_press(QEvent::KeyPress, select_all.key(), select_all.keyboardModifiers());
        QCoreApplication::sendEvent(focus, &select_all_press);
        QInputMethodEvent input;
        input.setCommitString(text);
        QCoreApplication::sendEvent(focus, &input);
        process_layout_events(3);
        const auto actual = find_named_object(studio_root, name)->property("text").toString();
        if (actual != text) std::cerr << "canvas input " << name.toStdString() << " actual=" << actual.toStdString()
                                     << " expected=" << text.toStdString() << " focus=" << focus->metaObject()->className() << '\n';
        return actual == text;
      };
      const auto canvas_shortcut = [&](int key, bool shift = false) {
        if (native_pointer_proof)
          activate_native_test_window(studio_window);
        studio_window->raise();
        studio_window->requestActivate();
        if (!wait_for_condition([&] {
          if (!studio_window->isActive()) { studio_window->raise(); studio_window->requestActivate(); }
          return studio_window->isActive();
        }, 5000)) {
          std::cerr << "Canvas shortcut requires an active native window\n";
          return;
        }
        process_layout_events(6);
        auto* shortcut_surface = find_named_item(studio_root, QStringLiteral("editStudioWorkspace"));
        if (shortcut_surface) shortcut_surface->forceActiveFocus(Qt::ShortcutFocusReason);
        else studio_window->contentItem()->forceActiveFocus(Qt::ShortcutFocusReason);
        process_layout_events(2);
        // Qt maps ControlModifier to the macOS Command key; MetaModifier is
        // the physical Control key on Cocoa and misses main Undo bindings.
        auto modifiers = Qt::KeyboardModifiers(Qt::ControlModifier);
        if (key == Qt::Key_C) modifiers = QKeySequence(QKeySequence::Copy)[0].keyboardModifiers();
        if (key == Qt::Key_V && shift)
          modifiers = QKeySequence(QStringLiteral("Ctrl+Shift+V"))[0].keyboardModifiers();
        else if (key == Qt::Key_V)
          modifiers = QKeySequence(QKeySequence::Paste)[0].keyboardModifiers();
        if (key == Qt::Key_S) modifiers = QKeySequence(QKeySequence::Save)[0].keyboardModifiers();
        if (key == Qt::Key_Z && studio_root->property("boardsWorkspaceActive").toBool())
          modifiers = QKeySequence(shift ? QKeySequence::Redo : QKeySequence::Undo)[0].keyboardModifiers();
        if (shift) modifiers |= Qt::ShiftModifier;
        if (native_pointer_proof) {
          key_native_test_window(studio_window, key, static_cast<int>(modifiers.toInt()));
        } else {
          QKeyEvent press(QEvent::KeyPress, key, modifiers);
          QKeyEvent release(QEvent::KeyRelease, key, modifiers);
          QCoreApplication::sendEvent(studio_window, &press);
          QCoreApplication::sendEvent(studio_window, &release);
        }
        process_layout_events(6);
      };
      if (!QMetaObject::invokeMethod(edit, "activateEditTool", Q_ARG(QVariant, QVariant(QStringLiteral("layers")))))
        return fail("Canvas proof could not activate the Layers tool");
      if (!wait_for_condition([&] { return !edit->property("editToolTransitionActive").toBool(); }, 5000))
        return fail("Canvas proof timed out while opening the Layers tool");
      auto* base_lock_button = find_visible_canvas_item(QStringLiteral("baseImageLayerLockButton"));
      if (!base_lock_button) {
        std::cerr << "Canvas proof could not find a visible Base Image lock control: "
                  << describe_canvas_item_candidates(QStringLiteral("baseImageLayerLockButton")).toStdString() << '\n';
        return fail("The visible Base Image lock control should be available before erasing");
      }
      if (!canvas_click_item(base_lock_button)) {
        std::cerr << "Canvas proof could not click the Base Image lock control: "
                  << describe_canvas_item_candidates(QStringLiteral("baseImageLayerLockButton")).toStdString()
                  << " editor=" << editor_preview.assetId().toStdString() << " hasAsset=" << editor_preview.hasAsset()
                  << " status=" << editor_preview.status().toStdString() << '\n';
        return fail("The visible Base Image lock control should accept a pointer click");
      }
      if (!wait_for_condition([&] { return !editor_preview.baseImageLocked(); }, 5000))
        return fail("Clicking the Base Image lock control should unlock the source layer");
      if (!canvas_click(QStringLiteral("editRailEraserButton")))
        return fail("The dedicated Eraser should accept a pointer click after unlocking Base Image");
      QQuickItem* unlocked_base_row = nullptr;
      if (!wait_for_condition([&] {
            unlocked_base_row = find_visible_canvas_item(QStringLiteral("baseImageLayerRow"));
            const auto unlocked_base_layers = editor_preview.photoLayers();
            return unlocked_base_row &&
                find_visible_canvas_item(QStringLiteral("baseImageLayerMoveUpButton")) &&
                find_visible_canvas_item(QStringLiteral("baseImageLayerMoveDownButton")) &&
                editor_preview.photoLayerCount() == 1 && unlocked_base_layers.size() == 1 &&
                unlocked_base_layers.front().toMap().value(QStringLiteral("copyBaseImage")).toBool() &&
                unlocked_base_row->property("baseLayerContract").toString().contains(
                    QStringLiteral("source-node:true|movable:true"));
          }, 5000)) {
        std::cerr << "Unlocked Base Image row: "
                  << describe_canvas_item_candidates(QStringLiteral("baseImageLayerRow")).toStdString()
                  << " layerCount=" << editor_preview.photoLayerCount() << '\n';
        return fail("Unlocking should expose one movable Base Image stack row with direct move controls");
      }
      auto* base_eraser = find_named_item(studio_root, QStringLiteral("photoPaintOverlay"));
      if (!wait_for_condition([&] { return base_eraser && base_eraser->property("editable").toBool() &&
            base_eraser->property("layerIndex").toInt() == -1; }, 5000)) return fail("Eraser should target the unlocked base row");
      base_eraser->setProperty("brushSize", 64.0);
      base_eraser->setProperty("hardness", 1.0);
      base_eraser->setProperty("pressureSize", false);
      base_eraser->setProperty("clipSelection", false);
      const auto erase_point = base_eraser->mapToScene(QPointF(base_eraser->property("imageX").toDouble() +
          base_eraser->property("imageWidth").toDouble() * 0.5, base_eraser->property("imageY").toDouble() +
          base_eraser->property("imageHeight").toDouble() * 0.5));
      canvas_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
      canvas_pointer(QEvent::MouseButtonPress, erase_point);
      canvas_pointer(QEvent::MouseButtonRelease, erase_point);
      if (!wait_for_condition([&] { return !editor_preview.photoPaintBusy() && !editor_preview.processing() && !editor_preview.fullRenderRunning(); }, 15000) ||
          !editor_preview.photoPaintError().isEmpty()) return fail("The base eraser gesture should finish successfully");
      const auto base_export_path = QDir(proof_dir).filePath(QStringLiteral("base-erased-export.png"));
      if (!editor_preview.exportFlattenedPhoto(base_export_path) || !wait_for_condition([&] { return !editor_preview.photoExportBusy(); }, 15000))
        return fail("Export native base-image erasure");
      const QImage erased_base(base_export_path);
      QMouseEvent leave_canvas(QEvent::MouseMove, QPointF(20, 20), QPointF(studio_window->mapToGlobal(QPoint(20, 20))),
          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
      QCoreApplication::sendEvent(studio_window, &leave_canvas);
      // Inspect image pixels beneath the brush cursor, which can outlive a synthetic hover exit.
      base_eraser->setProperty("cursorVisible", false);
      process_layout_events(4);
      if (!wait_for_condition([&] { return !editor_preview.processing() && !editor_preview.fullRenderRunning(); }, 15000))
        return fail("Wait for the final base-image preview");
      auto* native_image = find_named_item(studio_root, QStringLiteral("editImageViewportNativePreviewImage"));
      std::cout << "native_canvas api=" << static_cast<int>(studio_window->rendererInterface()->graphicsApi())
                << " texture=" << editor_preview.displayPreviewNativeHandle()
                << " native_visible=" << (native_image && item_visible(native_image))
                << " native_size=" << (native_image ? native_image->width() : 0) << 'x' << (native_image ? native_image->height() : 0) << '\n';
      if (studio_window->rendererInterface()->graphicsApi() != QSGRendererInterface::Metal ||
          !native_image || !item_visible(native_image)) return fail("Canvas visual proof requires a real native Metal scene graph");
      const auto provider_pixels = editor_preview_image_provider->image(QStringLiteral("edited"));
      if (!provider_pixels.isNull()) provider_pixels.save(QDir(proof_dir).filePath(QStringLiteral("base-preview-provider.png")));
      if (erased_base.isNull() || erased_base.pixelColor(erased_base.width()/2, erased_base.height()/2).alpha() > 1 ||
          erased_base.pixelColor(10,10).alpha() != 255 || !capture_visual_proof(studio_window, studio_root, proof_dir,
            QStringLiteral("base-image-eraser"), 1440, 900, {QStringLiteral("baseImageLayerRow"), QStringLiteral("photoPaintOverlay")}, &studio_manifest))
        return fail("Base erasure should create a transparent hole and preserve the rest of the image");
      const auto display = studio_window->grabWindow();
      const auto display_hole = display.pixelColor(qRound(erase_point.x() * display.width() / studio_window->width()),
          qRound(erase_point.y() * display.height() / studio_window->height()));
      if (display_hole.red() > 120 || display_hole.blue() - display_hole.red() < 3) {
        std::cerr << "Displayed erase center=" << display_hole.name().toStdString() << '\n';
        return fail("The native Metal viewport must show the transparency grid through the erased base");
      }
      if (!wait_for_condition([&] { return !editor_preview.recipeSavePending(); }, 15000) ||
          !editor_preview.recipeSaveError().isEmpty()) {
        std::cerr << "Base save error: " << editor_preview.recipeSaveError().toStdString() << '\n';
        return fail("The base-image erasure should persist through the real catalog save path");
      }
      canvas_shortcut(Qt::Key_Z);
      if (!wait_for_condition([&] { return !editor_preview.processing(); }, 15000)) return fail("Undo native base erasure");
      if (!wait_for_condition([&] { return !editor_preview.recipeSavePending(); }, 15000)) return fail("Persist the original photo before creating a board document");
      QFile original_recipe_file(QDir(proof_dir).filePath(QStringLiteral("original-photo-recipe.json")));
      if (!original_recipe_file.open(QIODevice::WriteOnly) || original_recipe_file.write(
          photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview).toUtf8()) <= 0)
        return fail("Record the original photo recipe for independence verification");
      if (!QMetaObject::invokeMethod(studio_root, "switchToWorkspaceIndex",
          Q_ARG(QVariant, studio_root->property("libraryWorkspaceIndex"))) ||
          !wait_for_condition([&] { return item_visible(find_named_item(studio_root, QStringLiteral("libraryCarouselsCollectionButton"))); }, 15000) ||
          !canvas_click(QStringLiteral("libraryCarouselsCollectionButton")) ||
          !wait_for_condition([&] { return item_visible(find_named_item(studio_root, QStringLiteral("libraryNewBoardsButton"))); }, 5000) ||
          !canvas_click(QStringLiteral("libraryNewBoardsButton")))
        return fail("Create blank boards from Library");
      if (!wait_for_condition([&] {
            auto* dialog = find_named_object(studio_root, QStringLiteral("newCanvasDialog"));
            return dialog && dialog->property("opened").toBool();
          }, 5000))
        return fail("Library New Boards should open the size chooser");
      auto* new_canvas_dialog = find_named_object(studio_root, QStringLiteral("newCanvasDialog"));
      auto* advanced_button = find_named_item(studio_root, QStringLiteral("newCanvasAdvancedButton"));
      auto* dimensions_group = find_named_item(studio_root, QStringLiteral("newCanvasDimensionsGroup"));
      if (!item_visible(dimensions_group) || new_canvas_dialog->property("advancedExpanded").toBool())
        return fail("Canvas dimensions should be visible before expanding background options");
      if (!canvas_type(QStringLiteral("newCanvasNameField"), QStringLiteral("Native Canvas")) ||
          !advanced_button || !canvas_click(QStringLiteral("newCanvasAdvancedButton")) ||
          !wait_for_condition([&] {
            return new_canvas_dialog && new_canvas_dialog->property("advancedExpanded").toBool() &&
                   item_visible(dimensions_group);
          }, 3000))
        return fail("Background options should expand without hiding canvas dimensions");
      if (!canvas_type(QStringLiteral("newCanvasWidthField"), QStringLiteral("8192")) ||
          !canvas_type(QStringLiteral("newCanvasHeightField"), QStringLiteral("8192")) ||
          find_named_object(studio_root, QStringLiteral("newCanvasCreateButton"))->property("enabled").toBool())
        return fail("The canvas dialog should accept keyboard input and reject oversized documents");
      if (!canvas_type(QStringLiteral("newCanvasWidthField"), QStringLiteral("640")) ||
          !canvas_type(QStringLiteral("newCanvasHeightField"), QStringLiteral("400")) ||
          !canvas_click(QStringLiteral("newCanvasSwapDimensionsButton")) ||
          new_canvas_dialog->property("canvasWidth").toInt() != 400 ||
          new_canvas_dialog->property("canvasHeight").toInt() != 640 ||
          !canvas_click(QStringLiteral("newCanvasSwapDimensionsButton")) ||
          !capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("new-canvas-dialog"), 1440, 900,
            {QStringLiteral("newCanvasAdvancedButton"), QStringLiteral("newCanvasWidthField"),
             QStringLiteral("newCanvasHeightField"),
             QStringLiteral("newCanvasBackgroundField"), QStringLiteral("newCanvasCreateButton")}, &studio_manifest) ||
          !canvas_click(QStringLiteral("newCanvasCreateButton"))) return fail("Create a custom transparent canvas through the dialog");
      if (!wait_for_condition([&] {
            return !catalog_assets.blankCanvasBusy() && editor_preview.sourceImageWidth() == 640 &&
                editor_preview.sourceImageHeight() == 400 && editor_preview.assetId() != QStringLiteral("studio-photo") &&
                !editor_preview.processing() && !studio_root->property("workspaceTransitionActive").toBool();
          }, 45000)) {
        std::cerr << catalog_assets.errorMessage().toStdString() << " asset=" << editor_preview.assetId().toStdString()
                  << " size=" << editor_preview.sourceImageWidth() << "x" << editor_preview.sourceImageHeight()
                  << " busy=" << catalog_assets.blankCanvasBusy() << " processing=" << editor_preview.processing()
                  << " transition=" << studio_root->property("workspaceTransitionActive").toBool()
                  << " pending=" << studio_root->property("pendingWorkspaceIndex").toInt()
                  << " boards=" << studio_root->property("editBoardsActive").toBool() << "\n";
        return fail("The unified canvas document should save and open its first board");
      }
      const auto canvas_id = editor_preview.assetId();
      const auto canvas_path = studio_root->property("localEditorFilePath").toString();
      const QImage canvas_source(canvas_path);
      if (canvas_id.isEmpty() || canvas_source.size() != QSize(640, 400) || canvas_source.pixelColor(20,20).alpha() != 0 ||
          editor_preview.photoLayerCount() != 0 || !canvas_path.contains(QStringLiteral("sources")) || !studio_root->property("editBoardsActive").toBool() || carousel_document.slideCount() != 1)
        return fail("A new canvas creates one saved board with an owned transparent source");
      if (!canvas_click(QStringLiteral("editRailShapesButton"))) return fail("Choose Shapes after opening the blank board");
      auto* shape_overlay = find_named_item(studio_root, QStringLiteral("photoShapeCreateOverlay"));
      if (!wait_for_condition([&] { return item_visible(shape_overlay) && shape_overlay->isEnabled() &&
            !edit->property("editToolTransitionActive").toBool() && !editor_preview.fullRenderRunning(); }, 15000))
        return fail("New canvases should finish activating the shape tool before input");
      const auto start = shape_overlay->mapToScene(QPointF(shape_overlay->property("imageX").toDouble() +
          shape_overlay->property("imageWidth").toDouble() * 0.15, shape_overlay->property("imageY").toDouble() +
          shape_overlay->property("imageHeight").toDouble() * 0.2));
      const auto end = start + QPointF(shape_overlay->property("imageWidth").toDouble() * 0.3,
          shape_overlay->property("imageHeight").toDouble() * 0.45);
      canvas_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
      canvas_pointer(QEvent::MouseButtonPress, start);
      canvas_pointer(QEvent::MouseMove, end);
      canvas_pointer(QEvent::MouseButtonRelease, end);
      if (!wait_for_condition([&] { return editor_preview.photoLayerCount() == 1 && !editor_preview.processing(); }, 15000) ||
          !canvas_click(QStringLiteral("editRailTextButton"))) return fail("Draw a shape and activate Text in the new document");
      if (!wait_for_condition([&] { return !edit->property("editToolTransitionActive").toBool(); }, 5000))
        return fail("The canvas text tool should finish activating");
      auto* text_creation = find_named_item(studio_root, QStringLiteral("photoTextCreationOverlay"));
      if (!item_visible(text_creation)) return fail("The Text tool should be usable on a blank canvas");
      const auto text_start = text_creation->mapToScene(QPointF(text_creation->property("imageX").toDouble() +
          text_creation->property("imageWidth").toDouble() * 0.5, text_creation->property("imageY").toDouble() +
          text_creation->property("imageHeight").toDouble() * 0.3));
      canvas_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
      canvas_pointer(QEvent::MouseButtonPress, text_start);
      canvas_pointer(QEvent::MouseButtonRelease, text_start);
      QInputMethodEvent text_input;
      text_input.setCommitString(QStringLiteral("Canvas type"));
      if (!studio_window->activeFocusItem()) return fail("Canvas text creation should focus its text editor");
      QCoreApplication::sendEvent(studio_window->activeFocusItem(), &text_input);
      if (!canvas_click(QStringLiteral("photoTextDoneButton")) || !wait_for_condition([&] {
              return editor_preview.photoLayerCount() == 2 && !editor_preview.processing();
            }, 15000)) return fail("Type and commit editable text on a new canvas");
      studio_window->contentItem()->forceActiveFocus(Qt::ShortcutFocusReason);
      const auto tool_before_paste = edit->property("editTool").toString();
      const auto background_timeline_clip_count = timeline_document.clipCount();
      const auto background_timeline_clipboard_count = timeline_document.clipClipboardCount();
      canvas_shortcut(Qt::Key_C);
      if (!wait_for_condition([&] { return !editor_preview.photoLayerClipboardBusy() && editor_preview.hasCopiedPhotoLayers(); }, 15000))
        return fail("Cmd/Ctrl+C should copy the selected editable text layer");
      if (!wait_for_condition([&] {return edit->property("editActionFeedback").toString()==QStringLiteral("1 layer copied");},5000))
        return fail("Copy completion has a visible layer-count confirmation");
      canvas_shortcut(Qt::Key_V);
      if (!wait_for_condition([&] {return edit->property("recentlyPastedLayerIds").toList().size()==1 &&
              edit->property("editActionFeedback").toString()==QStringLiteral("1 layer pasted");},1000) ||
          edit->property("editTool").toString()!=tool_before_paste ||
          !capture_visual_proof(studio_window,studio_root,proof_dir,QStringLiteral("paste-feedback"),1440,900,
              {QStringLiteral("editActionFeedbackLabel")},&studio_manifest))
        return fail("Paste confirms its count, highlights its selected layer and preserves a compatible tool");
      if (!wait_for_condition([&] { return editor_preview.photoLayerCount() == 3 && !editor_preview.processing(); }, 15000))
        { std::cerr << "Paste diagnostic: active=" << studio_window->isActive() << " copied=" << editor_preview.hasCopiedPhotoLayers()
                    << " clipboard_busy=" << editor_preview.photoLayerClipboardBusy() << " layers=" << editor_preview.photoLayerCount()
                    << " focus=" << (studio_window->activeFocusItem() ? studio_window->activeFocusItem()->metaObject()->className() : "none") << "\n";
          return fail("Cmd/Ctrl+V should paste and select a fresh editable layer"); }
      if (editor_preview.photoLayers().at(0).toMap().value(QStringLiteral("text")) != QStringLiteral("Canvas type")) {
        std::cerr << "Canvas layers: " << QJsonDocument::fromVariant(editor_preview.photoLayers()).toJson(QJsonDocument::Compact).toStdString() << '\n';
        return fail("Layer paste should preserve the typed text");
      }
      auto* in_place_action = find_named_object(studio_root, QStringLiteral("pasteLayersInPlaceAction"));
      if (!in_place_action || !wait_for_condition([&] {
            return in_place_action->property("enabled").toBool() && !carousel_document.editBoardImageBusy();
          }, 15000))
        return fail("Paste in place should become available after the preceding board-image update finishes");
      canvas_shortcut(Qt::Key_V, true);
      if (!wait_for_condition([&] { return editor_preview.photoLayerCount() == 4 && !editor_preview.processing(); }, 15000)) {
        QVariant mask_shortcut_context;
        QVariant text_input_active;
        QVariant primary_shift_shortcut;
        const bool queried_mask_context = QMetaObject::invokeMethod(
            edit, "maskShortcutContextActive", Q_RETURN_ARG(QVariant, mask_shortcut_context),
            Q_ARG(QVariant, QVariant(true)));
        const bool queried_text_input = QMetaObject::invokeMethod(
            edit, "shortcutTextInputActive", Q_RETURN_ARG(QVariant, text_input_active));
        const bool queried_primary_shortcut = QMetaObject::invokeMethod(
            studio_root, "primaryShiftShortcut", Q_RETURN_ARG(QVariant, primary_shift_shortcut),
            Q_ARG(QVariant, QVariant(QStringLiteral("V"))));
        const auto requested_shortcut = QKeySequence(QStringLiteral("Ctrl+Shift+V"));
        auto* active_focus = studio_window->activeFocusItem();
        std::cerr << "In-place paste diagnostic: requested="
                  << (queried_primary_shortcut ? primary_shift_shortcut.toString().toStdString() : "unavailable")
                  << " event_modifiers=" << static_cast<int>(requested_shortcut[0].keyboardModifiers())
                  << " native_sequence=" << requested_shortcut.toString(QKeySequence::NativeText).toStdString()
                  << " mask_context=" << (queried_mask_context ? mask_shortcut_context.toBool() : false)
                  << " mask_context_query=" << queried_mask_context
                  << " text_input=" << (queried_text_input ? text_input_active.toBool() : false)
                  << " text_input_query=" << queried_text_input
                  << " edit_tool=" << edit->property("editTool").toString().toStdString()
                  << " active_mask=" << edit->property("hasActiveMask").toBool()
                  << " compare=" << editor_preview.compareMode()
                  << " copied=" << editor_preview.hasCopiedPhotoLayers()
                  << " clipboard_busy=" << editor_preview.photoLayerClipboardBusy()
                  << " action_enabled=" << (in_place_action && in_place_action->property("enabled").toBool())
                  << " board_image_busy=" << carousel_document.editBoardImageBusy()
                  << " action_shortcut=" << (in_place_action ? in_place_action->property("shortcut").toString().toStdString() : "missing")
                  << " layers=" << editor_preview.photoLayerCount()
                  << " pasted_ids=" << edit->property("recentlyPastedLayerIds").toList().size()
                  << " focus=" << (active_focus ? active_focus->metaObject()->className() : "none") << '\n';
        return fail("Cmd/Ctrl+Shift+V should paste a layer in place");
      }
      if (timeline_document.clipCount() != background_timeline_clip_count ||
          timeline_document.clipClipboardCount() != background_timeline_clipboard_count)
        return fail("A retained hidden Timeline must not consume Edit copy or paste shortcuts");
      canvas_shortcut(Qt::Key_Z);
      if (!wait_for_condition([&] { return editor_preview.photoLayerCount() == 3 && !editor_preview.processing(); }, 15000))
        return fail("A single Undo should remove a complete pasted layer");
      if (!capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("canvas-shape-text"), 1440, 900,
            {QStringLiteral("editImageViewport"), QStringLiteral("baseImageLayerRow")}, &studio_manifest))
        return fail("Capture the composed blank canvas in Edit");
      const auto output_path = QDir(proof_dir).filePath(QStringLiteral("canvas-export.png"));
      if (!editor_preview.exportFlattenedPhoto(output_path) || !wait_for_condition([&] { return !editor_preview.photoExportBusy(); }, 15000) ||
          !editor_preview.photoExportError().isEmpty() || QImage(output_path).pixelColor(20,20).alpha() != 0)
        return fail("Export the native canvas composition with transparency");
      const auto saved_recipe = photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview);
      if (!wait_for_condition([&] { return !editor_preview.recipeSavePending(); }, 15000)) {
        std::cerr << "Canvas save error: " << editor_preview.recipeSaveError().toStdString() << '\n';
        return fail("Canvas edits should finish saving to the catalog");
      }
      if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_EDIT_BOARDS")) {
#include "edit_boards_interactions.inc"
      }
      QFile recipe_file(QDir(proof_dir).filePath(QStringLiteral("canvas-recipe.json")));
      QFile manifest_file(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
      if (!recipe_file.open(QIODevice::WriteOnly) || recipe_file.write(saved_recipe.toUtf8()) <= 0 ||
          !manifest_file.open(QIODevice::WriteOnly) || manifest_file.write(QJsonDocument(studio_manifest).toJson(QJsonDocument::Indented)) <= 0)
        return fail("Save canvas workflow evidence");
      std::cout << "Native Canvas: saved one-board document, dialog input, shape gesture, text entry and alpha export passed\n";
      return EXIT_SUCCESS;

      studio_window->resize(1440, 900);
      auto* canvas_viewport = find_named_item(studio_root, QStringLiteral("editImageViewport"));
      if (!canvas_viewport) return fail("Canvas proof requires the Edit viewport");
      canvas_viewport->setProperty("zoom", 0.5);
      process_layout_events(8);
      quint64 canvas_pointer_timestamp = 1000;
      const auto canvas_pointer = [&](QEvent::Type type, const QPointF& point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QMouseEvent event(type, point, QPointF(studio_window->mapToGlobal(point.toPoint())),
            type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
            type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, modifiers);
        event.setTimestamp(canvas_pointer_timestamp);
        canvas_pointer_timestamp += 16;
        QCoreApplication::sendEvent(studio_window, &event);
        process_layout_events(3);
      };
      const auto canvas_click = [&](const QString& name) {
        auto* item = find_named_item(studio_root, name);
        if (!item_visible(item) || !item->isEnabled()) return false;
        studio_window->grabWindow();
        canvas_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
        const auto point = scene_rect(item).center();
        canvas_pointer(QEvent::MouseButtonPress, point);
        canvas_pointer(QEvent::MouseButtonRelease, point);
        return true;
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
        studio_window->raise();
        studio_window->requestActivate();
        wait_for_condition([&] {
          if (!studio_window->isActive()) { studio_window->raise(); studio_window->requestActivate(); }
          return studio_window->isActive();
        }, 5000);
        process_layout_events(6);
        auto* shortcut_surface = find_named_item(studio_root, QStringLiteral("editStudioWorkspace"));
        if (shortcut_surface) shortcut_surface->forceActiveFocus(Qt::ShortcutFocusReason);
        else studio_window->contentItem()->forceActiveFocus(Qt::ShortcutFocusReason);
        process_layout_events(2);
#ifdef Q_OS_MAC
        auto modifiers = Qt::KeyboardModifiers(Qt::MetaModifier);
#else
        auto modifiers = Qt::KeyboardModifiers(Qt::ControlModifier);
#endif
        if (key == Qt::Key_C) modifiers = QKeySequence(QKeySequence::Copy)[0].keyboardModifiers();
        if (key == Qt::Key_V) modifiers = QKeySequence(QKeySequence::Paste)[0].keyboardModifiers();
        if (key == Qt::Key_S) modifiers = QKeySequence(QKeySequence::Save)[0].keyboardModifiers();
        if (key == Qt::Key_Z && studio_root->property("boardsWorkspaceActive").toBool())
          modifiers = QKeySequence(shift ? QKeySequence::Redo : QKeySequence::Undo)[0].keyboardModifiers();
        if (shift) modifiers |= Qt::ShiftModifier;
        QKeyEvent press(QEvent::KeyPress, key, modifiers);
        QKeyEvent release(QEvent::KeyRelease, key, modifiers);
        QCoreApplication::sendEvent(studio_window, &press);
        QCoreApplication::sendEvent(studio_window, &release);
        process_layout_events(6);
      };
      if (!QMetaObject::invokeMethod(edit, "activateEditTool", Q_ARG(QVariant, QVariant(QStringLiteral("layers")))) ||
          !wait_for_condition([&] { return !edit->property("editToolTransitionActive").toBool(); }, 5000) ||
          !canvas_click(QStringLiteral("baseImageLayerLockButton")) || !wait_for_condition([&] { return !editor_preview.baseImageLocked(); }, 5000) ||
          !canvas_click(QStringLiteral("editRailEraserButton"))) return fail("Unlock Base Image and choose the dedicated Eraser");
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
      if (!canvas_click(QStringLiteral("editNewCanvasButton")) || !wait_for_condition([&] {
            auto* dialog = find_named_object(studio_root, QStringLiteral("newCanvasDialog"));
            return dialog && dialog->property("opened").toBool();
          }, 5000)) return fail("New Canvas should open from the Edit toolbar");
      if (!canvas_type(QStringLiteral("newCanvasNameField"), QStringLiteral("Native Canvas")) ||
          !canvas_type(QStringLiteral("newCanvasWidthField"), QStringLiteral("8192")) ||
          !canvas_type(QStringLiteral("newCanvasHeightField"), QStringLiteral("8192")) ||
          find_named_object(studio_root, QStringLiteral("newCanvasCreateButton"))->property("enabled").toBool())
        return fail("The canvas dialog should accept keyboard input and reject oversized documents");
      if (!canvas_type(QStringLiteral("newCanvasWidthField"), QStringLiteral("640")) ||
          !canvas_type(QStringLiteral("newCanvasHeightField"), QStringLiteral("400")) ||
          !capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("new-canvas-dialog"), 1440, 900,
            {QStringLiteral("newCanvasWidthField"), QStringLiteral("newCanvasHeightField"),
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
      canvas_shortcut(Qt::Key_V, true);
      if (!wait_for_condition([&] { return editor_preview.photoLayerCount() == 4 && !editor_preview.processing(); }, 15000))
        return fail("Cmd/Ctrl+Shift+V should paste a layer in place");
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

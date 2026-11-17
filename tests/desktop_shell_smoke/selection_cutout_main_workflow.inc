      // Exercise the primary action while selection refinement is still staged.
      if (!editor_preview.selectionEditSessionActive() ||
          !click_item(find_named_item(edit_workspace, "cutoutWorkingSelectionButton")) ||
          !wait_for_condition([&] { return editor_preview.photoLayerCount() == 1 &&
              !editor_preview.selectionEditSessionActive() && !editor_preview.photoPaintBusy(); }, 30000) || !followup_idle())
        return fail("Cutout must commit staged refinement and copy Base image to one independent layer");
      const auto cutout_recipe = photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview);
      const auto cutout = photo_editor::desktop::parse_edit_recipe_json(cutout_recipe).layers[0].toMap();
      const auto pixels = photo_editor::desktop::rasterize_photo_paint_layer(cutout);
      if (cutout.value("kind") != "pixel" || cutout.value("paintTiles").toMap().isEmpty() ||
          cutout.value("copyBaseImage").toBool() || cutout.value("selectionScoped").toBool() ||
          !cutout.value("selectionMaskSetId").toString().isEmpty() || pixels.isNull() ||
          pixels.pixelColor(pixels.width() / 10, pixels.height() / 10).alpha() != 0 ||
          pixels.pixelColor(pixels.width() / 2, pixels.height() / 2).alpha() < 250 ||
          editor_preview.workingSelectionSetId() != working_id)
        return fail("Cutout must contain transparent pixels outside the retained selection without adjustment scaffolding");
      bool feathered = false;
      for (int y = 0; y < pixels.height() && !feathered; ++y)
        for (int x = 0; x < pixels.width(); ++x) {
          const auto alpha = pixels.pixelColor(x, y).alpha();
          if (alpha > 0 && alpha < 255) { feathered = true; break; }
        }
      if (!feathered || !pixels.save(QDir(visual_proof_dir).filePath("transparent-cutout.png")))
        return fail("Independent cutout must bake feathered alpha into its pixels");
      if (!wait_for_condition([&] {
        auto* result_image = find_named_item(edit_workspace, "cutoutPreviewImage");
        return item_visible(find_named_item(edit_workspace, "cutoutResultPreview")) && result_image && result_image->property("status").toInt() == 1;
      }, 10000))
        return fail("Cutout must automatically show a color-managed transparency preview");
      if (!menu_action("cutoutPreviewBackgroundMenu", "light")) return fail("Light background must be reachable for inspecting fringes");
      QTest::mouseMove(root_window, QPoint(900, 60)); QTest::qWait(100);
      root_window->grabWindow().save(QDir(visual_proof_dir).filePath("cutout-on-light.png"));
      if (!menu_action("cutoutPreviewBackgroundMenu", "dark")) return fail("Dark background must be reachable for inspecting fringes");
      QTest::mouseMove(root_window, QPoint(900, 60)); QTest::qWait(100);
      root_window->grabWindow().save(QDir(visual_proof_dir).filePath("cutout-on-dark.png"));
      if (!menu_action("cutoutPreviewBackgroundMenu", "checker")) return fail("Return to transparency preview");
      QTest::mouseMove(root_window, QPoint(900, 60)); QTest::qWait(100);
      root_window->grabWindow().save(QDir(visual_proof_dir).filePath("cutout-preview.png"));
      const auto before_dismiss = editor_preview.currentAssetBoundAdjustmentPatchJson();
      if (!click_item(find_named_item(edit_workspace, "cutoutPreviewReturnButton")) ||
          !wait_for_condition([&] { return !item_visible(find_named_item(edit_workspace, "cutoutResultPreview")); }, 5000) ||
          editor_preview.currentAssetBoundAdjustmentPatchJson() != before_dismiss)
        return fail("Returning to composition must leave pixels, layer visibility and history unchanged");
      const auto cutout_export = QDir(visual_proof_dir).filePath("trimmed-cutout.png");
      const auto cutout_key = QStringLiteral("layer:") + editor_preview.photoLayers().at(0).toMap().value("id").toString();
      QVariant export_started;
      if (!QMetaObject::invokeMethod(edit_workspace, "exportWorkingSelectionCutout", Q_RETURN_ARG(QVariant, export_started),
          Q_ARG(QVariant, QVariant(cutout_export)), Q_ARG(QVariant, QVariant(cutout_key)), Q_ARG(QVariant, QVariant(true))) ||
          !export_started.toBool() || !wait_for_condition([&] { return !editor_preview.photoExportBusy(); }, 20000) ||
          !editor_preview.photoExportError().isEmpty() || QImage(cutout_export).isNull() ||
          QImage(cutout_export).width() >= pixels.width() ||
          editor_preview.currentAssetBoundAdjustmentPatchJson() != before_dismiss)
        return fail("PNG delivery must asynchronously trim transparency without modifying the document");
      root_window->grabWindow().save(QDir(visual_proof_dir).filePath("cutout-layer.png"));
      if (!send_window_key(root_window, Qt::Key_Z, Qt::ControlModifier) ||
          !wait_for_condition([&] { return editor_preview.photoLayerCount() == 0; }, 10000) ||
          editor_preview.workingSelectionSetId() != working_id ||
          !send_window_key(root_window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier) ||
          !wait_for_condition([&] { return editor_preview.photoLayerCount() == 1; }, 10000) || !followup_idle() ||
          photo_editor::desktop::rasterize_photo_paint_layer(editor_preview.photoLayers().at(0).toMap()) != pixels)
        return fail("One Undo must remove the cutout and Redo must restore its exact pixels");
      // The working selection survives switching targets and clearing activeMask.
      if (!QMetaObject::invokeMethod(edit_workspace, "activateBaseImageLayerTarget") || !followup_idle())
        return fail("Base image remains reachable after creating a cutout");
      editor_preview.setActiveMaskIndex(-1);
      if (editor_preview.hasActiveMask() || !editor_preview.hasWorkingSelection() ||
          !send_window_key(root_window, Qt::Key_J, Qt::ControlModifier) ||
          !wait_for_condition([&] { return editor_preview.photoLayerCount() == 2 && !editor_preview.photoPaintBusy(); }, 30000) ||
          !followup_idle() || photo_editor::desktop::rasterize_photo_paint_layer(editor_preview.photoLayers().at(0).toMap()) != pixels)
        return fail("Ctrl/Cmd+J must copy Base image from the retained working selection without an active mask");
      const bool second_preview = wait_for_condition([&] { return item_visible(find_named_item(edit_workspace, "cutoutResultPreview")); }, 5000);
      const bool escaped = second_preview && send_window_key(root_window, Qt::Key_Escape, Qt::NoModifier);
      const bool dismissed = escaped && wait_for_condition([&] { return editor_preview.cutoutPreview().isEmpty(); }, 5000);
      if (!second_preview || !escaped || !dismissed || !editor_preview.hasWorkingSelection()) {
        std::cerr << "second-preview=" << second_preview << " escape=" << escaped << " dismissed=" << dismissed
            << " working=" << editor_preview.hasWorkingSelection() << " target=" << edit_workspace->property("selectedEditTarget").toMap().value("layerId").toString().toStdString()
            << " preview-layer=" << editor_preview.cutoutPreview().value("layerId").toString().toStdString() << '\n';
        root_window->grabWindow().save(QDir(visual_proof_dir).filePath("second-preview-failure.png"));
        return fail("Escape must leave the result preview without deselecting or changing the document");
      }
      editor_preview.activateWorkingSelection();
      if (!QMetaObject::invokeMethod(edit_workspace, "activateEditTool", Q_ARG(QVariant, QVariant("masks"))) ||
          !followup_idle() || !click_item(find_named_item(edit_workspace, "selectionRefineAddBrush")) || !followup_idle())
        return fail("Refinement must be reachable after delivering a cutout");
      const auto retained_components = editor_preview.maskStack();
      const auto other_key = QStringLiteral("layer:") + editor_preview.photoLayers().at(1).toMap().value("id").toString();
      QVariant switched;
      if (!QMetaObject::invokeMethod(edit_workspace, "switchSelectionLayerTarget", Q_RETURN_ARG(QVariant, switched), Q_ARG(QVariant, QVariant(other_key))) ||
          !switched.toBool() || !editor_preview.selectionEditSessionActive() || edit_workspace->property("editTool") != "brush" ||
          editor_preview.maskStack() != retained_components ||
          !QMetaObject::invokeMethod(edit_workspace, "activateBaseImageLayerTarget") ||
          !editor_preview.selectionEditSessionActive() || edit_workspace->property("editTool") != "brush" ||
          editor_preview.maskStack() != retained_components)
        return fail("Layer and Base image target switching must retain the brush, staged session and exact selection components");
      if (!editor_preview.finishSelectionEditSession() || !followup_idle()) return fail("Retained refinement session must finish normally");
      const auto saved = photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview);
      QFile recipe_file(QDir(visual_proof_dir).filePath("cutout-workflow.json"));
      if (!recipe_file.open(QIODevice::WriteOnly) || recipe_file.write(saved.toUtf8()) <= 0)
        return fail("Cutout recipe must save");
      photo_editor::desktop::EditorPreviewController reopened;
      reopened.loadAsset("cutout-reopened", mask_pointer_source_photo, {}, {}, {}, {}, saved);
      if (!wait_for_condition([&] { return !reopened.processing(); }, 20000) || reopened.photoLayerCount() != 2 ||
          photo_editor::desktop::rasterize_photo_paint_layer(reopened.photoLayers().at(0).toMap()) != pixels)
        return fail("Cutout alpha and pixels must survive save and reopen");
      QJsonObject receipt{{"schema", "looksy-selection-cutout:v1"}, {"platform", QGuiApplication::platformName()},
          {"baseImageButton", true}, {"shortcutWithoutActiveMask", true}, {"featheredAlpha", true},
          {"undoRedoPixelsExact", true}, {"saveReopenPixelsExact", true}, {"resultPreview", true}, {"trimmedPngExport", true}, {"stableLayerNavigation", true}, {"nativePresentationQualified", false}};
      QFile receipt_file(QDir(visual_proof_dir).filePath("workflow-receipt.json"));
      if (!receipt_file.open(QIODevice::WriteOnly) || receipt_file.write(QJsonDocument(receipt).toJson()) <= 0)
        return fail("Cutout receipt must save");
      std::cout << "selection-cutout: Base image button, staged refine, transparent feathered pixels, undo/redo, Ctrl+J, save/reopen passed\n";
      return EXIT_SUCCESS;

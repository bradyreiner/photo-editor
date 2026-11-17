    const auto click_item = [&](QQuickItem* item) {
      if (!item_visible(item) || !item->isEnabled()) return false;
      const QPointer<QQuickItem> target(item);
      auto* window = item->window();
      if (!window || window->grabWindow().isNull()) return false;
      for (auto* ancestor = item->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
        if (ancestor->metaObject()->indexOfProperty("contentY") < 0 ||
            ancestor->metaObject()->indexOfProperty("contentHeight") < 0) continue;
        const auto bounds = item->mapRectToItem(ancestor, item->boundingRect());
        const auto delta = bounds.top() < 4 ? bounds.top() - 4 :
            bounds.bottom() > ancestor->height() - 4 ? bounds.bottom() - ancestor->height() + 4 : 0;
        const auto maximum = qMax(0.0, ancestor->property("contentHeight").toDouble() - ancestor->height());
        ancestor->setProperty("contentY", qBound(0.0, ancestor->property("contentY").toDouble() + delta, maximum));
      }
      if (window->grabWindow().isNull() || !target) return false;
      auto rect = scene_rect(target);
      for (auto* ancestor = target->parentItem(); ancestor; ancestor = ancestor->parentItem())
        if (ancestor->clip()) rect = rect.intersected(scene_rect(ancestor));
      rect = rect.intersected(QRectF(0, 0, window->width(), window->height()));
      if (rect.width() < 2 || rect.height() < 2) return false;
      const auto point = rect.center();
      QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint(), 10);
      process_layout_events();
      return true;
    };
    const auto followup_idle = [&] {
      return wait_for_condition([&] {
        return !editor_preview.processing() && !editor_preview.brushStrokeActive() &&
            !editor_preview.photoPaintBusy() && !editor_preview.selectionWorkBusy();
      }, 60000);
    };
    auto* guide = find_named_object(root, QStringLiteral("firstRunGuideDialog"));
    if (guide && (!QMetaObject::invokeMethod(guide, "close") ||
        !wait_for_condition([&] { return !guide->property("visible").toBool(); }, 5000)))
      return fail("Initial guidance must close before selection input");
    if (!QMetaObject::invokeMethod(edit_workspace, "activateEditTool", Q_ARG(QVariant, QVariant("masks"))) ||
        !wait_for_condition([&] { const auto* transition = find_named_object(edit_workspace, "editToolTransitionOverlay");
        return (!transition || !transition->property("visible").toBool()) &&
            item_visible(find_named_item(edit_workspace, "selectionSource_rectangle")); }, 5000) ||
        !click_item(find_named_item(edit_workspace, "selectionSource_rectangle")) || !followup_idle())
      return fail("Selection follow-up must create a rectangle through the actual selection toolbox");
    const auto working_id = editor_preview.workingSelectionSetId();
    auto* layers = find_named_item(edit_workspace, "photoLayersInspectorGroup");
    if (working_id.isEmpty() || editor_preview.photoLayerCount() != 0 || !item_visible(layers) ||
        editor_preview.maskPreviewDisplayMode() != QStringLiteral("outline")) {
      std::cerr << "selection-start id=" << working_id.toStdString() << " count=" << editor_preview.photoLayerCount()
          << " layers=" << bool(layers) << " visible=" << item_visible(layers)
          << " tab=" << edit_workspace->property("editInspectorTab").toString().toStdString()
          << " tool=" << edit_workspace->property("editTool").toString().toStdString()
          << " mode=" << editor_preview.maskPreviewDisplayMode().toStdString() << '\n';
      root_window->grabWindow().save(QDir(visual_proof_dir).filePath("selection-start-failure.png"));
      return fail("Selection must start with an outline, no extra layer, and a visible Layers stack");
    }
    if (!click_item(find_named_item(edit_workspace, "selectionRefineRemoveBrush")) || !followup_idle() ||
        !item_visible(layers) || editor_preview.activeMaskOperation() != QStringLiteral("subtract"))
      return fail("Remove brush must retain the layer stack and select subtract coverage");
    if (!click_item(find_named_item(edit_workspace, "selectionRefineAddBrush")) || !followup_idle())
      return fail("Add brush must remain reachable beside Remove brush");
    const auto components = editor_preview.maskCount();
    const auto brush = editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap();
    if (!brush.value("parentId").toString().isEmpty() || brush.value("brushRefineMode").toBool())
      return fail("Quick Add/Remove must paint direct coverage without a nested edge-refinement dependency");
    if (!click_item(find_named_item(edit_workspace, "selectionRefineAddBrush")) ||
        editor_preview.maskCount() != components)
      return fail("Repeated Add brush must reuse the current refine component");
    auto* softness = find_named_item(edit_workspace, "selectionQuickSoftness");
    if (!click_item(find_named_item(edit_workspace, "maskFirstInspectorGroupHeader")))
      return fail("More settings must expose secondary edge controls");
    auto* edge = find_named_item(edit_workspace, "selectionQuickEdgeShift");
    if (!wait_for_condition([&] {
          root_window->grabWindow();
          return item_visible(softness) && item_visible(edge);
        }, 5000) ||
        !QMetaObject::invokeMethod(softness, "valueDragged", Q_ARG(double, 0.08 * editor_preview.selectionEdgeControls().value("featherPixelScale").toDouble())) ||
        !QMetaObject::invokeMethod(edge, "valueDragged", Q_ARG(double, -8.0)) ||
        std::abs(editor_preview.selectionEdgeControls().value("feather").toDouble() - 0.08) > 1e-6 ||
        std::abs(editor_preview.selectionEdgeControls().value("shift").toDouble() + 8.0) > 1e-6)
    {
      root_window->grabWindow().save(QDir(visual_proof_dir).filePath("edge-controls-failure.png"));
      std::cerr << "softness=" << bool(softness) << "/" << item_visible(softness) << " edge=" << bool(edge) << "/" << item_visible(edge)
          << " expanded=" << edit_workspace->property("selectionAdvancedRefinementExpanded").toBool()
          << " values=" << QJsonDocument::fromVariant(editor_preview.selectionEdgeControls()).toJson().toStdString() << '\n';
      return fail("Feather and advanced edge shift must edit real coverage");
    }
    if (editor_preview.activeMaskKind() != "brush" ||
        editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value("id") != brush.value("id"))
      return fail("Quick edge controls must preserve the selected brush component");
    if (!click_item(find_named_item(edit_workspace, "selectionQuickSoftnessControl")) ||
        !wait_for_condition([&] { return editor_preview.selectionEdgeControls().value("feather").toDouble() > .15; }, 5000))
      return fail("Dragging the real feather control must update the edge target");
    root_window->grabWindow().save(QDir(visual_proof_dir).filePath("more-settings.png"));
    if (!click_item(find_named_item(edit_workspace, "maskFirstInspectorGroupHeader")))
      return fail("More settings must collapse back to the compact inspector");
    auto* brush_input = find_named_item(edit_workspace, "editImageViewportPanZoomInputHandler");
    auto* painted_area = find_named_item(edit_workspace, "editImageViewportSharedMaskOverlay");
    auto* image_viewport = find_named_item(edit_workspace, "editImageViewport");
    if (!wait_for_condition([&] {
      const auto* transition = find_named_object(edit_workspace, "editToolTransitionOverlay");
      return item_visible(brush_input) && brush_input->isEnabled() && item_visible(painted_area) &&
          image_viewport && image_viewport->property("canvasBrushActive").toBool() &&
          (!transition || !transition->property("visible").toBool());
    }, 5000)) {
      std::cerr << "brush-input=" << bool(brush_input) << " visible=" << item_visible(brush_input)
          << " painted=" << bool(painted_area) << " visible=" << item_visible(painted_area)
          << " viewport=" << bool(image_viewport) << " canvasBrush=" << (image_viewport && image_viewport->property("canvasBrushActive").toBool())
          << " tool=" << edit_workspace->property("editTool").toString().toStdString()
          << " kind=" << editor_preview.activeMaskKind().toStdString() << " index=" << editor_preview.activeMaskIndex() << '\n';
      root_window->grabWindow().save(QDir(visual_proof_dir).filePath("brush-input-failure.png"));
      return fail("Refine brush must enable the production canvas input");
    }
    const auto stroke_point = painted_area->mapToScene(QPointF(painted_area->width() * 0.42, painted_area->height() * 0.5)).toPoint();
    QTest::mousePress(root_window, Qt::LeftButton, Qt::NoModifier, stroke_point, 10);
    if (!editor_preview.brushStrokeActive()) return fail("Pointer press must start an actual refinement stroke");
    QTest::mouseRelease(root_window, Qt::LeftButton, Qt::NoModifier, stroke_point, 10);
    if (!followup_idle()) return fail("Refine stroke must commit before adjustment creation");
    if (editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value("brushStrokes").toList().size() != 1)
      return fail("The quick refinement stroke must commit real brush coverage");
    auto* raster_preview = find_named_item(edit_workspace, "editImageViewportLiveBrushRasterImage");
    if (!wait_for_condition([&] {
      return item_visible(raster_preview) && !raster_preview->property("source").toUrl().isEmpty() &&
          raster_preview->property("status").toInt() == 1;
    }, 15000)) return fail("Refining edges must display the actual asynchronously rendered matte");
    process_layout_events();
    QTest::mouseMove(root_window, QPoint(700, 60));
    QTest::qWait(250);
    if (!root_window->grabWindow().save(QDir(visual_proof_dir).filePath("selection-and-layers.png")))
      return fail("Selection follow-up must capture the actual editor layout");
    const auto menu_action = [&](const QString& button, const QString& key) {
      if (!click_item(find_named_item(edit_workspace, button))) return false;
      std::function<QQuickItem*(QQuickItem*)> find_action = [&](QQuickItem* item) -> QQuickItem* {
        if (item->isVisible() && item->property("itemKey").toString() == key) return item;
        for (auto* child : item->childItems()) if (auto* found = find_action(child)) return found;
        return nullptr;
      };
      const QPointer<QObject> popup(find_named_object(edit_workspace, button + "Popup"));
      if (!click_item(find_action(root_window->contentItem()))) return false;
      return wait_for_condition([&] { return !popup || !popup->property("visible").toBool(); }, 5000);
    };
    const auto preview = [&](const QString& key) { return menu_action("selectionRefineViewMenu", key); };
    const auto overlay_source = raster_preview->property("source");
    if (!preview("view:white-on-black") ||
        !wait_for_condition([&] { return editor_preview.maskPreviewMode() == "white-on-black" &&
            raster_preview->property("source") != overlay_source && !raster_preview->property("source").toUrl().isEmpty() &&
            raster_preview->property("status").toInt() == 1; }, 15000))
      return fail("Mask preview must show the current rendered alpha");
    QTest::mouseMove(root_window, QPoint(800, 280));
    QTest::qWait(200);
    const auto mask_capture = root_window->grabWindow();
    const auto outside = raster_preview->mapToScene(QPointF(raster_preview->width() * .1, raster_preview->height() * .1)).toPoint();
    const auto inside = raster_preview->mapToScene(QPointF(raster_preview->width() * .5, raster_preview->height() * .5)).toPoint();
    const auto capture_saved = mask_capture.save(QDir(visual_proof_dir).filePath("mask-feather-preview.png"));
    if (mask_capture.pixelColor(outside).lightness() > 5 || mask_capture.pixelColor(inside).lightness() < 250 || !capture_saved) {
      std::cerr << "mask outside=" << mask_capture.pixelColor(outside).name().toStdString()
          << " inside=" << mask_capture.pixelColor(inside).name().toStdString()
          << " imageRect=" << raster_preview->property("imageRect").toRectF().width()
          << " opacity=" << raster_preview->opacity() << '\n';
      return fail("Mask mode must display black background and full-strength white coverage independently of overlay opacity");
    }
    if (!preview("view:overlay"))
      return fail("Overlay preview remains accessible beside Mask");
    if (!preview("edge-aware") || !followup_idle() || !editor_preview.activeMaskBrushAutoMask() || !editor_preview.activeMaskBrushProtectEdges() ||
        !preview("edge-aware") || !followup_idle() || editor_preview.activeMaskBrushAutoMask())
      return fail("Edge-aware refinement must be a working, reversible brush choice under View");
    const auto* detail_viewport = find_named_item(edit_workspace, "editImageViewport");
    if (!preview("inspect") || !detail_viewport ||
        std::abs(detail_viewport->property("zoom").toDouble() - detail_viewport->property("oneToOneZoom").toDouble()) > .001)
      return fail("Inspect edges must show actual 100 percent source scale");
    if (!QMetaObject::invokeMethod(const_cast<QQuickItem*>(detail_viewport), "setFitMode", Q_ARG(QVariant, QVariant("fit"))))
      return fail("Return from edge inspection to the fitted canvas");
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SELECTION_CUTOUT_WORKFLOW")) {
#include "selection_cutout_main_workflow.inc"
    }
    if (!click_item(find_named_item(edit_workspace, "adjustWorkingSelectionButton")) ||
        !wait_for_condition([&] { return editor_preview.photoLayerCount() == 1 &&
            !editor_preview.selectionEditSessionActive() && !editor_preview.processing(); }, 20000))
      return fail("Adjust selection must finish refinement and create one masked layer");
    const auto grade_changed = editor_preview.setPhotoLayerAdjustmentValue(0, "exposure", 0.6);
    QFile grade_recipe(QDir(visual_proof_dir).filePath("grade-recipe.json"));
    if (grade_recipe.open(QIODevice::WriteOnly))
      grade_recipe.write(photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview).toUtf8());
    const auto grade_settled = followup_idle();
    if (!grade_changed || !grade_settled) {
      const auto failed_state = photo_editor::desktop::parse_edit_recipe_json(photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview));
      std::cerr << "grade changed=" << grade_changed << " settled=" << grade_settled
          << " processing=" << editor_preview.processing() << " paint=" << editor_preview.photoPaintBusy()
          << " selection=" << editor_preview.selectionWorkBusy() << " stroke=" << editor_preview.brushStrokeActive()
          << " kind=" << failed_state.layers.value(0).toMap().value("kind").toString().toStdString()
          << " locked=" << failed_state.layers.value(0).toMap().value("locked").toBool()
          << " status=" << editor_preview.status().toStdString() << '\n';
      std::cerr << QJsonDocument::fromVariant(photo_editor::desktop::EditorPreviewControllerTestAccess::pending_work(editor_preview)).toJson().toStdString();
      root_window->grabWindow().save(QDir(visual_proof_dir).filePath("grade-failure.png"));
      return fail("The masked adjustment must accept color edits");
    }
    const auto grade = photo_editor::desktop::parse_edit_recipe_json(photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview)).layers[0].toMap();
    if (grade.value("selectionMaskSetId").toString() == working_id ||
        editor_preview.workingSelectionSetId() != working_id)
      return fail("The adjustment mask must be independent of the retained selection");
    if (!editor_preview.addPaintLayer() || !followup_idle() ||
        !QMetaObject::invokeMethod(edit_workspace, "setActivePhotoLayer", Q_ARG(QVariant, QVariant(0))))
      return fail("Switching to another layer must remain available after adjustment creation");
    process_layout_events();
    if (!followup_idle()) return fail("The selected paint layer must settle before painting");
    const auto before_paint = editor_preview.currentAssetBoundAdjustmentPatchJson();
    if (editor_preview.workingSelectionSetId() != working_id ||
        !editor_preview.beginPhotoPaintStroke(0, {{"color", "#ef4932"}, {"size", 22.0},
            {"hardness", 1.0}, {"opacity", 1.0}, {"flow", 1.0}, {"pressureSize", false}}))
      return fail("The retained selection must clip painting on the newly selected layer");
    editor_preview.addPhotoPaintPoint(editor_preview.sourceImageWidth() * 0.5,
        editor_preview.sourceImageHeight() * 0.5);
    editor_preview.finishPhotoPaintStroke();
    if (!followup_idle()) return fail("Selection-clipped layer paint must settle");
    const auto painted = editor_preview.currentAssetBoundAdjustmentPatchJson();
    const auto undo_dispatched = painted != before_paint && send_window_key(root_window, Qt::Key_Z, Qt::ControlModifier);
    const auto undo_exact = undo_dispatched && wait_for_condition([&] {
      return editor_preview.currentAssetBoundAdjustmentPatchJson() == before_paint; }, 10000);
    const auto redo_exact = undo_exact && send_window_key(root_window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier) &&
        wait_for_condition([&] { return editor_preview.currentAssetBoundAdjustmentPatchJson() == painted; }, 10000);
    if (!redo_exact) {
      std::cerr << "history painted=" << (painted != before_paint) << " undoSent=" << undo_dispatched
          << " undoExact=" << undo_exact << " redoExact=" << redo_exact << '\n';
      for (const auto& pair : {qMakePair(QString("before-paint.json"), before_paint),
          qMakePair(QString("painted.json"), painted),
          qMakePair(QString("after-history.json"), editor_preview.currentAssetBoundAdjustmentPatchJson())}) {
        QFile file(QDir(visual_proof_dir).filePath(pair.first));
        if (file.open(QIODevice::WriteOnly)) file.write(pair.second.toUtf8());
      }
      return fail("Main Undo/Redo must restore the exact selection-clipped paint edit");
    }
    if (!followup_idle()) return fail("History replay must settle before saving");
    const auto saved = photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview);
    QFile saved_file(QDir(visual_proof_dir).filePath("selection-workflow.json"));
    if (!saved_file.open(QIODevice::WriteOnly) || saved_file.write(saved.toUtf8()) <= 0)
      return fail("Selection workflow recipe must save");
    saved_file.close();
    const auto exported = QDir(visual_proof_dir).filePath("selection-export.png");
    if (!editor_preview.exportFlattenedPhoto(exported)) return fail("Selection workflow must export through the controller");
    photo_editor::desktop::EditorPreviewController reopened;
    reopened.loadAsset("selection-followup-reopened", mask_pointer_source_photo, {}, {}, {}, {}, saved);
    if (!wait_for_condition([&] { return !reopened.processing(); }, 20000) ||
        reopened.workingSelectionSetId() != working_id)
      return fail("Reopened recipe must retain selection identity");
    const auto reexported = QDir(visual_proof_dir).filePath("selection-reopened-export.png");
    if (!reopened.exportFlattenedPhoto(reexported) || QImage(exported) != QImage(reexported))
      return fail("Save/reopen must preserve exported pixels exactly");
    QJsonObject receipt{{"schema", "looksy-selection-followup:v1"}, {"platform", QGuiApplication::platformName()},
        {"layersVisibleDuringSelection", true}, {"quickRefinement", true}, {"selectionRetainedAcrossLayers", true},
        {"undoRedoExact", true}, {"saveReopenExportExact", true}, {"nativePresentationQualified", false}};
    QFile receipt_file(QDir(visual_proof_dir).filePath("workflow-receipt.json"));
    if (!receipt_file.open(QIODevice::WriteOnly) || receipt_file.write(QJsonDocument(receipt).toJson()) <= 0)
      return fail("Selection workflow receipt must save");
    std::cout << "selection-followup: production Main controls, refine, layers, paint, undo/redo, save/reopen/export passed\n";
    return EXIT_SUCCESS;

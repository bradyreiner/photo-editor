    {
      studio_window->resize(1440, 900);
      if (!QMetaObject::invokeMethod(edit, "setInterfaceDensity", Q_ARG(QVariant, QVariant(QStringLiteral("balanced")))) ||
          !invoke_edit_tool(QStringLiteral("masks")))
        return fail("Selection tools should open their right-side panel");
      auto* selection_sources = find_named_item(studio_root, QStringLiteral("creativeSelectionSourcesHost"));
      auto* selection_commands = find_named_item(studio_root, QStringLiteral("maskCreationToolbar"));
      auto* selection_settings = find_named_item(studio_root, QStringLiteral("maskFirstInspectorGroup"));
      auto* rectangle_source = find_named_item(studio_root, QStringLiteral("selectionSource_rectangle"));
      auto* combine_subtract = find_named_item(studio_root, QStringLiteral("maskCombineSubtractButton"));
      auto* combine_add = find_named_item(studio_root, QStringLiteral("maskCombineAddButton"));
      auto* selection_done = find_named_item(studio_root, QStringLiteral("maskCreationDoneButton"));
      auto* draw_menu = find_named_item(studio_root, QStringLiteral("maskBrushShapeMenuButton"));
      auto* object_source = find_named_item(studio_root, QStringLiteral("selectionSource_object"));
      auto* subject_source = find_named_item(studio_root, QStringLiteral("selectionSource_subject"));
      if (!object_source || !subject_source || !click_studio_item(object_source) ||
          !edit->property("objectPickArmed").toBool() ||
          !find_named_item(studio_root, QStringLiteral("selectionObjectBoxMode")))
        return fail("Individual object selection should be directly accessible beside Subject");
      auto* box_mode = find_named_item(studio_root, QStringLiteral("selectionObjectBoxMode"));
      if (box_mode->isEnabled()) {
        if (!click_studio_item(box_mode) || !edit->property("objectPickBoxMode").toBool())
          return fail("Object box mode should be reachable by pointer");
        const auto origin = tool_viewport->mapToScene(QPointF(
            tool_viewport->property("paintedContentX").toDouble(),
            tool_viewport->property("paintedContentY").toDouble()));
        const auto width = tool_viewport->property("paintedContentWidth").toDouble();
        const auto height = tool_viewport->property("paintedContentHeight").toDouble();
        paint_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
        send_paint_pointer(QEvent::MouseButtonPress, origin + QPointF(width * 0.35, height * 0.30));
        send_paint_pointer(QEvent::MouseMove, origin + QPointF(width * 0.65, height * 0.70));
        auto* box_outline = find_named_item(studio_root, QStringLiteral("selectionObjectBoxPreview"));
        if (!box_outline || !item_visible(box_outline) ||
            !capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("object-box-drag"),
                                 1440, 900, {QStringLiteral("selectionObjectBoxPreview")}, &studio_manifest))
          return fail("Dragging an object box should show its image-aligned bounds");
        send_paint_pointer(QEvent::MouseButtonRelease, origin + QPointF(width * 0.65, height * 0.70));
        const auto preview_ready = wait_for_condition([&] { return editor_preview.smartSelectPreviewActive(); }, 15000);
        if (!preview_ready || !click_studio_item(selection_done) ||
            !wait_for_condition([&] { return !edit->property("objectPickArmed").toBool() &&
                editor_preview.hasActiveMask(); }, 15000)) {
          std::cerr << "Box previewReady=" << preview_ready << " armed=" << edit->property("objectPickArmed").toBool()
                    << " status=" << editor_preview.status().toStdString()
                    << " doneVisible=" << item_visible(selection_done) << " doneEnabled=" << selection_done->isEnabled() << '\n';
          studio_window->grabWindow().save(QDir(proof_dir).filePath(QStringLiteral("object-box-accept-failure.png")));
          return fail("Object box preview should be accepted through Use selection");
        }
        const auto accepted = editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap();
        const auto prompts = accepted.value(QStringLiteral("generatedMaskMetadata")).toMap()
            .value(QStringLiteral("smartSelectionPrompts")).toList();
        if (prompts.size() != 1 || prompts.at(0).toMap().value(QStringLiteral("type")).toString() != QStringLiteral("box"))
          return fail("Accepted object selection should retain its exact box for regeneration");
        if (!invoke_edit_tool(QStringLiteral("masks"))) return fail("Selection panel should reopen after accepting an object");
      }
      if (edit->property("objectPickArmed").toBool() &&
          !click_studio_item(find_named_item(studio_root, QStringLiteral("selectionObjectCancelButton"))))
        return fail("Object selection should return to the other tools without modifying the image");
      if (!selection_sources || !selection_commands || !selection_settings || !rectangle_source ||
          !combine_subtract || !combine_add || !selection_done || !draw_menu ||
          !wait_for_condition([&] { return item_visible(rectangle_source) && item_visible(selection_commands); }, 5000) ||
          !scene_rect(creative_tools_surface).contains(scene_rect(rectangle_source)) ||
          !scene_rect(creative_tools_surface).contains(scene_rect(selection_commands)) ||
          !click_studio_item(rectangle_source) ||
          !wait_for_condition([&] { return editor_preview.activeMaskKind() == QStringLiteral("rectangle"); }, 5000) ||
          !click_studio_item(combine_subtract) || edit->property("maskCreationOperation").toString() != QStringLiteral("subtract") ||
          !click_studio_item(combine_add))
      {
        std::cerr << "Selection entry: kind=" << edit->property("creativeToolKind").toString().toStdString()
                  << " source=" << bool(rectangle_source) << " visible=" << item_visible(rectangle_source)
                  << " commands=" << item_visible(selection_commands)
                  << " mask=" << editor_preview.activeMaskKind().toStdString()
                  << " operation=" << edit->property("maskCreationOperation").toString().toStdString()
                  << " sourceBounds=" << (rectangle_source ? scene_rect(rectangle_source).x() : -1) << ','
                  << (rectangle_source ? scene_rect(rectangle_source).y() : -1)
                  << " commandBounds=" << (selection_commands ? scene_rect(selection_commands).x() : -1) << ','
                  << (selection_commands ? scene_rect(selection_commands).y() : -1) << '\n';
        studio_window->grabWindow().save(QDir(proof_dir).filePath(QStringLiteral("selection-entry-failure.png")));
        return fail("Selection sources and combine actions should work inside the right panel");
      }
      for (const auto& density : densities) {
        for (const auto& size : {QSize(1180, 768), QSize(1440, 900)}) {
        if (!QMetaObject::invokeMethod(edit, "setInterfaceDensity", Q_ARG(QVariant, QVariant(density))) ||
            !capture_visual_proof(studio_window, studio_root, proof_dir,
              QStringLiteral("selection-sidebar-%1-%2").arg(density).arg(size.width()), size.width(), size.height(),
              {QStringLiteral("creativeToolsSurface"), QStringLiteral("creativeSelectionSourcesHost"),
               QStringLiteral("selectionSource_object"), QStringLiteral("selectionSource_subject"),
               QStringLiteral("maskCreationToolbar"), QStringLiteral("maskBrushShapeMenuButton")}, &studio_manifest))
          return fail("Selection controls should render at every supported density");
        }
      }
      auto* refine_group = find_named_item(studio_root, QStringLiteral("creativeSelectionRefineGroup"));
      auto* feather_control = find_named_item(studio_root, QStringLiteral("maskFirstFeatherSlider"));
      if (!refine_group || !feather_control || refine_group->childItems().isEmpty() ||
          !click_studio_item(refine_group->childItems().constFirst()) ||
          !wait_for_condition([&] { return refine_group->property("expanded").toBool() &&
              item_visible(feather_control); }, 5000) ||
          !capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("selection-refine-sidebar"),
              1440, 900, {QStringLiteral("creativeSelectionRefineGroup"), QStringLiteral("maskFirstFeatherSlider")},
              &studio_manifest) || !click_studio_item(refine_group->childItems().constFirst()))
        return fail("Feather and edge refinement should expand in the selection panel");
      auto* polygon_source = find_named_item(studio_root, QStringLiteral("selectionSource_polygon"));
      auto* polygon_controls = find_named_item(studio_root, QStringLiteral("polygonToolHud"));
      if (!polygon_source || !polygon_controls || !click_studio_item(polygon_source) ||
          !wait_for_condition([&] { return editor_preview.activeMaskKind() == QStringLiteral("polygon") &&
              item_visible(polygon_controls); }, 5000) ||
          polygon_controls->parentItem() == tool_viewport ||
          !capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("polygon-sidebar"), 1440, 900,
            {QStringLiteral("polygonToolHud"), QStringLiteral("polygonHudCompleteButton"),
             QStringLiteral("polygonHudCancelButton")}, &studio_manifest) || !click_studio_item(rectangle_source))
        return fail("Polygon path commands should appear in the same right-side panel");
      if (!click_studio_item(creative_tools_float) ||
          !wait_for_condition([&] { return selection_commands->window() != studio_window &&
            item_visible(selection_commands) && selection_settings->window() == selection_commands->window(); }, 5000) ||
          !click_studio_item(draw_menu))
        return fail("Selection sources, commands, and settings should move into the same floating window");
      auto* draw_popup = find_named_object(studio_root, QStringLiteral("maskBrushShapeMenuButtonPopup"));
      if (!draw_popup || !wait_for_condition([&] { return draw_popup->property("opened").toBool(); }, 3000))
        return fail("Selection source menus should open from the floating panel");
      QMetaObject::invokeMethod(draw_popup, "close");
      if (!wait_for_condition([&] { return !draw_popup->property("visible").toBool(); }, 3000) ||
          !click_studio_item(creative_tools_float) ||
          !wait_for_condition([&] { return selection_commands->window() == studio_window; }, 5000))
        return fail("Selection tools should dock back into the sidebar");
      // The destination window regains focus on the deferred reparent callback.
      process_layout_events(8);
      const auto done_clicked = click_studio_item(selection_done);
      if (!done_clicked ||
          !wait_for_condition([&] { return edit->property("creativeToolKind").toString() != QStringLiteral("selection"); }, 5000) ||
          selection_settings->parentItem()->objectName() != QStringLiteral("creativeSelectionSettingsHost") ||
          item_visible(selection_settings)) {
        std::cerr << "Selection exit: tool=" << edit->property("creativeToolKind").toString().toStdString()
                  << " settingsParent=" << selection_settings->parentItem()->objectName().toStdString()
                  << " settingsVisible=" << item_visible(selection_settings)
                  << " doneVisible=" << item_visible(selection_done)
                  << " doneClicked=" << done_clicked << " doneEnabled=" << selection_done->isEnabled()
                  << " editTool=" << edit->property("editTool").toString().toStdString()
                  << " transition=" << edit->property("editToolTransitionActive").toBool()
                  << " preview=" << editor_preview.smartSelectPreviewActive()
                  << " status=" << editor_preview.status().toStdString()
                  << " doneBounds=" << scene_rect(selection_done).x() << ',' << scene_rect(selection_done).y() << '\n';
        studio_window->grabWindow().save(QDir(proof_dir).filePath(QStringLiteral("selection-exit-failure.png")));
        return fail("Selection Done should exit while keeping selection properties out of Layers");
      }

      if (!invoke_edit_tool(QStringLiteral("text")))
        return fail("New text should open the typography creation panel");
      auto* text_creation = find_named_item(studio_root, QStringLiteral("photoTextCreateControls"));
      auto* text_draft = find_named_item(studio_root, QStringLiteral("photoTextCreationInput"));
      auto* text_size = find_named_item(studio_root, QStringLiteral("photoTextCreationSize"));
      auto* text_creation_overlay = find_named_item(studio_root, QStringLiteral("photoTextCreationOverlay"));
      auto* text_input = find_named_item(studio_root, QStringLiteral("photoCanvasTextInput"));
      auto* text_finish = find_named_item(studio_root, QStringLiteral("photoTextCreationDone"));
      if (!text_creation || !text_draft || !text_size || !text_input || !text_finish || !text_creation_overlay ||
          !item_visible(text_creation) || !scene_rect(creative_tools_surface).contains(scene_rect(text_creation)))
        return fail("Text creation choices should be available in the right panel before placing text");
      text_draft->setProperty("text", QStringLiteral("Optical studio"));
      text_size->setProperty("value", 72);
      QMetaObject::invokeMethod(text_size, "valueModified");
      text_creation_overlay->setProperty("creationColor", QColor(QStringLiteral("#76ddc8")));
      for (const auto& density : densities) {
        for (const auto& size : {QSize(1180, 768), QSize(1440, 900)}) {
        if (!QMetaObject::invokeMethod(edit, "setInterfaceDensity", Q_ARG(QVariant, QVariant(density))) ||
            !capture_visual_proof(studio_window, studio_root, proof_dir,
              QStringLiteral("text-creation-sidebar-%1-%2").arg(density).arg(size.width()), size.width(), size.height(),
              {QStringLiteral("photoTextCreateControls"), QStringLiteral("photoTextCreationInput"),
               QStringLiteral("photoTextCreationFont"), QStringLiteral("photoTextCreationSize")}, &studio_manifest))
          return fail("Text creation controls should render at all densities");
        }
      }
      const auto layer_count_before_text = editor_preview.photoLayerCount();
      const auto origin = tool_viewport->mapToScene(QPointF(tool_viewport->property("paintedContentX").toDouble(),
          tool_viewport->property("paintedContentY").toDouble()));
      const QPointF text_position = origin + QPointF(tool_viewport->property("paintedContentWidth").toDouble() * 0.35,
          tool_viewport->property("paintedContentHeight").toDouble() * 0.35);
      paint_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
      send_paint_pointer(QEvent::MouseButtonPress, text_position);
      send_paint_pointer(QEvent::MouseButtonRelease, text_position);
      if (!wait_for_condition([&] { return item_visible(text_finish) && text_input->property("text").toString() ==
              QStringLiteral("Optical studio"); }, 5000) || !click_studio_item(text_finish) ||
          !wait_for_condition([&] { return editor_preview.photoLayerCount() == layer_count_before_text + 1 &&
              !editor_preview.processing(); }, 15000))
        return fail("Canvas placement and sidebar Done should create the prepared text draft");
      const auto created_text = editor_preview.photoLayers().at(0).toMap();
      if (created_text.value(QStringLiteral("text")).toString() != QStringLiteral("Optical studio") ||
          std::abs(created_text.value(QStringLiteral("fontSize")).toDouble() - 72) > 0.01 ||
          QColor(created_text.value(QStringLiteral("textColor")).toString()) != QColor(QStringLiteral("#76ddc8")))
        return fail("Prepared text style should persist");
      editor_preview.undo();
      if (editor_preview.photoLayerCount() != layer_count_before_text)
        return fail("One undo should remove the complete prepared text creation");
      creative_interaction_metrics.append(QJsonObject{{QStringLiteral("interaction"), QStringLiteral("right-sidebar-tools")},
        {QStringLiteral("selection_sources_combine_float_done"), true},
        {QStringLiteral("text_prepared_placement_single_undo"), true},
        {QStringLiteral("density_count"), densities.size()}});
      if (!invoke_edit_tool(QStringLiteral("layers"))) return fail("Sidebar proof should restore layer editing");
    }

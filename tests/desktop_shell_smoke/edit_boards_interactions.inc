        const auto choose_board_add = [&](int option) {
          if (!canvas_click(QStringLiteral("editCanvasAddBoardButtonPrimaryAction"))) return false;
          const auto row = QStringLiteral("editCanvasAddBoardButtonPopupItem") + QString::number(option);
          if (!wait_for_condition([&] { return item_visible(find_named_item(studio_root, row)); }, 5000)) return false;
          return canvas_click(row);
        };
        const auto board_dialog_closed = [&] {
          auto* dialog = find_named_object(studio_root, QStringLiteral("editBoardDialog"));
          return dialog && !dialog->property("visible").toBool();
        };
        if (!choose_board_add(0) || !wait_for_condition([&] {
              return studio_root->property("editBoardsActive").toBool() &&
                  carousel_document.slideCount() == 2 && carousel_document.currentIndex() == 1 &&
                  !editor_preview.processing() && !editor_preview.fullRenderRunning();
            }, 15000)) return fail("The canvas-side plus adds and selects a new blank board in Edit");
        const auto board_id = carousel_document.savedDocumentId();
        auto second_base = editor_preview.assetId();
        const auto second_source = studio_root->property("localEditorFilePath").toString();
        if (board_id.isEmpty() || editor_preview.photoLayerCount() != 0 || editor_preview.catalogRecipePersistenceEnabled() ||
            QImage(second_source).pixelColor(20,20).alpha() != 0 ||
            find_named_item(studio_root, QStringLiteral("carouselWorkspace")) ||
            find_named_item(studio_root, QStringLiteral("carouselBoardStrip")))
          return fail("Boards use the existing Edit tools, independent transparent base images and no Carousel UI");
        if (!QMetaObject::invokeMethod(studio_root,"activateEditBoard",Q_ARG(QVariant,QVariant(0))) || !wait_for_condition([&] {
              return carousel_document.currentIndex() == 0 && editor_preview.photoLayerCount() == 3 && !editor_preview.processing();
            }, 15000) || editor_preview.assetId() == second_base)
          return fail("Clicking Board 1 selects its own editable layers and base image");
        canvas_shortcut(Qt::Key_V);
        if (!wait_for_condition([&] { return editor_preview.photoLayerCount() == 4 && !editor_preview.processing(); }, 15000))
          return fail("The normal Edit layer paste works directly on an active board");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] { return editor_preview.photoLayerCount() == 3 && !editor_preview.processing(); }, 15000))
          return fail("Undo targets the active board's photo-layer history");
        canvas_shortcut(Qt::Key_Z, true);
        if (!wait_for_condition([&] { return editor_preview.photoLayerCount() == 4 && !editor_preview.processing(); }, 15000))
          return fail("Redo restores the active board's pasted layer");
        const auto before_alignment = editor_preview.currentAssetBoundAdjustmentPatchJson();
        edit->setProperty("selectedPhotoLayerIndices", QVariantList{0, 1, 2});
        process_layout_events(8);
        if (!canvas_click(QStringLiteral("photoLayerAlignSelectionButtonPrimaryAction")) ||
            !wait_for_condition([&] { return item_visible(find_named_item(studio_root,QStringLiteral("photoLayerAlignSelectionButtonPopupItem1"))) &&
                find_named_object(studio_root,QStringLiteral("photoLayerAlignSelectionButtonPopup"))->property("opened").toBool(); },5000) ||
            !capture_visual_proof(studio_window,studio_root,proof_dir,QStringLiteral("layer-alignment-menu"),1440,900,
                {QStringLiteral("photoLayerAlignSelectionButton")},&studio_manifest) ||
            !canvas_click(QStringLiteral("photoLayerAlignSelectionButtonPopupItem1")))
          return fail("Selected layers expose a contextual alignment menu");
        const auto center_x = [&](int index) {
          const auto layer = editor_preview.photoLayers().at(index).toMap();
          return layer.value("x").toDouble() + layer.value("pixelWidth").toDouble() * layer.value("scale").toDouble() / 2;
        };
        if (!wait_for_condition([&] { return !editor_preview.processing() &&
              std::abs(center_x(0)-center_x(1))<=1 && std::abs(center_x(0)-center_x(2))<=1; },15000))
          return fail("Align centers updates all selected layers");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] { return !editor_preview.processing() &&
              editor_preview.currentAssetBoundAdjustmentPatchJson()==before_alignment; },15000))
          return fail("One undo restores the entire alignment action");
        const auto distribution_layers = editor_preview.photoLayers();
        editor_preview.beginCoalescedEdit();
        for (int index=0;index<3;++index) {
          const auto layer=distribution_layers.at(index).toMap();
          editor_preview.setPhotoLayerTransform(index,index==0?20:index==1?200:500,
              layer.value("y").toInt(),layer.value("scale").toDouble(),layer.value("rotation").toDouble());
        }
        editor_preview.endCoalescedEdit();
        if (!wait_for_condition([&] {return !editor_preview.processing() && !editor_preview.fullRenderRunning();},15000))
          return fail("Distribution fixture settles");
        const auto before_distribution = editor_preview.currentAssetBoundAdjustmentPatchJson();
        edit->setProperty("selectedPhotoLayerIndices", QVariantList{0, 1, 2});
        if (!canvas_click(QStringLiteral("photoLayerAlignSelectionButtonPrimaryAction")) ||
            !wait_for_condition([&] {return item_visible(find_named_item(studio_root,QStringLiteral("photoLayerAlignSelectionButtonPopupItem6")));},5000) ||
            !canvas_click(QStringLiteral("photoLayerAlignSelectionButtonPopupItem6")) ||
            !wait_for_condition([&] {return !editor_preview.processing() && !editor_preview.fullRenderRunning();},15000))
          return fail("Distribute horizontal spacing runs from the alignment menu");
        const auto distributed = editor_preview.photoLayers();
        const auto right_edge = [&](int index) {const auto l=distributed.at(index).toMap();return l.value("x").toDouble()+l.value("pixelWidth").toDouble()*l.value("scale").toDouble();};
        const double gap1=distributed.at(1).toMap().value("x").toDouble()-right_edge(0);
        const double gap2=distributed.at(2).toMap().value("x").toDouble()-right_edge(1);
        if (std::abs(gap1-gap2)>1 || distributed.at(0).toMap().value("x").toInt()!=20 || distributed.at(2).toMap().value("x").toInt()!=500)
          return fail("Distribution equalizes gaps while preserving the outer layers");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return editor_preview.currentAssetBoundAdjustmentPatchJson()==before_distribution;},15000))
          return fail("One undo restores the distribution action");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return !editor_preview.processing() && editor_preview.currentAssetBoundAdjustmentPatchJson()==before_alignment;},15000))
          return fail("Restore the document after the temporary distribution fixture");
        edit->setProperty("selectedPhotoLayerIndices", QVariantList{0});
        const auto first_recipe = editor_preview.currentAssetBoundAdjustmentPatchJson();
        if (!canvas_click(QStringLiteral("editCanvasBoardSelect1")) || !wait_for_condition([&] {
              return editor_preview.assetId() == second_base && editor_preview.photoLayerCount() == 0 && !editor_preview.processing();
            }, 15000) || !canvas_click(QStringLiteral("editRailShapesButton")))
          return fail("Switching to Board 2 restores its independent empty layer stack");
        auto* board_shape = find_named_item(studio_root, QStringLiteral("photoShapeCreateOverlay"));
        if (!wait_for_condition([&] { return item_visible(board_shape) && !edit->property("editToolTransitionActive").toBool() &&
              !editor_preview.fullRenderRunning(); }, 15000)) return fail("The existing Edit shape tool activates on a board");
        const auto shape_start = board_shape->mapToScene(QPointF(board_shape->property("imageX").toDouble() +
            board_shape->property("imageWidth").toDouble() * 0.15, board_shape->property("imageY").toDouble() +
            board_shape->property("imageHeight").toDouble() * 0.2));
        const auto shape_end = shape_start + QPointF(board_shape->property("imageWidth").toDouble() * 0.3,
            board_shape->property("imageHeight").toDouble() * 0.3);
        canvas_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
        canvas_pointer(QEvent::MouseButtonPress, shape_start);
        canvas_pointer(QEvent::MouseMove, shape_end);
        canvas_pointer(QEvent::MouseButtonRelease, shape_end);
        if (!wait_for_condition([&] { return editor_preview.photoLayerCount() == 1 && !editor_preview.processing(); }, 15000) ||
            !canvas_click(QStringLiteral("editRailTextButton"))) return fail("Draw a shape with the unchanged Edit tool");
        if (!wait_for_condition([&] { return !edit->property("editToolTransitionActive").toBool(); }, 5000)) return fail("Board text tool activates");
        auto* board_text = find_named_item(studio_root, QStringLiteral("photoTextCreationOverlay"));
        const auto text_point = board_text->mapToScene(QPointF(board_text->property("imageX").toDouble() +
            board_text->property("imageWidth").toDouble() * 0.45, board_text->property("imageY").toDouble() +
            board_text->property("imageHeight").toDouble() * 0.5));
        canvas_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
        canvas_pointer(QEvent::MouseButtonPress, text_point);
        canvas_pointer(QEvent::MouseButtonRelease, text_point);
        QInputMethodEvent board_text_input;
        board_text_input.setCommitString(QStringLiteral("Second board"));
        if (!studio_window->activeFocusItem()) return fail("Board text creation focuses the existing Edit text editor");
        QCoreApplication::sendEvent(studio_window->activeFocusItem(), &board_text_input);
        if (!canvas_click(QStringLiteral("photoTextDoneButton")) || !wait_for_condition([&] {
              return editor_preview.photoLayerCount() == 2 && !editor_preview.processing(); }, 15000) ||
            !canvas_click(QStringLiteral("editRailPaintButton")) || !wait_for_condition([&] {
              return !edit->property("editToolTransitionActive").toBool(); }, 5000))
          return fail("The same Edit text and paint controls work on Board 2");
        auto* board_paint = find_named_item(studio_root, QStringLiteral("photoPaintOverlay"));
        if (!wait_for_condition([&] { return editor_preview.photoLayerCount() == 3 && board_paint->property("editable").toBool() &&
              !editor_preview.processing(); }, 15000)) {
          std::cerr << "board paint count=" << editor_preview.photoLayerCount()
                    << " tool=" << edit->property("editTool").toString().toStdString()
                    << " active=" << board_paint->property("active").toBool()
                    << " editable=" << board_paint->property("editable").toBool()
                    << " layer=" << board_paint->property("layerIndex").toInt()
                    << " kind=" << board_paint->property("layerData").toMap().value("kind").toString().toStdString()
                    << " processing=" << editor_preview.processing() << '\n';
          capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("board-paint-failure"), 1440, 900,
              {QStringLiteral("photoPaintNewLayerButton"), QStringLiteral("photoPaintControls")}, &studio_manifest);
          return fail("Board paint has its own new pixel layer");
        }
        const auto paint_point = board_paint->mapToScene(QPointF(board_paint->property("imageX").toDouble() +
            board_paint->property("imageWidth").toDouble() * 0.6, board_paint->property("imageY").toDouble() +
            board_paint->property("imageHeight").toDouble() * 0.75));
        canvas_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
        canvas_pointer(QEvent::MouseButtonPress, paint_point);
        canvas_pointer(QEvent::MouseMove, paint_point + QPointF(30, 0));
        canvas_pointer(QEvent::MouseButtonRelease, paint_point + QPointF(30, 0));
        if (!wait_for_condition([&] { return !editor_preview.photoPaintBusy() && !editor_preview.processing(); }, 15000) ||
            !editor_preview.photoPaintError().isEmpty()) return fail("Paint a native stroke on Board 2 with the existing brush");
        canvas_shortcut(Qt::Key_S);
        if (!wait_for_condition([&] { return !carousel_document.documentDirty(); }, 15000)) return fail("Save the active board's layers");
        if (!choose_board_add(1) || !wait_for_condition([&] { return carousel_document.slideCount() == 3 &&
              carousel_document.currentIndex() == 2 && !editor_preview.processing(); }, 15000) ||
            !carousel_document.slides().at(2).toMap().value("joinedPrevious").toBool())
          return fail("Extend adds an adjoining canvas using the same Edit tools");
        const auto shared_image_id = editor_preview.assetId();
        if (editor_preview.photoLayerCount() != 3 || shared_image_id == second_base)
          return fail("The extension opens the exact shared image and all original editable layers");
        second_base = shared_image_id;
        const auto shared_target = carousel_document.selectedPhotoEditPayload().value("targetKey");
        if (!canvas_click(QStringLiteral("editCanvasBoardHeaderSelect1")) || !wait_for_condition([&] {
              return editor_preview.assetId() == shared_image_id && editor_preview.photoLayerCount() == 3 &&
                  !editor_preview.processing() && !editor_preview.fullRenderRunning(); }, 15000) ||
            carousel_document.selectedPhotoEditPayload().value("targetKey") != shared_target)
          return fail("Selecting either tile keeps one live image and one editable layer stack");
        if (!canvas_click(QStringLiteral("editRailPaintButton")) || !wait_for_condition([&] {
              return !edit->property("editToolTransitionActive").toBool() && board_paint->property("editable").toBool(); }, 5000))
          return fail("Retain the existing brush tool on the shared image");
        const auto before_snapping=editor_preview.currentAssetBoundAdjustmentPatchJson();
        if (!QMetaObject::invokeMethod(edit,"setActivePhotoLayer",Q_ARG(QVariant,QVariant(2))) || !wait_for_condition([&] {
              return item_visible(find_named_item(studio_root,QStringLiteral("photoLayerTransformOverlay"))) &&
                  !editor_preview.processing() && !editor_preview.fullRenderRunning();},15000))
          return fail("Shared-board shape activates direct positioning handles");
        auto* snap_overlay=find_named_item(studio_root,QStringLiteral("photoLayerTransformOverlay"));
        edit->setProperty("layerSnappingEnabled",true);
        edit->setProperty("layerSafeMarginsEnabled",true);
        const auto snap_fixture=editor_preview.photoLayers().at(2).toMap();
        for (const auto modifiers : {Qt::NoModifier,Qt::AltModifier}) {
          const double factor=snap_overlay->property("viewportScaleX").toDouble();
          const double target=(canvas_viewport->property("imageFrameX").toDouble()+
              canvas_viewport->property("boardUnitWidth").toDouble()*canvas_viewport->property("zoom").toDouble()*1.5-
              canvas_viewport->property("paintedContentX").toDouble())/factor;
          const double desired_x=target-snap_overlay->property("layerWidth").toDouble()*snap_overlay->property("layerScale").toDouble()/2+2/factor;
          const auto start=canvas_viewport->mapToScene(QPointF(canvas_viewport->property("imageFrameX").toDouble()+
              canvas_viewport->property("boardUnitWidth").toDouble()*canvas_viewport->property("zoom").toDouble()*0.5,
              canvas_viewport->property("imageFrameY").toDouble()+canvas_viewport->property("boardUnitHeight").toDouble()*canvas_viewport->property("zoom").toDouble()*0.5));
          const auto end=start+QPointF((desired_x-snap_fixture.value("x").toDouble())*factor,0);
          canvas_pointer_timestamp+=static_cast<quint64>(QApplication::doubleClickInterval()+1);
          canvas_pointer(QEvent::MouseButtonPress,start,modifiers);
          canvas_pointer(QEvent::MouseMove,end,modifiers);
          process_layout_events(8);
          const auto moved=editor_preview.photoLayers().at(2).toMap();
          const double actual=moved.value("x").toDouble()+snap_overlay->property("layerWidth").toDouble()*snap_overlay->property("layerScale").toDouble()/2;
          if (modifiers==Qt::NoModifier) {
            if (std::abs(actual-target)>1 || !item_visible(find_named_item(studio_root,QStringLiteral("photoLayerSnapGuideX"))) ||
                !capture_visual_proof(studio_window,studio_root,proof_dir,QStringLiteral("board-snap-guide"),1440,900,
                    {QStringLiteral("photoLayerSnapGuideX"),QStringLiteral("editBoardSafeMargins1")},&studio_manifest))
              return fail("A native drag snaps to the next board center with a visible guide");
          } else if (std::abs(actual-target)*factor<1 || item_visible(find_named_item(studio_root,QStringLiteral("photoLayerSnapGuideX"))))
            return fail("Alt bypasses board snapping during a native drag");
          canvas_pointer(QEvent::MouseButtonRelease,end,modifiers);
          if (item_visible(find_named_item(studio_root,QStringLiteral("photoLayerSnapGuideX"))))
            return fail("Board snap guides disappear on release");
          if (!wait_for_condition([&] {return !editor_preview.processing() && !editor_preview.fullRenderRunning();},15000))
            return fail("Snapped preview settles");
          canvas_shortcut(Qt::Key_Z);
          if (!wait_for_condition([&] {return !editor_preview.processing() && editor_preview.currentAssetBoundAdjustmentPatchJson()==before_snapping;},15000))
            return fail("Undo restores a snapped or free drag in one step");
        }
        edit->setProperty("layerSafeMarginsEnabled",false);
        if (!QMetaObject::invokeMethod(edit,"setActivePhotoLayer",Q_ARG(QVariant,QVariant(0))) ||
            !canvas_click(QStringLiteral("editRailPaintButton")) || !wait_for_condition([&] {
              return edit->property("editTool").toString()==QStringLiteral("paint") &&
                  !edit->property("editToolTransitionActive").toBool() && board_paint->property("active").toBool() &&
                  board_paint->property("editable").toBool() && !editor_preview.processing() && !editor_preview.fullRenderRunning();},15000))
          return fail("Return to the same brush layer after positioning");
        // A real brush gesture crosses the board boundary in canonical source coordinates.
        const auto before_seam_recipe = editor_preview.currentAssetBoundAdjustmentPatchJson();
        const auto seam_center = board_paint->mapToScene(QPointF(board_paint->property("imageX").toDouble() +
            board_paint->property("imageWidth").toDouble() * 0.5, board_paint->property("imageY").toDouble() +
            board_paint->property("imageHeight").toDouble() * 0.5));
        canvas_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
        canvas_pointer(QEvent::MouseButtonPress, seam_center - QPointF(35,0));
        canvas_pointer(QEvent::MouseMove, seam_center + QPointF(35,0));
        canvas_pointer(QEvent::MouseButtonRelease, seam_center + QPointF(35,0));
        if (!wait_for_condition([&] { return !editor_preview.photoPaintBusy() && !editor_preview.processing() && !editor_preview.fullRenderRunning(); },15000) ||
            carousel_document.currentIndex() != 1 || !editor_preview.photoPaintError().isEmpty())
          return fail("Cross-seam brush input updates the shared recipe and retains the selected tile");
        const auto shared_recipe = editor_preview.currentAssetBoundAdjustmentPatchJson();
        if (shared_recipe == before_seam_recipe) {
          std::cerr << "seam active=" << board_paint->property("active").toBool() << " editable=" << board_paint->property("editable").toBool()
                    << " tool=" << edit->property("editTool").toString().toStdString() << " layer=" << board_paint->property("layerIndex").toInt()
                    << " drawing=" << board_paint->property("drawing").toBool() << " enabled=" << board_paint->isEnabled() << '\n';
          capture_visual_proof(studio_window,studio_root,proof_dir,QStringLiteral("seam-brush-failure"),1440,900,
              {QStringLiteral("photoPaintOverlay")},&studio_manifest);
          return fail("The cross-seam pointer gesture must commit actual brush pixels");
        }
        if (!canvas_click(QStringLiteral("editCanvasBoardHeaderSelect2")) ||
            editor_preview.assetId() != shared_image_id || editor_preview.currentAssetBoundAdjustmentPatchJson() != shared_recipe)
          return fail("A brush edit on one tile is immediately retained on its adjoining tile");
        if (!QMetaObject::invokeMethod(studio_root,"activateEditBoard",Q_ARG(QVariant,QVariant(1))))
          return fail("Return to the named board");
        const auto choose_board_action = [&](int index, const QString& key) {
          const auto name = QStringLiteral("editCanvasBoardActions") + QString::number(index);
          if (!wait_for_condition([&] {
                auto* menu = find_named_object(studio_root, name + QStringLiteral("Popup"));
                return board_dialog_closed() && (!menu || !menu->property("visible").toBool());
              }, 5000)) return false;
          if (!canvas_click(name + QStringLiteral("PrimaryAction"))) {
            std::cerr << "board action button unavailable " << name.toStdString() << '\n';
            return false;
          }
          auto* action_menu = find_named_object(studio_root, name);
          if (!action_menu) return false;
          const auto options = action_menu->property("menuItems").toList();
          int option = -1;
          for (qsizetype i = 0; i < options.size(); ++i) {
            if (options.at(i).toMap().value("key").toString() == key) option = static_cast<int>(i);
          }
          if (option < 0) return false;
          const auto row = name + QStringLiteral("PopupItem") + QString::number(option);
          if (!wait_for_condition([&] { return item_visible(find_named_item(studio_root, row)); }, 5000)) {
            std::cerr << "board action row unavailable " << row.toStdString() << '\n';
            return false;
          }
          return canvas_click(row);
        };
        if (!choose_board_action(1, QStringLiteral("rename")) || !wait_for_condition([&] {
              return item_visible(find_named_item(studio_root, QStringLiteral("editBoardNameField"))); }, 5000) ||
            !canvas_type(QStringLiteral("editBoardNameField"), QStringLiteral("Detail board")) ||
            !canvas_click(QStringLiteral("editBoardDialogApply")) || !wait_for_condition([&] {
              return carousel_document.slides().at(1).toMap().value("boardName").toString() == QStringLiteral("Detail board") &&
                  board_dialog_closed() &&
                  find_named_item(studio_root, QStringLiteral("editCanvasBoardLabel1"))->property("text").toString().contains("Detail board");
            }, 5000)) return fail("Rename the board through its canvas menu and show the saved name");
        if (!QMetaObject::invokeMethod(studio_root,"activateEditBoard",Q_ARG(QVariant,QVariant(0))) ||
            !wait_for_condition([&] { return !editor_preview.processing() && !editor_preview.fullRenderRunning(); },15000))
          return fail("Activate the source before creating a native poster variant");
        const auto variant_source_id = carousel_document.slides().at(0).toMap().value("boardId");
        const auto variant_source_recipe = editor_preview.currentAssetBoundAdjustmentPatchJson();
        if (!choose_board_action(0,QStringLiteral("variant")) || !wait_for_condition([&] {
              return carousel_document.slideCount() == 4 && carousel_document.currentIndex() == 1 &&
                  !editor_preview.processing() && !editor_preview.fullRenderRunning(); },15000) ||
            carousel_document.slides().at(1).toMap().value("boardId") == variant_source_id ||
            !carousel_document.slides().at(1).toMap().value("boardName").toString().contains("Variant 1") ||
            editor_preview.currentAssetBoundAdjustmentPatchJson() != variant_source_recipe)
          return fail("Create variant names an independent board with the source's editable layers");
        const auto variant_copy_id = carousel_document.slides().at(1).toMap().value("boardId");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] { return carousel_document.slideCount() == 3 &&
              carousel_document.slides().at(carousel_document.currentIndex()).toMap().value("boardId") == variant_source_id &&
              !editor_preview.processing() && !editor_preview.fullRenderRunning(); },15000))
          return fail("One native undo restores the source and removes the named variant");
        canvas_shortcut(Qt::Key_Z, true);
        if (!wait_for_condition([&] { return carousel_document.slideCount() == 4 &&
              carousel_document.slides().at(carousel_document.currentIndex()).toMap().value("boardId") == variant_copy_id &&
              !editor_preview.processing() && !editor_preview.fullRenderRunning(); },15000))
          return fail("Native redo restores the exact named variant identity");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] { return carousel_document.slideCount() == 3 &&
              !editor_preview.processing() && !editor_preview.fullRenderRunning(); },15000))
          return fail("Restore the board fixture after variant proof");
        if (!QMetaObject::invokeMethod(studio_root,"activateEditBoard",Q_ARG(QVariant,QVariant(0))) ||
            !wait_for_condition([&] { return !editor_preview.processing() && !editor_preview.fullRenderRunning(); },15000) ||
            !choose_board_action(0,QStringLiteral("duplicate")) || !wait_for_condition([&] { return carousel_document.slideCount()==4 &&
                carousel_document.currentIndex()==1 && !editor_preview.processing() && !editor_preview.fullRenderRunning(); },15000))
          return fail("Duplicate an independent board without breaking the other image's span");
        if (!choose_board_action(1,QStringLiteral("remove")) || !wait_for_condition([&] {
              auto* popup = find_named_object(studio_root,QStringLiteral("editCanvasBoardActions1Popup"));
              return item_visible(find_named_item(studio_root,QStringLiteral("editBoardDialogApply"))) && (!popup || !popup->property("visible").toBool()); },5000) ||
            !capture_visual_proof(studio_window,studio_root,proof_dir,QStringLiteral("edit-board-remove-confirmation"),1440,900,
              {QStringLiteral("editBoardDialogApply"),QStringLiteral("editBoardDialogCancel")},&studio_manifest) ||
            !canvas_click(QStringLiteral("editBoardDialogCancel")) || !wait_for_condition(board_dialog_closed,5000) ||
            carousel_document.slideCount()!=4)
          return fail("Cancelling removal retains the entire carousel document");
        if (!choose_board_action(1,QStringLiteral("remove")) || !wait_for_condition([&] {
              auto* popup = find_named_object(studio_root,QStringLiteral("editCanvasBoardActions1Popup"));
              return item_visible(find_named_item(studio_root,QStringLiteral("editBoardDialogApply"))) && (!popup || !popup->property("visible").toBool()); },5000) ||
            !canvas_click(QStringLiteral("editBoardDialogApply")) || !wait_for_condition([&] { return carousel_document.slideCount()==3 &&
              board_dialog_closed() && !editor_preview.processing() && !editor_preview.fullRenderRunning(); },15000) ||
            carousel_document.savedDocumentId()!=board_id || !carousel_document.slides().at(2).toMap().value("joinedPrevious").toBool())
          return fail("Deleting an independent board preserves the shared image and carousel document");
        if (!QMetaObject::invokeMethod(studio_root,"activateEditBoard",Q_ARG(QVariant,QVariant(2))) ||
            !wait_for_condition([&] { return !editor_preview.processing() && !editor_preview.fullRenderRunning(); },15000) ||
            !choose_board_action(2,QStringLiteral("remove")) || !wait_for_condition([&] { return item_visible(find_named_item(studio_root,QStringLiteral("editBoardDialogApply"))); },5000) ||
            !canvas_click(QStringLiteral("editBoardDialogApply")) || !wait_for_condition([&] { return carousel_document.slideCount()==2 &&
              board_dialog_closed() && !editor_preview.processing() && !editor_preview.fullRenderRunning(); },15000) ||
            carousel_document.savedDocumentId()!=board_id || editor_preview.assetId()!=shared_image_id ||
            editor_preview.currentAssetBoundAdjustmentPatchJson()!=shared_recipe)
          return fail("Deleting one extended tile preserves the image, edits and remaining carousel");
        if (!choose_board_add(1) || !wait_for_condition([&] { return carousel_document.slideCount()==3 &&
              !editor_preview.processing() && !editor_preview.fullRenderRunning(); },15000) ||
            editor_preview.assetId()!=shared_image_id || editor_preview.currentAssetBoundAdjustmentPatchJson()!=shared_recipe)
          return fail("Re-extending the remaining tile preserves the same live image and edits");
        if (!QMetaObject::invokeMethod(studio_root,"activateEditBoard",Q_ARG(QVariant,QVariant(1))))
          return fail("Select the shared image for final visual proof");
        if (!wait_for_condition([&] {return !editor_preview.processing() && !editor_preview.fullRenderRunning();},15000))
          return fail("Settle the shared image before drop input");
        QImage drop_source(320,480,QImage::Format_ARGB32);
        drop_source.fill(QColor(QStringLiteral("#43836d")));
        const auto drop_path=QDir(proof_dir).filePath(QStringLiteral("drop-portrait.png"));
        if (!drop_source.save(drop_path)) return fail("Save the native image-drop fixture");
        const auto original_shared_path=carousel_document.editBoardPayload(1).value("filePath").toString();
        const auto native_image_drop=[&](int index,bool capture) {
          auto* target=find_named_item(studio_root,QStringLiteral("editBoardImageDropArea")+QString::number(index));
          if (!item_visible(target)) return false;
          const auto point=scene_rect(target).center().toPoint();
          QMimeData mime;
          mime.setUrls({QUrl::fromLocalFile(drop_path)});
          QDragEnterEvent enter(point,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
          QCoreApplication::sendEvent(studio_window,&enter);
          QDragMoveEvent move(point,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
          QCoreApplication::sendEvent(studio_window,&move);
          process_layout_events(3);
          if (!enter.isAccepted() || !target->property("containsDrag").toBool()) return false;
          if (capture && !capture_visual_proof(studio_window,studio_root,proof_dir,QStringLiteral("board-image-drop-hover"),1440,900,
              {QStringLiteral("editBoardImageDropHighlight")+QString::number(index)},&studio_manifest)) return false;
          QDropEvent drop(QPointF(point),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
          QCoreApplication::sendEvent(studio_window,&drop);
          return drop.isAccepted() && wait_for_condition([&] {
            return item_visible(find_named_item(studio_root,QStringLiteral("editBoardDropAddLayerButton")));},5000);
        };
        if (!native_image_drop(1,true) || !capture_visual_proof(studio_window,studio_root,proof_dir,
              QStringLiteral("board-image-drop-choices"),1440,900,{QStringLiteral("editBoardDropAddLayerButton"),
              QStringLiteral("editBoardDropReplaceBaseButton"),QStringLiteral("editBoardDropScopeLabel")},&studio_manifest) ||
            !canvas_click(QStringLiteral("editBoardDropAddLayerButton")) || !wait_for_condition([&] {
              return editor_preview.photoLayerCount()==4 && !editor_preview.processing() && !editor_preview.fullRenderRunning();},15000) ||
            editor_preview.photoLayers().at(0).toMap().value("kind").toString()!=QStringLiteral("source"))
          return fail("A native image drop offers choices and creates an editable image layer");
        const auto dropped_layer=editor_preview.photoLayers().at(0).toMap();
        const auto layer_w=dropped_layer.value("pixelWidth").toDouble()*dropped_layer.value("scale").toDouble();
        const auto layer_h=dropped_layer.value("pixelHeight").toDouble()*dropped_layer.value("scale").toDouble();
        if (!(layer_w>0 && layer_h>0 && layer_w<=640 && layer_h<=400))
          return fail("The dropped portrait fits the board without stretching its proportions");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return editor_preview.currentAssetBoundAdjustmentPatchJson()==shared_recipe && !editor_preview.processing();},15000))
          return fail("One Undo removes the entire dropped layer and its placement");
        if (!native_image_drop(2,false) || carousel_document.currentIndex()!=1 ||
            !canvas_click(QStringLiteral("editBoardDropReplaceBaseButton")) || !wait_for_condition([&] {
              return !carousel_document.editBoardImageBusy() && carousel_document.currentIndex()==2 &&
                  !editor_preview.processing() && !editor_preview.fullRenderRunning() &&
                  carousel_document.editBoardPayload(2).value("filePath").toString()!=original_shared_path;},15000))
          return fail("Dropping on an inactive shared tile replaces the spanning base after a deliberate choice");
        const auto replaced_shared_path=carousel_document.editBoardPayload(2).value("filePath").toString();
        if (QImage(replaced_shared_path).size()!=QSize(640,400) ||
            editor_preview.currentAssetBoundAdjustmentPatchJson()!=shared_recipe ||
            carousel_document.editBoardPayload(1).value("filePath").toString()!=replaced_shared_path)
          return fail("Replacement keeps canonical geometry and the complete shared editable stack");
        // Payload queries select a tile; return to the last user-selected tile.
        QMetaObject::invokeMethod(studio_root,"activateEditBoard",Q_ARG(QVariant,QVariant(2)));
        if (!capture_visual_proof(studio_window,studio_root,proof_dir,QStringLiteral("board-base-replaced"),1440,900,
              {QStringLiteral("editImageViewport")},&studio_manifest)) return fail("Capture the replaced spanning base");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return !editor_preview.processing() &&
              carousel_document.editBoardPayload(2).value("filePath").toString()==original_shared_path;},15000)) {
          QVariant source_undo;
          QMetaObject::invokeMethod(studio_root,"canUndoBoardImageReplacement",Q_RETURN_ARG(QVariant,source_undo),Q_ARG(QVariant,QVariant(false)));
          std::cerr << "replacement undo eligible=" << source_undo.toBool() << " editorUndo=" << editor_preview.canUndo()
                    << " boardUndo=" << carousel_document.undoText().toStdString() << " boardRedo=" << carousel_document.redoText().toStdString()
                    << " localPath=" << studio_root->property("localEditorFilePath").toString().toStdString()
                    << " status=" << carousel_document.status().toStdString() << '\n';
          const auto marker=studio_root->property("editBoardImageReplacement").toMap();
          std::cerr << "marker asset=" << marker.value("assetId").toString().toStdString() << " actual=" << editor_preview.assetId().toStdString()
                    << " baseline matches=" << (marker.value("baseline").toString()==editor_preview.currentAssetBoundAdjustmentPatchJson()) << '\n';
          capture_visual_proof(studio_window,studio_root,proof_dir,QStringLiteral("replacement-undo-failure"),1440,900,
              {QStringLiteral("editImageViewport")},&studio_manifest);
          return fail("The regular Undo shortcut restores the replaced shared base");
        }
        canvas_shortcut(Qt::Key_Z,true);
        if (!wait_for_condition([&] {return !editor_preview.processing() &&
              carousel_document.editBoardPayload(2).value("filePath").toString()==replaced_shared_path;},15000))
          return fail("The regular Redo shortcut restores the replacement");
        if (!native_image_drop(2,false) || !canvas_click(QStringLiteral("editBoardDropReplaceBaseButton")) ||
            !wait_for_condition([&] {return !carousel_document.editBoardImageBusy() && !editor_preview.processing() &&
              !editor_preview.fullRenderRunning() && carousel_document.editBoardPayload(2).value("filePath").toString()!=replaced_shared_path;},15000))
          return fail("A second native replacement preserves the first replacement in history");
        const auto second_replacement_path=carousel_document.editBoardPayload(2).value("filePath").toString();
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return !editor_preview.processing() &&
              carousel_document.editBoardPayload(2).value("filePath").toString()==replaced_shared_path;},15000))
          return fail("Undo returns from the second replacement to the first");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return !editor_preview.processing() &&
              carousel_document.editBoardPayload(2).value("filePath").toString()==original_shared_path;},15000))
          return fail("Undo continues through multiple source replacements");
        canvas_shortcut(Qt::Key_Z,true);
        canvas_shortcut(Qt::Key_Z,true);
        if (!wait_for_condition([&] {return !editor_preview.processing() &&
              carousel_document.editBoardPayload(2).value("filePath").toString()==second_replacement_path;},15000))
          return fail("Redo restores multiple source replacements in order");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return !editor_preview.processing() &&
              carousel_document.editBoardPayload(2).value("filePath").toString()==replaced_shared_path;},15000))
          return fail("Return to the first replacement for layer history checks");
        if (!editor_preview.setPhotoLayerOpacity(0,0.5) || !wait_for_condition([&] {
              return !editor_preview.processing() && !editor_preview.fullRenderRunning();},15000))
          return fail("Editing content after replacement keeps normal layer history");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return !editor_preview.processing() &&
              editor_preview.currentAssetBoundAdjustmentPatchJson()==shared_recipe;},15000))
          return fail("The first undo after a new edit restores that edit on the replaced base");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return !editor_preview.processing() &&
              carousel_document.editBoardPayload(2).value("filePath").toString()==original_shared_path;},15000))
          return fail("Undo continues from layer history to the earlier base replacement");
        canvas_shortcut(Qt::Key_Z,true);
        canvas_shortcut(Qt::Key_Z,true);
        if (!wait_for_condition([&] {return !editor_preview.processing() &&
              editor_preview.photoLayers().at(0).toMap().value("opacity").toDouble()==0.5 &&
              carousel_document.editBoardPayload(2).value("filePath").toString()==replaced_shared_path;},15000))
          return fail("Redo preserves the order of base replacement and later layer edits");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return !editor_preview.processing() &&
              editor_preview.currentAssetBoundAdjustmentPatchJson()==shared_recipe;},15000))
          return fail("Restore layer opacity before returning to the original base");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return !editor_preview.processing() &&
              carousel_document.editBoardPayload(2).value("filePath").toString()==original_shared_path;},15000) ||
            !QMetaObject::invokeMethod(studio_root,"activateEditBoard",Q_ARG(QVariant,QVariant(1))) ||
            !canvas_click(QStringLiteral("editRailPaintButton")) || !wait_for_condition([&] {
              return edit->property("editTool").toString()==QStringLiteral("paint") &&
                  !edit->property("editToolTransitionActive").toBool() && board_paint->property("editable").toBool() &&
                  !editor_preview.processing() && !editor_preview.fullRenderRunning();},15000))
          return fail("Restore the shared base and brush for the existing board export workflow");
        const auto board_ids = [&] {
          QStringList ids;
          for (const auto& board : carousel_document.slides()) ids.push_back(board.toMap().value("boardId").toString());
          return ids;
        };
        const auto original_board_order = board_ids();
        const auto active_board_id = carousel_document.slides().at(1).toMap().value("boardId");
        const auto before_pan_zoom=canvas_viewport->property("zoom").toDouble();
        const auto before_pan_x=canvas_viewport->property("panX").toDouble();
        const auto before_pan_y=canvas_viewport->property("panY").toDouble();
        canvas_viewport->setProperty("zoom",before_pan_zoom*2);
        process_layout_events(4);
        auto* pan_header=find_named_item(studio_root,QStringLiteral("editCanvasBoardHeaderSelect1"));
        const auto pan_start=pan_header->mapToScene(QPointF(pan_header->width()/2,pan_header->height()/2));
        const auto pan_target=canvas_viewport->mapToScene(QPointF(8,canvas_viewport->property("imageFrameY").toDouble()-15));
        canvas_pointer_timestamp+=static_cast<quint64>(QApplication::doubleClickInterval()+1);
        canvas_pointer(QEvent::MouseButtonPress,pan_start);
        for (int step=1;step<=8;++step) canvas_pointer(QEvent::MouseMove,pan_start+(pan_target-pan_start)*(static_cast<double>(step)/8));
        if (!wait_for_condition([&] {return canvas_viewport->property("boardDragActive").toBool() &&
              canvas_viewport->property("panX").toDouble()>before_pan_x+8;},3000))
          return fail("A zoomed header drag pans to reveal boards beyond the viewport edge");
        QKeyEvent pan_escape_press(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
        QKeyEvent pan_escape_release(QEvent::KeyRelease,Qt::Key_Escape,Qt::NoModifier);
        QCoreApplication::sendEvent(studio_window,&pan_escape_press);
        QCoreApplication::sendEvent(studio_window,&pan_escape_release);
        canvas_pointer(QEvent::MouseButtonRelease,pan_target);
        if (board_ids()!=original_board_order || canvas_viewport->property("boardDragActive").toBool())
          return fail("Cancelling an edge-panning drag leaves all board content and order intact");
        canvas_viewport->setProperty("zoom",before_pan_zoom);
        canvas_viewport->setProperty("panX",before_pan_x);
        canvas_viewport->setProperty("panY",before_pan_y);
        if (!wait_for_condition([&] {return !editor_preview.processing() && !editor_preview.fullRenderRunning();},15000))
          return fail("Restore the viewport after edge-panning verification");
        const auto drag_board_header = [&](int index, int boundary, bool cancel, bool capture) {
          studio_window->raise();
          studio_window->requestActivate();
          if (!wait_for_condition([&] {
                if (!studio_window->isActive()) { studio_window->raise(); studio_window->requestActivate(); }
                return studio_window->isActive(); },5000)) return false;
          auto* header = find_named_item(studio_root,QStringLiteral("editCanvasBoardHeaderSelect")+QString::number(index));
          if (!item_visible(header)) return false;
          const auto start = header->mapToScene(QPointF(std::min(40.0,header->width()/2),header->height()/2));
          QVariant boundary_x;
          if (!QMetaObject::invokeMethod(canvas_viewport,"boardBoundaryX",Q_RETURN_ARG(QVariant,boundary_x),Q_ARG(QVariant,QVariant(boundary)))) return false;
          const auto target = canvas_viewport->mapToScene(QPointF(boundary_x.toDouble()+(boundary==0?6.0:-6.0),
              canvas_viewport->property("imageFrameY").toDouble()-15));
          canvas_pointer_timestamp+=static_cast<quint64>(QApplication::doubleClickInterval()+1);
          canvas_pointer(QEvent::MouseButtonPress,start);
          for (int step=1;step<=8;++step) canvas_pointer(QEvent::MouseMove,start+(target-start)*(static_cast<double>(step)/8));
          if (!canvas_viewport->property("boardDragActive").toBool() ||
              canvas_viewport->property("boardDragInsertion").toInt()!=boundary) {
            std::cerr << "header drag active=" << canvas_viewport->property("boardDragActive").toBool()
                      << " insertion=" << canvas_viewport->property("boardDragInsertion").toInt()
                      << " header enabled=" << header->property("enabled").toBool()
                      << " pressed=" << header->property("pressed").toBool() << '\n';
            canvas_pointer(QEvent::MouseButtonRelease,target);
            return false;
          }
          if (capture && !capture_visual_proof(studio_window,studio_root,proof_dir,QStringLiteral("board-header-drag"),1440,900,
              {QStringLiteral("editBoardReorderInsertionMarker"),QStringLiteral("editBoardReorderDragLabel")},&studio_manifest)) return false;
          if (cancel) {
            QKeyEvent escape_press(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
            QKeyEvent escape_release(QEvent::KeyRelease,Qt::Key_Escape,Qt::NoModifier);
            QCoreApplication::sendEvent(studio_window,&escape_press);
            QCoreApplication::sendEvent(studio_window,&escape_release);
            process_layout_events(2);
            if (canvas_viewport->property("boardDragActive").toBool()) {
              auto* focus=studio_window->activeFocusItem();
              std::cerr << "Escape did not cancel drag; window active=" << studio_window->isActive()
                        << " focus=" << (focus?focus->objectName().toStdString():"none") << '\n';
            }
          }
          canvas_pointer(QEvent::MouseButtonRelease,target);
          return !canvas_viewport->property("boardDragActive").toBool();
        };
        const auto moved_board_order = QStringList{original_board_order.at(1),original_board_order.at(2),original_board_order.at(0)};
        if (!drag_board_header(2,0,true,false) || board_ids()!=original_board_order)
          return fail("Escape cancels a header drag without changing the document");
        if (!drag_board_header(2,0,false,true) || !wait_for_condition([&] {
              return board_ids()==moved_board_order && carousel_document.currentIndex()==0 && !editor_preview.processing() &&
                  !editor_preview.fullRenderRunning();},15000) ||
            carousel_document.slides().at(0).toMap().value("boardId")!=active_board_id ||
            editor_preview.assetId()!=shared_image_id || editor_preview.currentAssetBoundAdjustmentPatchJson()!=shared_recipe ||
            carousel_document.slides().at(1).toMap().value("imageSpanStart").toInt()!=0)
          return fail("Dragging either shared header moves the complete image while retaining the active tile and layers");
        if (!capture_visual_proof(studio_window,studio_root,proof_dir,QStringLiteral("board-header-reordered"),1440,900,
              {QStringLiteral("editImageViewport")},&studio_manifest)) return fail("Capture reordered boards");
        if (!editor_preview.setPhotoLayerOpacity(0,0.5) || !wait_for_condition([&] {
              return !editor_preview.processing() && !editor_preview.fullRenderRunning();},15000))
          return fail("Layer edits continue normally after moving boards");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return editor_preview.currentAssetBoundAdjustmentPatchJson()==shared_recipe && !editor_preview.processing();},15000))
          return fail("Undo first restores the layer edit made after reordering");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return board_ids()==original_board_order && !editor_preview.processing();},15000))
          return fail("Undo continues from layer edits to the earlier board move");
        canvas_shortcut(Qt::Key_Z,true);
        canvas_shortcut(Qt::Key_Z,true);
        if (!wait_for_condition([&] {return board_ids()==moved_board_order && !editor_preview.processing() &&
              editor_preview.photoLayers().at(0).toMap().value("opacity").toDouble()==0.5;},15000))
          return fail("Redo preserves the order of board movement and later layer edits");
        canvas_shortcut(Qt::Key_Z);
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return board_ids()==original_board_order && !editor_preview.processing() &&
              !editor_preview.fullRenderRunning();},15000) || !drag_board_header(0,3,false,false) ||
            !wait_for_condition([&] {return board_ids()==moved_board_order && !editor_preview.processing();},15000))
          return fail("An independent board moves past a spanning image without splitting it");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return board_ids()==original_board_order && !editor_preview.processing();},15000) ||
            !canvas_click(QStringLiteral("editCanvasBoardHeaderSelect2")) ||
            !wait_for_condition([&] {return carousel_document.currentIndex()==2 && !editor_preview.processing();},15000) ||
            !choose_board_action(2,QStringLiteral("earlier")) || !wait_for_condition([&] {return board_ids()==moved_board_order && !editor_preview.processing();},15000))
          return fail("Move left from the last shared tile uses the same intact-image reordering path");
        if (!wait_for_condition([&] {
              auto* popup=find_named_object(studio_root,QStringLiteral("editCanvasBoardActions2Popup"));
              return (!popup || !popup->property("visible").toBool()) && !editor_preview.processing() && !editor_preview.fullRenderRunning();},15000))
          return fail("The board action menu closes before keyboard history input");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return board_ids()==original_board_order && !editor_preview.processing();},15000)) {
          QVariant eligible;
          QMetaObject::invokeMethod(studio_root,"canUndoBoardMove",Q_RETURN_ARG(QVariant,eligible),Q_ARG(QVariant,QVariant(false)));
          std::cerr << "menu move undo eligible=" << eligible.toBool() << " controller=" << carousel_document.canUndoEditBoardMove()
                    << " label=" << carousel_document.undoText().toStdString() << " redo=" << carousel_document.redoText().toStdString()
                    << " active=" << studio_window->isActive() << " editorUndo=" << editor_preview.canUndo()
                    << " order=" << board_ids().join('|').toStdString() << '\n';
          capture_visual_proof(studio_window,studio_root,proof_dir,QStringLiteral("board-menu-undo-failure"),1440,900,
              {QStringLiteral("editImageViewport")},&studio_manifest);
          return fail("Restore the original order after the menu move");
        }
        if (!QMetaObject::invokeMethod(studio_root,"activateEditBoard",Q_ARG(QVariant,QVariant(1))) ||
            !wait_for_condition([&] {return !editor_preview.processing() && !editor_preview.fullRenderRunning();},15000))
          return fail("Restore the original order before export and persistence checks");
        const auto carousel_export_dir = QDir(proof_dir).filePath(QStringLiteral("carousel-export"));
        QDir().mkpath(carousel_export_dir);
        carousel_document.setExportManifestEnabled(false);
        if (!carousel_document.exportCarouselToDirectory(carousel_export_dir) ||
            QDir(carousel_export_dir).entryList({QStringLiteral("*.png")},QDir::Files).size()!=3)
          return fail("The live carousel exports three separate images");
        const auto overview_width = canvas_viewport->property("paintedContentWidth").toDouble();
        if (!canvas_click(QStringLiteral("editBoardViewModeButton")) || !wait_for_condition([&] {
              return canvas_viewport->property("boardFocusMode").toBool() &&
                  canvas_viewport->property("paintedContentWidth").toDouble() > overview_width * 1.5 &&
                  !editor_preview.processing() && !editor_preview.fullRenderRunning(); }, 15000) ||
            !board_paint->property("editable").toBool() ||
            !capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("edit-board-focused"), 1440, 900,
                {QStringLiteral("editBoardViewModeButton"), QStringLiteral("editBoardDocumentStatus"), QStringLiteral("photoPaintOverlay")}, &studio_manifest) ||
            !canvas_click(QStringLiteral("editBoardViewModeButton")) || canvas_viewport->property("boardFocusMode").toBool())
          return fail("Focus board and All boards resize the canvas without changing the active paint tool or layer");
        if (!wait_for_condition([&] { return !editor_preview.processing() && !editor_preview.fullRenderRunning(); }, 15000))
          return fail("The overview settles before visual review");
        if (!wait_for_condition([&] {
              auto* thumbnail = find_named_item(studio_root,QStringLiteral("editCanvasBoardPreview0"));
              return thumbnail && thumbnail->property("status").toInt()==1 &&
                  !carousel_document.slides().at(0).toMap().value("previewSource").toString().isEmpty(); },15000))
          return fail("The overview must publish the other board's edited image before visual review");
        const auto other_preview_path = QUrl(carousel_document.slides().at(0).toMap().value("previewSource").toString()).toLocalFile();
        const QImage other_preview(other_preview_path);
        if (other_preview.isNull() || other_preview.pixelColor(other_preview.width()/4,other_preview.height()/3).alpha()==0)
          return fail("The inactive board's preview must include its editable graphics");
        const auto board_toolbar_fits = [&] {
          auto* zoom = find_named_item(studio_root, QStringLiteral("editZoomToolbar"));
          auto* actions = find_named_item(studio_root, QStringLiteral("editActionToolbar"));
          auto* create = find_named_item(studio_root, QStringLiteral("editNewCanvasButton"));
          return item_visible(zoom) && item_visible(actions) && item_visible(create) &&
              std::abs(scene_rect(zoom).top() - scene_rect(actions).top()) < 1 &&
              !scene_rect(zoom).intersects(scene_rect(actions)) &&
              !scene_rect(actions).intersects(scene_rect(create)) &&
              !scene_rect(zoom).intersects(scene_rect(create));
        };
        auto* shared_scope = find_named_item(studio_root, QStringLiteral("editBoardSharedLayersLabel"));
        if (!item_visible(shared_scope) || !shared_scope->property("text").toString().contains(QStringLiteral("Boards 2–3")) ||
            item_visible(find_named_item(studio_root, QStringLiteral("photoLayerPropertiesPanel"))) || !board_toolbar_fits())
          return fail("The shared brush stack labels its affected boards without duplicate properties or wrapped view controls");
        if (!capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("edit-board-stage"), 1440, 900,
              {QStringLiteral("editImageViewport"), QStringLiteral("editCanvasBoard0"), QStringLiteral("editCanvasBoard1"),
               QStringLiteral("editCanvasAddBoardButton"), QStringLiteral("editRailPaintButton")}, &studio_manifest) ||
            !capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("edit-board-stage-compact"), 1100, 800,
              {QStringLiteral("editImageViewport"), QStringLiteral("editCanvasBoard1"), QStringLiteral("editCanvasAddBoardButton")}, &studio_manifest))
          return fail("Capture the actual Edit canvas with direct board selection and existing tool UI");
        const auto visible_layer_menu_item = [&](const QString& text = QStringLiteral("Layer Properties…")) -> QQuickItem* {
          QList<QQuickItem*> pending{studio_window->contentItem()};
          while (!pending.isEmpty()) {
            auto* item = pending.takeLast();
            if (item_visible(item) && item->property("text").toString() == text)
              return item;
            pending.append(item->childItems());
          }
          return nullptr;
        };
        if (!canvas_click(QStringLiteral("photoLayerRowMoreMenuButton")) || !wait_for_condition([&] {
              return visible_layer_menu_item() != nullptr; }, 5000))
          return fail("The paint layer's More menu exposes advanced properties");
        auto* row_actions=find_named_object(studio_root,QStringLiteral("photoLayerRowActionsPopup"));
        if (!row_actions || row_actions->property("count").toInt()>14 ||
            !capture_visual_proof(studio_window,studio_root,proof_dir,QStringLiteral("layer-actions-menu"),1100,800,
                {QStringLiteral("photoLayerRowMoreMenuButton")},&studio_manifest))
          return fail("Layer actions use a compact menu with grouped secondary commands");
        const auto properties_point = scene_rect(visible_layer_menu_item()).center();
        canvas_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
        canvas_pointer(QEvent::MouseButtonPress, properties_point);
        canvas_pointer(QEvent::MouseButtonRelease, properties_point);
        if (!wait_for_condition([&] {
              return item_visible(find_named_item(studio_root, QStringLiteral("photoLayerPropertiesPanel"))) &&
                  item_visible(find_named_item(studio_root, QStringLiteral("photoLayerOpacitySlider"))) &&
                  visible_layer_menu_item() == nullptr; }, 5000)) {
          std::cerr << "properties root expanded=" << edit->property("photoLayerDetailsExpanded").toBool()
                    << " panel=" << item_visible(find_named_item(studio_root,QStringLiteral("photoLayerPropertiesPanel")))
                    << " opacity=" << item_visible(find_named_item(studio_root,QStringLiteral("photoLayerOpacitySlider")))
                    << " tool=" << edit->property("editTool").toString().toStdString() << '\n';
          capture_visual_proof(studio_window,studio_root,proof_dir,QStringLiteral("layer-properties-failure"),1100,800,
              {QStringLiteral("photoLayerPropertiesPanel"),QStringLiteral("photoLayerOpacitySlider")},&studio_manifest);
          return fail("The layer properties command opens the opacity controls");
        }
        if (edit->property("editTool").toString() != QStringLiteral("paint"))
          return fail("Opening layer properties preserves the paint tool");
        if (!canvas_click(QStringLiteral("photoLayerPropertiesDetailsButton")))
          return fail("Expanded layer properties expose a collapse control");
        if (!wait_for_condition([&] {
              return !item_visible(find_named_item(studio_root, QStringLiteral("photoLayerPropertiesPanel"))); },5000))
          return fail("Collapsing properties restores the sparse paint inspector");
        if (!canvas_click(QStringLiteral("photoLayerRowMoreMenuButton")) || !wait_for_condition([&] {
              return visible_layer_menu_item(QStringLiteral("Visibility"))!=nullptr;},5000))
          return fail("Grouped visibility commands remain available in the layer menu");
        auto click_layer_menu_text=[&](const QString& text) {
          auto* item=visible_layer_menu_item(text);
          if (!item) return false;
          const auto point=scene_rect(item).center();
          canvas_pointer_timestamp+=static_cast<quint64>(QApplication::doubleClickInterval()+1);
          canvas_pointer(QEvent::MouseButtonPress,point);
          canvas_pointer(QEvent::MouseButtonRelease,point);
          return true;
        };
        if (!click_layer_menu_text(QStringLiteral("Visibility")) || !wait_for_condition([&] {
              return visible_layer_menu_item(QStringLiteral("Hide Layer"))!=nullptr;},5000) ||
            !capture_visual_proof(studio_window,studio_root,proof_dir,QStringLiteral("layer-actions-submenu"),1100,800,
                {QStringLiteral("photoLayerRowMoreMenuButton")},&studio_manifest) ||
            !click_layer_menu_text(QStringLiteral("Hide Layer")))
          return fail("The visibility submenu can be clicked with native input");
        if (!wait_for_condition([&] {
              return !editor_preview.photoLayers().at(0).toMap().value("enabled").toBool() &&
                  !visible_layer_menu_item(QStringLiteral("Hide Layer")) && !visible_layer_menu_item();},5000)) {
          std::cerr << "submenu enabled=" << editor_preview.photoLayers().at(0).toMap().value("enabled").toBool()
                    << " hide visible=" << bool(visible_layer_menu_item(QStringLiteral("Hide Layer")))
                    << " properties visible=" << bool(visible_layer_menu_item()) << '\n';
          capture_visual_proof(studio_window,studio_root,proof_dir,QStringLiteral("submenu-action-failure"),1100,800,
              {QStringLiteral("photoLayerRowMoreMenuButton")},&studio_manifest);
          return fail("A submenu action hides the layer and dismisses the full chain");
        }
        const auto hidden_recipe=editor_preview.currentAssetBoundAdjustmentPatchJson();
        if (!canvas_click(QStringLiteral("photoLayerRowMoreMenuButton")) || !wait_for_condition([&] {
              return visible_layer_menu_item(QStringLiteral("Visibility"))!=nullptr;},5000) ||
            !click_layer_menu_text(QStringLiteral("Visibility")) || !wait_for_condition([&] {
              return visible_layer_menu_item(QStringLiteral("Show Layer"))!=nullptr;},5000) ||
            !click_layer_menu_text(QStringLiteral("Show Layer")) || !wait_for_condition([&] {
              return editor_preview.photoLayers().at(0).toMap().value("enabled").toBool() &&
                  !visible_layer_menu_item(QStringLiteral("Show Layer")) && !visible_layer_menu_item();},5000))
          return fail("The grouped visibility action also shows a hidden layer and closes its menus");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return !editor_preview.processing() &&
              editor_preview.currentAssetBoundAdjustmentPatchJson()==hidden_recipe;},15000))
          return fail("Undo restores the hidden layer after Show Layer");
        canvas_shortcut(Qt::Key_Z);
        if (!wait_for_condition([&] {return !editor_preview.processing() &&
              editor_preview.currentAssetBoundAdjustmentPatchJson()==shared_recipe;},15000))
          return fail("Undo restores the layer after Hide Layer");
        const auto brush_size_before_scopes=board_paint->property("brushSize");
        const auto brush_target_before_scopes=board_paint->property("targetId");
        const bool scopes_before_more = edit->property("scopeDockVisible").toBool();
        const auto toggle_compact_scopes = [&] {
          if (!canvas_click(QStringLiteral("editViewMoreButtonPrimaryAction")) || !wait_for_condition([&] {
                return item_visible(find_named_item(studio_root, QStringLiteral("editViewMoreButtonPopupItem1"))); }, 5000))
            return false;
          return canvas_click(QStringLiteral("editViewMoreButtonPopupItem1"));
        };
        if (!board_toolbar_fits() || !item_visible(find_named_item(studio_root, QStringLiteral("editViewMoreButton"))))
          return fail("Compact view controls fit on one row");
        if (!toggle_compact_scopes())
          return fail("The compact More menu opens and its Scopes row can be clicked");
        if (!wait_for_condition([&] {
              return edit->property("scopeDockVisible").toBool() != scopes_before_more &&
                  !edit->property("editToolTransitionActive").toBool() &&
                  !item_visible(find_named_item(studio_root, QStringLiteral("editViewMoreButtonPopupItem1"))); }, 5000)) {
          std::cerr << "compact scopes before=" << scopes_before_more
                    << " after=" << edit->property("scopeDockVisible").toBool()
                    << " transition=" << edit->property("editToolTransitionActive").toBool() << '\n';
          capture_visual_proof(studio_window,studio_root,proof_dir,QStringLiteral("compact-menu-failure"),1100,800,
              {QStringLiteral("editViewMoreButton"),QStringLiteral("editViewMoreButtonPopupItem1")},&studio_manifest);
          return fail("The compact Scopes command opens the dock and dismisses More");
        }
        if (!toggle_compact_scopes() || !wait_for_condition([&] {
              return edit->property("scopeDockVisible").toBool() == scopes_before_more &&
                  !edit->property("editToolTransitionActive").toBool(); }, 5000))
          return fail("The compact Scopes command closes the dock again");
        if (edit->property("editTool").toString()!=QStringLiteral("paint") ||
            board_paint->property("brushSize")!=brush_size_before_scopes || board_paint->property("targetId")!=brush_target_before_scopes ||
            edit->property("editInspectorTab").toString()!=QStringLiteral("layers"))
          return fail("Scopes preserve the active brush, its settings, selected layer and inspector tab");
        studio_root->setProperty("currentWorkspace", 0);
        process_layout_events(8);
        if (!QMetaObject::invokeMethod(studio_root, "finishCarouselPhotoEdit", Q_ARG(QVariant, QVariant(true))))
          return fail("Commit the board document before the reopen proof");
        carousel_document.closeCarousel();
        if (!canvas_click(QStringLiteral("libraryCarouselsCollectionButton")) || !wait_for_condition([&] {
              return item_visible(find_named_item(studio_root, QStringLiteral("libraryOpenBoardsButton") + board_id)); }, 10000) ||
            !canvas_click(QStringLiteral("libraryOpenBoardsButton") + board_id) || !wait_for_condition([&] {
              return studio_root->property("photoWorkspaceActive").toBool() && studio_root->property("editBoardsActive").toBool() &&
                   !studio_root->property("workspaceTransitionActive").toBool() && !editor_preview.processing(); }, 15000))
          return fail("Saved board documents reopen directly into the existing Edit workspace");
        if (carousel_document.slideCount() != 3 || !QMetaObject::invokeMethod(studio_root,"activateEditBoard",Q_ARG(QVariant,QVariant(0))) ||
            !wait_for_condition([&] { return editor_preview.photoLayerCount() == 4 && !editor_preview.processing(); }, 15000))
          return fail("Reopening restores the first board's full editable layer stack");
        const auto first_saved = photo_editor::desktop::parse_adjustment_patch_json(first_recipe);
        const auto first_reopened = photo_editor::desktop::parse_adjustment_patch_json(editor_preview.currentAssetBoundAdjustmentPatchJson());
        if (!first_saved.ok() || !first_reopened.ok() || !(*first_saved.state == *first_reopened.state))
          return fail("Reopening preserves the exact first-board recipe independently of other boards");
        if (carousel_document.slides().at(1).toMap().value("boardName").toString() != QStringLiteral("Detail board"))
          return fail("The renamed board survives saved reopening");
        std::cout << "Edit board stage: native plus menu, independent photos, live shared layers, cross-seam brush input, single-tile removal, separate exports and saved reopening passed\n";

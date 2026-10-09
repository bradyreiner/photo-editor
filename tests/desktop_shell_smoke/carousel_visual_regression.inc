    carousel_document.createCarouselWithSettings(
        QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Carousel Visual Regression")},
                    {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
                    {QStringLiteral("slideCount"), 3}});
    engine.load(QUrl::fromLocalFile(main_qml_path));
    process_layout_events(10);
    if (engine.rootObjects().isEmpty()) {
      return fail("Carousel visual regression should load Main.qml");
    }
    auto* visual_root = engine.rootObjects().constFirst();
    auto* visual_window = qobject_cast<QQuickWindow*>(visual_root);
    if (visual_window == nullptr) {
      return fail("Carousel visual regression requires a real QQuickWindow");
    }
    visual_root->setProperty(
        "currentWorkspace", visual_root->property("carouselWorkspaceIndex"));
    process_layout_events(10);
    if (auto* first_run_guide =
            find_named_object(visual_root, QStringLiteral("firstRunGuideDialog"));
        first_run_guide != nullptr && first_run_guide->property("visible").toBool()) {
      if (!QMetaObject::invokeMethod(first_run_guide, "close") ||
          !wait_for_condition(
              [first_run_guide]() { return !first_run_guide->property("visible").toBool(); },
              1500)) {
        return fail("Carousel visual regression should close first-run guidance before capture");
      }
      app_shell.completeFirstRunExperience();
      process_layout_events(4);
    }
    auto* visual_workspace =
        find_named_object(visual_root, QStringLiteral("carouselWorkspace"));
    if (visual_workspace == nullptr ||
        visual_workspace->property("carouselVisualRegressionContract").toString() !=
            QStringLiteral("carousel-visual-regression:v1|states:empty-template-browser-composer-multi-select-crop-missing-resource-preflight-export-compact-wide-high-dpi-reduced-motion|real-qml:true|required-objects:true|png:true|sha256:true|manifest:true|nonblank:true|no-elide:true")) {
      return fail("Carousel visual regression should load the contracted real workspace");
    }

    const auto carousel_visual_dir =
        qEnvironmentVariableIsSet("PHOTO_EDITOR_VISUAL_PROOF_DIR")
            ? QString::fromUtf8(qgetenv("PHOTO_EDITOR_VISUAL_PROOF_DIR"))
            : runtime_root.filePath(QStringLiteral("carousel-visual-regression"));
    if (!QDir().mkpath(carousel_visual_dir)) {
      return fail("Carousel visual regression should create its artifact directory");
    }
    QJsonArray carousel_visual_manifest;
    const auto image_has_visual_variance = [](const QImage& image) {
      if (image.isNull() || image.width() < 2 || image.height() < 2) {
        return false;
      }
      const auto reference = image.pixelColor(0, 0);
      const auto horizontal_step = std::max(1, image.width() / 32);
      const auto vertical_step = std::max(1, image.height() / 32);
      for (int y = 0; y < image.height(); y += vertical_step) {
        for (int x = 0; x < image.width(); x += horizontal_step) {
          if (image.pixelColor(x, y) != reference) {
            return true;
          }
        }
      }
      return false;
    };
    const auto capture_carousel_state = [&](const QString& state,
                                            const int width,
                                            const int height,
                                            const QStringList& required_objects,
                                            const QJsonObject& metadata = {}) {
      if (!capture_visual_proof(
              visual_window,
              visual_root,
              carousel_visual_dir,
              state,
              width,
              height,
              required_objects,
              &carousel_visual_manifest,
              metadata)) {
        return false;
      }
      auto entry = carousel_visual_manifest.at(carousel_visual_manifest.size() - 1).toObject();
      QFile captured_file(entry.value(QStringLiteral("path")).toString());
      if (!captured_file.open(QIODevice::ReadOnly)) {
        return false;
      }
      QCryptographicHash hash(QCryptographicHash::Sha256);
      if (!hash.addData(&captured_file)) {
        return false;
      }
      const QImage captured_image(entry.value(QStringLiteral("path")).toString());
      if (!image_has_visual_variance(captured_image)) {
        return false;
      }
      entry.insert(QStringLiteral("sha256"), QString::fromLatin1(hash.result().toHex()));
      entry.insert(QStringLiteral("pixel_width"), captured_image.width());
      entry.insert(QStringLiteral("pixel_height"), captured_image.height());
      entry.insert(QStringLiteral("device_pixel_ratio"), visual_window->devicePixelRatio());
      carousel_visual_manifest.replace(carousel_visual_manifest.size() - 1, entry);
      return true;
    };

    const QStringList base_required{
        QStringLiteral("carouselWorkspace"),
        QStringLiteral("carouselHeaderBar"),
        QStringLiteral("carouselArtboardViewport"),
        QStringLiteral("carouselBoardStrip"),
    };
    auto required_with = [&](const QStringList& extras) {
      auto required = base_required;
      required.append(extras);
      return required;
    };
    const auto settle_empty_size = [&](const int width, const int height) {
      visual_root->setProperty("width", width);
      visual_root->setProperty("height", height);
      QElapsedTimer settle_timer;
      settle_timer.start();
      while (settle_timer.elapsed() < 150) {
        process_layout_events(1);
        QThread::msleep(2);
      }
      visual_window->update();
      process_layout_events(4);
    };
    const auto empty_commands_fit = [&]() {
      auto* card = find_named_item(visual_root, QStringLiteral("carouselEmptyBoardStartCard"));
      if (card == nullptr || card->parentItem() == nullptr || !item_visible(card) ||
          !scene_rect(card->parentItem()).contains(scene_rect(card))) {
        return false;
      }
      QVector<QRectF> action_rects;
      for (const auto& name : {
               QStringLiteral("carouselEmptyBoardMediaAction"),
               QStringLiteral("carouselEmptyBoardTextAction"),
               QStringLiteral("carouselEmptyBoardShapeAction"),
               QStringLiteral("carouselEmptyBoardPasteAction"),
               QStringLiteral("carouselEmptyBoardTemplateAction"),
               QStringLiteral("carouselEmptyBoardCompactMediaAction"),
               QStringLiteral("carouselEmptyBoardCompactActionsMenu")}) {
        auto* action = find_named_item(card, name);
        if (action != nullptr && item_visible(action)) {
          const auto rect = scene_rect(action);
          if (!scene_rect(card).contains(rect)) {
            return false;
          }
          for (const auto& other : action_rects) {
            if (rect.intersects(other)) {
              return false;
            }
          }
          action_rects.push_back(rect);
        }
      }
      const auto expected_actions = card->property("compactCommandSurface").toBool() ? 2 : 5;
      if (action_rects.size() != expected_actions) {
        return false;
      }
      for (auto* prompt : find_named_items(visual_root, QStringLiteral("carouselEmptySlotPrompt"))) {
        if (item_visible(prompt)) {
          return false;
        }
      }
      return true;
    };

    if (!capture_carousel_state(
            QStringLiteral("carousel-empty"),
            1280,
            720,
            required_with({QStringLiteral("carouselEmptyBoardStartCard")}),
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("empty")}})) {
      return fail("Carousel visual regression should capture the empty state");
    }
    if (!empty_commands_fit()) {
      return fail("Empty-board commands should fit inside their board without competing slot prompts");
    }
    settle_empty_size(760, 600);
    auto* empty_actions_menu = find_named_object(
        visual_root, QStringLiteral("carouselEmptyBoardCompactActionsMenu"));
    if (empty_actions_menu == nullptr || empty_actions_menu->property("menuItems").toList().size() != 4 ||
        !QMetaObject::invokeMethod(empty_actions_menu, "openMenu") ||
        !wait_for_condition([&]() {
          auto* popup = find_named_object(
              visual_root, QStringLiteral("carouselEmptyBoardCompactActionsMenuPopup"));
          return popup != nullptr && popup->property("opened").toBool();
        }, 1500) ||
        !capture_carousel_state(
            QStringLiteral("carousel-empty-compact"), 760, 600,
            required_with({QStringLiteral("carouselEmptyBoardStartCard"),
                           QStringLiteral("carouselEmptyBoardCompactMediaAction"),
                           QStringLiteral("carouselEmptyBoardCompactActionsMenuPopup")}),
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("empty-compact")}}) ||
        !empty_commands_fit()) {
      return fail("Compact empty boards should keep media visible and the remaining creation actions in one readable menu");
    }
    QMetaObject::invokeMethod(empty_actions_menu, "closeMenu");
    if (!QMetaObject::invokeMethod(empty_actions_menu, "menuItemTriggered",
                                   Q_ARG(QString, QStringLiteral("text"))) ||
        !carousel_document.selectionDetails().value(QStringLiteral("isText")).toBool() ||
        carousel_document.canvasItems().size() != 1) {
      return fail("The compact empty-board menu should create an editable text layer");
    }
    carousel_document.undo();
    process_layout_events(6);
    if (!carousel_document.canvasItems().isEmpty() || !empty_commands_fit()) {
      return fail("Undoing a compact creation action should restore the empty-board command surface");
    }
    settle_empty_size(1920, 1080);
    if (!capture_carousel_state(
            QStringLiteral("carousel-empty-wide"), 1920, 1080,
            required_with({QStringLiteral("carouselEmptyBoardStartCard"),
                           QStringLiteral("carouselEmptyBoardMediaAction"),
                           QStringLiteral("carouselEmptyBoardTextAction"),
                           QStringLiteral("carouselEmptyBoardShapeAction"),
                           QStringLiteral("carouselEmptyBoardPasteAction"),
                           QStringLiteral("carouselEmptyBoardTemplateAction")}),
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("empty-wide")}}) ||
        !empty_commands_fit()) {
      return fail("Wide empty boards should expose all five creation actions inside the board");
    }
    settle_empty_size(1280, 720);

    auto* role_menu = find_named_object(visual_root, QStringLiteral("carouselBoardRoleMenu"));
    auto* layout_menu = find_named_object(visual_root, QStringLiteral("carouselBoardLayoutMenu"));
    if (role_menu == nullptr || layout_menu == nullptr ||
        role_menu->property("menuItems").toList().size() != 10 ||
        layout_menu->property("menuItems").toList().size() != 4 ||
        !QMetaObject::invokeMethod(role_menu, "menuItemTriggered",
                                   Q_ARG(QString, QStringLiteral("hook"))) ||
        carousel_document.selectionDetails().value(QStringLiteral("boardRole")).toString() !=
            QStringLiteral("hook") ||
        !QMetaObject::invokeMethod(role_menu, "menuItemTriggered",
                                   Q_ARG(QString, QStringLiteral("content")))) {
      return fail("Carousel board role and layout menus should expose choices and apply roles");
    }

    visual_workspace->setProperty("inspectorPrimarySurface", QStringLiteral("designs"));
    process_layout_events(6);
    if (!capture_carousel_state(
            QStringLiteral("carousel-template-browser"),
            1280,
            720,
            required_with({QStringLiteral("carouselReusableDesignTemplatesSection"),
                           QStringLiteral("carouselTemplateLibraryCard")}),
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("template-browser")}})) {
      return fail("Carousel visual regression should capture the template browser");
    }

    visual_workspace->setProperty("inspectorPrimarySurface", QStringLiteral("board"));
    carousel_document.selectSlide(0);
    carousel_document.addTextItem(QStringLiteral("Composer proof"));
    carousel_document.addShapeItem(QStringLiteral("rectangle"));
    process_layout_events(6);
    auto* board_properties_section = find_named_object(
        visual_root, QStringLiteral("carouselBoardPropertiesSurface"));
    auto* layers_section = find_named_object(
        visual_root, QStringLiteral("carouselLayersSection"));
    if (!carousel_document.selectionDetails().value(QStringLiteral("hasSelection")).toBool() ||
        board_properties_section == nullptr || layers_section == nullptr ||
        board_properties_section->property("expanded").toBool() ||
        layers_section->property("expanded").toBool()) {
      return fail("Selecting a Carousel object should bring its inspector controls ahead of board and layer settings");
    }
    auto* routine_status_surface = find_named_object(
        visual_root, QStringLiteral("carouselStatusSurface"));
    auto* routine_status_toast = find_named_object(
        visual_root, QStringLiteral("carouselStatusToast"));
    if (routine_status_surface == nullptr || routine_status_surface->property("visible").toBool() ||
        routine_status_toast == nullptr) {
      return fail("Routine Carousel actions should use temporary canvas feedback without a layout banner");
    }
    if (!capture_carousel_state(
            QStringLiteral("carousel-composer"),
            1280,
            720,
            required_with({QStringLiteral("carouselBoardPropertiesSurface"),
                           QStringLiteral("carouselLayersSection"),
                           QStringLiteral("carouselSelectionTransformSection")}),
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("composer")}})) {
      return fail("Carousel visual regression should capture the composer");
    }

    carousel_document.addShapeItem(QStringLiteral("ellipse"));
    const auto multi_select_items = carousel_document.canvasItems();
    if (multi_select_items.size() < 3) {
      return fail("Carousel multi-select visual fixture should create three editable layers");
    }
    carousel_document.selectItem(
        multi_select_items.at(multi_select_items.size() - 2).toMap()
            .value(QStringLiteral("itemId")).toString());
    carousel_document.toggleItemSelection(
        multi_select_items.constLast().toMap().value(QStringLiteral("itemId")).toString());
    process_layout_events(6);
    if (!capture_carousel_state(
            QStringLiteral("carousel-multi-select"),
            1280,
            720,
            required_with({QStringLiteral("carouselLayersSection"),
                           QStringLiteral("carouselSelectionTransformSection")}),
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("multi-select")},
                        {QStringLiteral("selected_layers"), 2}})) {
      return fail("Carousel visual regression should capture multi-selection");
    }

    const auto crop_fixture_path =
        runtime_root.filePath(QStringLiteral("carousel-crop-fixture.png"));
    if (!write_overlay_visual_fixture(crop_fixture_path, QStringLiteral("saturated")) ||
        !carousel_document.dropMediaOnTarget(
            crop_fixture_path,
            QStringLiteral("photo"),
            0,
            QStringLiteral("layer-stack"),
            QStringLiteral("primary"),
            0.5,
            0.5,
            QStringLiteral("Crop fixture"))) {
      return fail("Carousel crop visual fixture should add a real image layer");
    }
    QString crop_item_id;
    for (const auto& value : carousel_document.canvasItems()) {
      const auto item = value.toMap();
      if (item.value(QStringLiteral("type")).toString() == QStringLiteral("image")) {
        crop_item_id = item.value(QStringLiteral("itemId")).toString();
      }
    }
    if (crop_item_id.isEmpty()) {
      return fail("Carousel crop visual fixture should expose its image layer identity");
    }
    carousel_document.selectItem(crop_item_id);
    auto* crop_toggle_button = find_named_object(
        visual_root, QStringLiteral("carouselFocusedCropToggleButton"));
    if (crop_toggle_button == nullptr || !QMetaObject::invokeMethod(crop_toggle_button, "clicked") ||
        !visual_workspace->property("focusedCropMode").toBool() ||
        carousel_document.selectionDetails().value(QStringLiteral("cropMode")).toString() != QStringLiteral("manual")) {
      return fail("Opening focused crop should activate manual framing for the selected media");
    }
    process_layout_events(10);
    auto* focused_crop_controls = find_named_item(
        visual_root, QStringLiteral("carouselFocusedCropControls"));
    auto* inspector_panel = find_named_item(
        visual_root, QStringLiteral("carouselInspectorPanel"));
    if (focused_crop_controls == nullptr || inspector_panel == nullptr ||
        !wait_for_condition([&]() {
          return scene_rect(inspector_panel).contains(scene_rect(focused_crop_controls));
        }, 1000)) {
      if (focused_crop_controls != nullptr && inspector_panel != nullptr) {
        const auto controls = scene_rect(focused_crop_controls);
        const auto inspector = scene_rect(inspector_panel);
        std::cerr << "Focused crop controls rect=" << controls.x() << ',' << controls.y()
                  << ' ' << controls.width() << 'x' << controls.height()
                  << " inspector=" << inspector.x() << ',' << inspector.y()
                  << ' ' << inspector.width() << 'x' << inspector.height()
                  << " isImage=" << carousel_document.selectionDetails().value(QStringLiteral("isImage")).toBool()
                  << '\n';
      }
      return fail("Focused crop should reveal its controls inside the Carousel inspector");
    }
    auto* crop_source_combo = find_named_object(
        visual_root, QStringLiteral("carouselSelectedMediaCropSourceCombo"));
    if (crop_source_combo == nullptr || !crop_source_combo->setProperty("currentIndex", 2) ||
        !QMetaObject::invokeMethod(crop_source_combo, "activated", Q_ARG(int, 2)) ||
        carousel_document.selectionDetails().value(QStringLiteral("cropSource")).toString() != QStringLiteral("carousel") ||
        !crop_source_combo->setProperty("currentIndex", 0) ||
        !QMetaObject::invokeMethod(crop_source_combo, "activated", Q_ARG(int, 0)) ||
        carousel_document.selectionDetails().value(QStringLiteral("cropSource")).toString() != QStringLiteral("edit")) {
      return fail("The crop source dropdown should apply and restore the selected media's crop source");
    }
    if (!capture_carousel_state(
            QStringLiteral("carousel-focused-crop"),
            1280,
            720,
            required_with({QStringLiteral("carouselFocusedCropOverlay"),
                           QStringLiteral("carouselFocusedCropControls"),
                           QStringLiteral("carouselSelectedMediaCropSourceCombo")}),
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("crop")}})) {
      return fail("Carousel visual regression should capture focused crop mode");
    }

    visual_workspace->setProperty("focusedCropMode", false);
    carousel_document.selectSlide(1);
    carousel_document.assignPrimaryImage(
        runtime_root.filePath(QStringLiteral("missing-resource.png")),
        QStringLiteral("Missing resource"));
    process_layout_events(8);
    auto* board_properties_item = qobject_cast<QQuickItem*>(board_properties_section);
    if (board_properties_item == nullptr || !board_properties_section->property("expanded").toBool() ||
        !layers_section->property("expanded").toBool() ||
        !wait_for_condition([&]() {
          const auto board_top = scene_rect(board_properties_item).top();
          const auto inspector = scene_rect(inspector_panel);
          return board_top >= inspector.top() && board_top < inspector.top() + 100;
        }, 1000)) {
      return fail("Returning to a Carousel board should restore its settings at the top of the inspector");
    }
    if (!capture_carousel_state(
            QStringLiteral("carousel-missing-resource"),
            1280,
            720,
            required_with({QStringLiteral("carouselRelinkMissingMediaButton")}),
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("missing-resource")},
                        {QStringLiteral("missing_media"), carousel_document.missingMediaCount()}})) {
      return fail("Carousel visual regression should capture missing-resource recovery");
    }

    auto* preflight_panel =
        find_named_object(visual_root, QStringLiteral("carouselPreflightPanel"));
    if (preflight_panel == nullptr || !preflight_panel->setProperty("expanded", true)) {
      return fail("Carousel visual regression should expand Preflight");
    }
    process_layout_events(6);
    auto* inspector_scroll =
        find_named_object(visual_root, QStringLiteral("carouselInspectorScroll"));
    auto* inspector_flickable = inspector_scroll != nullptr
        ? inspector_scroll->property("contentItem").value<QObject*>()
        : nullptr;
    if (inspector_flickable == nullptr ||
        !inspector_flickable->setProperty(
            "contentY",
            std::max(
                0.0,
                inspector_flickable->property("contentHeight").toDouble() -
                    inspector_flickable->property("height").toDouble()))) {
      return fail("Carousel visual regression should reveal expanded Preflight in the inspector");
    }
    process_layout_events(6);
    if (!capture_carousel_state(
            QStringLiteral("carousel-preflight"),
            1280,
            720,
            required_with({QStringLiteral("carouselPreflightPanel")}),
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("preflight")},
                        {QStringLiteral("blocking"), carousel_document.preflightBlockingCount()}})) {
      return fail("Carousel visual regression should capture Preflight");
    }

    QVariant export_open_result;
    QMetaObject::invokeMethod(
        visual_workspace,
        "runCarouselCommand",
        Q_RETURN_ARG(QVariant, export_open_result),
        Q_ARG(QVariant, QStringLiteral("export.open")));
    process_layout_events(8);
    if (!capture_carousel_state(
            QStringLiteral("carousel-export"),
            1280,
            720,
            {QStringLiteral("carouselWorkspace"), QStringLiteral("carouselExportSheet"),
             QStringLiteral("carouselExportSettingsTab"),
             QStringLiteral("carouselExportReadyStatus")},
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("export")}})) {
      return fail("Carousel visual regression should capture Export review");
    }
    auto* export_preflight = qobject_cast<QQuickItem*>(find_named_object(
        visual_root, QStringLiteral("carouselExportPreflightSection")));
    auto* export_board_review = qobject_cast<QQuickItem*>(find_named_object(
        visual_root, QStringLiteral("carouselExportOrderedBoardReview")));
    auto* export_checks_tab = find_named_object(visual_root, QStringLiteral("carouselExportChecksTab"));
    auto* export_settings_tab = find_named_object(visual_root, QStringLiteral("carouselExportSettingsTab"));
    if (export_preflight == nullptr || export_board_review == nullptr ||
        !send_activation_key(export_checks_tab) || !wait_for_condition([&] { return export_preflight->isVisible(); }) ||
        !send_activation_key(export_settings_tab) || !wait_for_condition([&] { return !export_preflight->isVisible(); })) {
      return fail("Carousel export should expose scoped checks on its dedicated page and return to Output");
    }
    if (auto* export_sheet =
            find_named_object(visual_root, QStringLiteral("carouselExportSheet"));
        export_sheet != nullptr) {
      auto* review_button = find_named_object(
          visual_root, QStringLiteral("carouselExportReviewBoardsButton"));
      auto* export_scroll = find_named_object(
          visual_root, QStringLiteral("carouselExportScroll"));
      auto* export_flickable = export_scroll != nullptr
          ? export_scroll->property("contentItem").value<QObject*>()
          : nullptr;
      auto* preset_section = find_named_object(
          visual_root, QStringLiteral("carouselExportPresetSection"));
      if (export_sheet->property("activePage").toInt() != 0 ||
          export_sheet->property("boardReviewExpanded").toBool() ||
          review_button == nullptr || export_board_review->isVisible() ||
          !QMetaObject::invokeMethod(review_button, "clicked") ||
          !wait_for_condition([&]() {
            return export_board_review->isVisible();
          }, 3000) ||
          !capture_carousel_state(
              QStringLiteral("carousel-export-boards"), 1280, 720,
              {QStringLiteral("carouselWorkspace"), QStringLiteral("carouselExportSheet"),
               QStringLiteral("carouselExportOrderedBoardReview")},
              QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("export-boards")}}) ||
          preset_section == nullptr || export_flickable == nullptr ||
          preset_section->property("visible").toBool() ||
          !export_flickable->setProperty("contentY", 120.0) ||
          !export_sheet->setProperty("activePage", 1) ||
          !wait_for_condition([&]() {
            return preset_section->property("visible").toBool() &&
                   export_flickable->property("contentY").toDouble() < 1.0;
          }, 3000) ||
          !capture_carousel_state(
              QStringLiteral("carousel-export-presets"), 1280, 720,
              {QStringLiteral("carouselWorkspace"), QStringLiteral("carouselExportSheet"),
               QStringLiteral("carouselExportPresetBrowser")},
              QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("export-presets")}})) {
        return fail("Carousel export should separate core settings from delivery presets");
      }
      auto* preset_browser = find_named_object(
          visual_root, QStringLiteral("carouselExportPresetBrowser"));
      auto* review_preset_button = find_named_object(visual_root, QStringLiteral("carouselApplyExportPresetButton"));
      if (preset_browser == nullptr || review_preset_button == nullptr) return fail("Carousel export should provide its preset selector and Apply control");
      const auto presets = carousel_document.exportPresets();
      int review_index = -1;
      for (int index = 0; index < presets.size(); ++index)
        if (presets.at(index).toMap().value(QStringLiteral("presetId")).toString() == QStringLiteral("review-package")) review_index = index;
      if (review_index < 0 || !preset_browser->setProperty("currentIndex", review_index) ||
          !QMetaObject::invokeMethod(preset_browser, "activated", Q_ARG(int, review_index)))
        return fail("Carousel export should select its compatible review preset");
      process_layout_events(4);
      if (!QMetaObject::invokeMethod(review_preset_button, "clicked") ||
          !wait_for_condition([&]() {
            return export_sheet->property("activePage").toInt() == 0 &&
                   carousel_document.exportPresetState()
                           .value(QStringLiteral("activePresetId")).toString() ==
                       QStringLiteral("review-package");
          }, 3000)) {
        return fail("Applying a Carousel delivery preset should return to resolved export settings");
      }
      QMetaObject::invokeMethod(export_sheet, "close");
    }
    process_layout_events(4);

    // Keep the missing-media proof isolated so the responsive captures show a usable document.
    carousel_document.createCarouselWithSettings(
        QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Carousel Responsive Review")},
                    {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
                    {QStringLiteral("slideCount"), 3}});
    if (!carousel_document.dropMediaOnTarget(
            crop_fixture_path, QStringLiteral("photo"), 0, QStringLiteral("layer-stack"),
            QStringLiteral("primary"), 0.5, 0.5, QStringLiteral("Responsive fixture"))) {
      return fail("Carousel responsive review should add its valid media fixture");
    }
    carousel_document.setExportBoardScope(QStringLiteral("current"));
    carousel_document.setExportDirectory(runtime_root.path());
    QVariant ready_export_result;
    QMetaObject::invokeMethod(
        visual_workspace, "runCarouselCommand", Q_RETURN_ARG(QVariant, ready_export_result),
        Q_ARG(QVariant, QStringLiteral("export.open")));
    if (!wait_for_condition([&]() {
          auto* ready_sheet = find_named_object(
              visual_root, QStringLiteral("carouselExportSheet"));
          auto* start_button = find_named_object(
              visual_root, QStringLiteral("carouselStartExportButton"));
          return ready_sheet != nullptr && ready_sheet->property("visible").toBool() &&
                 start_button != nullptr && start_button->property("enabled").toBool();
        }, 3000) ||
        !capture_carousel_state(
            QStringLiteral("carousel-export-ready"), 1280, 720,
            {QStringLiteral("carouselWorkspace"), QStringLiteral("carouselExportSheet"),
             QStringLiteral("carouselExportReadyStatus"),
             QStringLiteral("carouselStartExportButton")},
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("export-ready")}})) {
      return fail("Carousel export should show a ready state for valid scoped media");
    }
    if (auto* ready_sheet = find_named_object(
            visual_root, QStringLiteral("carouselExportSheet"));
        ready_sheet != nullptr) {
      QMetaObject::invokeMethod(ready_sheet, "close");
    }
    carousel_document.setExportBoardScope(QStringLiteral("all"));
    process_layout_events(6);

    if (!capture_carousel_state(
            QStringLiteral("carousel-compact"),
            760,
            600,
            required_with({QStringLiteral("carouselCompactInspectorButton")}),
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("compact")}})) {
      return fail("Carousel visual regression should capture compact layout");
    }
    auto* first_thumb = qobject_cast<QQuickItem*>(
        find_named_object(visual_root, QStringLiteral("carouselBoardThumbnail1")));
    auto* first_thumb_label = first_thumb != nullptr
        ? qobject_cast<QQuickItem*>(
              find_named_object(first_thumb, QStringLiteral("carouselBoardThumbLabel")))
        : nullptr;
    if (first_thumb == nullptr || first_thumb_label == nullptr ||
        !first_thumb->mapRectToScene(first_thumb->boundingRect()).adjusted(-1, -1, 1, 1)
             .contains(first_thumb_label->mapRectToScene(first_thumb_label->boundingRect()))) {
      return fail("Compact Carousel board thumbnails should keep their labels inside the strip");
    }
    if (!capture_carousel_state(
            QStringLiteral("carousel-wide"),
            1920,
            1080,
            required_with({QStringLiteral("carouselInspectorInlineHost")}),
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("wide")}})) {
      return fail("Carousel visual regression should capture wide layout");
    }

    app_shell.setInterfaceScale(1.5);
    process_layout_events(6);
    if (!capture_carousel_state(
            QStringLiteral("carousel-high-dpi"),
            1600,
            1000,
            base_required,
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("high-dpi")},
                        {QStringLiteral("interface_scale"), app_shell.interfaceScale()},
                        {QStringLiteral("device_pixel_ratio"), visual_window->devicePixelRatio()}})) {
      return fail("Carousel visual regression should capture high-DPI scaling");
    }

    app_shell.setInterfaceScale(1.0);
    app_shell.setReducedMotion(true);
    process_layout_events(6);
    if (!capture_carousel_state(
            QStringLiteral("carousel-reduced-motion"),
            1280,
            720,
            base_required,
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("reduced-motion")},
                        {QStringLiteral("reduced_motion"), app_shell.reducedMotion()}})) {
      return fail("Carousel visual regression should capture reduced-motion UI");
    }

    carousel_document.clearStatusNotices();
    const auto missing_status_fixture =
        runtime_root.filePath(QStringLiteral("carousel-unreadable-status.png"));
    if (carousel_document.dropMediaOnTarget(
            missing_status_fixture, QStringLiteral("photo"), 0,
            QStringLiteral("layer-stack"), QStringLiteral("primary"), 0.5, 0.5,
            QStringLiteral("Unreadable status fixture"))) {
      return fail("Carousel status proof should reject its unreadable image fixture");
    }
    process_layout_events(6);
    auto* retained_status_surface = find_named_object(
        visual_root, QStringLiteral("carouselStatusSurface"));
    auto* retained_status_button = find_named_object(
        visual_root, QStringLiteral("carouselStatusDetailsButton"));
    if (carousel_document.statusNotices().size() != 1 ||
        retained_status_surface == nullptr ||
        !retained_status_surface->property("visible").toBool() ||
        retained_status_button == nullptr ||
        !retained_status_button->property("visible").toBool() ||
        routine_status_toast->property("visible").toBool() ||
        !capture_carousel_state(
            QStringLiteral("carousel-retained-error"), 1280, 720,
            required_with({QStringLiteral("carouselStatusSurface"),
                           QStringLiteral("carouselStatusDetailsButton")}),
            QJsonObject{{QStringLiteral("state_contract"),
                         QStringLiteral("retained-error")}})) {
      return fail("Carousel failures should stay visible with a retained-issues entry");
    }
    carousel_document.addShapeItem(QStringLiteral("rectangle"));
    process_layout_events(4);
    if (retained_status_surface->property("visible").toBool() ||
        !retained_status_button->property("visible").toBool() ||
        carousel_document.statusNotices().size() != 1 ||
        !routine_status_toast->property("visible").toBool() ||
        !wait_for_condition([&]() {
          return !routine_status_toast->property("visible").toBool();
        }, 4500)) {
      return fail("A later Carousel success should use temporary feedback and retain issue access");
    }

    if (carousel_visual_manifest.size() != 18) {
      return fail("Carousel visual regression manifest should contain exactly eighteen required states");
    }
    QSet<QString> visual_states;
    QSet<QString> visual_hashes;
    for (const auto& value : carousel_visual_manifest) {
      const auto entry = value.toObject();
      visual_states.insert(entry.value(QStringLiteral("state")).toString());
      visual_hashes.insert(entry.value(QStringLiteral("sha256")).toString());
      if (entry.value(QStringLiteral("sha256")).toString().size() != 64 ||
          entry.value(QStringLiteral("pixel_width")).toInt() <= 0 ||
          entry.value(QStringLiteral("pixel_height")).toInt() <= 0 ||
          entry.value(QStringLiteral("required_objects")).toArray().isEmpty()) {
        return fail("Every Carousel visual regression entry should contain a nonblank PNG hash and required-object proof");
      }
    }
    if (visual_states.size() != 18 || visual_hashes.size() < 12) {
      return fail("Carousel visual regression states should be unique and visually differentiated");
    }
    QFile carousel_visual_manifest_file(
        QDir(carousel_visual_dir).filePath(QStringLiteral("manifest.json")));
    if (!carousel_visual_manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        carousel_visual_manifest_file.write(
            QJsonDocument(carousel_visual_manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("Carousel visual regression should persist its proof manifest");
    }
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;

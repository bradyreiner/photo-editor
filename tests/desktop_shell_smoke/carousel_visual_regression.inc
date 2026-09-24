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

    if (!capture_carousel_state(
            QStringLiteral("carousel-empty"),
            1280,
            720,
            required_with({QStringLiteral("carouselEmptyBoardStartCard")}),
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("empty")}})) {
      return fail("Carousel visual regression should capture the empty state");
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
    if (!capture_carousel_state(
            QStringLiteral("carousel-composer"),
            1280,
            720,
            required_with({QStringLiteral("carouselBoardPropertiesSurface"),
                           QStringLiteral("carouselLayerRow")}),
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
            required_with({QStringLiteral("carouselLayerRow")}),
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
    visual_workspace->setProperty("focusedCropMode", true);
    process_layout_events(10);
    if (!capture_carousel_state(
            QStringLiteral("carousel-focused-crop"),
            1280,
            720,
            required_with({QStringLiteral("carouselFocusedCropOverlay")}),
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("crop")}})) {
      return fail("Carousel visual regression should capture focused crop mode");
    }

    visual_workspace->setProperty("focusedCropMode", false);
    carousel_document.selectSlide(1);
    carousel_document.assignPrimaryImage(
        runtime_root.filePath(QStringLiteral("missing-resource.png")),
        QStringLiteral("Missing resource"));
    process_layout_events(8);
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
            {QStringLiteral("carouselWorkspace"), QStringLiteral("carouselExportSheet")},
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("export")}})) {
      return fail("Carousel visual regression should capture Export review");
    }
    if (auto* export_sheet =
            find_named_object(visual_root, QStringLiteral("carouselExportSheet"));
        export_sheet != nullptr) {
      QMetaObject::invokeMethod(export_sheet, "close");
    }
    process_layout_events(4);

    if (!capture_carousel_state(
            QStringLiteral("carousel-compact"),
            760,
            600,
            required_with({QStringLiteral("carouselCompactInspectorButton")}),
            QJsonObject{{QStringLiteral("state_contract"), QStringLiteral("compact")}})) {
      return fail("Carousel visual regression should capture compact layout");
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

    if (carousel_visual_manifest.size() != 12) {
      return fail("Carousel visual regression manifest should contain exactly twelve required states");
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
    if (visual_states.size() != 12 || visual_hashes.size() < 8) {
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

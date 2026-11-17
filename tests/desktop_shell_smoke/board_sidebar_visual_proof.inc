  const auto board_source_path =
      QString::fromUtf8(qgetenv("PHOTO_EDITOR_BOARD_UI_DOCUMENT")).trimmed();
  if (board_source_path.isEmpty()) {
    return fail("Board typography proof requires PHOTO_EDITOR_BOARD_UI_DOCUMENT");
  }
  QFile board_source_file(board_source_path);
  if (!board_source_file.open(QIODevice::ReadOnly)) {
    return fail("Board typography proof should read its source document without modifying it");
  }
  const auto source_envelope = QJsonDocument::fromJson(board_source_file.readAll());
  if (!source_envelope.isObject()) {
    return fail("Board typography proof source should be a JSON document envelope");
  }
  auto source_document = source_envelope.object().value(QStringLiteral("document_json")).toObject();
  if (source_document.isEmpty()) {
    source_document = source_envelope.object().value(QStringLiteral("document")).toObject();
  }
  if (!source_document.value(QStringLiteral("slides")).isArray() ||
      source_document.value(QStringLiteral("slides")).toArray().size() != 8) {
    return fail("Board typography proof source should contain the eight-board source document");
  }

  // Load through the controller's normal document model, then save a uniquely
  // named copy under this smoke process's isolated PHOTO_EDITOR_APP_DATA_DIR.
  // The source envelope and its referenced media remain read-only.
  source_document.insert(QStringLiteral("documentName"), QStringLiteral("Native Board Text Inspector Proof"));
  // The user screenshot focused Boards 2–3. Keep the existing NEPAL title on
  // Board 2 and all source layers intact in the isolated copy.
  source_document.insert(QStringLiteral("currentIndex"), 1);
  if (!photo_editor::desktop::CarouselHistoryTestAccess::loadDocumentObject(
          carousel_document, source_document) ||
      carousel_document.slideCount() < 2 ||
      !carousel_document.saveDocument(QStringLiteral("Native Board Text Inspector Proof")) ||
      carousel_document.savedDocumentId().isEmpty()) {
    std::cerr << "Board clone setup failed: " << carousel_document.status().toStdString() << '\n';
    return fail("Board typography proof should persist only its isolated document clone");
  }

  QVariant opened;
  const auto clone_id = carousel_document.savedDocumentId();
  if (!QMetaObject::invokeMethod(root, "openSavedBoards", Q_RETURN_ARG(QVariant, opened),
                                 Q_ARG(QVariant, QVariant(clone_id))) ||
      !opened.toBool()) {
    std::cerr << "Board clone open failed: " << carousel_document.status().toStdString() << '\n';
    return fail("Open the isolated board clone through Main's regular board editing route");
  }
  QObject* edit = nullptr;
  auto* studio_window = root_window;
  if (!studio_window ||
      !wait_for_condition([&] {
        edit = root->findChild<QObject*>(QStringLiteral("editStudioWorkspace"));
        return root->property("currentWorkspace") == root->property("editWorkspaceIndex") &&
            edit != nullptr &&
            !root->property("workspaceTransitionActive").toBool() &&
            !editor_preview.processing() && !editor_preview.fullRenderRunning();
      }, 60000)) {
    return fail("The isolated board clone should reach the full Edit workspace");
  }

  auto text_layer_index = -1;
  QVariantList photo_layers;
  if (!wait_for_condition([&] {
        photo_layers = editor_preview.photoLayers();
        text_layer_index = -1;
        for (int index = 0; index < photo_layers.size(); ++index) {
          const auto layer = photo_layers.at(index).toMap();
          if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("text") &&
              layer.value(QStringLiteral("text")).toString() == QStringLiteral("NEPAL")) {
            text_layer_index = index;
            break;
          }
        }
        return text_layer_index >= 0 && !editor_preview.processing() &&
            !editor_preview.fullRenderRunning();
      }, 30000)) {
    std::cerr << "Board 2 layers: " << QJsonDocument::fromVariant(photo_layers).toJson(QJsonDocument::Compact).toStdString()
              << " editor_status=" << editor_preview.status().toStdString() << '\n';
    return fail("Open the source document's existing NEPAL text layer on Board 2");
  }
  QVariant selected;
  if (!QMetaObject::invokeMethod(edit, "setActivePhotoLayerSelection", Q_RETURN_ARG(QVariant, selected),
                                 Q_ARG(QVariant, QVariant(text_layer_index)), Q_ARG(QVariant, QVariant(false))) ||
      !selected.toBool() || !edit->property("boardTextLayerSelected").toBool()) {
    return fail("Select the source document's existing NEPAL title in the full Studio layer stack");
  }
  {
    const auto selected_layer = editor_preview.photoLayers().value(text_layer_index).toMap();
    if (selected_layer.value(QStringLiteral("text")).toString() != QStringLiteral("NEPAL")) {
      return fail("The selected Board layer should remain the source document's NEPAL title");
    }
  }
  edit->setProperty("editInspectorTab", QStringLiteral("layers"));
  edit->setProperty("editCreativeToolsVisible", true);
  root->setProperty("width", 1440);
  root->setProperty("height", 900);
  process_layout_events(12);
  auto* layout_panel = find_named_object(edit, QStringLiteral("boardLayoutInspectorGroup"));
  auto* layers_panel = find_named_object(edit, QStringLiteral("photoLayersInspectorGroup"));
  auto* tools_panel = find_named_object(edit, QStringLiteral("creativeToolsPanel"));
  auto* inspector = find_named_item(edit, QStringLiteral("editInspectorScroll"));
  if (!layout_panel || !layers_panel || !tools_panel || !inspector ||
      find_named_object(edit, QStringLiteral("boardInspectorNavigation")) ||
      layout_panel->property("expanded").toBool() ||
      !tools_panel->property("active").toBool() ||
      !tools_panel->property("contentSized").toBool()) {
    return fail("Board editing should retain stacked Layout, Layers, and existing Typography panels without sub-navigation");
  }
  auto* tool_surface = find_named_item(edit, QStringLiteral("creativeToolsSurface"));
  if (!wait_for_condition([&] {
        return tool_surface && tool_surface->width() > 100 && tool_surface->height() > 100 &&
            qobject_cast<QQuickItem*>(tools_panel)->width() > 100;
      }, 5000)) {
    return fail("The reparented Typography panel and painted surface must retain nonzero width");
  }
  const auto stack_headers = QStringList{
      QStringLiteral("boardLayoutInspectorGroupTitle"),
      QStringLiteral("photoLayersInspectorGroupTitle"),
      QStringLiteral("creativeToolsTitle"),
  };
  qreal previous_bottom = -1;
  for (const auto& name : stack_headers) {
    auto* header = find_named_item(edit, name);
    if (!header || !item_visible(header) || scene_rect(header).top() <= previous_bottom) {
      return fail("Layout, Layers, and Typography headers should stack vertically without overlap");
    }
    previous_bottom = scene_rect(header).bottom();
  }
  const auto log_tool_geometry = [&] {
    auto* title = find_named_item(edit, QStringLiteral("creativeToolsTitle"));
    for (auto* item = title; item; item = item->parentItem()) {
      const auto rect = scene_rect(item);
      std::cerr << "tool ancestor " << item->objectName().toStdString()
                << " class=" << item->metaObject()->className()
                << " scene=" << rect.x() << ',' << rect.y() << ' ' << rect.width() << 'x' << rect.height()
                << " implicit=" << item->implicitWidth() << 'x' << item->implicitHeight()
                << " visible=" << item->isVisible() << " opacity=" << item->opacity()
                << " clip=" << item->clip() << '\n';
    }
  };
  log_tool_geometry();
  if (!capture_visual_proof(studio_window, root, visual_proof_dir,
                            QStringLiteral("stacked-sidebar"), 1440, 900,
                            stack_headers, &visual_proof_manifest)) {
    return fail("Capture the existing stacked panels with Layout collapsed and Layers expanded");
  }

  auto* outer_scroll = qvariant_cast<QQuickItem*>(inspector->property("contentItem"));
  auto* tool_scroll = find_named_item(edit, QStringLiteral("creativeToolsScroll"));
  auto* inner_scroll = tool_scroll ? qvariant_cast<QQuickItem*>(tool_scroll->property("contentItem")) : nullptr;
  auto* family_picker = find_named_item(edit, QStringLiteral("photoTypographyFamilyPicker"));
  if (!outer_scroll || !inner_scroll || !family_picker)
    return fail("Find the existing inspector scroll and embedded Typography scroll");
  const auto wheel_position = scene_rect(family_picker).center();
  const auto wheel_global = QPointF(studio_window->mapToGlobal(wheel_position.toPoint()));
  QTest::mouseMove(studio_window, wheel_position.toPoint());
  process_layout_events(2);
  QWheelEvent wheel(wheel_position, wheel_global, QPoint(0, -140), QPoint(0, -120),
                    Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QCoreApplication::sendEvent(studio_window, &wheel);
  if (!wait_for_condition([&] { return outer_scroll->property("contentY").toDouble() > 1; }, 1500) ||
      std::abs(inner_scroll->property("contentY").toDouble()) > 1) {
    return fail("Wheel input over Typography should scroll the shared inspector instead of trapping input in a nested scroll");
  }
  outer_scroll->setProperty("contentY", 0);
  process_layout_events(8);

  // Collapsing a stack panel preserves the other panels and uses the same
  // outer inspector scroll, rather than switching to another inspector page.
  if (!QMetaObject::invokeMethod(layers_panel, "toggleExpanded"))
    return fail("Layers should retain the shared panel collapse action");
  process_layout_events(12);
  const auto required_text_controls = QStringList{
      QStringLiteral("photoTypographyFamilyPicker"),
      QStringLiteral("photoTypography_size"),
      QStringLiteral("photoLayerTextColorField"),
      QStringLiteral("photoTypographyQuickColorPalette"),
  };
  const auto typography_fits = [&] {
    const auto viewport = scene_rect(inspector);
    for (const auto& name : required_text_controls) {
      auto* control = find_named_item(edit, name);
      if (!control || !item_visible(control)) return false;
      const auto rect = scene_rect(control);
      if (rect.isEmpty() || !scene_rect(tool_surface).contains(rect) ||
          rect.top() < viewport.top() - 1 || rect.bottom() > viewport.bottom() + 1)
        return false;
    }
    return true;
  };
  if (!wait_for_condition(typography_fits, 5000) ||
      !capture_visual_proof(studio_window, root, visual_proof_dir,
                            QStringLiteral("stacked-typography"), 1440, 900,
                            required_text_controls, &visual_proof_manifest)) {
    return fail("Collapsing Layers should expose font, size, and color while retaining the stacked panel headers");
  }
  if (!QMetaObject::invokeMethod(layout_panel, "toggleExpanded") ||
      !wait_for_condition([&] {
        return layout_panel->property("expanded").toBool() &&
            item_visible(find_named_item(edit, QStringLiteral("boardPhotoControls"))) &&
            tools_panel->property("active").toBool() && item_visible(qobject_cast<QQuickItem*>(layers_panel));
      }, 5000) ||
      !capture_visual_proof(studio_window, root, visual_proof_dir,
                            QStringLiteral("stacked-layout-expanded"), 1920, 900,
                            {QStringLiteral("boardPhotoControls"), QStringLiteral("photoLayersInspectorGroup")},
                            &visual_proof_manifest)) {
    return fail("Layout should expand in its own panel without replacing Layers or Typography");
  }
  QFile manifest_file(QDir(visual_proof_dir).filePath(QStringLiteral("captures.json")));
  if (!manifest_file.open(QIODevice::WriteOnly)) return fail("Write the native stacked-panel capture receipt");
  manifest_file.write(QJsonDocument(visual_proof_manifest).toJson());
  std::cout << "Board sidebar: existing Layout, Layers, and Typography panels stack without sub-tabs; neutral contrast, collapse actions, and font/size/color access passed at 1440×900 and 1920×900\n";
  return EXIT_SUCCESS;

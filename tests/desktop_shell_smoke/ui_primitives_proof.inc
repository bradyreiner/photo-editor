    QStringList primitives_warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError>& warnings) {
      for (const auto& warning : warnings) {
        if (warning.url().toString().contains(QStringLiteral("UiPrimitive")))
          primitives_warnings.append(warning.toString());
      }
    });
    engine.load(QUrl::fromLocalFile(main_qml_path));
    process_layout_events(8);
    if (engine.rootObjects().isEmpty()) return fail("Primitives proof should load the actual app shell");
    auto* shell = engine.rootObjects().constFirst();
    const auto original_density = app_shell.property("interfaceDensity");
    const auto original_motion = app_shell.property("reducedMotion");
    const auto original_contrast = app_shell.property("highContrast");
    const auto original_workspace = shell->property("currentWorkspace");
    if (find_named_object(shell, QStringLiteral("uiPrimitivesWindow")) != nullptr)
      return fail("Primitives page should be lazy before opening");
    auto* action = find_named_object(shell, QStringLiteral("uiPrimitivesAction"));
    if (action == nullptr || action->property("text").toString() != QStringLiteral("UI Primitives") ||
        !QMetaObject::invokeMethod(shell, "openUiPrimitivesPage"))
      return fail("Actual app route should open the primitives page");
    auto* page = find_named_object(shell, QStringLiteral("uiPrimitivesWindow"));
    auto* page_window = qobject_cast<QQuickWindow*>(page);
    if (page_window == nullptr || !wait_for_condition([&] { return page_window->isVisible(); }))
      return fail("Primitives page should open a visible native window");
    if (page->property("previewFontFamily").toString() != app_shell.systemUiFontFamily())
      return fail("Gallery must use the application's native UI typeface");

    const auto proof_dir = qEnvironmentVariableIsSet("PHOTO_EDITOR_VISUAL_PROOF_DIR")
        ? QString::fromUtf8(qgetenv("PHOTO_EDITOR_VISUAL_PROOF_DIR"))
        : runtime_root.filePath(QStringLiteral("ui-primitives-proof"));
    if (!QDir().mkpath(proof_dir)) return fail("Primitives proof should create its artifact directory");
    QJsonArray manifest;
    const auto capture = [&](const QString& state, const QStringList& required, int width = 1220, int height = 820) {
      // Capture resting states after the shared 110-140 ms color transitions.
      QElapsedTimer state_settle;
      state_settle.start();
      if (!wait_for_condition([&] { return state_settle.elapsed() >= 160; }, 500)) return false;
      return capture_visual_proof(page_window, page, proof_dir, state, width, height, required, &manifest);
    };
    const auto navigate = [&](int index) {
      auto* button = find_named_object(page, QStringLiteral("uiPrimitivesSection-") + QString::number(index));
      return send_activation_key(button) && wait_for_condition([&] {
        return page->property("sectionIndex").toInt() == index;
      });
    };
    const auto scroll_to_bottom = [&] {
      auto* scroll = find_named_object(page, QStringLiteral("uiPrimitivesScroll"));
      auto* flick = scroll == nullptr ? nullptr : scroll->property("contentItem").value<QObject*>();
      if (flick == nullptr) return false;
      flick->setProperty("contentY", std::max(0.0, flick->property("contentHeight").toDouble() -
                                                   flick->property("height").toDouble()));
      process_layout_events(8);
      return true;
    };
    const auto preview_densities = QStringList{QStringLiteral("comfortable"), QStringLiteral("balanced"), QStringLiteral("compact")};
    const auto sections = QStringList{QStringLiteral("foundations"), QStringLiteral("commands"), QStringLiteral("inputs"),
                                     QStringLiteral("feedback"), QStringLiteral("icons"), QStringLiteral("index")};
    const QList<QStringList> required_sections{
        {QStringLiteral("uiPrimitivesFoundations")},
        {QStringLiteral("uiPrimitivesActionPrimary"), QStringLiteral("uiPrimitivesToolSelect")},
        {QStringLiteral("uiPrimitivesTextField"), QStringLiteral("uiPrimitivesSpinBox")},
        {QStringLiteral("uiPrimitivesFeedbackNavigationPanel"), QStringLiteral("uiPrimitivesFeedbackStatusPanel"),
         QStringLiteral("uiPrimitivesStatusNeutral")},
        {QStringLiteral("uiPrimitivesIcons")},
        {QStringLiteral("uiPrimitivesCatalogSearch"), QStringLiteral("uiPrimitivesCatalogCount")}};
    for (const auto& density : preview_densities) {
      if (!send_activation_key(find_named_object(page, QStringLiteral("uiPrimitivesDensity-") + density)) ||
          page->property("renderedDensity").toString() != density)
        return fail("Local preview should change density through a real keyboard-operable control");
      if (!navigate(0) || !capture(density + QStringLiteral("-optical-idle"), {QStringLiteral("uiPrimitivesOpticalDetails")}))
        return fail("Optical interaction workbench should open in Foundations");
      auto* optical = find_named_object(page, QStringLiteral("uiPrimitivesOpticalDetails"));
      auto* optical_button = qobject_cast<QQuickItem*>(find_named_object(page, QStringLiteral("uiPrimitivesOpticalSelected")));
      if (optical == nullptr || optical_button == nullptr || !item_visible(optical_button))
        return fail("Optical workbench should expose its real shared controls");
      const auto button_size = QSizeF(optical_button->width(), optical_button->height());
      const auto pointer = [&](QEvent::Type type, Qt::MouseButton button, Qt::MouseButtons buttons) {
        const auto point = optical_button->mapToScene(QPointF(optical_button->width() / 2, optical_button->height() / 2));
        const auto global_point = QPointF(page_window->mapToGlobal(point.toPoint()));
        QMouseEvent event(type, point, global_point, button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(page_window, &event);
        process_layout_events(3);
      };
      pointer(QEvent::MouseMove, Qt::NoButton, Qt::NoButton);
      if (!optical_button->property("hovered").toBool() ||
          !capture(density + QStringLiteral("-optical-hover"), {QStringLiteral("uiPrimitivesOpticalSelected")}))
        return fail("Optical hover should respond to the real pointer");
      pointer(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
      if (!optical_button->property("down").toBool() ||
          !capture(density + QStringLiteral("-optical-pressed"), {QStringLiteral("uiPrimitivesOpticalSelected")}))
        return fail("Optical pressed finish should render while holding the pointer");
      pointer(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
      if (!optical->property("sampleActive").toBool() ||
          QSizeF(optical_button->width(), optical_button->height()) != button_size)
        return fail("Optical interaction should select without changing control geometry");
      if (!send_activation_key(find_named_object(page, QStringLiteral("uiPrimitivesOpticalTooltipButton"))) ||
          !capture(density + QStringLiteral("-optical-tooltip"), {QStringLiteral("uiPrimitivesOpticalTooltip")}))
        return fail("Optical tooltip should display text and shortcut together");
      if (!send_activation_key(find_named_object(page, QStringLiteral("uiPrimitivesOpticalTooltipButton"))) ||
          !send_activation_key(find_named_object(page, QStringLiteral("uiPrimitivesOpticalMenuPrimaryAction"))) ||
          !capture(density + QStringLiteral("-optical-menu"), {QStringLiteral("uiPrimitivesOpticalMenuPopup")}))
        return fail("Optical menu should render actual selected, disabled and destructive items");
      if (!send_activation_key(find_named_object(page, QStringLiteral("uiPrimitivesOpticalMenuPopupItem0")), Qt::Key_Return) ||
          optical->property("lastAction").toString() != QStringLiteral("Layer action · duplicate"))
        return fail("Optical menu selection should perform its local action");
      auto* warning = find_named_object(page, QStringLiteral("uiPrimitivesOpticalWarning"));
      if (warning == nullptr || !QMetaObject::invokeMethod(warning, "actionTriggered") ||
          warning->property("tone").toString() != QStringLiteral("success"))
        return fail("Optical warning should offer an actionable recovery");
      auto* activity = find_named_object(page, QStringLiteral("uiPrimitivesOpticalBusyActivity"));
      if (activity == nullptr || !send_activation_key(find_named_object(page, QStringLiteral("uiPrimitivesReducedMotion"))) ||
          activity->property("animating").toBool())
        return fail("Reduced motion should stop aperture animation");
      if (!send_activation_key(find_named_object(page, QStringLiteral("uiPrimitivesReducedMotion"))) ||
          !activity->property("animating").toBool() ||
          !send_activation_key(find_named_object(page, QStringLiteral("uiPrimitivesOpticalBusyToggle"))) ||
          optical->property("sampleBusy").toBool())
        return fail("Optical activity should resume motion and settle when complete");
      for (auto index = 0; index < static_cast<int>(sections.size()); ++index) {
        if (!navigate(index) || !capture(density + QStringLiteral("-") + sections[index],
                                         required_sections[index]))
          return fail("Every primitives section should render at each density");
        if (index == 2) {
          auto* field = find_named_object(page, QStringLiteral("uiPrimitivesTextField"));
          const auto expected_height = density == QStringLiteral("comfortable") ? 36 : density == QStringLiteral("compact") ? 30 : 32;
          if (field == nullptr || field->property("implicitHeight").toInt() != expected_height ||
              field->property("font").value<QFont>().family() != app_shell.systemUiFontFamily())
            return fail("Real nested controls must inherit local density, not the live application controller");
        }
        if (index <= 4) {
          const auto bottom_required = index == 0
              ? QStringList{QStringLiteral("uiPrimitivesGeometryReadout")}
              : index == 4
                  ? QStringList{QStringLiteral("uiPrimitivesIcons")}
              : index == 2
              ? QStringList{QStringLiteral("uiPrimitivesInspectorSection"), QStringLiteral("uiPrimitivesStudioCollapsibleGroup")}
              : index == 3
                  ? QStringList{QStringLiteral("uiPrimitivesFeedbackActivityPanel"), QStringLiteral("uiPrimitivesFeedbackInformationPanel"),
                                QStringLiteral("uiPrimitivesStudioMetadataRow"), QStringLiteral("uiPrimitivesStudioEmptyState")}
                  : QStringList{QStringLiteral("uiPrimitivesFilterAll"), QStringLiteral("uiPrimitivesCommandsOpenIssues")};
          if (!scroll_to_bottom() || !capture(density + QStringLiteral("-") + sections[index] + QStringLiteral("-bottom"),
                                               bottom_required))
            return fail("Specimens should scroll without moving the page navigation");
          if (index == 2 &&
              (!visible_named_items_do_not_overlap(page, {QStringLiteral("uiPrimitivesInspectorSection"), QStringLiteral("uiPrimitivesStudioCollapsibleGroup")}) ||
               !visible_named_items_fit_parent(page, QStringLiteral("uiPrimitivesInspectorSection"),
                                                {QStringLiteral("uiPrimitivesInspectorSectionField"), QStringLiteral("uiPrimitivesInspectorSectionResetButton")})))
            return fail("Inspector comparisons must have visible, contained, nonoverlapping bodies and headers");
          if (index == 3 &&
              (!visible_named_items_fit_parent(page, QStringLiteral("uiPrimitivesFeedbackStatusPanel"),
                                                {QStringLiteral("uiPrimitivesStatusNeutral"), QStringLiteral("uiPrimitivesStatusBusy")}) ||
               !visible_named_items_fit_parent(page, QStringLiteral("uiPrimitivesFeedbackInformationPanel"),
                                                {QStringLiteral("uiPrimitivesStudioMetadataRow"), QStringLiteral("uiPrimitivesStudioEmptyState")})))
            return fail("Feedback specimens must fit their actual panel surfaces");
        }
      }
    }

    if (!navigate(1) || !send_activation_key(find_named_object(page, QStringLiteral("uiPrimitivesActionActive"))))
      return fail("Live action sample should be keyboard operable");
    auto* commands = find_named_object(page, QStringLiteral("uiPrimitivesCommandsPage"));
    if (commands == nullptr || !commands->property("sampleActionActive").toBool())
      return fail("Action sample should update only its local selected state");
    if (!capture(QStringLiteral("command-selected-focus"), {QStringLiteral("uiPrimitivesActionActive")}))
      return fail("Primitives proof should capture selected and focus states");
    auto* selected_action = qobject_cast<QQuickItem*>(find_named_object(page, QStringLiteral("uiPrimitivesActionActive")));
    auto* primary_action = qobject_cast<QQuickItem*>(find_named_object(page, QStringLiteral("uiPrimitivesActionPrimary")));
    if (!selected_action || !primary_action) return fail("Shared action specimens should be available");
    auto* selected_surface = selected_action->property("background").value<QObject*>();
    auto* primary_surface = primary_action->property("background").value<QObject*>();
    if (!selected_surface || !primary_surface || selected_surface->property("borderWidth").toDouble() < 2)
      return fail("Keyboard selection must retain a visible focus indicator");
    primary_action->forceActiveFocus(Qt::MouseFocusReason);
    const auto selected_point = selected_action->mapToScene(QPointF(selected_action->width() / 2, selected_action->height() / 2));
    const auto selected_global = QPointF(page_window->mapToGlobal(selected_point.toPoint()));
    QCursor::setPos(selected_global.toPoint());
    for (int click = 0; click < 2; ++click) {
      QMouseEvent press(QEvent::MouseButtonPress, selected_point, selected_global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QCoreApplication::sendEvent(page_window, &press);
      process_layout_events(2);
      QMouseEvent release(QEvent::MouseButtonRelease, selected_point, selected_global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QCoreApplication::sendEvent(page_window, &release);
      process_layout_events(2);
    }
    process_layout_events(3);
    if (!commands->property("sampleActionActive").toBool() || selected_surface->property("borderWidth").toDouble() != 0)
      return fail("Pointer selection should not add an outline");
    const auto primary_idle_color = primary_surface->property("fillColor").value<QColor>();
    if (!primary_idle_color.isValid() || primary_idle_color.hsvSaturationF() > 0.20)
      return fail("Primary command surfaces should remain neutral graphite");
    const auto primary_geometry = QSizeF(primary_action->width(), primary_action->height());
    const auto hover_point = primary_action->mapToScene(QPointF(primary_action->width() / 2, primary_action->height() / 2));
    const auto hover_global = QPointF(page_window->mapToGlobal(hover_point.toPoint()));
    QCursor::setPos(hover_global.toPoint());
    QMouseEvent primary_move(QEvent::MouseMove, hover_point, hover_global, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(page_window, &primary_move);
    if (!wait_for_condition([&] { return primary_action->property("hovered").toBool(); }) ||
        !capture(QStringLiteral("command-pointer-hover"), {QStringLiteral("uiPrimitivesActionPrimary")}))
      return fail("Primary hover should respond to the actual pointer");
    const auto primary_hover_color = primary_surface->property("fillColor").value<QColor>();
    if (primary_surface->property("borderWidth").toDouble() != 0 ||
        primary_hover_color.hsvSaturationF() > 0.20 ||
        std::abs(primary_hover_color.valueF() - primary_idle_color.valueF()) > 0.08 ||
        QSizeF(primary_action->width(), primary_action->height()) != primary_geometry)
      return fail("Hover should be a subtle neutral fill without an edge or geometry change");
    selected_action->forceActiveFocus(Qt::TabFocusReason);
    if (!send_activation_key(primary_action) ||
        commands->property("lastAction").toString() != QStringLiteral("Primary action · Apply") ||
        !primary_action->property("visualFocus").toBool() || primary_surface->property("borderWidth").toDouble() < 2)
      return fail("Primary commands must remain keyboard operable with visible focus");
    if (!send_activation_key(find_named_object(page, QStringLiteral("uiPrimitivesMenuPrimaryPrimaryAction"))))
      return fail("Gallery menu should open from its real primary button");
    auto* menu_popup = find_named_object(page, QStringLiteral("uiPrimitivesMenuPrimaryPopup"));
    if (menu_popup == nullptr || !wait_for_condition([&] { return menu_popup->property("visible").toBool(); }) ||
        !capture(QStringLiteral("command-menu-open"), {QStringLiteral("uiPrimitivesMenuPrimaryPopup")}))
      return fail("Gallery should render the actual menu popup");
    auto* first_menu_item = find_named_object(page, QStringLiteral("uiPrimitivesMenuPrimaryPopupItem0"));
    if (!send_activation_key(first_menu_item, Qt::Key_Return) ||
        commands->property("lastAction").toString() != QStringLiteral("Menu choice · video"))
      return fail("Menu keyboard selection should run the local sample action");

    if (!navigate(2)) return fail("Inputs navigation should work");
    auto* field = qobject_cast<QQuickItem*>(find_named_object(page, QStringLiteral("uiPrimitivesTextField")));
    if (field == nullptr) return fail("Inputs should expose the actual text field");
    field->forceActiveFocus(Qt::TabFocusReason);
    QMetaObject::invokeMethod(field, "selectAll");
    QKeyEvent typed_name(QEvent::KeyPress, 0, Qt::NoModifier, QStringLiteral("Primitives review"));
    QCoreApplication::sendEvent(page_window, &typed_name);
    if (field->property("text").toString() != QStringLiteral("Primitives review"))
      return fail("Gallery text field should accept actual keyboard entry");
    auto* exposure = find_named_object(page, QStringLiteral("uiPrimitivesExposureSlider"));
    if (exposure == nullptr || !QMetaObject::invokeMethod(exposure, "testCommitInputText", Q_ARG(QVariant, QVariant(QStringLiteral("1.75")))) ||
        std::abs(exposure->property("value").toDouble() - 1.75) > 0.001)
      return fail("Typed slider values should commit in the local sample");
    if (!send_activation_key(find_named_object(page, QStringLiteral("uiPrimitivesResetAllButton"))) ||
        exposure->property("value").toDouble() != 0 || field->property("text").toString() != QStringLiteral("Morning light"))
      return fail("Sample reset should restore fields and sliders");
    auto* combo = find_named_object(page, QStringLiteral("uiPrimitivesComboBox"));
    if (!send_activation_key(combo) ||
        !capture(QStringLiteral("input-combo-open"), {QStringLiteral("uiPrimitivesComboBoxOption0")}))
      return fail("Gallery should render actual selectable combo-box options");
    auto* combo_popup = combo->property("popup").value<QObject*>();
    if (combo_popup == nullptr || !QMetaObject::invokeMethod(combo_popup, "close"))
      return fail("Combo popup should close before leaving the sample");

    if (!navigate(3) || !send_activation_key(find_named_object(page, QStringLiteral("uiPrimitivesImportFolder")), Qt::Key_Return))
      return fail("Import folder specimen should accept keyboard activation");
    auto* folder = find_named_object(page, QStringLiteral("uiPrimitivesImportFolder"));
    auto* feedback = find_named_object(page, QStringLiteral("uiPrimitivesFeedbackPage"));
    if (feedback == nullptr || !folder->property("selected").toBool() ||
        feedback->property("lastOpenedLocation").toString() != QStringLiteral("sample://reference") ||
        !send_activation_key(find_named_object(page, QStringLiteral("uiPrimitivesImportSource")), Qt::Key_Return) ||
        folder->property("selected").toBool() ||
        feedback->property("lastOpenedLocation").toString() != QStringLiteral("sample://pictures"))
      return fail("Source/folder activation should preserve paths and update only the local selection");
    if (!send_activation_key(find_named_object(page, QStringLiteral("uiPrimitivesToggleFailureButton"))))
      return fail("Feedback sample should enter its recoverable error state");
    auto* status = find_named_object(page, QStringLiteral("uiPrimitivesStatusBusy"));
    if (status == nullptr || status->property("actionText").toString() != QStringLiteral("Retry") ||
        !QMetaObject::invokeMethod(status, "actionTriggered") || status->property("actionText").toString() != QString())
      return fail("Local feedback action should recover from the sample failure");

    if (!navigate(5)) return fail("Component index should open");
    page->setProperty("catalogQuery", QStringLiteral("InspectorSlider"));
    process_layout_events(4);
    auto* count = find_named_object(page, QStringLiteral("uiPrimitivesCatalogCount"));
    if (count == nullptr || !count->property("text").toString().startsWith(QStringLiteral("1 / ")))
      return fail("Component index should filter the shared inventory");
    if (!capture(QStringLiteral("index-search"), {QStringLiteral("uiPrimitivesCatalogSearch"), QStringLiteral("uiPrimitivesCatalogCount")}, 1000, 700))
      return fail("Component index should remain usable at minimum desktop width");
    page->setProperty("catalogQuery", QStringLiteral("no-component-has-this-name"));
    process_layout_events(4);
    if (!count->property("text").toString().startsWith(QStringLiteral("0 / ")))
      return fail("Index should show an empty result clearly");
    page->setProperty("catalogQuery", QString());
    if (!navigate(0) || !send_activation_key(find_named_object(page, QStringLiteral("uiPrimitivesHighContrast"))) ||
        !send_activation_key(find_named_object(page, QStringLiteral("uiPrimitivesReducedMotion"))) ||
        !capture(QStringLiteral("accessible-preview"), {QStringLiteral("uiPrimitivesGeometryReadout")}, 1000, 700))
      return fail("Accessibility previews should be operable and rendered");
    if (app_shell.property("interfaceDensity") != original_density ||
        app_shell.property("reducedMotion") != original_motion ||
        app_shell.property("highContrast") != original_contrast ||
        shell->property("currentWorkspace") != original_workspace)
      return fail("Gallery preview must preserve app settings and the active workspace");
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER")) {
      // Window shortcuts require an active native window. Direct Qt key
      // delivery alone does not activate the gallery on macOS.
      page_window->requestActivate();
      if (!wait_for_condition([&] { return page_window->isActive(); }, 1500))
        return fail("Native gallery must acquire keyboard focus before Escape");
    }
    if (!send_window_key(page_window, Qt::Key_Escape) || page_window->isVisible() ||
        !QMetaObject::invokeMethod(shell, "openUiPrimitivesPage") || !page_window->isVisible())
      return fail("Escape should close the page and the app route should reopen it");
    page_window->close();
    process_layout_events(3);
    // Exercise the actual Import route with a temporary photo. Opening review
    // stages a selection; confirmation is never invoked and nothing is imported.
    const auto import_source = runtime_root.filePath(QStringLiteral("button-import-source"));
    const auto import_photo = QDir(import_source).filePath(QStringLiteral("mountain.jpg"));
    if (!QDir().mkpath(import_source) || !write_overlay_visual_fixture(import_photo, QStringLiteral("saturated-small")) ||
        !QMetaObject::invokeMethod(shell, "beginImportWorkspace"))
      return fail("Button proof should open the actual Import workspace with a fixture");
    process_layout_events(8);
    auto* import_workspace = find_named_object(shell, QStringLiteral("importWorkspace"));
    auto* shell_window = qobject_cast<QQuickWindow*>(shell);
    const auto import_url = QUrl::fromLocalFile(import_source).toString();
    if (!import_workspace || !shell_window ||
        !QMetaObject::invokeMethod(import_workspace, "openSource", Q_ARG(QVariant, QVariant(import_source)),
                                  Q_ARG(QVariant, QVariant(import_url)), Q_ARG(QVariant, QVariant(QStringLiteral("Button proof photos")))) ||
        !wait_for_condition([&] { return import_workspace->property("currentFolderPath").toString() == import_source; }))
      return fail("Import fixture should load through the real source navigation");
    import_workspace->setProperty("selectedPathsByFile", QVariantMap{{import_photo, true}});
    import_workspace->setProperty("selectionRevision", import_workspace->property("selectionRevision").toInt() + 1);
    process_layout_events(8);
    auto* import_primary = find_named_object(shell, QStringLiteral("importPrimarySplitButtonPrimaryAction"));
    auto* import_menu = find_named_object(shell, QStringLiteral("importPrimarySplitButtonMenuAction"));
    auto* import_surface = import_primary ? import_primary->property("background").value<QObject*>() : nullptr;
    auto* import_menu_surface = import_menu ? import_menu->property("background").value<QObject*>() : nullptr;
    QElapsedTimer import_settle;
    import_settle.start();
    if (!import_surface || !import_menu_surface ||
        !wait_for_condition([&] { return import_settle.elapsed() >= 160 && import_primary->property("enabled").toBool(); }) ||
        !capture_visual_proof(shell_window, shell, proof_dir, QStringLiteral("import-selected-graphite"), 1440, 900,
                              {QStringLiteral("importPrimarySplitButton"), QStringLiteral("importMediaGrid")}, &manifest) ||
        import_surface->property("fillColor").value<QColor>().hsvSaturationF() > 0.20 ||
        import_surface->property("fillColor") != import_menu_surface->property("fillColor")) {
      std::cerr << "Import commands: selected=" << import_workspace->property("selectedFileCount").toInt()
                << " enabled=" << (import_primary && import_primary->property("enabled").toBool())
                << " primary=" << (import_surface ? import_surface->property("fillColor").value<QColor>().name().toStdString() : "missing")
                << " options=" << (import_menu_surface ? import_menu_surface->property("fillColor").value<QColor>().name().toStdString() : "missing") << '\n';
      return fail("The actual Import primary and options must share a neutral command surface");
    }
    if (!send_activation_key(import_primary) ||
        !wait_for_condition([&] { return import_workspace->property("canConfirmImport").toBool(); }))
      return fail("Import should remain keyboard operable and finish its review scan");
    auto* import_confirm = find_named_object(shell, QStringLiteral("importPreflightConfirmButton"));
    QElapsedTimer review_settle;
    review_settle.start();
    if (!import_confirm ||
        !wait_for_condition([&] { return review_settle.elapsed() >= 160 && import_confirm->property("enabled").toBool(); }) ||
        !capture_visual_proof(shell_window, shell, proof_dir, QStringLiteral("import-review-graphite"), 1440, 900,
                              {QStringLiteral("importPreflightConfirmButton")}, &manifest))
      return fail("Import should remain keyboard operable and open a usable review");
    import_workspace->setProperty("importProjectChooserOpen", false);
    if (!primitives_warnings.isEmpty()) {
      std::cerr << primitives_warnings.join(QChar('\n')).toStdString() << '\n';
      return fail("New primitives components should render without QML runtime warnings");
    }
    QFile manifest_file(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
    if (!manifest_file.open(QIODevice::WriteOnly)) return fail("Primitives proof should write its manifest");
    manifest_file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    std::cout << "UI Primitives: actual shell route, 3 local densities, 6 sections, neutral pointer hover, borderless pointer selection, keyboard focus/input, actual Import selection/review, slider entry/reset, recovery, search, settings isolation, close/reopen; "
              << manifest.size() << " rendered captures\n";
    return 0;

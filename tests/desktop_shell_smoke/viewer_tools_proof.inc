    if (!show_workspace("editWorkspaceIndex", QStringLiteral("editStudioWorkspace")))
      return fail("Viewer tools proof must open the real Edit workspace");
    auto* edit = find_named_object(visual_root, QStringLiteral("editStudioWorkspace"));
    if (!edit) return fail("Viewer tools proof needs the Edit workspace");
    edit->setProperty("developerDiagnosticsEnabled", false);
    const auto click = [&](const QString& name) {
      auto* item = find_named_item(visual_root, name);
      if (!item || !item_visible(item) || item->width() <= 0 || item->height() <= 0) return false;
      const auto point = item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint();
      QTest::mouseClick(visual_window, Qt::LeftButton, Qt::NoModifier, point);
      process_layout_events(8);
      return true;
    };
    for (const auto width : {1280, 900}) {
      visual_window->resize(width, 800);
      process_layout_events(10);
      if (!click(QStringLiteral("diagnosticsOverlayButton")) ||
          !edit->property("diagnosticsPanelOpen").toBool() ||
          !click(QStringLiteral("clippingOverlayButton")) ||
          !edit->property("clippingOverlayEnabled").toBool())
        return fail("Visible viewer controls must open overlays and toggle clipping through real pointer input");
      if (!wait_for_condition([&] { return !editor_preview.clippingOverlaySource().isEmpty(); }, 10000)) {
        std::cerr << QJsonDocument(QJsonObject::fromVariantMap(
            photo_editor::desktop::EditorPreviewControllerTestAccess::inspection_state(editor_preview)))
                         .toJson(QJsonDocument::Compact).constData() << '\n';
        return fail("Clipping controls must produce an actual overlay image");
      }
      QSet<QString> overlay_icons;
      for (const auto* name : {"loupeOverlayButton", "focusOverlayButton", "peakingOverlayButton",
                               "clippingOverlayButton", "falseColorOverlayButton", "gamutOverlayButton", "zoneMapOverlayButton"}) {
        auto* action = find_named_item(visual_root, QString::fromLatin1(name));
        if (!action || !action->property("visualIconOnly").toBool() ||
            action->property("resolvedAccessibleName").toString().isEmpty() ||
            std::abs(action->width() - action->height()) > 1)
          return fail("Overlay quick actions must be square icons with full accessible names");
        overlay_icons.insert(action->property("iconName").toString());
      }
      if (overlay_icons.size() != 7)
        return fail("Each overlay must keep a distinct identifying icon");
      const auto original_overlay = editor_preview_image_provider->image(QStringLiteral("diagnostic-clipping"));
      editor_preview.setInspectionOverlaySettings({{QStringLiteral("highlightThreshold"), 0.5}});
      if (!wait_for_condition([&] {
            return editor_preview_image_provider->image(QStringLiteral("diagnostic-clipping")) != original_overlay;
          }, 10000)) return fail("Changing clipping thresholds must change the actual warning pixels");
      editor_preview.setInspectionOverlaySettings({{QStringLiteral("highlightThreshold"), 253.0/255.0}});
      if (!wait_for_condition([&] {
            return editor_preview_image_provider->image(QStringLiteral("diagnostic-clipping")) == original_overlay;
          }, 10000)) return fail("Resetting clipping thresholds must restore the warning pixels");
      if (!capture_visual_proof(visual_window, visual_root, proof_dir,
              QStringLiteral("viewer-overlays-%1").arg(width), width, 800,
              {QStringLiteral("diagnosticsOverlayButton"), QStringLiteral("clippingOverlayButton")}, &manifest))
        return fail("Overlay controls must stay inside the viewer at each size");
      if (!click(QStringLiteral("diagnosticsPanelCloseButton")) || edit->property("diagnosticsPanelOpen").toBool())
        return fail("Closing overlay settings must keep the active overlay usable");
      QMetaObject::invokeMethod(edit, "setAnalysisOverlay", Q_ARG(QVariant, QVariant(QStringLiteral("clipping"))));
      auto* view = find_named_object(visual_root, QStringLiteral("editViewMoreButton"));
      const auto view_items = view ? view->property("menuItems").toList() : QVariantList{};
      if (view_items.isEmpty() || view_items.size() > 12)
        return fail("View options must remain compact");
      for (const auto& value : view_items) {
        const auto key = value.toMap().value(QStringLiteral("key")).toString();
        if (key == QStringLiteral("diagnostics") || key == QStringLiteral("fit") ||
            key.startsWith(QStringLiteral("layout:")) || key == QStringLiteral("save-layout"))
          return fail("View options must omit duplicate toolbar and layout commands");
      }
      if (!click(QStringLiteral("editViewMoreButtonPrimaryAction")) ||
          !capture_visual_proof(visual_window, visual_root, proof_dir,
              QStringLiteral("viewer-view-menu-%1").arg(width), width, 800,
              {QStringLiteral("editViewMoreButtonPopupItem0")}, &manifest))
        return fail("Compact View options must remain pointer-accessible");
      if (!click(QStringLiteral("editViewMoreButtonPrimaryAction"))) return fail("View opener must close its menu on repeat click");
      auto* layout = find_named_object(visual_root, QStringLiteral("editWorkspaceLayoutButton"));
      const auto layout_items = layout ? layout->property("menuItems").toList() : QVariantList{};
      for (const auto& value : layout_items) {
        const auto key = value.toMap().value(QStringLiteral("key")).toString();
        if (key == QStringLiteral("layout:Default") || key == QStringLiteral("layout:Focus"))
          return fail("Default and Focus layout commands must not be listed twice");
      }
    }
    edit->setProperty("scopeDockPosition", QStringLiteral("bottom"));
    QMetaObject::invokeMethod(edit, "openScopesDock", Q_ARG(QVariant, QVariant(QStringLiteral("bottom"))));
    if (!wait_for_condition([&] { return editor_preview.histogramSampleCount() > 0 && !editor_preview.histogramUpdateActive(); }, 10000))
      return fail("Real Edit scopes must publish before rendering review");
    if (!capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("viewer-live-scopes"), 1280, 900,
            {QStringLiteral("studioNativeScopePlot"), QStringLiteral("bottomScopeFreezeButton")}, &manifest))
      return fail("The real viewer must show native scopes beside the photograph");
    auto* freeze = find_named_object(visual_root, QStringLiteral("bottomScopeFreezeButton"));
    if (!freeze || !QMetaObject::invokeMethod(freeze, "clicked") || !editor_preview.scopesFrozen())
      return fail("The scope Freeze action must reach the controller");
    QMetaObject::invokeMethod(freeze, "clicked");
    const auto previous_scale = app_shell.interfaceScale();
    app_shell.setInterfaceScale(1.5);
    process_layout_events(12);
    auto* context = find_named_item(visual_root, QStringLiteral("editContextPanel"));
    auto* viewport = find_named_item(visual_root, QStringLiteral("editImageViewport"));
    if (item_visible(context) || !viewport || viewport->width() < 359)
      return fail("Enlarged controls must release secondary pane space and retain a usable image");
    if (!capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("viewer-large-controls"), 1280, 900,
            {QStringLiteral("diagnosticsOverlayButton"), QStringLiteral("editImageViewport")}, &manifest))
      return fail("Enlarged viewer controls must remain reachable");
    if (!click(QStringLiteral("bottomScopeSettingsButton")) ||
        !capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("viewer-scope-settings"), 1280, 900,
            {QStringLiteral("bottomScopeDensityGain")}, &manifest))
      return fail("Enlarged scope settings must remain readable and scroll within the window");
    auto* quality = find_named_object(visual_root, QStringLiteral("bottomScopeSamplingControl"));
    auto* range = find_named_object(visual_root, QStringLiteral("bottomScopeRangeControl"));
    if (!quality || !range || !QMetaObject::invokeMethod(quality, "activated", Q_ARG(int, 2)) ||
        editor_preview.scopeSamplingMode() != QStringLiteral("Precise") ||
        !QMetaObject::invokeMethod(range, "activated", Q_ARG(int, 1)) ||
        editor_preview.scopeRangeMode() != QStringLiteral("Extended"))
      return fail("Compact scope selectors must update real analysis quality and range");
    app_shell.setInterfaceScale(previous_scale);
    std::cout << "Real viewer overlay pointer controls, threshold pixels, native scopes, freeze and enlarged layout passed\n";
    return EXIT_SUCCESS;

    const auto photo_path = runtime_root.filePath(QStringLiteral("timeline-review-photo.png"));
    if (!write_overlay_visual_fixture(photo_path, QStringLiteral("saturated"))) {
      return fail("Timeline review should create its media fixture");
    }
    timeline_document.createTimeline(QStringLiteral("Timeline layout review"));
    timeline_document.addClipFromGallery(
        QStringLiteral("timeline-review-video"), photo_path, QStringLiteral("Opening shot"), 8.0,
        QUrl::fromLocalFile(photo_path).toString());
    timeline_document.selectClip(0);
    if (!timeline_document.saveTimeline() || timeline_document.savedDocumentPath().isEmpty()) {
      return fail("Timeline review should save its fixture before opening the editor");
    }
    const auto document_path = timeline_document.savedDocumentPath();
    timeline_document.closeTimeline();
    if (!timeline_document.loadTimeline(document_path)) {
      return fail("Timeline review should reopen its saved fixture");
    }
    engine.load(QUrl::fromLocalFile(main_qml_path));
    process_layout_events(10);
    if (engine.rootObjects().isEmpty()) {
      return fail("Timeline review should load Main.qml");
    }
    auto* visual_root = engine.rootObjects().constFirst();
    auto* visual_window = qobject_cast<QQuickWindow*>(visual_root);
    if (visual_window == nullptr) {
      return fail("Timeline review requires a QQuickWindow");
    }
    visual_root->setProperty("currentWorkspace", visual_root->property("timelineWorkspaceIndex"));
    if (!wait_for_condition([&]() {
          auto* shell_loading = find_named_object(
              visual_root, QStringLiteral("timelineWorkspaceLoadingSurface"));
          auto* editor_loading = find_named_object(
              visual_root, QStringLiteral("timelineEditorLoadingSurface"));
          auto* editor_page = qobject_cast<QQuickItem*>(find_named_object(
              visual_root, QStringLiteral("timelineEditorPageContent")));
          return !visual_root->property("workspaceTransitionActive").toBool() &&
                 shell_loading != nullptr && !shell_loading->property("visible").toBool() &&
                 editor_loading != nullptr && !editor_loading->property("visible").toBool() &&
                 editor_page != nullptr && item_visible(editor_page) &&
                 find_named_object(visual_root, QStringLiteral("timelineProgramMonitor")) != nullptr &&
                 find_named_object(visual_root, QStringLiteral("timelineViewport")) != nullptr;
        }, 25000)) {
      auto* shell_loading = find_named_object(
          visual_root, QStringLiteral("timelineWorkspaceLoadingSurface"));
      auto* editor_loading = find_named_object(
          visual_root, QStringLiteral("timelineEditorLoadingSurface"));
      std::cerr << "Timeline review readiness: transition="
                << visual_root->property("workspaceTransitionActive").toBool()
                << " shell_loading=" << (shell_loading != nullptr && shell_loading->property("visible").toBool())
                << " editor_loading=" << (editor_loading != nullptr && editor_loading->property("visible").toBool())
                << " page=" << (find_named_object(visual_root, QStringLiteral("timelineEditorPageContent")) != nullptr)
                << '\n';
      return fail("Timeline review should load its populated editor");
    }
    if (auto* recovery_popup = find_named_object(
            visual_root, QStringLiteral("timelineRecoveryPopup"));
        recovery_popup != nullptr && recovery_popup->property("visible").toBool()) {
      if (!QMetaObject::invokeMethod(recovery_popup, "close") ||
          !wait_for_condition([&]() {
            return !recovery_popup->property("visible").toBool();
          }, 3000)) {
        return fail("Timeline review should dismiss fixture recovery before capture");
      }
    }
    process_layout_events(10);
    const auto proof_dir = qEnvironmentVariableIsSet("PHOTO_EDITOR_VISUAL_PROOF_DIR")
        ? QString::fromUtf8(qgetenv("PHOTO_EDITOR_VISUAL_PROOF_DIR"))
        : runtime_root.filePath(QStringLiteral("timeline-visual-review"));
    if (!QDir().mkpath(proof_dir)) {
      return fail("Timeline review should create its artifact directory");
    }
    QJsonArray manifest;
    const auto panels_and_transport_fit = [&]() {
      const QRectF window_rect(0, 0, visual_window->width(), visual_window->height());
      for (const auto& name : {QStringLiteral("timelineSourceRailHost"),
                               QStringLiteral("timelineProgramPanel"),
                               QStringLiteral("timelineEffectsRailHost")}) {
        auto* panel = find_named_item(visual_root, name);
        if (panel == nullptr || !item_visible(panel) || !window_rect.contains(scene_rect(panel))) {
          return false;
        }
      }
      const QStringList controls{QStringLiteral("timelinePlayPauseButton"),
                                 QStringLiteral("timelineJumpStartButton"),
                                 QStringLiteral("timelineStepBackButton"),
                                 QStringLiteral("timelineStepForwardButton"),
                                 QStringLiteral("timelineTransportSplitButton"),
                                 QStringLiteral("timelineCompactTransportEditsMenu"),
                                 QStringLiteral("timelineProgramTransportTimecode")};
      auto* transport = find_named_item(visual_root, QStringLiteral("timelineTransportBar"));
      if (transport == nullptr || !item_visible(transport)) {
        return false;
      }
      for (const auto& name : controls) {
        auto* control = find_named_item(visual_root, name);
        if (control != nullptr && item_visible(control) &&
            !scene_rect(transport).contains(scene_rect(control))) {
          return false;
        }
      }
      return visible_named_items_do_not_overlap(visual_root, controls);
    };
    const QStringList required{
        QStringLiteral("timelineWorkspace"), QStringLiteral("timelineProgramMonitor"),
        QStringLiteral("timelineViewport")};
    auto* timeline_workspace = find_named_object(
        visual_root, QStringLiteral("timelineWorkspace"));
    if (timeline_workspace == nullptr ||
        !timeline_workspace->setProperty("sourceRailTab", QStringLiteral("media")) ||
        !wait_for_condition([&]() {
          auto* media_tab = find_named_object(
              visual_root, QStringLiteral("timelineMediaTab"));
          auto* media_empty = find_named_object(
              visual_root, QStringLiteral("timelineMediaEmptyState"));
          return media_tab != nullptr && media_tab->property("active").toBool() &&
                 media_empty != nullptr && media_empty->property("visible").toBool();
        }, 5000)) {
      return fail("Timeline review should explain an empty media browser in its source rail");
    }
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_TIMELINE_KEYFRAME_PROOF_ONLY")) {
#include "timeline_keyframe_workflow_proof.inc"
    }
    if (!capture_visual_proof(visual_window, visual_root, proof_dir,
                              QStringLiteral("timeline-default"), 1280, 800,
                              required + QStringList{QStringLiteral("timelineMediaTab"),
                                                     QStringLiteral("timelineMediaEmptyState"),
                                                     QStringLiteral("timelineEmptyMediaImportButton")},
                              &manifest) ||
        !capture_visual_proof(visual_window, visual_root, proof_dir,
                              QStringLiteral("timeline-compact"), 900, 700,
                              required + QStringList{QStringLiteral("timelineMediaTab"),
                                                     QStringLiteral("timelineMediaEmptyState"),
                                                     QStringLiteral("timelineEmptyMediaImportButton")},
                              &manifest) || !panels_and_transport_fit()) {
      return fail("Timeline review should capture the media-first desktop and compact layouts");
    }
    auto* compact_edits = find_named_object(
        visual_root, QStringLiteral("timelineCompactTransportEditsMenu"));
    auto* desktop_insert = find_named_object(
        visual_root, QStringLiteral("timelineInsertSourceButton"));
    if (compact_edits == nullptr || !compact_edits->property("visible").toBool() ||
        desktop_insert == nullptr || desktop_insert->property("visible").toBool()) {
      return fail("Compact Timeline should move source edits into its transport menu");
    }
    if (!timeline_workspace->setProperty("sourceRailTab", QStringLiteral("source")) ||
        !wait_for_condition([&]() {
          auto* source_tab = find_named_object(
              visual_root, QStringLiteral("timelineSourceTab"));
          return source_tab != nullptr && source_tab->property("active").toBool();
        }, 3000) ||
        !capture_visual_proof(visual_window, visual_root, proof_dir,
                              QStringLiteral("timeline-source-compact"), 900, 700,
                              required + QStringList{QStringLiteral("timelineSourceTab")},
                              &manifest)) {
      return fail("Timeline review should keep the source monitor directly accessible");
    }
    if (!timeline_workspace->setProperty("sourceRailTab", QStringLiteral("media"))) {
      return fail("Timeline review should return to the media browser");
    }
    auto* motion_rail_button = find_named_object(
        visual_root, QStringLiteral("timelineInspectorMotionRailButton"));
    if (motion_rail_button == nullptr || !motion_rail_button->property("enabled").toBool() ||
        !QMetaObject::invokeMethod(motion_rail_button, "clicked") ||
        !wait_for_condition([&]() {
          auto* inspector = find_named_item(
              visual_root, QStringLiteral("timelineMotionInspector"));
          return !timeline_workspace->property("effectsPanelCollapsed").toBool() &&
                 timeline_workspace->property("inspectorTab").toString() == QStringLiteral("motion") &&
                 inspector != nullptr && item_visible(inspector);
        }, 15000) ||
        !capture_visual_proof(visual_window, visual_root, proof_dir,
                              QStringLiteral("timeline-motion-inspector"), 1280, 800,
                              required + QStringList{QStringLiteral("timelineMotionInspector")},
                              &manifest) ||
        !capture_visual_proof(visual_window, visual_root, proof_dir,
                              QStringLiteral("timeline-motion-inspector-compact"), 900, 700,
                              required + QStringList{QStringLiteral("timelineMotionInspector")},
                              &manifest) || !panels_and_transport_fit()) {
      return fail("The initial Inspector rail should open Motion in one click and fit desktop and compact layouts");
    }
    auto* inspector_collapse = find_named_object(
        visual_root, QStringLiteral("timelineInspectorCollapseButton"));
    if (inspector_collapse == nullptr ||
        !QMetaObject::invokeMethod(inspector_collapse, "clicked") ||
        !wait_for_condition([&]() {
          return timeline_workspace->property("effectsPanelCollapsed").toBool();
        }, 3000)) {
      return fail("The expanded Timeline Inspector should collapse directly");
    }
    auto* effects_rail_button = find_named_object(
        visual_root, QStringLiteral("timelineInspectorEffectsRailButton"));
    if (effects_rail_button == nullptr || !effects_rail_button->property("visible").toBool() ||
        !QMetaObject::invokeMethod(effects_rail_button, "clicked") ||
        !wait_for_condition([&]() {
          auto* inspector = find_named_item(
              visual_root, QStringLiteral("timelineEffectsInspector"));
          return !timeline_workspace->property("effectsPanelCollapsed").toBool() &&
                 timeline_workspace->property("inspectorTab").toString() == QStringLiteral("effects") &&
                 inspector != nullptr && item_visible(inspector);
        }, 15000) ||
        !capture_visual_proof(visual_window, visual_root, proof_dir,
                              QStringLiteral("timeline-effects-inspector-compact"), 900, 700,
                              required + QStringList{QStringLiteral("timelineEffectsInspector")},
                              &manifest) || !panels_and_transport_fit()) {
      return fail("The previously loaded Inspector rail should open Effects in one click");
    }
    auto* compact_menu = find_named_object(
        visual_root, QStringLiteral("timelineCompactTransportEditsMenu"));
    const auto compact_menu_items = compact_menu == nullptr
        ? QVariantList{} : compact_menu->property("menuItems").toList();
    bool has_jump_start = false;
    bool has_split = false;
    for (const auto& item : compact_menu_items) {
      has_jump_start |= item.toMap().value(QStringLiteral("key")).toString() == QStringLiteral("jump-start");
      has_split |= item.toMap().value(QStringLiteral("key")).toString() == QStringLiteral("split");
    }
    if (!has_jump_start || !has_split) {
      return fail("Narrow Program transport should retain jump and split actions in its menu");
    }
    timeline_playback.seekSequenceSeconds(4.0);
    if (!wait_for_condition([&]() {
          return std::abs(timeline_document.currentClipPlayheadSeconds() - 4.0) < 0.001;
        }, 3000) || !QMetaObject::invokeMethod(compact_menu, "menuItemTriggered",
                                  Q_ARG(QString, QStringLiteral("split"))) ||
        timeline_document.clipCount() != 2) {
      std::cerr << "Compact split: clips=" << timeline_document.clipCount()
                << " sequence=" << timeline_document.sequencePlayheadSeconds()
                << " source=" << timeline_document.currentClipPlayheadSeconds()
                << " qml_source=" << timeline_workspace->property("playheadSeconds").toDouble()
                << " status=" << timeline_document.status().toStdString() << '\n';
      return fail("The narrow Program transport menu should split the selected clip at the playhead");
    }
    timeline_document.undo();
    if (timeline_document.clipCount() != 1 ||
        std::abs(timeline_document.sequencePlayheadSeconds() - 4.0) > 0.001 ||
        !QMetaObject::invokeMethod(compact_menu, "menuItemTriggered",
                                  Q_ARG(QString, QStringLiteral("jump-start"))) ||
        !wait_for_condition([&]() {
          return timeline_document.sequencePlayheadSeconds() < 0.001;
        }, 3000)) {
      return fail("Narrow transport edits should remain undoable and jump to the sequence start");
    }
    auto* effects_inspector = find_named_item(
        visual_root, QStringLiteral("timelineEffectsInspector"));
    auto* effect_library = find_named_item(
        visual_root, QStringLiteral("timelineEffectLibraryList"));
    if (effects_inspector == nullptr || effect_library == nullptr ||
        !effects_inspector->setProperty("contentY", std::max(0.0,
            effects_inspector->property("contentHeight").toDouble() - effects_inspector->height()))) {
      return fail("Compact effects should let the editor scroll to the effect library");
    }
    process_layout_events(6);
    if (!contains_with_tolerance(scene_rect(effects_inspector), scene_rect(effect_library)) ||
        effect_library->height() < 96 ||
        !capture_visual_proof(visual_window, visual_root, proof_dir,
                              QStringLiteral("timeline-effects-library-compact"), 900, 700,
                              required + QStringList{QStringLiteral("timelineEffectLibraryList")},
                              &manifest)) {
      return fail("The compact effect library should be reachable inside the Inspector viewport");
    }
    auto* first_effect = effect_library->property("currentItem").value<QObject*>();
    if (first_effect == nullptr || !first_effect->property("actionEnabled").toBool() ||
        !QMetaObject::invokeMethod(first_effect, "addRequested",
                                  Q_ARG(QString, first_effect->property("effectName").toString())) ||
        timeline_document.currentClip().value(QStringLiteral("effectStack")).toList().isEmpty()) {
      return fail("The compact effect library should add a real effect to the selected clip");
    }
    timeline_document.undo();
    timeline_workspace->setProperty("effectsPanelCollapsed", true);
    auto* import_button = find_named_object(
        visual_root, QStringLiteral("timelineEmptyMediaImportButton"));
    if (import_button == nullptr || !import_button->property("visible").toBool() ||
        !QMetaObject::invokeMethod(import_button, "clicked") ||
        !wait_for_condition([&]() {
          return visual_root->property("libraryWorkspaceMode").toString() ==
                     QStringLiteral("import") &&
                 visual_root->property("currentWorkspace").toInt() ==
                     visual_root->property("libraryWorkspaceIndex").toInt();
        }, 15000)) {
      return fail("Empty Timeline media should lead directly to Library Import");
    }
    QFile manifest_file(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
    if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        manifest_file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("Timeline review should persist its manifest");
    }
    return EXIT_SUCCESS;

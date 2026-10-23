  QTemporaryDir runtime_root;
  QTemporaryDir cache_root;
  if (!runtime_root.isValid() || !cache_root.isValid()) {
    return fail("temporary runtime directories should be created");
  }
  AccessibilityCatalogCleanup accessibility_catalog_cleanup;

  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(
      QSettings::IniFormat,
      QSettings::UserScope,
      runtime_root.filePath(QStringLiteral("settings")));
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", cache_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", QByteArray("postgres://example/test"));
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_BLANK_CANVAS")) {
    const auto proof_database = qgetenv("PHOTO_EDITOR_CANVAS_PROOF_DATABASE_URL");
    if (proof_database.isEmpty()) return fail("Canvas proof requires a disposable catalog database");
    qputenv("PHOTO_EDITOR_DATABASE_URL", proof_database);
  }
  accessibility_catalog_cleanup.runtime_root = &runtime_root;
  accessibility_catalog_cleanup.enabled =
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_ACCESSIBILITY");

  photo_editor::desktop::AppShellController app_shell;
  photo_editor::desktop::CatalogAssetModel catalog_assets;
  photo_editor::desktop::LibraryWorkflowController library_workflow(
      runtime_root.filePath(QStringLiteral("library-workflow.json")), QStringLiteral("isolated-test-catalog"));
  photo_editor::desktop::CarouselDocumentController carousel_document;
  photo_editor::desktop::EditorPreviewController editor_preview;
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_TEXTURE") &&
      !photo_editor::desktop::EditorPreviewControllerTestAccess::enable_native_preview_texture(editor_preview)) {
    return fail("Native creative-tools proof requires an available Metal preview texture");
  }
  photo_editor::desktop::JobRunner job_runner;
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_BLANK_CANVAS")) job_runner.initializeFromStore();
  photo_editor::desktop::JobQueueModel job_queue;
  photo_editor::desktop::ExportJobController export_jobs;
  photo_editor::desktop::MacosUpdater macos_updater;
  photo_editor::desktop::PreviewBackendController preview_backend;
  photo_editor::desktop::TimelineDocumentController timeline_document;
  photo_editor::desktop::TimelinePlaybackController timeline_playback(&timeline_document);

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_COLOR_ROUTING") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_IMPORT_SAFETY") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_OFFLINE_RECOVERY") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_CRASH_RECOVERY_UX") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_AI_EDIT_UX") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_DELIVERY_EXPORT") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYER_WORKSPACE") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYOUT_VISUAL_PROOF") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_LIBRARY_COLLECTIONS") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_CULL_WORKFLOW") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_PROFESSIONAL_LIBRARY") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_PRESET_LIBRARY") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_COLOR_ALIGNMENT") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_UI_PRIMITIVES") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_STUDIO_POLISH") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_TIMELINE_WORKFLOW") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_TIMELINE_VISUAL_REVIEW") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_TIMELINE_UI_PROOF")) {
    app_shell.completeFirstRunExperience();
  }

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_CRASH_RECOVERY_UX")) {
    auto interrupted_export = make_persistent_job(
        QStringLiteral("Wedding delivery"),
        QStringLiteral("asset_export"),
        QStringLiteral("Running"),
        QStringLiteral("Interrupted while writing the next file."));
    interrupted_export.destination_path = runtime_root.filePath(QStringLiteral("Wedding delivery"));
    if (!photo_editor::desktop::upsertPersistedJobRecord(interrupted_export, nullptr)) {
      return fail("crash recovery visual proof should persist an interrupted delivery");
    }
    job_queue.initializeFromStore();
  }

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_ACCESSIBILITY")) {
    app_shell.restartFirstRunExperience();
    app_shell.setInterfaceScale(1.3);
    app_shell.setReducedMotion(true);
    app_shell.setHighContrast(true);
    app_shell.setColorVisionMode(QStringLiteral("deuteranopia"));
    app_shell.setInterfaceDensity(QStringLiteral("pro"));
    const auto accessible_photo_path = runtime_root.filePath(QStringLiteral("accessible-photo.png"));
    if (!write_overlay_visual_fixture(accessible_photo_path, QStringLiteral("saturated"))) {
      return fail("accessibility proof should create its photo fixture");
    }
    auto accessible_asset = make_asset(
        QStringLiteral("accessibility-photo"),
        accessible_photo_path,
        QUrl::fromLocalFile(accessible_photo_path).toString());
    catalog_assets.replaceAssetsForTesting({accessible_asset});
    catalog_assets.selectIndex(0);
  }

  preview_backend.setPreferredBackend(QStringLiteral("CPU"));
  if (preview_backend.activeBackend() != QStringLiteral("CPU")) {
    return fail("CPU preview preference should force the active preview backend to CPU");
  }
  if (!preview_backend.fallbackReason().isEmpty()) {
    return fail("CPU preview preference should not report a GPU fallback reason");
  }
  preview_backend.setPreferredBackend(QStringLiteral("GPU Preferred"));
  if (preview_backend.gpuAvailable()) {
    if (preview_backend.activeBackend() == QStringLiteral("CPU")) {
      return fail("GPU Preferred should use an available GPU preview backend");
    }
  } else {
    if (preview_backend.activeBackend() != QStringLiteral("CPU")) {
      return fail("GPU Preferred should fall back to CPU when no GPU preview backend is available");
    }
    if (preview_backend.fallbackReason().trimmed().isEmpty()) {
      return fail("GPU Preferred CPU fallback should expose an unavailable-backend reason");
    }
  }
  preview_backend.setPreferredBackend(QStringLiteral("Auto"));
  const auto mask_memory_policy = editor_preview.maskMemoryPolicySignature();
  if (!mask_memory_policy.contains(QStringLiteral("foreground-mattes:67108864")) ||
      !mask_memory_policy.contains(QStringLiteral("mask-tiles:134217728")) ||
      !mask_memory_policy.contains(QStringLiteral("mask-thumbnails:524288")) ||
      !mask_memory_policy.contains(QStringLiteral("inpaint-patch:100663296")) ||
      !mask_memory_policy.contains(QStringLiteral("inpaint-candidates:33554432:5")) ||
      !mask_memory_policy.contains(QStringLiteral("embeddings:0"))) {
    return fail("desktop shell should expose the mask memory policy for generated mattes, thumbnails, inpaint patches, candidates, and embeddings");
  }

  qmlRegisterType<photo_editor::desktop::PhotoTextDocumentStyle>("PhotoEditor", 1, 0, "PhotoTextDocumentStyle");
  qmlRegisterType<photo_editor::desktop::BrushLivePreviewItem>(
      "PhotoEditor",
      1,
      0,
      "BrushLivePreviewItem");
  qmlRegisterType<photo_editor::desktop::EditorPreviewTextureItem>(
      "PhotoEditor",
      1,
      0,
      "EditorPreviewTextureItem");
  qmlRegisterType<photo_editor::desktop::TabletInputArea>(
      "PhotoEditor",
      1,
      0,
      "TabletInputArea");
  qmlRegisterType<photo_editor::desktop::TimelineAudioOutput>(
      "PhotoEditor",
      1,
      0,
      "TimelineAudioOutput");

  QQmlApplicationEngine engine;
  auto* editor_preview_image_provider = new photo_editor::desktop::EditorPreviewImageProvider;
  editor_preview.setImageProvider(editor_preview_image_provider);
  engine.addImageProvider(QStringLiteral("editor-preview"), editor_preview_image_provider);
  engine.addImageProvider(QStringLiteral("library-original"), new photo_editor::desktop::LibraryOriginalImageProvider);
  engine.addImageProvider(
      QStringLiteral("timeline-playback"), timeline_playback.createImageProvider());
  engine.rootContext()->setContextProperty("appShell", &app_shell);
  engine.rootContext()->setContextProperty("catalogAssets", &catalog_assets);
  engine.rootContext()->setContextProperty("libraryWorkflow", &library_workflow);
  engine.rootContext()->setContextProperty("carouselDocument", &carousel_document);
  engine.rootContext()->setContextProperty("editorPreview", &editor_preview);
  engine.rootContext()->setContextProperty("exportJobs", &export_jobs);
  engine.rootContext()->setContextProperty("jobQueue", &job_queue);
  engine.rootContext()->setContextProperty("macosUpdater", &macos_updater);
  engine.rootContext()->setContextProperty("previewBackend", &preview_backend);
  engine.rootContext()->setContextProperty("timelineDocument", &timeline_document);
  engine.rootContext()->setContextProperty("timelinePlayback", &timeline_playback);

  // A captured QML tree lets visual audits compare the same native fixture
  // against a before/after snapshot without changing the working checkout.
  const auto main_qml_override = qEnvironmentVariable("PHOTO_EDITOR_SMOKE_MAIN_QML_PATH");
  const auto main_qml_path = main_qml_override.isEmpty()
      ? QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath(QStringLiteral("apps/desktop/qml/Main.qml"))
      : main_qml_override;
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_COLOR_ALIGNMENT")) {
#include "color_alignment_controls_proof.inc"
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_LIBRARY_COLLECTIONS")) {
#include "library_collection_proof.inc"
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_CULL_WORKFLOW")) {
#include "library_cull_workflow_proof.inc"
  }
#include "professional_library_workflow_proof.inc"
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_PRESET_LIBRARY")) {
#include "preset_library_proof.inc"
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYER_WORKSPACE")) {
#include "layer_workspace_interactions.inc"
  }
#include "import_preflight.inc"
#include "export_preflight.inc"
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_STUDIO_POLISH") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_TIMELINE_WORKFLOW")) {
#include "studio_polish_proof.inc"
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYOUT_VISUAL_PROOF")) {
#include "layout_visual_proof.inc"
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_UI_PRIMITIVES")) {
#include "ui_primitives_proof.inc"
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_TIMELINE_VISUAL_REVIEW")) {
#include "timeline_visual_review.inc"
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_CAROUSEL_VISUAL_REGRESSION")) {
#include "carousel_visual_regression.inc"
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_CAROUSEL_LOCALE_STRESS")) {
    QApplication::setLayoutDirection(Qt::RightToLeft);
    const QStringList locale_board_names{
        QStringLiteral("افتتاحية"),
        QStringLiteral("צילום מוצר"),
        QStringLiteral("Résumé été"),
    };
    const QStringList locale_text_layers{
        QStringLiteral("مرحبا بالعالم ٢٠٢٦"),
        QStringLiteral("סיפור חזותי 2026"),
        QStringLiteral("Crème brûlée — São Paulo"),
    };
    carousel_document.createCarouselWithSettings(
        QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("مشروع צילום — Été 2026")},
                    {QStringLiteral("documentFormat"), QStringLiteral("1x1")},
                    {QStringLiteral("slideCount"), 3}});
    for (int index = 0; index < locale_board_names.size(); ++index) {
      carousel_document.selectSlide(index);
      carousel_document.setCurrentSlideName(locale_board_names.at(index));
      carousel_document.addTextItem(locale_text_layers.at(index));
      const auto items = carousel_document.canvasItems();
      bool found_exact_text = false;
      for (const auto& value : items) {
        found_exact_text = found_exact_text ||
            value.toMap().value(QStringLiteral("text")).toString() == locale_text_layers.at(index);
      }
      if (!found_exact_text) {
        return fail("Carousel locale stress should preserve exact mixed-direction authored text layers");
      }
    }
    carousel_document.setExportFilenamePattern(QStringLiteral("{document}-{board_number}"));
    const auto locale_output_preview = carousel_document.exportOutputPreview();
    if (locale_output_preview.size() < 3) {
      return fail("Carousel locale stress should resolve every numbered board output");
    }
    for (int index = 0; index < 3; ++index) {
      const auto resolved_name = locale_output_preview.at(index).toMap()
                                     .value(QStringLiteral("resolvedName")).toString();
      if (!resolved_name.contains(QStringLiteral("مشروع צילום — Été 2026")) ||
          !resolved_name.contains(QStringLiteral("%1").arg(index + 1, 2, 10, QLatin1Char('0')))) {
        std::cerr << "localeOutputIndex=" << index
                  << " expected=" << locale_board_names.at(index).toStdString()
                  << " resolved=" << resolved_name.toStdString() << '\n';
        return fail("Carousel export naming should preserve Unicode board names and logical sequence numbers");
      }
    }
    const auto locale_export_dir = runtime_root.filePath(QStringLiteral("locale-stress-export"));
    if (!QDir().mkpath(locale_export_dir) ||
        !carousel_document.exportCarouselToDirectory(locale_export_dir)) {
      return fail("Carousel locale stress should render mixed-direction text to real output files");
    }
    const auto locale_outputs =
        QDir(locale_export_dir).entryList(QStringList{QStringLiteral("*.png")}, QDir::Files);
    if (locale_outputs.size() != 3) {
      return fail("Carousel locale stress should write all three Unicode-named board outputs");
    }
    for (const auto& output_name : locale_outputs) {
      const QImage output(QDir(locale_export_dir).filePath(output_name));
      if (output.isNull() || output.width() != 1080 || output.height() != 1080) {
        return fail("Carousel locale stress outputs should remain valid full-size square images");
      }
    }

    engine.load(QUrl::fromLocalFile(main_qml_path));
    process_layout_events(8);
    if (engine.rootObjects().isEmpty()) {
      return fail("Carousel locale stress should load Main.qml in right-to-left mode");
    }
    auto* locale_root = engine.rootObjects().constFirst();
    locale_root->setProperty("currentWorkspace", locale_root->property("carouselWorkspaceIndex"));
    process_layout_events(8);
    auto* locale_workspace = find_named_object(locale_root, QStringLiteral("carouselWorkspace"));
    QVariant locale_probe;
    if (locale_workspace == nullptr ||
        locale_workspace->property("carouselLocaleStressContract").toString() !=
            QStringLiteral("carousel-locale-stress:v1|ui-mirroring:rtl-aware|canvas-geometry:logical-unmirrored|text:bidi-auto|board-sequence:logical-number-preserved|templates:mixed-script-readable|export-filenames:unicode-preserved|no-elide:true") ||
        !locale_workspace->property("uiRightToLeft").toBool() ||
        !QMetaObject::invokeMethod(
            locale_workspace,
            "carouselLocaleStressProbe",
            Q_RETURN_ARG(QVariant, locale_probe)) ||
        locale_probe.toMap().value(QStringLiteral("layoutDirection")).toString() !=
            QStringLiteral("rtl") ||
        locale_probe.toMap().value(QStringLiteral("canvasGeometryMirrored")).toBool() ||
        !locale_probe.toMap().value(QStringLiteral("logicalBoardNumberingPreserved")).toBool()) {
      return fail("Carousel RTL UI should mirror chrome while preserving logical canvas geometry and board numbering");
    }
    auto* document_name_field =
        find_named_object(locale_root, QStringLiteral("carouselDocumentNameField"));
    if (document_name_field == nullptr ||
        document_name_field->property("text").toString() !=
            QStringLiteral("مشروع צילום — Été 2026")) {
      return fail("Carousel RTL UI should preserve mixed-script document labels without truncating their value");
    }
    for (int index = 0; index < 3; ++index) {
      auto* board_thumbnail = find_named_object(
          locale_root, QStringLiteral("carouselBoardThumbnail%1").arg(index + 1));
      auto* board_accessible = board_thumbnail != nullptr
          ? QAccessible::queryAccessibleInterface(board_thumbnail)
          : nullptr;
      if (board_accessible == nullptr ||
          !board_accessible->text(QAccessible::Name).contains(
              QStringLiteral("Board %1").arg(index + 1))) {
        return fail("RTL board thumbnails should retain logical accessible sequence numbers");
      }
    }
    locale_workspace->setProperty("inspectorPrimarySurface", QStringLiteral("designs"));
    process_layout_events(4);
    auto* template_card =
        find_named_object(locale_root, QStringLiteral("carouselTemplateLibraryCard"));
    auto* template_accessible = template_card != nullptr
        ? QAccessible::queryAccessibleInterface(template_card)
        : nullptr;
    if (template_accessible == nullptr ||
        template_accessible->text(QAccessible::Name).isEmpty()) {
      return fail("Template sample copy should remain readable and focusable in right-to-left UI layout");
    }
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_CAROUSEL_RESPONSIVE_LAYOUT")) {
    carousel_document.createCarouselWithSettings(
        QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Responsive proof")},
                    {QStringLiteral("slideCount"), 1}});
    engine.load(QUrl::fromLocalFile(main_qml_path));
    process_layout_events(8);
    if (engine.rootObjects().isEmpty()) {
      return fail("Carousel responsive layout proof should load Main.qml");
    }
    auto* responsive_root = engine.rootObjects().constFirst();
    responsive_root->setProperty(
        "currentWorkspace", responsive_root->property("carouselWorkspaceIndex"));
    process_layout_events(8);
    auto* carousel_workspace =
        responsive_root->findChild<QObject*>(QStringLiteral("carouselWorkspace"));
    if (carousel_workspace == nullptr) {
      return fail("Carousel responsive layout proof should load the real workspace");
    }
#include "carousel_dialog_loading.inc"
    if (carousel_workspace->property("carouselControlVocabularyContract").toString() !=
        QStringLiteral("carousel-control-vocabulary:v1|gradients:horizontal-vertical|distribution:horizontal-vertical|delete:board-layer|increments:board-count-export-version|add-board:explicit|icons:labeled|abbreviations:none|duplicate-plus:none|no-elide:true")) {
      return fail("Carousel should publish unambiguous control vocabulary for gradients, distribution, deletion, addition, and numeric increments");
    }
    if (carousel_workspace->property("carouselInteractionConsistencyContract").toString() !=
        QStringLiteral("carousel-interaction-consistency:v1|shared:theme-interaction-states|surfaces:header-actions-chips-template-cards-swatches-layer-rows-board-thumbnails|states:hover-pressed-focus-disabled-destructive-selected|non-color-cues:true|duration:theme-fast|no-elide:true")) {
      return fail("Carousel should publish one interaction-state contract across every control and card family");
    }
    if (carousel_workspace->property("carouselAccessibilityContract").toString() !=
        QStringLiteral("carousel-accessibility:v1|names:true|roles:true|values-in-descriptions:true|selected-state:true|state-bindings-live:true|focus-restoration:drawer-command-reading-order:true|swatches:true|thumbnails:true|slots:true|canvas-objects:true|drag-alternatives:keyboard-nudge-reorder:true|no-mouse-required:true")) {
      return fail("Carousel should publish its screen-reader, live-state, focus-restoration, and drag-alternative contract");
    }
    auto* empty_board_card =
        find_named_object(responsive_root, QStringLiteral("carouselEmptyBoardStartCard"));
    if (empty_board_card == nullptr || !empty_board_card->property("visible").toBool() ||
        empty_board_card->property("height").toDouble() > 180.0 ||
        empty_board_card->property("emptyStateContract").toString() !=
            QStringLiteral("carousel-empty-board:v3|chrome:compact-command-surface|commands:add-media-add-text-add-shape-paste-apply-template|narrow:primary-media-plus-menu|instruction-copy:none|direct-or-one-expansion:true|keyboard:true|no-elide:true|no-overlap:true")) {
      if (empty_board_card != nullptr) {
        std::cerr << "emptyCardVisible=" << empty_board_card->property("visible").toBool()
                  << " height=" << empty_board_card->property("height").toDouble()
                  << " width=" << empty_board_card->property("width").toDouble()
                  << " implicitHeight=" << empty_board_card->property("implicitHeight").toDouble()
                  << " compact=" << empty_board_card->property("responsiveCompactCard").toBool()
                  << " interfaceScale=" << app_shell.interfaceScale()
                  << " contract="
                  << empty_board_card->property("emptyStateContract").toString().toStdString()
                  << '\n';
        for (const auto& action_name : {
                 QStringLiteral("carouselEmptyBoardMediaAction"),
                 QStringLiteral("carouselEmptyBoardTextAction"),
                 QStringLiteral("carouselEmptyBoardShapeAction"),
                 QStringLiteral("carouselEmptyBoardPasteAction"),
                 QStringLiteral("carouselEmptyBoardTemplateAction")}) {
          auto* action = find_named_object(responsive_root, action_name);
          if (action != nullptr) {
            std::cerr << action_name.toStdString()
                      << " height=" << action->property("height").toDouble()
                      << " implicitHeight=" << action->property("implicitHeight").toDouble()
                      << " width=" << action->property("width").toDouble() << '\n';
          }
        }
      }
      return fail("Carousel empty boards should expose a compact direct-command surface instead of an oversized instruction card");
    }
    const QStringList empty_board_actions{
        QStringLiteral("carouselEmptyBoardMediaAction"),
        QStringLiteral("carouselEmptyBoardTextAction"),
        QStringLiteral("carouselEmptyBoardShapeAction"),
        QStringLiteral("carouselEmptyBoardPasteAction"),
        QStringLiteral("carouselEmptyBoardTemplateAction"),
    };
    for (const auto& action_name : empty_board_actions) {
      if (find_named_object(responsive_root, action_name) == nullptr) {
        return fail("Carousel empty boards should expose all five direct creation actions");
      }
    }
    auto* board_properties_surface =
        find_named_object(responsive_root, QStringLiteral("carouselBoardPropertiesSurface"));
    auto* reusable_design_surface =
        find_named_object(responsive_root, QStringLiteral("carouselReusableDesignLibrarySurface"));
    auto* reusable_templates_surface =
        find_named_object(responsive_root, QStringLiteral("carouselReusableDesignTemplatesSection"));
    if (carousel_workspace->property("carouselInspectorSplitContract").toString() !=
            QStringLiteral("carousel-inspector-split:v1|surfaces:board-properties-reusable-designs|board:authoring-selection-layers-media-layout-preflight|designs:saved-boards-components-templates-brand-kits|selection-preserved:true|board-preserved:true|single-visible-surface:true|no-elide:true") ||
        board_properties_surface == nullptr || reusable_design_surface == nullptr ||
        reusable_templates_surface == nullptr ||
        !board_properties_surface->property("surfaceActive").toBool() ||
        reusable_design_surface->property("surfaceActive").toBool()) {
      std::cerr << "inspectorSplitContract="
                << carousel_workspace->property("carouselInspectorSplitContract").toString().toStdString()
                << " boardSurface=" << (board_properties_surface != nullptr)
                << " boardVisible="
                << (board_properties_surface != nullptr
                        ? board_properties_surface->property("surfaceActive").toBool()
                        : false)
                << " designSurface=" << (reusable_design_surface != nullptr)
                << " designVisible="
                << (reusable_design_surface != nullptr
                        ? reusable_design_surface->property("surfaceActive").toBool()
                        : false)
                << " templatesSurface=" << (reusable_templates_surface != nullptr)
                << '\n';
      return fail("Carousel inspector should open on a distinct Board Properties surface");
    }
    carousel_document.addTextItem(QStringLiteral("Inspector context proof"));
    process_layout_events(4);
    const auto selected_item_before_surface_switch = carousel_document.selectedItemId();
    const auto selected_board_before_surface_switch = carousel_document.currentIndex();
    auto* accessible_canvas_object = find_named_object(
        responsive_root,
        QStringLiteral("carouselCanvasObject%1").arg(selected_item_before_surface_switch));
    auto* canvas_accessible_interface =
        accessible_canvas_object != nullptr
            ? QAccessible::queryAccessibleInterface(accessible_canvas_object)
            : nullptr;
    if (selected_item_before_surface_switch.isEmpty() || accessible_canvas_object == nullptr ||
        canvas_accessible_interface == nullptr ||
        canvas_accessible_interface->role() != QAccessible::Button ||
        canvas_accessible_interface->text(QAccessible::Name).isEmpty() ||
        !canvas_accessible_interface->state().checked) {
      return fail("Selected Carousel canvas objects should expose a live named Button role and checked state to accessibility clients");
    }
    carousel_workspace->setProperty("inspectorPrimarySurface", QStringLiteral("designs"));
    process_layout_events(4);
    if (carousel_document.selectedItemId() != selected_item_before_surface_switch ||
        carousel_document.currentIndex() != selected_board_before_surface_switch ||
        board_properties_surface->property("surfaceActive").toBool() ||
        !reusable_design_surface->property("surfaceActive").toBool() ||
        !reusable_templates_surface->property("surfaceActive").toBool()) {
      return fail("Opening Reusable Designs should preserve the selected board and layer while hiding Board Properties");
    }
    carousel_workspace->setProperty("inspectorPrimarySurface", QStringLiteral("board"));
    process_layout_events(4);
    if (carousel_document.selectedItemId() != selected_item_before_surface_switch ||
        carousel_document.currentIndex() != selected_board_before_surface_switch ||
        !board_properties_surface->property("surfaceActive").toBool() ||
        reusable_design_surface->property("surfaceActive").toBool()) {
      return fail("Returning to Board Properties should restore that inspector surface without changing editing context");
    }
    const QStringList tool_values{
        QStringLiteral("Select"),
        QStringLiteral("Hand"),
        QStringLiteral("Text"),
        QStringLiteral("Media"),
        QStringLiteral("Shape"),
        QStringLiteral("Brush"),
    };
    for (const auto& tool_value : tool_values) {
      auto* tool_button = find_named_object(
          responsive_root, QStringLiteral("carouselToolRail%1Button").arg(tool_value));
      QVariant icon_name;
      if (tool_button == nullptr ||
          tool_button->property("carouselToolRailButtonContract").toString() !=
              QStringLiteral("carousel-tool-rail-button:v2|stable-symbol:true|active-fill-marker:true|shortcut-tooltip:true|accessible-name-state:true|compact-label:true|rail-width:48|expandable-menu:true|checked-state:true|disabled-reasons:true|keyboard:true|readable-text:true|no-elide:true|geometry:true") ||
          !tool_button->property("compactLabelMode").toBool() ||
          !QMetaObject::invokeMethod(
              tool_button,
              "toolIconName",
              Q_RETURN_ARG(QVariant, icon_name),
              Q_ARG(QVariant, tool_value.toLower())) ||
          icon_name.toString().isEmpty()) {
        return fail("Every Carousel tool-rail entry should have a stable symbol, compact label, active state, shortcut tooltip, and accessible identity");
      }
    }
    const auto verify_layout = [&](const int view_width,
                                   const int view_height,
                                   const QString& expected_mode,
                                   const QString& expected_inspector,
                                   const int expected_inspector_width,
                                   const int expected_composer_width,
                                   const int expected_composer_minimum,
                                   const int expected_board_strip_height,
                                   const int expected_artboard_height) {
      QVariant result;
      if (!QMetaObject::invokeMethod(
              carousel_workspace,
              "carouselResponsiveLayoutMetrics",
              Q_RETURN_ARG(QVariant, result),
              Q_ARG(QVariant, view_width),
              Q_ARG(QVariant, view_height))) {
        return false;
      }
      const auto metrics = result.toMap();
      return metrics.value(QStringLiteral("mode")).toString() == expected_mode &&
             metrics.value(QStringLiteral("inspectorPresentation")).toString() ==
                 expected_inspector &&
             metrics.value(QStringLiteral("inspectorWidth")).toInt() ==
                 expected_inspector_width &&
             metrics.value(QStringLiteral("composerWidth")).toInt() ==
                 expected_composer_width &&
             metrics.value(QStringLiteral("composerMinimum")).toInt() ==
                 expected_composer_minimum &&
             metrics.value(QStringLiteral("boardStripHeight")).toInt() ==
                 expected_board_strip_height &&
             metrics.value(QStringLiteral("artboardHeight")).toInt() ==
                 expected_artboard_height &&
             metrics.value(QStringLiteral("horizontalNonOverlap")).toBool() &&
             metrics.value(QStringLiteral("verticalNonOverlap")).toBool() &&
             metrics.value(QStringLiteral("supported")).toBool();
    };
    if (carousel_workspace->property("carouselResponsiveLayoutContract").toString() !=
            QStringLiteral("carousel-responsive-layout:v1|modes:compact-under-1050,standard-1050-to-1719,wide-1720-plus|supported:760x600,1280x720,1920x1080|compact-inspector:right-drawer|standard-inspector:300-scaled|wide-inspector:336-scaled|composer-min:360-460-600|board-strip:112-136-152|header:condensed|empty-card:bounded|state-preserved:true|no-elide:true|no-overlap:true") ||
        !verify_layout(
            760, 600, QStringLiteral("compact"), QStringLiteral("drawer"), 0, 700, 360, 112, 424) ||
        !verify_layout(
            1280, 720, QStringLiteral("standard"), QStringLiteral("inline"), 300, 920, 460, 136, 520) ||
        !verify_layout(
            1920, 1080, QStringLiteral("wide"), QStringLiteral("inline"), 336, 1524, 600, 152, 864)) {
      return fail("Carousel responsive layout proof should preserve non-overlapping composer, inspector, artboard, and board-strip geometry at compact, standard, and wide sizes");
    }
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }
  QFile main_qml(main_qml_path);
  if (!main_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect Main.qml preset routing");
  }
  const auto main_qml_source = QString::fromUtf8(main_qml.readAll());
  if (!main_qml_source.contains(QStringLiteral("qmlPerformance.routeStarted")) ||
      !main_qml_source.contains(QStringLiteral("qmlPerformance.routeReady")) ||
      !main_qml_source.contains(QStringLiteral("qmlPerformance.frameSummary")) ||
      !main_qml_source.contains(QStringLiteral("qmlFrameSampleCount += 1")) ||
      !main_qml_source.contains(QStringLiteral("qmlFrameJankCount += 1")) ||
      !main_qml_source.contains(QStringLiteral("qmlFrameDroppedCount += 1")) ||
      !main_qml_source.contains(QStringLiteral(
          "qmlFrameMaxIntervalMs = Math.max(qmlFrameMaxIntervalMs, elapsed)")) ||
      !main_qml_source.contains(QStringLiteral("property bool qmlFrameCaptureEnabled: false")) ||
      !main_qml_source.contains(QStringLiteral("hasFrameIssue || qmlFrameCaptureEnabled")) ||
      !main_qml_source.contains(QStringLiteral("function beginQmlFrameCaptureWindow()")) ||
      !main_qml_source.contains(QStringLiteral("if (elapsed > 1000)")) ||
      !main_qml_source.contains(QStringLiteral("function workspacePerformanceSurface(")) ||
      !main_qml_source.contains(QStringLiteral("function sampleQmlFrame(")) ||
      !main_qml_source.contains(QStringLiteral("id: qmlFrameSampler"))) {
    return fail("Main QML should emit route readiness and frame/jank performance telemetry");
  }
  QFile mask_view_qml(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                           .filePath(QStringLiteral("apps/desktop/qml/workspaces/MaskView.qml")));
  if (!mask_view_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect MaskView generated-mask labels");
  }
  const auto mask_view_source = QString::fromUtf8(mask_view_qml.readAll());
  QFile studio_workspace_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/workspaces/StudioWorkspace.qml")));
  if (!studio_workspace_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect StudioWorkspace selection labels");
  }
  const auto studio_workspace_document_source = QString::fromUtf8(studio_workspace_qml.readAll());
  QFile studio_image_viewport_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral(
              "apps/desktop/qml/components/StudioImageViewport.qml")));
  if (!studio_image_viewport_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should inspect the focused Studio image viewport");
  }
  const auto studio_image_viewport_source =
      QString::fromUtf8(studio_image_viewport_qml.readAll());
  const auto studio_workspace_source =
      studio_workspace_document_source + QLatin1Char('\n') + studio_image_viewport_source;
  if (!studio_workspace_document_source.contains(
          QStringLiteral("component ImageViewport: StudioImageViewport")) ||
      studio_workspace_document_source.contains(
          QStringLiteral("component ImageViewport: Rectangle")) ||
      !studio_image_viewport_source.contains(
          QStringLiteral("required property var workspaceContext")) ||
      !studio_image_viewport_source.contains(
          QStringLiteral("required property var previewContext")) ||
      !studio_image_viewport_source.contains(
          QStringLiteral("required property var themeContext")) ||
      !studio_image_viewport_source.contains(
          QStringLiteral("StudioViewportInputCommandSurface {")) ||
      !studio_image_viewport_source.contains(
          QStringLiteral("StudioRetouchOverlayCommandSurface {"))) {
    return fail("StudioWorkspace should delegate its image viewport to the focused component with explicit dependencies");
  }
  QFile studio_offline_recovery_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral(
              "apps/desktop/qml/components/StudioOfflineRecoveryBanner.qml")));
  if (!studio_offline_recovery_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should inspect the focused offline recovery banner");
  }
  const auto studio_offline_recovery_source =
      QString::fromUtf8(studio_offline_recovery_qml.readAll());
  QFile studio_tool_transition_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral(
              "apps/desktop/qml/components/StudioToolTransitionOverlay.qml")));
  if (!studio_tool_transition_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should inspect the focused tool transition overlay");
  }
  const auto studio_tool_transition_source =
      QString::fromUtf8(studio_tool_transition_qml.readAll());
  QFile agent_walkthrough_dialog_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral(
              "apps/desktop/qml/components/AgentWalkthroughDialog.qml")));
  if (!agent_walkthrough_dialog_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should inspect the live AI Edit dialog contract");
  }
  const auto agent_walkthrough_dialog_source =
      QString::fromUtf8(agent_walkthrough_dialog_qml.readAll());
  QFile agent_conversation_view_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral(
              "apps/desktop/qml/components/AgentConversationView.qml")));
  if (!agent_conversation_view_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should inspect the live AI Edit conversation surface");
  }
  const auto agent_conversation_view_source =
      QString::fromUtf8(agent_conversation_view_qml.readAll());
  QFile studio_adjustment_slider_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral(
              "apps/desktop/qml/components/StudioAdjustmentSlider.qml")));
  if (!studio_adjustment_slider_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect the Studio adjustment slider owner");
  }
  const auto studio_adjustment_slider_source =
      QString::fromUtf8(studio_adjustment_slider_qml.readAll());
  QFile studio_color_management_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral(
              "apps/desktop/qml/components/StudioColorManagementPanel.qml")));
  if (!studio_color_management_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect the active Studio color-management panel");
  }
  const auto studio_color_management_source =
      QString::fromUtf8(studio_color_management_qml.readAll());
  QFile studio_diagnostic_overlay_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral(
              "apps/desktop/qml/components/StudioDiagnosticOverlayStack.qml")));
  if (!studio_diagnostic_overlay_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect the Studio diagnostic overlay stack");
  }
  const auto studio_diagnostic_overlay_source =
      QString::fromUtf8(studio_diagnostic_overlay_qml.readAll());
  QFile studio_viewport_status_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral(
              "apps/desktop/qml/components/StudioViewportStatusStack.qml")));
  if (!studio_viewport_status_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect the Studio viewport status stack");
  }
  const auto studio_viewport_status_source =
      QString::fromUtf8(studio_viewport_status_qml.readAll());
  QFile studio_brush_cursor_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral(
              "apps/desktop/qml/components/StudioViewportBrushCursors.qml")));
  if (!studio_brush_cursor_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect the Studio brush cursor stack");
  }
  const auto studio_brush_cursor_source =
      QString::fromUtf8(studio_brush_cursor_qml.readAll());
  QFile studio_viewport_input_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral(
              "apps/desktop/qml/components/StudioViewportInputCommandSurface.qml")));
  if (!studio_viewport_input_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect the Studio viewport input command surface");
  }
  const auto studio_viewport_input_source =
      QString::fromUtf8(studio_viewport_input_qml.readAll());
  QFile studio_tiny_input_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/StudioTinyInput.qml")));
  if (!studio_tiny_input_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect the Studio input contract");
  }
  const auto studio_tiny_input_source = QString::fromUtf8(studio_tiny_input_qml.readAll());
  QFile inspector_section_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/InspectorSection.qml")));
  if (!inspector_section_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared InspectorSection contracts");
  }
  const auto inspector_section_source =
      QString::fromUtf8(inspector_section_qml.readAll());
  if (!studio_workspace_source.contains(
          QStringLiteral("color-control-domain-disclosure:v2|light:scene-referred-before-output|levels:scene-luminance-input|tone-curve:display-referred|color:look-space-after-curves|grading:look-space-after-selective-color|header-tooltip:accessible|renderer:recipe-model-and-upgrade-availability")) ||
      !studio_workspace_source.contains(
          QStringLiteral("objectName: \"lightInspectorGroup\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function colorControlDomainHelpText(groupKey)")) ||
      !studio_workspace_source.contains(QStringLiteral("helpText: root.colorControlDomainHelpText(\"light\")")) ||
      !studio_workspace_source.contains(QStringLiteral("helpText: root.colorControlDomainHelpText(\"levels\")")) ||
      !studio_workspace_source.contains(QStringLiteral("helpText: root.colorControlDomainHelpText(\"tone-curve\")")) ||
      !studio_workspace_source.contains(QStringLiteral("helpText: root.colorControlDomainHelpText(\"color\")")) ||
      !studio_workspace_source.contains(QStringLiteral("helpText: root.colorControlDomainHelpText(\"color-grading\")")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.currentPhotographicRenderingModel")) ||
      !studio_workspace_source.contains(QStringLiteral("Open Info > Rendering for the recipe model and upgrade availability.")) ||
      !inspector_section_source.contains(QStringLiteral("property string helpText: \"\"")) ||
      !inspector_section_source.contains(QStringLiteral("text: root.resolvedTooltipText")) ||
      !inspector_section_source.contains(QStringLiteral("root.helpText.length > 0)")) ||
      !inspector_section_source.contains(QStringLiteral("? root.helpText : root.subtitle")) ||
      !studio_workspace_source.contains(
          QStringLiteral("rendering-upgrade:v2|explicit:true|preview:side-by-side|recipe-mutation-before-apply:false|apply:undoable|cancel:restores-current|legacy:supported|target:aces2-ocio-v1|transition:preserve-legacy-direct-luminance-v1")) ||
      !studio_workspace_source.contains(
          QStringLiteral("objectName: \"previewRenderingUpgradeButton\"")) ||
      !studio_workspace_source.contains(
          QStringLiteral("objectName: \"cancelRenderingUpgradeButton\"")) ||
      !studio_workspace_source.contains(
          QStringLiteral("objectName: \"applyRenderingUpgradeButton\"")) ||
      !studio_workspace_source.contains(
          QStringLiteral("editorPreview.startRenderingUpgradePreview()")) ||
      !studio_workspace_source.contains(
          QStringLiteral("editorPreview.cancelRenderingUpgradePreview()")) ||
      !studio_workspace_source.contains(
          QStringLiteral("editorPreview.applyRenderingUpgrade()"))) {
    return fail("Studio should disclose color domains and provide an explicit reversible rendering-upgrade comparison");
  }
  trace_smoke_progress("color control domain disclosure source checked");
  if (qEnvironmentVariableIsSet(
          "PHOTO_EDITOR_SMOKE_STOP_AFTER_COLOR_DOMAIN_SOURCE")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }
  QFile agent_edit_controller_cpp(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/src/agent_edit_controller.cpp")));
  if (!agent_edit_controller_cpp.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect agent edit status labels");
  }
  const auto agent_edit_controller_source = QString::fromUtf8(agent_edit_controller_cpp.readAll());
  QFile editor_preview_controller_cpp(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/src/editor_preview_controller.cpp")));
  if (!editor_preview_controller_cpp.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect editor preview status labels");
  }
  const auto editor_preview_controller_source =
      QString::fromUtf8(editor_preview_controller_cpp.readAll());
  QFile thumbnail_worker_renderer_factory_cpp(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/src/thumbnail_worker_renderer_factory.cpp")));
  if (!thumbnail_worker_renderer_factory_cpp.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should inspect thumbnail renderer ownership");
  }
  const auto thumbnail_worker_renderer_factory_source =
      QString::fromUtf8(thumbnail_worker_renderer_factory_cpp.readAll());
  QFile mask_outline_tracing_cpp(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/src/mask_outline_tracing.cpp")));
  if (!mask_outline_tracing_cpp.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should inspect the focused mask outline tracer");
  }
  const auto mask_outline_tracing_source =
      QString::fromUtf8(mask_outline_tracing_cpp.readAll());
  QFile mask_outline_presentation_cpp(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/src/mask_outline_presentation.cpp")));
  if (!mask_outline_presentation_cpp.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should inspect current mask outline presentation cleanup");
  }
  const auto mask_outline_presentation_source =
      QString::fromUtf8(mask_outline_presentation_cpp.readAll());
  QFile mask_outline_simplification_cpp(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/src/mask_outline_simplification.cpp")));
  if (!mask_outline_simplification_cpp.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should inspect mask outline simplification");
  }
  const auto mask_outline_simplification_source =
      QString::fromUtf8(mask_outline_simplification_cpp.readAll());
  QFile catalog_asset_model_cpp(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/src/catalog_asset_model.cpp")));
  if (!catalog_asset_model_cpp.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect catalog status labels");
  }
  const auto catalog_asset_model_source = QString::fromUtf8(catalog_asset_model_cpp.readAll());
  QFile layer_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/LayerRow.qml")));
  if (!layer_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared LayerRow contracts");
  }
  const auto layer_row_source = QString::fromUtf8(layer_row_qml.readAll());
  QFile layer_row_action_zone_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/LayerRowActionZone.qml")));
  if (!layer_row_action_zone_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared LayerRowActionZone contracts");
  }
  const auto layer_row_action_zone_source =
      QString::fromUtf8(layer_row_action_zone_qml.readAll());
  QFile layer_row_chrome_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/LayerRowChrome.qml")));
  if (!layer_row_chrome_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared LayerRowChrome contracts");
  }
  const auto layer_row_chrome_source = QString::fromUtf8(layer_row_chrome_qml.readAll());
  QFile review_preview_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ReviewPreview.qml")));
  if (!review_preview_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ReviewPreview card contracts");
  }
  const auto review_preview_source = QString::fromUtf8(review_preview_qml.readAll());
  QFile action_button_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ActionButton.qml")));
  if (!action_button_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ActionButton contracts");
  }
  const auto action_button_source = QString::fromUtf8(action_button_qml.readAll());
  QFile confirm_action_button_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ConfirmActionButton.qml")));
  if (!confirm_action_button_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ConfirmActionButton contracts");
  }
  const auto confirm_action_button_source = QString::fromUtf8(confirm_action_button_qml.readAll());
  QFile status_chip_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/StatusChip.qml")));
  if (!status_chip_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared StatusChip contracts");
  }
  const auto status_chip_source = QString::fromUtf8(status_chip_qml.readAll());
  QFile app_icon_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/AppIcon.qml")));
  if (!app_icon_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared AppIcon symbols");
  }
  const auto app_icon_source = QString::fromUtf8(app_icon_qml.readAll());
  QFile activity_banner_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ActivityBanner.qml")));
  if (!activity_banner_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ActivityBanner contracts");
  }
  const auto activity_banner_source =
      QString::fromUtf8(activity_banner_qml.readAll());
  QFile inline_activity_indicator_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/InlineActivityIndicator.qml")));
  if (!inline_activity_indicator_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect InlineActivityIndicator action usage");
  }
  const auto inline_activity_indicator_source =
      QString::fromUtf8(inline_activity_indicator_qml.readAll());
  QFile agent_model_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/AgentModelRow.qml")));
  if (!agent_model_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared AgentModelRow contracts");
  }
  const auto agent_model_row_source = QString::fromUtf8(agent_model_row_qml.readAll());
  QFile agent_batch_status_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/AgentBatchStatusRow.qml")));
  if (!agent_batch_status_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared AgentBatchStatusRow contracts");
  }
  const auto agent_batch_status_row_source =
      QString::fromUtf8(agent_batch_status_row_qml.readAll());
  QFile agent_permission_mode_button_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/AgentPermissionModeButton.qml")));
  if (!agent_permission_mode_button_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared AgentPermissionModeButton contracts");
  }
  const auto agent_permission_mode_button_source =
      QString::fromUtf8(agent_permission_mode_button_qml.readAll());
  QFile agent_tool_permission_toggle_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/AgentToolPermissionToggle.qml")));
  if (!agent_tool_permission_toggle_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared AgentToolPermissionToggle contracts");
  }
  const auto agent_tool_permission_toggle_source =
      QString::fromUtf8(agent_tool_permission_toggle_qml.readAll());
  QFile agent_proof_card_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/AgentProofCard.qml")));
  if (!agent_proof_card_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared AgentProofCard contracts");
  }
  const auto agent_proof_card_source =
      QString::fromUtf8(agent_proof_card_qml.readAll());
  QFile agent_recipe_diff_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/AgentRecipeDiffRow.qml")));
  if (!agent_recipe_diff_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared AgentRecipeDiffRow contracts");
  }
  const auto agent_recipe_diff_row_source =
      QString::fromUtf8(agent_recipe_diff_row_qml.readAll());
  QFile agent_run_history_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/AgentRunHistoryRow.qml")));
  if (!agent_run_history_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared AgentRunHistoryRow contracts");
  }
  const auto agent_run_history_row_source =
      QString::fromUtf8(agent_run_history_row_qml.readAll());
  QFile agent_style_tile_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/AgentStyleTile.qml")));
  if (!agent_style_tile_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared AgentStyleTile contracts");
  }
  const auto agent_style_tile_source = QString::fromUtf8(agent_style_tile_qml.readAll());
  QFile agent_run_stage_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/AgentRunStageRow.qml")));
  if (!agent_run_stage_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared AgentRunStageRow contracts");
  }
  const auto agent_run_stage_row_source =
      QString::fromUtf8(agent_run_stage_row_qml.readAll());
  QFile agent_tool_call_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/AgentToolCallRow.qml")));
  if (!agent_tool_call_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared AgentToolCallRow contracts");
  }
  const auto agent_tool_call_row_source =
      QString::fromUtf8(agent_tool_call_row_qml.readAll());
  QFile edit_rail_button_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/EditRailButton.qml")));
  if (!edit_rail_button_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared EditRailButton contracts");
  }
  const auto edit_rail_button_source =
      QString::fromUtf8(edit_rail_button_qml.readAll());
  QFile edit_layout_toggle_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/EditLayoutToggleRow.qml")));
  if (!edit_layout_toggle_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared EditLayoutToggleRow contracts");
  }
  const auto edit_layout_toggle_row_source =
      QString::fromUtf8(edit_layout_toggle_row_qml.readAll());
  QFile edit_inspector_tab_button_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/EditInspectorTabButton.qml")));
  if (!edit_inspector_tab_button_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared EditInspectorTabButton contracts");
  }
  const auto edit_inspector_tab_button_source =
      QString::fromUtf8(edit_inspector_tab_button_qml.readAll());
  QFile edit_filmstrip_thumb_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/EditFilmstripThumb.qml")));
  if (!edit_filmstrip_thumb_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared EditFilmstripThumb contracts");
  }
  const auto edit_filmstrip_thumb_source =
      QString::fromUtf8(edit_filmstrip_thumb_qml.readAll());
  QFile gallery_asset_card_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/GalleryAssetCard.qml")));
  if (!gallery_asset_card_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared GalleryAssetCard contracts");
  }
  const auto gallery_asset_card_source =
      QString::fromUtf8(gallery_asset_card_qml.readAll());
  QFile gallery_picker_dialog_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/GalleryPickerDialog.qml")));
  if (!gallery_picker_dialog_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect GalleryPickerDialog card usage");
  }
  const auto gallery_picker_dialog_source =
      QString::fromUtf8(gallery_picker_dialog_qml.readAll());
  QFile command_palette_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/CommandPaletteRow.qml")));
  if (!command_palette_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared CommandPaletteRow contracts");
  }
  const auto command_palette_row_source =
      QString::fromUtf8(command_palette_row_qml.readAll());
  QFile command_palette_dialog_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/CommandPaletteDialog.qml")));
  if (!command_palette_dialog_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect CommandPaletteDialog row usage");
  }
  const auto command_palette_dialog_source =
      QString::fromUtf8(command_palette_dialog_qml.readAll());
  QFile keyboard_shortcuts_dialog_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/KeyboardShortcutsDialog.qml")));
  if (!keyboard_shortcuts_dialog_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect KeyboardShortcutsDialog visibility contracts");
  }
  const auto keyboard_shortcuts_dialog_source =
      QString::fromUtf8(keyboard_shortcuts_dialog_qml.readAll());
  QFile preference_nav_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/PreferenceNavRow.qml")));
  if (!preference_nav_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared PreferenceNavRow contracts");
  }
  const auto preference_nav_row_source =
      QString::fromUtf8(preference_nav_row_qml.readAll());
  QFile app_preferences_dialog_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/AppPreferencesDialog.qml")));
  if (!app_preferences_dialog_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect AppPreferencesDialog navigation usage");
  }
  const auto app_preferences_dialog_source =
      QString::fromUtf8(app_preferences_dialog_qml.readAll());
  QFile activity_status_bar_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ActivityStatusBar.qml")));
  if (!activity_status_bar_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect the shared activity status bar");
  }
  const auto activity_status_bar_source =
      QString::fromUtf8(activity_status_bar_qml.readAll());
  QFile startup_loading_popup_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/StartupLoadingPopup.qml")));
  if (!startup_loading_popup_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should inspect the startup loading surface");
  }
  const auto startup_loading_popup_source =
      QString::fromUtf8(startup_loading_popup_qml.readAll());
  QFile popup_surface_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/PopupSurface.qml")));
  if (!popup_surface_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared PopupSurface hierarchy contracts");
  }
  const auto popup_surface_source = QString::fromUtf8(popup_surface_qml.readAll());
  QFile menu_button_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/MenuButton.qml")));
  if (!menu_button_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared MenuButton contracts");
  }
  const auto menu_button_source = QString::fromUtf8(menu_button_qml.readAll());
  QFile menu_item_metrics_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/MenuItemMetrics.qml")));
  if (!menu_item_metrics_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared menu metrics contracts");
  }
  const auto menu_item_metrics_source =
      QString::fromUtf8(menu_item_metrics_qml.readAll());
  QFile menu_popover_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/MenuPopover.qml")));
  if (!menu_popover_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared menu popover contracts");
  }
  const auto menu_popover_source = QString::fromUtf8(menu_popover_qml.readAll());
  QFile app_menu_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/AppMenu.qml")));
  if (!app_menu_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared AppMenu contracts");
  }
  const auto app_menu_source = QString::fromUtf8(app_menu_qml.readAll());
  QFile app_menu_item_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/AppMenuItem.qml")));
  if (!app_menu_item_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared AppMenuItem contracts");
  }
  const auto app_menu_item_source = QString::fromUtf8(app_menu_item_qml.readAll());
  QFile popup_menu_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/PopupMenuRow.qml")));
  if (!popup_menu_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared PopupMenuRow contracts");
  }
  const auto popup_menu_row_source = QString::fromUtf8(popup_menu_row_qml.readAll());
  QFile mini_button_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/MiniButton.qml")));
  if (!mini_button_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared MiniButton contracts");
  }
  const auto mini_button_source = QString::fromUtf8(mini_button_qml.readAll());
  QFile tool_button_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ToolButton.qml")));
  if (!tool_button_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ToolButton contracts");
  }
  const auto tool_button_source = QString::fromUtf8(tool_button_qml.readAll());
  QFile tool_hud_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ToolHud.qml")));
  if (!tool_hud_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ToolHud contracts");
  }
  const auto tool_hud_source = QString::fromUtf8(tool_hud_qml.readAll());
  QFile tool_command_bar_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ToolCommandBar.qml")));
  if (!tool_command_bar_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ToolCommandBar contracts");
  }
  const auto tool_command_bar_source = QString::fromUtf8(tool_command_bar_qml.readAll());
  QFile brush_tool_hud_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/BrushToolHud.qml")));
  if (!brush_tool_hud_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect BrushToolHud contracts");
  }
  const auto brush_tool_hud_source = QString::fromUtf8(brush_tool_hud_qml.readAll());
  QFile brush_context_panel_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/BrushContextPanel.qml")));
  if (!brush_context_panel_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect BrushContextPanel contracts");
  }
  const auto brush_context_panel_source = QString::fromUtf8(brush_context_panel_qml.readAll());
  QFile brush_layer_inspector_header_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/BrushLayerInspectorHeader.qml")));
  if (!brush_layer_inspector_header_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect BrushLayerInspectorHeader contracts");
  }
  const auto brush_layer_inspector_header_source =
      QString::fromUtf8(brush_layer_inspector_header_qml.readAll());
  QFile split_button_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/SplitButton.qml")));
  if (!split_button_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared SplitButton contracts");
  }
  const auto split_button_source = QString::fromUtf8(split_button_qml.readAll());
  QFile tool_rail_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ToolRail.qml")));
  if (!tool_rail_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ToolRail popup row usage");
  }
  const auto tool_rail_source = QString::fromUtf8(tool_rail_qml.readAll());
  QFile export_queue_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ExportQueueRow.qml")));
  if (!export_queue_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ExportQueueRow contracts");
  }
  const auto export_queue_row_source = QString::fromUtf8(export_queue_row_qml.readAll());
  QFile export_preflight_issue_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ExportPreflightIssueRow.qml")));
  if (!export_preflight_issue_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ExportPreflightIssueRow contracts");
  }
  const auto export_preflight_issue_row_source =
      QString::fromUtf8(export_preflight_issue_row_qml.readAll());
  QFile export_preset_summary_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ExportPresetSummary.qml")));
  if (!export_preset_summary_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ExportPresetSummary contracts");
  }
  const auto export_preset_summary_source =
      QString::fromUtf8(export_preset_summary_qml.readAll());
  QFile export_view_qml(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                            .filePath(QStringLiteral("apps/desktop/qml/workspaces/ExportView.qml")));
  if (!export_view_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect ExportView queue row usage");
  }
  const auto export_view_source = QString::fromUtf8(export_view_qml.readAll());
  QFile export_workspace_host_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/workspaces/ExportWorkspaceHost.qml")));
  if (!export_workspace_host_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect deferred Export workspace host");
  }
  const auto export_workspace_host_source =
      QString::fromUtf8(export_workspace_host_qml.readAll());
  QFile recent_document_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/RecentDocumentRow.qml")));
  if (!recent_document_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared RecentDocumentRow contracts");
  }
  const auto recent_document_row_source =
      QString::fromUtf8(recent_document_row_qml.readAll());
  QFile carousel_board_thumb_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/CarouselBoardThumb.qml")));
  if (!carousel_board_thumb_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared CarouselBoardThumb contracts");
  }
  const auto carousel_board_thumb_source =
      QString::fromUtf8(carousel_board_thumb_qml.readAll());
  QFile carousel_media_thumb_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/CarouselMediaThumb.qml")));
  if (!carousel_media_thumb_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared CarouselMediaThumb contracts");
  }
  const auto carousel_media_thumb_source =
      QString::fromUtf8(carousel_media_thumb_qml.readAll());
  QFile carousel_tool_rail_button_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/CarouselToolRailButton.qml")));
  if (!carousel_tool_rail_button_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared CarouselToolRailButton contracts");
  }
  const auto carousel_tool_rail_button_source =
      QString::fromUtf8(carousel_tool_rail_button_qml.readAll());
  QFile color_swatch_button_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ColorSwatchButton.qml")));
  if (!color_swatch_button_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ColorSwatchButton contracts");
  }
  const auto color_swatch_button_source =
      QString::fromUtf8(color_swatch_button_qml.readAll());
  QFile tool_option_toggle_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ToolOptionToggle.qml")));
  if (!tool_option_toggle_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ToolOptionToggle contracts");
  }
  const auto tool_option_toggle_source =
      QString::fromUtf8(tool_option_toggle_qml.readAll());
  QFile tool_value_slider_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ToolValueSlider.qml")));
  if (!tool_value_slider_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ToolValueSlider contracts");
  }
  const auto tool_value_slider_source =
      QString::fromUtf8(tool_value_slider_qml.readAll());
  QFile inspector_slider_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/InspectorSlider.qml")));
  if (!inspector_slider_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared InspectorSlider contracts");
  }
  const auto inspector_slider_source =
      QString::fromUtf8(inspector_slider_qml.readAll());
  QFile tone_text_field_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ToneTextField.qml")));
  if (!tone_text_field_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ToneTextField accessibility contracts");
  }
  const auto tone_text_field_source =
      QString::fromUtf8(tone_text_field_qml.readAll());
  QFile channel_mixer_button_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ChannelMixerButton.qml")));
  if (!channel_mixer_button_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ChannelMixerButton contracts");
  }
  const auto channel_mixer_button_source =
      QString::fromUtf8(channel_mixer_button_qml.readAll());
  QFile carousel_layer_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/CarouselLayerRow.qml")));
  if (!carousel_layer_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared CarouselLayerRow contracts");
  }
  const auto carousel_layer_row_source =
      QString::fromUtf8(carousel_layer_row_qml.readAll());
  QFile timeline_marker_card_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/TimelineMarkerCard.qml")));
  if (!timeline_marker_card_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared TimelineMarkerCard contracts");
  }
  const auto timeline_marker_card_source =
      QString::fromUtf8(timeline_marker_card_qml.readAll());
  QFile timeline_marker_pin_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/TimelineMarkerPin.qml")));
  if (!timeline_marker_pin_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared TimelineMarkerPin contracts");
  }
  const auto timeline_marker_pin_source =
      QString::fromUtf8(timeline_marker_pin_qml.readAll());
  QFile timeline_waveform_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/TimelineWaveform.qml")));
  if (!timeline_waveform_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect adaptive TimelineWaveform contracts");
  }
  const auto timeline_waveform_source =
      QString::fromUtf8(timeline_waveform_qml.readAll());
  QFile timeline_clip_content_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/TimelineClipContent.qml")));
  if (!timeline_clip_content_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared TimelineClipContent contracts");
  }
  const auto timeline_clip_content_source =
      QString::fromUtf8(timeline_clip_content_qml.readAll());
  QFile timeline_effect_library_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/TimelineEffectLibraryRow.qml")));
  if (!timeline_effect_library_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared TimelineEffectLibraryRow contracts");
  }
  const auto timeline_effect_library_row_source =
      QString::fromUtf8(timeline_effect_library_row_qml.readAll());
  QFile timeline_transition_library_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/TimelineTransitionLibraryRow.qml")));
  if (!timeline_transition_library_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared TimelineTransitionLibraryRow contracts");
  }
  const auto timeline_transition_library_row_source =
      QString::fromUtf8(timeline_transition_library_row_qml.readAll());
  QFile timeline_track_toggle_button_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/TimelineTrackToggleButton.qml")));
  if (!timeline_track_toggle_button_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared TimelineTrackToggleButton contracts");
  }
  const auto timeline_track_toggle_button_source =
      QString::fromUtf8(timeline_track_toggle_button_qml.readAll());
  QFile timeline_track_header_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/TimelineTrackHeader.qml")));
  if (!timeline_track_header_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect TimelineTrackHeader toggle usage");
  }
  const auto timeline_track_header_source =
      QString::fromUtf8(timeline_track_header_qml.readAll());
  QFile tone_curve_channel_button_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ToneCurveChannelButton.qml")));
  if (!tone_curve_channel_button_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ToneCurveChannelButton contracts");
  }
  const auto tone_curve_channel_button_source =
      QString::fromUtf8(tone_curve_channel_button_qml.readAll());
  QFile tone_curve_preset_button_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ToneCurvePresetButton.qml")));
  if (!tone_curve_preset_button_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ToneCurvePresetButton contracts");
  }
  const auto tone_curve_preset_button_source =
      QString::fromUtf8(tone_curve_preset_button_qml.readAll());
  QFile tone_curve_point_handle_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ToneCurvePointHandle.qml")));
  if (!tone_curve_point_handle_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ToneCurvePointHandle contracts");
  }
  const auto tone_curve_point_handle_source =
      QString::fromUtf8(tone_curve_point_handle_qml.readAll());
  QFile tone_curve_editor_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ToneCurveEditor.qml")));
  if (!tone_curve_editor_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect ToneCurveEditor control usage");
  }
  const auto tone_curve_editor_source =
      QString::fromUtf8(tone_curve_editor_qml.readAll());
  QFile timeline_effects_panel_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/TimelineEffectsPanel.qml")));
  if (!timeline_effects_panel_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect TimelineEffectsPanel library row usage");
  }
  const auto timeline_effects_panel_source =
      QString::fromUtf8(timeline_effects_panel_qml.readAll());
  QFile timeline_media_asset_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/TimelineMediaAssetRow.qml")));
  if (!timeline_media_asset_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared TimelineMediaAssetRow contracts");
  }
  const auto timeline_media_asset_row_source =
      QString::fromUtf8(timeline_media_asset_row_qml.readAll());
  QFile timeline_source_panel_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/TimelineSourcePanel.qml")));
  if (!timeline_source_panel_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect TimelineSourcePanel media row usage");
  }
  const auto timeline_source_panel_source =
      QString::fromUtf8(timeline_source_panel_qml.readAll());
  QFile timeline_view_qml(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                              .filePath(QStringLiteral("apps/desktop/qml/workspaces/TimelineView.qml")));
  if (!timeline_view_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect TimelineView recent row usage");
  }
  const auto timeline_view_source = QString::fromUtf8(timeline_view_qml.readAll());
  QFile timeline_setup_page_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/workspaces/TimelineSetupPage.qml")));
  if (!timeline_setup_page_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect Timeline setup recent rows");
  }
  const auto timeline_setup_page_source =
      QString::fromUtf8(timeline_setup_page_qml.readAll());
  QFile carousel_view_qml(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                              .filePath(QStringLiteral("apps/desktop/qml/workspaces/CarouselView.qml")));
  if (!carousel_view_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect CarouselView recent row usage");
  }
  auto carousel_view_source = QString::fromUtf8(carousel_view_qml.readAll());
  const QDir carousel_dialog_dir(QFileInfo(carousel_view_qml.fileName()).dir().filePath("carousel"));
  for (const auto& name : carousel_dialog_dir.entryList({"*Content.qml"}, QDir::Files)) {
    QFile content(carousel_dialog_dir.filePath(name));
    if (!content.open(QIODevice::ReadOnly | QIODevice::Text))
      return fail("Carousel source contracts should include deferred dialog contents");
    carousel_view_source += '\n' + QString::fromUtf8(content.readAll());
  }
  QFile carousel_color_vision_effect_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/CarouselColorVisionEffect.qml")));
  if (!carousel_color_vision_effect_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect the Carousel color-vision effect contract");
  }
  const auto carousel_color_vision_effect_source =
      QString::fromUtf8(carousel_color_vision_effect_qml.readAll());
  QFile carousel_color_vision_fragment_shader(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/shaders/carousel_color_vision.frag")));
  if (!carousel_color_vision_fragment_shader.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect the Carousel color-vision shader");
  }
  const auto carousel_color_vision_fragment_source =
      QString::fromUtf8(carousel_color_vision_fragment_shader.readAll());
  QFile thumbnail_state_utils_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ThumbnailStateUtils.qml")));
  if (!thumbnail_state_utils_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect thumbnail state utility contracts");
  }
  const auto thumbnail_state_utils_source =
      QString::fromUtf8(thumbnail_state_utils_qml.readAll());
  QFile library_view_qml(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                             .filePath(QStringLiteral("apps/desktop/qml/workspaces/LibraryView.qml")));
  if (!library_view_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect LibraryView review card usage");
  }
  const auto library_view_source = QString::fromUtf8(library_view_qml.readAll());
  QFile import_view_qml(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                            .filePath(QStringLiteral("apps/desktop/qml/workspaces/ImportView.qml")));
  if (!import_view_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect ImportView source and folder row usage");
  }
  const auto import_view_source = QString::fromUtf8(import_view_qml.readAll());
  QFile import_folder_chip_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ImportFolderChip.qml")));
  if (!import_folder_chip_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ImportFolderChip contracts");
  }
  const auto import_folder_chip_source =
      QString::fromUtf8(import_folder_chip_qml.readAll());
  QFile import_folder_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ImportFolderRow.qml")));
  if (!import_folder_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ImportFolderRow contracts");
  }
  const auto import_folder_row_source =
      QString::fromUtf8(import_folder_row_qml.readAll());
  QFile import_image_tile_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ImportImageTile.qml")));
  if (!import_image_tile_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ImportImageTile contracts");
  }
  const auto import_image_tile_source =
      QString::fromUtf8(import_image_tile_qml.readAll());
  QFile import_source_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ImportSourceRow.qml")));
  if (!import_source_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ImportSourceRow contracts");
  }
  const auto import_source_row_source =
      QString::fromUtf8(import_source_row_qml.readAll());
  QFile project_membership_row_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/ProjectMembershipRow.qml")));
  if (!project_membership_row_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared ProjectMembershipRow contracts");
  }
  const auto project_membership_row_source =
      QString::fromUtf8(project_membership_row_qml.readAll());
  QFile mask_inspector_qml(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                               .filePath(QStringLiteral(
                                   "apps/desktop/qml/workspaces/MaskInspectorSections.qml")));
  if (!mask_inspector_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect mask inspector selection labels");
  }
  const auto mask_inspector_source = QString::fromUtf8(mask_inspector_qml.readAll());
  QFile mask_search_model_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/MaskSearchModel.qml")));
  if (!mask_search_model_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared mask search behavior");
  }
  const auto mask_search_model_source = QString::fromUtf8(mask_search_model_qml.readAll());
  QFile retouch_before_after_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/RetouchBeforeAfterControls.qml")));
  if (!retouch_before_after_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared retouch compare controls");
  }
  const auto retouch_before_after_source = QString::fromUtf8(retouch_before_after_qml.readAll());
  QFile mask_row_actions_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/MaskRowActions.qml")));
  if (!mask_row_actions_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect shared mask row actions");
  }
  const auto mask_row_actions_source = QString::fromUtf8(mask_row_actions_qml.readAll());
  QFile mask_canvas_overlay_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/workspaces/MaskCanvasOverlay.qml")));
  if (!mask_canvas_overlay_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect mask canvas overlay controls");
  }
  const auto mask_canvas_overlay_source = QString::fromUtf8(mask_canvas_overlay_qml.readAll());
  QFile desktop_main_cpp(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                             .filePath(QStringLiteral("apps/desktop/src/main.cpp")));
  if (!desktop_main_cpp.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect desktop font startup contracts");
  }
  const auto desktop_main_source = QString::fromUtf8(desktop_main_cpp.readAll());
  QFile job_runner_cpp(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                           .filePath(QStringLiteral("apps/desktop/src/job_runner.cpp")));
  if (!job_runner_cpp.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect export telemetry contracts");
  }
  const auto job_runner_source = QString::fromUtf8(job_runner_cpp.readAll());
  QFile shared_mask_overlay_qml(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/workspaces/SharedMaskOverlay.qml")));
  if (!shared_mask_overlay_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect the shared mask overlay");
  }
  const auto shared_mask_overlay_source = QString::fromUtf8(shared_mask_overlay_qml.readAll());
  const QStringList workspace_qml_paths = {
      QStringLiteral("apps/desktop/qml/workspaces/CarouselView.qml"),
      QStringLiteral("apps/desktop/qml/workspaces/ExportView.qml"),
      QStringLiteral("apps/desktop/qml/workspaces/ImportView.qml"),
      QStringLiteral("apps/desktop/qml/workspaces/LibraryView.qml"),
      QStringLiteral("apps/desktop/qml/workspaces/LibraryWorkspace.qml"),
      QStringLiteral("apps/desktop/qml/workspaces/MaskCanvasOverlay.qml"),
      QStringLiteral("apps/desktop/qml/workspaces/MaskInspectorSections.qml"),
      QStringLiteral("apps/desktop/qml/workspaces/MaskView.qml"),
      QStringLiteral("apps/desktop/qml/workspaces/SequenceWorkspace.qml"),
      QStringLiteral("apps/desktop/qml/workspaces/SharedMaskOverlay.qml"),
      QStringLiteral("apps/desktop/qml/workspaces/StudioWorkspace.qml"),
      QStringLiteral("apps/desktop/qml/workspaces/TimelineView.qml"),
        };
  const QRegularExpression raw_workspace_control_re(
      QStringLiteral("\\n\\s*(Button|Slider|CheckBox|ComboBox|Menu)\\s*\\{"));
  for (const auto& workspace_path : workspace_qml_paths) {
    QFile workspace_qml(
        QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath(workspace_path));
    if (!workspace_qml.open(QIODevice::ReadOnly | QIODevice::Text)) {
      std::cerr << "workspace=" << workspace_path.toStdString() << '\n';
      return fail("desktop shell smoke should be able to inspect every workspace QML file for raw controls");
    }
    const auto workspace_source = QString::fromUtf8(workspace_qml.readAll());
    const auto raw_control_match = raw_workspace_control_re.match(workspace_source);
    if (raw_control_match.hasMatch()) {
      std::cerr << "workspace=" << workspace_path.toStdString()
                << " rawControlOffset=" << raw_control_match.capturedStart()
                << '\n';
      return fail("workspace QML should use shared polished control components instead of raw Button, Slider, CheckBox, ComboBox, or Menu blocks");
    }
  }
  if (!desktop_main_source.contains(QStringLiteral("#include <QFontDatabase>")) ||
      !desktop_main_source.contains(QStringLiteral("QApplication::setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont))")) ||
      QApplication::font().family() != QFontDatabase::systemFont(QFontDatabase::GeneralFont).family()) {
    return fail("desktop startup should set the shared UI font before QML loads to avoid default Sans Serif fallback");
  }
  if (!mask_canvas_overlay_source.contains(
          QStringLiteral("likely edge, shadow, or reflection pixels"))) {
    return fail("object remove canvas guidance should explain assisted shadow/reflection expansion");
  }
  if (!agent_walkthrough_dialog_source.contains(QStringLiteral("function localToolsLabel")) ||
      !agent_walkthrough_dialog_source.contains(QStringLiteral("function mcpEndpointLabel")) ||
      !agent_conversation_view_source.contains(QStringLiteral("objectName: \"agentLiveMcpStatusLabel\"")) ||
      !agent_conversation_view_source.contains(QStringLiteral("Accessible.description: root.dialog.mcpEndpointLabel()"))) {
    return fail("the live AI Edit Details surface should expose local tool health and endpoint context");
  }
  if (!agent_walkthrough_dialog_source.contains(QStringLiteral("function memoryVaultLabel")) ||
      !agent_walkthrough_dialog_source.contains(QStringLiteral("externalChangeDetected")) ||
      !agent_walkthrough_dialog_source.contains(QStringLiteral("outside change")) ||
      !agent_walkthrough_dialog_source.contains(QStringLiteral("function openMemoryVaultInObsidian")) ||
      !agent_conversation_view_source.contains(QStringLiteral("objectName: \"agentOpenMemoryVaultInObsidianButton\"")) ||
      !agent_walkthrough_dialog_source.contains(QStringLiteral("openMemoryVaultInObsidian(false)")) ||
      !agent_conversation_view_source.contains(QStringLiteral("objectName: \"agentExportRedactedMemoryButton\""))) {
    return fail("the live AI Edit Details surface should expose external memory changes and local vault actions");
  }
  if (!agent_walkthrough_dialog_source.contains(QStringLiteral("function latestStyleChangeSummary")) ||
      !agent_conversation_view_source.contains(QStringLiteral("objectName: \"agentLiveStyleChangeSummaryLabel\""))) {
    return fail("the live AI Edit Details surface should show the latest approved style-memory change");
  }
  if (!agent_walkthrough_dialog_source.contains(QStringLiteral("function toolCalls")) ||
      !agent_walkthrough_dialog_source.contains(QStringLiteral("function runWarnings")) ||
      !agent_walkthrough_dialog_source.contains(QStringLiteral("function latestToolCallSummary")) ||
      !agent_conversation_view_source.contains(QStringLiteral("objectName: \"agentLiveRunAuditPanel\"")) ||
      !agent_conversation_view_source.contains(QStringLiteral("objectName: \"agentLiveRunProofPathLabel\"")) ||
      !agent_conversation_view_source.contains(QStringLiteral("objectName: \"agentLiveRunWarningsLabel\""))) {
    return fail("the live AI Edit Details surface should expose proof, tool-call, validation, and warning audit context");
  }
  if (!agent_proof_card_source.contains(QStringLiteral("agentProofCardContract: \"agent-proof-card:v1|image-fit:true|label-badge:true|empty-placeholder:true|tooltip-full:true|keyboard-focus:true|readable-text:true|no-elide:true\"")) ||
      !agent_proof_card_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !agent_proof_card_source.contains(QStringLiteral("objectName: root.imageObjectName")) ||
      !agent_proof_card_source.contains(QStringLiteral("fillMode: Image.PreserveAspectFit")) ||
      !agent_proof_card_source.contains(QStringLiteral("visible: root.source.toString().length === 0")) ||
      !agent_proof_card_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !agent_proof_card_source.contains(QStringLiteral("elide: Text.ElideNone"))) {
    return fail("Edit agent proof previews should use AgentProofCard with stable image object names, fit rendering, and no-elide labels");
  }
  if (!agent_tool_call_row_source.contains(QStringLiteral("agentToolCallRowContract: \"agent-tool-call-row:v1|status-dot:true|title-wrap:true|detail-wrap:true|hover:true|keyboard-focus:true|readable-text:true|no-elide:true\"")) ||
      !agent_tool_call_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|title-wrap:true|detail-wrap:true|tooltip-full:true|accessibility-full:true\"")) ||
      !agent_tool_call_row_source.contains(QStringLiteral("Accessible.description: root.detail")) ||
      !agent_tool_call_row_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !agent_tool_call_row_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !agent_tool_call_row_source.contains(QStringLiteral("text: root.detail.length > 0 ? root.title + \"\\n\" + root.detail : root.title"))) {
    return fail("Edit agent tool-call audit rows should use AgentToolCallRow with readable no-elide title/detail text and preserved status logic");
  }
  if (!agent_recipe_diff_row_source.contains(QStringLiteral("agentRecipeDiffRowContract: \"agent-recipe-diff-row:v1|label-wrap:true|summary-wrap:true|count-badge:true|hover:true|keyboard-focus:true|readable-text:true|no-elide:true\"")) ||
      !agent_recipe_diff_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-wrap:true|summary-wrap:true|badge-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !agent_recipe_diff_row_source.contains(QStringLiteral("Accessible.description: root.summary")) ||
      !agent_recipe_diff_row_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !agent_recipe_diff_row_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !agent_recipe_diff_row_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !agent_recipe_diff_row_source.contains(QStringLiteral("text: root.summary.length > 0 ? root.label + \"\\n\" + root.summary : root.label"))) {
    return fail("Edit agent recipe diff rows should use AgentRecipeDiffRow with readable label/summary text and preserved count binding");
  }
  if (!agent_run_history_row_source.contains(QStringLiteral("agentRunHistoryRowContract: \"agent-run-history-row:v1|title-wrap:true|detail-wrap:true|status-badge:true|hover:true|keyboard-focus:true|readable-text:true|no-elide:true\"")) ||
      !agent_run_history_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|title-wrap:true|detail-wrap:true|badge-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !agent_run_history_row_source.contains(QStringLiteral("Accessible.description: root.detail")) ||
      !agent_run_history_row_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !agent_run_history_row_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !agent_run_history_row_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !agent_run_history_row_source.contains(QStringLiteral("text: root.detail.length > 0 ? root.title + \"\\n\" + root.detail : root.title"))) {
    return fail("Edit agent run history rows should use AgentRunHistoryRow with readable no-elide title/detail text and preserved status binding");
  }
  if (!studio_workspace_source.contains(QStringLiteral("SharedMaskOverlay {")) ||
      !mask_view_source.contains(QStringLiteral("SharedMaskOverlay {")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("objectNamePrefix")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("sharedOverlayVersion")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("overlayGeometryContract")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("overlayGeometrySignature")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("maskRasterPreviewPolicy")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("maskPreviewMode")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("GeometricMaskCanvas")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("polygonVertexHandle")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("polygonEdgeInsertHandle")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("handleReadabilityContract")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("handleOuterHaloColor")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("handleInnerHaloColor")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("anchors.margins: -8"))) {
    return fail("StudioWorkspace and MaskView should share one mask overlay renderer with stable matte, geometry, and polygon handle anchors");
  }
  if (studio_workspace_source.contains(QStringLiteral("ctx.lineTo(x + height, 0)")) ||
      studio_workspace_source.contains(QStringLiteral("#D4FFF8"))) {
    return fail("mask and brush modes should not draw diagonal hatch lines over the image");
  }
  const auto photo_layer_opacity_slider_index =
      studio_workspace_source.indexOf(QStringLiteral("objectName: \"photoLayerOpacitySlider\""));
  const auto photo_layer_mask_opacity_slider_index =
      studio_workspace_source.indexOf(QStringLiteral("objectName: \"photoLayerMaskOpacitySlider\""));
  const auto photo_layer_opacity_slider_source =
      photo_layer_opacity_slider_index >= 0
          ? studio_workspace_source.mid(
                photo_layer_opacity_slider_index,
                photo_layer_mask_opacity_slider_index > photo_layer_opacity_slider_index
                    ? photo_layer_mask_opacity_slider_index - photo_layer_opacity_slider_index
                    : 900)
          : QString();
  if (photo_layer_opacity_slider_source.isEmpty() ||
      !photo_layer_opacity_slider_source.contains(QStringLiteral("onPressedChanged: {")) ||
      !photo_layer_opacity_slider_source.contains(QStringLiteral("editorPreview.beginCoalescedEdit()")) ||
      !photo_layer_opacity_slider_source.contains(QStringLiteral("editorPreview.endCoalescedEdit()")) ||
      !photo_layer_opacity_slider_source.contains(
          QStringLiteral("onMoved: editorPreview.setPhotoLayerOpacity(photoLayerPropertiesPanel.layerIndex, value / 100.0)"))) {
    return fail("photo layer opacity drags should open a coalesced edit so one drag creates one undo step");
  }
  if (!thumbnail_worker_renderer_factory_source.contains(
          QStringLiteral("layer-mask-render-stage-telemetry-v1")) ||
      !thumbnail_worker_renderer_factory_source.contains(
          QStringLiteral("thumbnail_layer_composite_ms"))) {
    return fail("thumbnail renderer should expose layer/mask stage telemetry fields");
  }
  if (!editor_preview_controller_source.contains(QStringLiteral("layer_recipe_evaluation_ms")) ||
      !editor_preview_controller_source.contains(QStringLiteral("mask_alpha_retrieval_ms")) ||
      !editor_preview_controller_source.contains(QStringLiteral("layer_composite_ms"))) {
    return fail("preview renderer should expose layer/mask stage telemetry fields");
  }
  if (!job_runner_source.contains(QStringLiteral("final_export_stage")) ||
      !job_runner_source.contains(QStringLiteral("final_export_ms")) ||
      !job_runner_source.contains(QStringLiteral("mask_alpha_retrieval_provider"))) {
    return fail("export renderer should expose layer/mask stage telemetry fields");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function previewFailureRowLabel")) ||
      !studio_workspace_source.contains(QStringLiteral("function photoLayerPreviewFailureStage")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskSetPreviewFailureStage")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.previewFailureLayerIds")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.previewFailureMaskSetIds")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerFailureLabel")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskFailureLabel")) ||
      !studio_workspace_source.contains(QStringLiteral("? layerFailureLabel")) ||
      !studio_workspace_source.contains(QStringLiteral("text: maskFailureLabel.length > 0 ? maskFailureLabel"))) {
    return fail("layer and mask rows should surface preview failure stages while preserving the last good canvas");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("id: previewItem")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("x: root.imageX")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("y: root.imageY")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("width: root.imageWidth")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("height: root.imageHeight")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("clip: true")) ||
      !studio_workspace_source.contains(QStringLiteral("x: viewport.paintedContentWidth > 1 ? viewport.paintedContentX")) ||
      !studio_workspace_source.contains(QStringLiteral("width: viewport.paintedContentWidth > 1 ? viewport.paintedContentWidth")) ||
      !studio_workspace_source.contains(QStringLiteral("imageX: editViewport.paintedContentX")) ||
      !studio_workspace_source.contains(QStringLiteral("imageWidth: editViewport.paintedContentWidth"))) {
    return fail("mask overlays and canvas background treatment should be clipped to the painted image rect, leaving pasteboard treatment outside image pixels");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("outlineRenderContract")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("overlayGeometryContract")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("overlayGeometrySignature")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("clip: true")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("stableOutlineWidth")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("stableOutlineDash")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("normalizedOutlinePathsCache")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("outlineQualityLadderContract")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("activeMaskOutlineQualityTier")) ||
	      !mask_canvas_overlay_source.contains(QStringLiteral("outlineTraceContract: \"public-outline-trace|source|tolerance|provider|path-count|image-normalized|final-alpha:v2\"")) ||
	      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property var activeOutlineTrace: outlineActivationDeferred ? ({}) : (editorPreview.activeMaskOutlineTrace || ({}))")) ||
	      !mask_canvas_overlay_source.contains(QStringLiteral("!root.outlineActivationDeferred")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property real outlineTraceTolerancePixels: root.activeOutlineTraceTolerancePixels")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property string outlineCoordinateSpace: root.activeOutlineCoordinateSpace")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property string outlineAlphaSourceContract: root.activeOutlineAlphaSourceContract")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property string outlineTraceProviderContract: root.activeOutlineTraceProviderContract")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property string outlineTraceMaskAlphaSignature: root.activeOutlineTraceMaskAlphaSignature")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskOutlineQualityLabel")) ||
      !mask_inspector_source.contains(QStringLiteral("function maskOutlineQualityLabel")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("pixelAligned")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("antialiasing: true")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("outlinePhaseAnimationEnabled")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("onSelectionOutlineAnimationEnabledChanged")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("handleReadabilityContract")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("handleOuterHaloColor")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("handleInnerHaloColor")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("outlineRenderContract")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("outlineAnimationContract")) ||
	      !shared_mask_overlay_source.contains(QStringLiteral("outlineTraceContract: \"public-outline-trace|source|tolerance|provider|path-count|image-normalized|final-alpha:v2\"")) ||
	      !shared_mask_overlay_source.contains(QStringLiteral("readonly property var activeOutlineTrace: outlineActivationDeferred ? ({}) : (editorPreview.activeMaskOutlineTrace || ({}))")) ||
	      !shared_mask_overlay_source.contains(QStringLiteral("!root.outlineActivationDeferred")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property real outlineTraceTolerancePixels: root.activeOutlineTraceTolerancePixels")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property string outlineCoordinateSpace: root.activeOutlineCoordinateSpace")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property string outlineAlphaSourceContract: root.activeOutlineAlphaSourceContract")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property string outlineTraceProviderContract: root.activeOutlineTraceProviderContract")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property string outlineTraceMaskAlphaSignature: root.activeOutlineTraceMaskAlphaSignature")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("SharedActiveSelectionOutline")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("outlineAnimationEnabled")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("sharedCanvasPaintTimer")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("scheduleActiveSelectionOutlinePaint")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("stableOutlineWidth")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("stableOutlineDash")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("normalizedOutlinePathsCache")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("pixelAligned")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("antialiasing: true")) ||
      !studio_workspace_source.contains(QStringLiteral("selectionOutlineAnimationContract")) ||
      !studio_workspace_source.contains(QStringLiteral("selectionOutlineAnimationPointBudget")) ||
      !studio_workspace_source.contains(QStringLiteral("selectionOutlineAnimationShouldRun")) ||
      !studio_workspace_source.contains(QStringLiteral("if (!root.visible || page !== \"edit\"")) ||
      !studio_workspace_source.contains(QStringLiteral("pointCount >= 2 && pointCount <= root.selectionOutlineAnimationPointBudget")) ||
      !studio_workspace_source.contains(QStringLiteral("cropX: editorPreview.cropRectX")) ||
      !studio_workspace_source.contains(QStringLiteral("transformRotation: editorPreview.transformRotation")) ||
      !mask_view_source.contains(QStringLiteral("maskViewActiveSelectionOutline")) ||
      !mask_view_source.contains(QStringLiteral("outlineRenderContract")) ||
      !mask_view_source.contains(QStringLiteral("cropX: editorPreview.cropRectX")) ||
      !mask_view_source.contains(QStringLiteral("transformRotation: editorPreview.transformRotation")) ||
      !mask_view_source.contains(QStringLiteral("selectionOutlineAnimationContract")) ||
      !mask_view_source.contains(QStringLiteral("selectionOutlineAnimationPointBudget")) ||
      !mask_view_source.contains(QStringLiteral("selectionOutlineAnimationShouldRun")) ||
      !mask_view_source.contains(QStringLiteral("if (!root.visible || (!editorPreview.hasActiveMask")) ||
      !mask_view_source.contains(QStringLiteral("pointCount >= 2 && pointCount <= root.selectionOutlineAnimationPointBudget")) ||
      !mask_outline_tracing_source.contains(QStringLiteral("interpolated_alpha_contours")) ||
      !mask_outline_presentation_source.contains(QStringLiteral("filter_minor_composite_outline_paths")) ||
      !mask_outline_presentation_source.contains(QStringLiteral("if (area <= std::numeric_limits<double>::epsilon())")) ||
      !mask_outline_presentation_source.contains(QStringLiteral("stats->minimum_secondary_area = 0.0")) ||
      !mask_outline_presentation_source.contains(QStringLiteral("stats->applied = stats->output_path_count != stats->input_path_count")) ||
      !editor_preview_controller_source.contains(QStringLiteral("preserve-valid-holes-and-islands:v2")) ||
      !mask_outline_simplification_source.contains(QStringLiteral("tolerance_simplified_closed_outline_points")) ||
      !mask_outline_simplification_source.contains(QStringLiteral("max_closed_outline_error_pixels")) ||
      !editor_preview_controller_source.contains(QStringLiteral("kMaskOutlineTraceLatencyBudgetMs = 33")) ||
      !editor_preview_controller_source.contains(QStringLiteral("outline_trace_within_latency_budget")) ||
      !mask_view_source.contains(QStringLiteral("handleReadabilityContract")) ||
      !mask_view_source.contains(QStringLiteral("handleOuterHaloColor")) ||
      !mask_view_source.contains(QStringLiteral("handleInnerHaloColor")) ||
      !mask_view_source.contains(QStringLiteral("stableOutlineWidth")) ||
      !mask_view_source.contains(QStringLiteral("stableOutlineDash")) ||
      !mask_view_source.contains(QStringLiteral("pixelAligned")) ||
      !mask_view_source.contains(QStringLiteral("antialiasing: true"))) {
    return fail("mask outline tracing should preserve valid islands and holes, discard degenerate contours, and disclose measured trace/simplification budgets");
  }
  if (!editor_preview_controller_source.contains(QStringLiteral("QVariantMap EditorPreviewController::activeMaskOutlineTrace() const")) ||
      !editor_preview_controller_source.contains(QStringLiteral("(void)activeMaskOutlinePaths();")) ||
      !editor_preview_controller_source.contains(QStringLiteral("return active_mask_outline_trace_fields_;"))) {
    return fail("active mask outline trace metadata should be exposed through the public controller API");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("maskCanvasHoverInputHandler")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("maskCanvasBrushStrokeTabletInput")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("maskCanvasBrushStrokeMouseInput")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("maskCanvasSelectionDragMouseInput")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("brush-resize")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smart-selection-drag")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("lasso-drag")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("pointerGrabPolicy: \"exclusive-brush-stroke\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("pointerGrabPolicy: \"exclusive-selection-drag\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("preventStealing: true")) ||
      !studio_viewport_input_source.contains(QStringLiteral("PanZoomInputHandler")) ||
      !studio_viewport_input_source.contains(QStringLiteral("inputHandlerRole: \"pan-zoom\"")) ||
      !studio_viewport_input_source.contains(QStringLiteral("pointerGrabPolicy: viewport.masked ? \"defer-to-mask-overlay\" : \"viewport-pan-zoom\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool nativePreviewActive: editedPreviewSurface &&")) ||
      !studio_image_viewport_source.contains(QStringLiteral("previewContext.displayPreviewNativeAvailable")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool providerPreviewVisible: String(source || \"\").length > 0 &&")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool previewContentVisible: nativePreviewActive ||")) ||
      !studio_workspace_source.contains(QStringLiteral("EditorPreviewTextureItem {")) ||
      !studio_image_viewport_source.contains(QStringLiteral("previewContext.setPreviewPresentationMetrics(")) ||
      studio_workspace_source.contains(QStringLiteral("const contentEdge = Math.max(width, height)")) ||
      studio_workspace_source.contains(QStringLiteral("const contentEdge = Math.max(width, height, paintedContentWidth, paintedContentHeight)")) ||
      !studio_workspace_source.contains(QStringLiteral("function setZoom(nextZoom, anchorX, anchorY)")) ||
      !studio_image_viewport_source.contains(QStringLiteral("const anchorNormalizedX = workspaceContext.clamp((pivotX - currentPaintedContentX) / Math.max(1, currentPaintedContentWidth), 0, 1)")) ||
      !studio_workspace_source.contains(QStringLiteral("function wheelZoomFactor(wheel)")) ||
      !studio_image_viewport_source.contains(QStringLiteral("Math.pow(wheelZoomStepBase, workspaceContext.clamp(steps, -6.0, 6.0))")) ||
      !studio_viewport_input_source.contains(QStringLiteral("viewport.setZoom(viewport.zoom * zoomFactor, wheel.x, wheel.y)")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskSpacePanShortcutActive()")) ||
      !studio_workspace_source.contains(QStringLiteral("event.key === Qt.Key_Space")) ||
      !studio_workspace_source.contains(QStringLiteral("root.spacePanActive = true")) ||
      !studio_image_viewport_source.contains(QStringLiteral("!workspaceContext.spacePanActive &&")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskOperationForModifierState(modifierState)")) ||
      !studio_workspace_source.contains(QStringLiteral("function applyMaskOperationForModifierState(modifierState)")) ||
      !studio_workspace_source.contains(QStringLiteral("Qt.ShiftModifier")) ||
      !studio_workspace_source.contains(QStringLiteral("Qt.AltModifier")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editRoot.applyMaskOperationForModifierState(mouse.modifiers)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editRoot.applyObjectPickModeForModifierState(mouse.modifiers)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("enabled: active && !editRoot.spacePanActive")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("enabled: !editRoot.spacePanActive &&")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("polygonEdgeInsertInputHandler")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("polygonVertexDragInputHandler")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("inputHandlerRole: \"polygon-edit\"")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("pointerGrabPolicy: \"exclusive-polygon-edit\"")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("preventStealing: true")) ||
      !mask_view_source.contains(QStringLiteral("pointerGrabPolicy: \"exclusive-smart-selection-stroke\"")) ||
      !mask_view_source.contains(QStringLiteral("pointerGrabPolicy: \"exclusive-lasso-drag\"")) ||
      !mask_view_source.contains(QStringLiteral("pointerGrabPolicy: \"exclusive-polygon-tap\"")) ||
      !mask_view_source.contains(QStringLiteral("grabPermissions: PointerHandler.TakeOverForbidden"))) {
    return fail("masking input paths should expose separate hover, tap, drag, brush stroke, polygon edit, resize, pan/zoom, and modifier operation roles");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("function brushCursorState(mask)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function selectionCursorState()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function cursorShapeForState(cursorState)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("return \"clone-source\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("return \"retouch-apply\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("return editorPreview.activeMaskOperation === \"subtract\" ? \"erase\" : \"add\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("return \"subtract\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("? \"close-polygon\" : \"polygon-point\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property string cursorState: root.brushCursorState(mask)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property string cursorState: root.selectionCursorState()"))) {
    return fail("masking cursor states should distinguish add, subtract, erase, polygon point, close polygon, clone source, and retouch apply");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("smartSelectHoverPrewarmContract: \"pointer-pause|jitter-gated|cancel-stale|request-serial:v2\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("id: smartSelectPreviewPauseTimer")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewPauseMs: 180")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewMoveEpsilon: 0.003")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("property int smartSelectPreviewRequestSerial: 0")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("property int pendingSmartSelectPreviewSerial: 0")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("root.pendingSmartSelectPreviewSerial !== root.smartSelectPreviewRequestSerial")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("root.smartSelectPreviewRequestSerial += 1")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewPauseTimer.restart()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("root.stopPendingSmartSelectPreview()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("root.scheduleSmartSelectPreviewFromItem(previewItem, mouse.x, mouse.y, false)")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("root.scheduleSmartSelectPreviewDebounced(mouse.x / previewItem.width, mouse.y / previewItem.height"))) {
    return fail("smart-selection hover prewarm should wait for pointer pause, ignore jitter, and avoid immediate enter/release segmentation");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("maskCanvasBrushCursorFeatherRing")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("maskCanvasBrushCursorOuterHaloRing")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("maskCanvasBrushCursorHardnessRing")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("maskCanvasBrushCursorReadout")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("brushCursorVisibilityContract: \"persistent-last-position|dual-contrast-ring|hardness-inner-ring:v1\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("brushCursorRepaintContract: \"cursor-item-position-binding|no-static-canvas-repaint-on-hover:v1\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("liveOverlayFrameBudgetMs: 16")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("liveOverlayPointBudget: 512")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("live-brush-overlay|ui-frame-budget:16ms|point-budget:512|no-preview-render-block:v1")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("const paintStart = Math.max(0, points.length - paintCount)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("brushCursorPositionKnown")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("visible: ((brushMode && cursorPositionReady)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("x: root.hoverX - width / 2")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("y: root.hoverY - height / 2")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("\"  Flow \" + brushPercentText(editorPreview.activeMaskBrushFlow")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("\"  Density \" + brushPercentText(editorPreview.activeMaskDensity")) ||
      !mask_view_source.contains(QStringLiteral("maskViewBrushCursorOuterHaloRing")) ||
      !mask_view_source.contains(QStringLiteral("maskViewBrushCursorFeatherRing")) ||
      !mask_view_source.contains(QStringLiteral("maskViewBrushCursorHardnessRing")) ||
      !mask_view_source.contains(QStringLiteral("maskViewBrushCursorReadout")) ||
      !mask_view_source.contains(QStringLiteral("brushCursorVisibilityContract: \"persistent-last-position|dual-contrast-ring|hardness-inner-ring:v1\"")) ||
      !mask_view_source.contains(QStringLiteral("brushCursorPositionKnown")) ||
      !mask_view_source.contains(QStringLiteral("\"  Flow \" + root.percentText(editorPreview.activeMaskBrushFlow)")) ||
      !mask_view_source.contains(QStringLiteral("\"  Density \" + root.percentText(editorPreview.activeMaskDensity)")) ||
      !studio_workspace_source.contains(QStringLiteral("function brushDensityValueText()")) ||
      !studio_workspace_source.contains(QStringLiteral("function brushOpacityValueText()")) ||
      !studio_workspace_source.contains(QStringLiteral("BrushContextPanel {")) ||
      !studio_workspace_source.contains(QStringLiteral("brush-context-default:v2|hidden:true|reason:hud-owns-command-surface")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: false")) ||
      !brush_context_panel_source.contains(QStringLiteral("brush-context-panel:v1|component:true|target-summary:true|mask-summary:true|hidden-advanced-preserved:true|object-names:preserved|no-elide:true")) ||
      !brush_context_panel_source.contains(QStringLiteral("objectName: \"brushContextCompactBrushPanel\"")) ||
      !brush_context_panel_source.contains(QStringLiteral("brushPanelContract: \"size-flow-hardness-density-opacity|compact:v2|visible:false|tuning-menu:true|inspector-preserved:true\"")) ||
      !studio_workspace_source.contains(QStringLiteral("BrushLayerInspectorHeader {")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"brushLayerInspectorHeader\"")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: root.brushToolReady()")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerSparseHeader\"")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: !root.brushToolReady()")) ||
      !brush_layer_inspector_header_source.contains(QStringLiteral("brush-layer-inspector-header:v1|brush-only:true|target-summary:true|mask-summary:true|add-menu:true|isolate-action:true|no-elide:true|functionality:preserved")) ||
      !brush_layer_inspector_header_source.contains(QStringLiteral("objectName: \"brushLayerInspectorAddMenuButton\"")) ||
      !brush_layer_inspector_header_source.contains(QStringLiteral("objectName: \"brushLayerInspectorIsolationButton\"")) ||
      !brush_layer_inspector_header_source.contains(QStringLiteral("objectName: \"brushLayerInspectorTargetNameLabel\"")) ||
      !brush_layer_inspector_header_source.contains(QStringLiteral("objectName: \"brushLayerInspectorMaskStatusLabel\"")) ||
      !studio_brush_cursor_source.contains(QStringLiteral("BrushCursorOuterHaloRing")) ||
      !studio_brush_cursor_source.contains(QStringLiteral("required property string viewportObjectName")) ||
      !studio_brush_cursor_source.contains(QStringLiteral("required property bool brushVisible")) ||
      studio_brush_cursor_source.contains(QStringLiteral("required property Item workspace")) ||
      studio_brush_cursor_source.contains(QStringLiteral("required property Item viewport")) ||
      !studio_brush_cursor_source.contains(QStringLiteral("brushCursorVisibilityContract: \"persistent-last-position|dual-contrast-ring|hardness-inner-ring:v1\"")) ||
      !studio_brush_cursor_source.contains(QStringLiteral("visible: cursorStack.brushVisible")) ||
      studio_brush_cursor_source.contains(QStringLiteral("viewportMouse.containsMouse")) ||
      !studio_brush_cursor_source.contains(QStringLiteral("BrushCursorFeatherRing")) ||
      !studio_brush_cursor_source.contains(QStringLiteral("\" / Flow \" +")) ||
      !studio_brush_cursor_source.contains(QStringLiteral("cursorStack.brushFlowText")) ||
      !studio_brush_cursor_source.contains(QStringLiteral("\" / Density \" +")) ||
      !studio_brush_cursor_source.contains(QStringLiteral("cursorStack.brushDensityText"))) {
    return fail("brush cursors and compact brush panels should expose feather, hardness, flow, density, and opacity readouts");
  }
  if (!shared_mask_overlay_source.contains(QStringLiteral("maskOverlayColorContract: \"color-safe-overlay:v2|palette:standard-protanopia-deuteranopia-tritanopia-grayscale|operations:add-subtract-intersect|dual-contrast-halo:true|custom-color-override|retouch-exception\"")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property color defaultMaskOverlayColor: theme.selectionPalette(\"add\")")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property color activeOperationColor: resolvedOverlayColor(")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("defaultMaskOverlayColor)")) ||
      !editor_preview_controller_source.contains(QStringLiteral("QColor selection_operation_preview_color")) ||
      !editor_preview_controller_source.contains(QStringLiteral("return QColor(QStringLiteral(\"#24dcc2\"));")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function selectionOperationOverlayColor(operationName)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function resolvedMaskOverlayColor()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("settledMaskOutlineContract: \"final-alpha-contour|overlay-fill|marching-ants:v2\"")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("settledMaskOutlineContract: \"final-alpha-contour|overlay-fill|marching-ants:v2\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function displayOutlinePathLargeEnough(points, isCandidate)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("function displayOutlinePathLargeEnough(points)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property color overlayAccent: root.resolvedMaskOverlayColor()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("return resolvedMaskOverlayColor()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("GradientStop { position: 0.0; color: Qt.alpha(overlayAccent")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("root.strokeOutlinePaths(ctx, outlinePaths, root.resolvedMaskOverlayColor(), false)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("root.fillOutlinePaths(")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("root.fillOutlinePaths(ctx, outlinePaths)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("property bool selectionFillVisible: false")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("property bool selectionFillVisible: false")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("return root.selectionFillVisible &&")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("if (!root.selectionFillVisible ||")) ||
      !studio_image_viewport_source.contains(QStringLiteral("selectionFillVisible: workspaceContext.editInspectorTab === \"layers\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("return selectionOperationOverlayColor(\"subtract\")")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("root.selectionOperationOverlayColor(editorPreview.activeMaskOperation)")) ||
      !mask_view_source.contains(QStringLiteral("function selectionOperationOverlayColor(operationName)")) ||
      !mask_view_source.contains(QStringLiteral("function resolvedMaskOverlayColor(mask)")) ||
      !mask_view_source.contains(QStringLiteral("readonly property color overlayAccent: root.resolvedMaskOverlayColor(mask)")) ||
      !mask_view_source.contains(QStringLiteral("ctx.fillStyle = Qt.alpha(overlayAccent")) ||
      !studio_workspace_source.contains(QStringLiteral("function selectionOperationOverlayColor(operationName)")) ||
      !studio_brush_cursor_source.contains(QStringLiteral("border.color: cursorStack.brushOperationColor")) ||
      !studio_image_viewport_source.contains(QStringLiteral("brushOperationColor: workspaceContext.selectionOperationOverlayColor(previewContext.activeMaskOperation)"))) {
    return fail("selection overlays should use color-safe custom-overridable operation colors with final-alpha contour fill and marching-ants support");
  }
  if (!mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayButton")) ||
      !mask_inspector_source.contains(QStringLiteral("Color Overlay")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewColorOnBwButton")) ||
      !mask_inspector_source.contains(QStringLiteral("Color on B&W")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewImageOnBwButton")) ||
      !mask_inspector_source.contains(QStringLiteral("Image on B&W")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewMatteButton")) ||
      !mask_inspector_source.contains(QStringLiteral("Grayscale Alpha")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOnBlackButton")) ||
      !mask_inspector_source.contains(QStringLiteral("Isolated Mask")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOnWhiteButton")) ||
      !mask_inspector_source.contains(QStringLiteral("On White")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOutlineButton")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskPreviewMode = \"outline\"")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewMarchingAntsButton")) ||
      !mask_inspector_source.contains(QStringLiteral("Marching Ants")) ||
      !mask_inspector_source.contains(QStringLiteral("rubylithOverlayPresetContract: \"rubylith-overlay-preset:v1|display-mode:overlay|color:#ff2f5f|single-color:true|separate-from-diagnostics:true\"")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewRubylithOverlayPresetButton")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskPreviewOverlayColor = \"#ff2f5f\"")) ||
      !mask_inspector_source.contains(QStringLiteral("grayscale-alpha")) ||
      !mask_inspector_source.contains(QStringLiteral("isolated-mask")) ||
      !mask_inspector_source.contains(QStringLiteral("hidden")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("maskRasterPreviewPolicy")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function cleanVectorMaskFillVisible()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.maskPreviewMode !== \"outline\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("source: \"\"")) ||
      shared_mask_overlay_source.contains(QStringLiteral("activeMaskPreviewSource")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property string previewSource: \"\"")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("editorPreview.maskPreviewMode === \"outline\"")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("MaskPreviewBwBackdrop")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("function previewSourceOpacity(activeBrushPainting)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("activeSelectionBwBackdrop")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function usesBlackWhiteImageContext()")) ||
      !mask_inspector_source.contains(QStringLiteral("normalMaskDisplayContract")) ||
      !mask_inspector_source.contains(QStringLiteral("normalMaskPreviewModeFlow")) ||
      !mask_inspector_source.contains(QStringLiteral("diagnosticMaskDisplayContract")) ||
      !mask_inspector_source.contains(QStringLiteral("property bool diagnosticMaskPreviewModesVisible: false")) ||
      !mask_inspector_source.contains(QStringLiteral("property bool runtimeDiagnosticsExpanded: false")) ||
      !mask_inspector_source.contains(QStringLiteral("runtimeDiagnosticsVisibilityContract: \"runtime-diagnostics:v1|chips:always|raw-status:details-only|default-expanded:false|toggle:available|no-debug-text-at-rest:true|functionality:preserved\"")) ||
      !mask_inspector_source.contains(QStringLiteral("developerDiagnosticsMenuContract: \"developer-diagnostics-menu:v2|developer-mode-gated:true|normal-mask-modes:separate|matte-debug:collapsed|runtime-detail:collapsed|raw-alpha:diagnostic-only|confidence:false-color-focus:diagnostic-only|default-visible:false\"")) ||
      !mask_inspector_source.contains(QStringLiteral("function runtimeDiagnosticsSignature()")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewDiagnosticsToggleButton")) ||
      !mask_inspector_source.contains(QStringLiteral("diagnosticMaskPreviewModeFlow")) ||
      !mask_inspector_source.contains(QStringLiteral("visible: editRoot.developerDiagnosticsEnabled === true &&")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskRuntimeDiagnosticsToggleButton\"")) ||
      !mask_inspector_source.contains(QStringLiteral("text: root.runtimeDiagnosticsExpanded ? \"Hide Runtime Details\" : \"Runtime Details\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskRuntimeDiagnosticsLabel\"")) ||
      !mask_inspector_source.contains(QStringLiteral("visible: editRoot.developerDiagnosticsEnabled === true &&")) ||
      !mask_inspector_source.contains(QStringLiteral("runtimeDiagnosticsSignature: root.runtimeDiagnosticsSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.maskPreviewMode = editorPreview.maskPreviewDisplayMode === \"marching-ants\"")) ||
      !studio_workspace_source.contains(QStringLiteral("? \"overlay\"")) ||
      !studio_workspace_source.contains(QStringLiteral(": \"marching-ants\"")) ||
      !studio_workspace_source.contains(QStringLiteral("root.setActiveMaskPreviewVisible(editorPreview.maskPreviewDisplayMode === \"hidden\")")) ||
      !studio_workspace_source.contains(QStringLiteral("\"|preview:\" + String(editorPreview.maskPreviewDisplayMode || \"overlay\")")) ||
      !studio_workspace_source.contains(QStringLiteral("const mode = String(editorPreview.maskPreviewDisplayMode || \"overlay\").trim().toLowerCase()")) ||
      !studio_workspace_source.contains(QStringLiteral("\"|mode:\" + String(editorPreview.maskPreviewDisplayMode || \"overlay\")")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.maskPreviewMode = visible ? \"overlay\" : \"hidden\"")) ||
      !studio_workspace_source.contains(QStringLiteral("return editorPreview.maskPreviewDisplayMode")) ||
      !studio_workspace_source.contains(QStringLiteral("previewActive: editorPreview.maskPreviewDisplayMode !== \"hidden\"")) ||
      !brush_context_panel_source.contains(QStringLiteral("active: root.previewActive")) ||
      !studio_workspace_source.contains(QStringLiteral("active: editorPreview.maskPreviewDisplayMode === \"overlay\"")) ||
      !studio_workspace_source.contains(QStringLiteral("active: editorPreview.maskPreviewDisplayMode === \"marching-ants\"")) ||
      !studio_workspace_source.contains(QStringLiteral("active: editorPreview.maskPreviewDisplayMode === \"hidden\"")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.maskPreviewMode = \"hidden\"")) ||
      studio_workspace_source.contains(QStringLiteral("editorPreview.maskPreviewMode = \"none\"")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"Outline\"")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"Grayscale Alpha\"")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"Isolated Mask\""))) {
    return fail("mask preview controls should keep inspection modes explicit while the normal V shortcut cycles only overlay, marching ants, and hidden");
  }
  if (!mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayOpacitySlider")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskPreviewOverlayOpacity")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayColorDialog")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayColorSwatch")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayColorButton")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewOverlayColorResetButton")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewShowAllButton")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskPreviewShowAll = !editorPreview.maskPreviewShowAll")) ||
      !editor_preview_controller_source.contains(QStringLiteral("EditorPreviewController::maskPreviewShowAll() const")) ||
      !editor_preview_controller_source.contains(QStringLiteral("EditorPreviewController::setMaskPreviewShowAll(bool show_all)")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskPreviewOverlayColor = selectedColor")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("function resolvedOverlayColor(fallbackColor)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("function overlayOpacityScale()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("Number(editorPreview.maskPreviewOverlayOpacity")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.maskPreviewOverlayColor")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.maskPreviewShowAll !== true"))) {
    return fail("mask preview controls should expose overlay opacity/color controls and an explicit show-all-masks toggle");
  }
  if (!studio_workspace_source.contains(QStringLiteral("activeInpaintRegenerationAvailable")) ||
      !studio_workspace_source.contains(QStringLiteral("regenerateActiveInpaintPatch()")) ||
      !studio_workspace_source.contains(QStringLiteral("Advance the seed for the pending retouch patch candidate"))) {
    return fail("retouch Regenerate should route committed patches through real inpaint regeneration while keeping pending seed control explicit");
  }
  if (!studio_workspace_source.contains(QStringLiteral("editorPreview.inpaintProgressActive")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.inpaintProgressValue")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.inpaintProgressLabel")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskActivityProgressValue()")) ||
      !studio_workspace_source.contains(QStringLiteral("|progress-animation:canvas-strip|smart:indeterminate|retouch:determinate")) ||
      !studio_viewport_status_source.contains(QStringLiteral("activityAnimationContract: \"nonblocking|persistent|animated-dot:v2|progress-strip:true|smart:indeterminate|retouch:determinate\"")) ||
      !studio_viewport_status_source.contains(QStringLiteral("required property string viewportObjectName")) ||
      !studio_viewport_status_source.contains(QStringLiteral("signal retryRequested()")) ||
      !studio_viewport_status_source.contains(QStringLiteral("progress: statusStack.maskActivityProgressValue")) ||
      !studio_viewport_status_source.contains(QStringLiteral("trackColor: \"transparent\""))) {
    return fail("Studio should bind smart-selection and retouch progress into a nonblocking animated canvas chip");
  }
  trace_smoke_progress("selection retouch progress source checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_SELECTION_RETOUCH_PROGRESS_SOURCE")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }
  if (!mask_view_source.contains(QStringLiteral("editorPreview.inpaintPatchDiagnosticsAvailable")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.inpaintPatchDiagnosticsAvailable")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.inpaintPatchDiagnosticsAvailable")) ||
      !mask_view_source.contains(QStringLiteral("Show Patch Bounds")) ||
      !mask_inspector_source.contains(QStringLiteral("Show Patch Bounds"))) {
    return fail("inpaint patch bounds diagnostics should exist only behind the developer/debug availability gate");
  }
  if (!mask_inspector_source.contains(QStringLiteral("activeMaskColorRangeSampleButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskColorRangeSampleCountLabel")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskColorRangeRemoveSampleButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskColorSamples")) ||
      !studio_workspace_source.contains(QStringLiteral("maskColorRangeSampling")) ||
      !studio_workspace_source.contains(QStringLiteral("sampleActiveColorRangeAt"))) {
    return fail("color range mask UI should expose persistent eyedropper samples and remove controls");
  }
  if (!mask_inspector_source.contains(QStringLiteral("activeMaskLuminanceRangeSampleButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskLuminanceRangeSampleCountLabel")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskLuminanceRangeRemoveSampleButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskLuminanceSamples")) ||
      !studio_workspace_source.contains(QStringLiteral("maskLuminanceRangeSampling")) ||
      !studio_workspace_source.contains(QStringLiteral("luminanceRangeSamplePatch")) ||
      !studio_workspace_source.contains(QStringLiteral("beginActiveLuminanceRangeSample")) ||
      !studio_workspace_source.contains(QStringLiteral("commitActiveLuminanceRangeSample"))) {
    return fail("luminance range mask UI should expose drag patch sampling and remove controls");
  }
  if (!mask_inspector_source.contains(QStringLiteral("activeMaskDepthRangeSampleButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskDepthRangeSampleCountLabel")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskDepthRangeRemoveSampleButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskDepthSamplingDisabledReason")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskDepthSamplingAvailable")) ||
      !studio_workspace_source.contains(QStringLiteral("maskDepthRangeSampling")) ||
      !studio_workspace_source.contains(QStringLiteral("sampleActiveDepthRangeAt"))) {
    return fail("depth range mask UI should expose depth-gated sampling and disabled reason copy");
  }
  if (!mask_inspector_source.contains(QStringLiteral("generatedMaskCleanupSummary")) ||
      !mask_inspector_source.contains(QStringLiteral("generatedMaskHoleFillThresholdSlider")) ||
      !mask_inspector_source.contains(QStringLiteral("activeGeneratedMaskHoleFillThreshold")) ||
      !mask_inspector_source.contains(QStringLiteral("generatedMaskIslandRemovalThresholdSlider")) ||
      !mask_inspector_source.contains(QStringLiteral("activeGeneratedMaskIslandRemovalThreshold"))) {
    return fail("generated mask UI should expose hole-fill and island-removal cleanup thresholds");
  }
  if (!mask_inspector_source.contains(QStringLiteral("maskEdgeRefinePreview")) ||
      !mask_inspector_source.contains(QStringLiteral("maskEdgePreviewShiftMarker")) ||
      !mask_inspector_source.contains(QStringLiteral("maskEdgePreviewContrastBand")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskShiftEdgeSlider")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskContrastEdgeSlider")) ||
      !mask_inspector_source.contains(QStringLiteral("maskInspectorShowOriginalRefinementButton")) ||
      !mask_inspector_source.contains(QStringLiteral("maskInspectorShowRefinedRefinementButton")) ||
      !mask_inspector_source.contains(QStringLiteral("maskInspectorHighQualityRefinementPreviewButton")) ||
      !mask_inspector_source.contains(QStringLiteral("maskInspectorFullResolutionPreviewButton")) ||
      !mask_inspector_source.contains(QStringLiteral("maskPreviewRefinementEdgeButton")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskPreviewRefinementEdgeButton\"")) ||
      !mask_inspector_source.contains(QStringLiteral("visible: false")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskRefinementPreviewMode = \"original\"")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskRefinementPreviewMode = \"refined\"")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskRefinementHighQualityPreview")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.maskFullResolutionPreview")) ||
      !mask_inspector_source.contains(QStringLiteral("function maskPreviewQualityLabel")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.activeMaskPreviewQualityTier")) ||
      !studio_workspace_source.contains(QStringLiteral("function activeMaskPreviewQualityLabel()")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.activeMaskPreviewQualityTier")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskEdgeSection\"")) ||
      !mask_inspector_source.contains(QStringLiteral("advanced refine changes active")) ||
      !mask_inspector_source.contains(QStringLiteral("Advanced refinement is collapsed until needed.")) ||
      mask_inspector_source.contains(QStringLiteral("expanded: editRoot.isAiMaskKind(editorPreview.activeMaskKind)")) ||
      !mask_inspector_source.contains(QStringLiteral("label: \"Shift Edge\"")) ||
      !mask_inspector_source.contains(QStringLiteral("label: \"Contrast Edge\"")) ||
      !mask_view_source.contains(QStringLiteral("function activeRefineEffectCount()")) ||
      !mask_view_source.contains(QStringLiteral("Fine Tune · ")) ||
      !mask_view_source.contains(QStringLiteral("advanced refine changes active")) ||
      !mask_view_source.contains(QStringLiteral("Advanced refinement is collapsed until needed.")) ||
      !mask_view_source.contains(QStringLiteral("maskViewShowOriginalRefinementButton")) ||
      !mask_view_source.contains(QStringLiteral("maskViewShowRefinedRefinementButton")) ||
      !mask_view_source.contains(QStringLiteral("maskViewHighQualityRefinementPreviewButton")) ||
      !mask_view_source.contains(QStringLiteral("maskViewRefinementEdgePreviewButton")) ||
      mask_view_source.contains(QStringLiteral("editorPreview.maskPreviewMode = \"refinement-edge\"")) ||
      !mask_view_source.contains(QStringLiteral("editorPreview.maskRefinementPreviewMode = \"original\"")) ||
      !mask_view_source.contains(QStringLiteral("editorPreview.maskRefinementPreviewMode = \"refined\"")) ||
      !mask_view_source.contains(QStringLiteral("editorPreview.maskRefinementHighQualityPreview")) ||
      !mask_view_source.contains(QStringLiteral("label: \"Shift Edge\"")) ||
      !mask_view_source.contains(QStringLiteral("label: \"Contrast Edge\"")) ||
      mask_view_source.contains(QStringLiteral("label: \"Expand / Contract\""))) {
    return fail("selection refinement UI should expose separate Shift Edge and Contrast Edge controls, original/refined preview, and a visual edge preview");
  }
  if (!mask_inspector_source.contains(QStringLiteral("label: \"Pressure Size\"")) ||
      !mask_inspector_source.contains(QStringLiteral("label: \"Pressure Flow\"")) ||
      !mask_inspector_source.contains(QStringLiteral("label: \"Pressure Opacity\"")) ||
      !mask_view_source.contains(QStringLiteral("label: \"Pressure Size\"")) ||
      !mask_view_source.contains(QStringLiteral("label: \"Pressure Flow\"")) ||
      !mask_view_source.contains(QStringLiteral("label: \"Pressure Opacity\""))) {
    return fail("brush mask UI should expose tablet pressure response sliders for size, flow, and opacity");
  }
  if (!mask_inspector_source.contains(QStringLiteral("Sample First Dab")) ||
      !mask_inspector_source.contains(QStringLiteral("Sample Each Dab")) ||
      !mask_view_source.contains(QStringLiteral("Sample First Dab")) ||
      !mask_view_source.contains(QStringLiteral("Sample Each Dab"))) {
    return fail("brush auto-mask UI should expose first-dab and continuous center sampling modes");
  }
  if (!mask_inspector_source.contains(QStringLiteral("maskSourceDragHandler")) ||
      !mask_inspector_source.contains(QStringLiteral("maskSourceReorderDropArea")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.moveMaskSource")) ||
      !mask_view_source.contains(QStringLiteral("maskSourceDragHandler")) ||
      !mask_view_source.contains(QStringLiteral("maskSourceReorderDropArea")) ||
      !mask_view_source.contains(QStringLiteral("editorPreview.moveMaskSource"))) {
    return fail("selection source lists should expose drag-to-reorder and cross-set move affordances");
  }
  if (!mask_view_source.contains(QStringLiteral("beginAlphaImageMaskImport")) ||
      !mask_view_source.contains(QStringLiteral("alphaMaskImportPreviewPanel")) ||
      !mask_view_source.contains(QStringLiteral("alphaMaskImportInvertCheck")) ||
      !mask_view_source.contains(QStringLiteral("alphaMaskImportThresholdSlider")) ||
      !mask_view_source.contains(QStringLiteral("alphaMaskImportResampleModeCombo")) ||
      !mask_view_source.contains(QStringLiteral("ToneComboBox {\n                objectName: \"alphaMaskImportResampleModeCombo\"")) ||
      !mask_view_source.contains(QStringLiteral("onActivated: editorPreview.alphaMaskImportResampleMode = currentText")) ||
      QRegularExpression(QStringLiteral("\\n\\s*ComboBox\\s*\\{")).match(mask_view_source).hasMatch() ||
      !mask_view_source.contains(QStringLiteral("alphaMaskImportCommitButton")) ||
      !mask_view_source.contains(QStringLiteral("commitAlphaImageMaskImport"))) {
    return fail("alpha image mask UI should preview invert, threshold, and resample settings with polished shared controls before commit");
  }
  if (!mask_view_source.contains(QStringLiteral("Sky (Heuristic)")) ||
      !mask_view_source.contains(QStringLiteral("Background (Subject Invert)")) ||
      !mask_view_source.contains(QStringLiteral("People (Unsupported)")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedPeopleUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedFaceUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedHairUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedClothingUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedFacialHairUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedLipsUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedTeethUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedEyesUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedSkinUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedWaterUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedVegetationUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedMountainsUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedGroundUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedArchitectureUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("maskGeneratedSnowUnsupportedButton")) ||
      !mask_view_source.contains(QStringLiteral("visible: false")) ||
      !mask_view_source.contains(QStringLiteral("not semantic sky segmentation")) ||
      !mask_view_source.contains(QStringLiteral("not a separate background model")) ||
      !mask_view_source.contains(QStringLiteral("person-part matte")) ||
      !mask_view_source.contains(QStringLiteral("real landscape semantic matte"))) {
    return fail("generated mask tools should label heuristic/derived paths and hide unsupported semantic masks");
  }
  if (!mask_view_source.contains(QStringLiteral("editorPreview.beginSmartSelectStroke(")) ||
      !mask_view_source.contains(QStringLiteral("editorPreview.updateSmartSelectStroke(")) ||
      !mask_view_source.contains(QStringLiteral("editorPreview.commitSmartSelectStroke(objectPickMode, objectPickCreateNew)")) ||
      !mask_view_source.contains(QStringLiteral("editorPreview.smartSelectStrokeActive")) ||
      mask_view_source.contains(QStringLiteral("property var smartSelectStrokePoints")) ||
      mask_view_source.contains(QStringLiteral("function smartSelectStrokePromptPayload")) ||
      mask_view_source.contains(QStringLiteral("scheduleSmartSelectPreviewPath(smartSelectStrokePromptPayload())")) ||
      mask_view_source.contains(QStringLiteral("pickMaskPath(smartSelectStrokePromptPayload()"))) {
    return fail("object-drag smart selection strokes should use controller-backed stroke state instead of QML-owned prompt payloads");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewAcceptButton")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewRejectButton")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("objectName: \"smartSelectPreviewCommitControls\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("previewCommitContract: \"preview-only|accept-reject|required-before-layer:v1\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewPreviousButton")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewNextButton")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectCandidateHudLabel")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectCandidateHudText")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("acceptSmartSelectPreview")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("rejectSmartSelectPreview"))) {
    return fail("smart selection candidate previews should expose cycle, accept, and reject controls");
  }
  if (!studio_workspace_source.contains(QStringLiteral("root.objectPickArmed ||")) ||
      !studio_workspace_source.contains(QStringLiteral("property bool objectPickArmed: false")) ||
      !studio_workspace_source.contains(QStringLiteral("property string objectPickMode: \"selection\"")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"Object\"")) ||
      !studio_workspace_source.contains(QStringLiteral("\"Hover or drag / \" + root.maskOperationLabel(root.objectPickMode)")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.acceptSmartSelectPreview(root.objectPickMode, root.objectPickCreateNew)")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.rejectSmartSelectPreview()"))) {
    return fail("object selection should use the shared active tool chip with Done and Cancel routes");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewOutline")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("candidateOutlineRenderContract: \"smart-selection-hover|candidate-outline|precommit:v1\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("candidatePreviewPipelineSignature: editorPreview.smartSelectPreviewPipelineSignature")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartPreviewRasterContract: \"disabled|outline-only-candidate|no-speckled-matte:v1\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("id: smartPreviewMatte")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("visible: false")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("candidateOutlineVisible: editRoot.objectPickArmed")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("visible: candidateOutlineVisible"))) {
    return fail("smart selection hover should draw only a clean pre-commit candidate outline");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("smartSelectHoverPrewarmContract: \"pointer-pause|jitter-gated|cancel-stale|request-serial:v2\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewPauseMs: 180")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function smartSelectPreviewPointNear(")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("property int smartSelectPreviewRequestSerial: 0")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("property int pendingSmartSelectPreviewSerial: 0")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("root.pendingSmartSelectPreviewSerial !== root.smartSelectPreviewRequestSerial")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("root.smartSelectPreviewRequestSerial += 1")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("root.pendingSmartSelectPreviewSerial = root.smartSelectPreviewRequestSerial")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectPreviewPauseTimer.restart()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.scheduleSmartSelectPreview(root.pendingSmartSelectPreviewX")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("root.lastSmartSelectPreviewMs"))) {
    return fail("smart selection hover previews should wait for pointer pause before they reach the renderer");
  }
  trace_smoke_progress("smart selection hover debounce source checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_SMART_SELECTION_HOVER_SOURCE")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }
  if (!editor_preview_controller_source.contains(QStringLiteral("pending_interactive_preempts_full")) ||
      !editor_preview_controller_source.contains(QStringLiteral("preview.render.interactive_preempted_full")) ||
      !editor_preview_controller_source.contains(QStringLiteral("pending_interactive_request_.render_priority < pending_full_request_.render_priority")) ||
      !editor_preview_controller_source.contains(QStringLiteral("startRenderStage(false, overlay_request)"))) {
    return fail("visible overlay renders should preempt pending background full-resolution renders");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("handleSmartSelectPromptModifier")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("Qt.ShiftModifier")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("Qt.AltModifier")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("refineSmartSelectPreviewWithPrompt")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("smartSelectModifierPromptHandled")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.beginSmartSelectStroke(point.x, point.y, previewItem.width, previewItem.height)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.updateSmartSelectStroke(point.x, point.y, previewItem.width, previewItem.height)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.commitSmartSelectStroke(")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("property var smartSelectStrokePoints")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("function smartSelectStrokePromptPayload")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("scheduleSmartSelectPreviewPath(smartSelectStrokePromptPayload())"))) {
    return fail("smart selection overlay should support Shift positive prompts, Option negative prompts, and controller-backed drag prompts");
  }
  QFile mask_glossary_doc(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath(QStringLiteral("docs/masking-glossary.md")));
  if (!mask_glossary_doc.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect the masking glossary");
  }
  const auto mask_glossary_source = QString::fromUtf8(mask_glossary_doc.readAll());
  const QStringList required_mask_glossary_terms = {
      QStringLiteral("| Selection |"),
      QStringLiteral("| Retouch |"),
      QStringLiteral("| Retouch patch |"),
      QStringLiteral("| Mask set |"),
      QStringLiteral("| Source |"),
      QStringLiteral("| Operation |"),
      QStringLiteral("| Overlay |"),
      QStringLiteral("| Generated mask |"),
      QStringLiteral("| Heuristic mask |"),
      QStringLiteral("| Stale generated mask |"),
  };
  for (const auto& term : required_mask_glossary_terms) {
    if (!mask_glossary_source.contains(term)) {
      return fail("masking glossary should define the shared Selection/Retouch vocabulary");
    }
  }
  QFile generated_mask_corpus_file(
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("tests/fixtures/generated_mask_benchmark_corpus.json")));
  if (!generated_mask_corpus_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return fail("desktop shell smoke should be able to inspect the generated-mask benchmark corpus");
  }
  const auto generated_mask_corpus_document =
      QJsonDocument::fromJson(generated_mask_corpus_file.readAll());
  if (!generated_mask_corpus_document.isObject()) {
    return fail("generated-mask benchmark corpus should be a JSON object");
  }
  const auto generated_mask_corpus = generated_mask_corpus_document.object();
  if (generated_mask_corpus.value(QStringLiteral("schemaVersion")).toInt() < 1 ||
      generated_mask_corpus.value(QStringLiteral("name")).toString() !=
          QStringLiteral("generated-mask-benchmark-synthetic-v1")) {
    return fail("generated-mask benchmark corpus should expose a stable schema and name");
  }
  const QSet<QString> required_generated_mask_categories = {
      QStringLiteral("portrait"),
      QStringLiteral("product"),
      QStringLiteral("landscape"),
      QStringLiteral("low_contrast"),
      QStringLiteral("busy_background"),
      QStringLiteral("transparent_object"),
  };
  QSet<QString> manifest_required_categories;
  for (const auto& value : generated_mask_corpus.value(QStringLiteral("requiredCategories")).toArray()) {
    manifest_required_categories.insert(value.toString());
  }
  if (manifest_required_categories != required_generated_mask_categories) {
    return fail("generated-mask benchmark corpus should declare every required benchmark category");
  }
  QSet<QString> generated_mask_case_categories;
  const auto generated_mask_cases = generated_mask_corpus.value(QStringLiteral("cases")).toArray();
  if (generated_mask_cases.size() < required_generated_mask_categories.size()) {
    return fail("generated-mask benchmark corpus should include at least one case per required category");
  }
  for (const auto& value : generated_mask_cases) {
    const auto test_case = value.toObject();
    const auto category = test_case.value(QStringLiteral("category")).toString();
    generated_mask_case_categories.insert(category);
    const auto quality_checks = test_case.value(QStringLiteral("qualityChecks")).toObject();
    if (test_case.value(QStringLiteral("id")).toString().trimmed().isEmpty() ||
        !required_generated_mask_categories.contains(category) ||
        test_case.value(QStringLiteral("source")).toString() != QStringLiteral("synthetic") ||
        test_case.value(QStringLiteral("width")).toInt() < 384 ||
        test_case.value(QStringLiteral("height")).toInt() < 384 ||
        test_case.value(QStringLiteral("generatedKind")).toString().trimmed().isEmpty() ||
        test_case.value(QStringLiteral("promptType")).toString().trimmed().isEmpty() ||
        test_case.value(QStringLiteral("stressors")).toArray().isEmpty() ||
        test_case.value(QStringLiteral("expectedFeatures")).toArray().isEmpty() ||
        quality_checks.value(QStringLiteral("minSelectedFraction")).toDouble() <= 0.0 ||
        quality_checks.value(QStringLiteral("maxSelectedFraction")).toDouble() <=
            quality_checks.value(QStringLiteral("minSelectedFraction")).toDouble() ||
        !quality_checks.value(QStringLiteral("requiresBoundaryPixels")).toBool() ||
        !quality_checks.value(QStringLiteral("requiresInteriorPixels")).toBool() ||
        !quality_checks.value(QStringLiteral("requiresExteriorPixels")).toBool()) {
      return fail("generated-mask benchmark corpus cases should be complete and quality-checkable");
    }
  }
  if (generated_mask_case_categories != required_generated_mask_categories) {
    return fail("generated-mask benchmark corpus should cover portrait, product, landscape, low-contrast, busy-background, and transparent-object cases");
  }
  const QStringList forbidden_selection_copy = {
      QStringLiteral("Paint a manual mask source."),
      QStringLiteral("Create a linear gradient mask source."),
      QStringLiteral("Create a radial mask source."),
      QStringLiteral("Retry the last failed mask action."),
      QStringLiteral("Paint into the active mask."),
      QStringLiteral("Erase from the active mask."),
      QStringLiteral("Layers / Masks"),
      QStringLiteral("Base / Masks"),
      QStringLiteral("Mask Exposure"),
      QStringLiteral("Mask Temp"),
      QStringLiteral("Mask Stack"),
      QStringLiteral("Mask Sets"),
      QStringLiteral("Mask Properties"),
      QStringLiteral("Mask set name"),
      QStringLiteral("No mask sets yet"),
      QStringLiteral("No selection sets yet"),
      QStringLiteral("Selection Stack"),
      QStringLiteral("Selection Sets"),
      QStringLiteral("Exit Mask"),
      QStringLiteral("Auto Mask"),
      QStringLiteral("Freehand Mask"),
      QStringLiteral("Auto-Mask Range"),
  };
  const auto combined_selection_ui_source =
      studio_workspace_source + QLatin1Char('\n') + mask_view_source + QLatin1Char('\n') +
      mask_inspector_source;
  for (const auto& phrase : forbidden_selection_copy) {
    if (combined_selection_ui_source.contains(phrase)) {
      return fail("selection and retouch UI should not use legacy mask vocabulary for local workflows");
    }
  }
  const auto mask_view_active_controls_index =
      mask_view_source.indexOf(QStringLiteral("objectName: \"activeMaskControlsSection\""));
  const auto mask_view_adjustment_index =
      mask_view_source.indexOf(QStringLiteral("objectName: \"activeMaskAdjustmentSection\""));
  const auto mask_inspector_active_controls_index =
      mask_inspector_source.indexOf(QStringLiteral("objectName: \"activeMaskControlsSection\""));
  const auto mask_inspector_adjustment_index =
      mask_inspector_source.indexOf(QStringLiteral("objectName: \"activeMaskAdjustmentSection\""));
  if (mask_view_active_controls_index < 0 || mask_view_adjustment_index < 0 ||
      mask_inspector_active_controls_index < 0 || mask_inspector_adjustment_index < 0 ||
      mask_view_active_controls_index > mask_view_adjustment_index ||
      mask_inspector_active_controls_index > mask_inspector_adjustment_index) {
    return fail("mask active controls should stay above local adjustment sliders");
  }
  if (!studio_workspace_source.contains(QStringLiteral("Paint an additive brush selection.")) ||
      !studio_workspace_source.contains(QStringLiteral("Paint a subtractive brush selection.")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskShortcutContextActive(requireActiveMask)")) ||
      !studio_workspace_source.contains(QStringLiteral("function createMaskFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function selectMaskMoveFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function startMaskLassoFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function startMaskPolygonFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function setMaskOperationFromShortcut(operationName)")) ||
      !studio_workspace_source.contains(QStringLiteral("function shortcutTextInputActive()")) ||
      !studio_workspace_source.contains(QStringLiteral("function brushShortcutContextActive()")) ||
      !studio_workspace_source.contains(QStringLiteral("function invertActiveMaskFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function setTemporaryBrushEraseFromShortcut(active)")) ||
      !studio_workspace_source.contains(QStringLiteral("function toggleActiveMaskOverlayFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function toggleMaskOutlineFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function switchLayerTargetFromShortcut(delta)")) ||
      !studio_workspace_source.contains(QStringLiteral("function switchMaskTargetFromShortcut(delta)")) ||
      !studio_workspace_source.contains(QStringLiteral("function returnToBaseImageFromShortcut()")) ||
      !studio_workspace_source.contains(
          QStringLiteral("function setMaskPreviewTemporarilyHiddenFromShortcut(hidden)")) ||
      !studio_workspace_source.contains(QStringLiteral("function deleteActiveMaskFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function renameActiveMaskFromShortcut()")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskShortcutSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskCreateShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskOperationAddShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskOperationSubtractShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskOperationIntersectShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskInvertShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskOverlayShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskOutlineShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"layerTargetNextShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"layerTargetPreviousShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskTargetNextShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskTargetPreviousShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"baseImageTargetShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskSelectMoveShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskLassoShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskPolygonShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskSourcePolygonButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("Keys.onPressed: function(event)")) ||
      !studio_workspace_source.contains(QStringLiteral("Keys.onReleased: function(event)")) ||
      !studio_workspace_source.contains(QStringLiteral("Qt.Key_H")) ||
      !studio_workspace_source.contains(QStringLiteral("Qt.Key_X")) ||
      !studio_workspace_source.contains(QStringLiteral("temporaryBrushEraseActive")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.maskPreviewTemporarilyHidden")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskDeleteShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskRenameShortcut\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeMaskRenameDialog\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeMaskRenameShortcutField\"")) ||
      !studio_workspace_source.contains(QStringLiteral("select:V|brush:B|erase:E|lasso:L|polygon:P|")) ||
      !studio_workspace_source.contains(QStringLiteral("create:Ctrl+Shift+M|add:Ctrl+Shift+A|subtract:Ctrl+Shift+S|")) ||
      !studio_workspace_source.contains(QStringLiteral("outline:Ctrl+Shift+U|layer-target:Ctrl+Shift+L|layer-target-prev:Ctrl+Alt+Shift+L|")) ||
      !studio_workspace_source.contains(QStringLiteral("mask-target:Ctrl+Shift+K|mask-target-prev:Ctrl+Alt+Shift+K|")) ||
      !studio_workspace_source.contains(QStringLiteral("base-image:Ctrl+Shift+B|temp-erase:X-hold|temporary-hide:H-hold|")) ||
      !studio_workspace_source.contains(QStringLiteral("Layers / Selections")) ||
      !mask_view_source.contains(QStringLiteral("title: \"Masks\"")) ||
      !mask_view_source.contains(QStringLiteral("Build each mask from ordered Components")) ||
      !mask_view_source.contains(QStringLiteral("createNewMaskButton")) ||
      !mask_view_source.contains(QStringLiteral("text: \"Create New Mask\"")) ||
      !mask_view_source.contains(QStringLiteral("createNewMaskChooser")) ||
      !mask_view_source.contains(QStringLiteral("createMaskSmartButton")) ||
      !mask_view_source.contains(QStringLiteral("createMaskBrushAddButton")) ||
      !mask_view_source.contains(QStringLiteral("createMaskPolygonButton")) ||
      !mask_view_source.contains(QStringLiteral("createMaskAlphaImageButton")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskEmptyStateGuidance\"")) ||
      !mask_view_source.contains(QStringLiteral("visible: editorPreview.maskSetCount === 0")) ||
      !mask_view_source.contains(QStringLiteral("No masks yet. Create a mask with Smart Select")) ||
      !mask_view_source.contains(QStringLiteral("maskSetThumbnail")) ||
      !mask_view_source.contains(QStringLiteral("maskSourceThumbnail")) ||
      !mask_view_source.contains(QStringLiteral("modelData.thumbnailSource")) ||
      !mask_view_source.contains(QStringLiteral("maskComponentsHeaderLabel")) ||
      !mask_view_source.contains(QStringLiteral("text: \"Components\"")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskOperationAddButton")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskOperationSubtractButton")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskOperationIntersectButton")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskOperationReplaceButton")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskInvertButton")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskHideButton")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskLockButton")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskRenameField")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskDuplicateButton")) ||
      !mask_view_source.contains(QStringLiteral("activeMaskDeleteButton")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskControlsSection\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskAdjustmentSection\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustToneGroup\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustColorGroup\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustPresenceGroup\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustDetailGroup\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustEffectsGroup\"")) ||
      !mask_view_source.contains(QStringLiteral("maskCompareAdjustmentGroups")) ||
      !mask_view_source.contains(QStringLiteral("toggleMaskCompareGroup")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustToneBeforeAfterButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustColorBeforeAfterButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustPresenceBeforeAfterButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustDetailBeforeAfterButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskAdjustEffectsBeforeAfterButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSearchField\"")) ||
      !mask_view_source.contains(QStringLiteral("placeholderText: \"Search masks, tools, controls\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSearchResultsPanel\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSearchResultButton\"")) ||
      !mask_view_source.contains(QStringLiteral("maskSearchToolItems")) ||
      !mask_view_source.contains(QStringLiteral("maskSearchAdjustmentItems")) ||
      !mask_view_source.contains(QStringLiteral("activateMaskSearchResult")) ||
      !mask_view_source.contains(QStringLiteral("MaskSearchModel {")) ||
      !mask_view_source.contains(QStringLiteral("filter(maskSearchModel.sourceMatches)")) ||
      !mask_view_source.contains(QStringLiteral("function activeMaskEditingContext()")) ||
      !mask_view_source.contains(QStringLiteral("function activeMaskEditingContextSignature()")) ||
      !mask_view_source.contains(QStringLiteral("function maskContextVisible(contextName)")) ||
      !mask_view_source.contains(QStringLiteral("function openCreateMaskContext()")) ||
      !mask_view_source.contains(QStringLiteral("function activeMaskCompactHeaderSignature()")) ||
      !mask_view_source.contains(QStringLiteral("function activeMaskHeaderTarget()")) ||
      !mask_view_source.contains(QStringLiteral("MaskRowActions {")) ||
      !mask_view_source.contains(QStringLiteral("id: maskRowActions")) ||
      !mask_view_source.contains(QStringLiteral("maskRowActions.rename(modelData.flatIndex)")) ||
      !mask_view_source.contains(QStringLiteral("maskRowActions.toggleEnabled(modelData.flatIndex, modelData.enabled)")) ||
      !mask_view_source.contains(QStringLiteral("maskRowActions.applyRetouch(modelData.flatIndex)")) ||
      !mask_view_source.contains(QStringLiteral("maskRowActions.cancelRetouch(modelData.flatIndex)")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskActiveContextHeader\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskActiveContextLabel\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskActiveContextSignature\"")) ||
      !mask_view_source.contains(QStringLiteral("AppMenu {\n                                                    id: maskSourceContextMenu")) ||
      !mask_view_source.contains(QStringLiteral("AppMenu {\n                                        id: retouchRowContextMenu")) ||
      !mask_view_source.contains(QStringLiteral("AppMenuItem {")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextMenu\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextSelectItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextRenameItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextEnableItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextSoloItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextLockItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextMoveUpItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextMoveDownItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextDuplicateItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextDeleteItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"retouchRowContextMenu\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"retouchRowContextApplyItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"retouchRowContextCancelItem\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSetFocusRow\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceFocusRow\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"retouchFocusRow\"")) ||
      !mask_view_source.contains(QStringLiteral("activeFocusOnTab: true")) ||
      !mask_view_source.contains(QStringLiteral("Keys.onReturnPressed: editorPreview.activeMaskIndex = modelData.flatIndex")) ||
      !mask_view_source.contains(QStringLiteral("Keys.onSpacePressed: editorPreview.activeMaskIndex = modelData.flatIndex")) ||
      !mask_view_source.contains(QStringLiteral("border.color: activeFocus")) ||
      !mask_view_source.contains(QStringLiteral("acceptedButtons: Qt.LeftButton | Qt.RightButton")) ||
      !mask_view_source.contains(QStringLiteral("maskSourceContextMenu.popup()")) ||
      !mask_view_source.contains(QStringLiteral("retouchRowContextMenu.popup()")) ||
      QRegularExpression(QStringLiteral("\\n\\s*Menu\\s*\\{")).match(mask_view_source).hasMatch() ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskCompactHeader\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderNameLabel\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderKindLabel\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderOperationLabel\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderTargetLabel\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderOpacityLabel\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderEnabledButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderSoloButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderLockButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskHeaderSignature\"")) ||
      !mask_view_source.contains(QStringLiteral("|target:\" + root.activeMaskHeaderTarget()")) ||
      !mask_view_source.contains(QStringLiteral("|solo:\" + editorPreview.activeMaskSolo.toString()")) ||
      !mask_view_source.contains(QStringLiteral("|opacity:\" + root.percentText(editorPreview.activeMaskOpacity)")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskDensityOpacityHelp\"")) ||
      !mask_view_source.contains(QStringLiteral("Density sets maximum mask strength. Opacity controls blend amount.")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskOpacitySlider\"")) ||
      !mask_view_source.contains(QStringLiteral("label: \"Mask Opacity\"")) ||
      !mask_view_source.contains(QStringLiteral("valueText: root.percentText(editorPreview.activeMaskOpacity)")) ||
      !mask_view_source.contains(QStringLiteral("onValueDragged: function(value) { editorPreview.activeMaskOpacity = value / 100 }")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"activeMaskDensitySlider\"")) ||
      !mask_view_source.contains(QStringLiteral("label: \"Mask Density\"")) ||
      !mask_view_source.contains(QStringLiteral("valueText: root.percentText(editorPreview.activeMaskDensity)")) ||
      !mask_view_source.contains(QStringLiteral("onValueDragged: function(value) { editorPreview.activeMaskDensity = value / 100 }")) ||
      !mask_view_source.contains(QStringLiteral("readonly property int stableInspectorWidth: 410")) ||
      !mask_view_source.contains(QStringLiteral("readonly property int minimumCanvasWidth: 360")) ||
      !mask_view_source.contains(QStringLiteral("readonly property int minimumCanvasHeight: 320")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskWorkspaceStableSplit\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskCanvasPane\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskCanvasViewport\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskViewPointerHandlerLayer\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskViewToolHoverHandler\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskViewSmartSelectPointHandler\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskViewLassoPointHandler\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskViewPolygonTapHandler\"")) ||
      !mask_view_source.contains(QStringLiteral("onDoubleTapped:")) ||
      !mask_view_source.contains(QStringLiteral("HoverHandler {")) ||
      !mask_view_source.contains(QStringLiteral("PointHandler {")) ||
      !mask_view_source.contains(QStringLiteral("TapHandler {")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskInspectorPane\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskInspectorScrollView\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskInspectorColumn\"")) ||
      !mask_view_source.contains(QStringLiteral("Layout.minimumWidth: root.minimumCanvasWidth")) ||
      !mask_view_source.contains(QStringLiteral("Layout.preferredWidth: root.stableInspectorWidth")) ||
      !mask_view_source.contains(QStringLiteral("Layout.maximumWidth: root.stableInspectorWidth")) ||
      !mask_view_source.contains(QStringLiteral("contentWidth: availableWidth")) ||
      !mask_view_source.contains(QStringLiteral("contentHeight: maskInspectorColumn.implicitHeight")) ||
      !mask_view_source.contains(QStringLiteral("root.objectPickArmed || editorPreview.smartSelectStrokeActive || editorPreview.smartSelectPreviewActive")) ||
      !mask_view_source.contains(QStringLiteral("|smart-select:\" + (context === \"smart-select\").toString()")) ||
      !mask_view_source.contains(QStringLiteral("visible: root.maskContextVisible(\"create\")")) ||
      !mask_view_source.contains(QStringLiteral("visible: root.maskContextVisible(\"brush\")")) ||
      !mask_view_source.contains(QStringLiteral("visible: root.maskContextVisible(\"polygon\")")) ||
      !mask_view_source.contains(QStringLiteral("visible: root.maskContextVisible(\"refine\") && root.activeMaskSupportsSelectionRefine()")) ||
      !mask_view_source.contains(QStringLiteral("visible: root.maskContextVisible(\"refine\") && editorPreview.hasActiveMask")) ||
      !mask_view_source.contains(QStringLiteral("visible: root.maskActiveControlsContextVisible()")) ||
      !mask_view_source.contains(QStringLiteral("Active Selection")) ||
      !mask_inspector_source.contains(QStringLiteral("title: \"Masks\"")) ||
      !mask_inspector_source.contains(QStringLiteral("Each mask contains ordered Components")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorEmptyStateGuidance\"")) ||
      !mask_inspector_source.contains(QStringLiteral("visible: editorPreview.maskSetCount === 0")) ||
      !mask_inspector_source.contains(QStringLiteral("text: \"No masks yet\"")) ||
      !mask_inspector_source.contains(QStringLiteral("maskSetThumbnail")) ||
      !mask_inspector_source.contains(QStringLiteral("maskSourceThumbnail")) ||
      !mask_inspector_source.contains(QStringLiteral("modelData.thumbnailSource")) ||
      !mask_inspector_source.contains(QStringLiteral("maskComponentsHeaderLabel")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskOperationAddButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskOperationSubtractButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskOperationIntersectButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskOperationReplaceButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskInvertButton")) ||
      !mask_inspector_source.contains(QStringLiteral("ToneComboBox {\n                id: maskBlendModeCombo")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"activeMaskBlendModeCombo\"")) ||
      !mask_inspector_source.contains(QStringLiteral("onActivated: editorPreview.activeMaskBlendMode = model[index]")) ||
      QRegularExpression(QStringLiteral("\\n\\s*ComboBox\\s*\\{")).match(mask_inspector_source).hasMatch() ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskHideButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskLockButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskRenameField")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskDuplicateButton")) ||
      !mask_inspector_source.contains(QStringLiteral("activeMaskDeleteButton")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"activeMaskControlsSection\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"activeMaskAdjustmentSection\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustToneGroup\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustColorGroup\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustPresenceGroup\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustDetailGroup\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustEffectsGroup\"")) ||
      !mask_inspector_source.contains(QStringLiteral("maskCompareAdjustmentGroups")) ||
      !mask_inspector_source.contains(QStringLiteral("toggleMaskCompareGroup")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustToneBeforeAfterButton\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustColorBeforeAfterButton\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustPresenceBeforeAfterButton\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustDetailBeforeAfterButton\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskAdjustEffectsBeforeAfterButton\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskSearchField\"")) ||
      !mask_inspector_source.contains(QStringLiteral("placeholderText: \"Search masks, tools, controls\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskSearchResultsPanel\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskSearchResultButton\"")) ||
      !mask_inspector_source.contains(QStringLiteral("maskSearchToolItems")) ||
      !mask_inspector_source.contains(QStringLiteral("maskSearchAdjustmentItems")) ||
      !mask_inspector_source.contains(QStringLiteral("activateMaskSearchResult")) ||
      !mask_inspector_source.contains(QStringLiteral("MaskSearchModel {")) ||
      !mask_inspector_source.contains(QStringLiteral("filter(maskSearchModel.sourceMatches)")) ||
      !mask_search_model_source.contains(QStringLiteral("function searchText()")) ||
      !mask_search_model_source.contains(QStringLiteral("function matches(text)")) ||
      !mask_search_model_source.contains(QStringLiteral("function sourceMatches(source)")) ||
      !mask_search_model_source.contains(QStringLiteral("function setMatches(maskSet)")) ||
      !mask_search_model_source.contains(QStringLiteral("function results()")) ||
      !mask_inspector_source.contains(QStringLiteral("MaskRowActions {")) ||
      !mask_inspector_source.contains(QStringLiteral("id: maskRowActions")) ||
      !mask_view_source.contains(QStringLiteral("MaskRowActions {")) ||
      !mask_row_actions_source.contains(QStringLiteral("function rename(flatIndex)")) ||
      !mask_row_actions_source.contains(QStringLiteral("function toggleEnabled(flatIndex, enabled)")) ||
      !mask_row_actions_source.contains(QStringLiteral("function toggleSolo(flatIndex, solo)")) ||
      !mask_row_actions_source.contains(QStringLiteral("function toggleLocked(flatIndex, locked)")) ||
      !mask_row_actions_source.contains(QStringLiteral("function move(flatIndex, offset)")) ||
      !mask_row_actions_source.contains(QStringLiteral("function duplicate(flatIndex)")) ||
      !mask_row_actions_source.contains(QStringLiteral("function remove(flatIndex)")) ||
      !mask_row_actions_source.contains(QStringLiteral("function applyRetouch(flatIndex)")) ||
      !mask_row_actions_source.contains(QStringLiteral("function cancelRetouch(flatIndex)")) ||
      !mask_inspector_source.contains(QStringLiteral("AppMenu {\n                                    id: maskInspectorSourceContextMenu")) ||
      !mask_inspector_source.contains(QStringLiteral("AppMenu {\n                        id: maskInspectorRetouchContextMenu")) ||
      !mask_inspector_source.contains(QStringLiteral("AppMenuItem {")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextMenu\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextSelectItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextRenameItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextEnableItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextSoloItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextLockItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextMoveUpItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextMoveDownItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextDuplicateItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceContextDeleteItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorRetouchContextMenu\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorRetouchContextApplyItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorRetouchContextCancelItem\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSetFocusRow\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorSourceFocusRow\"")) ||
      !mask_inspector_source.contains(QStringLiteral("objectName: \"maskInspectorRetouchFocusRow\"")) ||
      !mask_inspector_source.contains(QStringLiteral("activeFocusOnTab: true")) ||
      !mask_inspector_source.contains(QStringLiteral("Keys.onReturnPressed: editorPreview.activeMaskIndex = modelData.flatIndex")) ||
      !mask_inspector_source.contains(QStringLiteral("Keys.onSpacePressed: editorPreview.activeMaskIndex = modelData.flatIndex")) ||
      !mask_inspector_source.contains(QStringLiteral("border.color: activeFocus")) ||
      !mask_inspector_source.contains(QStringLiteral("maskInspectorSourceContextMenu.popup()")) ||
      !mask_inspector_source.contains(QStringLiteral("maskInspectorRetouchContextMenu.popup()")) ||
      QRegularExpression(QStringLiteral("\\n\\s*Menu\\s*\\{")).match(mask_inspector_source).hasMatch() ||
       !mask_inspector_source.contains(QStringLiteral("label: \"Remove\"")) ||
       !mask_inspector_source.contains(QStringLiteral("Selection Properties")) ||
       !mask_inspector_source.contains(QStringLiteral("Edge-Aware Range"))) {
    return fail("selection and retouch UI should expose the revised selection vocabulary");
  }
  const auto studio_shortcuts = literal_shortcut_sequences(studio_workspace_source);
  if (!studio_shortcuts.contains(QStringLiteral("Ctrl+Shift+L")) ||
      !studio_shortcuts.contains(QStringLiteral("Ctrl+Alt+Shift+L")) ||
      !studio_shortcuts.contains(QStringLiteral("Ctrl+Shift+K")) ||
      !studio_shortcuts.contains(QStringLiteral("Ctrl+Alt+Shift+K")) ||
      !studio_shortcuts.contains(QStringLiteral("Ctrl+Shift+B"))) {
    return fail("Studio should retain the canonical layer and mask shortcut set");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function defaultCurvePoints()")) ||
      !studio_workspace_source.contains(QStringLiteral("function primaryCurveMode()")) ||
      !studio_workspace_source.contains(QStringLiteral("function primaryCurvePoints()")) ||
      !studio_workspace_source.contains(QStringLiteral("function primaryAdjustmentLayerIndex()")) ||
      !studio_workspace_source.contains(QStringLiteral("const selectedLayerIndex = root.activeAdjustmentLayerIndex()")) ||
      !studio_workspace_source.contains(QStringLiteral("return root.activeMaskAdjustmentLayerIndex()")) ||
      !studio_workspace_source.contains(QStringLiteral("const layerIndex = root.primaryAdjustmentLayerIndex()")) ||
      !studio_workspace_source.contains(QStringLiteral("if (layerIndex < 0)")) ||
      !studio_workspace_source.contains(QStringLiteral("return editorPreview.activeCurveMode")) ||
      !studio_workspace_source.contains(QStringLiteral("return editorPreview.activeCurvePoints")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"Neutral\"")) ||
      !studio_workspace_source.contains(QStringLiteral("return root.defaultCurvePoints()")) ||
      !studio_workspace_source.contains(QStringLiteral("if (!root.adjustmentControlCanWriteActiveTarget(\"base-image,adjustment-layer\"))")) ||
      !studio_workspace_source.contains(QStringLiteral("if (!root.globalAdjustmentWritesAllowed())")) ||
      !studio_workspace_source.contains(QStringLiteral("return editorPreview.setPhotoLayerCurvePoints(layerIndex, editorPreview.curveChannel, points)")) ||
      !studio_workspace_source.contains(QStringLiteral("function resetActiveAdjustmentLayerRecipe()")) ||
      !studio_workspace_source.contains(QStringLiteral("return editorPreview.resetPhotoLayerAdjustments(layerIndex)")) ||
      !studio_workspace_source.contains(QStringLiteral("key: \"paste-base-adjustments\", label: \"Paste Base Image Adjustments\"")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.pasteGlobalAdjustmentsToPhotoLayer(index)")) ||
      !studio_workspace_source.contains(QStringLiteral("key: \"preview-adjustments\"")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.setPhotoLayerAdjustmentsPreviewBypassed(index,"))) {
    return fail("Studio tone curve routing should distinguish Base Image curves from neutral active-layer curves and linked selection layers");
  }
  if (!studio_workspace_source.contains(QStringLiteral("target-aware-adjustment-command-router:v3")) ||
      !studio_workspace_source.contains(QStringLiteral("|scope-normalized:true|write-gated:true|curve-guarded:true|no-global-fallback:true")) ||
      !studio_workspace_source.contains(QStringLiteral("target-routing-matrix:v1")) ||
      !studio_workspace_source.contains(QStringLiteral("root.activeEditTargetScope() === \"mask\"")) ||
      !studio_workspace_source.contains(QStringLiteral("if (!editorPreview.addAdjustmentLayer(\"\"))")) ||
      !studio_workspace_source.contains(QStringLiteral("return root.setActivePhotoLayer(0)")) ||
      !studio_workspace_source.contains(QStringLiteral("const selectedLayer = root.photoLayerRows[selectedIndex] || ({})")) ||
      !studio_workspace_source.contains(QStringLiteral("String(selectedLayer.kind || \"\").trim().toLowerCase() === \"adjustment\""))) {
    return fail("new adjustment layers should normalize and select their real target so every color control routes like the curve editor");
  }
  if (!editor_preview_controller_source.contains(
          QStringLiteral("preview_failure_stage_.clear();\n  preview_failure_detail_.clear();\n  preview_failure_layer_ids_.clear();\n  preview_failure_mask_set_ids_.clear();\n  setStatus(QStringLiteral(\"Loading preview.\"));"))) {
    return fail("loading a new asset should clear stale preview failure text before QML can re-present it");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function activeEditTargetVisualSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("property var selectedEditTarget: ({ scope: \"base-image\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string selectedEditTargetContract: selectedEditTargetSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("function selectedEditTargetSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("function setSelectedEditTarget(scope, layerIndex, maskSet, sourceIndex)")) ||
      !studio_workspace_source.contains(QStringLiteral("function adjustmentControlDisabledReason(rawTargets)")) ||
      !studio_adjustment_slider_source.contains(QStringLiteral("studioAdjustmentSliderContract: \"studio-adjustment-slider:v2|shared-inspector-slider:true|wide-rail:true|fixed-value:true|fixed-reset:true|modified-marker:false|double-click-reset:true|coalesced-drag:true|target-aware:true|disabled-reason:true|no-elide:true\"")) ||
      !studio_adjustment_slider_source.contains(QStringLiteral("disabledReason: supportsActiveEditTarget ? \"\" : workspace.adjustmentControlDisabledReason(targetSupport)")) ||
      !studio_adjustment_slider_source.contains(QStringLiteral("onInteractionStarted: {")) ||
      !studio_adjustment_slider_source.contains(QStringLiteral("onInteractionFinished: {")) ||
      !studio_adjustment_slider_source.contains(QStringLiteral("previewController.beginCoalescedEdit()")) ||
      !studio_adjustment_slider_source.contains(QStringLiteral("previewController.endCoalescedEdit()")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"selected-target:v1|scope:\" + root.selectedEditTargetScope()")) ||
      !studio_workspace_source.contains(QStringLiteral("root.setSelectedEditTarget(\"base-image\", -1, ({}), -1)")) ||
      !studio_workspace_source.contains(QStringLiteral("root.setSelectedEditTarget(root.selectedLayerTargetScope(index), index, ({}), -1)")) ||
      !studio_workspace_source.contains(QStringLiteral("root.setSelectedEditTarget(showAdjustments && layerIndex >= 0 ? root.selectedLayerTargetScope(layerIndex) : \"mask\"")) ||
      !studio_workspace_source.contains(QStringLiteral("\"|selected-target:\" + root.selectedEditTargetContract")) ||
      !studio_workspace_source.contains(QStringLiteral("function activeMaskSetData()")) ||
      !studio_workspace_source.contains(QStringLiteral("function activeEditTargetThumbnailData()")) ||
      !studio_workspace_source.contains(QStringLiteral("function activeEditTargetBadgeText()")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeEditTargetAccentRail\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeEditTargetScopeBadge\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeEditTargetThumbnail\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeEditTargetThumbnailImage\"")) ||
      !studio_workspace_source.contains(QStringLiteral("|compact-copy:true")) ||
	      !studio_workspace_source.contains(QStringLiteral("|implementation-path:false")) ||
	      !studio_workspace_source.contains(QStringLiteral("|inspector-context:visible")) ||
	      !studio_workspace_source.contains(QStringLiteral("|max-visible-breadcrumbs:1")) ||
	      !studio_workspace_source.contains(QStringLiteral("active-target-context:v4|inspector-visible:true|breadcrumb-visible:true|status-visible:true|owner:inspector|queryable:true")) ||
	      !studio_workspace_source.contains(QStringLiteral("active-target-breadcrumbs:v2|inspector-visible:true|queryable:true|max-visible:1|duplicate-visible:false")) ||
	      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeEditTargetMultiPhotoChip\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property string routingMatrixContract: root.targetRoutingMatrixSignature()")) ||
	      !studio_workspace_source.contains(QStringLiteral("|rail:true|separation:hard")) ||
	      !studio_workspace_source.contains(QStringLiteral("function activeEditTargetToneName()")) ||
	      !studio_workspace_source.contains(QStringLiteral("|tone:\" + root.activeEditTargetToneName()")) ||
	      !studio_workspace_source.contains(QStringLiteral("|base-tone:cool")) ||
	      !studio_workspace_source.contains(QStringLiteral("|layer-tone:mint")) ||
	      !studio_workspace_source.contains(QStringLiteral("|selection-tone:periwinkle")) ||
	      !studio_workspace_source.contains(QStringLiteral("|photo-layer-tone:warm")) ||
	      !studio_workspace_source.contains(QStringLiteral("if (scope === \"mask\")")) ||
	      !studio_workspace_source.contains(QStringLiteral("return \"#8FA8FF\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("|motion:color-opacity-scale")) ||
	      !studio_workspace_source.contains(QStringLiteral("|motion-duration:\" + String(root.studioMotionFast)")) ||
	      !studio_workspace_source.contains(QStringLiteral("Behavior on border.color")) ||
	      !studio_workspace_source.contains(QStringLiteral("Behavior on scale")) ||
	      !studio_workspace_source.contains(QStringLiteral("|thumbnail:\" + String(scope !== \"base-image\")")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property string visualContract: root.activeEditTargetVisualSignature()")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property string toneName: root.activeEditTargetToneName()")) ||
	      !studio_workspace_source.contains(QStringLiteral("visible: inspectorContextVisible"))) {
	    return fail("Studio active target bar should use one shared selected-target state for Base Image, layer, mask, canvas, and inspector targets");
	  }
  trace_smoke_progress("active target visual source checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_ACTIVE_TARGET_VISUAL_SOURCE")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }
  if (!studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerMaskSetAdjustButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerMaskSetSelectButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerMaskSetPreviewButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskEditLayerButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskSelectButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskPreviewButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerMaskSetRowMoreMenuButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskRowMoreMenuButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("mask-row-actions:v2|default:more-only|edit-layer:menu|edit-mask:menu|preview:menu|delete:menu|move:menu")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.setMaskSetPreviewBypassed(")) ||
      !studio_workspace_source.contains(QStringLiteral("text: \"Edit Layer\"")) ||
      !studio_workspace_source.contains(QStringLiteral("text: \"Edit Selection\"")) ||
      studio_workspace_source.contains(QStringLiteral("text: \"Edit Mask\"")) ||
      !studio_workspace_source.contains(QStringLiteral("role: \"segmented\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.selectMaskSetAndLayer(modelData, true)")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.selectMaskSetAndLayer(modelData, false)"))) {
    return fail("Studio selection rows should keep edit/preview/delete actions in a visible More menu while preserving direct command hooks");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function maskTargetInspectorActive()")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskFirstInspectorSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstInspectorGroup\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstOverlayButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstOutlineButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstHiddenButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstEditLayerButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("root.selectMaskSetAndLayer(root.activeMaskSetData(), true, false)")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.requestLayerFullResolutionPreview()")) ||
      !editor_preview_controller_source.contains(QStringLiteral("EditorPreviewController::requestLayerFullResolutionPreview()")) ||
      !studio_workspace_source.contains(QStringLiteral("text: \"Outline\"")) ||
      !studio_workspace_source.contains(QStringLiteral("value: root.clamp(editorPreview.activeMaskSelectionSmooth, 0.0, 100.0)")) ||
      !studio_workspace_source.contains(QStringLiteral("value: root.clamp(editorPreview.activeMaskSelectionFineEdge, 0.0, 100.0)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstVisibilityButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstInvertButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstFeatherSlider\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstDensitySlider\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstOpacitySlider\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstRefineGroup\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstBrushSettingsGroup\"")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: root.editInspectorTab === \"adjust\" && !root.maskTargetInspectorActive() && root.adjustmentGroupMatches(\"white-balance\")")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: root.editInspectorTab === \"adjust\" && !root.maskTargetInspectorActive() && root.adjustmentGroupMatches(\"light\")")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: root.editInspectorTab === \"adjust\" && !root.maskTargetInspectorActive() && root.adjustmentGroupMatches(\"color\")")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: root.editInspectorTab === \"adjust\" && !root.maskTargetInspectorActive() && root.adjustmentGroupMatches(\"tone-curve\")"))) {
    return fail("Studio Adjust inspector should show mask controls first and hide unrelated color controls while a mask is selected");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function layerTargetInspectorActive()")) ||
      !studio_workspace_source.contains(QStringLiteral("function activeLayerLinkedMaskSets()")) ||
      !studio_workspace_source.contains(QStringLiteral("function activeLayerMaskChildrenSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeLayerMaskChildrenGroup\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeLayerMaskChildRow\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeLayerMaskChildThumbnail\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeLayerMaskChildEditMaskButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: root.layerTargetInspectorActive() && activeLayerMaskChildrenGroup.linkedMaskSets.length > 0")) ||
      !studio_workspace_source.contains(QStringLiteral("full layer adjustments below")) ||
      !studio_workspace_source.contains(QStringLiteral("root.selectMaskSetAndLayer(modelData, false, true)"))) {
    return fail("Studio Adjust inspector should keep linked masks visible as children while a layer target shows the full adjustment stack");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function layerChangedAdjustmentGroups(layer)")) ||
      !studio_workspace_source.contains(QStringLiteral("function layerChangedAdjustmentGroupSignature(layer)")) ||
      !studio_workspace_source.contains(QStringLiteral("function layerLocalAdjustmentSummary(layer)")) ||
      !studio_workspace_source.contains(QStringLiteral("function layerDominantHslChannelName(layer)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerChangedGroupBadgeRow\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerChangedGroupBadge\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerAdjustmentSummaryLabel\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string changedAdjustmentGroupSignature: root.layerChangedAdjustmentGroupSignature(layerData)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string localAdjustmentSummary: root.layerLocalAdjustmentSummary(layerData)")) ||
      !studio_workspace_source.contains(QStringLiteral("function appendGroup(key, label)")) ||
      !studio_workspace_source.contains(QStringLiteral("appendGroup(\"tone-curve\", \"Curve\")")) ||
      !studio_workspace_source.contains(QStringLiteral("appendGroup(\"hsl\", \"HSL\")")) ||
      !studio_workspace_source.contains(QStringLiteral("appendGroup(\"grading\", \"Grade\")")) ||
      !studio_workspace_source.contains(QStringLiteral("appendGroup(\"lut\", \"LUT\")")) ||
      !studio_workspace_source.contains(QStringLiteral("String(layer && layer.changedAdjustmentGroupLabel || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("parts.push(hslChannel.length > 0 ? \"HSL \" + hslChannel : \"HSL\")")) ||
      !studio_workspace_source.contains(QStringLiteral("parts.push(\"Exposure \" + root.signedLayerValueText(root.layerStoredAdjustmentValue(layer, \"exposure\"), \" EV\"))"))) {
    return fail("Studio layer rows should expose changed adjustment group badges and a layer-local adjustment summary");
  }
  if (!layer_row_source.contains(QStringLiteral("rowContract: \"layer-row:v3|fixed-zones|content-height|no-elide|state-priority|actions:visibility-more|secondary-actions:overflow-menu|thumbnail-lock-badge:none\"")) ||
      !layer_row_source.contains(QStringLiteral("zoneContract: \"layer-row-zones:v3|thumbnail|name-state|metadata|state-badges|actions:visibility-more|secondary-actions:overflow-menu\"")) ||
      !layer_row_source.contains(QStringLiteral("premiumLayerContract: \"premium-layer-row:v3|content-height:true|thumbnail-zone:fixed|action-zone:fixed|selected-surface:true|left-accent:none|active-mask-secondary:true|rename-stable:true|delete-separated:true\"")) ||
      !layer_row_source.contains(QStringLiteral("layerInteractionAcceptanceContract: \"layer-interaction-acceptance:v1|select:true|rename:true|reorder:true|visibility:true|opacity:true|mask-select:true|transform:true|escape:true\"")) ||
      !layer_row_source.contains(QStringLiteral("property int thumbnailZoneSize: compact ? 30 : 34")) ||
      !layer_row_source.contains(QStringLiteral("readonly property int actionZoneWidth: (showLockButton ? theme.iconButtonSizeCompact : 0) +")) ||
      !layer_row_source.contains(QStringLiteral("property string maskStateObjectName: \"layerRowMaskState\"")) ||
      !layer_row_source.contains(QStringLiteral("property string opacityReadoutObjectName: \"layerRowOpacityReadout\"")) ||
      !layer_row_source.contains(QStringLiteral("statePriorityContract: \"layer-row-state-priority:v2|selected-layer-primary>active-mask-secondary>solo>changed>locked>hidden>hover\"")) ||
      !layer_row_source.contains(QStringLiteral("activeMaskIndicatorContract: \"layer-active-mask-indicator:v1|secondary-edge:right|accent:cool|visible:\"")) ||
      !layer_row_source.contains(QStringLiteral("readonly property color activeMaskAccent: theme.accentCool400")) ||
      !layer_row_source.contains(QStringLiteral("interactionContract: \"layer-row-interaction:v1|hover-actions:true|keyboard-actions:true|disabled-reasons:true|rename:direct-or-menu|reorder:move-buttons-or-menu|delete:separated\"")) ||
      !layer_row_source.contains(QStringLiteral("thumbnailDprContract: \"layer-row-thumbnail-dpr:v1|source-size:screen-aware|base-image:true|display:\"")) ||
      !layer_row_source.contains(QStringLiteral("readonly property real thumbnailDevicePixelRatio: Math.max(1.0, Number(Screen.devicePixelRatio || 1.0))")) ||
      !layer_row_source.contains(QStringLiteral("readonly property int screenAwareThumbnailSourceSize: Math.max(")) ||
      !layer_row_source.contains(QStringLiteral("sourceSize.width: root.screenAwareThumbnailSourceSize")) ||
      !layer_row_source.contains(QStringLiteral("sourceSize.height: root.screenAwareThumbnailSourceSize")) ||
      !layer_row_source.contains(QStringLiteral("Keys.onReturnPressed: root.clicked()")) ||
      !layer_row_source.contains(QStringLiteral("objectName: \"layerRowReorderHandle\"")) ||
      !layer_row_source.contains(QStringLiteral("readonly property bool contextualActionsVisible: hovered || activeFocus || selected || activeMask")) ||
      !layer_row_chrome_source.contains(QStringLiteral("rowChromeContract: \"layer-row-chrome:v1|kind:\"")) ||
      !layer_row_chrome_source.contains(QStringLiteral("zoneContract: \"layer-row-chrome-zones:v2|thumbnail|content|state-summary|row-actions|nested-children\"")) ||
      !layer_row_chrome_source.contains(QStringLiteral("premiumLayerContract: \"premium-layer-chrome:v3|stable-height:true|nested-mask-relation:true|selected-surface:true|left-accent:none|active-mask-secondary:true|quiet-hover:true|focus-ring:true\"")) ||
      !layer_row_chrome_source.contains(QStringLiteral("layerInteractionAcceptanceContract: \"layer-interaction-acceptance:v1|select:true|rename:true|reorder:true|visibility:true|opacity:true|mask-select:true|transform:true|escape:true\"")) ||
      !layer_row_chrome_source.contains(QStringLiteral("statePriorityContract: \"layer-row-state-priority:v2|selected-layer-primary>active-mask-secondary>solo>changed>locked>hidden>hover\"")) ||
	      !layer_row_chrome_source.contains(QStringLiteral("activeMaskIndicatorContract: \"layer-active-mask-indicator:v1|secondary-edge:right|accent:cool|visible:\"")) ||
	      !layer_row_chrome_source.contains(QStringLiteral("readonly property color activeMaskAccent: theme.accentCool400")) ||
	      !layer_row_chrome_source.contains(QStringLiteral("interactionContract: \"layer-row-interaction:v1|hover-actions:true|keyboard-actions:true|disabled-reasons:true|rename:properties|reorder:move-buttons-or-menu|delete:separated\"")) ||
	      !layer_row_chrome_source.contains(QStringLiteral("motionContract: \"layer-row-motion:v1|duration:\"")) ||
	      !layer_row_chrome_source.contains(QStringLiteral("|reveal:subtle|hover-fill:true|accent-fade:true|height-stable:true|layout-shift:false")) ||
	      !layer_row_chrome_source.contains(QStringLiteral("Behavior on color")) ||
	      !layer_row_chrome_source.contains(QStringLiteral("Behavior on border.color")) ||
	      !layer_row_chrome_source.contains(QStringLiteral("property bool accentVisible: selected || activeMask || solo || changed")) ||
	      !studio_workspace_source.contains(QStringLiteral("function layerStackAcceptanceSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("layerStackAcceptanceContract: root.layerStackAcceptanceSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("delegate: LayerRowChrome")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerRow\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerReorderHandle\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function photoLayerGroupDropTargetAtPointer(sourceIndex, pointerItem, pointerX, pointerY, rowsRepeater)")) ||
      !studio_workspace_source.contains(QStringLiteral("function finishPhotoLayerDrag(sourceIndex, sourceRow, pointerItem, pointerX, pointerY, dragProxy, rowsRepeater)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerHierarchyDropHighlight\"")) ||
      !studio_workspace_source.contains(QStringLiteral("LayerRowActionZone {")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerFixedStateActionZone\"")) ||
      !studio_workspace_source.contains(QStringLiteral("brushMode: root.brushToolReady()")) ||
      !studio_workspace_source.contains(QStringLiteral("menuItems: root.layerRowMenuItems(layerIndex, layerData)")) ||
      !layer_row_action_zone_source.contains(QStringLiteral("actionZoneContract: \"layer-row-action-zone:v6|component:LayerRowActionZone|normal-width:edit-visibility-more|brush-width:edit-more|edit:labeled|summary-in-tooltip:true|mask-state:tooltip-only|opacity-readout:properties-only|visibility:icon-only|more:icon-only|hover-no-shift:true|no-elide:true\"")) ||
      !layer_row_action_zone_source.contains(QStringLiteral("objectName: \"photoLayerRowEditButton\"")) ||
      !layer_row_action_zone_source.contains(QStringLiteral("objectName: \"photoLayerMaskStatePill\"")) ||
      !layer_row_action_zone_source.contains(QStringLiteral("visible: false")) ||
      !layer_row_action_zone_source.contains(QStringLiteral("objectName: \"photoLayerOpacityLabel\"")) ||
      !layer_row_action_zone_source.contains(QStringLiteral("objectName: \"photoLayerVisibilityButton\"")) ||
      !layer_row_action_zone_source.contains(QStringLiteral("objectName: \"photoLayerRowMoreMenuButton\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("reorderContract: \"layer-reorder:v2|drag-release:any-distance:true|keyboard:true|menu:true|selection-preserved:true|locked:blocked\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("function selectPhotoLayerWithModifiers(index, modifiers)")) ||
	      !studio_workspace_source.contains(QStringLiteral("function beginInlinePhotoLayerRename(index)")) ||
	      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerInlineNameField\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("drag.target: photoLayerReorderDragProxy")) ||
	      !studio_workspace_source.contains(QStringLiteral("asynchronous: true")) ||
	      !studio_workspace_source.contains(QStringLiteral("actionVisibilityContract: \"layer-actions:v2|lazy:true|active:false|hover-instantiates:false|precision-controls:properties-panel|keyboard-accessible:properties-panel|disabled-reasons:true\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("actionVisibilityContract: \"layer-structure-actions:v2|lazy:true|active:false|hover-instantiates:false|reorder:properties-menu|overflow-menu:true|destructive-separated:true|disabled-reasons:true\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerInlineActions\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("active: false\n                                                        asynchronous: true\n                                                        sourceComponent: Flow")) ||
	      !studio_workspace_source.contains(QStringLiteral("rowKind: \"photo-layer\"")) ||
      !studio_workspace_source.contains(QStringLiteral("rowKind: \"standalone-mask-set\"")) ||
      !studio_workspace_source.contains(QStringLiteral("rowKind: \"nested-mask-set\"")) ||
      !studio_workspace_source.contains(QStringLiteral("selected: layerSelected || activeLayerTarget")) ||
      !studio_workspace_source.contains(QStringLiteral("activeMask: layerMaskSelected")) ||
      !studio_workspace_source.contains(QStringLiteral("activeMask: activeMaskSet")) ||
      !studio_workspace_source.contains(QStringLiteral("changed: changedAdjustmentGroups.length > 0"))) {
    return fail("Studio photo and mask layer rows should expose strict zones, state priority, contextual actions, reorder affordance, and full editing surface");
  }
  if (!menu_button_source.contains(QStringLiteral("menuContract: \"menu-button:v1|items:\"")) ||
      !menu_button_source.contains(QStringLiteral("|danger-last:\"")) ||
      !menu_button_source.contains(QStringLiteral("String(destructiveItemsLast)")) ||
      !menu_button_source.contains(QStringLiteral("|disabled-reasons-complete:\"")) ||
      !menu_button_source.contains(QStringLiteral("String(disabledItemsExplainState)")) ||
	      !menu_button_source.contains(QStringLiteral("|keyboard:true|escape:true|disabled-reasons:true|checked-state:true|no-elide:true|readable-text:true|geometry:true|width:\"")) ||
	      !menu_button_source.contains(QStringLiteral("property bool menuEnabled: (menuItems || []).length > 0")) ||
	      !menu_button_source.contains(QStringLiteral("property bool primaryOpensMenu: false")) ||
	      !menu_button_source.contains(QStringLiteral("property bool secondaryActionMenu: false")) ||
	      !menu_button_source.contains(QStringLiteral("property bool allowLabelWrap: false")) ||
	      !menu_button_source.contains(QStringLiteral("property string menuTooltipText: text.length > 0 ? \"More options for \" + text : \"More options\"")) ||
	      !menu_button_source.contains(QStringLiteral("allowLabelWrap: root.allowLabelWrap")) ||
	      !menu_button_source.contains(QStringLiteral("? root.disabledReason : root.menuTooltipText")) ||
	      !menu_button_source.contains(QStringLiteral("readonly property bool secondaryActionsUseMore: !secondaryActionMenu || text === \"More\"")) ||
	      !menu_button_source.contains(QStringLiteral("secondaryActionSemanticsContract: \"secondary-action-menu:v1|secondary:\"")) ||
	      !menu_button_source.contains(QStringLiteral("|uses-more:\"")) ||
	      !menu_button_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|menu-wrap:true")) ||
      !menu_button_source.contains(QStringLiteral("menuGeometryContract: \"menu-geometry:v1|min-width:\"")) ||
      !menu_button_source.contains(QStringLiteral("readonly property int menuMinimumWidth: theme.menuMinWidth")) ||
      !menu_button_source.contains(QStringLiteral("readonly property int menuMaximumWidth: theme.menuMaxWidth")) ||
      !menu_button_source.contains(QStringLiteral("MenuItemMetrics { id: menuMetrics; items: root.menuItems }")) ||
      !menu_button_source.contains(QStringLiteral("activeFocusOnTab: false")) ||
      !menu_button_source.contains(QStringLiteral("Accessible.ignored: true")) ||
      !menu_button_source.contains(QStringLiteral("accessibleName: root.resolvedAccessibleName")) ||
      !menu_button_source.contains(QStringLiteral("accessibleName: root.menuTooltipText")) ||
      !menu_button_source.contains(QStringLiteral("MenuPopover {")) ||
      !split_button_source.contains(QStringLiteral("splitContract: \"split-button:v1|primary:\"")) ||
      !split_button_source.contains(QStringLiteral("|danger-last:\"")) ||
      !split_button_source.contains(QStringLiteral("String(destructiveItemsLast)")) ||
      !split_button_source.contains(QStringLiteral("|disabled-reasons-complete:\"")) ||
      !split_button_source.contains(QStringLiteral("String(disabledItemsExplainState)")) ||
      !split_button_source.contains(QStringLiteral("|keyboard:true|escape:true|disabled-reasons:true|checked-state:true|no-elide:true|readable-text:true|geometry:true|width:\"")) ||
      !split_button_source.contains(QStringLiteral("property bool allowLabelWrap: false")) ||
      !split_button_source.contains(QStringLiteral("allowLabelWrap: root.allowLabelWrap")) ||
      !split_button_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|primary-fit:true|menu-wrap:true")) ||
      !split_button_source.contains(QStringLiteral("menuGeometryContract: \"menu-geometry:v1|min-width:\"")) ||
      !split_button_source.contains(QStringLiteral("readonly property int menuMinimumWidth: theme.menuMinWidth")) ||
      !split_button_source.contains(QStringLiteral("readonly property int menuMaximumWidth: theme.menuMaxWidth")) ||
      !split_button_source.contains(QStringLiteral("MenuItemMetrics { id: menuMetrics; items: root.menuItems }")) ||
      !split_button_source.contains(QStringLiteral("activeFocusOnTab: false")) ||
      !split_button_source.contains(QStringLiteral("Accessible.ignored: true")) ||
      !split_button_source.contains(QStringLiteral("property string accessibleName: \"\"")) ||
      !split_button_source.contains(QStringLiteral("accessibleName: root.resolvedAccessibleName")) ||
      !split_button_source.contains(QStringLiteral("accessibleName: root.menuTooltipText")) ||
      !split_button_source.contains(QStringLiteral("|wrapper:ignored|single-focus-stop:true|accessible-name-override:true")) ||
      !split_button_source.contains(QStringLiteral("MenuPopover {")) ||
      !menu_item_metrics_source.contains(QStringLiteral("function nonDangerAfterFirstDangerIndex()")) ||
      !menu_item_metrics_source.contains(QStringLiteral("readonly property int disabledItemReasonCount: countDisabledItemsWithReasons()")) ||
      !menu_item_metrics_source.contains(QStringLiteral("readonly property bool disabledItemsExplainState: disabledItemCount === disabledItemReasonCount")) ||
      !menu_item_metrics_source.contains(QStringLiteral("function disabledReasonForItem(item)")) ||
      !menu_item_metrics_source.contains(QStringLiteral("function countDisabledItemsWithReasons()")) ||
      !menu_item_metrics_source.contains(QStringLiteral("function countDisabledItems()")) ||
      !menu_item_metrics_source.contains(QStringLiteral("function countDangerItems()")) ||
      !menu_popover_source.contains(QStringLiteral("Math.max(root.menuMinimumWidth, root.menuWidth, root.anchorWidth")) ||
      !menu_popover_source.contains(QStringLiteral("closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside")) ||
      !(menu_popover_source.contains(QStringLiteral("Keys.onPressed: function(event)")) ||
        menu_popover_source.contains(QStringLiteral("Keys.onPressed: function (event)"))) ||
      !menu_popover_source.contains(QStringLiteral("delegate: PopupMenuRow"))) {
    return fail("Shared menu and split buttons should expose keyboard, checked, disabled-reason completeness, destructive-last, no-elide, readable-text, and geometry contracts");
  }
  if (!popup_menu_row_source.contains(QStringLiteral("popupMenuRowContract: \"popup-menu-row:v3|shared-delegate:true|keyboard:true|hover:true|description:true|disabled-reason:true|disabled-fallback:true|checked-state:true|danger-state:true|shortcut-fit:true|readable-text:true|no-elide:true|geometry:true|icon-column:true|shortcut-column:true|role:menu-item\"")) ||
      !popup_menu_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v2|ellipsis:none|label-wrap:true|description-wrap:true|disabled-wrap:true|shortcut-fit:true|tooltip-full:true|accessibility-full:true|full-label-available:true\"")) ||
      !popup_menu_row_source.contains(QStringLiteral("menuGeometryContract: \"popup-menu-row-geometry:v2|min-height:dense-token|preferred-height:menu-token|icon-column:16|shortcut-column:72|shortcut-align:right|label-lines:\"")) ||
      !popup_menu_row_source.contains(QStringLiteral("readonly property int rowMinimumHeight: theme.controlHeightDense")) ||
      !popup_menu_row_source.contains(QStringLiteral("readonly property int menuIconColumnWidth: theme.scaled(16)")) ||
      !popup_menu_row_source.contains(QStringLiteral("readonly property int menuShortcutColumnWidth: theme.scaled(72)")) ||
      !popup_menu_row_source.contains(QStringLiteral("readonly property string itemDescription: String(modelData.description || modelData.detail || modelData.summary || \"\")")) ||
      !popup_menu_row_source.contains(QStringLiteral("Accessible.description: itemDisabledReason.length > 0 ? itemDisabledReason : itemDescription")) ||
      !popup_menu_row_source.contains(QStringLiteral("Accessible.role: Accessible.MenuItem")) ||
      !popup_menu_row_source.contains(QStringLiteral("Accessible.checked: itemChecked")) ||
      !popup_menu_row_source.contains(QStringLiteral("visible: root.enabled && root.itemDescription.length > 0")) ||
      !popup_menu_row_source.contains(QStringLiteral("AppToolTip {\n        targetItem: root")) ||
      !popup_menu_row_source.contains(QStringLiteral("signal activated(string key)")) ||
      !popup_menu_row_source.contains(QStringLiteral("signal navigateRequested(int direction)")) ||
      !popup_menu_row_source.contains(QStringLiteral("Keys.onPressed: function(event)")) ||
      !popup_menu_row_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !popup_menu_row_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !popup_menu_row_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !menu_button_source.contains(QStringLiteral("MenuPopover {")) ||
      !menu_button_source.contains(QStringLiteral("root.menuItemTriggered(key)")) ||
      !split_button_source.contains(QStringLiteral("MenuPopover {")) ||
      !split_button_source.contains(QStringLiteral("root.menuItemTriggered(key)")) ||
      !menu_popover_source.contains(QStringLiteral("delegate: PopupMenuRow")) ||
      !menu_popover_source.contains(QStringLiteral("root.itemTriggered(key)")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("delegate: PopupMenuRow")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("root.menuItemTriggered(root.toolValue, key)")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("menuGeometryContract: \"menu-geometry:v1|min-width:\"")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("Math.max(root.menuMinimumWidth, root.menuWidth")) ||
      !tool_rail_source.contains(QStringLiteral("delegate: PopupMenuRow")) ||
      !tool_rail_source.contains(QStringLiteral("root.menuItemTriggered(toolDelegate.tool, key)")) ||
      !tool_rail_source.contains(QStringLiteral("|keyboard-menu:true|disabled-reasons:true|no-elide:true|readable-text:true|geometry:true")) ||
      !tool_rail_source.contains(QStringLiteral("toolDiscoveryContract: \"tool-discovery:v1|families-visible:true|active-family:true|active-variant:true|variants-one-expansion:true|stale-menus-close:true|disabled-reasons:true\"")) ||
      !tool_rail_source.contains(QStringLiteral("staleMenuCleanupContract: \"tool-rail-stale-menu-cleanup:v1|one-expansion:true|on-tool-click:true|on-menu-open:true|on-menu-trigger:true|on-tools-changed:true|on-switch-serial:true\"")) ||
      !tool_rail_source.contains(QStringLiteral("property int switchSerial: 0")) ||
      !tool_rail_source.contains(QStringLiteral("onSwitchSerialChanged: closeOpenMenus()")) ||
      !tool_rail_source.contains(QStringLiteral("function closeOpenMenus()")) ||
      !tool_rail_source.contains(QStringLiteral("function openToolMenu()")) ||
      !tool_rail_source.contains(QStringLiteral("root.closeOpenMenus()")) ||
      !tool_rail_source.contains(QStringLiteral("Math.max(root.menuMinimumWidth,")) ||
      menu_button_source.contains(QStringLiteral("delegate: Button")) ||
      split_button_source.contains(QStringLiteral("delegate: Button")) ||
      carousel_tool_rail_button_source.contains(QStringLiteral("delegate: Button")) ||
      tool_rail_source.contains(QStringLiteral("delegate: Button"))) {
    return fail("Expandable menu buttons should share PopupMenuRow for readable no-elide rows with stable geometry while preserving trigger and keyboard behavior");
  }
  if (!tool_hud_source.contains(QStringLiteral("layoutContract: \"tool-hud:v2|stable-height:true|status-title-values-done-cancel:true|flex-status-column:true|fixed-actions:true|subdued-chrome:true|keyboard:enter-escape|readable-text:true|no-elide:true|no-overlap:true\"")) ||
      !tool_hud_source.contains(QStringLiteral("toolDiscoveryContract: \"tool-hud-discovery:v1|active-family:\"")) ||
      !tool_hud_source.contains(QStringLiteral("commandSemanticsContract: \"tool-hud-command-semantics:v1|done:exit-tool-state|cancel:abort-pending-or-exit|apply:not-used|close:not-used|enter:done-when-completable|escape:cancel\"")) ||
      !tool_hud_source.contains(QStringLiteral("property string activeFamilyLabel: title")) ||
      !tool_hud_source.contains(QStringLiteral("property string activeVariantLabel: subtitle")) ||
      !tool_hud_source.contains(QStringLiteral("readableTextContract: \"readable-text:v2|ellipsis:none|title-fit:true|subtitle-fit:true|tooltip-full:true|accessibility-full:true|min-title-px:9|min-subtitle-px:8\"")) ||
      !tool_hud_source.contains(QStringLiteral("readonly property int hudHeight: 44")) ||
      !tool_hud_source.contains(QStringLiteral("property int doneMinimumReadableWidth: 74")) ||
      !tool_hud_source.contains(QStringLiteral("property int cancelMinimumReadableWidth: 88")) ||
      !tool_hud_source.contains(QStringLiteral("Layout.preferredWidth: 188")) ||
      !tool_hud_source.contains(QStringLiteral("Layout.maximumWidth: 280")) ||
      !tool_hud_source.contains(QStringLiteral("text: root.title")) ||
      !tool_hud_source.contains(QStringLiteral("text: root.subtitle")) ||
      !tool_hud_source.contains(QStringLiteral("minimumReadableWidth: root.doneMinimumReadableWidth")) ||
      !tool_hud_source.contains(QStringLiteral("minimumReadableWidth: root.cancelMinimumReadableWidth")) ||
      !tool_hud_source.contains(QStringLiteral("activeFocusOnTab: true")) ||
      !tool_hud_source.contains(QStringLiteral("Keys.onReturnPressed: if (root.canComplete) root.doneRequested()")) ||
      !tool_hud_source.contains(QStringLiteral("Keys.onEnterPressed: if (root.canComplete) root.doneRequested()")) ||
      !tool_hud_source.contains(QStringLiteral("Keys.onEscapePressed: root.cancelRequested()")) ||
      !tool_hud_source.contains(QStringLiteral("border.width: 0")) ||
      !tool_hud_source.contains(QStringLiteral("border.color: \"transparent\"")) ||
      !tool_hud_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"cropHudDoneButton\"\n                                minimumReadableWidth: 74")) ||
      !brush_tool_hud_source.contains(QStringLiteral("objectName: \"brushHudDoneButton\"")) ||
      !brush_tool_hud_source.contains(QStringLiteral("minimumReadableWidth: theme.scaled(78)")) ||
      !tool_command_bar_source.contains(QStringLiteral("commandBarContract: \"tool-command-bar:v2|single-row:true|fixed-height:\"")) ||
      !tool_command_bar_source.contains(QStringLiteral("adaptive-menu:true|narrow-inline-to-more:true|status-collapses:true")) ||
      !tool_command_bar_source.contains(QStringLiteral("adaptiveLayoutContract: \"tool-command-bar-adaptive:v1|threshold:\"")) ||
      !tool_command_bar_source.contains(QStringLiteral("function inlineActionMenuItems()")) ||
      !tool_command_bar_source.contains(QStringLiteral("function moreKeyIsAdaptiveInline(key)")) ||
      !tool_command_bar_source.contains(QStringLiteral("property var inlineActions: []")) ||
      !tool_command_bar_source.contains(QStringLiteral("property bool primaryEnabled: true")) ||
	      !tool_command_bar_source.contains(QStringLiteral("property string moreButtonObjectName: \"toolCommandMoreMenuButton\"")) ||
	      !tool_command_bar_source.contains(QStringLiteral("primaryActionSemanticsContract: \"tool-command-primary-semantics:v1|text:\"")) ||
	      !tool_command_bar_source.contains(QStringLiteral("? \"commit-or-generate-result\"")) ||
	      !tool_command_bar_source.contains(QStringLiteral("? \"exit-tool-state\"")) ||
	      !tool_command_bar_source.contains(QStringLiteral("|apply-only-commit:true|done-only-exit:true|cancel:not-primary|close:not-primary")) ||
	      !tool_command_bar_source.contains(QStringLiteral("motionContract: \"tool-command-bar-motion:v2|duration:140|fade:true|stable-height:true|stable-layout:true|interruptible:true|mode-change:\"")) ||
	      !tool_command_bar_source.contains(QStringLiteral("modeChangeMotionContract: \"mode-change-motion:v1|enabled:\"")) ||
	      !tool_command_bar_source.contains(QStringLiteral("property bool modeChangeAnimationEnabled: false")) ||
	      !tool_command_bar_source.contains(QStringLiteral("onModeTextChanged:")) ||
	      !tool_command_bar_source.contains(QStringLiteral("modeChangeAnimation.restart()")) ||
	      !tool_command_bar_source.contains(QStringLiteral("SequentialAnimation {\n            id: modeChangeAnimation")) ||
	      !tool_command_bar_source.contains(QStringLiteral("Behavior on opacity")) ||
	      !tool_command_bar_source.contains(QStringLiteral("primaryOpensMenu: true")) ||
	      !studio_workspace_source.contains(QStringLiteral("objectName: \"retouchToolHud\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("studioCommandSemanticsContract: \"studio-command-semantics:v2|apply:commit-or-generate-result|done:exit-tool-state-to-layers|cancel:abort-pending-work|close:panel-or-dialog-only")) ||
	      !studio_workspace_source.contains(QStringLiteral("selection:smart-brush-shape-retouch-unified")) ||
	      !studio_workspace_source.contains(QStringLiteral("retouch:apply-before-commit-done-after-commit")) ||
	      !studio_workspace_source.contains(QStringLiteral("studioSecondaryActionSemanticsContract: \"studio-secondary-actions:v1|generic-label:More")) ||
	      !studio_workspace_source.contains(QStringLiteral("studioDestructiveActionPolicyContract: \"studio-destructive-actions:v1|primary-surface:false|default-layer-row:false|secondary-surface:More-or-properties|menu-order:destructive-last|role:destructive|confirmation:when-needed|inline-exception:blocking-retry-only\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("studioAdvancedControlDisclosureContract: \"studio-advanced-disclosure:v1|brush:inspector-and-more|retouch:more-or-mode-menu|experimental:hidden-disabled|light:collapsed-rolloff-levels-zones|advanced-color:collapsed-specialist-tools|diagnostics:details-only\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("return editorPreview.activeMaskCommitted ? \"Done\" : \"Apply\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("primaryButtonObjectName: editorPreview.inpaintProgressActive")) ||
	      !studio_workspace_source.contains(QStringLiteral("specialized-tool-hud:v3|tool:retouch|component:ToolCommandBar|single-row:true")) ||
	      !studio_workspace_source.contains(QStringLiteral("mode-change-animation:true")) ||
	      !studio_workspace_source.contains(QStringLiteral("modeChangeAnimationEnabled: true")) ||
	      !studio_workspace_source.contains(QStringLiteral("commandPlacementContract: \"canvas-command-placement:v1|slot:top-center|top-margin:12|safe-width:true|consistent:true|image-overlap:minimized\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeCanvasToolChip\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("studio-tool-hud-motion:v1|surface:active-canvas-tool-chip|duration:")) ||
	      !studio_workspace_source.contains(QStringLiteral("objectName: \"brushToolHud\"")) ||
	      !brush_tool_hud_source.contains(QStringLiteral("brush-tool-hud-component:v3|owns:mode-size-quick-tuning-done|precision:size-percent-field|advanced:inspector-or-shortcuts|signals:mode-tuning-size-done|object-names:preserved|workspace-state:external")) ||
	      !studio_workspace_source.contains(QStringLiteral("studio-tool-hud-motion:v1|surface:polygon-lasso-hud|duration:")) ||
	      !studio_workspace_source.contains(QStringLiteral("inspector-panel-motion:v1|duration:")) ||
	      !studio_workspace_source.contains(QStringLiteral("selection-header-motion:v1|duration:")) ||
	      !studio_workspace_source.contains(QStringLiteral("StudioToolTransitionOverlay {")) ||
	      !studio_tool_transition_source.contains(QStringLiteral("tool-switch-transition-motion:v1|duration:")) ||
	      !studio_tool_transition_source.contains(QStringLiteral("objectName: \"editToolTransitionOverlay\"")) ||
	      !studio_tool_transition_source.contains(QStringLiteral("objectName: \"editToolTransitionProgress\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("moreButtonObjectName: \"retouchHudMoreMenuButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"polygonHudCompleteButton\"\n                                    minimumReadableWidth: 96")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"polygonHudCancelButton\"\n                                    minimumReadableWidth: 88"))) {
    return fail("Shared ToolHud should preserve stable, subdued, keyboard-safe, no-elide canvas tool status behavior");
  }
  trace_smoke_progress("command semantics source checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_COMMAND_SEMANTICS_SOURCE")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }
  if (!studio_workspace_source.contains(QStringLiteral("objectName: \"selectedPhotoLayerBatchActionsButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("text: root.normalizedPhotoLayerSelection().length + \" selected · Batch actions\"")) ||
      !studio_workspace_source.contains(QStringLiteral("menuItems: root.selectedPhotoLayerBatchMenuItems(photoLayerPropertiesPanel.layerIndex, photoLayerPropertiesPanel.layerData)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"studioPresetManageMenuButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("text: \"Manage presets\"")) ||
      !studio_workspace_source.contains(QStringLiteral("tooltip: \"Rename, move, export, or delete custom presets.\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"exportPresetManageMenuButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("{ key: \"manage-saved\", label: \"Manage Saved Presets\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"exportQueueManagementMenuButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("tooltipText: \"Queue retry and cleanup actions.\"")) ||
      !studio_workspace_source.contains(QStringLiteral("secondaryActionMenu: true")) ||
      studio_workspace_source.contains(QStringLiteral("text: \"Manage\"")) ||
      studio_workspace_source.contains(QStringLiteral("text: \"Queue Actions\""))) {
    return fail("Studio generic secondary action menus should use More while preserving specific tooltips and menu items");
  }
  trace_smoke_progress("secondary action menu semantics source checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_MORE_SEMANTICS_SOURCE")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }
  trace_smoke_progress("retouch mode animation source checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_RETOUCH_MODE_ANIMATION_SOURCE")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }
  if (!command_palette_row_source.contains(QStringLiteral("commandPaletteRowContract: \"command-palette-row:v1|current-state:true|disabled-state:true|hover:true|keyboard-compatible:true|readable-text:true|no-elide:true\"")) ||
      !command_palette_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-fit:true|category-fit:true|shortcut-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !command_palette_row_source.contains(QStringLiteral("signal selected(int rowIndex)")) ||
      !command_palette_row_source.contains(QStringLiteral("signal activated(int rowIndex)")) ||
      !command_palette_row_source.contains(QStringLiteral("onHoveredChanged")) ||
      !command_palette_row_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !command_palette_row_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !command_palette_dialog_source.contains(QStringLiteral("objectName: \"commandPaletteDialog\"")) ||
      !command_palette_dialog_source.contains(QStringLiteral("dialogVisibilityContract: \"dialog-visibility:v1|command-palette|always:search-results-count-shortcuts|contextual:filtered-results-disabled-state-empty-state|one-expansion:all-commands-through-search|hover-focus:row-current-selection|diagnostic:missing-required-commands|preserve:functionality\"")) ||
      !command_palette_dialog_source.contains(QStringLiteral("dialogSurfaceContract: \"dialog-surface:v1|command-palette|search:always|results:dominant|footer:keyboard-help|rows:shared-readable|no-elide:true|no-overlap:true\"")) ||
      !command_palette_dialog_source.contains(QStringLiteral("functionPreservationMap: \"function-preservation:v1|command-search:always|command-run:keyboard-and-row|disabled-reasons:row-state|shortcuts:footer-and-row|missing-required:diagnostic-signature\"")) ||
      !command_palette_dialog_source.contains(QStringLiteral("delegate: CommandPaletteRow")) ||
      !command_palette_dialog_source.contains(QStringLiteral("rowIndex: index")) ||
      !command_palette_dialog_source.contains(QStringLiteral("current: ListView.isCurrentItem")) ||
      !command_palette_dialog_source.contains(QStringLiteral("commandList.currentIndex = rowIndex")) ||
      !command_palette_dialog_source.contains(QStringLiteral("searchField.executeSelected()"))) {
    return fail("Command palette should expose dialog visibility/function contracts and use CommandPaletteRow with readable labels, current/disabled state, and preserved selection activation");
  }
  if (!keyboard_shortcuts_dialog_source.contains(QStringLiteral("objectName: \"keyboardShortcutsDialog\"")) ||
      !keyboard_shortcuts_dialog_source.contains(QStringLiteral("dialogVisibilityContract: \"dialog-visibility:v2|keyboard-shortcuts|always:title-search-close|contextual:matching-editable-shortcuts-matching-reference-sections-presets-without-query-status-conflicts-empty-search-platform-modifier|one-expansion:preset-apply-editable-sequence-reset|hover-focus:preset-buttons-edit-fields-reset-controls|diagnostic:conflict-count-search-results|preserve:functionality\"")) ||
      !keyboard_shortcuts_dialog_source.contains(QStringLiteral("dialogSurfaceContract: \"dialog-surface:v2|keyboard-shortcuts|search:header|editable:matching|sections:scrollable-matching|keys:scaled-wrapped|descriptions:wrap|presets:wrapping-buttons|empty:explicit|no-elide:true|no-overlap:true\"")) ||
      !keyboard_shortcuts_dialog_source.contains(QStringLiteral("functionPreservationMap: \"function-preservation:v2|search-shortcuts:header-field|edit-sequence:inline-field-persisted|reset-sequence:inline-reset|apply-presets:preset-buttons-persisted|conflict-detection:dialog-and-controller|runtime-actions:live-bindings|platform-modifiers:mac-or-control|shortcut-help:section-cards\"")) ||
      !keyboard_shortcuts_dialog_source.contains(QStringLiteral("function shortcutDialogWorkflowSignature()")) ||
      !keyboard_shortcuts_dialog_source.contains(QStringLiteral("minimumReadableWidth: 112")) ||
      !keyboard_shortcuts_dialog_source.contains(QStringLiteral("Layout.minimumWidth: theme.scaled(150)")) ||
      !keyboard_shortcuts_dialog_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !keyboard_shortcuts_dialog_source.contains(QStringLiteral("elide: Text.ElideNone"))) {
    return fail("Keyboard shortcuts dialog should expose visibility/function contracts and keep presets, editable shortcuts, and help rows readable without elision");
  }
  if (!preference_nav_row_source.contains(QStringLiteral("preferenceNavRowContract: \"preference-nav-row:v1|selected-state:true|hover:true|keyboard:true|readable-text:true|no-elide:true\"")) ||
      !preference_nav_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|title-fit:true|subtitle-wrap:true|tooltip-full:true|accessibility-full:true\"")) ||
      !preference_nav_row_source.contains(QStringLiteral("signal activated(int pageIndex)")) ||
      !preference_nav_row_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      !preference_nav_row_source.contains(QStringLiteral("fontSizeMode: root.showSubtitle ? Text.HorizontalFit : Text.FixedSize")) ||
      !preference_nav_row_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !app_preferences_dialog_source.contains(QStringLiteral("objectName: \"appPreferencesDialog\"")) ||
      !app_preferences_dialog_source.contains(QStringLiteral("dialogVisibilityContract: \"dialog-visibility:v1|preferences|always:nav-current-page-close-by-escape|contextual:page-settings-storage-paths-backend-status|one-expansion:workspace-picker-backend-picker-folder-open|hover-focus:preference-navigation-controls|diagnostic:backend-availability-storage-paths|preserve:functionality\"")) ||
      !app_preferences_dialog_source.contains(QStringLiteral("dialogSurfaceContract: \"dialog-surface:v2|preferences|nav:scrollable-left|page:dominant|sections:scrollable|toggles:stable|paths:copyable-wrap|no-elide:true|no-overlap:true\"")) ||
      !app_preferences_dialog_source.contains(QStringLiteral("functionPreservationMap: \"function-preservation:v4|dark-mode:general-toggle|interface-scale:0.9-2.0|density:comfortable-balanced-compact|contrast:standard-high|motion:standard-reduced|color-vision:5-palettes|startup-workspace:general-combo|reopen-workspace:general-toggle|preview-backend:general-combo|timeline-defaults:timeline-page|storage-folders:storage-open-actions\"")) ||
      !app_preferences_dialog_source.contains(QStringLiteral("delegate: PreferenceNavRow")) ||
      !app_preferences_dialog_source.contains(QStringLiteral("pageIndex: index")) ||
      !app_preferences_dialog_source.contains(QStringLiteral("subtitle: root.pageSubtitle(index)")) ||
      !app_preferences_dialog_source.contains(QStringLiteral("accent: root.pageAccent(index)")) ||
      !app_preferences_dialog_source.contains(QStringLiteral("root.currentPage = pageIndex"))) {
    return fail("Preferences dialog should expose dialog visibility/function contracts and use PreferenceNavRow with readable labels, selected state, keyboard activation, and preserved page switching");
  }
  if (!activity_status_bar_source.contains(QStringLiteral("activityStatusContract: \"activity-status-bar:v2|placement:under-navigation|surface:integrated|states:loading-error-recovery|progress:additive-rgb|actions:cancel-retry-review-dismiss|no-modal:true\"")) ||
      !activity_status_bar_source.contains(QStringLiteral("property bool dismissible: false")) ||
      !activity_status_bar_source.contains(QStringLiteral("signal actionRequested()")) ||
      !main_qml_source.contains(QStringLiteral("readonly property bool recoveryBannerVisible")) ||
      !main_qml_source.contains(QStringLiteral("objectName: window.recoveryBannerVisible ? \"recoveryStatusIndicator\"")) ||
      !main_qml_source.contains(QStringLiteral("ActivityStatusBar {")) ||
      main_qml_source.contains(QStringLiteral("StartupLoadingPopup {")) ||
      !main_qml_source.contains(QStringLiteral("RecoveryStatusPanel {")) ||
      !main_qml_source.contains(QStringLiteral("!window.libraryLoadingSceneVisible")) ||
      !startup_loading_popup_source.contains(QStringLiteral("objectName: \"startupLoadingPopup\"")) ||
      !startup_loading_popup_source.contains(QStringLiteral("function synchronize()")) ||
      !startup_loading_popup_source.contains(QStringLiteral("observedLoading = true")) ||
      !startup_loading_popup_source.contains(QStringLiteral("finished = true")) ||
      !popup_surface_source.contains(QStringLiteral("objectName: \"popupSurface\"")) ||
      !popup_surface_source.contains(QStringLiteral("popupSurfaceContract: \"popup-surface:v3|shared|fill:neutral-flat|border:single|accent:optional-rail|radius:consistent|nested-card:false|no-overlap:true\"")) ||
      !popup_surface_source.contains(QStringLiteral("surfaceHierarchyContract: \"surface-hierarchy:v2|popover-dialog|structure-border:true|decorative-border:false|accent-restraint:true|content-owned-by-caller:true\""))) {
    return fail("Shared startup popup and popup surface should expose visibility, function-preservation, and hierarchy contracts");
  }
  if (!main_qml_source.contains(QStringLiteral("pageVisibilityAuditContract: \"page-visibility-audit:v1|pages:library,import,edit,masks,carousel,timeline,export|buckets:always,contextual,one-expansion,hover-focus,diagnostic,preserve|runtime-active:true|functionality:preserved|major-shape:preserved\"")) ||
      !main_qml_source.contains(QStringLiteral("function activePageKind()")) ||
      !main_qml_source.contains(QStringLiteral("function activeWorkspaceVisibilityContract()")) ||
      !main_qml_source.contains(QStringLiteral("function pageVisibilityAuditSignature()")) ||
      !main_qml_source.contains(QStringLiteral("\"|coverage:library/import/edit/masks/carousel/timeline/export\"")) ||
      !studio_workspace_source.contains(QStringLiteral("libraryStudioVisibilityContract: \"library-studio-visibility:v1|always:sources-search-filters-grid-inspector|contextual:selection-review-import-browser-batch-actions-empty-offline|one-expansion:project-assignment-import-actions-export-compare-panel-toggles|hover-focus:gallery-actions-review-keyboard|diagnostic:import-progress-offline-state|preserve:functionality\"")) ||
      !studio_workspace_source.contains(QStringLiteral("importStudioVisibilityContract: \"import-studio-visibility:v1|always:back-title-import-workspace-source-browser|contextual:folder-media-selection-destination-progress-empty-error|one-expansion:source-picker-project-target-import-options-refresh|hover-focus:folder-media-selection-keyboard|diagnostic:import-progress-source-empty|preserve:functionality\"")) ||
      !studio_workspace_source.contains(QStringLiteral("maskStudioVisibilityContract: \"mask-studio-visibility:v1|always:mask-tool-rail-image-overlay-inspector-active-target|contextual:source-controls-brush-cursor-lasso-polygon-retouch-hud-pending-actions|one-expansion:mask-source-menu-refine-options-runtime-details-layer-actions|hover-focus:source-row-actions-reorder-visibility-solo-lock|diagnostic:matte-preview-runtime-detail-failures|preserve:functionality\"")) ||
      !studio_workspace_source.contains(QStringLiteral("exportStudioVisibilityContract: \"export-studio-visibility:v1|always:presets-settings-queue-destination-primary-action|contextual:selected-assets-progress-failures-output-summary-empty-queue|one-expansion:queue-actions-preset-management-destination-actions-clear-finished|hover-focus:queue-row-actions-retry-cancel-remove|diagnostic:job-errors-preflight-detail-active-progress|preserve:functionality\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function activeVisibilityContract()"))) {
    return fail("App shell and Studio workspace should expose one aggregate page visibility/function-preservation audit across Library, Import, Edit, Masks, Carousel, Timeline, and Export");
  }
  if (!studio_workspace_source.contains(QStringLiteral("studioLayerTerminologyContract: \"studio-layer-terminology:v1|stack-object:Layer")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"Layer\"")) ||
      !studio_workspace_source.contains(QStringLiteral("label: \"Layer\"")) ||
      !studio_workspace_source.contains(QStringLiteral("tooltipText: \"Add the default layer.\"")) ||
      !studio_workspace_source.contains(QStringLiteral("? \"Linked Layer\"")) ||
      !brush_layer_inspector_header_source.contains(QStringLiteral("property string targetName: \"Layer\"")) ||
      !brush_layer_inspector_header_source.contains(QStringLiteral("tooltipText: \"Add the default layer.\"")) ||
      !keyboard_shortcuts_dialog_source.contains(QStringLiteral("\"description\": \"Create a new selection and linked layer.\"")) ||
      studio_workspace_source.contains(QStringLiteral("return \"Adjustment Layer\"")) ||
      studio_workspace_source.contains(QStringLiteral("return \"Photo Layer\"")) ||
      studio_workspace_source.contains(QStringLiteral("label: \"Adjustment Layer\"")) ||
      studio_workspace_source.contains(QStringLiteral("tooltipText: \"Add the default adjustment layer.\"")) ||
      studio_workspace_source.contains(QStringLiteral("\"Selection adjustment layer\"")) ||
      studio_workspace_source.contains(QStringLiteral("\"Select an adjustment layer\"")) ||
      studio_workspace_source.contains(QStringLiteral("selection-backed adjustment layer")) ||
      studio_workspace_source.contains(QStringLiteral("selected adjustment layers")) ||
      brush_layer_inspector_header_source.contains(QStringLiteral("Adjustment Layer")) ||
      keyboard_shortcuts_dialog_source.contains(QStringLiteral("linked adjustment layer"))) {
    return fail("Studio visible stack-object copy should use Layer rather than implementation-specific adjustment/photo layer terms");
  }
  trace_smoke_progress("layer terminology source checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYER_TERMINOLOGY_SOURCE")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }
  if (!studio_workspace_source.contains(QStringLiteral("property bool studioReducedMotionOverride: false")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool studioPlatformReducedMotionAvailable: platformReducedMotionAvailable()")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool studioPlatformReducedMotion: platformReducedMotionPreference()")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool studioReducedMotion: studioReducedMotionOverride || studioPlatformReducedMotion")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool studioRenderWorkActive: editorPreview.processing")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool studioMotionSuppressedForRender: studioRenderWorkActive")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property int studioMotionFast: studioMotionDurationForState(studioReducedMotion, studioMotionSuppressedForRender, Math.max(120, theme.durationFast))")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property int studioMotionMedium: studioMotionDurationForState(studioReducedMotion, studioMotionSuppressedForRender, theme.durationMedium)")) ||
      !studio_workspace_source.contains(QStringLiteral("function studioMotionDurationForState(reducedMotion, renderWorkActive, defaultDuration)")) ||
      !studio_workspace_source.contains(QStringLiteral("function platformStyleHints()")) ||
      !studio_workspace_source.contains(QStringLiteral("Qt.application.styleHints")) ||
      !studio_workspace_source.contains(QStringLiteral("hints.reducedMotion !== undefined")) ||
      !studio_workspace_source.contains(QStringLiteral("hints.reduceMotion !== undefined")) ||
      !studio_workspace_source.contains(QStringLiteral("hints.prefersReducedMotion !== undefined")) ||
      !studio_workspace_source.contains(QStringLiteral("|render-work:\"")) ||
      !studio_workspace_source.contains(QStringLiteral("|render-suppressed:\"")) ||
      !studio_workspace_source.contains(QStringLiteral("|platform-available:\"")) ||
      !studio_workspace_source.contains(QStringLiteral("|platform-reduced:\"")) ||
      !studio_workspace_source.contains(QStringLiteral("|override:\""))) {
    return fail("Studio motion should honor Qt/platform reduced-motion style hints when exposed and provide a deterministic override for proof");
  }
  trace_smoke_progress("reduced motion source checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_REDUCED_MOTION_SOURCE")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }
  if (!studio_workspace_source.contains(QStringLiteral("toolSwitchLatencyProbeContract: \"tool-switch-latency-probe:v1|monitor:qmlPerformance|surface:studio-tool-switch|start:beginEditToolSwitch|ready:completeEditToolTransition|serial:true|workflow-id:true|safe-when-monitor-missing:true\"")) ||
      !studio_workspace_source.contains(QStringLiteral("firstInteractionResponsivenessContract: \"first-interaction-responsiveness:v1|surface:studio-tool-rail|trigger:sync|first-feedback:transition-active|state:tool-switch-serial|gate:smoke-perf\"")) ||
      !studio_workspace_source.contains(QStringLiteral("previewFeedbackContract: \"preview-feedback:v1|first-frame:controls-crop-brush-mask-layer-compare-tool-switch|states:draft-refining-full-resolution|last-good:preserved|stale:rejected|context:zoom-pan-overlay-tool-preserved|parity:preview-export\"")) ||
      !studio_workspace_source.contains(QStringLiteral("comparisonUxContract: \"comparison-ux:v1|hold:B-and-toolbar|sources:original-external-named-snapshot|layouts:split-wipe-side-by-side|labels:before-after-source|viewport:sync-zoom-pan|release:edited-full-resolution|recipe-history:immutable\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function setOriginalHold(active)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"originalHoldButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"compareSnapshotSelector\"")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"Before · Original\"")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"After · Current edit\"")) ||
      !studio_workspace_source.contains(QStringLiteral("syncViewport: root.syncEnabled")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"Full resolution held • refining\"")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"Draft preview • refining\"")) ||
      !studio_workspace_source.contains(QStringLiteral("property double editToolSwitchStartedAtMs: 0")) ||
      !studio_workspace_source.contains(QStringLiteral("property string editToolSwitchWorkflowId: \"\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function qmlPerformanceMonitorAvailable()")) ||
      !studio_workspace_source.contains(QStringLiteral("function beginToolSwitchLatencyProbe(previousTool, nextTool)")) ||
      !studio_workspace_source.contains(QStringLiteral("function finishToolSwitchLatencyProbe()")) ||
      !studio_workspace_source.contains(QStringLiteral("root.beginToolSwitchLatencyProbe(previousTool, nextTool)")) ||
      !studio_workspace_source.contains(QStringLiteral("root.finishToolSwitchLatencyProbe()")) ||
      !studio_workspace_source.contains(QStringLiteral("qmlPerformance.nowMs()")) ||
      !studio_workspace_source.contains(QStringLiteral("qmlPerformance.routeStarted(\n                    \"studio-tool-switch\"")) ||
      !studio_workspace_source.contains(QStringLiteral("qmlPerformance.routeReady(\n                    \"studio-tool-switch\""))) {
    return fail("Studio tool switching should emit qmlPerformance timing probes for start and ready latency");
  }
  trace_smoke_progress("tool switch latency probe source checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_TOOL_SWITCH_LATENCY_SOURCE")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }
  const auto source_uses_incomplete_text_elide = [](const QString& source) {
    return source.contains(QStringLiteral("Text.ElideRight")) ||
           source.contains(QStringLiteral("Text.ElideMiddle")) ||
           source.contains(QStringLiteral("Text.ElideLeft"));
  };
  if (source_uses_incomplete_text_elide(action_button_source) ||
      source_uses_incomplete_text_elide(mini_button_source) ||
      source_uses_incomplete_text_elide(menu_button_source) ||
      source_uses_incomplete_text_elide(split_button_source) ||
      source_uses_incomplete_text_elide(popup_menu_row_source) ||
      source_uses_incomplete_text_elide(agent_style_tile_source) ||
      source_uses_incomplete_text_elide(app_menu_item_source)) {
    return fail("Shared command, menu, and agent style labels should wrap instead of truncating their action names");
  }
  const QRegularExpression quoted_ellipsis(
      QStringLiteral(R"ellipsis("[^"\n]*\.\.\.[^"\n]*")ellipsis"));
  if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_SKIP_VISIBLE_TEXT_SOURCE_CONTRACT") &&
      (studio_workspace_source.contains(QStringLiteral("...")) ||
      menu_button_source.contains(QStringLiteral("...")) ||
      split_button_source.contains(QStringLiteral("...")) ||
      popup_menu_row_source.contains(QStringLiteral("...")) ||
      tool_hud_source.contains(QStringLiteral("...")) ||
      review_preview_source.contains(QStringLiteral("...")) ||
      agent_edit_controller_source.contains(quoted_ellipsis) ||
      editor_preview_controller_source.contains(quoted_ellipsis) ||
      catalog_asset_model_source.contains(quoted_ellipsis) ||
      !action_button_source.contains(QStringLiteral("property string disabledReason: \"\"")) ||
      !action_button_source.contains(QStringLiteral("property bool allowLabelWrap: false")) ||
      !action_button_source.contains(QStringLiteral("actionButtonContract: \"action-button:v11|shared-component:true|hierarchy:primary-graphite-secondary-neutral-tertiary-quiet|flat-surface:true|accent-restrained:true|unified-shape:true|borderless-rest:true|hover-fill-not-border:true|keyboard:true|focus-visible:true|accessible-name-role-state:true|density-token-heights:true|tooltip-full:true|readable-text:true|no-elide:true|explicit-name-override:true|disabled-reason-keeps-action-name:true\"")) ||
      !action_button_source.contains(QStringLiteral("property string accessibleName: \"\"")) ||
      !action_button_source.contains(QStringLiteral("Accessible.name: root.resolvedAccessibleName")) ||
      !action_button_source.contains(QStringLiteral("readonly property bool accentEdgeVisible: root.enabled && root.clippedShape && root.slashAccent &&")) ||
      !action_button_source.contains(QStringLiteral("background: CommandButtonSurface {")) ||
      !action_button_source.contains(QStringLiteral("fillColor: theme.commandFill(root.tone, root.active, root.hovered, root.down, root.enabled, root.quietTone)")) ||
      !action_button_source.contains(QStringLiteral("borderWidth: theme.commandBorderWidth(root.visualFocus)")) ||
      !action_button_source.contains(QStringLiteral("readableTextContract: \"readable-text:v3|ellipsis:none|primary-fit:true|optional-wrap:true|tooltip-full:true|minimum-px:10\"")) ||
      !action_button_source.contains(QStringLiteral("wrapMode: root.allowLabelWrap ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap")) ||
      !action_button_source.contains(QStringLiteral("maximumLineCount: root.allowLabelWrap ? 2 : 1")) ||
      !action_button_source.contains(QStringLiteral("property string disabledFallbackReason: \"Unavailable in the current state.\"")) ||
      !action_button_source.contains(QStringLiteral("readonly property string resolvedTooltipText: !root.enabled")) ||
      !action_button_source.contains(QStringLiteral("? root.disabledReason\n                                                     : root.disabledFallbackReason)")) ||
      !action_button_source.contains(QStringLiteral("disabledReasonContract: \"disabled-reason:v2|tooltip-when-disabled:true|accessible-description:true|fallback-tooltip:true|fallback-reason:true\"")) ||
      !action_button_source.contains(QStringLiteral("disabledStateMotionContract: \"disabled-state-motion:v1|duration:110|opacity:true|layout-stable:true|tooltip-preserved:true|interruptible:true\"")) ||
      !action_button_source.contains(QStringLiteral("opacity: root.enabled ? 1.0 : 0.74")) ||
      !action_button_source.contains(QStringLiteral("Behavior on opacity {\n        NumberAnimation { duration: theme.durationFast; easing.type: Easing.OutCubic }")) ||
      !action_button_source.contains(QStringLiteral("Accessible.description: !root.enabled")) ||
      !action_button_source.contains(QStringLiteral("root.resolvedTooltipText !== root.resolvedAccessibleName")) ||
      !mini_button_source.contains(QStringLiteral("property bool allowLabelWrap: false")) ||
      !mini_button_source.contains(QStringLiteral("readableTextContract: \"readable-text:v3|ellipsis:none|compact-fit:true|optional-wrap:true|tooltip-full:true|a11y-full:true|minimum-px:10|readable-width:true\"")) ||
      !mini_button_source.contains(QStringLiteral("wrapMode: root.allowLabelWrap ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap")) ||
      !mini_button_source.contains(QStringLiteral("maximumLineCount: root.allowLabelWrap ? 2 : 1")) ||
      !mini_button_source.contains(QStringLiteral("property string disabledReason: \"\"")) ||
      !mini_button_source.contains(QStringLiteral("property string disabledFallbackReason: \"Unavailable in the current state.\"")) ||
      !mini_button_source.contains(QStringLiteral("readonly property string resolvedTooltipText: !root.enabled")) ||
      !mini_button_source.contains(QStringLiteral("? root.disabledReason\n                                                     : root.disabledFallbackReason)")) ||
      !mini_button_source.contains(QStringLiteral("disabledReasonContract: \"disabled-reason:v2|tooltip-when-disabled:true|accessible-description:true|fallback-tooltip:true|fallback-reason:true\"")) ||
      !mini_button_source.contains(QStringLiteral("disabledStateMotionContract: \"disabled-state-motion:v1|duration:110|opacity:true|layout-stable:true|tooltip-preserved:true|interruptible:true\"")) ||
      !mini_button_source.contains(QStringLiteral("opacity: root.enabled ? 1.0 : 0.74")) ||
      !mini_button_source.contains(QStringLiteral("Behavior on opacity {\n        NumberAnimation { duration: theme.durationFast; easing.type: Easing.OutCubic }")) ||
      !mini_button_source.contains(QStringLiteral("Accessible.description: !root.enabled")) ||
      !mini_button_source.contains(QStringLiteral("toolbarButtonContract: \"toolbar-button:v1|baseline-aligned:true|stable-height:true|role:\"")) ||
      !mini_button_source.contains(QStringLiteral("|accent-default:false\"")) ||
      !mini_button_source.contains(QStringLiteral("|subdued-chrome:true|hover-no-shift:true|focus-visible:true|media-subordinate:true\"")) ||
      !mini_button_source.contains(QStringLiteral("toolbarGeometryContract: \"toolbar-button-geometry:v1|height:\"")) ||
      !mini_button_source.contains(QStringLiteral("|icon-size:13|label-fit:true|minimum-readable-width:\"")) ||
      !mini_button_source.contains(QStringLiteral("property int minimumReadableWidth: 0")) ||
      !mini_button_source.contains(QStringLiteral("readonly property real readableImplicitWidth: label.implicitWidth + (iconName.length > 0 ? 42 : 24)")) ||
	      !agent_style_tile_source.contains(QStringLiteral("agentStyleTileContract: \"agent-style-tile:v2|selected-state:true|keyboard:true|hover:true|style-name-wrap:true|responsive-columns:true|minimum-readable-width:240|readable-text:true|no-elide:true|no-clip:true\"")) ||
	      !agent_style_tile_source.contains(QStringLiteral("readableTextContract: \"readable-text:v2|ellipsis:none|style-name-wrap:true|max-lines:4|horizontal-fit:true|tooltip-full:true|accessibility-full:true|no-clip:true\"")) ||
	      !agent_style_tile_source.contains(QStringLiteral("readonly property int minimumReadableWidth: 240")) ||
	      !agent_style_tile_source.contains(QStringLiteral("Layout.minimumWidth: Math.min(root.minimumReadableWidth")) ||
	      !agent_style_tile_source.contains(QStringLiteral("implicitWidth: root.minimumReadableWidth")) ||
	      !agent_style_tile_source.contains(QStringLiteral("readonly property string styleName: String(styleData.name || styleData.id || \"Style\")")) ||
	      !agent_style_tile_source.contains(QStringLiteral("signal selectedRequested(string styleId)")) ||
	      !agent_style_tile_source.contains(QStringLiteral("maximumLineCount: 4")) ||
	      !agent_style_tile_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
	      !agent_style_tile_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
	      !agent_style_tile_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !studio_workspace_source.contains(QStringLiteral("property string agentScopeMode: \"current-photo\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function openAgentRunDialog(explicitScope)")) ||
      !studio_workspace_source.contains(QStringLiteral("root.agentScopeMode = explicitScope === \"current-photo\" || explicitScope === \"selected-assets\" || explicitScope === \"confirmed-assets\"")) ||
      !studio_workspace_source.contains(QStringLiteral("? explicitScope : root.bestAgentScopeMode(root.agentScopeMode)")) ||
      !studio_workspace_source.contains(QStringLiteral("root.agentScopeEstimatedCount(\"filtered-library\") > 0")) ||
      !studio_workspace_source.contains(QStringLiteral("if (scope === \"filtered-library\") {")) ||
      !studio_workspace_source.contains(QStringLiteral("LooksyAssistantDock {")) ||
      !studio_workspace_source.contains(QStringLiteral("onActivated: root.openAgentRunDialog()")) ||
	      !agent_walkthrough_dialog_source.contains(QStringLiteral("surface:crt-with-character|art:procedural-line")) ||
	      !agent_walkthrough_dialog_source.contains(QStringLiteral("entry:left-rail-agent-terminal")) ||
	      !agent_walkthrough_dialog_source.contains(QStringLiteral("inspiration:visual-selection")) ||
	      !studio_workspace_source.contains(QStringLiteral("objectName: \"agentStudioPageLoader\"")) ||
	      !agent_conversation_view_source.contains(QStringLiteral("objectName: \"agentLiveActivityTimeline\"")) ||
	      !agent_conversation_view_source.contains(QStringLiteral("objectName: \"aiEditPlanVerificationSummary\"")) ||
	      !agent_conversation_view_source.contains(QStringLiteral("objectName: \"agentRunGoalLabel\"")) ||
	      !agent_walkthrough_dialog_source.contains(QStringLiteral("scope-override:details")) ||
	      !agent_walkthrough_dialog_source.contains(QStringLiteral("details:progressive-disclosure")) ||
	      !agent_conversation_view_source.contains(QStringLiteral("objectName: \"agentDetailsToggle\"")) ||
	      !agent_conversation_view_source.contains(QStringLiteral("objectName: \"agentDetailsPanel\"")) ||
	      !agent_conversation_view_source.contains(QStringLiteral("objectName: \"agentScopeSelector\"")) ||
	      !agent_conversation_view_source.contains(QStringLiteral("objectName: \"agentAddInspirationButton\"")) ||
	      !agent_conversation_view_source.contains(QStringLiteral("objectName: \"agentPrimaryCreateDraftButton\"")) ||
	      !agent_conversation_view_source.contains(QStringLiteral("text: root.dialog.runButtonLabel()")) ||
	      !agent_conversation_view_source.contains(QStringLiteral("onClicked: root.dialog.triggerPrimaryDirectionAction()")) ||
	      studio_workspace_source.contains(QStringLiteral("id: agentRunDialogComponent")) ||
	      studio_workspace_source.contains(QStringLiteral("objectName: \"agentRunDialogContent\"")) ||
	      studio_workspace_source.contains(QStringLiteral("objectName: \"agentStyleChooser\"")) ||
	      studio_workspace_source.contains(QStringLiteral("objectName: \"agentRunAuditPanel\"")) ||
      !studio_workspace_source.contains(QStringLiteral("harmonyControlTextContract: \"harmony-control-text:v1|preset-flow:full-labels|mode-flow:full-labels|wrap:true|no-slice:true|no-elide:true\"")) ||
      !studio_workspace_source.contains(QStringLiteral("Flow {\n                                        id: harmonyPresetFlow\n                                        objectName: \"harmonyPresetFlow\"\n                                        Layout.fillWidth: true\n                                        Layout.preferredHeight: childrenRect.height")) ||
      !studio_workspace_source.contains(QStringLiteral("text: \"Warm Analogous\"")) ||
      !studio_workspace_source.contains(QStringLiteral("text: \"Soft Complement\"")) ||
      !studio_workspace_source.contains(QStringLiteral("text: \"Cool Triad\"")) ||
      !studio_workspace_source.contains(QStringLiteral("Flow {\n                                        id: harmonyModeFlow\n                                        objectName: \"harmonyModeFlow\"\n                                        Layout.fillWidth: true\n                                        Layout.preferredHeight: childrenRect.height")) ||
      !studio_workspace_source.contains(QStringLiteral("text: modelData")) ||
      studio_workspace_source.contains(QStringLiteral("modelData.slice(0, 4)")) ||
      studio_workspace_source.contains(QStringLiteral("not wired")) ||
      studio_workspace_source.contains(QStringLiteral("not available yet")))) {
    return fail("Visible control text should avoid literal ellipses and use readable fit, wrap, or tooltip-backed contracts");
  }
  if (!menu_button_source.contains(QStringLiteral("disabledStateMotionContract: \"disabled-state-motion:v1|duration:110|opacity:true|layout-stable:true|tooltip-preserved:true|interruptible:true\"")) ||
      !menu_button_source.contains(QStringLiteral("Behavior on opacity {\n        NumberAnimation { duration: theme.durationFast; easing.type: Easing.OutCubic }")) ||
      !split_button_source.contains(QStringLiteral("disabledStateMotionContract: \"disabled-state-motion:v1|duration:110|opacity:true|layout-stable:true|tooltip-preserved:true|interruptible:true\"")) ||
      !split_button_source.contains(QStringLiteral("Behavior on opacity {\n        NumberAnimation { duration: theme.durationFast; easing.type: Easing.OutCubic }"))) {
    return fail("Shared Studio buttons should animate disabled-state opacity without changing layout or losing disabled tooltips");
  }
  trace_smoke_progress("disabled state motion source checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_DISABLED_STATE_MOTION_SOURCE")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }
  if (!action_button_source.contains(QStringLiteral("property string disabledFallbackReason: \"Unavailable in the current state.\"")) ||
      !action_button_source.contains(QStringLiteral("disabledReasonContract: \"disabled-reason:v2|tooltip-when-disabled:true|accessible-description:true|fallback-tooltip:true|fallback-reason:true\"")) ||
      !mini_button_source.contains(QStringLiteral("property string disabledFallbackReason: \"Unavailable in the current state.\"")) ||
      !mini_button_source.contains(QStringLiteral("disabledReasonContract: \"disabled-reason:v2|tooltip-when-disabled:true|accessible-description:true|fallback-tooltip:true|fallback-reason:true\"")) ||
      !popup_menu_row_source.contains(QStringLiteral("readonly property string disabledFallbackReason: \"Unavailable in the current state.\"")) ||
      !popup_menu_row_source.contains(QStringLiteral("popupMenuRowContract: \"popup-menu-row:v3|shared-delegate:true|keyboard:true|hover:true|description:true|disabled-reason:true|disabled-fallback:true")) ||
      !tool_button_source.contains(QStringLiteral("property string disabledFallbackReason: \"Unavailable in the current state.\"")) ||
      !tool_button_source.contains(QStringLiteral("return disabledReason.length > 0 ? disabledReason : disabledFallbackReason")) ||
      !tool_button_source.contains(QStringLiteral("Accessible.description: !enabled || !commandEnabled ? resolvedTooltip : variantLabel")) ||
      !tool_option_toggle_source.contains(QStringLiteral("property string disabledFallbackReason: \"Unavailable in the current state.\"")) ||
      !tool_option_toggle_source.contains(QStringLiteral("readonly property string disabledTooltipText: !toggleEnabled")) ||
      !menu_button_source.contains(QStringLiteral("MenuItemMetrics { id: menuMetrics; items: root.menuItems }")) ||
      !split_button_source.contains(QStringLiteral("MenuItemMetrics { id: menuMetrics; items: root.menuItems }")) ||
      !menu_item_metrics_source.contains(QStringLiteral("String(item.disabledReason || item.reason || \"Unavailable in the current state.\")"))) {
    return fail("Shared disabled actions should always expose a disabled-reason tooltip, using a fallback when a caller omits a specific reason");
  }
  trace_smoke_progress("disabled action tooltip fallback source checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_DISABLED_ACTION_TOOLTIPS_SOURCE")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }
  if (!agent_run_stage_row_source.contains(QStringLiteral("agentRunStageRowContract: \"agent-run-stage-row:v1|status-dot:true|label-fit:true|status-fit:true|hover:true|keyboard-focus:true|readable-text:true|no-elide:true\"")) ||
      !agent_run_stage_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-fit:true|status-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !agent_run_stage_row_source.contains(QStringLiteral("Accessible.description: root.statusText")) ||
      !agent_run_stage_row_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !agent_run_stage_row_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !agent_run_stage_row_source.contains(QStringLiteral("text: root.label + \" - \" + root.statusText"))) {
    return fail("Edit agent run stages should use AgentRunStageRow with readable status text while preserving stage status logic");
  }
  if (!edit_rail_button_source.contains(QStringLiteral("editRailButtonContract: \"edit-rail-button:v2|active-state:true|rounded:true|centered-icons:true|hover:true|keyboard:true|tooltip-full:true|accessibility-full:true|icon-only:true\"")) ||
      !edit_rail_button_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|tooltip-full:true|accessibility-full:true\"")) ||
      !edit_rail_button_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      !edit_rail_button_source.contains(QStringLiteral("Accessible.name: root.label")) ||
      !edit_layout_toggle_row_source.contains(QStringLiteral("editLayoutToggleRowContract: \"edit-layout-toggle-row:v1|active-state:true|hover:true|keyboard:true|readable-text:true|no-elide:true|tooltip-full:true\"")) ||
      !edit_layout_toggle_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !edit_layout_toggle_row_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !edit_layout_toggle_row_source.contains(QStringLiteral("elide: Text.ElideNone"))) {
    return fail("Edit rail and layout controls should use modular no-elide components while preserving mode and layout toggles");
  }
  if (!channel_mixer_button_source.contains(QStringLiteral("channelMixerButtonContract: \"channel-mixer-button:v2|preset-reset:true|keyboard:true|tooltip-full:true|readable-text:true|no-elide:true|action-value:true|icon-only-reset:true\"")) ||
      !channel_mixer_button_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !channel_mixer_button_source.contains(QStringLiteral("ActionButton {")) ||
      !channel_mixer_button_source.contains(QStringLiteral("signal actionRequested(string actionValue)")) ||
      !channel_mixer_button_source.contains(QStringLiteral("property bool iconOnlyAction: false")) ||
      !channel_mixer_button_source.contains(QStringLiteral("iconName: iconOnlyAction ? \"reset\" : \"\"")) ||
      !channel_mixer_button_source.contains(QStringLiteral("showText: !iconOnlyAction")) ||
      !channel_mixer_button_source.contains(QStringLiteral("showFullTextTooltip: true")) ||
      !channel_mixer_button_source.contains(QStringLiteral("Accessible.name: iconOnlyAction && helperText.length > 0 ? helperText : text"))) {
    return fail("Edit channel mixer presets and reset actions should use ChannelMixerButton with readable no-elide labels and preserved preset/reset behavior");
  }
  if (!edit_inspector_tab_button_source.contains(QStringLiteral("editInspectorTabButtonContract: \"edit-inspector-tab-button:v1|selected-state:true|hover:true|keyboard:true|width-to-label:true|readable-text:true|no-elide:true\"")) ||
      !edit_inspector_tab_button_source.contains(QStringLiteral("readableTextContract: \"readable-text:v2|ellipsis:none|label-width:true|label-fit:true|minimum-px:10|tooltip-full:true|accessibility-full:true\"")) ||
      !edit_inspector_tab_button_source.contains(QStringLiteral("implicitWidth: Math.max(54, tabLabel.implicitWidth + theme.space20)")) ||
      !edit_inspector_tab_button_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !edit_inspector_tab_button_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !edit_inspector_tab_button_source.contains(QStringLiteral("Accessible.selected: root.selected")) ||
      !edit_inspector_tab_button_source.contains(QStringLiteral("signal selectedRequested(string tabKey)"))) {
    return fail("Edit inspector tabs should use a modular no-elide tab button while preserving tab and mode switching");
  }
  if (!mask_view_source.contains(QStringLiteral("RetouchBeforeAfterControls {")) ||
      !mask_inspector_source.contains(QStringLiteral("RetouchBeforeAfterControls {")) ||
      !retouch_before_after_source.contains(QStringLiteral("tooltipText: \"Show retouch before\"")) ||
      !retouch_before_after_source.contains(QStringLiteral("Accessible.name: \"Show retouch before\"")) ||
      !retouch_before_after_source.contains(QStringLiteral("tooltipText: \"Show retouch after\"")) ||
      !retouch_before_after_source.contains(QStringLiteral("Accessible.name: \"Show retouch after\"")) ||
      !retouch_before_after_source.contains(QStringLiteral("iconName: \"compare\"\n        showText: false")) ||
      !mask_inspector_source.contains(QStringLiteral("tooltipText: \"Reset mask grade\"")) ||
      !mask_inspector_source.contains(QStringLiteral("tooltipText: \"Reset range refine\"")) ||
      !mask_inspector_source.contains(QStringLiteral("iconName: \"reset\"\n                showText: false"))) {
    return fail("Mask retouch compare and dense reset controls should be icon-first with full tooltip/accessibility text instead of truncated visible labels");
  }
  if (!edit_filmstrip_thumb_source.contains(QStringLiteral("editFilmstripThumbContract: \"edit-filmstrip-thumb:v1|current-state:true|selected-state:true|empty-state:true|keyboard:true|tooltip-full:true|accessibility-full:true\"")) ||
      !edit_filmstrip_thumb_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|tooltip-full:true|accessibility-full:true|image-only:true\"")) ||
      !edit_filmstrip_thumb_source.contains(QStringLiteral("mediaSurfaceContract: \"media-surface:v1|dock-thumbnail:stable|image-dominant:true|selection:single-border|badge-corner:reserved|chrome:quiet\"")) ||
      !edit_filmstrip_thumb_source.contains(QStringLiteral("Accessible.name: accessibleLabel")) ||
      !edit_filmstrip_thumb_source.contains(QStringLiteral("Accessible.selected: root.current || root.selected")) ||
      !edit_filmstrip_thumb_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      !edit_filmstrip_thumb_source.contains(QStringLiteral("signal activated(int assetIndex, int modifiers)"))) {
    return fail("Edit filmstrip should use a shared thumbnail component with current, selected, keyboard, and preserved selection behavior");
  }
  if (!agent_model_row_source.contains(QStringLiteral("agentModelRowContract: \"agent-model-row:v1|selected-state:true|status-state:true|keyboard:true|hover:true|readable-text:true|no-elide:true\"")) ||
      !agent_model_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|name-wrap:true|detail-wrap:true|status-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !agent_model_row_source.contains(QStringLiteral("signal selectedRequested(string modelId)")) ||
      !agent_model_row_source.contains(QStringLiteral("Accessible.selected: selected")) ||
      !agent_model_row_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      !agent_model_row_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !agent_model_row_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !agent_model_row_source.contains(QStringLiteral("elide: Text.ElideNone"))) {
    return fail("Edit agent model rows should use AgentModelRow with readable text, status state, keyboard selection, and preserved selected-model behavior");
  }
  if (!tone_curve_channel_button_source.contains(QStringLiteral("toneCurveChannelButtonContract: \"tone-curve-channel-button:v1|selected-state:true|hover:true|keyboard:true|readable-text:true|no-elide:true|tooltip-full:true\"")) ||
      !tone_curve_channel_button_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !tone_curve_channel_button_source.contains(QStringLiteral("signal selectedRequested(string channelName)")) ||
      !tone_curve_channel_button_source.contains(QStringLiteral("Accessible.selected: root.selected")) ||
      !tone_curve_channel_button_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !tone_curve_channel_button_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !tone_curve_preset_button_source.contains(QStringLiteral("toneCurvePresetButtonContract: \"tone-curve-preset-button:v1|preview-hover:true|select:true|keyboard:true|readable-text:true|no-elide:true|tooltip-full:true\"")) ||
      !tone_curve_preset_button_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !tone_curve_preset_button_source.contains(QStringLiteral("signal previewRequested(string presetName)")) ||
      !tone_curve_preset_button_source.contains(QStringLiteral("signal previewCleared()")) ||
      !tone_curve_preset_button_source.contains(QStringLiteral("signal selectedRequested(string presetName)")) ||
      !tone_curve_preset_button_source.contains(QStringLiteral("onEntered: root.previewRequested(root.presetName)")) ||
      !tone_curve_preset_button_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !tone_curve_editor_source.contains(QStringLiteral("objectName: root.objectName.length > 0 ? root.objectName + \"DeletePointButton\"")) ||
      !tone_curve_editor_source.contains(QStringLiteral("objectName: root.objectName.length > 0 ? root.objectName + \"ResetCurveButton\"")) ||
      !tone_curve_editor_source.contains(QStringLiteral("tooltipText: \"Delete the selected curve point.\"")) ||
      !tone_curve_editor_source.contains(QStringLiteral("tooltipText: \"Reset the active curve.\"")) ||
      !tone_curve_editor_source.contains(QStringLiteral("root.deleteSelectedPoint()")) ||
      !tone_curve_editor_source.contains(QStringLiteral("root.resetActiveCurve()")) ||
      tone_curve_editor_source.contains(QStringLiteral("\n            Button {\n                objectName: root.objectName.length > 0 ? root.objectName + \"DeletePointButton\"")) ||
      tone_curve_editor_source.contains(QStringLiteral("\n            Button {\n                objectName: root.objectName.length > 0 ? root.objectName + \"ResetCurveButton\"")) ||
      !tone_curve_editor_source.contains(QStringLiteral("delegate: ToneCurveChannelButton")) ||
      !tone_curve_editor_source.contains(QStringLiteral("channelName: String(modelData)")) ||
      !tone_curve_editor_source.contains(QStringLiteral("selected: root.selectedChannel === modelData")) ||
      !tone_curve_editor_source.contains(QStringLiteral("root.channelSelected(channelName)")) ||
      !tone_curve_editor_source.contains(QStringLiteral("delegate: ToneCurvePresetButton")) ||
      !tone_curve_editor_source.contains(QStringLiteral("presetName: String(modelData)")) ||
      !tone_curve_editor_source.contains(QStringLiteral("root.previewPreset(presetName)")) ||
      !tone_curve_editor_source.contains(QStringLiteral("root.clearPresetPreview()")) ||
      !tone_curve_editor_source.contains(QStringLiteral("root.presetSelected(presetName)"))) {
    return fail("Tone curve channel and preset controls should use modular no-elide buttons while preserving channel selection, hover preview, preview clear, and preset apply behavior");
  }
  if (!tone_curve_point_handle_source.contains(QStringLiteral("toneCurvePointHandleContract: \"tone-curve-point-handle:v1|selected-state:true|endpoint-state:true|stable-size:true|premium-handle:true\"")) ||
      !tone_curve_point_handle_source.contains(QStringLiteral("width: endpoint ? (selected ? 12 : 10) : (selected ? 11 : 9)")) ||
      !tone_curve_point_handle_source.contains(QStringLiteral("border.color: selected ? theme.polar : theme.alpha(theme.ink950, 0.8)")) ||
      !tone_curve_editor_source.contains(QStringLiteral("delegate: ToneCurvePointHandle")) ||
      !tone_curve_editor_source.contains(QStringLiteral("readonly property bool endpointHandle: index === 0 || index === root.currentPoints().length - 1")) ||
      !tone_curve_editor_source.contains(QStringLiteral("selected: index === interaction.selectedIndex")) ||
      !tone_curve_editor_source.contains(QStringLiteral("endpoint: endpointHandle")) ||
      !tone_curve_editor_source.contains(QStringLiteral("accent: root.accent"))) {
    return fail("Tone curve point handles should use a shared selected/endpoint handle component while preserving drag mechanics");
  }
  if (!agent_conversation_view_source.contains(QStringLiteral("objectName: \"agentScopeSelector\"")) ||
      !agent_conversation_view_source.contains(QStringLiteral("model: root.dialog.availableScopeLabels()")) ||
      studio_workspace_source.contains(QStringLiteral("objectName: \"agentRunModeChooser\"")) ||
      !studio_workspace_source.contains(QStringLiteral("key === \"apply_to_all\"")) ||
      !studio_workspace_source.contains(QStringLiteral("root.agentEditController.applySampleReviewToAll(true)")) ||
      !agent_walkthrough_dialog_source.contains(QStringLiteral("function reviseSamples()")) ||
      !agent_walkthrough_dialog_source.contains(QStringLiteral("root.agentEditController.reviseSampleReview(feedback)")) ||
      !agent_permission_mode_button_source.contains(QStringLiteral("agentPermissionModeButtonContract: \"agent-permission-mode-button:v1|selected-state:true|mode-signal:true|tooltip-full:true|keyboard:true|readable-text:true|no-elide:true\"")) ||
      !agent_permission_mode_button_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !agent_permission_mode_button_source.contains(QStringLiteral("signal modeSelected(string mode)")) ||
      !agent_permission_mode_button_source.contains(QStringLiteral("active: selectedMode === mode")) ||
      !agent_permission_mode_button_source.contains(QStringLiteral("iconName: active ? \"check\" : \"adjust\"")) ||
      !agent_tool_permission_toggle_source.contains(QStringLiteral("agentToolPermissionToggleContract: \"agent-tool-permission-toggle:v1|checked-state:true|keyboard:true|hover:true|disabled-state:true|readable-text:true|no-elide:true\"")) ||
      !agent_tool_permission_toggle_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !agent_tool_permission_toggle_source.contains(QStringLiteral("signal toggled(bool checked)")) ||
      !agent_tool_permission_toggle_source.contains(QStringLiteral("function toggle()")) ||
      !agent_tool_permission_toggle_source.contains(QStringLiteral("Keys.onSpacePressed: root.toggle()")) ||
      !agent_tool_permission_toggle_source.contains(QStringLiteral("Accessible.role: Accessible.CheckBox")) ||
      !agent_tool_permission_toggle_source.contains(QStringLiteral("Accessible.checked: checked")) ||
      !agent_tool_permission_toggle_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !agent_tool_permission_toggle_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !agent_tool_permission_toggle_source.contains(QStringLiteral("text: root.helperText.length > 0 ? root.helperText : root.label")) ||
      !agent_batch_status_row_source.contains(QStringLiteral("agentBatchStatusRowContract: \"agent-batch-status-row:v1|thumbnail:true|status-badge:true|actions:true|details-toggle:true|label-wrap:true|detail-wrap:true|readable-text:true|no-elide:true\"")) ||
      !agent_batch_status_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|asset-wrap:true|detail-wrap:true|suggested-wrap:true|details-wrap:true|badge-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !agent_batch_status_row_source.contains(QStringLiteral("signal fixRequested()")) ||
      !agent_batch_status_row_source.contains(QStringLiteral("signal acceptRequested()")) ||
      !agent_batch_status_row_source.contains(QStringLiteral("signal rollbackRequested()")) ||
      !agent_batch_status_row_source.contains(QStringLiteral("signal detailsToggled()")) ||
      !agent_batch_status_row_source.contains(QStringLiteral("objectName: \"agentReviewIssueThumbnail\"")) ||
      !agent_batch_status_row_source.contains(QStringLiteral("objectName: \"agentReviewIssueThumbnailImage\"")) ||
      !agent_batch_status_row_source.contains(QStringLiteral("objectName: \"agentReviewIssueSuggestedActionLabel\"")) ||
      !agent_batch_status_row_source.contains(QStringLiteral("objectName: \"agentReviewIssueDetailsButton\"")) ||
      !agent_batch_status_row_source.contains(QStringLiteral("objectName: \"agentReviewIssueDetailsLabel\"")) ||
      !agent_batch_status_row_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !agent_batch_status_row_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !agent_batch_status_row_source.contains(QStringLiteral("objectName: \"agentReviewIssueActions\"")) ||
      !agent_batch_status_row_source.contains(QStringLiteral("objectName: \"agentFixReviewIssueButton\"")) ||
      !agent_batch_status_row_source.contains(QStringLiteral("objectName: \"agentAcceptReviewIssueButton\"")) ||
      !agent_batch_status_row_source.contains(QStringLiteral("objectName: \"agentRollbackReviewIssueButton\"")) ||
      !agent_conversation_view_source.contains(QStringLiteral("implicitWidth: Math.max(118, buttonLabel.implicitWidth + 34)")) ||
      !agent_conversation_view_source.contains(QStringLiteral("objectName: \"agentPrimaryCreateDraftButton\"")) ||
      !agent_conversation_view_source.contains(QStringLiteral("objectName: \"aiEditPrimaryReviewButton\"")) ||
      !action_button_source.contains(QStringLiteral("property int minimumReadableWidth: 0")) ||
      !action_button_source.contains(QStringLiteral("minimumReadableWidth,")) ||
      !confirm_action_button_source.contains(QStringLiteral("property int minimumReadableWidth: 0")) ||
      !confirm_action_button_source.contains(QStringLiteral("minimumReadableWidth: root.minimumReadableWidth"))) {
    return fail("Agent chooser and action buttons should use full readable labels, Apply to All workflow wiring, and explicit no-ellipsis widths");
  }
  if (!review_preview_source.contains(QStringLiteral("reviewCardContract: \"review-preview-card:v1|image-dominant:true|selection:\"")) ||
      !review_preview_source.contains(QStringLiteral("|badges:status-chip|no-elide:true|double-click:true\"")) ||
      !review_preview_source.contains(QStringLiteral("StatusChip {")) ||
      !review_preview_source.contains(QStringLiteral("width: implicitWidth")) ||
      !library_view_source.contains(QStringLiteral("delegate: ReviewPreview")) ||
      !studio_workspace_source.contains(QStringLiteral("delegate: ReviewPreview"))) {
    return fail("Shared review cards should use StatusChip badges, no-elide text, and the review card contract in Library and Studio");
  }
  if (!status_chip_source.contains(QStringLiteral("ActionButton {")) ||
      !status_chip_source.contains(QStringLiteral("statusChipContract: \"status-chip:v1|one-line:true|tone:\"")) ||
      !status_chip_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !status_chip_source.contains(QStringLiteral("statusSurfaceContract: \"status-surface:v1|render-import-export-model-runtime-warning|compact-density:true|status-before-action:true|retry-action-supported:\"")) ||
      !status_chip_source.contains(QStringLiteral("property string detailText: \"\"")) ||
      !status_chip_source.contains(QStringLiteral("property int minimumReadableWidth: 72")) ||
      !status_chip_source.contains(QStringLiteral("readonly property bool attentionTone: tone === \"error\" || tone === \"warning\"")) ||
      !status_chip_source.contains(QStringLiteral("Accessible.description: root.detailText.length > 0 ? root.detailText : root.resolvedTooltipText")) ||
      !status_chip_source.contains(QStringLiteral("targetItem: root")) ||
      !status_chip_source.contains(QStringLiteral("text: root.resolvedTooltipText")) ||
      !status_chip_source.contains(QStringLiteral("compact: true")) ||
      !status_chip_source.contains(QStringLiteral("embedded: true")) ||
      !status_chip_source.contains(QStringLiteral("accent: root.toneColor")) ||
      !status_chip_source.contains(QStringLiteral("onClicked: root.actionTriggered()")) ||
      QRegularExpression(QStringLiteral("\\n\\s*Button\\s*\\{")).match(status_chip_source).hasMatch()) {
    return fail("StatusChip should expose compact status-surface contracts, readable full text, calm neutral tone, and shared ActionButton actions with preserved actionTriggered behavior");
  }
  if (!app_icon_source.contains(QStringLiteral("case \"search\":")) ||
      !app_icon_source.contains(QStringLiteral("circle(ctx, w * 0.44, h * 0.44, w * 0.22, false)")) ||
      !app_icon_source.contains(QStringLiteral("line(ctx, w * 0.60, h * 0.60, w * 0.80, h * 0.80)"))) {
    return fail("AppIcon should include a dedicated search glyph for Library search controls");
  }
  if (!inline_activity_indicator_source.contains(QStringLiteral("ActionButton {\n            id: actionButton")) ||
      !activity_banner_source.contains(QStringLiteral("activityBannerContract: \"activity-banner:v1|title-detail-actions-progress|tone:\"")) ||
      !activity_banner_source.contains(QStringLiteral("statusSurfaceContract: \"status-surface:v1|progress-summary:true|actions:right|compact-but-readable:true\"")) ||
      !activity_banner_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|title-wrap:true|detail-wrap:true|tooltip-full:true|accessibility-full:true\"")) ||
      !inline_activity_indicator_source.contains(QStringLiteral("objectName: \"activityCancelButton\"")) ||
      !inline_activity_indicator_source.contains(QStringLiteral("text: root.retryable ? \"Retry\" : \"Cancel\"")) ||
      !inline_activity_indicator_source.contains(QStringLiteral("compact: true")) ||
      !inline_activity_indicator_source.contains(QStringLiteral("embedded: true")) ||
      !inline_activity_indicator_source.contains(QStringLiteral("onClicked: root.retryable ? root.retryRequested() : root.cancelRequested()")) ||
      !inline_activity_indicator_source.contains(QStringLiteral("root.retryRequested()")) ||
      !inline_activity_indicator_source.contains(QStringLiteral("root.cancelRequested()")) ||
      QRegularExpression(QStringLiteral("\\n\\s*Button\\s*\\{")).match(inline_activity_indicator_source).hasMatch()) {
    return fail("Inline activity cancel and retry controls should use shared ActionButton styling while preserving cancel/retry signals");
  }
  if (!import_folder_chip_source.contains(QStringLiteral("importFolderChipContract: \"import-folder-chip:v1|readable-text:true|keyboard:true|hover:true|tooltip-full:true|no-elide:true\"")) ||
      !import_folder_chip_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !import_folder_chip_source.contains(QStringLiteral("required property string fileName")) ||
      !import_folder_chip_source.contains(QStringLiteral("required property string fileUrl")) ||
      !import_folder_chip_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      !import_folder_chip_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !import_image_tile_source.contains(QStringLiteral("importImageTileContract: \"import-image-tile:v1|preview:true|selection:true|readable-text:true|keyboard:true|hover:true|tooltip-full:true\"")) ||
      !import_image_tile_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|file-name-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !import_image_tile_source.contains(QStringLiteral("required property string filePath")) ||
      !import_image_tile_source.contains(QStringLiteral("required property string fileUrl")) ||
      !import_image_tile_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      !import_image_tile_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !library_view_source.contains(QStringLiteral("delegate: ImportFolderChip")) ||
      !library_view_source.contains(QStringLiteral("root.openLocalFolder(folderUrl)")) ||
      !library_view_source.contains(QStringLiteral("delegate: ImportImageTile")) ||
      !library_view_source.contains(QStringLiteral("selected: root.localSelectedFilePath === filePath")) ||
      !library_view_source.contains(QStringLiteral("root.selectLocalFile(filePath)"))) {
    return fail("Library import browser should use shared folder chips and image tiles with readable text, keyboard, hover, and preserved import behavior");
  }
  if (!import_source_row_source.contains(QStringLiteral("importSourceRowContract: \"import-source-row:v1|active-state:true|hover:true|keyboard:true|readable-text:true|no-elide:true\"")) ||
      !import_source_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-wrap:true|subtitle-wrap:true|tooltip-full:true|accessibility-full:true\"")) ||
      !import_source_row_source.contains(QStringLiteral("rowAnatomyContract: \"row-anatomy:v2|content:title-detail-wrap|selection:surface-text|hover:neutral|focus:ring|activation:path\"")) ||
      !import_source_row_source.contains(QStringLiteral("signal activated(string path, string sourceUrl, string label)")) ||
      !import_source_row_source.contains(QStringLiteral("Accessible.description: selected")) ||
      !import_source_row_source.contains(QStringLiteral("radius: theme.radiusControl")) ||
      !import_source_row_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      !import_source_row_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !import_folder_row_source.contains(QStringLiteral("importFolderRowContract: \"import-folder-row:v2|active-state:true|keyboard:true|hover:true|readable-text:true|no-elide:true|selection:surface-text\"")) ||
      !import_folder_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !import_folder_row_source.contains(QStringLiteral("signal activated(string fileUrl)")) ||
      !import_folder_row_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      !import_folder_row_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !import_view_source.contains(QStringLiteral("required property var modelData")) ||
      !import_view_source.contains(QStringLiteral("label: String(sourceDelegate.modelData.label")) ||
      !import_view_source.contains(QStringLiteral("sourceUrl: String(sourceDelegate.modelData.url")) ||
      !import_view_source.contains(QStringLiteral("root.openSource(path, sourceUrl, label)")) ||
      !import_view_source.contains(QStringLiteral("fileName: folderDelegate.fileName")) ||
      !import_view_source.contains(QStringLiteral("fileUrl: folderDelegate.fileUrl")) ||
      !import_view_source.contains(QStringLiteral("function browseForFiles()")) ||
      !import_view_source.contains(QStringLiteral("catalogAssets.pickFiles(\"Choose one or more media files\")")) ||
      !import_view_source.contains(QStringLiteral("text: \"Choose Files\"")) ||
      !import_view_source.contains(QStringLiteral("text: \"Choose Folder\"")) ||
      !import_view_source.contains(QStringLiteral("root.openFolder(fileUrl)"))) {
    return fail("Import workspace source and folder rails should use shared readable rows with keyboard activation and preserved navigation behavior");
  }
  if (!import_view_source.contains(QStringLiteral("importActionContract: \"import-actions:v1|split-button:true|primary:import-to-photo-library|menu:import-selection,import-folder,import-with-preset,select-visible,clear-selection,refresh-sources|disabled-reasons:true|no-clutter:true\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importPrimarySplitButton\"")) ||
      !import_view_source.contains(QStringLiteral("SplitButton {")) ||
      !import_view_source.contains(QStringLiteral("text: root.hasActiveImportJobs() ? \"Importing\" : \"Import to Photo Library\"")) ||
      !import_view_source.contains(QStringLiteral("primaryEnabled: root.canImportSelection")) ||
      !import_view_source.contains(QStringLiteral("menuItems: root.importActionMenuItems()")) ||
      !import_view_source.contains(QStringLiteral("disabledReason: root.hasStagedSelection")) ||
      !import_view_source.contains(QStringLiteral("key: \"import-selection\"")) ||
      !import_view_source.contains(QStringLiteral("key: \"import-folder\"")) ||
      !import_view_source.contains(QStringLiteral("key: \"import-with-preset\"")) ||
      !import_view_source.contains(QStringLiteral("tooltip: \"Open Studio preflight and load a saved import preset.\"")) ||
      !import_view_source.contains(QStringLiteral("key: \"refresh-sources\"")) ||
      !import_view_source.contains(QStringLiteral("onMenuItemTriggered: function(key) { root.handleImportMenuAction(key) }"))) {
    return fail("Import workspace should expose a primary split button with readable import variants, disabled reasons, and preserved action routing");
  }
  if (!library_view_source.contains(QStringLiteral("objectName: \"libraryWorkspace\"")) ||
      !library_view_source.contains(QStringLiteral("libraryVisibilityContract: \"library-visibility:v2|always:header-collections|contextual:media-grid-inspector-carousel-documents-project-collections-selection-import-empty-state|one-expansion:review-filter-selection-actions-import-actions-project-assignment-export|hover-focus:gallery-selection-keyboard-review|diagnostic:offline-state|preserve:functionality\"")) ||
      !library_view_source.contains(QStringLiteral("librarySurfaceContract: \"library-surfaces:v3|browse:images-and-timelines-separated|collections:carousels-groups-recent-import-sessions|filters:search-date-media-project-review-clear|inspector:responsive-preview-metadata-review-source-actions|import-browser:folder-preview-handoff|selection:batch-project-export-delete|no-elide:true|no-overlap:true\"")) ||
      !library_view_source.contains(QStringLiteral("libraryWorkflowPolishContract: \"library-workflow-polish:v1|first-glance:grid-dominant-header-filters-inspector|close-inspection:search-filter-selection-review-inspector-import|clutter:one-primary-selection-action-clear-filters-disabled|interaction:search-filter-select-rate-pick-reject-edit-compare-export|text:no-elide-no-overlap|functionality:preserved\"")) ||
      !library_view_source.contains(QStringLiteral("libraryPageCompletionContract: \"page-completion:v3|library|collections:images-timelines-carousels-groups-imports|content:grid-compare-survey-timeline-groups-import-sessions-empty|inspector:responsive-media-preview-metadata-review-source-batch|toolbar:collection-view-select-refresh-filters|actions:edit-compare-export-group-delete-carousel-open-session|states:default-selected-multi-filtered-import-empty-offline\"")) ||
      !library_view_source.contains(QStringLiteral("function libraryWorkflowPolishSignature()")) ||
      !library_view_source.contains(QStringLiteral("objectName: \"libraryMainSurface\"")) ||
      !library_view_source.contains(QStringLiteral("surfaceContract: \"library-main-surface:v1|browse-dominant:true|filters-visible:true|view-switch-visible:true|selection-contextual:true|import-handoff-contextual:true|no-elide:true|no-overlap:true\"")) ||
      !library_view_source.contains(QStringLiteral("objectName: \"libraryHeaderBar\"")) ||
      !library_view_source.contains(QStringLiteral("viewportContract: \"library-viewport:v1|layout:single-fill-slot|panes:overlaid\"")) ||
      !library_view_source.contains(QStringLiteral("objectName: \"libraryUtilityToolbar\"")) ||
      !library_view_source.contains(QStringLiteral("objectName: \"libraryFilterBar\"")) ||
      !library_view_source.contains(QStringLiteral("objectName: \"librarySelectionActionBar\"")) ||
      !library_view_source.contains(QStringLiteral("objectName: \"libraryGalleryGrid\"")) ||
      !library_view_source.contains(QStringLiteral("objectName: \"libraryReviewLoader\"")) ||
      !library_view_source.contains(QStringLiteral("objectName: \"libraryEmptyState\"")) ||
      !library_view_source.contains(QStringLiteral("objectName: \"libraryImportBrowser\"")) ||
      !library_view_source.contains(QStringLiteral("objectName: \"libraryInspectorPanel\"")) ||
      !library_view_source.contains(QStringLiteral("surfaceContract: \"library-inspector:v1|preview:always|metadata:selection|review:single-selection|source-actions:selection|batch-summary:multi-selection|no-elide:true|no-overlap:true\"")) ||
      !studio_workspace_source.contains(QStringLiteral("libraryWorkflowPolishContract: \"library-workflow-polish:v3|first-glance:justified-grid-dominant-toolbar-import-filters-inspector|close-inspection:search-filter-selection-review-import|clutter:no-empty-sources-rail-contextual-selection-actions|interaction:search-filter-select-review-edit-import-compare-export|text:filenames-sidebar-only|functionality:preserved\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("libraryPageCompletionContract: \"page-completion:v3|library|sidebar:none|content:justified-grid-survey-empty-import|inspector:preview-filename-metadata-review-batch|toolbar:search-filters-import-view-panel-toggles|actions:edit-import-compare-export-select|states:default-selected-multi-filtered-import-empty-offline\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("objectName: \"studioLibraryImportButton\"")) ||
	      studio_workspace_source.contains(QStringLiteral("objectName: \"librarySourcesPanel\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("objectName: \"studioLibraryGalleryGrid\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("function libraryGalleryRows(viewWidth)")) ||
	      !studio_workspace_source.contains(QStringLiteral("property var galleryRows: []")) ||
	      !studio_workspace_source.contains(QStringLiteral("function scheduleGalleryRowsRefresh()")) ||
	      studio_workspace_source.contains(QStringLiteral("readonly property var galleryRows: root.libraryGalleryRows")) ||
	      !studio_workspace_source.contains(QStringLiteral("root.setLibraryGalleryAspectRatio(")) ||
	      !studio_workspace_source.contains(QStringLiteral("function libraryWorkflowPolishSignature()"))) {
    return fail("Library workspace should declare visibility, page-completion, workflow-polish, and preserved-function surfaces for browse, filters, inspector, selection, and import handoff");
  }
  if (!library_view_source.contains(QStringLiteral("librarySearchBehaviorContract: \"library-search:v1|draft-state:true|debounce-ms:180|active-state:field-and-chip|clear-visible-only-when-text:true|enter-applies:true|external-sync:true|no-elide:true|functionality:preserved\"")) ||
      !library_view_source.contains(QStringLiteral("function librarySearchBehaviorSignature()")) ||
      !library_view_source.contains(QStringLiteral("id: librarySearchDebounceTimer")) ||
      !library_view_source.contains(QStringLiteral("objectName: \"librarySearchControl\"")) ||
      !library_view_source.contains(QStringLiteral("objectName: \"librarySearchField\"")) ||
      !library_view_source.contains(QStringLiteral("objectName: \"librarySearchClearButton\"")) ||
      !library_view_source.contains(QStringLiteral("visible: root.librarySearchHasText")) ||
      !library_view_source.contains(QStringLiteral("onAccepted: root.applyLibrarySearchNow()")) ||
      !library_view_source.contains(QStringLiteral("objectName: \"libraryActiveSearchChip\"")) ||
      !studio_workspace_source.contains(QStringLiteral("librarySearchBehaviorContract: \"library-search:v1|draft-state:true|debounce-ms:180|active-state:field-and-chip|clear-visible-only-when-text:true|enter-applies:true|external-sync:true|no-elide:true|functionality:preserved\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function librarySearchBehaviorSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("function clearLibraryFilters()")) ||
      !studio_tiny_input_source.contains(QStringLiteral("readonly property string tinyInputContract: \"tiny-input:v1|active-highlight:true|enter-accepted:true|right-inset:true|no-elide:true\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"studioLibrarySearchInput\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"studioLibrarySearchClearButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: root.librarySearchHasText")) ||
      !studio_workspace_source.contains(QStringLiteral("onAccepted: root.applyLibrarySearchNow()")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"studioLibraryActiveSearchChip\""))) {
    return fail("Library search should use debounced draft state, explicit active search affordances, clear buttons visible only with search text, and no-elide readable status chips");
  }
  if (!import_view_source.contains(QStringLiteral("objectName: \"importWorkspace\"")) ||
      !import_view_source.contains(QStringLiteral("importVisibilityContract: \"import-visibility:v1|always:sources-folders-header-media-grid-primary-import|contextual:destination-panel-selection-actions-filter-empty-progress|one-expansion:import-options-project-target-refresh-sources-clear-selection|hover-focus:media-selection-source-folder-keyboard|diagnostic:import-progress-source-empty|preserve:functionality\"")) ||
      !import_view_source.contains(QStringLiteral("importSurfaceContract: \"import-surfaces:v1|sources:left-rail|folders:navigation-list|media:grid-filter-selection|destination:contextual-project-picker|actions:split-import-menu|no-elide:true|no-overlap:true\"")) ||
      !import_view_source.contains(QStringLiteral("importWorkflowPolishContract: \"import-workflow-polish:v1|first-glance:sources-current-folder-media-grid-primary-import|close-inspection:source-confirmation-filter-counts-selection-destination-progress|clutter:single-progress-place-split-import-options|interaction:choose-source-select-visible-clear-import-retry-refresh|text:no-elide-no-overlap-paths-wrap|functionality:preserved\"")) ||
      !import_view_source.contains(QStringLiteral("importPageCompletionContract: \"page-completion:v1|import|sidebar:sources-folders-refresh|content:header-filter-media-grid-empty|destination:project-picker-create-confirm-cancel|toolbar:select-visible-clear-primary-import|states:no-source-empty-filtered-selected-importing-failed-complete\"")) ||
      !import_view_source.contains(QStringLiteral("importSafetyContract: \"import-safety:v1|preflight:source-destination-mode-count-types-size|problems:duplicates-unsupported-unreadable-raw-jpeg|organization:folders-filenames|modes:managed-copy-referenced-link|duplicate-policy:skip-replace|progress:nonblocking|summary:success-skipped-duplicates-failures|retry:job-queue\"")) ||
      !import_view_source.contains(QStringLiteral("catalogAssets.requestImportPreflight(")) ||
      !import_view_source.contains(QStringLiteral("catalogAssets.importSelectionWithOptions(")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importManagedModeButton\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importReferencedModeButton\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importPreflightFacts\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importDuplicateSkipButton\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importDuplicateReplaceButton\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importPreflightPrimaryActions\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importPreflightConfirmButton\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importProgressSummaryCard\"")) ||
      !import_view_source.contains(QStringLiteral("jobQueue.pauseJob(")) ||
      !import_view_source.contains(QStringLiteral("jobQueue.resumeJob(")) ||
      !import_view_source.contains(QStringLiteral("jobQueue.cancelJob(")) ||
      !import_view_source.contains(QStringLiteral("jobQueue.retryJob(")) ||
      !import_view_source.contains(QStringLiteral("function importWorkflowPolishSignature()")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importSourcePanel\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importSourceList\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importFolderNavigation\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importFolderList\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importMainPanel\"")) ||
      !import_view_source.contains(QStringLiteral("surfaceContract: \"import-main-panel:v1|header:always|media-grid:always|filter:always|selection-actions:always|empty-states:contextual|primary-import:always|no-elide:true|no-overlap:true\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importHeaderBar\"")) ||
      !import_view_source.contains(QStringLiteral("function sourceConfirmationText()")) ||
      !import_view_source.contains(QStringLiteral("function selectionMixText()")) ||
      !import_view_source.contains(QStringLiteral("function supportedImportTypesText()")) ||
      !import_view_source.contains(QStringLiteral("readonly property int selectedPhotoCount")) ||
      !import_view_source.contains(QStringLiteral("readonly property int selectedVideoCount")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importSourceConfirmationChip\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importSelectionMixChip\"")) ||
      !import_view_source.contains(QStringLiteral("root.supportedImportTypesText()")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importMediaFilterBar\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importMediaGrid\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importEmptyState\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importNoSourceState\"")) ||
      !import_view_source.contains(QStringLiteral("objectName: \"importDestinationPanel\"")) ||
      !studio_workspace_source.contains(QStringLiteral("importWorkflowPolishContract: \"import-workflow-polish:v1|first-glance:library-routed-import-source-browser|close-inspection:source-folder-media-destination-progress|clutter:library-chrome-quiet-import-dominant|interaction:back-source-select-import-refresh|text:no-elide-no-overlap|functionality:preserved\"")) ||
      !studio_workspace_source.contains(QStringLiteral("importPageCompletionContract: \"page-completion:v1|import-in-library|sidebar:quiet-hidden-during-import|content:ImportView|toolbar:back-title|states:no-source-empty-selected-importing-failed-complete|return:back-to-library\""))) {
    return fail("Import workspace should declare visibility, page-completion, workflow-polish, and preserved-function surfaces for sources, folders, media selection, destination, and import actions");
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_IMPORT_SAFETY_SOURCE")) {
    return EXIT_SUCCESS;
  }
  if (!studio_workspace_source.contains(QStringLiteral("offlineRecoveryContract: \"offline-recovery:v1|states:drive-offline,moved-file,permission-denied,permanently-missing|surfaces:library-edit-export|preserve:metadata-thumbnail-recipe-identity|actions:recheck-relink-selected-relink-all-mark-missing|default:relink-not-delete|blocked-jobs:auto-resume-after-relink\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function chooseOfflineRecoveryFolder(selectedOnly)")) ||
      !studio_workspace_source.contains(QStringLiteral("function confirmSelectedOriginalPermanentlyMissing()")) ||
      !studio_workspace_source.contains(QStringLiteral("StudioOfflineRecoveryBanner {")) ||
      !studio_offline_recovery_source.contains(QStringLiteral("\"offlineRecoveryBanner\"")) ||
      !studio_offline_recovery_source.contains(QStringLiteral("\"offlineRelinkSelectedButton\"")) ||
      !studio_offline_recovery_source.contains(QStringLiteral("\"offlineRelinkAllButton\"")) ||
      !studio_offline_recovery_source.contains(QStringLiteral("\"offlineRecheckButton\"")) ||
      !studio_offline_recovery_source.contains(QStringLiteral("\"offlineMarkMissingButton\"")) ||
      !studio_offline_recovery_source.contains(QStringLiteral("signal relinkSelectedRequested()")) ||
      !studio_offline_recovery_source.contains(QStringLiteral("signal relinkAllRequested()")) ||
      !studio_offline_recovery_source.contains(QStringLiteral("signal recheckRequested()")) ||
      !studio_offline_recovery_source.contains(QStringLiteral("signal markMissingRequested()")) ||
      !studio_workspace_source.contains(QStringLiteral("catalogAssets.markSelectedAssetPermanentlyMissing(true)")) ||
      !export_view_source.contains(QStringLiteral("offlineRecoveryContract: \"offline-recovery:v1|surface:export|states:drive-offline,moved-file,permission-denied,permanently-missing|preserve:metadata-thumbnail-recipe-identity|actions:recheck-relink-selected-relink-all-mark-missing|default:relink-not-delete|blocked-jobs:auto-resume-after-relink\"")) ||
      !export_view_source.contains(QStringLiteral("StudioOfflineRecoveryBanner {")) ||
      !export_view_source.contains(QStringLiteral("pageName: \"export\"")) ||
      !studio_offline_recovery_source.contains(QStringLiteral("\"exportOfflineRecoveryBanner\"")) ||
      !studio_offline_recovery_source.contains(QStringLiteral("\"exportOfflineRelinkSelectedButton\"")) ||
      !studio_offline_recovery_source.contains(QStringLiteral("\"exportOfflineRelinkAllButton\"")) ||
      !studio_offline_recovery_source.contains(QStringLiteral("\"exportOfflineRecheckButton\"")) ||
      !studio_offline_recovery_source.contains(QStringLiteral("\"exportOfflineMarkMissingButton\""))) {
    std::cerr
        << "offline_recovery_workspace_contract="
        << (studio_workspace_source.contains(QStringLiteral("offlineRecoveryContract:")) &&
            studio_workspace_source.contains(QStringLiteral("function chooseOfflineRecoveryFolder(selectedOnly)")) &&
            studio_workspace_source.contains(QStringLiteral("function confirmSelectedOriginalPermanentlyMissing()")) &&
            studio_workspace_source.contains(QStringLiteral("StudioOfflineRecoveryBanner {")) &&
            studio_workspace_source.contains(QStringLiteral("catalogAssets.markSelectedAssetPermanentlyMissing(true)")))
        << " component_contract="
        << (studio_offline_recovery_source.contains(QStringLiteral("\"offlineRecoveryBanner\"")) &&
            studio_offline_recovery_source.contains(QStringLiteral("\"offlineRelinkSelectedButton\"")) &&
            studio_offline_recovery_source.contains(QStringLiteral("signal relinkSelectedRequested()")) &&
            studio_offline_recovery_source.contains(QStringLiteral("signal markMissingRequested()")))
        << " export_contract="
        << (export_view_source.contains(QStringLiteral("StudioOfflineRecoveryBanner {")) &&
            studio_offline_recovery_source.contains(QStringLiteral("\"exportOfflineRecoveryBanner\"")))
        << '\n';
    return fail("Offline-original recovery should expose classified, non-destructive relink actions in Library, Edit, and Export while preserving catalog data and resuming blocked jobs");
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_OFFLINE_RECOVERY_SOURCE")) {
    return EXIT_SUCCESS;
  }
  if (!project_membership_row_source.contains(QStringLiteral("projectMembershipRowContract: \"project-membership-row:v1|name-wrap:true|summary-wrap:true|action-state:true|keyboard:true|hover:true|readable-text:true|no-elide:true\"")) ||
      !project_membership_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|name-wrap:true|summary-wrap:true|tooltip-full:true|accessibility-full:true\"")) ||
      !project_membership_row_source.contains(QStringLiteral("rowAnatomyContract: \"row-anatomy:v1|state-edge:left|content:name-summary-wrap|action-zone:fixed-112|selection:left-edge|hover:neutral|focus:ring\"")) ||
      !project_membership_row_source.contains(QStringLiteral("readonly property int actionZoneWidth: 112")) ||
      !project_membership_row_source.contains(QStringLiteral("readonly property string actionLabel: fullyApplied ? \"Remove\" : (membershipCount > 0 ? \"Add Missing\" : \"Add\")")) ||
      !project_membership_row_source.contains(QStringLiteral("signal toggleRequested(string projectName)")) ||
      !project_membership_row_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      !project_membership_row_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !project_membership_row_source.contains(QStringLiteral("ActionButton {")) ||
      !project_membership_row_source.contains(QStringLiteral("minimumReadableWidth: root.actionZoneWidth")) ||
      !library_view_source.contains(QStringLiteral("delegate: ProjectMembershipRow")) ||
      !library_view_source.contains(QStringLiteral("summary: root.projectMembershipSummary(modelData)")) ||
      !library_view_source.contains(QStringLiteral("membershipCount: root.selectedProjectMembershipCount(modelData)")) ||
      !library_view_source.contains(QStringLiteral("selectedCount: catalogAssets.selectedAssetCount")) ||
      !library_view_source.contains(QStringLiteral("root.toggleSelectedProjectMembership(projectName)"))) {
    return fail("Library project membership rows should use ProjectMembershipRow with readable names, summaries, keyboard toggle, and preserved Add/Remove behavior");
  }
  if (!export_queue_row_source.contains(QStringLiteral("exportQueueRowContract: \"export-queue-row:v1|preview:\"")) ||
      !export_queue_row_source.contains(QStringLiteral("|status-chip:true|progress:\"")) ||
      !export_queue_row_source.contains(QStringLiteral("|readable-text:true|status-surface:true|calm-row:true|destination:\"")) ||
      !export_queue_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|title-wrap:true|destination-wrap:true|metadata-fit:true|tooltip-full:true\"")) ||
      !export_queue_row_source.contains(QStringLiteral("statusSurfaceContract: \"status-surface:v2|export-queue|preview-status-progress-actions|status-before-actions:true|error-diagnostic:true|retry-menu:true|actions-hover-focus:true|attention-actions-visible:true|fixed-action-zone:true\"")) ||
      !export_queue_row_source.contains(QStringLiteral("queueActionVisibilityContract: \"queue-row-actions:v1|visibility:hover-focus-or-attention|fixed-width:172|no-layout-shift:true|keyboard-row-focus:true|remove-preserved:true|menu-preserved:true\"")) ||
      !export_queue_row_source.contains(QStringLiteral("readonly property bool actionsNeedAttention")) ||
      !export_queue_row_source.contains(QStringLiteral("readonly property bool actionsRevealed")) ||
      !export_queue_row_source.contains(QStringLiteral("readonly property int actionZoneWidth: (menuVisible || showRemoveButton) ? 172 : 0")) ||
      !export_queue_row_source.contains(QStringLiteral("activeFocusOnTab: true")) ||
      !export_queue_row_source.contains(QStringLiteral("HoverHandler { id: rowHover }")) ||
      !export_queue_row_source.contains(QStringLiteral("objectName: \"exportQueueRowActionZone\"")) ||
      !export_queue_row_source.contains(QStringLiteral("opacity: root.actionsRevealed ? 1.0 : 0.0")) ||
      !export_queue_row_source.contains(QStringLiteral("Layout.preferredWidth: root.actionZoneWidth")) ||
      !export_queue_row_source.contains(QStringLiteral("Accessible.name: root.title + \", \" + root.statusText")) ||
      !export_queue_row_source.contains(QStringLiteral("detailText: root.errorText")) ||
      !export_queue_row_source.contains(QStringLiteral("StatusChip {")) ||
      !export_queue_row_source.contains(QStringLiteral("ProgressStrip {")) ||
      !export_queue_row_source.contains(QStringLiteral("MenuButton {")) ||
      !export_queue_row_source.contains(QStringLiteral("ConfirmActionButton {")) ||
      !export_view_source.contains(QStringLiteral("delegate: ExportQueueRow")) ||
      !studio_workspace_source.contains(QStringLiteral("ExportQueueRow {")) ||
      !studio_workspace_source.contains(QStringLiteral("function exportQueueStatusTone(status)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"exportQueueRow\""))) {
    return fail("Export and Studio queue rows should share ExportQueueRow with status, progress, actions, and readable text contracts");
  }
  if (!export_view_source.contains(QStringLiteral("exportActionContract: \"export-actions:v3|split-button:true|primary:start-current-or-selected|clear:queue-management-only|menu:queue-current,queue-selected,browse-destination,reset-destination|cleanup-menu:clear-finished|disabled-reasons:true|preflight-aware:true|no-clutter:true|label-intent:start-export\"")) ||
      !export_view_source.contains(QStringLiteral("exportActionHierarchyContract: \"export-action-hierarchy:v3|primary:start-export-split|primary-menu-clean:true|secondary:queue-management-menu|clear:secondary-cleanup|retry:row-or-queue-menu|destination:secondary-menu|one-primary:true|clear-not-primary:true\"")) ||
      !export_view_source.contains(QStringLiteral("exportWorkflowPolishContract: \"export-workflow-polish:v1|first-glance:presets-selection-queue-settings-primary-action|close-inspection:preset-destination-preflight-progress-errors-output-summary|clutter:secondary-management-in-menus-row-actions-contextual|interaction:select-preset-destination-add-queue-start-retry-cancel-clear-reveal|text:no-elide-no-overlap|functionality:preserved\"")) ||
      !export_view_source.contains(QStringLiteral("exportPageCompletionContract: \"page-completion:v1|export|sidebar:presets-destinations|content:selected-assets-queue-progress|settings:preset-color-crop-destination-preflight|toolbar:add-assets-start-export-clear-queue|states:empty-ready-warning-blocked-running-complete-failed\"")) ||
      !export_view_source.contains(QStringLiteral("function exportWorkflowPolishSignature()")) ||
      !export_view_source.contains(QStringLiteral("function exportActionHierarchySignature()")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportBatchSplitButton\"")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportCurrentSplitButton\"")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportQueueManagementMenuButton\"")) ||
      !export_view_source.contains(QStringLiteral("tone: \"secondary\"\n                    compact: true\n                    active: false")) ||
      !export_view_source.contains(QStringLiteral("? \"Start \" + root.currentBatchPreflight().readyCount + \" Ready\"")) ||
      !export_view_source.contains(QStringLiteral("text: \"Start Export\"")) ||
      !export_view_source.contains(QStringLiteral("label: \"Start Current Export\"")) ||
      !export_view_source.contains(QStringLiteral("label: \"Start Selected Exports\"")) ||
      !export_view_source.contains(QStringLiteral("text: \"Clear Done\"")) ||
      !export_view_source.contains(QStringLiteral("menuItems: root.exportActionMenuItems()")) ||
      !export_view_source.contains(QStringLiteral("disabledReason: root.exportActionGuidance()")) ||
      !export_view_source.contains(QStringLiteral("const reason = exportDisabledReason(hasBatchSelection)")) ||
      !export_view_source.contains(QStringLiteral("key: \"queue-current\"")) ||
      !export_view_source.contains(QStringLiteral("key: \"queue-selected\"")) ||
      !export_view_source.contains(QStringLiteral("key: \"browse-destination\"")) ||
      !export_view_source.contains(QStringLiteral("key: \"clear-finished\"")) ||
      !export_view_source.contains(QStringLiteral("\"|hierarchy:\" + root.exportActionHierarchySignature()")) ||
      !export_view_source.contains(QStringLiteral("\"|primary:start-export-split\"")) ||
      !export_view_source.contains(QStringLiteral("\"|primary-menu-clean:\" + String(actionItems.map(function(item) { return String(item.key || \"\") }).indexOf(\"clear-finished\") < 0)")) ||
      !export_view_source.contains(QStringLiteral("\"|clear:secondary-cleanup\"")) ||
      !export_view_source.contains(QStringLiteral("onMenuItemTriggered: function(key) { root.handleExportMenuAction(key) }"))) {
    return fail("Export workspace should expose preflight-aware split buttons with batch/current variants, disabled reasons, and preserved action routing");
  }
  if (!export_view_source.contains(QStringLiteral("objectName: \"exportColorSpaceSection\"")) ||
      !export_view_source.contains(QStringLiteral("title: \"Color space\"")) ||
      !export_view_source.contains(QStringLiteral("bitDepth: currentPresetDetail().bitDepth")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportPreflightStatusLabel\"")) ||
      !export_view_source.contains(QStringLiteral("exportPreflightSection.preflight.outputColorSpace")) ||
      !export_view_source.contains(QStringLiteral("exportPreflightSection.preflight.bitDepth")) ||
      !export_view_source.contains(QStringLiteral("Soft proof \" + exportPreflightSection.preflight.softProofProfile")) ||
      !studio_workspace_source.contains(QStringLiteral("developerDiagnosticsMenuContract: \"developer-diagnostics-menu:v3|image-inspection:user-accessible|advanced-details:developer-mode-gated|canvas-overlays:collapsed|focus:false-color:gamut:zone:diagnostic-only|hidden-during-mask-work:true|hidden-during-primary-hud:true|default-visible:false\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"diagnosticsOverlayButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"gamutOverlayButton\"")) ||
      !studio_diagnostic_overlay_source.contains(QStringLiteral("objectName: \"gamutOverlayTargetLabel\"")) ||
      !studio_diagnostic_overlay_source.contains(QStringLiteral("required property real contentX")) ||
      !studio_diagnostic_overlay_source.contains(QStringLiteral("required property bool highCostOverlaysVisible")) ||
      !studio_diagnostic_overlay_source.contains(QStringLiteral("required property var gamutSource")) ||
      studio_diagnostic_overlay_source.contains(QStringLiteral("required property Item workspace")) ||
      studio_diagnostic_overlay_source.contains(QStringLiteral("required property Item viewport")) ||
      !studio_diagnostic_overlay_source.contains(QStringLiteral("\"Output gamut  \" + overlayStack.outputColorSpace")) ||
      !studio_workspace_source.contains(QStringLiteral("function proofingStatusText()")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.softProofEnabled")) ||
      !studio_workspace_source.contains(QStringLiteral("StudioColorManagementPanel {")) ||
      !studio_color_management_source.contains(QStringLiteral("objectName: \"studioPreviewOutputModeCombo\"")) ||
      !studio_color_management_source.contains(QStringLiteral("model: panel.editorPreview.previewOutputModeOptions")) ||
      !studio_color_management_source.contains(QStringLiteral("objectName: \"studioReferenceDisplayTargetCombo\"")) ||
      !studio_color_management_source.contains(QStringLiteral("panel.editorPreview.referenceDisplayTarget = currentText")) ||
      !studio_color_management_source.contains(QStringLiteral("objectName: \"studioProofRenderIntentCombo\"")) ||
      !studio_color_management_source.contains(QStringLiteral("panel.editorPreview.proofSimulatePaperWhite")) ||
      !studio_color_management_source.contains(QStringLiteral("panel.editorPreview.proofSimulateInkBlack")) ||
      !studio_color_management_source.contains(QStringLiteral("function openIccProfileDialog(slot, relink)")) ||
      !studio_color_management_source.contains(QStringLiteral("panel.editorPreview.importIccProfileAsset")) ||
      !studio_color_management_source.contains(QStringLiteral("panel.editorPreview.relinkIccProfileAsset")) ||
      !studio_color_management_source.contains(QStringLiteral("panel.editorPreview.clearIccProfileAsset")) ||
      !studio_color_management_source.contains(QStringLiteral("function openDcpProfileDialog(relink)")) ||
      !studio_color_management_source.contains(QStringLiteral("panel.editorPreview.importDcpProfileAsset")) ||
      !studio_color_management_source.contains(QStringLiteral("panel.editorPreview.relinkDcpProfileAsset")) ||
      !studio_color_management_source.contains(QStringLiteral("panel.editorPreview.clearDcpProfileAsset"))) {
    return fail("Color profile, soft-proof, bit-depth, and gamut diagnostics should be reachable without cluttering default edit/export workflows");
  }
  if (!export_view_source.contains(QStringLiteral("objectName: \"exportWorkspace\"")) ||
      !export_view_source.contains(QStringLiteral("exportVisibilityContract: \"export-visibility:v2|always:config-panel-selection-preset-destination-primary-action-layout-menu|configurable:preview-queue-panel|contextual:preflight-batch-feedback-empty-queue-progress-errors|one-expansion:export-options-queue-management-destination-reset-clear-finished|hover-focus:queue-row-actions-remove-retry-cancel|diagnostic:job-errors-preflight-blockers-active-progress|preserve:functionality\"")) ||
      !export_view_source.contains(QStringLiteral("exportSurfaceContract: \"export-surfaces:v1|queue:jobs-and-progress|config:selection-preset-color-crop-destination|preflight:blockers-visible|primary-action:split-export|queue-management:menu|no-elide:true|no-overlap:true\"")) ||
      !studio_workspace_source.contains(QStringLiteral("exportWorkflowPolishContract: \"export-workflow-polish:v1|first-glance:presets-assets-queue-settings-primary-action|close-inspection:preset-destination-progress-errors-output-summary|clutter:secondary-management-in-menus-row-actions-contextual|interaction:select-preset-destination-add-queue-start-retry-cancel-clear-reveal|text:no-elide-no-overlap|functionality:preserved\"")) ||
      !studio_workspace_source.contains(QStringLiteral("exportPageCompletionContract: \"page-completion:v1|export|sidebar:presets-destinations|content:selected-assets-queue-progress|settings:preset-output-destination-summary|toolbar:add-assets-start-export-clear-queue|states:empty-ready-running-complete-failed\"")) ||
      !studio_workspace_source.contains(QStringLiteral("studioExportActionHierarchyContract: \"export-action-hierarchy:v2|primary:start-export-split|primary-menu-clean:true|secondary:queue-actions-menu|clear:secondary-cleanup|retry:queue-actions-menu|preset-management:secondary-menu|one-primary:true|clear-not-primary:true\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function exportWorkflowPolishSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("function studioExportActionHierarchySignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("function studioExportActionMenuItems()")) ||
      !studio_workspace_source.contains(QStringLiteral("function studioExportQueueManagementMenuItems()")) ||
      !studio_workspace_source.contains(QStringLiteral("function handleStudioExportAction(key)")) ||
      !studio_workspace_source.contains(QStringLiteral("function handleStudioExportQueueManagementAction(key)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"studioExportPrimarySplitButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("primaryEnabled: root.studioExportCanQueueCurrent() &&")) ||
      !studio_workspace_source.contains(QStringLiteral("disabledReason: root.studioExportDisabledReason(false)")) ||
      !studio_workspace_source.contains(QStringLiteral("menuItems: root.studioExportActionMenuItems()")) ||
      !studio_workspace_source.contains(QStringLiteral("onMenuItemTriggered: function(key) { root.handleStudioExportAction(key) }")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"exportQueueManagementMenuButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("tooltipText: \"Queue retry and cleanup actions.\"")) ||
      !studio_workspace_source.contains(QStringLiteral("menuItems: root.studioExportQueueManagementMenuItems()")) ||
      !studio_workspace_source.contains(QStringLiteral("onMenuItemTriggered: function(key) { root.handleStudioExportQueueManagementAction(key) }")) ||
      !studio_workspace_source.contains(QStringLiteral("key: \"queue-current\"")) ||
      !studio_workspace_source.contains(QStringLiteral("key: \"queue-selected\"")) ||
      !studio_workspace_source.contains(QStringLiteral("key: \"clear-finished\"")) ||
      !studio_workspace_source.contains(QStringLiteral("key: \"retry-failed\"")) ||
      !studio_workspace_source.contains(QStringLiteral("label: \"Export Selected Assets\"")) ||
      !studio_workspace_source.contains(QStringLiteral("label: \"Clear Finished Jobs\"")) ||
      !studio_workspace_source.contains(QStringLiteral("destructive: true")) ||
      !studio_workspace_source.contains(QStringLiteral("\"|hierarchy:\" + root.studioExportActionHierarchySignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("\"|primary-menu-clean:\" + String(actionItems.map(function(item) { return String(item.key || \"\") }).indexOf(\"clear-finished\") < 0)")) ||
      !studio_workspace_source.contains(QStringLiteral("\"|clear:secondary-cleanup\"")) ||
      !studio_workspace_source.contains(QStringLiteral("signal presetManagementRequested(string action)")) ||
      !studio_workspace_source.contains(QStringLiteral("key: \"save-current\", label: \"Save Current Settings\"")) ||
      !studio_workspace_source.contains(QStringLiteral("key: \"manage-saved\", label: \"Manage Saved Presets\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onMenuItemTriggered: function(key) { root.presetManagementRequested(key) }")) ||
      !studio_workspace_source.contains(QStringLiteral("function failedExportJobCount()")) ||
      !studio_workspace_source.contains(QStringLiteral("function retryFailedExportJobs()")) ||
      !studio_workspace_source.contains(QStringLiteral("const writable = exportJobs && exportJobs.historyMutationsEnabled")) ||
      !studio_workspace_source.contains(QStringLiteral("enabled: writable && failedCount > 0")) ||
      !studio_workspace_source.contains(QStringLiteral("root.retryFailedExportJobs()")) ||
      !main_qml_source.contains(QStringLiteral("property string pendingEditInspectorTab: \"\"")) ||
      !main_qml_source.contains(QStringLiteral("function openEditPresetPanelFromExport(action)")) ||
      !main_qml_source.contains(QStringLiteral("pendingEditInspectorTab = \"presets\"")) ||
      !main_qml_source.contains(QStringLiteral("function onPresetManagementRequested(action)")) ||
      !export_workspace_host_source.contains(QStringLiteral("onPresetManagementRequested: function(action)")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportMainWorkspace\"")) ||
      !export_view_source.contains(QStringLiteral("surfaceContract: \"export-main-workspace:v2|preview:configurable|queue-panel:configurable|configuration-panel:always|primary-action:always|progress-contextual:true|failure-contextual:true|no-elide:true|no-overlap:true\"")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportQueuePanel\"")) ||
      !export_view_source.contains(QStringLiteral("surfaceContract: \"export-queue-panel:v1|queue-list:always|empty-state:contextual|management:one-expansion|row-actions:hover-focus|progress:contextual|errors:diagnostic|no-elide:true\"")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportQueueEmptyState\"")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportQueueRepeater\"")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportConfigurationPanel\"")) ||
      !export_view_source.contains(QStringLiteral("surfaceContract: \"export-configuration-panel:v1|selection:always|preset:always|color-space:always|crop:always|destination:always|preflight:contextual|primary-actions:always|no-elide:true|no-overlap:true\"")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportSelectionSection\"")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportPresetSection\"")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportColorSpaceSection\"")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportCropSection\"")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportDestinationSection\"")) ||
      !export_view_source.contains(QStringLiteral("visibilityRole: \"contextual|delivery-destination-filenames-collisions-blockers-profile-metadata-sharpening\""))) {

    return fail("Export workspace should declare always-visible, contextual, one-expansion, hover/focus, diagnostic, and preserved-function surfaces for queue, configuration, preflight, destination, progress, and split export actions");
  }
  if (!export_view_source.contains(QStringLiteral("deliveryExportContract: \"delivery-export:v1|preflight:destination-filenames-collisions-missing-unsupported-unavailable|progress:per-item-overall-nonblocking|actions:pause-resume-cancel-retry-failed-reveal|summary:profile-dimensions-format-metadata-sharpening|collision:numbered-preserve-existing\"")) ||
      !export_view_source.contains(QStringLiteral("function currentBatchPreflight()")) ||
      !export_view_source.contains(QStringLiteral("exportJobs.requestPreflight(")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportPreflightDestinationLabel\"")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportPreflightCollisionSummary\"")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportBatchPreflightRepeater\"")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportBatchPreflightItem\"")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportDeliveryPolicyLabel\"")) ||
      !export_view_source.contains(QStringLiteral("objectName: \"exportOverallProgress\"")) ||
      !export_view_source.contains(QStringLiteral("jobQueue.progressSummaryForKind(\"asset_export\")")) ||
      !export_view_source.contains(QStringLiteral("key: \"pause\"")) ||
      !export_view_source.contains(QStringLiteral("key: \"resume\"")) ||
      !export_view_source.contains(QStringLiteral("key: \"reveal\"")) ||
      !export_view_source.contains(QStringLiteral("jobQueue.pauseJob(jobId)")) ||
      !export_view_source.contains(QStringLiteral("jobQueue.resumeJob(jobId)")) ||
      !export_view_source.contains(QStringLiteral("jobQueue.revealJobOutput(jobId)")) ||
      !export_view_source.contains(QStringLiteral("metadata.deliverySummary"))) {
    return fail("Delivery export should preview exact destination filenames and collisions, show overall and per-item progress, expose safe pause/resume/cancel/retry/reveal actions, and summarize delivered artifact properties");
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_DELIVERY_EXPORT_SOURCE")) {
    return EXIT_SUCCESS;
  }
  if (!export_preflight_issue_row_source.contains(QStringLiteral("exportPreflightIssueRowContract: \"export-preflight-issue-row:v1|severity-chip:true|severity-role:\"")) ||
      !export_preflight_issue_row_source.contains(QStringLiteral("|message-wrap:true|readable-text:true|no-elide:true|shared-tone:true|compact-failure:true|tooltip-full:true\"")) ||
      !export_preflight_issue_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|message-wrap:true|tooltip-full:true|accessibility-full:true\"")) ||
      !export_preflight_issue_row_source.contains(QStringLiteral("statusSurfaceContract: \"status-surface:v1|preflight:blocking-warning|compact-row:true|status-chip:true|message-after-status:true\"")) ||
      !export_preflight_issue_row_source.contains(QStringLiteral("StatusChip {")) ||
      !export_preflight_issue_row_source.contains(QStringLiteral("detailText: root.message")) ||
      !export_preflight_issue_row_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !export_preflight_issue_row_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !export_preflight_issue_row_source.contains(QStringLiteral("severity === \"error\" ? \"error\" : \"warning\"")) ||
      !export_view_source.contains(QStringLiteral("delegate: ExportPreflightIssueRow")) ||
      !export_view_source.contains(QStringLiteral("issue: modelData"))) {
    return fail("Export preflight issues should use the shared severity row with readable wrapped messages and shared status tones");
  }
  if (!export_preflight_issue_row_source.contains(QStringLiteral("text-preflight-actions:v1|inspect:true|resize-frame:true|fit-text:true|ignore:true|keyboard:true|no-elide:true")) ||
      !export_preflight_issue_row_source.contains(QStringLiteral("property bool showTextResolutionActions: false")) ||
      !export_preflight_issue_row_source.contains(QStringLiteral("text: \"Inspect\"")) ||
      !export_preflight_issue_row_source.contains(QStringLiteral("text: \"Resize Frame\"")) ||
      !export_preflight_issue_row_source.contains(QStringLiteral("text: \"Fit Text\"")) ||
      !export_preflight_issue_row_source.contains(QStringLiteral("text: \"Ignore\""))) {
    return fail("Shared preflight rows should expose direct keyboard-operable text inspection, frame resize, fit, and ignore actions when requested");
  }
  if (!export_preset_summary_source.contains(QStringLiteral("exportPresetSummaryContract: \"export-preset-summary:v1|metric-fit:true|description-wrap:true|adaptive-grid:true|readable-text:true|no-elide:true\"")) ||
      !export_preset_summary_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|metric-fit:true|description-wrap:true|tooltip-full:true|accessibility-full:true\"")) ||
      !export_preset_summary_source.contains(QStringLiteral("readonly property int metricColumns: width < 280 ? 2 : 4")) ||
      !export_preset_summary_source.contains(QStringLiteral("GridLayout {")) ||
      !export_preset_summary_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !export_preset_summary_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !export_preset_summary_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !export_view_source.contains(QStringLiteral("ExportPresetSummary {")) ||
      !export_view_source.contains(QStringLiteral("format: currentPresetDetail().format")) ||
      !export_view_source.contains(QStringLiteral("description: currentPresetDetail().description"))) {
    return fail("Export preset summaries should use shared adaptive no-ellipsis metadata and wrapped descriptions");
  }
  if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER") &&
      (!recent_document_row_source.contains(QStringLiteral("recentDocumentRowContract: \"recent-document-row:v1|title-wrap:true|detail-wrap:true|hover:true|keyboard:true|readable-text:true\"")) ||
      !recent_document_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|title-wrap:true|detail-wrap:true|tooltip-full:true\"")) ||
      !recent_document_row_source.contains(QStringLiteral("rowAnatomyContract: \"row-anatomy:v1|state-edge:left|icon-zone:fixed-18|content:title-detail-wrap|selection:left-edge|hover:neutral|focus:ring\"")) ||
      !recent_document_row_source.contains(QStringLiteral("readonly property int iconZoneWidth: 18")) ||
      !recent_document_row_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      !recent_document_row_source.contains(QStringLiteral("MouseArea {")) ||
      !recent_document_row_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !timeline_view_source.contains(QStringLiteral("source: Qt.resolvedUrl(\"TimelineSetupPage.qml\")")) ||
      !timeline_setup_page_source.contains(QStringLiteral("delegate: RecentDocumentRow")) ||
      !timeline_setup_page_source.contains(QStringLiteral("timelineDocument.loadTimeline(modelData.documentPath)")) ||
      !carousel_view_source.contains(QStringLiteral("delegate: RecentDocumentRow")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.loadDocument(String(modelData.documentId || \"\"))")))) {
    return fail("Timeline and Carousel recent documents should share RecentDocumentRow with readable text, hover, keyboard, and activation contracts");
  }
  if (!carousel_view_source.contains(QStringLiteral("signal editPhotoRequested(var payload)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.selectedPhotoEditPayload()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"editCarouselSlotPhotoButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"editCarouselLayerPhotoButton\"")) ||
      !main_qml_source.contains(QStringLiteral("function beginCarouselPhotoEdit(payload)")) ||
      !main_qml_source.contains(QStringLiteral("function finishCarouselPhotoEdit(applyChanges)")) ||
      !main_qml_source.contains(QStringLiteral("carouselDocument.applyAdjustmentPatchToPhotoEditTarget(")) ||
      !main_qml_source.contains(QStringLiteral("function commitActiveEditBoard()")) ||
      !main_qml_source.contains(QStringLiteral("return syncBoardPhotoEdit() && carouselDocument.saveDocument()")) ||
      !main_qml_source.contains(QStringLiteral("text: \"Boards\"")) ||
      !main_qml_source.contains(QStringLiteral("onTriggered: window.switchToWorkspaceIndex(window.carouselWorkspaceIndex)")) ||
      !main_qml_source.contains(QStringLiteral("if (carouselPhotoEditActive && !finishCarouselPhotoEdit(true)) return"))) {
    return fail("Carousel photo edits should save into the selected board and expose a direct return route");
  }
  if (!gallery_asset_card_source.contains(QStringLiteral("galleryAssetCardContract: \"gallery-asset-card:v1|preview:true|selection:true|online-state:true|keyboard:true|double-click:true|readable-text:true|no-elide:true\"")) ||
      !gallery_asset_card_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|title-wrap:true|metadata-wrap:true|tooltip-full:true|accessibility-full:true\"")) ||
      !gallery_asset_card_source.contains(QStringLiteral("cardAnatomyContract: \"card-anatomy:v1|preview-dominant:true|content:title-wrap|metadata:bottom-wrap|selection:quiet-border|keyboard:focus-ring|online-state:opacity\"")) ||
      !gallery_asset_card_source.contains(QStringLiteral("signal selectedRequested(int assetIndex)")) ||
      !gallery_asset_card_source.contains(QStringLiteral("signal activated(int assetIndex)")) ||
      !gallery_asset_card_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      !gallery_asset_card_source.contains(QStringLiteral("onDoubleTapped")) ||
      !gallery_asset_card_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !gallery_picker_dialog_source.contains(QStringLiteral("objectName: \"galleryPickerDialog\"")) ||
      !gallery_picker_dialog_source.contains(QStringLiteral("dialogVisibilityContract: \"dialog-visibility:v2|gallery-picker|always:title-scope-grid-close|contextual:selection-panel-selected-preview-offline-warning-side-footer-empty-state|one-expansion:project-scope-all-media-asset-activation|hover-focus:asset-card-selection-keyboard|diagnostic:offline-path-detail|preserve:functionality\"")) ||
      !gallery_picker_dialog_source.contains(QStringLiteral("dialogSurfaceContract: \"dialog-surface:v2|gallery-picker|grid:dominant|scope:header|selection-panel:when-selected|preview:contextual|footer:preserved-in-empty-state|cards:shared-readable|empty-state:flat-guidance|no-elide:true|no-overlap:true\"")) ||
      !gallery_picker_dialog_source.contains(QStringLiteral("functionPreservationMap: \"function-preservation:v1|select-asset:grid-card|activate-asset:double-click-or-enter|scope-project:header-toggle|scope-all:header-toggle|offline-state:selection-panel|side-footer:contextual-loader\"")) ||
      !gallery_picker_dialog_source.contains(QStringLiteral("objectName: \"galleryPickerEmptyStateCard\"")) ||
      !gallery_picker_dialog_source.contains(QStringLiteral("emptyStateContract: \"gallery-empty-state:v2|guidance:flat|text-wrap:true|max-lines:3|no-elide:true|no-overlap:true\"")) ||
      !gallery_picker_dialog_source.contains(QStringLiteral("objectName: \"galleryPickerEmptyStateLabel\"")) ||
      !gallery_picker_dialog_source.contains(QStringLiteral("delegate: GalleryAssetCard")) ||
      !gallery_picker_dialog_source.contains(QStringLiteral("assetIndex: index")) ||
      !gallery_picker_dialog_source.contains(QStringLiteral("asset: modelData")) ||
      !gallery_picker_dialog_source.contains(QStringLiteral("root.selectAssetIndex(assetIndex)")) ||
      !gallery_picker_dialog_source.contains(QStringLiteral("root.assetActivated(root.selectedAsset)"))) {
    return fail("Gallery picker should expose dialog visibility/function contracts and use GalleryAssetCard with readable labels, selection, keyboard, double-click activation, and preserved selected asset behavior");
  }
  if (!carousel_board_thumb_source.contains(QStringLiteral("carouselBoardThumbContract: \"carousel-board-thumb:v2|preview:true|selection:true|active-edit-target:explicit|keyboard:true|double-click:true|readable-text:true|no-elide:true\"")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("text: \"EDITING\"")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-wrap:true|tooltip-full:true|accessibility-full:true\"")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("mediaSurfaceContract: \"media-surface:v1|board-thumbnail:stable|preview-dominant:true|selection:single-border|badge-corner:bottom|chrome:quiet\"")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("required property var modelData")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("onDoubleClicked: root.duplicateRequested(root.index)")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("signal selectedRequested(int index")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("signal duplicateRequested(int index)")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("signal reorderPreviewRequested(int fromIndex")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("signal mediaDropRequested(int boardIndex, var drop)")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("objectName: \"carouselBoardThumbnailDropArea\"")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("readonly property string boardName")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("readonly property string boardRole")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      (!carousel_board_thumb_source.contains(QStringLiteral("onDoubleTapped")) &&
       !carousel_board_thumb_source.contains(QStringLiteral("onDoubleClicked"))) ||
      !carousel_board_thumb_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !carousel_view_source.contains(QStringLiteral("delegate: CarouselBoardThumb")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBoardDropMarker\"")) ||
      !carousel_view_source.contains(QStringLiteral("footer: Rectangle")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.selectSlide(index)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.duplicateCurrentSlide()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCreateBoardVariantButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.createBoardVariant()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselNeighborComparisonDialog.open()"))) {
    return fail("Carousel board filmstrip should use CarouselBoardThumb with preview, selection, semantic role/name labels, keyboard, double-click duplicate, and an add-board tile");
  }
  if (!carousel_view_source.contains(QStringLiteral("readonly property var boardRoleButtons")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setCurrentSlideName(text)")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBoardRoleMenu\"")) ||
      !carousel_view_source.contains(QStringLiteral("onMenuItemTriggered: carouselDocument.setCurrentSlideRole(key)")) ||
      !carousel_view_source.contains(QStringLiteral("title: \"Board Notes\"")) ||
      !carousel_view_source.contains(QStringLiteral("subtitle: \"Saved as metadata · never rendered\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setCurrentSlideNotes(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setCurrentSlideAccessibilityText("))) {
    return fail("Carousel should expose editable board names, semantic roles, production notes, alt text, and plain-language metadata without rendering metadata into artwork");
  }
  if (!carousel_view_source.contains(QStringLiteral("objectName: \"carouselDocumentSaveState\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: carouselDocument.saveState")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.saveState === \"Autosaved\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.lastSavedAt")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.saveError")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselStatusSurface\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: carouselDocument.status")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselStatusDetailsButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselStatusNoticesDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: carouselDocument.statusNotices")) ||
      !carousel_view_source.contains(QStringLiteral("Failures stay here until dismissed")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.dismissStatusNotice(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.clearStatusNotices()")) ||
      !carousel_view_source.contains(QStringLiteral("root.handleStatusNoticeAction(")) ||
      !carousel_view_source.contains(QStringLiteral("actionKey === \"retry-save\"")) ||
      !carousel_view_source.contains(QStringLiteral("actionKey === \"open-export\"")) ||
      !carousel_view_source.contains(QStringLiteral("actionKey === \"relink-media\"")) ||
      !carousel_view_source.contains(QStringLiteral("actionKey === \"resolve-fonts\""))) {
    return fail("Carousel should keep document save state, last-save detail, and controller status visible in the active workspace");
  }
  if (!carousel_view_source.contains(QStringLiteral("objectName: \"carouselRecoveryDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.recoveryState")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.restoreRecovery()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.openRecoveryCopy()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.openCurrentSavedVersion()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.discardRecovery()")) ||
      !carousel_view_source.contains(QStringLiteral("Using last good snapshot")) ||
      !carousel_view_source.contains(QStringLiteral("closePolicy: Popup.NoAutoClose"))) {
    return fail("Carousel should expose a non-dismissible recovery decision with restore, open-copy, discard, current-saved, and last-good state");
  }
  if (!carousel_view_source.contains(QStringLiteral("readonly property var textRoleButtons")) ||
      !carousel_view_source.contains(QStringLiteral("key: \"text-role:title\"")) ||
      !carousel_view_source.contains(QStringLiteral("key: \"text-role:cta\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.addTextItemWithRole(key.slice(10), \"\")")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setSelectedTextRole(modelData.value)"))) {
    return fail("Carousel should expose semantic text-role insertion and role editing through the text tool and inspector");
  }
  if (!carousel_view_source.contains(QStringLiteral("objectName: \"carouselPreflightPanel\"")) ||
      !carousel_view_source.contains(QStringLiteral("carousel-preflight:v2|required-slots:true|missing-media:true|hidden-off-canvas:true|text-overflow:true|unsafe-seams:true|contrast:true|video-composition:true|fonts:true|duplicate-names:true|previous-next:true|board-counts:true|severity-filters:true|ignore-with-reason:true|live-revalidation:true|no-elide:true")) ||
      !carousel_view_source.contains(QStringLiteral("model: carouselDocument.preflightIssueState.issues || []")) ||
      !carousel_view_source.contains(QStringLiteral("delegate: ExportPreflightIssueRow")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.preflightBlockingCount")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.preflightWarningCount")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.focusPreflightIssueById("))) {
    return fail("Carousel should expose controller-backed preflight counts, readable issue rows, and board or layer issue navigation");
  }
  if (!carousel_view_source.contains(QStringLiteral("modelData.code === \"text-overflow\"")) ||
      !carousel_view_source.contains(QStringLiteral("modelData.code === \"text-too-small\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.resolveTextPreflightIssue(")) ||
      !carousel_view_source.contains(QStringLiteral("\"resize-frame\"")) ||
      !carousel_view_source.contains(QStringLiteral("\"fit-text\""))) {
    return fail("Carousel preflight should route overflow and minimum-readable-size rows to direct resolution commands");
  }
  if (!carousel_view_source.contains(QStringLiteral("objectName: \"carouselRelinkMissingMediaButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselRelinkDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.missingMediaEntries")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.relinkPlan")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.chooseRelinkDirectory()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyRelinkPlan()")) ||
      !carousel_view_source.contains(QStringLiteral("Only unique metadata matches can be applied.")) ||
      !carousel_view_source.contains(QStringLiteral("Ambiguous")) ||
      !carousel_view_source.contains(QStringLiteral("Mismatch"))) {
    return fail("Carousel should expose metadata-verified batch relink with visible ambiguous and mismatched candidates");
  }
  if (!carousel_view_source.contains(QStringLiteral("objectName: \"carouselResolveMissingFontsButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselFontRecoveryDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.missingFontEntries")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.installedFontFamilies")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.fontSubstitutionPreview")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewFontSubstitution(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyFontSubstitution()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.openFontManagement()")) ||
      !carousel_view_source.contains(QStringLiteral("Export stays blocked until each unavailable face")) ||
      !carousel_view_source.contains(QStringLiteral("OVERFLOW"))) {
    return fail("Carousel should block missing-font export and expose exact layers, installed-font routing, layout preview, overflow, and explicit substitution");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselFontBrowserContract: \"carousel-font-browser:v1|installed-families:true|search:true|favorites:persistent|recents:persistent|live-preview:true|missing-state:true|explicit-fallback-preview:true|no-silent-substitution:true|virtualized-list:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselOpenFontBrowserButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselFontBrowserDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselFontSearchField\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselFontFamilyList\"")) ||
      !carousel_view_source.contains(QStringLiteral("reuseItems: true")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.favoriteFontFamilies")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.recentFontFamilies")) ||
      !carousel_view_source.contains(QStringLiteral("font.family: fontFamilyRow.modelData")) ||
      !carousel_view_source.contains(QStringLiteral("text: content.selection.textFontAvailable ? \"Use font\" : \"Preview fallback\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewFontSubstitution(selection.textFontFamily, family)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyFontFamilyFromBrowser(family)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.toggleFavoriteFontFamily(fontFamilyRow.modelData)"))) {
    return fail("Carousel text should expose a virtualized installed-font browser with search, persistent favorites and recents, live face previews, missing-face state, and explicit fallback preview");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselCopyProofContract: \"carousel-copy-proof:v1|languages:en-US-en-GB|spelling:true|repeated-words:true|spacing:true|next-issue:true|ignore:true|custom-dictionary:persistent|exact-layer-location:true|no-silent-rewrite:true|undoable-ignore:true|persistence:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselOpenCopyProofButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCopyProofDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselNextCopyProofIssueButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCopyProofIssueList\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: carouselDocument.copyProofLanguageOptions")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setCopyProofLanguage(modelData)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.nextCopyProofIssue()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.addCopyProofWord(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.ignoreCopyProofIssue(")) ||
      !carousel_view_source.contains(QStringLiteral("It never changes authored copy."))) {
    return fail("Carousel copy proof should expose language choice, exact issue navigation, ignore, persistent dictionary additions, and an explicit no-rewrite contract");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselKeyboardContract: \"carousel-keyboard:v1|board-navigation:true|board-reorder:true|tool-selection:true|layer-selection:true|layer-reorder:true|transforms:true|text-editing:true|inspector-focus:true|preflight:true|save:true|export:true|platform-modifiers:true|no-mouse-required:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("sequence: \"Meta+Left\"")) ||
      !carousel_view_source.contains(QStringLiteral("sequence: \"Ctrl+Shift+Left\"")) ||
      !carousel_view_source.contains(QStringLiteral("sequence: \"Alt+Up\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.selectAdjacentLayer(-1)")) ||
      !carousel_view_source.contains(QStringLiteral("sequence: \"Alt+Shift+Up\"")) ||
      !carousel_view_source.contains(QStringLiteral("sequence: \"Return\"")) ||
      !carousel_view_source.contains(QStringLiteral("textEditor.forceActiveFocus()")) ||
      !carousel_view_source.contains(QStringLiteral("sequence: \"Ctrl+Alt+I\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselInspectorScroll\"")) ||
      !carousel_view_source.contains(QStringLiteral("sequence: \"Ctrl+Shift+P\"")) ||
      !carousel_view_source.contains(QStringLiteral("sequence: \"Ctrl+Shift+E\"")) ||
      !carousel_view_source.contains(QStringLiteral("onActivated: root.runCarouselCommand(\"preflight.open\")")) ||
      !carousel_view_source.contains(QStringLiteral("onActivated: root.runCarouselCommand(\"export.open\")")) ||
      !carousel_view_source.contains(QStringLiteral("onActivated: root.runCarouselCommand(\"save.document\")"))) {
    return fail("Carousel should support keyboard-only board and layer navigation, reorder, tools, transforms, text editing, inspector focus, preflight, save, and export");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselCommandPaletteContract: \"carousel-command-palette:v1|search:true|shared-dispatch:true|categories:add-transform-arrange-board-template-inspect-accessibility-preflight-save-export|keyboard:contextual-ctrl-meta-k|navigation:up-down-enter-escape|disabled-reasons:true|focus-restore:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("function carouselCommandRegistry()")) ||
      !carousel_view_source.contains(QStringLiteral("function filteredCarouselCommands()")) ||
      !carousel_view_source.contains(QStringLiteral("function runCarouselCommand(commandId)")) ||
      !carousel_view_source.contains(QStringLiteral("root.carouselCommand(\"add.media\"")) ||
      !carousel_view_source.contains(QStringLiteral("root.carouselCommand(\"transform.rotate-clockwise\"")) ||
      !carousel_view_source.contains(QStringLiteral("root.carouselCommand(\"arrange.group\"")) ||
      !carousel_view_source.contains(QStringLiteral("root.carouselCommand(\"board.add\"")) ||
      !carousel_view_source.contains(QStringLiteral("root.carouselCommand(\"template.library\"")) ||
      !carousel_view_source.contains(QStringLiteral("root.carouselCommand(\"accessibility.reading-order\"")) ||
      !carousel_view_source.contains(QStringLiteral("root.carouselCommand(\"preflight.open\"")) ||
      !carousel_view_source.contains(QStringLiteral("root.carouselCommand(\"save.document\"")) ||
      !carousel_view_source.contains(QStringLiteral("root.carouselCommand(\"export.open\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCommandPaletteButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCommandPalette\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCommandSearchField\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCommandResultList\"")) ||
      !main_qml_source.contains(QStringLiteral("shortcut: window.configuredShortcut(\"command-palette\", window.primaryShortcut(\"K\"))")) ||
      !main_qml_source.contains(QStringLiteral("return \"Ctrl+\" + key")) ||
      !main_qml_source.contains(QStringLiteral("return \"Ctrl+Shift+\" + key")) ||
      !main_qml_source.contains(QStringLiteral("return \"Ctrl+Alt+\" + key")) ||
      !main_qml_source.contains(QStringLiteral("return sequence.replace(/(^|\\+)Cmd(?=\\+)/g, \"$1Ctrl\")")) ||
      !main_qml_source.contains(QStringLiteral("function canOpenSelectedPhotoInEdit()")) ||
      !main_qml_source.contains(QStringLiteral("if (catalogAssets.selectedAssetCount > 0)")) ||
      !main_qml_source.contains(QStringLiteral("return catalogAssets.firstVisiblePhotoIndex >= 0")) ||
      !main_qml_source.contains(QStringLiteral("if (catalogAssets.selectedAssetCount === 0)")) ||
      !main_qml_source.contains(QStringLiteral("catalogAssets.selectIndex(photoIndex)")) ||
      !keyboard_shortcuts_dialog_source.contains(QStringLiteral("function primarySequence(key)")) ||
      !keyboard_shortcuts_dialog_source.contains(QStringLiteral("function normalizeShortcutSequence(sequence)")) ||
      !keyboard_shortcuts_dialog_source.contains(QStringLiteral("function displayShortcutSequence(sequence)")) ||
      !main_qml_source.contains(QStringLiteral("carouselView.openCarouselCommandPalette()")) ||
      !carousel_view_source.contains(QStringLiteral("event.key === Qt.Key_Down")) ||
      !carousel_view_source.contains(QStringLiteral("event.key === Qt.Key_Up")) ||
      !carousel_view_source.contains(QStringLiteral("event.key === Qt.Key_Return")) ||
      !carousel_view_source.contains(QStringLiteral("event.key === Qt.Key_Escape")) ||
      !carousel_view_source.contains(QStringLiteral("String(commandResultDelegate.modelData.disabledReason)")) ||
      !carousel_view_source.contains(QStringLiteral("root.carouselCommandRestoreFocusItem")) ||
      !carousel_view_source.contains(QStringLiteral("onClicked: root.runCarouselCommand(\"save.document\")")) ||
      !carousel_view_source.contains(QStringLiteral("onClicked: root.runCarouselCommand(\"export.open\")")) ||
      !carousel_view_source.contains(QStringLiteral("onClicked: root.runCarouselCommand(\"board.add\")"))) {
    return fail("Carousel should expose a searchable command palette whose add, transform, arrange, board, template, inspect, accessibility, preflight, save, and export entries share dispatch with visible actions and shortcuts");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselReadingOrderContract: \"carousel-reading-order:v1|board-specific:true|content:visible-text-meaningful-image-video|decorative-media:excluded|slots-and-layers:true|reorder:explicit|visual-fallback:top-left|focus-entry:true|undo:true|persistence:true|manifest:v2|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("function openCarouselReadingOrderEditor()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselReadingOrderButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselReadingOrderDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselReadingOrderList\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselReadingOrderMoveEarlierButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselReadingOrderMoveLaterButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.moveReadingOrderEntry(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setReadingOrderEntryMeaningful(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.focusReadingOrderEntry(")) ||
      !carousel_view_source.contains(QStringLiteral("Decorative media stays in the artwork and is excluded from delivery order.")) ||
      !carousel_view_source.contains(QStringLiteral("elide: Text.ElideNone"))) {
    return fail("Carousel should expose a board-specific reading-order editor for visible text and meaningful imagery with decorative exclusion, keyboard buttons, exact focus, persistence, undo, and manifest routing");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselBrandKitEditorContract: \"carousel-brand-kit-editor:v1|named-palettes:true|role-typography:true|logo-assets:true|required-checks:true|font-license-reminder:true|token-scales:true|live-preview:true|safe-cancel:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBrandKitEditorDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.beginBrandKitEditor(")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Primary\", key: \"logoPath\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setBrandKitDraftField(variantField.modelData.key, text)")) ||
      !carousel_view_source.contains(QStringLiteral("root.openMediaPicker(\"brand-logo\")")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Named palette\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Typography roles\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Design-token scales\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.brandKitValidation")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.saveBrandKitDraft()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.cancelBrandKitEditor()"))) {
    return fail("Carousel brand kits should expose a live editor for named palette, role typography, logo requirements, license reminders, design tokens, validated save, and safe cancel");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselLogoUsageRulesContract: \"carousel-logo-usage-rules:v1|safe-area:true|minimum-size:true|variants:primary-light-dark-monochrome|surface-rule:true|contrast-rule:true|warnings-only:true|never-mutate-artwork:true|jump-to-placement:true|ignore-explicit:true|persistence:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBrandLogoVariantEditor\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Primary\", key: \"logoPath\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Light on dark\", key: \"logoLightPath\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Dark on light\", key: \"logoDarkPath\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Monochrome\", key: \"logoMonochromePath\"")) ||
      !carousel_view_source.contains(QStringLiteral("root.brandLogoDraftField = logoField.modelData.key")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBrandLogoRuleEditor\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Clear space (%)\", key: \"logoSafeAreaPercent\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Minimum width (px)\", key: \"logoMinimumWidth\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Minimum height (px)\", key: \"logoMinimumHeight\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Minimum contrast\", key: \"logoMinimumContrast\"")) ||
      !carousel_view_source.contains(QStringLiteral("Logo checks only warn. They never resize, move, recolor, or swap placed artwork."))) {
    return fail("Carousel brand kits should define logo variants and clear-space, size, surface, and contrast warning rules without mutating artwork");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselBrandTokenBindingContract: \"carousel-brand-token-binding:v1|named-colors:true|named-typography:true|text-shape-brush:true|follow-updates:true|explicit-detach:true|local-value-retained:true|local-edit-detaches:true|undo:true|persistence:true|export:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBrandTokenInspector\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBrandColorTokenCombo\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: carouselDocument.brandColorTokens")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.bindSelectedLayerToBrandColorToken(")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselDetachBrandColorTokenButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.detachSelectedBrandColorToken()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBrandTypographyTokenCombo\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: carouselDocument.brandTypographyTokens")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.bindSelectedTextToBrandTypographyToken(")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselDetachBrandTypographyTokenButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.detachSelectedBrandTypographyToken()")) ||
      !carousel_view_source.contains(QStringLiteral("text: selection.followsBrandColorToken")) ||
      !carousel_view_source.contains(QStringLiteral("text: selection.followsBrandTypographyToken"))) {
    return fail("Carousel inspectors should bind text, shapes, and brushes to named brand color/type tokens or explicitly detach while retaining local values");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselBrandThemeVariantsContract: \"carousel-brand-theme-variants:v1|variants:light-dark-campaign|editable:true|scopes:current-selected-all|content-preserved:true|geometry-preserved:true|token-aware:true|atomic-undo:true|persistence:true|export:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBrandThemeScopeControls\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Current\", value: \"current\", enabled: true }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Selected\", value: \"selected\", enabled: carouselDocument.selectedSlideCount > 1 }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"All boards\", value: \"all\", enabled: true }")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBrandThemeVariantControls\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Light\", value: \"light\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Dark\", value: \"dark\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Campaign\", value: \"campaign\" }")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyBrandThemeVariant(")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBrandThemeVariantEditor\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Theme variants\"")) ||
      !carousel_view_source.contains(QStringLiteral("key: \"darkBackgroundColor\"")) ||
      !carousel_view_source.contains(QStringLiteral("key: \"campaignAccentColor\""))) {
    return fail("Carousel brand kits should expose editable light, dark, and campaign variants with current, selected, and all-board switching");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselReusableComponentsContract: \"carousel-reusable-components:v1|types:logo-lockup-page-number-quote-attribution-stat-card-product-badge-cta-block|ordinary-editable-layers:true|built-in:true|custom:true|fresh-instance-identity:true|preview-thumbnails:bounded|ratio-adaptive:true|undo:true|persistence:true|export:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("title: \"Components\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselReusableComponentLibrary\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: carouselDocument.componentLibraryEntries")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselReusableComponentThumbnail\"")) ||
      !carousel_view_source.contains(QStringLiteral("Math.min(240, Number(reusableComponentCard.modelData.previewMaximumPixelWidth || 240))")) ||
      !carousel_view_source.contains(QStringLiteral("Math.min(400, Number(reusableComponentCard.modelData.previewMaximumPixelHeight || 160))")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselAddReusableComponentButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyBuiltInComponent(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyUserTemplate(")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Add\""))) {
    return fail("Carousel should expose the six built-in and custom reusable components as bounded-thumbnail cards that add ordinary editable layers");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselComponentInstanceLifecycleContract: \"carousel-component-instance-lifecycle:v1|definition-versioned:true|automatic-updates:false|update-definition:explicit|apply-update:explicit|local-overrides:field-preserved|removed-overrides:detached|new-parts:added|detach-keeps-content-geometry:true|atomic-undo:true|persistence:true|export:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselComponentInstanceState\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.componentInstanceState.updateAvailable")) ||
      !carousel_view_source.contains(QStringLiteral("Instances never update automatically. Applying an update retains local field overrides.")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselUpdateComponentDefinitionButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Update saved component\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.updateSelectedComponentDefinition()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselApplyComponentUpdateButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Apply update\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applySelectedComponentUpdate()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselDetachComponentButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Detach\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.detachSelectedComponentInstance()"))) {
    return fail("Carousel component instances should expose explicit definition updates, per-instance update application, local-override retention, and content-preserving detach");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselDesignPackageContract: \"carousel-design-package:v1|artifacts:brand-kit-component|manifest:true|artifact-sha256:true|package-sha256:true|asset-sha256:true|brand-assets:embedded|private-project-media:never-embedded|absolute-paths:excluded|import-preview:non-mutating|conflicts:id-name|resolutions:keep-both-replace|checksum-required:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselImportDesignPackageButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Import design package\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.chooseDesignPackageImport()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselExportComponentPackageButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.exportDesignPackageInteractive(")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselExportBrandPackageButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Export brand package\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselDesignPackageImportDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.designPackageImportPreview.checksumValid")) ||
      !carousel_view_source.contains(QStringLiteral("private project media assets were intentionally not embedded")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselDesignPackageConflictReview\"")) ||
      !carousel_view_source.contains(QStringLiteral("The artifact ID already exists.")) ||
      !carousel_view_source.contains(QStringLiteral("The artifact name already exists.")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselImportPackageKeepBothButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyDesignPackageImport(\"keep-both\")")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselImportPackageReplaceButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyDesignPackageImport(\"replace\")"))) {
    return fail("Carousel should export and review checksummed brand/component packages, explicitly exclude private project media, and require Keep Both or Replace for conflicts");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselBrandApplicationDiffContract: \"carousel-brand-application-diff:v1|boards:true|layers:true|before-target-result:true|missing-mappings:true|contrast-regressions:true|retain-overrides:true|replace-overrides:true|explicit-apply:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBrandApplicationDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewBrandKitApplication(brandId, scope, \"retain\")")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Retain local values\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Replace with brand\"")) ||
      !carousel_view_source.contains(QStringLiteral("diff.missingMappings || []")) ||
      !carousel_view_source.contains(QStringLiteral("diff.contrastRegressions || []")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Layer diff\"")) ||
      !carousel_view_source.contains(QStringLiteral("\"Before: \" + carouselBrandApplicationDialog.styleSummary(modelData.before)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyPreviewedBrandKit()"))) {
    return fail("Carousel brand application should review exact board/layer diffs, missing mappings, contrast regressions, local override policy, and explicit confirmation before mutation");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselBrandLintContract: \"carousel-brand-lint:v1|unapproved-colors:true|unapproved-fonts:true|required-logo:true|heading-caption-consistency:true|minimum-contrast:true|jump-to-layer:true|explicit-ignore:true|persistent-ignore:true|live-revalidation:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBrandLintDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setActiveBrandKitForLint(")) ||
      !carousel_view_source.contains(QStringLiteral("readonly property var lint: carouselDocument.brandLintState")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.focusBrandLintIssue(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.ignoreBrandLintIssue(")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Ignored exceptions are document-specific and persist after save.\""))) {
    return fail("Carousel brand lint should report palette, font, logo, role-consistency, and contrast exceptions with exact navigation, explicit ignore, persistence, and live revalidation");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselLiveContrastContract: \"carousel-live-contrast:v1|wcag-style:true|opacity:true|gradients:true|image-region-samples:true|text-background-treatment:true|stroke-treatment:true|photographic-uncertainty:true|large-text-threshold:true|live:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselLiveContrastCard\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.textContrastChecks || []")) ||
      !carousel_view_source.contains(QStringLiteral("selectedTextContrast.minimumRatio")) ||
      !carousel_view_source.contains(QStringLiteral("selectedTextContrast.threshold")) ||
      !carousel_view_source.contains(QStringLiteral("selectedTextContrast.backgroundType")) ||
      !carousel_view_source.contains(QStringLiteral("selectedTextContrast.sampleCount")) ||
      !carousel_view_source.contains(QStringLiteral("selectedTextContrast.textOpacity")) ||
      !carousel_view_source.contains(QStringLiteral("selectedTextContrast.backgroundTreatmentOpacity")) ||
      !carousel_view_source.contains(QStringLiteral("selectedTextContrast.strokeWidth")) ||
      !carousel_view_source.contains(QStringLiteral("selectedTextContrast.uncertaintyReasons"))) {
    return fail("Carousel text should show live WCAG-style minimum contrast across opacity, gradients, image-region samples, boxes, strokes, large-text thresholds, and explicit photographic uncertainty");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselPlatformPreviewContract: \"carousel-platform-preview:v1|feed-grid-crop:true|profile-header:true|captions:true|like-comment-controls:true|progress-bars:true|device-corners:true|reference-only:true|verify-current:true|never-export:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: carouselDocument.platformPreviewProfiles")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Platform crop & obstruction preview\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"FEED GRID CROP\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"PROFILE HEADER CROP\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"CAPTION / ACCOUNT / AUDIO\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"LIKE / COMMENT\"")) ||
      !carousel_view_source.contains(QStringLiteral("profile.progressEdge")) ||
      !carousel_view_source.contains(QStringLiteral("profile.deviceCornerRatio")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"REFERENCE UI — NOT EXPORTED\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: String(root.platformPreviewProfile.disclaimer || \"\")"))) {
    return fail("Carousel should expose non-exporting reference previews for feed-grid and profile-header crops plus caption, engagement-control, progress, and device-corner obstructions with a currentness warning");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselColorInspectionContract: \"carousel-color-inspection:v1|modes:normal-protanopia-deuteranopia-tritanopia-grayscale|matrix:linear-rgb|transfer:srgb-linear-srgb|canvas-only:true|document-state:unchanged|export:unchanged|normal-return:always-visible|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("property string carouselInspectionMode: \"normal\"")) ||
      !carousel_view_source.contains(QStringLiteral("function setCarouselInspectionMode(mode)")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselColorInspectionMenu\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselReturnToNormalColorButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Return to Normal Color\"")) ||
      !carousel_view_source.contains(QStringLiteral("layer.enabled: root.carouselInspectionModeActive")) ||
      !carousel_view_source.contains(QStringLiteral("layer.effect: CarouselColorVisionEffect")) ||
      !carousel_view_source.contains(QStringLiteral("root.carouselCommand(\"inspect.protanopia\"")) ||
      !carousel_view_source.contains(QStringLiteral("root.carouselCommand(\"inspect.deuteranopia\"")) ||
      !carousel_view_source.contains(QStringLiteral("root.carouselCommand(\"inspect.tritanopia\"")) ||
      !carousel_view_source.contains(QStringLiteral("root.carouselCommand(\"inspect.grayscale\"")) ||
      !carousel_view_source.contains(QStringLiteral("onClicked: root.runCarouselCommand(\"inspect.normal\")")) ||
      !carousel_color_vision_effect_source.contains(QStringLiteral("effectContract: \"carousel-color-vision-effect:v1|modes:protanopia-deuteranopia-tritanopia-grayscale|transfer:srgb-linear-srgb|alpha:premultiplied-safe|preview-only:true\"")) ||
      !carousel_color_vision_effect_source.contains(QStringLiteral("qrc:/photo-editor/shaders/carousel_color_vision.frag.qsb")) ||
      !carousel_color_vision_fragment_source.contains(QStringLiteral("vec3 srgbToLinear(vec3 value)")) ||
      !carousel_color_vision_fragment_source.contains(QStringLiteral("vec3 linearToSrgb(vec3 value)")) ||
      !carousel_color_vision_fragment_source.contains(QStringLiteral("0.56667, 0.55833, 0.00000")) ||
      !carousel_color_vision_fragment_source.contains(QStringLiteral("0.62500, 0.70000, 0.00000")) ||
      !carousel_color_vision_fragment_source.contains(QStringLiteral("0.95000, 0.00000, 0.00000")) ||
      !carousel_color_vision_fragment_source.contains(QStringLiteral("0.21260, 0.21260, 0.21260")) ||
      !carousel_color_vision_fragment_source.contains(QStringLiteral("sampled.rgb / sampled.a"))) {
    return fail("Carousel should expose canvas-only protanopia, deuteranopia, tritanopia, and grayscale inspection with a persistent normal-color return and packaged linear-RGB shader");
  }
  if (!carousel_view_source.contains(QStringLiteral("preflightContract: \"carousel-preflight:v2|required-slots:true|missing-media:true|hidden-off-canvas:true|text-overflow:true|unsafe-seams:true|contrast:true|video-composition:true|fonts:true|duplicate-names:true|previous-next:true|board-counts:true|severity-filters:true|ignore-with-reason:true|live-revalidation:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.navigatePreflightIssue(-1)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.navigatePreflightIssue(1)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setPreflightSeverityFilter(modelData.value)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.preflightIssueState.boardCounts || []")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.focusPreflightIssueById(")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselPreflightIgnoreDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselPreflightIgnoreReasonField\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Ignore with Reason\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.ignorePreflightIssue(")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"The reason is saved with this carousel. Artwork is not changed.\""))) {
    return fail("Carousel preflight should support previous and next navigation, board counts, severity filters, exact issue focus, reason-required persistent ignore, and live revalidation");
  }
  if (!carousel_view_source.contains(QStringLiteral("objectName: \"carouselExportSheet\"")) ||
      !carousel_view_source.contains(QStringLiteral("carousel-export-sheet:v3|scope:current-selected-all|exact-ordered-thumbnail-review:true|board-count-order:true|ratio-pixels:true|file-types:true|color-space:true|destination:true|estimated-output:true|presets:versioned-resolved-override-aware-dated-guidance|blocking-preflight:true|controlled-start:true|no-elide:true")) ||
      !carousel_view_source.contains(QStringLiteral("readonly property var summary: carouselDocument.exportSummary")) ||
      !carousel_view_source.contains(QStringLiteral("exportPresetContract: \"carousel-export-presets:v1|built-ins:tiktok-vertical,tiktok-square,youtube-square,review,archive|versioned:true|compatibility:strict-no-stretch|apply:one-undo-step|overrides:preserve-reset-visible|resolved-settings:all-visible|persistence:document-schema-11|manifest:identity-version-override-count|guidance:first-party-source-last-reviewed-review-due-stale-state|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("readonly property var presetState: carouselDocument.exportPresetState")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselExportPresetSection\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselExportPresetBrowser\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: content.carouselDocument.exportPresets")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyExportPreset(")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselResetExportPresetOverridesButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.resetExportPresetOverrides()")) ||
      !carousel_view_source.contains(QStringLiteral("Open First-Party Source")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselExportPresetResolvedSettings\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: content.carouselExportSheet.presetState.resolvedRows || []")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Override\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselExportOrderedBoardReview\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: content.carouselDocument.exportBoardScopeOptions")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setExportBoardScope(modelData)")) ||
      !carousel_view_source.contains(QStringLiteral("Exact ordered output · boards ")) ||
      !carousel_view_source.contains(QStringLiteral("carouselExportSizeContract: \"carousel-export-size:v1|one-x:true|two-x:true|custom:true|aspect-lock:true|platform-warnings:true|preview-vs-export:true|pixel-verification:true|undo:true|persistence:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: content.carouselDocument.exportSizeModeOptions")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setExportSizeMode(modelData)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setExportCustomPixelSize(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setExportAspectLocked(")) ||
      !carousel_view_source.contains(QStringLiteral("Composer preview · ")) ||
      !carousel_view_source.contains(QStringLiteral("Rendered export · ")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.chooseExportDirectory()")) ||
      !carousel_view_source.contains(QStringLiteral("model: content.carouselDocument.exportColorSpaceOptions")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setExportColorSpace(modelData)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setExportFilenamePattern(text)")) ||
      !carousel_view_source.contains(QStringLiteral("{document}")) ||
      !carousel_view_source.contains(QStringLiteral("{board_number}")) ||
      !carousel_view_source.contains(QStringLiteral("{board_role}")) ||
      !carousel_view_source.contains(QStringLiteral("{ratio}")) ||
      !carousel_view_source.contains(QStringLiteral("{version}")) ||
      !carousel_view_source.contains(QStringLiteral("{date}")) ||
      !carousel_view_source.contains(QStringLiteral("model: content.carouselDocument.exportCollisionPolicyOptions")) ||
      !carousel_view_source.contains(QStringLiteral("model: content.carouselDocument.exportOutputPreview")) ||
      !carousel_view_source.contains(QStringLiteral("checked: content.carouselDocument.exportManifestEnabled")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setExportManifestEnabled(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselPdfProofContract: \"carousel-pdf-proof:v1|review-only:true|platform-output:separate|page:a4-landscape|boards-per-page:4|board-numbers:true|board-names:true|roles:true|resolved-file-names:true|optional-notes:true|thumbnail-color-management:output-profile-to-srgb|collision-policy:true|undo:true|persistence:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselPdfProofToggle\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselPdfProofNotesToggle\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselPdfProofResolvedSettings\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setExportProofSheetEnabled(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setExportProofIncludeNotes(")) ||
      !carousel_view_source.contains(QStringLiteral("A4 landscape · 4 boards per page")) ||
      !carousel_view_source.contains(QStringLiteral("thumbnails converted to sRGB proof space")) ||
      !carousel_view_source.contains(QStringLiteral("model: content.carouselDocument.exportImageFormatOptions")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setExportImageFormat(modelData)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setExportImageQuality(Math.round(value))")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setExportPreserveTransparency(")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselStartExportButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.startConfiguredExport()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselExportProgressSurface\"")) ||
      !carousel_view_source.contains(QStringLiteral("readonly property var progress: carouselDocument.exportProgress")) ||
      !carousel_view_source.contains(QStringLiteral("model: content.carouselDocument.exportPartialCleanupPolicyOptions")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.cancelExport()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.retryFailedExport()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.cleanupPartialExport()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.revealExportOutput()")) ||
      carousel_view_source.contains(QStringLiteral("onClicked: carouselDocument.exportCarousel()"))) {
    return fail("Carousel export actions should open a controlled summary sheet with destination, resolved output details, preflight blocking, progress, cancellation, retry, partial cleanup, reveal, and an explicit start command");
  }
  if (!carousel_view_source.contains(QStringLiteral("objectName: \"carouselEmptySlotPrompt\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselSlotRegionLabel\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselPlaceholderLayoutContract: \"carousel-placeholder-layout:v1|slot-label-zone:reserved|slot-action-zone:separate|slot-min-gap:8|tiny:icon-only|gallery-empty-card:bounded|no-elide:true|no-overlap:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("readonly property real slotLabelReserveHeight: 36")) ||
      !carousel_view_source.contains(QStringLiteral("height: Math.max(0, slotRegion.slotLabelReserveHeight - 12)")) ||
      !carousel_view_source.contains(QStringLiteral("promptContract: \"carousel-empty-slot:v3|bounded-to-slot:true|label-zone:reserved|action-zone:separate|min-gap:8|tiny-hidden:true|centered-action:true|icon-zone:fixed|full-label-min:112x34|icon-only-when-tight:true|tooltip-full:true|readable-text:true|no-incomplete-text:true|no-elide:true|no-overlap:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("readonly property real labelReserveHeight: slotRegion.slotLabelReserveHeight")) ||
      !carousel_view_source.contains(QStringLiteral("readonly property real labelActionGap: 8")) ||
      !carousel_view_source.contains(QStringLiteral("readonly property real readablePromptMinWidth: 112")) ||
      !carousel_view_source.contains(QStringLiteral("readonly property real readablePromptMinHeight: 34")) ||
      !carousel_view_source.contains(QStringLiteral("? availablePromptWidth >= 32 && availablePromptHeight >= 30")) ||
      !carousel_view_source.contains(QStringLiteral(": availablePromptWidth >= readablePromptMinWidth && availablePromptHeight >= readablePromptMinHeight")) ||
      !carousel_view_source.contains(QStringLiteral("readonly property bool compactPrompt: availablePromptWidth < readablePromptMinWidth || availablePromptHeight < readablePromptMinHeight")) ||
      !carousel_view_source.contains(QStringLiteral("readonly property bool labelTextFullyVisible: compactPrompt ||")) ||
      !carousel_view_source.contains(QStringLiteral("readonly property bool promptInsideSlot: x >= 0 && y >= labelReserveHeight")) ||
      !carousel_view_source.contains(QStringLiteral("readonly property bool promptClearOfSlotLabel: y >= labelReserveHeight + labelActionGap")) ||
      !carousel_view_source.contains(QStringLiteral("readonly property string promptGeometrySignature: \"x:\" + Math.round(x)")) ||
      !carousel_view_source.contains(QStringLiteral("\"|gap:\" + Math.round(labelActionGap)")) ||
      !carousel_view_source.contains(QStringLiteral("\"|has-room:\" + String(promptHasRoom)")) ||
      !carousel_view_source.contains(QStringLiteral("\"|label-fits:\" + String(labelTextFullyVisible)")) ||
      !carousel_view_source.contains(QStringLiteral("\"|inside:\" + String(promptInsideSlot)")) ||
      !carousel_view_source.contains(QStringLiteral("\"|clear-label:\" + String(promptClearOfSlotLabel)")) ||
      !carousel_view_source.contains(QStringLiteral("y: labelReserveHeight + labelActionGap + Math.max(0, (availablePromptHeight - height) / 2)")) ||
      !carousel_view_source.contains(QStringLiteral("visible: !modelData.hasMedia &&")) ||
      !carousel_view_source.contains(QStringLiteral("promptHasRoom")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselEmptySlotPromptContent\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselEmptySlotPromptIconZone\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Add media\"")) ||
      !carousel_view_source.contains(QStringLiteral("visible: !emptySlotPromptChrome.compactPrompt")) ||
      !carousel_view_source.contains(QStringLiteral("readonly property string promptAccessibleName: \"Add media to \" + String(modelData.label || \"slot\")")) ||
      !carousel_view_source.contains(QStringLiteral("text: emptySlotPromptChrome.promptAccessibleName")) ||
      !carousel_view_source.contains(QStringLiteral("id: slotHitArea")) ||
      !carousel_view_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !carousel_view_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !carousel_view_source.contains(QStringLiteral("carouselSlotSelectorContract: \"carousel-slot-selector:v1|split:left-right|stack:top-middle-bottom|full-labels:true|no-abbreviations:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: selection.templateKind === \"split-pair\" ? \"Right\" : \"Middle\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Bottom\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Duplicate\"")) ||
      carousel_view_source.contains(QStringLiteral("text: \"Btm\"")) ||
      carousel_view_source.contains(QStringLiteral("text: \"Mid\"")) ||
      carousel_view_source.contains(QStringLiteral("text: \"Dup\""))) {
    return fail("Carousel empty media slots should keep the add-media prompt bounded, readable, compact, and non-overlapping");
  }
  if (!carousel_media_thumb_source.contains(QStringLiteral("carouselMediaThumbContract: \"carousel-media-thumb:v2|preview:true|online-state:true|click-double-click:true|drag-payload:true|copy-action:true|readable-text:true|no-elide:true\"")) ||
      !carousel_media_thumb_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|media-label-wrap:true|tooltip-full:true|accessibility-full:true\"")) ||
      !carousel_media_thumb_source.contains(QStringLiteral("mediaSurfaceContract: \"media-surface:v1|quick-library-thumbnail:stable|preview-dominant:true|label-zone:bottom|badge-corner:reserved|chrome:quiet\"")) ||
      !carousel_media_thumb_source.contains(QStringLiteral("signal activated(var asset, var modifiers)")) ||
      !carousel_media_thumb_source.contains(QStringLiteral("signal doubleActivated(var asset, var modifiers)")) ||
      !carousel_media_thumb_source.contains(QStringLiteral("Drag.active: thumbHover.drag.active && root.online")) ||
      !carousel_media_thumb_source.contains(QStringLiteral("\"application/x-carousel-media\": JSON.stringify(root.asset)")) ||
      !carousel_media_thumb_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !carousel_media_thumb_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !carousel_media_thumb_source.contains(QStringLiteral("cursorShape: root.online ? Qt.PointingHandCursor : Qt.ForbiddenCursor")) ||
      !carousel_view_source.contains(QStringLiteral("delegate: CarouselMediaThumb")) ||
      !carousel_view_source.contains(QStringLiteral("asset: modelData")) ||
      !carousel_view_source.contains(QStringLiteral("root.placeLibraryAsset(asset, root.hasMultiSelectModifier(modifiers) ? \"layer\" : \"slot\")"))) {
    return fail("Carousel quick library thumbnails should use CarouselMediaThumb with readable media labels, online state, and preserved slot/layer placement");
  }
  if (!carousel_tool_rail_button_source.contains(QStringLiteral("carouselToolRailButtonContract: \"carousel-tool-rail-button:v2|stable-symbol:true|active-fill-marker:true|shortcut-tooltip:true|accessible-name-state:true|compact-label:true|rail-width:48|expandable-menu:true|checked-state:true|disabled-reasons:true|keyboard:true|readable-text:true|no-elide:true|geometry:true\"")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|tooltip-full:true|menu-wrap:true|accessibility-full:true\"")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("menuGeometryContract: \"menu-geometry:v1|min-width:\"")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("signal toolSelected(string toolValue)")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("signal menuItemTriggered(string toolValue, string itemKey)")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("function focusNextMenuItem(step)")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("delegate: PopupMenuRow")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("root.menuItemTriggered(root.toolValue, key)")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("Accessible.name: toolLabel + \" tool, shortcut \" + toolKey")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("Accessible.checked: toolActive")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("visible: root.compactLabelMode")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("anchors.rightMargin: root.compactLabelMode ? theme.scaled(14) : 0")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("visible: root.hovered || carouselToolMenu.visible")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("selected: root.toolActive")) ||
      !carousel_tool_rail_button_source.contains(QStringLiteral("fillColor: theme.commandFill(\"secondary\", root.toolActive")) ||
      !carousel_view_source.contains(QStringLiteral("delegate: CarouselToolRailButton")) ||
      !carousel_view_source.contains(QStringLiteral("compactLabelMode: true")) ||
      !carousel_view_source.contains(QStringLiteral("toolData: modelData")) ||
      !carousel_view_source.contains(QStringLiteral("activeTool: root.activeTool")) ||
      !carousel_view_source.contains(QStringLiteral("menuItems: root.carouselToolMenuItems(modelData.value)")) ||
      !carousel_view_source.contains(QStringLiteral("root.setActiveTool(toolValue)")) ||
      !carousel_view_source.contains(QStringLiteral("root.handleCarouselToolMenuAction(toolValue, itemKey)")) ||
      !app_icon_source.contains(QStringLiteral("case \"hand\":")) ||
      !app_icon_source.contains(QStringLiteral("case \"text-tool\":")) ||
      !app_icon_source.contains(QStringLiteral("case \"shape-tool\":")) ||
      !app_icon_source.contains(QStringLiteral("case \"brush-tool\":"))) {
    return fail("Carousel tool rail should use stable symbols, full tooltip labels, visible selected state, shortcut and accessibility metadata, keyboard menus, and preserved tool actions without widening the rail");
  }
  if (!color_swatch_button_source.contains(QStringLiteral("colorSwatchButtonContract: \"color-swatch-button:v1|selected:true|keyboard:true|hover:true|tooltip-full:true|readable-text:true|no-elide:true\"")) ||
      !color_swatch_button_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|tooltip-full:true|accessibility-full:true|label-source:tooltip\"")) ||
      !color_swatch_button_source.contains(QStringLiteral("signal picked(string swatchColor)")) ||
      !color_swatch_button_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      !color_swatch_button_source.contains(QStringLiteral("TapHandler {")) ||
      !color_swatch_button_source.contains(QStringLiteral("targetItem: root")) ||
      !color_swatch_button_source.contains(QStringLiteral("text: root.label")) ||
      !carousel_view_source.contains(QStringLiteral("delegate: ColorSwatchButton")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setCurrentSlideBackgroundColor(swatchColor)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setSelectedItemFrameColor(swatchColor)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setSelectedItemShadowColor(swatchColor)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setSelectedShapeFillColor(swatchColor)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setSelectedShapeSecondaryFillColor(swatchColor)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setSelectedTextColor(swatchColor)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setSelectedTextBackgroundColor(swatchColor)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setSelectedTextStrokeColor(swatchColor)"))) {
    return fail("Carousel color customization should use ColorSwatchButton with selected, keyboard, tooltip, and preserved color setter behavior");
  }
  if (!tool_option_toggle_source.contains(QStringLiteral("toolOptionToggleContract: \"tool-option-toggle:v2|checked-state:true|keyboard:true|hover:true|disabled-state:true|disabled-fallback:true|readable-text:true|no-elide:true|tooltip-full:true\"")) ||
      !tool_option_toggle_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !tool_option_toggle_source.contains(QStringLiteral("property string disabledFallbackReason: \"Unavailable in the current state.\"")) ||
      !tool_option_toggle_source.contains(QStringLiteral("readonly property string disabledTooltipText: !toggleEnabled")) ||
      !tool_option_toggle_source.contains(QStringLiteral("signal toggled(bool checked)")) ||
      !tool_option_toggle_source.contains(QStringLiteral("function toggle()")) ||
      !tool_option_toggle_source.contains(QStringLiteral("Keys.onSpacePressed: root.toggle()")) ||
      !tool_option_toggle_source.contains(QStringLiteral("Accessible.role: Accessible.CheckBox")) ||
      !tool_option_toggle_source.contains(QStringLiteral("Accessible.checked: checked")) ||
      !tool_option_toggle_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !tool_option_toggle_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !tool_option_toggle_source.contains(QStringLiteral("text: root.disabledTooltipText")) ||
      !mask_view_source.contains(QStringLiteral("ToolOptionToggle {\n                objectName: \"alphaMaskImportInvertCheck\"")) ||
      !mask_view_source.contains(QStringLiteral("onToggled: checked => editorPreview.alphaMaskImportInvert = checked")) ||
      !studio_workspace_source.contains(QStringLiteral("ToolOptionToggle {")) ||
      !studio_workspace_source.contains(QStringLiteral("checked: root.primaryBoolAdjustmentValue(\"blackAndWhiteEnabled\", editorPreview.blackAndWhiteEnabled)")) ||
      !studio_workspace_source.contains(QStringLiteral("checked: root.primaryBoolAdjustmentValue(\"colorReplacementEnabled\", editorPreview.colorReplacementEnabled)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"colorReplacementPreserveLuminanceCheckBox\"")) ||
      !studio_workspace_source.contains(QStringLiteral("checked: root.primaryBoolAdjustmentValue(\"colorReplacementPreserveLuminance\", editorPreview.colorReplacementPreserveLuminance)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"colorReplacementPreserveSaturationCheckBox\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"colorReplaceProtectSkinCheckBox\"")) ||
      !studio_workspace_source.contains(QStringLiteral("checked: root.primaryBoolAdjustmentValue(\"colorHarmonyEnabled\", editorPreview.colorHarmonyEnabled)")) ||
      !studio_workspace_source.contains(QStringLiteral("onToggled: function(checked) { root.setPrimaryBoolAdjustmentValue(\"colorHarmonyEnabled\", checked) }")) ||
      studio_workspace_source.contains(QStringLiteral("component CompactCheckBox")) ||
      mask_view_source.contains(QStringLiteral("CheckBox {"))) {
    return fail("Edit, Mask, and Studio tool option toggles should use ToolOptionToggle with readable no-elide labels while preserving base/layer RAW, color, crop, and alpha import boolean bindings");
  }
  if (!inspector_section_source.contains(QStringLiteral("sectionContract: \"inspector-section:v2|header:title-changed-before-after-reset-menu-chevron|fixed-header:true|fixed-actions:true|icon-first-actions:true|collapse:true|keyboard:true|disabled-reason:true|changed-state:true|no-nested-card:true|no-elide:true\"")) ||
      !inspector_section_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|title-fit:true|subtitle-wrap:true|tooltip-full:true|accessibility-full:true\"")) ||
      !inspector_section_source.contains(QStringLiteral("headerActionContract: \"inspector-header-actions:v1|before-after:icon-only-tooltip|reset:icon-only-tooltip|menu:menu-button|fixed-size:\"")) ||
      !inspector_section_source.contains(QStringLiteral("sectionGeometryContract: \"inspector-section-geometry:v2|left-edge:shared|header-min:\" + String(root.headerMinimumHeight) + \"|action-size:\"")) ||
      !inspector_section_source.contains(QStringLiteral("property string disabledReason: \"\"")) ||
      !inspector_section_source.contains(QStringLiteral("readonly property int headerMinimumHeight: theme.controlHeightDense")) ||
      !inspector_section_source.contains(QStringLiteral("readonly property int headerActionSize: theme.controlHeightDense")) ||
      !inspector_section_source.contains(QStringLiteral("Accessible.role: root.expandable ? Accessible.Button : Accessible.StaticText")) ||
      !inspector_section_source.contains(QStringLiteral("activeFocusOnTab: root.expandable")) ||
      !inspector_section_source.contains(QStringLiteral("function toggleExpanded()")) ||
      !inspector_section_source.contains(QStringLiteral("Keys.onReturnPressed: event =>")) ||
      !inspector_section_source.contains(QStringLiteral("Keys.onSpacePressed: event =>")) ||
      !inspector_section_source.contains(QStringLiteral("if (root.activeFocus) root.toggleExpanded()")) ||
      !inspector_section_source.contains(QStringLiteral("Layout.preferredWidth: root.headerActionSize")) ||
      !inspector_section_source.contains(QStringLiteral("showText: false")) ||
      !inspector_section_source.contains(QStringLiteral("MenuButton {")) ||
      !mask_view_source.contains(QStringLiteral("InspectorSection {")) ||
      !mask_inspector_source.contains(QStringLiteral("component MaskSection: InspectorSection")) ||
      !carousel_view_source.contains(QStringLiteral("InspectorSection {")) ||
      !export_view_source.contains(QStringLiteral("InspectorSection {"))) {
    return fail("Inspector-heavy pages should use shared InspectorSection with fixed header actions, changed/collapse/menu states, disabled reasons, keyboard behavior, and readable no-elide text");
  }
  if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_SKIP_VISIBLE_TEXT_SOURCE_CONTRACT") &&
      (!tool_value_slider_source.contains(QStringLiteral("toolValueSliderContract: \"tool-value-slider:v1|stable-size:true|keyboard:true|hover:true|tooltip-full:true|accessible-value:true|disabled-reason:true|neutral-rest:true|fixed-geometry:true|no-elide:true|premium-rail:true\"")) ||
      !tool_value_slider_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|tooltip-full:true|accessibility-full:true|value-fit:true\"")) ||
      !tool_value_slider_source.contains(QStringLiteral("valueGeometryContract: \"tool-value-slider-geometry:v1|min-width:120|track-height:2|thumb:12-14|layout-shift:false\"")) ||
      !tool_value_slider_source.contains(QStringLiteral("property string disabledReason: \"\"")) ||
      !tool_value_slider_source.contains(QStringLiteral("property bool modified: false")) ||
      !tool_value_slider_source.contains(QStringLiteral("readonly property bool accentFillVisible: root.pressed || root.hovered || root.visualFocus || root.modified")) ||
      !tool_value_slider_source.contains(QStringLiteral("Accessible.name: label.length > 0 ? label : tooltipText")) ||
      !tool_value_slider_source.contains(QStringLiteral("Accessible.description: !root.enabled && root.disabledReason.length > 0")) ||
      !tool_value_slider_source.contains(QStringLiteral("targetItem: root")) ||
      !tool_value_slider_source.contains(QStringLiteral("text: root.resolvedTooltipText")) ||
      !tool_value_slider_source.contains(QStringLiteral("root.visualPosition * parent.width")) ||
      !tool_value_slider_source.contains(QStringLiteral("handle: Rectangle")) ||
      !QRegularExpression(QStringLiteral("ToolValueSlider\\s*\\{\\s*id:\\s*transportSlider\\b")).match(timeline_view_source).hasMatch() ||
      !QRegularExpression(QStringLiteral("ToolValueSlider\\s*\\{\\s*id:\\s*zoomSlider\\b")).match(timeline_view_source).hasMatch() ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerOpacitySlider\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onMoved: editorPreview.setPhotoLayerOpacity(photoLayerPropertiesPanel.layerIndex, value / 100.0)")) ||
      !mask_view_source.contains(QStringLiteral("ToolValueSlider {\n                objectName: \"alphaMaskImportThresholdSlider\"")) ||
      !mask_view_source.contains(QStringLiteral("onMoved: editorPreview.alphaMaskImportThreshold = value")) ||
      !timeline_source_panel_source.contains(QStringLiteral("objectName: \"timelineSourceScrubber\"")) ||
      !timeline_source_panel_source.contains(QStringLiteral("label: \"Source position\"")) ||
      !timeline_source_panel_source.contains(QStringLiteral("onMoved: root.seekSource(value, true)")) ||
      QRegularExpression(QStringLiteral("\\n\\s*Slider\\s*\\{")).match(timeline_view_source).hasMatch() ||
      QRegularExpression(QStringLiteral("\\n\\s*Slider\\s*\\{")).match(timeline_source_panel_source).hasMatch() ||
      QRegularExpression(QStringLiteral("\\n\\s*Slider\\s*\\{")).match(mask_view_source).hasMatch() ||
      QRegularExpression(QStringLiteral("\\n\\s*Slider\\s*\\{")).match(studio_workspace_source).hasMatch())) {
    return fail("Timeline, source preview, Edit HSL, Mask alpha import, and Studio layer opacity sliders should use ToolValueSlider with stable readable premium rails and preserved movement bindings");
  }
  if (!tool_button_source.contains(QStringLiteral("variantBadgeContract: \"tool-variant-discovery:v1|visual-badge:false|tooltip-full:true|accessibility-full:true\"")) ||
      !tool_button_source.contains(QStringLiteral("menuAffordanceContract: \"tool-menu-affordance:v2|hover-chevron:true|corner:bottom-right|right-click:true|press-hold:true|keyboard:alt-down-alt-space|tooltip-full:true\"")) ||
      !tool_button_source.contains(QStringLiteral("|menu-affordance:hover-chevron")) ||
      !tool_button_source.contains(QStringLiteral("name: \"chevron-right\"")) ||
      !tool_button_source.contains(QStringLiteral("rotation: 90")) ||
      !tool_button_source.contains(QStringLiteral("property string variantLabel: \"\"")) ||
      !tool_button_source.contains(QStringLiteral("parts.push(variantLabel)")) ||
      !tool_button_source.contains(QStringLiteral("Accessible.description: !enabled || !commandEnabled ? resolvedTooltip : variantLabel")) ||
      tool_button_source.contains(QStringLiteral("variantLabel.slice(0, 2)"))) {
    return fail("ToolButton variants should remain discoverable through full tooltip and accessibility text without a duplicate visual badge");
  }
  if (!mini_button_source.contains(QStringLiteral("miniButtonContract: \"mini-button:v9|shared-component:true|active-state:true|roles:primary-secondary-segmented-ghost-icon-destructive|hierarchy:primary-graphite-secondary-neutral-tertiary-quiet|monochrome-default:true|neutral-primary:true|flat-surface:true|borderless-rest:true|centered-icons:true|hover-fill-not-border:true|keyboard:true|tooltip-full:true|readable-text:true|no-elide:true|disabled-reason-keeps-action-name:true|density-token-heights:true\"")) ||
      !mini_button_source.contains(QStringLiteral("readableTextContract: \"readable-text:v3|ellipsis:none|compact-fit:true|optional-wrap:true|tooltip-full:true|a11y-full:true|minimum-px:10|readable-width:true\"")) ||
      !mini_button_source.contains(QStringLiteral("readonly property bool accentEdgeVisible: root.enabled && root.slashEdge &&")) ||
      !mini_button_source.contains(QStringLiteral("background: CommandButtonSurface {")) ||
      !mini_button_source.contains(QStringLiteral("fillColor: theme.commandFill(root.destructive ? \"danger\" : root.primary ? \"primary\" : \"secondary\",")) ||
      !mini_button_source.contains(QStringLiteral("borderWidth: theme.commandBorderWidth(root.visualFocus)")) ||
      !mini_button_source.contains(QStringLiteral("property bool allowLabelWrap: false")) ||
      !mini_button_source.contains(QStringLiteral("Accessible.name: resolvedAccessibleName")) ||
      !mini_button_source.contains(QStringLiteral("Accessible.description: !root.enabled")) ||
      !mini_button_source.contains(QStringLiteral("text: root.resolvedTooltipText.length > 0 ? root.resolvedTooltipText : root.text")) ||
      !mini_button_source.contains(QStringLiteral("CommandButtonSurface {")) ||
      !mini_button_source.contains(QStringLiteral("wrapMode: root.allowLabelWrap ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap")) ||
      !mini_button_source.contains(QStringLiteral("maximumLineCount: root.allowLabelWrap ? 2 : 1")) ||
      !mini_button_source.contains(QStringLiteral("fontSizeMode: root.fitTextToWidth ? Text.HorizontalFit : Text.FixedSize")) ||
      !mini_button_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !studio_workspace_source.contains(QStringLiteral("MiniButton {")) ||
      studio_workspace_source.contains(QStringLiteral("component MiniButton: Button"))) {
    return fail("Studio mini controls should resolve to shared MiniButton with readable no-elide labels, full tooltips, keyboard access, and no inline workspace button definition");
  }
  if (      !studio_workspace_source.contains(QStringLiteral(": page === \"edit\" ? \"editStudioWorkspace\"")) ||
      !studio_workspace_source.contains(QStringLiteral("editWorkflowPolishContract: \"edit-workflow-polish:v1|first-glance:tool-rail-image-stage-active-tool-inspector-filmstrip|close-inspection:active-family-variant-target-hud-pending-actions|clutter:one-active-hud-one-primary-action-diagnostics-collapsed|interaction:select-expand-switch-commit-cancel-customize|text:no-elide-no-overlap|functionality:preserved\"")) ||
      !studio_workspace_source.contains(QStringLiteral("editPageCompletionContract: \"page-completion:v1|edit|rail:primary-tool-families|content:image-viewport-toolbar-hud|inspector:active-tool-targeted-sections-layers-masks-agent|filmstrip:stable-selection|bottom:rating-flag-export|states:no-photo-active-tool-pending-disabled-error\"")) ||
      !studio_workspace_source.contains(QStringLiteral("editStudioVisibilityContract: \"edit-studio-visibility:v1|always:edit-context-tool-rail-image-viewport-zoom-toolbar-action-toolbar-inspector-filmstrip|contextual:mask-creation-refine-toolbar-mask-stack-active-hud-layer-actions-selected-target|one-expansion:tool-variants-mask-sources-layer-actions-presets-export-agent|hover-focus:layer-row-actions-filmstrip-actions-reorder-controls|diagnostic:preview-failure-agent-runtime-jobs|preserve:functionality\"")) ||
      !studio_workspace_source.contains(QStringLiteral("editStudioSurfaceContract: \"edit-studio-surfaces:v1|context-panel:target-and-library|tool-rail:families-visible|image-viewport:dominant|toolbars:view-actions|inspector:adjust-mask-layer-agent|filmstrip:stable|layers:fixed-zones|masks:source-stack|no-elide:true|no-overlap:true\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function editWorkflowPolishSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("function studioProfessionalUxProofSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("studio-professional-ux-proof:v1")) ||
      !studio_workspace_source.contains(QStringLiteral("function studioInspectorWorkflowProofSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("studio-inspector-workflow-proof:v1")) ||
      !studio_workspace_source.contains(QStringLiteral("function studioInteractionResponsivenessProofSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("studio-interaction-responsiveness-proof:v1")) ||
      !studio_image_viewport_source.contains(QStringLiteral("mediaSurfaceContract: \"media-surface:v1|canvas-stage:dominant|image-corners:consistent|empty-loading-failure:same-bounds|chrome:quiet|badges:corner-stacked|toolbar-baseline:single\"")) ||
      !studio_image_viewport_source.contains(QStringLiteral("previewChromeContract: \"preview-chrome:v2|status:global-under-navigation|diagnostics:overlay-menu|top-controls:baseline|failure:global-retry|content-dominant:true\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"editContextPanel\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"editImageViewport\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"editZoomToolbar\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"editActionToolbar\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"editFilmstrip\"")) ||
      !studio_workspace_source.contains(QStringLiteral("required property string thumbnailSource")) ||
      !studio_workspace_source.contains(QStringLiteral("thumbnailSource: parent.thumbnailSource")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"editInspectorPanel\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"editInspectorFooter\""))) {
    return fail("Edit workspace should declare page-level visibility/function contracts and keep tool rail, image stage, canvas toolbars, inspector, filmstrip, and footer surfaces testable");
  }
  if (!mask_view_source.contains(QStringLiteral("objectName: \"maskWorkspace\"")) ||
      !mask_view_source.contains(QStringLiteral("maskVisibilityContract: \"mask-visibility:v1|always:canvas-inspector-active-context-source-controls-overlay-state|contextual:brush-cursor-lasso-polygon-handles-alpha-import-refine-toolbar-pending-apply-cancel|one-expansion:mask-source-variants-context-menu-refine-options-adjustment-groups|hover-focus:mask-source-actions-reorder-visibility-solo-lock|diagnostic:generated-unsupported-preview-failure-runtime-detail|preserve:functionality\"")) ||
      !mask_view_source.contains(QStringLiteral("maskSurfaceContract: \"mask-surfaces:v1|canvas:dominant-overlay|sources:generated-geometric-brush-alpha|inspector:active-mask-stack-adjustments|overlays:brush-lasso-polygon-outline|pending:apply-cancel-retry-visible|context-menu:one-expansion|no-elide:true|no-overlap:true\"")) ||
      !mask_view_source.contains(QStringLiteral("maskWorkflowPolishContract: \"mask-workflow-polish:v1|first-glance:canvas-active-selection-inspector-source-tools|close-inspection:context-source-operation-preview-refine-pending-actions|clutter:provider-details-diagnostic-only-one-active-context|interaction:create-select-refine-combine-preview-rename-reorder-delete|text:no-elide-no-overlap|functionality:preserved\"")) ||
      !mask_view_source.contains(QStringLiteral("maskPageCompletionContract: \"page-completion:v1|masks|sidebar:source-variant-controls-search|content:canvas-overlay-brush-lasso-polygon-handles|inspector:active-context-mask-stack-refine-adjustments|toolbar:combine-preview-import-retry-apply-cancel|states:no-mask-active-mask-generating-locked-empty-error\"")) ||
      !mask_view_source.contains(QStringLiteral("function maskWorkflowPolishSignature()")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskWorkspaceStableSplit\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskCanvasPane\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskCanvasViewport\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskInspectorPane\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskActiveContextHeader\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSearchField\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskGeneratedSubjectButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskSourceContextMenu\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskEmptyStateGuidance\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskViewBrushCursor\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"maskViewActiveSelectionOutline\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"alphaMaskImportPreviewPanel\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"alphaMaskImportCancelButton\"")) ||
      !mask_view_source.contains(QStringLiteral("objectName: \"alphaMaskImportCommitButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskCreationToolbar\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskRefineToolbar\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskRefineToolbarStatusText()")) ||
      !studio_workspace_source.contains(QStringLiteral("text: root.maskRefineToolbarStatusText()")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string debugSignature: root.maskCreateRefineSplitSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("diagnosticTextContract: \"diagnostic-text:v1|visible-copy:user-facing|signature:property-only|tooltip:developer-only|no-debug-text-at-rest:true\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskStackStrip\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskToolRail\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskSelectionMenuButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskFirstInspectorGroup\"")) ||
      !studio_workspace_source.contains(QStringLiteral("maskWorkflowPolishContract: \"mask-workflow-polish:v1|first-glance:tool-rail-mask-sources-canvas-overlay-inspector-stack|close-inspection:active-context-source-operation-preview-refine-pending-actions|clutter:provider-details-diagnostic-only-one-active-context|interaction:create-select-refine-combine-preview-rename-reorder-delete|text:no-elide-no-overlap|functionality:preserved\"")) ||
      !studio_workspace_source.contains(QStringLiteral("maskPageCompletionContract: \"page-completion:v1|masks-in-edit|rail:source-variant-controls|content:image-overlay-brush-lasso-polygon-hud|inspector:mask-first-controls-stack-layers|toolbar:combine-preview-import-retry-apply-cancel|states:no-mask-active-mask-generating-locked-empty-error\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskWorkflowPolishSignature()"))) {
    return fail("Mask workspace should declare page-level visibility/function contracts and keep canvas, inspector, source controls, overlays, stack, tool rail, alpha import, and pending actions testable");
  }
  if (!carousel_layer_row_source.contains(QStringLiteral("carouselLayerRowContract: \"carousel-layer-row:v2|zones:primary-edge-label-state-secondary-edge|selected:\"")) ||
      !carousel_layer_row_source.contains(QStringLiteral("|keyboard:true|multi-select:true|readable-text:true|primary-selection:left-edge|secondary-selection:right-edge")) ||
      !carousel_layer_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|state-icons:true\"")) ||
      !carousel_layer_row_source.contains(QStringLiteral("zoneContract: \"carousel-layer-row-zones:v2|primary-edge|type-icon|label|hidden-state|locked-state|secondary-edge\"")) ||
      !carousel_layer_row_source.contains(QStringLiteral("statePriorityContract: \"layer-row-state-priority:v2|primary-selected>multi-selected-secondary>locked>hidden>hover\"")) ||
      !carousel_layer_row_source.contains(QStringLiteral("visible: root.primarySelected")) ||
      !carousel_layer_row_source.contains(QStringLiteral("visible: root.selected && !root.primarySelected")) ||
      !carousel_layer_row_source.contains(QStringLiteral("interactionContract: \"carousel-layer-row-interaction:v1|keyboard:true|multi-select:true|canvas-drag:true|state-icons:true|tooltip-full:true\"")) ||
      !carousel_layer_row_source.contains(QStringLiteral("signal activated(int modifiers)")) ||
      !carousel_layer_row_source.contains(QStringLiteral("name: \"hidden\"")) ||
      !carousel_layer_row_source.contains(QStringLiteral("name: \"lock\"")) ||
      !carousel_view_source.contains(QStringLiteral("delegate: CarouselLayerRow")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselLayerRow\"")) ||
      !carousel_view_source.contains(QStringLiteral("root.hasMultiSelectModifier(modifiers)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.toggleItemSelection(String(modelData.itemId || \"\"))")) ||
      !carousel_view_source.contains(QStringLiteral("root.focusLayerItem(String(modelData.itemId || \"\"), Boolean(modelData.selected))"))) {
    return fail("Carousel object layers should use CarouselLayerRow with state icons, readable text, keyboard activation, and preserved multi-select behavior");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselPrecisionLayoutContract: \"carousel-precision-layout:v1|persistent-settings:true|multi-selection-transform:true|anchor-positioning:9-point|grid:columns-rows-margins-gutters|rulers:create-guides|custom-guides:drag-remove|snap:priority-distance-equal-gap|safe-bleed:true|nudge:configurable|presets:4|undo:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("readonly property var layoutSettings: carouselDocument.layoutSettings")) ||
      !carousel_view_source.contains(QStringLiteral("function layoutGridForSlide(index)")) ||
      !carousel_view_source.contains(QStringLiteral("function selectionAnchorPosition()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyLayoutPreset(modelData.key)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.addLayoutGuide(\"vertical\", mouse.x / root.stripScale)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.addLayoutGuide(\"horizontal\", mouse.y / root.stripScale)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setLayoutSetting(\"aspectLocked\", !root.transformAspectLocked)")) ||
      !carousel_view_source.contains(QStringLiteral("\"Equal gap\", 6, true")) ||
      !carousel_view_source.contains(QStringLiteral("label: guide.label + \"  \" + distance.toFixed(1) + \" px\""))) {
    return fail("Carousel precision layout should keep persistent grids, rulers, draggable guides, measured snapping, multi-selection transforms, anchor positioning, presets, and configurable nudges wired to the document model");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselBoardLayoutTokensContract: \"carousel-board-layout-tokens:v1|board-local:true|columns:true|rows:true|margins:true|gutters:true|canvas-visualization:true|snap:true|presets:true|undo:true|persistence:true|never-export:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("const board = slideData(index)")) ||
      !carousel_view_source.contains(QStringLiteral("board ? board.layoutColumns : 4")) ||
      !carousel_view_source.contains(QStringLiteral("board ? board.layoutRows : 4")) ||
      !carousel_view_source.contains(QStringLiteral("board ? board.layoutMarginX : 72")) ||
      !carousel_view_source.contains(QStringLiteral("board ? board.layoutGutterY : 24")) ||
      !carousel_view_source.contains(QStringLiteral("function setCurrentBoardLayoutToken(key, value)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setCurrentSlideLayoutTokens(columns, rows, marginX, marginY, gutterX, gutterY)")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Board-local layout — this grid belongs only to canvas \"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Gutter X\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Gutter Y\"")) ||
      !carousel_view_source.contains(QStringLiteral("onInteractionStarted: carouselDocument.beginHistoryTransaction(\"Change board layout tokens\")")) ||
      !carousel_view_source.contains(QStringLiteral("onInteractionFinished: carouselDocument.commitHistoryTransaction()"))) {
    return fail("Carousel layout tokens should be board-local, visible on canvas, editable by axis, persistent, and grouped into atomic undo gestures");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselContinuityPreviewContract: \"carousel-continuity-preview:v1|linked-spread:true|actual-export-cut:true|rendered-board-previews:true|platform-gutter-simulation:true|unsafe-seam-band:true|critical-content-warning:true|jump-to-layer:true|never-export:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("id: carouselContinuityPreviewDialog")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Preview actual seam\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Simulated platform gutter\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"None\", pixels: 0 }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Standard 16 px\", pixels: 16 }")) ||
      !carousel_view_source.contains(QStringLiteral("source: String(root.continuityPreview.leftPreviewSource || \"\")")) ||
      !carousel_view_source.contains(QStringLiteral("source: String(root.continuityPreview.rightPreviewSource || \"\")")) ||
      !carousel_view_source.contains(QStringLiteral("\"Critical content crosses the unsafe seam\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.focusPreflightIssue("))) {
    return fail("Carousel linked spreads should expose their exact rendered export cut, simulated platform gutters, unsafe seam bands, crossing-layer warnings, and direct layer selection");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselNeighborComparisonContract: \"carousel-neighbor-comparison:v1|previous-overlay:true|next-overlay:true|two-up:true|live-previews:true|current-focus:true|direct-open:true|temporary:true|never-export:true|keyboard:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselPreviousBoardOverlay\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselNextBoardOverlay\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCompareNeighborOverlay\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselNeighborComparisonDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselNeighborComparisonSurface\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselNeighborComparisonLeftPreview\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselNeighborComparisonRightPreview\"")) ||
      !carousel_view_source.contains(QStringLiteral("source: String(root.neighborComparison.leftPreviewSource || \"\")")) ||
      !carousel_view_source.contains(QStringLiteral("source: String(root.neighborComparison.rightPreviewSource || \"\")")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Live board previews · temporary comparison · never exports\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.selectAdjacentSlide(-1)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.selectAdjacentSlide(1)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselNeighborComparisonDialog.openBoard("))) {
    return fail("Carousel composer should expose previous and next artboard controls plus a temporary live two-up neighbor comparison with direct board return");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselBoardRangeCommandContract: \"carousel-board-range-command:v1|operations:background-template-brand-spacing-text-style|scopes:current-selected-following-all|preview:exact-boards|linked-spread-guard:true|atomic:true|one-step-undo:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("menuObjectName: \"carouselBoardActionsMenu\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Apply to board range…\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBoardRangeCommandDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Background\", value: \"background\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Layout\", value: \"template\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Brand style\", value: \"brand\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Spacing\", value: \"spacing\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Text style\", value: \"text-style\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Current\", value: \"current\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Selected\", value: \"selected\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Following\", value: \"following\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"All boards\", value: \"all\" }")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewBoardRangeCommand(")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBoardRangePreview\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselApplyBoardRangeCommandButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyBoardRangeCommand("))) {
    return fail("Carousel board range commands should preview and atomically apply background, layout, brand, spacing, and text style to current, selected, following, or all boards");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselDocumentStructureContract: \"carousel-document-structure:v1|empty-required-slots:true|duplicate-copy:true|unmatched-components:true|broken-links:true|style-outliers:true|category-filter:true|direct-jump:true|live-refresh:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("menuObjectName: \"carouselBoardActionsMenu\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Inspect document structure\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.documentStructureIssues()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselDocumentStructureDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Empty slots\", value: \"empty-slots\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Duplicate copy\", value: \"duplicate-copy\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Unmatched components\", value: \"unmatched-components\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Broken links\", value: \"broken-links\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Style outliers\", value: \"style-outliers\" }")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselDocumentStructureList\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselDocumentStructureJumpButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.focusDocumentStructureIssue("))) {
    return fail("Carousel document structure inspection should categorize empty slots, duplicate copy, component-link faults, and style outliers with direct board or layer jumps");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselSafeAreaContract: \"carousel-safe-area:v1|instagram-feed:true|reels-stories:true|tiktok:true|generic-device-ui:true|preview-only:true|never-export:true|undo:true|persistence:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Safe-area preview preset\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Instagram Feed\", key: \"instagram-feed\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Reels / Stories\", key: \"reels-stories\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"TikTok\", key: \"tiktok\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Generic Device UI\", key: \"device-ui\" }")) ||
      !carousel_view_source.contains(QStringLiteral("String(root.layoutSettings.safePreset || \"custom\")")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applySafeAreaPreset(modelData.key)")) ||
      !carousel_view_source.contains(QStringLiteral("Preview overlay only — safe-area guides never bake into exported files."))) {
    return fail("Carousel safe-area presets should expose platform-specific, persistent preview overlays that are explicitly excluded from export");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselTextLayoutContract: \"carousel-text-layout:v1|line-height:true|paragraph-spacing:true|text-inset:true|maximum-lines:true|vertical-overflow:true|shared-preview-export-layout:true|undo:true|persistence:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: layerItem.modelData.textLayoutLines || []")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Text overflow\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Line height\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Paragraph\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Text inset\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Maximum lines\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setSelectedTextLineHeight(value)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setSelectedTextParagraphSpacing(value)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setSelectedTextInset(value)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setSelectedTextMaximumLineCount(Math.round(value))"))) {
    return fail("Carousel text layout controls should share saved line geometry between preview and export and expose overflow without truncating UI labels");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselTextFitContract: \"carousel-text-fit:v1|content-height:true|auto-size:true|fixed-warn:true|minimum:true|maximum:true|fitted-state:true|live-copy:true|frame-resize:true|typography-change:true|undo:true|persistence:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselTextFittingControls\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Content height\", value: \"content\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Auto size\", value: \"auto\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Fixed + warn\", value: \"fixed\" }")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setSelectedTextFitSettings(")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Minimum\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Maximum\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: String(selection.textFitState || \"\")"))) {
    return fail("Carousel text fitting should expose content-height, bounded automatic sizing, fixed-size warning, and live fitted-state controls");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselRichTextContract: \"carousel-rich-text:v1|range-based:true|emphasis:true|links:true|color:true|weight:true|italic:true|superscript:true|edit-remap:true|shared-measurement:true|preview-export-parity:true|undo:true|persistence:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("function applyRichTextSpan(attributes)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applySelectedTextSpan(start, end - start, attributes)")) ||
      !carousel_view_source.contains(QStringLiteral("function clearRichTextSpan()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.clearSelectedTextSpan(start, end - start)")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselRichTextSpanControls\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Emphasis\", attributes: { emphasis: true } }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Italic\", attributes: { italic: true } }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Superscript\", attributes: { superscript: true } }")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselRichTextColorField\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselRichTextLinkField\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselApplyRichTextLinkButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselClearRichTextSpanButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: richTextLine.segments")) ||
      !carousel_view_source.contains(QStringLiteral("Number(modelData.pointSizeScale || 1)")) ||
      !carousel_view_source.contains(QStringLiteral("font.underline: Boolean(modelData.underline)"))) {
    return fail("Carousel rich text should apply removable range formatting for emphasis, links, color, weight, italic, and superscript through shared measured preview/export segments");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselTextStylesContract: \"carousel-text-styles:v1|type-scale:true|named:true|custom:true|selected-boards:true|all-document:true|affected-preview:true|explicit-apply:true|one-step-undo:true|persistence:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselNamedTextStyles\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: carouselDocument.textStyles")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewTextStyleApplication(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewTextStyleUpdate(")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Update style · selected boards\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Update style · all boards\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.createTextStyleFromSelection(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyTextStylePreview()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.clearTextStylePreview()"))) {
    return fail("Carousel named text styles should preview and explicitly apply reusable type-scale changes to selected boards or the whole document");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselDocumentVariablesContract: \"carousel-document-variables:v1|keys:title,client,campaign,date,url,handle,board_number,total_boards|raw-tokens:true|resolved-layout:true|resolved-preview:true|board-aware:true|snippets:true|detach-to-text:true|undo:true|persistence:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselDocumentVariables\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: carouselDocument.documentVariableKeys")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.insertDocumentVariable(")) ||
      !carousel_view_source.contains(QStringLiteral("text: String(selection.resolvedText || \"\")")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Detach variables to text\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.saveSelectedTextAsSnippet(")) ||
      !carousel_view_source.contains(QStringLiteral("model: carouselDocument.copySnippets")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.insertCopySnippet("))) {
    return fail("Carousel document copy should expose reusable snippets, board-aware variables, resolved preview, and safe detach-to-text");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselListsNumberingContract: \"carousel-lists-numbering:v1|bullets:true|numbering:true|non-destructive:true|hanging-indent:true|shared-line-geometry:true|auto-board:true|auto-step:true|reorder-aware:true|undo:true|persistence:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselListsAndNumbering\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Bullets\", value: \"bullets\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Numbered\", value: \"numbering\" }")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Hanging indent\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setSelectedTextHangingIndent(value)")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Board order\", value: \"board\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Step order\", value: \"step\" }")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setSelectedTextAutoNumberSource(modelData.value)"))) {
    return fail("Carousel text should expose non-destructive lists, shared hanging-indent geometry, and reorder-aware board or step number fields");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselFocusedCropContract: \"carousel-focused-crop:v1|outside-dim:true|rule-overlays:thirds-golden-center|reset:true|numeric-zoom-position:true|keyboard-nudge:true|previous-next-media:true|manual-authoritative:true|undo:true|preview-export-parity:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselFocusedCropControls\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: root.focusedCropMode ? \"Done cropping\" : \"Open focused crop\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Reset crop\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Previous media\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Next media\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Rule of thirds\", value: \"thirds\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Golden ratio\", value: \"golden\" }")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.nudgeSelectedItemMediaByPixels(-distance, 0)")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Crop · arrows 1 px · Shift + arrows 10 px · Esc done\""))) {
    return fail("Carousel focused crop mode should expose outside dimming, composition rules, reset, numeric state, keyboard nudge, and media navigation without entering export data");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselCropOwnershipContract: \"carousel-crop-ownership:v1|use-edit-crop:true|use-original:true|use-carousel-crop:true|edit-crop-isolated:true|original-bypasses-edits:true|carousel-keeps-color:true|explicit-only:true|undo:true|persistence:true|preview-export-parity:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCropOwnership\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Use Edit crop\", value: \"edit\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Use original\", value: \"original\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Use Carousel crop\", value: \"carousel\" }")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setCurrentMediaCropSource(String(currentValue || \"edit\"))")) ||
      !carousel_view_source.contains(QStringLiteral("Original pixels are used; saved photo edits and their crop remain untouched.")) ||
      !carousel_view_source.contains(QStringLiteral("Photo adjustments stay active, the Edit crop is bypassed, and Carousel framing is authoritative."))) {
    return fail("Carousel media should make Edit, original, or Carousel crop ownership explicit without mutating saved photo edits");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselPhotoFramesContract: \"carousel-photo-frames:v1|bordered-print:true|rounded-card:true|instant-photo-caption:true|contact-sheet-cell:true|browser-device:true|editorial-mount:true|editable-compositions:true|grouped:true|undo:true|persistence:true|export:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselPhotoFrameComponents\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: carouselDocument.photoFramePresets")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyPhotoFramePreset(")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Editable photo frames\""))) {
    return fail("Carousel photo frames should expose all six presets as grouped, editable, normally rendered compositions");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselVideoCompositionContract: \"carousel-video-composition:v1|trim-in-out:true|poster-frame:true|audio-policy:mute-preserve|loop-intent:true|duration:true|board-playback-preview:true|trim-aware-preview:true|export-enforced:true|undo:true|persistence:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselVideoCompositionControls\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselSlotVideoCompositionLoader\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselLayerVideoCompositionLoader\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Trim in\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Trim out\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Poster frame\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setCurrentVideoTrim(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setCurrentVideoPosterFrame(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setCurrentVideoAudioPolicy(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setCurrentVideoLoopIntent(")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselVideoPreviewDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselVideoBoardPlaybackSurface\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselVideoPreviewScrubber\"")) ||
      !carousel_view_source.contains(QStringLiteral("videoOutput: carouselVideoPreviewOutput")) ||
      !carousel_view_source.contains(QStringLiteral("audioOutput: carouselVideoPreviewAudio")) ||
      !carousel_view_source.contains(QStringLiteral("if (carouselVideoPreviewDialog.loopIntent)"))) {
    return fail("Carousel video composition should expose persistent timing, poster, audio, loop, and trim-aware board playback for slots and layers");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselMediaUsageContract: \"carousel-media-usage:v1|duplicates:true|unused-selection:true|reused-crops:true|offline:true|dependent-boards:true|placement-jump:true|autofill-overflow:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselMediaUsageButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselMediaUsageDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.mediaUsageReport(")) ||
      !carousel_view_source.contains(QStringLiteral("mediaUsageReportState.duplicateSourceCount")) ||
      !carousel_view_source.contains(QStringLiteral("mediaUsageReportState.offlineSourceCount")) ||
      !carousel_view_source.contains(QStringLiteral("mediaUsageReportState.unusedSelectionCount")) ||
      !carousel_view_source.contains(QStringLiteral("model: mediaUsageCard.modelData.placements || []")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.focusMediaUsagePlacement(")) ||
      !carousel_view_source.contains(QStringLiteral("Unused autofill selection")) ||
      !carousel_view_source.contains(QStringLiteral("Unused · no open Carousel target"))) {
    return fail("Carousel media usage should report duplicates, crop reuse, offline dependencies, exact boards, and unused autofill sources with direct placement jumps");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselSelectionBatchContract: \"carousel-selection-batch:v1|match-size:true|match-width:true|match-height:true|match-style:true|swap-position:true|replace-content:true|anchor-explicit:true|preview:true|compatible-skip:true|one-step-undo:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselSelectionBatchCommands\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Match size\", operation: \"match-size\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Match width\", operation: \"match-width\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Match height\", operation: \"match-height\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Match style\", operation: \"match-style\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Swap position\", operation: \"swap-position\", exactTwo: true }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Replace content\", operation: \"replace-content\" }")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewSelectionBatchOperation(operation)")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselSelectionBatchDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Before · \"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Result · \"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselApplySelectionBatchButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applySelectionBatchOperation()"))) {
    return fail("Carousel multi-selection should preview and atomically apply match, swap, and replace commands from an explicit anchor while showing incompatible skips");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselTransformFieldsContract: \"carousel-transform-fields:v1|coordinate-space:board|units:px-degrees|anchor:9-point|aspect-lock:visible-persistent|relative:add-subtract-multiply-divide|absolute-prefix:equals|mixed:x-y-width-height-rotation|multi-selection:true|undo:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselTransformFieldState\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"X px\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Y px\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"W px\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"H px\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"R °\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyTransformExpression(")) ||
      !carousel_view_source.contains(QStringLiteral("root.transformAspectLocked")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Board coordinates · px / °\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Mixed · \" + root.transformMixedLabel()")) ||
      !carousel_view_source.contains(QStringLiteral("Type +24, -10, *1.2, or /2 for a relative change."))) {
    return fail("Carousel transforms should expose board-relative units, persistent aspect lock, arithmetic expressions, exact negatives, and clear mixed multi-selection state");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselRepeatGridContract: \"carousel-repeat-grid:v1|rows:1-10|columns:1-10|gap-x:true|gap-y:true|ordering:row-column-major|projected-bounds:true|projected-layer-count:true|preview:true|atomic:true|detached:true|editable:true|unique-identities:true|undo:true|persistence:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselRepeatGridButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewRepeatGrid(")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselRepeatGridDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Rows\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Columns\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Gap X\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Gap Y\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Across rows\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Down columns\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselRepeatGridPreviewSurface\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Detached + editable\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselApplyRepeatGridButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyRepeatGrid()"))) {
    return fail("Carousel repeat grid should preview rows, columns, independent gaps, ordering, projected bounds/layers, and atomically create detached editable results");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselConstraintStackContract: \"carousel-constraint-stack:v1|explicit-only:true|axes:vertical-horizontal|ordered:true|padding:true|gap:true|copy-reflow:true|media-size-preserved:true|move-as-unit:true|manual-reflow:true|detach-keeps-geometry:true|undo:true|persistence:true|export:true|no-hidden-auto-layout:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselConstraintStackControls\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCreateVerticalStackButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCreateHorizontalStackButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.createConstraintStack(\"vertical\", 24, 16)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.createConstraintStack(\"horizontal\", 24, 16)")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Stack padding\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Item gap\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.updateSelectedConstraintStack(")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselReflowConstraintStackButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.reflowSelectedConstraintStack()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselDetachConstraintStackButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.detachSelectedConstraintStack()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselConstraintStackOutline\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Explicit\""))) {
    return fail("Carousel constraint stacks should be explicit, visible, editable vertical or horizontal relationships with padding, gaps, manual reflow, and geometry-preserving detach");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselCrossDocumentClipboardContract: \"carousel-cross-document-clipboard:v1|kinds:boards-components|system-clipboard:true|versioned-payload:true|fresh-identities:true|groups:true|stacks:true|repeated-links:true|missing-assets:true|missing-fonts:true|style-policy:preserve-map-brand|aspect-adaptation:true|atomic-undo:true|save-reopen:true|export:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("function openClipboardPasteReview()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewClipboardPaste()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCopyBoardsButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.copySelectedBoards()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselReviewClipboardPasteButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselClipboardPasteDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselClipboardResourceSummary\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Missing asset · \"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Missing font · \"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Preserve source\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Map brand roles\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselClipboardBrandTargetCombo\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselApplyClipboardPasteButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyClipboardPaste("))) {
    return fail("Carousel cross-document clipboard should copy boards or components, review missing assets/fonts, remap identities, optionally map brand roles, and apply atomically");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselLayoutRecipesContract: \"carousel-layout-recipes:v1|layout-only:true|grid:true|guides:true|safe-areas:true|margins:true|gutters:true|repeated-spacing:true|preview:true|style-mutations:zero|content-mutations:zero|atomic-apply:true|undo:true|library-persistence:true|export-invariant:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselLayoutRecipeControls\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselLayoutRecipeNameField\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselSaveLayoutRecipeButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.saveCurrentLayoutRecipe(")) ||
      !carousel_view_source.contains(QStringLiteral("function openLayoutRecipePreview(recipeId)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewLayoutRecipe(recipeId)")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselLayoutRecipeDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("preview.beforeSettings")) ||
      !carousel_view_source.contains(QStringLiteral("preview.afterSettings")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselApplyLayoutRecipeButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyPreviewedLayoutRecipe()"))) {
    return fail("Carousel layout recipes should save, preview, and atomically apply only grids, guides, safe areas, margins, gutters, and repeated spacing with zero style/content changes");
  }
  // Keep unrelated Carousel and Timeline source contracts out of the native
  // selection pointer shard; the dedicated source-contract shard still runs
  // these checks without the selection-shard environment.
  if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER")) {
  if (!carousel_view_source.contains(QStringLiteral("carouselVariantComparisonContract: \"carousel-variant-comparison:v1|captured-left:true|live-right:true|synchronized-board-navigation:true|bounded-rendered-previews:true|different-state:true|keep-left:true|keep-right:true|keep-both-as-version:true|restore-version:true|delete-version:true|atomic-undo:true|persistence:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselVariantComparisonControls\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCaptureVariantButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.captureVariantComparisonBaseline(")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselOpenVariantComparisonButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.refreshVariantComparison()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselVariantComparisonDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setVariantComparisonBoardIndex(")) ||
      !carousel_view_source.contains(QStringLiteral("key: \"keep-left\"")) ||
      !carousel_view_source.contains(QStringLiteral("key: \"keep-right\"")) ||
      !carousel_view_source.contains(QStringLiteral("key: \"keep-both\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselVariantComparisonApplyButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.restoreSavedDesignVersion(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.deleteSavedDesignVersion("))) {
    return fail("Carousel variant comparison should render synchronized left/current boards and expose explicit keep-left, keep-right, keep-both, restore, and delete actions");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselMediaAutofillContract: \"carousel-media-autofill:v1|ordered-selection:true|slot-mapping-preview:true|reorder:true|skip:true|atomic-apply:true|undo:true|persistence:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselMediaAutofillDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewMediaAutofill(paths)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.moveMediaAutofillEntry(index, index - 1)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setMediaAutofillEntrySkipped(index, !Boolean(modelData.skipped))")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyMediaAutofillPlan()"))) {
    return fail("Carousel media autofill should preview, reorder, skip, and atomically apply an ordered source-to-slot mapping");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselStoryboardContract: \"carousel-storyboard:v1|all-boards:true|useful-size:true|drag-reorder:true|duplicate:true|delete:true|multi-select:true|role-assignment:true|return-to-composer:true|undo:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselStoryboardDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Storyboard overview\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: carouselDocument.slides")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setSelectedSlidesRole(modelData.value)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.duplicateSelectedSlides()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.removeSelectedSlides()")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Open in composer\""))) {
    return fail("Carousel storyboard overview should expose useful-size boards with reorder, multi-select, role, duplicate, delete, and composer return operations");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselBoardSectionsContract: \"carousel-board-sections:v1|named:true|consecutive-selection:true|linked-spread-boundary:true|overview:true|collapse:true|expand:true|move-complete:true|preserve-internal-order:true|rename:true|ungroup:true|undo:true|save-reopen:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCreateBoardSectionControls\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselStoryboardSectionNameField\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCreateBoardSectionButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.createBoardSection(root.storyboardSectionName)")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBoardSectionsOverview\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: carouselDocument.boardSections")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBoardSectionRenameField\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.renameBoardSection(")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBoardSectionCollapseButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setBoardSectionCollapsed(")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.moveBoardSection(")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBoardSectionUngroupButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.removeBoardSection(")) ||
      !carousel_view_source.contains(QStringLiteral("model: carouselDocument.storyboardEntries")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCollapsedBoardSectionSummary\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.selectBoardSection("))) {
    return fail("Carousel storyboard should create, rename, collapse, expand, select, move, and ungroup persistent named board sections without splitting their internal order");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselRepeatedElementLinksContract: \"carousel-repeated-element-links:v1|multi-part:true|scopes:selected-following-all|board-relative:true|automatic-sync:true|geometry:true|style:true|content:true|media:true|detach-board-override:true|unlink-all:true|undo:true|save-reopen:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselRepeatedElementControls\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCreateRepeatedElementLinkButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselRepeatedElementLinkDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselRepeatedElementLinkNameField\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Selected\", value: \"selected\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Following\", value: \"following\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"All boards\", value: \"all\" }")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselApplyRepeatedElementLinkButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.createRepeatedElementLink(")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselDetachRepeatedElementButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.detachSelectedRepeatedElement()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselUnlinkRepeatedElementButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselRepeatedElementSyncExplanation\"")) ||
      !carousel_view_source.contains(QStringLiteral("root.repeatedElementBoardSummary()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.removeRepeatedElementLink("))) {
    return fail("Carousel repeated elements should directly link selected layers across explicit scopes, expose synchronized state, and support board-local detach or unlink-all operations");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselSequenceTemplateContract: \"carousel-sequence-templates:v1|atomic:true|visual-cards:true|full-preview:true|metadata:true|editable-layers:true|roles:true|component-links:true|placeholders:true|undo:true|save-reopen:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: root.filteredTemplateLibraryEntries()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselSequenceTemplateDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: root.sequenceTemplatePreview.previewRoles || []")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselApplyTemplateLibraryEntryButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyTemplateLibraryEntry("))) {
    return fail("Carousel sequence templates should expose visual metadata and full previews before one atomic editable application");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselTemplateLibraryContract: \"carousel-template-library:v1|unified-metadata:true|search:true|goal-category-filter:true|favorites:persistent|recents:persistent|sections:all-favorites-recent-installed-custom|density:compact-comfortable|installed-preview:role-metadata|custom-preview-max:320x400|card-decode-max:160x200|no-full-resolution-card-assets:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("property string templateLibrarySearchText: \"\"")) ||
      !carousel_view_source.contains(QStringLiteral("function templateLibraryCategories()")) ||
      !carousel_view_source.contains(QStringLiteral("function templateLibrarySectionCount(section)")) ||
      !carousel_view_source.contains(QStringLiteral("function filteredTemplateLibraryEntries()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.templateLibraryEntries")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselTemplateLibrarySearchField\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Favorites\", value: \"favorites\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Recent\", value: \"recent\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Installed\", value: \"installed\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Custom\", value: \"custom\" }")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselTemplateLibraryCompactDensityButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselTemplateLibraryComfortableDensityButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.toggleFavoriteTemplate(")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselClearRecentTemplatesButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.clearRecentTemplates()")) ||
      !carousel_view_source.contains(QStringLiteral("sourceSize: Qt.size(160, 200)")) ||
      !carousel_view_source.contains(QStringLiteral("cache: false")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyTemplateLibraryEntry("))) {
    return fail("Carousel template library should search and filter one lightweight installed/custom feed with persistent favorites, recents, density controls, and bounded card previews");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselTemplatePackUpdateContract: \"carousel-template-pack-update:v1|provenance:id-version|availability:non-mutating|changes:component-level|preview:exact-counts|local-overrides:preserved|missing-components:reported|automatic:false|approval:explicit|apply:one-step-undo|history:persistent|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselTemplatePackUpdateBanner\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.templatePackUpdateState.updateAvailable")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselReviewTemplatePackUpdateButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewTemplatePackUpdate()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselTemplatePackUpdateDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: carouselDocument.templatePackUpdatePreview.changes || []")) ||
      !carousel_view_source.contains(QStringLiteral("modelData.preservedOverrideCount")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"No automatic migration. Apply approved update creates one undo step and records the migration in this document.\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Keep current version\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselApplyTemplatePackUpdateButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyPreviewedTemplatePackUpdate()")) ||
      !carousel_view_source.contains(QStringLiteral("onClosed: carouselDocument.clearTemplatePackUpdatePreview()"))) {
    return fail("Carousel template packs should retain document provenance, explain exact component changes, preserve local overrides, and migrate only after explicit previewed approval");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselBackgroundRemovalContract: \"carousel-background-removal:v1|targets:slot-layer|processing:visible|failure:retryable|refine:feather-expand-edge-contrast|original-fallback:reversible|preview:result-black-white-matte-edges|preview-only-inspection:true|transparent-background:true|export:png-webp-alpha|async:true|undo:true|persistence:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCutoutControls\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.startSelectedMediaBackgroundRemoval()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.useSelectedMediaOriginal()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.restoreSelectedMediaCutout()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCutoutRefineDialog\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setSelectedMediaCutoutRefinement(")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"On black\", value: \"black\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"On white\", value: \"white\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Matte\", value: \"matte\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Edges\", value: \"edges\" }")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setCurrentSlideBackgroundTransparent()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselSlotCutoutLoader\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselLayerCutoutLoader\""))) {
    return fail("Carousel background removal should expose processing and failure states, reversible original fallback, refinement, edge inspection, and transparent output for slot and layer photos");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselListiclePackContract: \"carousel-listicle-pack:v1|cover:true|numbered-variants:5|image-caption:true|comparison-pair:true|progress-numbering:true|reorder-aware:true|recap:true|final-prompt:true|editable-layers:true|ratios:4x5-1x1-9x16-5x4|undo:true|save-reopen:true|export:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: root.filteredTemplateLibraryEntries()")) ||
      !carousel_view_source.contains(QStringLiteral("model: root.sequenceTemplatePreview.previewRoles || []")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyTemplateLibraryEntry("))) {
    return fail("Carousel should expose a complete editable Listicle pack with five reorder-aware numbered variants, image-caption patterns, progress, recap, and a final prompt");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselPortfolioEditorialPackContract: \"carousel-portfolio-editorial-pack:v1|photography-dominant:true|opener:true|diptych:true|triptych:true|full-bleed:true|pull-quote:true|credits:true|contact:true|no-marketing-card:true|editable-layers:true|ratios:4x5-1x1-9x16-5x4|undo:true|save-reopen:true|export:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: root.filteredTemplateLibraryEntries()")) ||
      !carousel_view_source.contains(QStringLiteral("model: root.sequenceTemplatePreview.previewRoles || []")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyTemplateLibraryEntry("))) {
    return fail("Carousel should expose a photography-dominant Portfolio / Editorial pack with opener, diptych, triptych, full bleed, pull quote, credits, and contact boards");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselAnnouncementEventPackContract: \"carousel-announcement-event-pack:v1|save-date:true|headline:true|date-time:true|location-access:true|supporting-point:true|program:true|contributors:true|reminder:true|cta-copy:true|editable-layers:true|ratios:4x5-1x1-9x16-5x4|undo:true|save-reopen:true|export:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: root.filteredTemplateLibraryEntries()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyTemplateLibraryEntry("))) {
    return fail("Carousel should expose a complete Announcement / Event pack with exact date, location, supporting point, contributors, reminder, and CTA copy roles");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselThoughtLeadershipPackContract: \"carousel-thought-leadership-pack:v1|hook-question:true|thesis:true|supporting-point:true|matched-evidence:true|quote:true|attribution:true|counterpoint:true|implications:true|cta-copy:true|editable-layers:true|ratios:4x5-1x1-9x16-5x4|undo:true|save-reopen:true|export:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("model: root.filteredTemplateLibraryEntries()")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.applyTemplateLibraryEntry("))) {
    return fail("Carousel should expose a thesis-led Quote / Thought Leadership pack with evidence, sourced pull quote, counterpoint, implications, and CTA copy roles");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselFocalPointContract: \"carousel-focal-point:v1|per-instance:true|smart-across-ratios:true|manual-authoritative:true|slot:true|layer:true|undo:true|persistence:true|preview-export-parity:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Smart focal crop\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Manual crop\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Focal X\"")) ||
      !carousel_view_source.contains(QStringLiteral("label: \"Focal Y\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setCurrentMediaFocalPoint(value, selection.focalY)")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.setCurrentMediaCropMode(\"manual\")")) ||
      !carousel_view_source.contains(QStringLiteral("String(item.mediaCropMode || \"smart\") === \"manual\""))) {
    return fail("Carousel focal points should visibly control smart per-instance crops while preserving explicit manual authority");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselMediaDropContract: \"carousel-media-drop:v1|library:true|filesystem:true|board:true|exact-slot:true|layer-stack:true|thumbnail:true|preview:true|copy-action:true|atomic-undo:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("function mediaPayloadForDrop(drop)")) ||
      !carousel_view_source.contains(QStringLiteral("drop.getDataAsString(\"application/x-carousel-media\")")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBoardMediaDropArea\"")) ||
      !carousel_view_source.contains(QStringLiteral("root.slotNameForDrop(")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselLayerStackDropArea\"")) ||
      !carousel_view_source.contains(QStringLiteral("root.applyMediaDrop(drop, boardIndex, \"thumbnail\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.dropMediaOnTarget("))) {
    return fail("Carousel media drag-and-drop should carry library or filesystem media into visible exact slot, layer-stack, and thumbnail targets with one backend command");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselPreviewInvalidationContract: \"carousel-preview-invalidation:v2|board-aware:true|item-aware:true|selection-no-render:true|decode-ms:true|layout-ms:true|render-ms:true|publish-ms:true|revision:true|shared-cache:true|budget:512mb|stale-request-cancellation:true|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselPreviewDiagnostics\"")) ||
      !carousel_view_source.contains(QStringLiteral("title: \"Preview performance\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewDiagnostics.decodeMs")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewDiagnostics.layoutMs")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewDiagnostics.renderMs")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewDiagnostics.publishMs")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewDiagnostics.cacheBudgetBytes")) ||
      !carousel_view_source.contains(QStringLiteral("carouselDocument.previewDiagnostics.staleRequestsDropped"))) {
    return fail("Carousel preview diagnostics should expose board-aware invalidation revisions and decode, layout, render, and publish timing");
  }
  if (!carousel_view_source.contains(QStringLiteral("objectName: \"carouselWorkspace\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselVisibilityContract: \"carousel-visibility:v2|always:composer-header-artboard-layout-menu|configurable:tool-rail-board-strip-inspector|contextual:setup-recent-carousel-media-picker-selection-rating-layer-controls-empty-slot-guides|one-expansion:tool-variants-layer-add-export-actions-object-actions-board-actions|hover-focus:canvas-selection-handles-layer-row-actions-board-thumbs|diagnostic:export-busy-snap-guides-media-offline|preserve:functionality\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselSurfaceContract: \"carousel-surfaces:v1|setup:create-and-recent|composer:dominant-artboard|tools:rail-with-menus|board-strip:stable|inspector:board-or-selection|layers:object-stack|media-picker:contextual|no-elide:true|no-overlap:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselWorkflowPolishContract: \"carousel-workflow-polish:v1|first-glance:setup-or-board-canvas-tool-rail-inspector-strip|close-inspection:active-tool-board-selection-media-layer-rating-export|clutter:one-active-selection-context-media-picker-contextual|interaction:create-select-pan-text-media-shape-guide-layer-export|text:no-elide-no-overlap|functionality:preserved\"")) ||
      !carousel_view_source.contains(QStringLiteral("carouselPageCompletionContract: \"page-completion:v1|carousel|rail:select-pan-text-media-shape-guides|content:setup-or-artboard-empty-slot-board-strip|inspector:board-selection-layers-media|toolbar:save-export-board-actions|states:setup-empty-board-selected-media-exporting-error\"")) ||
      !carousel_view_source.contains(QStringLiteral("function carouselWorkflowPolishSignature()")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselSetupSurface\"")) ||
      !carousel_view_source.contains(QStringLiteral("surfaceContract: \"carousel-setup-surface:v2|create-carousel:direct|ratios:4|custom-pixels:true|board-count:1-20|color-space:true|background:true|template:true|selected-media:true|remember-last:true|recent:secondary|no-elide:true|no-overlap:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("\"customPixelWidth\": Math.max(64")) ||
      !carousel_view_source.contains(QStringLiteral("\"customPixelHeight\": Math.max(64")) ||
      !carousel_view_source.contains(QStringLiteral("\"colorSpace\": setupColorSpace")) ||
      !carousel_view_source.contains(QStringLiteral("\"backgroundColor\": String(setupBackgroundColor)")) ||
      !carousel_view_source.contains(QStringLiteral("\"templateKind\": setupTemplateKind")) ||
      !carousel_view_source.contains(QStringLiteral("\"primaryMediaPath\": setupUseSelectedMedia")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselActiveWorkspace\"")) ||
      !carousel_view_source.contains(QStringLiteral("surfaceContract: \"carousel-active-workspace:v3|tool-rail:configurable|composer:dominant|inspector:configurable-inline-standard-wide-drawer-compact|board-strip:configurable|selection-contextual:true|modes:compact-standard-wide|no-elide:true|no-overlap:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselToolRail\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselComposerPanel\"")) ||
      !carousel_view_source.contains(QStringLiteral("surfaceContract: \"carousel-composer-panel:v2|header:always|artboard:dominant|zoom:contextual|board-strip:configurable|empty-slot:contextual|drag-handles:hover-focus|no-overlap:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselHeaderBar\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselArtboardFrame\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselArtboardViewport\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselStripSurface\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBoardStrip\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselInspectorPanel\"")) ||
      !carousel_view_source.contains(QStringLiteral("surfaceContract: \"carousel-inspector:v1|board-settings:when-no-object|selection-settings:contextual|layers:always|media:one-expansion|actions:menus|no-elide:true|no-overlap:true\""))) {
    return fail("Carousel workspace should keep the composer and layout menu visible while allowing tool rail, board strip, and inspector visibility to be customized");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselResponsiveLayoutContract: \"carousel-responsive-layout:v1|modes:compact-under-1050,standard-1050-to-1719,wide-1720-plus|supported:760x600,1280x720,1920x1080|compact-inspector:right-drawer|standard-inspector:300-scaled|wide-inspector:336-scaled|composer-min:360-460-600|board-strip:112-136-152|header:condensed|empty-card:bounded|state-preserved:true|no-elide:true|no-overlap:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("function carouselResponsiveLayoutMetrics(viewWidth, viewHeight)")) ||
      !carousel_view_source.contains(QStringLiteral("const mode = viewWidth < 1050 ? \"compact\" : (viewWidth >= 1720 ? \"wide\" : \"standard\")")) ||
      !carousel_view_source.contains(QStringLiteral("Layout.minimumWidth: root.responsiveComposerMinimumWidth")) ||
      !carousel_view_source.contains(QStringLiteral("Layout.preferredHeight: visible ? root.responsiveBoardStripHeight : 0")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselEmptyBoardStartCard\"")) ||
      !carousel_view_source.contains(QStringLiteral("columns: emptyBoardStartCard.responsiveCompactCard ? 2 : 3")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselInspectorInlineHost\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCompactInspectorButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCompactInspectorDrawer\"")) ||
      !carousel_view_source.contains(QStringLiteral("parent: root.compactWorkspace ? carouselCompactInspectorHost : carouselInspectorInlineHost")) ||
      !carousel_view_source.contains(QStringLiteral("visible: !root.compactWorkspace")) ||
      !carousel_view_source.contains(QStringLiteral("showText: !root.compactWorkspace"))) {
    return fail("Carousel should implement compact, standard, and wide structures with a bounded empty state, responsive composer and board strip, and one state-preserving inspector instance that moves into a compact drawer");
  }
  if (!carousel_view_source.contains(QStringLiteral("emptyStateContract: \"carousel-empty-board:v3|chrome:compact-command-surface|commands:add-media-add-text-add-shape-paste-apply-template|narrow:primary-media-plus-menu|instruction-copy:none|direct-or-one-expansion:true|keyboard:true|no-elide:true|no-overlap:true\"")) ||
      carousel_view_source.contains(QStringLiteral("text: \"Build your first board\"")) ||
      carousel_view_source.contains(QStringLiteral("text: \"Start with media, or choose a layout before you place it.\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Add media\", icon: \"media\", action: \"media\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Add text\", icon: \"edit\", action: \"text\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Add shape\", icon: \"geometry\", action: \"shape\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Paste\", icon: \"paste\", action: \"paste\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Apply template\", icon: \"grid\", action: \"template\" }")) ||
      !carousel_view_source.contains(QStringLiteral("root.runCarouselCommand(\"add.paste\")")) ||
      !carousel_view_source.contains(QStringLiteral("root.openCarouselTemplateLibrary()"))) {
    return fail("Carousel empty boards should replace instruction-heavy chrome with direct Media, Text, Shape, Paste, and Template commands");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselControlVocabularyContract: \"carousel-control-vocabulary:v1|gradients:horizontal-vertical|distribution:horizontal-vertical|delete:board-layer|increments:board-count-export-version|add-board:explicit|icons:labeled|abbreviations:none|duplicate-plus:none|no-elide:true\"")) ||
      carousel_view_source.contains(QStringLiteral("\"H Grad\"")) ||
      carousel_view_source.contains(QStringLiteral("\"V Grad\"")) ||
      carousel_view_source.contains(QStringLiteral("text: \"Del\"")) ||
      carousel_view_source.contains(QStringLiteral("text: \"Dist H\"")) ||
      carousel_view_source.contains(QStringLiteral("text: \"Dist V\"")) ||
      carousel_view_source.contains(QStringLiteral("text: \"+\"")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Horizontal gradient\", value: \"linear-horizontal\" }")) ||
      !carousel_view_source.contains(QStringLiteral("{ label: \"Vertical gradient\", value: \"linear-vertical\" }")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Add board\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Delete layer\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Distribute horizontally\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Distribute vertically\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Increase board count\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Increase export version\"")) ||
      !app_icon_source.contains(QStringLiteral("case \"distribute-horizontal\":")) ||
      !app_icon_source.contains(QStringLiteral("case \"distribute-vertical\":"))) {
    return fail("Carousel should use full words or distinctly labeled icons instead of ambiguous abbreviations and duplicate plus controls");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselInspectorSplitContract: \"carousel-inspector-split:v1|surfaces:board-properties-reusable-designs|board:authoring-selection-layers-media-layout-preflight|designs:saved-boards-components-templates-brand-kits|selection-preserved:true|board-preserved:true|single-visible-surface:true|no-elide:true\"")) ||
      carousel_view_source.contains(QStringLiteral("Boards & Saved Designs")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselInspectorSurfaceSwitcher\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselBoardPropertiesSurface\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselReusableDesignLibrarySurface\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselReusableDesignTemplatesSection\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselReusableDesignComponentsSection\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselReusableDesignBrandKitsSection\"")) ||
      !carousel_view_source.contains(QStringLiteral("onClicked: root.inspectorPrimarySurface = \"board\"")) ||
      !carousel_view_source.contains(QStringLiteral("onClicked: root.inspectorPrimarySurface = \"designs\"")) ||
      !carousel_view_source.contains(QStringLiteral("text: \"Browse reusable designs\""))) {
    return fail("Carousel should separate Board Properties from Reusable Designs while preserving the active document and selection context");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselInteractionConsistencyContract: \"carousel-interaction-consistency:v1|shared:theme-interaction-states|surfaces:header-actions-chips-template-cards-swatches-layer-rows-board-thumbnails|states:hover-pressed-focus-disabled-destructive-selected|non-color-cues:true|duration:theme-fast|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("readonly property string interactionStateContract: theme.interactionStateContract")) ||
      !carousel_view_source.contains(QStringLiteral("id: templateCardTap")) ||
      !carousel_view_source.contains(QStringLiteral("id: templateCardHover")) ||
      !carousel_view_source.contains(QStringLiteral("border.color: activeFocus")) ||
      !action_button_source.contains(QStringLiteral("readonly property string interactionStateContract: theme.interactionStateContract")) ||
      !color_swatch_button_source.contains(QStringLiteral("readonly property string interactionStateContract: theme.interactionStateContract")) ||
      !color_swatch_button_source.contains(QStringLiteral("swatchTap.pressed")) ||
      !carousel_layer_row_source.contains(QStringLiteral("readonly property string interactionStateContract: theme.interactionStateContract")) ||
      !carousel_layer_row_source.contains(QStringLiteral("rowTap.pressed")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("readonly property string interactionStateContract: theme.interactionStateContract")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("color: reorderDrag.active"))) {
    return fail("Carousel header actions, chips, template cards, swatches, layer rows, and board thumbnails should share hover, pressed, focus, disabled, destructive, and selected semantics");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselAccessibilityContract: \"carousel-accessibility:v1|names:true|roles:true|values-in-descriptions:true|selected-state:true|state-bindings-live:true|focus-restoration:drawer-command-reading-order:true|swatches:true|thumbnails:true|slots:true|canvas-objects:true|drag-alternatives:keyboard-nudge-reorder:true|no-mouse-required:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselMoveBoardEarlierButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("accessibleName: \"Move board earlier\"")) ||
      !carousel_view_source.contains(QStringLiteral("tooltipText: \"Move board earlier\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselMoveBoardLaterButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("accessibleName: \"Move board later\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselAlignSelectionLeftButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("accessibleName: \"Align selection left\"")) ||
      !carousel_view_source.contains(QStringLiteral("tooltipText: \"Align selection left\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselAlignSelectionHorizontalCenterButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("accessibleName: \"Align selection horizontally centered\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselAlignSelectionRightButton\"")) ||
      !carousel_view_source.contains(QStringLiteral("accessibleName: \"Align selection right\"")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCanvasSlot\" +")) ||
      !carousel_view_source.contains(QStringLiteral("Accessible.name: \"Media slot \"")) ||
      !carousel_view_source.contains(QStringLiteral("Accessible.checked: slotRegion.isActive")) ||
      !carousel_view_source.contains(QStringLiteral("objectName: \"carouselCanvasObject\" + String(modelData.itemId || \"\")")) ||
      !carousel_view_source.contains(QStringLiteral("Accessible.checked: layerItem.selected")) ||
      !carousel_view_source.contains(QStringLiteral("Arrow keys move; Shift plus arrows uses the large nudge.")) ||
      !carousel_view_source.contains(QStringLiteral("carouselCompactInspectorButton.forceActiveFocus()")) ||
      !carousel_view_source.contains(QStringLiteral("if (restoreFocus && restoreItem)")) ||
      !color_swatch_button_source.contains(QStringLiteral("Accessible.checked: selected")) ||
      !carousel_layer_row_source.contains(QStringLiteral("Accessible.checked: selected")) ||
      !carousel_layer_row_source.contains(QStringLiteral("Alt Shift Up or Down reorders this layer without dragging.")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("Accessible.checked: selected")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("Control Shift Left or Right reorders this board without dragging."))) {
    return fail("Carousel custom controls should expose names, roles, values, live selected state, focus restoration, and keyboard alternatives to dragging");
  }
  if (!carousel_view_source.contains(QStringLiteral("carouselLocaleStressContract: \"carousel-locale-stress:v1|ui-mirroring:rtl-aware|canvas-geometry:logical-unmirrored|text:bidi-auto|board-sequence:logical-number-preserved|templates:mixed-script-readable|export-filenames:unicode-preserved|no-elide:true\"")) ||
      !carousel_view_source.contains(QStringLiteral("LayoutMirroring.enabled: uiRightToLeft")) ||
      !carousel_view_source.contains(QStringLiteral("LayoutMirroring.childrenInherit: true")) ||
      !carousel_view_source.contains(QStringLiteral("function carouselLocaleStressProbe()")) ||
      !carousel_view_source.contains(QStringLiteral("id: stripSurface\n                            LayoutMirroring.enabled: false")) ||
      !carousel_view_source.contains(QStringLiteral("layoutDirection: root.uiRightToLeft ? Qt.RightToLeft : Qt.LeftToRight")) ||
      !carousel_board_thumb_source.contains(QStringLiteral("objectName: \"carouselBoardThumbnail\" + String(index + 1)")) ||
      !carousel_view_source.contains(QStringLiteral("elide: Text.ElideNone"))) {
    return fail("Carousel should mirror RTL UI chrome, preserve logical canvas and board order, retain bidi text and Unicode filenames, and keep all labels under the no-ellipsis rule");
  }
  if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_COLOR_ROUTING") &&
      !qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_FIRST_INTERACTION_RESPONSIVENESS") &&
      !qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_IMPORT_SAFETY") &&
      !qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_OFFLINE_RECOVERY") &&
      !qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_CRASH_RECOVERY_UX") &&
      !qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_ACCESSIBILITY") &&
      !qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_AI_EDIT_UX") &&
      !qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_DELIVERY_EXPORT") &&
      !qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER") &&
      (!timeline_view_source.contains(QStringLiteral("objectName: \"timelineWorkspace\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineVisibilityContract: \"timeline-visibility:v1|always:header-program-monitor-timeline|contextual:source-panel-effects-panel-selected-clip-markers|one-expansion:tools-media-picker-effects-transitions-render-jobs|hover-focus:clip-actions-marker-actions-track-resize|diagnostic:render-errors-job-detail|preserve:functionality\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineSurfaceContract: \"timeline-surfaces:v1|program-monitor:dominant|transport:compact|source-panel:collapsible|effects-panel:collapsible|tracks:stable-rows|clips:readable|markers:contextual-strip|waveform:track-contained|minimap:secondary|no-elide:true|no-overlap:true\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineUiConsistencyContract: \"timeline-ui-consistency:v1|layout:source-program-inspector-multitrack|theme:shared|type:shared|spacing:shared|controls:shared|popups:shared-lifecycle|states:accessible|video-editor-identity:preserved\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineIntrinsicGeometryContract: \"timeline-intrinsic-geometry:v1|allowed:time-to-pixel-lane-height-ruler-cadence-playhead-trim-handles-waveform-safe-area-media-black|chrome-values:theme-only|documented-exceptions:true\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineProgressiveDisclosureContract: \"timeline-progressive-disclosure:v1|default:calm|always:media-undo-save-playback-tool|contextual:inspector-split-review-status|menus:create-view-tools-zoom|panels:source-inspector-collapsible-detachable|advanced:opt-in|commands:preserved\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineDetachedSourceWindow\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineDetachedInspectorWindow\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineHeaderBar\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineWorkspaceVerticalSplit\"")) ||
      !timeline_view_source.contains(QStringLiteral("layoutContract: \"timeline-shell:v2|header:single|monitor-timeline:resizable|side-panels:usable-collapsible|search-review:contextual|tools:grouped|empty-state:guided\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineMainSplit\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineSourceRail\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineProgramPanel\"")) ||
      !timeline_view_source.contains(QStringLiteral("surfaceContract: \"timeline-program-panel:v3|surface:shared|media-well:semantic|monitor-dominant:true|transport:shared-compact|timecode:mono|empty-guidance:recovery|error:semantic-nonblocking|focus:actual-only|no-overlap:true\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineProgramMonitor\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineProgramGapState\"")) ||
      !timeline_view_source.contains(QStringLiteral("text: \"GAP  ·  \" + timecodeText(sequencePlayheadSeconds)")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineTransportBar\"")) ||
      !timeline_view_source.contains(QStringLiteral("transportContract: \"timeline-transport:v1|playback:first|edit-actions:compact|timecode:stable|slider:stable|disabled-reasons:true|no-elide:true|no-overlap:true\"")) ||
      !timeline_view_source.contains(QStringLiteral("function pickedMediaDisabledReason()")) ||
      !timeline_view_source.contains(QStringLiteral("function playableClipDisabledReason()")) ||
      !timeline_view_source.contains(QStringLiteral("function sourceClipDisabledReason()")) ||
      !timeline_view_source.contains(QStringLiteral("function moveClipEarlierDisabledReason()")) ||
      !timeline_view_source.contains(QStringLiteral("function moveClipLaterDisabledReason()")) ||
      !timeline_view_source.contains(QStringLiteral("disabledReason: root.pickedMediaDisabledReason()")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelinePlayPauseButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("function hasSplitSelection()")) ||
      !timeline_view_source.contains(QStringLiteral("disabledReason: root.selectedClipDisabledReason()")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineInsertSourceButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("disabledReason: root.sourceClipDisabledReason()")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineEffectsRail\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineSurfacePanel\"")) ||
      !timeline_view_source.contains(QStringLiteral("surfaceContract: \"timeline-surface:v3|surface:shared|toolbar:shared-compact|search-filter:contextual|tools:one-expansion|tracks:stable|clips:readable|markers:contextual|minimap:secondary|waveform:track-contained|focus:selection-expands-paired-tracks|unrelated-tracks:temporary-compact|restore:base-heights|no-overlap:true\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineSearchBar\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineSearchTruthSignature: \"timeline-search:v2|query:clip-name-marker-note-timecode-media-effect-transition-online-state|filters:clips-markers-video-audio-photo-title-caption-offline-effects-marker-status|scope:all-or-selected-track|results:controller-sorted|navigation:select-clip-seek-marker-prev-next-wrap|ui:contextual-single-row-shared-menu|performance:id-map\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineSearchField\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineSearchFilterMenuButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.timelineSearchResults(")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.activateTimelineSearchResult(")) ||
      !timeline_view_source.contains(QStringLiteral("key: \"kind-offline\"")) ||
      !timeline_view_source.contains(QStringLiteral("key: \"kind-effects\"")) ||
      !timeline_view_source.contains(QStringLiteral("key: \"scope-selected\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineMarkerFilterBar\"")) ||
      !timeline_view_source.contains(QStringLiteral("visibilityRole: \"contextual|marker-status-filter\"")) ||
      !timeline_view_source.contains(QStringLiteral("visible: sequenceMarkerModel.length > 0 && root.markerReviewExpanded")) ||
      !timeline_view_source.contains(QStringLiteral("visible: root.timelineSearchExpanded || root.timelineSearchActive")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineToolBar\"")) ||
      !timeline_view_source.contains(QStringLiteral("visibilityRole: \"progressive|always:select-edit-mode-blade-trim-create-search-link-snap-zoom|contextual:ripple-split\"")) ||
      !timeline_view_source.contains(QStringLiteral("function activeTimelineToolLabel()")) ||
      !timeline_view_source.contains(QStringLiteral("\"Start your first cut\"")) ||
      !timeline_view_source.contains(QStringLiteral("\"Import or drag a clip to a video track to begin.\"")) ||
      !timeline_view_source.contains(QStringLiteral("{\"name\": \"Exposure\", \"category\": \"Color\"")) ||
      !timeline_view_source.contains(QStringLiteral("{\"name\": \"Blur\", \"category\": \"Utility\"")) ||
      !timeline_view_source.contains(QStringLiteral("{\"name\": \"Punch\", \"category\": \"Style\"")) ||
      !timeline_view_source.contains(QStringLiteral("readonly property var transitionLibraryModel: [")) ||
      !timeline_view_source.contains(QStringLiteral("\"name\": \"Cross Dissolve\"")) ||
      !timeline_view_source.contains(QStringLiteral("transitionTruthSignature: \"timeline-transition-truth:v1|enabled:cross-dissolve|timing:controller-owned|preview:dual-source-linear-alpha|export:dual-source-linear-alpha|handles:incoming-preroll|pairing:adjacent-same-track|undo-save-reopen:true|proof:start-mid-end-rendered-frames\"")) ||
      !timeline_view_source.contains(QStringLiteral("transformTruthSignature: \"timeline-transform-truth:v1|frame-modes:fit-fill-stretch|order:frame-scale-rotate-translate-clip-effects-opacity|position:positive-right-down-quarter-frame|rotation:center-transparent|preview:program-layer-shared|export:ffmpeg-shared|blend:unsupported-hidden|proof:fit-fill-position-rotation-opacity-pixels\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.transitionPreviewStatesAt(sequencePlayheadSeconds)")) ||
      !timeline_view_source.contains(QStringLiteral("function programTransitionStateForClip(layer)")) ||
      !timeline_view_source.contains(QStringLiteral("return baseOpacity * Number(transition.incomingAlpha || 0)")) ||
      !timeline_view_source.contains(QStringLiteral("opacity: root.programLayerOpacity(programClip)")) ||
      !timeline_view_source.contains(QStringLiteral("timeline-keyframe-motion:v2|parameters:transform-opacity-gain-effects-shared-grade-controls")) ||
      !timeline_view_source.contains(QStringLiteral("timeline-keyframe-bezier:v2|handles:weighted-time-value-in-out|modes:linked-broken|evaluation:cubic-time-solved")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineBezierInHandle\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineBezierOutHandle\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineBezierHandleLinkButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("function interpolatedKeyframeValue(left, right, progress)")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.setSelectedClipKeyframeBezierHandle(")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.setSelectedClipKeyframeBezierHandlesLinked(")) ||
      !timeline_view_source.contains(QStringLiteral("timeline-keyframe-lanes:v3|clip:inline-transform-opacity-gain-effects-shared-grade-controls|track:inline-opacity-gain-pan")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineTrackAutomationLane\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.updateTrackAutomationKeyframe(")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.removeTrackAutomationKeyframe(")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.setTrackAutomationKeyframeInterpolation(")) ||
      !timeline_view_source.contains(QStringLiteral("property var programBackgroundLayerModel: []")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineProgramBackgroundLayer\"")) ||
      !timeline_view_source.contains(QStringLiteral("function syncLayerPlayer()")) ||
      !timeline_view_source.contains(QStringLiteral("videoOutput: backgroundVideo")) ||
      !timeline_view_source.contains(QStringLiteral("timeline-magnetic-snap:v1|resolver:controller-single-source|targets:playhead-video-audio-transitions-markers-frames-optional-waveform-peaks")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.resolveTimelineSnap(")) ||
      !timeline_view_source.contains(QStringLiteral("onClicked: timelineDocument.undo()")) ||
      !timeline_view_source.contains(QStringLiteral("onClicked: timelineDocument.redo()")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("timelineEffectsPanelStyleContract: \"timeline-effects-panel-style:v9|")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("setSelectedClipEffectAmount(effectName, value)")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("setSelectedClipEffectEnabled(")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("clearSelectedClipEffects()")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("onInteractionStarted: root.beginInspectorEdit()")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("onInteractionFinished: root.endInspectorEdit()")) ||
      !timeline_view_source.contains(QStringLiteral("property var trimFeedbackState")) ||
      !timeline_view_source.contains(QStringLiteral("property bool markerFeedbackVisible")) ||
      !timeline_view_source.contains(QStringLiteral("markerFeedbackSignature: \"timeline-marker-feedback:v1|visible:\"")) ||
      !timeline_view_source.contains(QStringLiteral("trimFeedbackSignature: \"timeline-trim-feedback:v1|visible:\"")) ||
      !timeline_view_source.contains(QStringLiteral("function updateTrimFeedback(mode, edge, baselineSeconds, nextSeconds)")) ||
      !timeline_view_source.contains(QStringLiteral("key: \"tool-slide\"")) ||
      !timeline_view_source.contains(QStringLiteral("sequence: \"U\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.slideSelectedClipBySeconds(slideDeltaSeconds)")) ||
      !app_icon_source.contains(QStringLiteral("case \"slide\":")) ||
      !timeline_view_source.contains(QStringLiteral("function addMarkerAtPlayhead()")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineMarkerFeedbackChip\"")) ||
      !timeline_view_source.contains(QStringLiteral("root.updateTrimFeedback(\"slip\", \"body\", 0, deltaSeconds)")) ||
      !timeline_view_source.contains(QStringLiteral("updateTrimFeedback(gesture.mode, gesture.edge, gesture.baselineSeconds, boundary)")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineTrimFeedbackOverlay\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineTrimFeedbackLabel\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineMarkerStrip\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineMinimapBar\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineTrackHeaderColumn\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineViewport\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineWorkflowPolishContract: \"timeline-workflow-polish:v1|first-glance:source-program-timeline-effects|close-inspection:active-tool-edit-mode-playhead-selection-markers-effects-render|clutter:panels-collapsible-details-contextual|interaction:load-source-insert-overwrite-play-trim-split-marker-effect-render|text:no-elide-no-overlap|functionality:preserved\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelinePageCompletionContract: \"page-completion:v1|timeline|sidebar:source-media-picker|content:program-monitor-transport-timeline-tracks-waveform|inspector:effects-transitions-selected-clip|toolbar:tools-search-marker-filter-zoom|states:setup-empty-source-selected-dragging-rendering-error\"")) ||
       !timeline_view_source.contains(QStringLiteral("function timelineWorkflowPolishSignature()")))) {
    return fail("Timeline workspace should declare visible, contextual, expansion, hover, diagnostic, and preserved-function surfaces across monitor, source, effects, tracks, markers, waveform, and timeline chrome");
  }
  if (!timeline_marker_card_source.contains(QStringLiteral("timelineMarkerCardContract: \"timeline-marker-card:v1|title-wrap:true|note-wrap:true|status-chip:true|jump-edit-done:true|keyboard:true|readable-text:true\"")) ||
      !timeline_marker_card_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|title-wrap:true|note-wrap:true|timecode-fit:true|tooltip-full:true\"")) ||
      !timeline_marker_card_source.contains(QStringLiteral("StatusChip {")) ||
      !timeline_marker_card_source.contains(QStringLiteral("signal jumpRequested(string markerId)")) ||
      !timeline_marker_card_source.contains(QStringLiteral("signal editRequested(string markerId)")) ||
      !timeline_marker_card_source.contains(QStringLiteral("signal completionToggled(string markerId, bool completed)")) ||
      !timeline_view_source.contains(QStringLiteral("delegate: TimelineMarkerCard")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineMarkerCard\"")) ||
      !timeline_view_source.contains(QStringLiteral("function markerStatusTone(marker)")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.seekSequenceMarker(markerId)")) ||
      !timeline_view_source.contains(QStringLiteral("root.openMarkerEditor(modelData)")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.setSequenceMarkerCompleted(markerId, completed)"))) {
    return fail("Timeline marker strip should use TimelineMarkerCard with shared status chip, readable text, keyboard, jump, edit, and done behavior");
  }
  if (!timeline_marker_pin_source.contains(QStringLiteral("timelineMarkerPinContract: \"timeline-marker-pin:v2|line:true|label-fit:true|jump-edit:true|drag-preview-commit:true|keyboard:true|tooltip-full:true|readable-text:true|no-elide:true\"")) ||
      !timeline_marker_pin_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !timeline_marker_pin_source.contains(QStringLiteral("signal jumpRequested(string markerId)")) ||
      !timeline_marker_pin_source.contains(QStringLiteral("signal editRequested(string markerId)")) ||
      !timeline_marker_pin_source.contains(QStringLiteral("signal movePreviewRequested(string markerId, real proposedSeconds)")) ||
      !timeline_marker_pin_source.contains(QStringLiteral("signal moveCommittedRequested(string markerId, real proposedSeconds)")) ||
      !timeline_marker_pin_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      !timeline_marker_pin_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !timeline_marker_pin_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !timeline_view_source.contains(QStringLiteral("delegate: TimelineMarkerPin")) ||
      !timeline_view_source.contains(QStringLiteral("markerId: String(modelData.markerId || \"\")")) ||
      !timeline_view_source.contains(QStringLiteral("label: String(modelData.label || \"Marker\")")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.seekSequenceMarker(markerId)")) ||
      !timeline_view_source.contains(QStringLiteral("root.openMarkerEditor(modelData)"))) {
    return fail("Timeline canvas marker pins should use TimelineMarkerPin with readable labels, keyboard jump, and preserved double-edit behavior");
  }
  if (!timeline_clip_content_source.contains(QStringLiteral("timelineClipContentContract: \"timeline-clip-content:v2|title-fit:true|metadata-fit:true|status-strip:compact-consistent|readable-text:true|no-elide:true\"")) ||
      !timeline_clip_content_source.contains(QStringLiteral("readableTextContract: \"readable-text:v2|ellipsis:none|title-fit:true|metadata-fit:true|status-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !timeline_clip_content_source.contains(QStringLiteral("property real horizontalInset: 0")) ||
      !timeline_clip_content_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !timeline_clip_content_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !timeline_clip_content_source.contains(QStringLiteral("model: root.statusBadges ? root.statusBadges : []")) ||
      !timeline_clip_content_source.contains(QStringLiteral("Accessible.description: metadata")) ||
      !timeline_view_source.contains(QStringLiteral("function clipMetadataText(clip)")) ||
      !timeline_view_source.contains(QStringLiteral("TimelineClipContent {")) ||
      !timeline_view_source.contains(QStringLiteral("title: String(modelData.displayLabel || \"Clip\")")) ||
      !timeline_view_source.contains(QStringLiteral("metadata: root.clipMetadataText(modelData)")) ||
      !timeline_view_source.contains(QStringLiteral("statusBadges: root.clipStatusBadges(")) ||
      !timeline_view_source.contains(QStringLiteral("horizontalInset: clipBlock.trimHandleWidth"))) {
    return fail("Timeline clip bodies should use TimelineClipContent for readable no-ellipsis labels, metadata, and effect chips without moving clip behavior");
  }
  if (!QRegularExpression(QStringLiteral("AppMenu\\s*\\{\\s*id:\\s*timelineCanvasMenu\\b")).match(timeline_view_source).hasMatch() ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineCanvasMenu\"")) ||
      !timeline_view_source.contains(QStringLiteral("onTriggered: root.addMarkerAtPlayhead()")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.insertSourceClipAtPlayhead()")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.overwriteSourceClipAtPlayhead()")) ||
      !QRegularExpression(QStringLiteral("AppMenu\\s*\\{\\s*id:\\s*clipMenu\\b")).match(timeline_view_source).hasMatch() ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineClipMenu\"")) ||
      !timeline_view_source.contains(QStringLiteral("onTriggered: splitAtPlayhead()")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.moveSelectedBackward()")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.moveSelectedForward()")) ||
      !timeline_view_source.contains(QStringLiteral("onTriggered: root.deleteSelectedTimelineClip()")) ||
      QRegularExpression(QStringLiteral("\\n\\s*Menu\\s*\\{")).match(timeline_view_source).hasMatch()) {
    return fail("Timeline canvas and clip context menus should use shared AppMenu/AppMenuItem surfaces while preserving marker, source insert/overwrite, split, move, and lift/extract actions");
  }
  if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER") &&
      (!timeline_source_panel_source.contains(QStringLiteral("timelineSourcePanelStyleContract: \"timeline-source-panel-style:v2|surface:shared|tabs:filter-chip|media-well:semantic|type:shared|spacing:shared|transport:shared|states:empty-disabled-offline|layout:video-editor-preserved|pool:search-filter-browse-selection-actions\"")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("timelineEffectsPanelStyleContract: \"timeline-effects-panel-style:v9|surface:borderless|tabs:primary-three-more-menu|effects:seven-real-search-favorites-recents-categories-compatibility-live-thumbnails-versioned-presets-stack-amount-enable-bypass-reset-remove-clear-per-effect-spatial-mask-feather-invert|motion:collapsed-stabilization-analysis|color:per-clip-camera-input-before-grade-stream-tags-range-matrix|controls:shared|type:shared|spacing:shared|states:no-selection-focused-empty-disabled-modified|layout:video-inspector-preserved\"")) ||
      !timeline_track_header_source.contains(QStringLiteral("timelineTrackHeaderStyleContract: \"timeline-track-header-style:v3|surface:quiet-editorial|padding:compact|type:shared|targets:32|states:patched-locked-muted-solo-hidden|patch:independent-exclusive-stream-destination|target:accent-rail|edges:structural-only|resize:intrinsic\"")) ||
      !timeline_view_source.contains(QStringLiteral("sourcePatchTruthSignature: \"timeline-source-patching:v1|streams:video-audio-independent|destinations:exclusive-per-kind|disable:either-or-both|insert-overwrite-append:deterministic|linked-audio:cross-lane|fallback:none|undo-save-reopen:true\"")) ||
      !timeline_view_source.contains(QStringLiteral("shuttleTransportTruthSignature: \"timeline-shuttle-transport:v2|keys:j-k-l-shift-i-shift-o-f|directions:reverse-stop-forward|ladder:1-2-4-8|clock:sequence-authoritative|boundaries:auto-stop|frames:paused-step|navigation:edits-markers-source-in-out-match-frame|feedback:contextual-rate\"")) ||
      !timeline_view_source.contains(QStringLiteral("magneticSnapTruthSignature: \"timeline-magnetic-snap:v1|resolver:controller-single-source|targets:playhead-video-audio-transitions-markers-frames-optional-waveform-peaks|gestures:move-trim-roll-marker|threshold:pixels-zoom-stable|feedback:typed-guide-timecode|marker-drag:preview-commit-undo|performance:waveform-peaks-cached\"")) ||
      !timeline_view_source.contains(QStringLiteral("clipClipboardTruthSignature: \"timeline-clip-clipboard:v2|copy-cut-paste:overwrite-insert|selection:multi-track-relative-spacing|routing:targeted-video-audio|linked-av:preserved|duplicate:multi-selection|replace:source-timing-finishing-preserved|fit-to-fill:marked-source-retimed-target-duration|match-frame:exact|undo:atomic|feedback:edit-menu-status\"")) ||
      !timeline_view_source.contains(QStringLiteral("compoundTruthSignature: \"timeline-compound:v3|child:editable-document|nesting:arbitrary-depth-cycle-safe|navigation:breadcrumb-return|guard:self-ancestor-and-32-level|undo:isolated-restored|duration:source-updated-parent-edit-preserved|render:dependency-queued-bounded|failure:retryable|persistence:save-reopen|ui:contextual\"")) ||
      !timeline_view_source.contains(QStringLiteral("multicamTruthSignature: \"timeline-multicam:v2|groups:source-relationship-preserved|sync:embedded-timecode-waveform-correlation-source-marker|monitor:all-angles-controller-exact-muted-decode|cuts:frame-snapped-switch-nudge-remove|program:active-angle-shared|audio:follow-video-fixed-angle-mute|export:shared-video-and-audio-segments|keyboard:angles-1-through-9|transport:play-pause-step-timecode|persistence:undo-save-reopen|performance:sources-released-on-close|ui:single-modular-popup-default-closed\"")) ||
      !timeline_view_source.contains(QStringLiteral("sequenceVersionsTruthSignature: \"timeline-sequence-versions:v2|named:immutable-content-mutable-metadata|metadata:notes-author-created-updated|milestones:protected-delete-guard-explicit-unprotect|duplicate:exact-snapshot-new-identity|compare:added-removed-changed-clips-markers-format-finishing|restore:atomic-undoable|recovery:pre-restore-and-autosave|storage:timeline-scoped-atomic|ui:single-contextual-popup-progressive-details\"")) ||
      !timeline_view_source.contains(QStringLiteral("captionDeliveryTruthSignature: \"timeline-captions:v2|track:dedicated-cc-no-empty-audio|authoring:clip-and-word-text-timing|preview:title-safe-active-word|delivery:burn-in-srt-vtt|persistence:undo-save-reopen|ui:contextual\"")) ||
      !timeline_view_source.contains(QStringLiteral("textBasedEditingTruthSignature: \"timeline-text-edit:v3|plan:controller-owned-document-revision|stage:delete-rearrange-earlier-later|review:nonmutating-final-transcript-duration-operations|restore:per-operation-or-clear-all|stale:hash-invalidated|commit:all-or-nothing-one-undo|sync:video-audio-captions-markers-automation-review-ranges-multicam|persistence:save-reopen|export:ordinary-delivery-order|ui:existing-transcript-window-calm-until-staged\"")) ||
      !timeline_view_source.contains(QStringLiteral("cleanupReviewTruthSignature: \"timeline-cleanup-review:v2|detect:conservative-fillers-timed-speech-gaps-waveform-silence|audio:ffmpeg-serial-bounded-cancellable|mapping:source-trim-rate-sequence|review:non-destructive-toggle-preview|apply:confirmed-multi-range-one-undo|handles:frame-editable-natural-pause|sync:video-audio-captions-markers-automation|persistence:save-reopen|ui:transcript-popup-mode\"")) ||
      !timeline_view_source.contains(QStringLiteral("outputVariantsTruthSignature: \"timeline-output-variants:v1|formats:16x9-9x16-1x1-4x5|switch:single-context-menu|overrides:clip-framing-transform-keyframes-title-layout|inherit:first-visit|reset:selected-active-format|viewer-export:active-format|undo-save-reopen:true|density:advanced-only\"")) ||
      !timeline_view_source.contains(QStringLiteral("autoReframeTruthSignature: \"timeline-auto-reframe:v1|analysis:async-bounded-frame-samples|subject:vision-mask-contrast-fallback|composition:headroom-center|motion:smoothed|output:full-source-fit-cover-scale-x-y-keyframes|editing:curve-editor|undo-save-reopen:true|ui:contextual-popup\"")) ||
      !timeline_view_source.contains(QStringLiteral("reviewNotesTruthSignature: \"timeline-review-notes:v1|time:frame-quantized|assignment:editable|approval:open-changes-requested-approved|resolution:independent|exchange:versioned-json-frame-rate-guard|merge:exchange-id-one-undo|markers:snap-jump-shared|persistence:undo-save-reopen|ui:single-modular-popup\"")) ||
      !timeline_view_source.contains(QStringLiteral("colorFinishingTruthSignature: \"timeline-color-finishing:v3|lut:cube-validated-tetrahedral|shot-match:reference-frame-rgb-gains|scopes:post-grade-luma-waveform-rgb-histogram-vectorscope|preview:shared-photo-engine-pointwise-cube-or-full-frame-graph-rec709|preview-format:8bit|export:shared-engine-cube-pointwise-only|spatial-grade-export:open|cache:ffmpeg-final-filter-chain|persistence:undo-save-reopen|ui:single-modular-popup\"")) ||
      !timeline_view_source.contains(QStringLiteral("previewExportParityTruthSignature: \"timeline-preview-export-parity:v1|program:final-pixel-render-cache|export:ordinary-master|sample:frame-accurate-center-pixel|tolerance:codec-four-levels|coverage:effects-shot-match-lut|automated:true\"")) ||
      !timeline_view_source.contains(QStringLiteral("beatMarkerTruthSignature: \"timeline-beat-markers:v1|analysis:ffmpeg-pcm-onset-serial-cancellable|sources:unique-online-audio|mapping:source-trim-rate-sequence|review:suggestions-inert|commit:confirmed-one-undo|markers:editable-persistent-snap-ready|dedupe:frame-aware|ui:create-menu-modular-popup\"")) ||
      !timeline_view_source.contains(QStringLiteral("titleTemplateTruthSignature: \"timeline-title-templates:v1|builtins:basic-headline-lower-third-callout-quote-brand-card|create:single-modular-picker|apply:inspector-preserve-text-one-undo|customize:shared-title-controls|render:preview-export-shared|variants:position-size-compatible|persistence:save-reopen|density:add-menu-contextual\"")) ||
      !timeline_view_source.contains(QStringLiteral("deliveryPreflightTruthSignature: \"timeline-delivery-preflight:v3|platforms:youtube-shorts-tiktok-instagram|checks:offline-media-fonts-effects-caption-validation-loudness-clipping-gamut-flash-risk-frame-rate-aspect-ratio-free-disk|flash-risk:edit-graph-screen-with-frame-analysis-disclosure|loudness:postmix-lufs-truepeak-lra-cancellable|normalize:static-gain-peak-limited-one-undo|severity:blocking-warning|fix:recommended-output-variant|ui:publish-flow-modular-popup\"")) ||
      !timeline_view_source.contains(QStringLiteral("publishFlowTruthSignature: \"timeline-publish-flow:v1|primary:one-clear-publish-action|summary:format-size-duration-range-destination-always-visible|advanced:progressive|preflight:blocking-before-render|save:available-in-split-menu|render:existing-background-queue|ui:single-modular-popup\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelinePublishPopup\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelinePublishAdvancedToggle\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelinePublishConfirmButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.timelinePublishSummary()")) ||
      !timeline_view_source.contains(QStringLiteral("advancedRetimeTruthSignature: \"timeline-advanced-retime:v2|model:timeline-local-to-source-time-curve|presets:constant-ramp-up-ramp-down-reverse-freeze|holds:duration-authored-at-playhead|curve:linear-ease-hold-bezier-linked-broken|preview:bezier-source-seek-effective-rate|frames:sampling-blend-optical-flow|audio:preserve-follow-speed-mute-reverse-hold-silent|limits:source-in-out-handle-overshoot-peak-rate|cache:existing-final-preview-render-range|export:bounded-sampled-curve-shared-options|duration:ripple-downstream|persistence:undo-save-reopen\"")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("root.documentController.applySelectedClipRetimePreset(key)")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.addSelectedClipRetimePoint(")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineSequenceVersionsPopup\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.createSequenceVersionWithMetadata(")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.restoreSequenceVersion(String(modelData.versionId))")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.exportCaptionSidecar(\"srt\")")) ||
      !timeline_view_source.contains(QStringLiteral("function programCaptionRichText(layer)")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineTranscriptEditorPopup\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineTranscriptSegmentList\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineTranscribeAudioButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.transcribeTimelineAudio()")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.cancelTranscription()")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.transcriptionStatus")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.selectClipsInSequenceRange(")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.stageTextBasedEditDeletion(")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineTranscriptCleanupList\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineAnalyzeSilenceButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.analyzeTimelineSilence()")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.cancelSilenceAnalysis()")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.applyTranscriptCleanupSuggestions(")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineOutputVariantMenuButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.outputVariants")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.resetSelectedClipOutputVariantOverride()")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("objectName: \"timelineAutoReframeButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineAutoReframePopup\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineAutoReframeCompositionCombo\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineAnalyzeAutoReframeButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineAutoReframeSampleList\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineApplyAutoReframeButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.analyzeSelectedClipForAutoReframe(")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.cancelAutoReframeAnalysis()")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.applyAutoReframeSuggestions()")) ||
      !timeline_view_source.contains(QStringLiteral("key: \"review-notes\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineReviewNotesPopup\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineReviewNotesList\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineAddReviewNoteButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineReviewNoteAssigneeField\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineReviewNotesImportDialog\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineImportReviewNotesButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineExportReviewNotesButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.addReviewNote(")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.updateSequenceMarkerReview(")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.importReviewNotes(")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.exportReviewNotes()")) ||
      !timeline_view_source.contains(QStringLiteral("key: \"color-finishing\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineColorFinishingPopup\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineColorScopesCanvas\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineColorReferenceCombo\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineShotMatchButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineLoadColorLutButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineBuildColorPreviewButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.applySelectedClipColorLut(")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.matchSelectedClipColorToReference(")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.requestSelectedClipColorScopes()")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.generateRenderCache()")) ||
      !timeline_view_source.contains(QStringLiteral("key: \"beat-markers\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineBeatAnalysisPopup\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineAnalyzeBeatsButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.analyzeTimelineBeats()")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.cancelBeatAnalysis()")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.createBeatMarkersFromAnalysis()")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineMulticamPopup\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineMulticamSyncMethod\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineCreateMulticamButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineMulticamCutList\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.createMulticamGroupFromSelection(")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.switchMulticamAngle(")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.removeMulticamCut(")) ||
      !timeline_view_source.contains(QStringLiteral("layer.multicamActive === false")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineTitleTemplatePopup\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineTitleTemplateGrid\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.titleTemplates")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.addTitleClipFromTemplate(")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("objectName: \"timelineTitleTemplateMenuButton\"")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("root.documentController.applySelectedTitleTemplate(key)")) ||
      !timeline_view_source.contains(QStringLiteral("professionalDeliveryTruthSignature: \"timeline-professional-delivery:v1|")) ||
      !timeline_view_source.contains(QStringLiteral("key: \"delivery-settings\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineDeliverySettingsPopup\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineDeliveryEncoderCombo\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineDeliveryAdvancedToggle\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineDeliveryTwoPassButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineDeliveryAlphaButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineDeliveryStemsButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineDeliveryImageSequenceButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineQueueDeliveryButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.setDeliverySetting(")) ||
      !timeline_view_source.contains(QStringLiteral("key: \"delivery-check\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineDeliveryPreflightPopup\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineDeliveryPreflightIssueList\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.timelineDeliveryPreflight()")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineMeasureLoudnessButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.analyzeTimelineLoudness()")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.cancelTimelineLoudnessAnalysis()")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineNormalizeLoudnessButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.normalizeTimelineLoudness()")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineApplyDeliveryFormatButton\"")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("title: \"Word Timing\"")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("root.documentController.updateSelectedCaptionWord(")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineEditMenuButton\"")) ||
      !timeline_view_source.contains(QStringLiteral("sequences: [StandardKey.Copy]")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.pasteClipsAtPlayhead(\"insert\")")) ||
      !timeline_view_source.contains(QStringLiteral("timelineDocument.replaceSelectedClipFromSource()")) ||
      !keyboard_shortcuts_dialog_source.contains(QStringLiteral("Copy, cut, or paste selected timeline clips.")) ||
      !timeline_waveform_source.contains(QStringLiteral("timelineWaveformContract: \"timeline-waveform:v2|cache:media-revision-multiresolution-bounded|tiers:600-2400-9600|zoom:adaptive|trim:source-cropped|generation:async-serial|viewport:lazy|background:transparent|fallback:progressive\"")) ||
      !timeline_waveform_source.contains(QStringLiteral("Qt.rect(root.sourceStartFraction * root.resolvedSourcePixelWidth")) ||
      !timeline_waveform_source.contains(QStringLiteral("source: root.viewportActive ? root.resolvedSource : \"\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineAdaptiveWaveform\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineSourcePatchSummary\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineShuttleRateChip\"")) ||
      !timeline_view_source.contains(QStringLiteral("adaptivePlaybackSignature: \"timeline-adaptive-playback:v4|")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelinePlaybackHealthChip\"")) ||
      !timeline_view_source.contains(QStringLiteral("timelinePlayback.adaptiveQualityActive")) ||
      !timeline_view_source.contains(QStringLiteral("mediaAnalysisSignature: \"timeline-media-analysis:v1|")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineClipAnalyzingState\"")) ||
      !timeline_source_panel_source.contains(QStringLiteral("objectName: \"timelineSourceAnalyzingState\"")) ||
      !timeline_source_panel_source.contains(QStringLiteral("enabled: root.sourceEditReady")) ||
      !timeline_view_source.contains(QStringLiteral("enabled: timelineDocument.hasSourceClip && timelineDocument.sourcePatchReady")) ||
      !timeline_track_header_source.contains(QStringLiteral("const trackLabel = String(trackData.displayLabel || \"Track\")")) ||
      !timeline_track_header_source.contains(QStringLiteral("Patch source \" + trackData.trackKind + \" to \" + trackData.displayLabel")) ||
      !timeline_view_source.contains(QStringLiteral("function closeTransientSurfaces()")) ||
      !timeline_view_source.contains(QStringLiteral("onVisibleChanged: {")) ||
      !timeline_view_source.contains(QStringLiteral("root.scheduleBackgroundWorkIntentPublish()")) ||
      !timeline_view_source.contains(QStringLiteral("if (!visible) {\n            adjustmentPreviewCacheTimer.stop()")) ||
      !timeline_view_source.contains(QStringLiteral("closeTransientSurfaces()")) ||
      !timeline_view_source.contains(QStringLiteral("background: PopupSurface {")) ||
      !timeline_view_source.contains(QStringLiteral("ToneTextArea {")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineToolMenuButton\"")) ||
      !menu_button_source.contains(QStringLiteral("ownerObjectName: root.objectName")) ||
      !menu_popover_source.contains(QStringLiteral("objectName: ownerObjectName.length > 0 ? ownerObjectName + \"Popup\" : \"\"")) ||
      !menu_button_source.contains(QStringLiteral("if (!visible)\n            menuPopup.close()")) ||
      !timeline_source_panel_source.contains(QStringLiteral("FilterChip {")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("InspectorSection {")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("ToneTextField {")) ||
      !timeline_track_toggle_button_source.contains(QStringLiteral("implicitWidth: theme.iconButtonSizeCompact")) ||
      !timeline_track_toggle_button_source.contains(QStringLiteral("implicitHeight: theme.iconButtonSizeCompact")) ||
      timeline_view_source.contains(QStringLiteral("#0b1014"), Qt::CaseInsensitive) ||
      timeline_view_source.contains(QStringLiteral("#4f1713"), Qt::CaseInsensitive) ||
      timeline_view_source.contains(QStringLiteral("#e58767"), Qt::CaseInsensitive) ||
      timeline_view_source.contains(QStringLiteral("#ffd7ca"), Qt::CaseInsensitive) ||
      timeline_source_panel_source.contains(QStringLiteral("#0b1014"), Qt::CaseInsensitive) ||
      timeline_media_asset_row_source.contains(QStringLiteral("#0b1014"), Qt::CaseInsensitive) ||
      QRegularExpression(QStringLiteral("\\n\\s*TextField\\s*\\{")).match(timeline_view_source).hasMatch() ||
      QRegularExpression(QStringLiteral("\\n\\s*TextArea\\s*\\{")).match(timeline_view_source).hasMatch() ||
      QRegularExpression(QStringLiteral("\\n\\s*TextField\\s*\\{")).match(timeline_source_panel_source).hasMatch() ||
      QRegularExpression(QStringLiteral("\\n\\s*TextField\\s*\\{")).match(timeline_effects_panel_source).hasMatch() ||
      QRegularExpression(QStringLiteral("\\n\\s*TextArea\\s*\\{")).match(timeline_effects_panel_source).hasMatch())) {
    return fail("Timeline UI should preserve the video-editor layout while using shared theme tokens, controls, inspector sections, 32-pixel targets, semantic media wells, and transient popup lifecycle rules");
  }
  if (!timeline_view_source.contains(QStringLiteral("audioMixerTruthSignature: \"timeline-audio-mixer:v6|window:focused-modular|strips:selected-clip-tracks-role-groups-submixes-master|roles:dialogue-music-ambience-effects-voiceover-commentary|role-routing:dialogue-music-sfx|role-batch:mute-solo-gain-route|processors:eq-compressor-limiter-gate-deesser-highpass-lowpass|processor-automation:track-keyframes|bypass:loudness-matched|ducking:role-generated-editable-regenerable-manual-preserved|controls:mute-solo-pan-gain-fades-inserts|processing:preview-export-shared|state:undo-save-reopen|density:default-unchanged\"")) ||
      !timeline_view_source.contains(QStringLiteral("masterLoudnessTruthSignature: \"timeline-master-loudness:v1|source:post-processing-track-rms|aggregate:active-stereo-energy|momentary:400ms|short-term:3s|history:3s-bounded|clip-latch:800ms|ui:playback-contextual|delivery:exact-postmix-lufs\"")) ||
      !timeline_view_source.contains(QStringLiteral("trackAutomationSignature: \"timeline-track-automation:v8|video:opacity|audio:gain-pan-eq-compressor-limiter-gate-deesser-highpass-lowpass|keyframe-tangents:weighted-bezier-in-out-linked-broken|curve:shared-modular-value-speed-editor|clipboard:copy-paste-full-or-selected|bulk-align-distribute-scale:multi-key-undoable|ui:context-mixer-processor-window-inline-lanes|edit:add-drag-time-value-interpolation-delete|preview:decoded-buffer-playhead-evaluated|export:continuous-gain-pan-keyframe-segment-processors|meters:stereo-peak-clip|persistence:undo-save-reopen\"")) ||
      !timeline_view_source.contains(QStringLiteral("function programAudioPan(layer)")) ||
      !timeline_view_source.contains(QStringLiteral("TimelineAudioOutput {")) ||
      !timeline_view_source.contains(QStringLiteral("audioBufferOutput: layerAudioOutput.bufferOutput")) ||
      !timeline_view_source.contains(QStringLiteral("Number(layerAudioOutput.rmsLeft || 0)")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineMasterLoudnessMeter\"")) ||
      !timeline_view_source.contains(QStringLiteral("objectName: \"timelineMasterLoudnessHistory\"")) ||
      !timeline_view_source.contains(QStringLiteral("masterMomentaryLufs")) ||
      !timeline_view_source.contains(QStringLiteral("masterShortTermLufs")) ||
      !timeline_view_source.contains(QStringLiteral("meterVisible: root.sequencePlaying")) ||
      !timeline_track_header_source.contains(QStringLiteral("timelineTrackMixerContract: \"timeline-track-mixer:v3|popup:gain-pan-processing|automation:inline-lanes-contextual|routing:main-dialogue-music-sfx|processing:progressive-three-band-eq-compressor-ducking|meters:contextual-stereo-peak-clip|default-density:unchanged|controls:shared\"")) ||
      !timeline_track_header_source.contains(QStringLiteral("objectName: \"timelineTrackStereoMeter\"")) ||
      !timeline_track_header_source.contains(QStringLiteral("label: \"Pan\"")) ||
      !timeline_track_header_source.contains(QStringLiteral("model: [\"Main\", \"Dialogue\", \"Music\", \"SFX\"]")) ||
      !timeline_track_header_source.contains(QStringLiteral("\"audioCompressorEnabled\"")) ||
      !timeline_track_header_source.contains(QStringLiteral("\"audioDuckingEnabled\"")) ||
      !timeline_track_header_source.contains(QStringLiteral("trackData.trackIndex, \"audioPan\", nextValue"))) {
    return fail("Timeline track audio should share persistent routing, EQ, dynamics, ducking, decoded-buffer preview/export, and contextual meters");
  }
  if (!timeline_track_toggle_button_source.contains(QStringLiteral("timelineTrackToggleButtonContract: \"timeline-track-toggle-button:v1|checked-state:true|disabled-state:true|hover:true|keyboard:true|tooltip-full:true|accessibility-full:true|no-elide:true\"")) ||
      !timeline_track_toggle_button_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|compact-glyph:true|tooltip-full:true|accessibility-full:true\"")) ||
      !timeline_track_toggle_button_source.contains(QStringLiteral("Accessible.name: root.label")) ||
      !timeline_track_toggle_button_source.contains(QStringLiteral("Accessible.checked: root.checked")) ||
      !timeline_track_toggle_button_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      !timeline_track_toggle_button_source.contains(QStringLiteral("text: root.label")) ||
      !app_menu_source.contains(QStringLiteral("appMenuContract: \"app-menu:v1|shared-surface:true|keyboard:true|escape:true|premium-border:true|consistent-padding:true|readable-items:true\"")) ||
      !app_menu_source.contains(QStringLiteral("closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent")) ||
      !app_menu_item_source.contains(QStringLiteral("appMenuItemContract: \"app-menu-item:v1|hover:true|keyboard:true|disabled-state:true|shortcut-fit:true|readable-text:true|no-elide:true\"")) ||
      !app_menu_item_source.contains(QStringLiteral("fontSizeMode: Text.HorizontalFit")) ||
      !app_menu_item_source.contains(QStringLiteral("elide: Text.ElideNone")) ||
      !timeline_track_header_source.contains(QStringLiteral("Components.ToolButton {")) ||
      !QRegularExpression(QStringLiteral("AppMenu\\s*\\{\\s*id:\\s*trackMenu\\b")).match(timeline_track_header_source).hasMatch() ||
      !timeline_track_header_source.contains(QStringLiteral("AppMenuItem {")) ||
      !timeline_track_header_source.contains(QStringLiteral("label: trackData.trackKind === \"video\"")) ||
      !timeline_track_header_source.contains(QStringLiteral(": (trackData.muted ? \"audio-muted\" : \"audio\")")) ||
      !timeline_track_header_source.contains(QStringLiteral("onClicked: root.togglePrimaryTrackState()")) ||
      !timeline_track_header_source.contains(QStringLiteral("onClicked: root.toggleSoloTrackState()")) ||
      !timeline_track_header_source.contains(QStringLiteral("onClicked: root.toggleTargetedTrackState()")) ||
      !timeline_track_header_source.contains(QStringLiteral("onClicked: root.toggleLockedTrackState()")) ||
      QRegularExpression(QStringLiteral("\\n\\s*Menu\\s*\\{")).match(timeline_track_header_source).hasMatch()) {
    return fail("Timeline track headers should use labeled no-elide toggle buttons and shared AppMenu context actions while preserving primary, solo, target, and lock commands");
  }
  if (!timeline_effect_library_row_source.contains(QStringLiteral("timelineEffectLibraryRowContract: \"timeline-effect-library-row:v2|drag:true|add:true|favorite:true|recent:true|compatible:true|thumbnail:bounded-live-current-source|thumbnail-size:144x88-max|animation:none|keyboard:true|hover:true|readable-text:true|no-elide:true\"")) ||
      !timeline_effect_library_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|title-wrap:true|description-wrap:true|tooltip-full:true|accessibility-full:true\"")) ||
      !timeline_effect_library_row_source.contains(QStringLiteral("property string libraryType: \"effect\"")) ||
      !timeline_effect_library_row_source.contains(QStringLiteral("Drag.keys: [\"effect\"]")) ||
      !timeline_effect_library_row_source.contains(QStringLiteral("DragHandler {")) ||
      !timeline_effect_library_row_source.contains(QStringLiteral("signal addRequested(string effectName)")) ||
      !timeline_effect_library_row_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      !timeline_effect_library_row_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("delegate: TimelineEffectLibraryRow")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("model: root.filteredEffectLibraryItems")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("actionEnabled: root.hasSelection")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("root.addQuickEffect(effectName)"))) {
    return fail("Timeline effect library should use TimelineEffectLibraryRow with drag metadata, readable text, keyboard add, and preserved add behavior");
  }
  if (!timeline_transition_library_row_source.contains(QStringLiteral("timelineTransitionLibraryRowContract: \"timeline-transition-library-row:v1|drag:true|in-out:true|search-filter:true|keyboard:true|hover:true|readable-text:true|no-elide:true\"")) ||
      !timeline_transition_library_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|title-wrap:true|description-wrap:true|tooltip-full:true|accessibility-full:true\"")) ||
      !timeline_transition_library_row_source.contains(QStringLiteral("property string libraryType: \"transition\"")) ||
      !timeline_transition_library_row_source.contains(QStringLiteral("readonly property real transitionDurationSeconds: transitionName === \"Cut\" ? 0 : 0.35")) ||
      !timeline_transition_library_row_source.contains(QStringLiteral("Drag.keys: [\"transition\"]")) ||
      !timeline_transition_library_row_source.contains(QStringLiteral("DragHandler {")) ||
      !timeline_transition_library_row_source.contains(QStringLiteral("signal transitionRequested(string edge, string transitionName, real durationSeconds)")) ||
      !timeline_transition_library_row_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      !timeline_transition_library_row_source.contains(QStringLiteral("wrapMode: Text.WrapAtWordBoundaryOrAnywhere")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("delegate: TimelineTransitionLibraryRow")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("searchText: root.transitionSearchText")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("actionEnabled: root.hasSelection")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("root.assignTransition(edge, transitionName, durationSeconds)"))) {
    return fail("Timeline transition library should use TimelineTransitionLibraryRow with drag metadata, readable text, keyboard In assignment, and preserved In/Out behavior");
  }
  if (!timeline_effects_panel_source.contains(QStringLiteral("InspectorSlider {")) ||
      !inspector_slider_source.contains(QStringLiteral("property int resetColumnWidth: 28")) ||
      !inspector_slider_source.contains(QStringLiteral("property int controlHeight: theme.scaled(theme.densityComfortable ? 62 : theme.densityCompact ? 50 : 56)")) ||
      !inspector_slider_source.contains(QStringLiteral("readonly property int trackHeight: 3")) ||
      !inspector_slider_source.contains(QStringLiteral("readonly property int thumbRestSize: 12")) ||
      !inspector_slider_source.contains(QStringLiteral("readonly property int thumbPressedSize: 16")) ||
      !inspector_slider_source.contains(QStringLiteral("readonly property int thumbReservedSize: Math.max(thumbRestSize, thumbPressedSize)")) ||
      !inspector_slider_source.contains(QStringLiteral("|reserved-thumb:\" +")) ||
      !inspector_slider_source.contains(QStringLiteral("|fixed-reset:true|stable-value:true|label-layout:fluid|disabled-reason:true|no-elide:true|double-click-reset:true|no-handle-layout-shift:true")) ||
      !inspector_slider_source.contains(QStringLiteral("|drag-commit-throttle:\" +")) ||
      !inspector_slider_source.contains(QStringLiteral("function queueDraggedValue(value)")) ||
      !inspector_slider_source.contains(QStringLiteral("function flushQueuedDraggedValue()")) ||
      !inspector_slider_source.contains(QStringLiteral("id: dragCommitTimer")) ||
      !inspector_slider_source.contains(QStringLiteral("onMoved: root.queueDraggedValue(control.value)")) ||
      !inspector_slider_source.contains(QStringLiteral("root.flushQueuedDraggedValue()")) ||
      !inspector_slider_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|label-fit:true|value-fit:true|tooltip-full:true|accessibility-full:true\"")) ||
      !inspector_slider_source.contains(QStringLiteral("valueGeometryContract: \"slider-value-geometry:v5|layout:adaptive-compact-row-or-label-value-header-wide-rail|label-column-fixed:false|value-column-fixed:true|reset-column-fixed:true|wide-row-height:density-fit|compact-row-height:configured|track-hit-height:24|track-height:3|thumb:12-16|reserved-thumb:16|handle-layout-shift:false\"")) ||
      !inspector_slider_source.contains(QStringLiteral("resetActionContract: \"slider-reset-action:v1|icon-only:true|tooltip-full:true|accessible-label:true|disabled-reason:true|active-when-modified:true|fixed-column:\"")) ||
      !inspector_slider_source.contains(QStringLiteral("property string disabledReason: \"\"")) ||
      !inspector_slider_source.contains(QStringLiteral("Accessible.description: !root.enabled && root.disabledReason.length > 0 ? root.disabledReason : root.valueText")) ||
      !inspector_slider_source.contains(QStringLiteral("accessibleLabel: root.label + \" value\"")) ||
      !tone_text_field_source.contains(QStringLiteral("Accessible.name: root.resolvedAccessibleName")) ||
      !tone_text_field_source.contains(QStringLiteral("Accessible.role: Accessible.EditableText")) ||
      !inspector_slider_source.contains(QStringLiteral("readonly property bool wideRailLayout: !forceCompact && (controlHeight >= theme.scaled(52)")) ||
      !inspector_slider_source.contains(QStringLiteral("x: root.compactValueX")) ||
      !inspector_slider_source.contains(QStringLiteral("width: Math.max(1, root.wideRailLayout")) ||
      !inspector_slider_source.contains(QStringLiteral("width: root.wideRailLayout")) ||
      !inspector_slider_source.contains(QStringLiteral("x: root.wideRailLayout ? theme.space6 : root.compactTrackX")) ||
      !inspector_slider_source.contains(QStringLiteral("width: Math.round(root.thumbReservedSize * root.sliderScale)")) ||
      !inspector_slider_source.contains(QStringLiteral("width: parent.width * root.thumbVisualScale")) ||
      !inspector_slider_source.contains(QStringLiteral("Components.ToolButton {")) ||
      !inspector_slider_source.contains(QStringLiteral("objectName: root.objectName.length > 0 ? root.objectName + \"ResetButton\" : \"\"")) ||
      !inspector_slider_source.contains(QStringLiteral("showLabel: false")) ||
      !inspector_slider_source.contains(QStringLiteral("enabled: root.enabled && root.modified")) ||
      !inspector_slider_source.contains(QStringLiteral("onClicked: if (root.modified) root.resetRequested()")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("label: \"Scale\"")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("root.documentController.setSelectedFrameScale(value)")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("label: \"Horizontal\"")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("root.documentController.setSelectedFrameOffsetX(value)")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("label: \"Vertical\"")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("root.documentController.setSelectedFrameOffsetY(value)")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("label: \"Opacity\"")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("valueInputMode: \"percentNormalized\"")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("root.documentController.setSelectedClipOpacity(value)")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("label: \"Gain\"")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("root.documentController.setSelectedClipAudioGain(value)")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("label: \"Fade In\"")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("root.documentController.setSelectedClipFadeInSeconds(value)")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("label: \"Fade Out\"")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("root.documentController.setSelectedClipFadeOutSeconds(value)")) ||
      !timeline_effects_panel_source.contains(QStringLiteral("onResetRequested: if (root.documentController) root.documentController.setSelectedClipAudioGain(0)"))) {
    return fail("Timeline motion and audio controls should use shared InspectorSlider rows with stable value columns, typed entry, reset, and preserved setters");
  }
  if (!timeline_media_asset_row_source.contains(QStringLiteral("timelineMediaAssetRowContract: \"timeline-media-asset-row:v5|preview:true|row-actions:more-menu|selection:click|double-click:preview-source|rating-favorite-reject-color-tags:contextual|search:controller-indexed-with-match-scope|hover-scrub:debounced-bounded-low-resolution-cache|filmstrip-density:5-9-15|keyboard:0-5-f-x|hover:true|readable-text:true|no-elide:true\"")) ||
      !timeline_media_asset_row_source.contains(QStringLiteral("readableTextContract: \"readable-text:v1|ellipsis:none|title-wrap:true|metadata-wrap:true|tooltip-full:true|accessibility-full:true\"")) ||
      !timeline_media_asset_row_source.contains(QStringLiteral("rowAnatomyContract: \"row-anatomy:v2|preview-zone:fixed-84|content:title-metadata-wrap|action-zone:more-menu-34|actions:source-insert-overwrite|selection:left-edge|hover:neutral|focus:ring\"")) ||
      !timeline_media_asset_row_source.contains(QStringLiteral("readonly property int actionMenuWidth: theme.scaled(34)")) ||
      !timeline_media_asset_row_source.contains(QStringLiteral("signal sourceRequested(string assetId, string filePath, string titleText, string thumbnailSource)")) ||
      !timeline_media_asset_row_source.contains(QStringLiteral("signal insertRequested(string assetId, string filePath, string titleText, string thumbnailSource)")) ||
      !timeline_media_asset_row_source.contains(QStringLiteral("signal overwriteRequested(string assetId, string filePath, string titleText, string thumbnailSource)")) ||
      !timeline_media_asset_row_source.contains(QStringLiteral("Keys.onReturnPressed")) ||
      !timeline_media_asset_row_source.contains(QStringLiteral("onDoubleTapped: root.sourceRequested")) ||
      !timeline_media_asset_row_source.contains(QStringLiteral("objectName: \"timelineMediaAssetActionsMenu\"")) ||
      !timeline_media_asset_row_source.contains(QStringLiteral("\"label\": \"Preview in Source\"")) ||
      !timeline_media_asset_row_source.contains(QStringLiteral("\"label\": \"Add to Timeline at Playhead\"")) ||
      !timeline_media_asset_row_source.contains(QStringLiteral("\"label\": \"Overwrite at Playhead\"")) ||
      !timeline_media_asset_row_source.contains(QStringLiteral("MenuButton {")) ||
      !timeline_source_panel_source.contains(QStringLiteral("delegate: TimelineMediaAssetRow")) ||
      !timeline_source_panel_source.contains(QStringLiteral("mediaSearchText: root.mediaSearchText")) ||
      !timeline_source_panel_source.contains(QStringLiteral("root.loadIntoSource(assetId, filePath, titleText, thumbnailSource)")) ||
      !timeline_source_panel_source.contains(QStringLiteral("timelineDocument.insertClipFromGallery(assetId, filePath, titleText, 0, 0, 0, thumbnailSource)")) ||
      !timeline_source_panel_source.contains(QStringLiteral("timelineDocument.overwriteClipFromGallery(assetId, filePath, titleText, 0, 0, 0, thumbnailSource)"))) {
    return fail("Timeline source media browser should use TimelineMediaAssetRow with readable clip labels, keyboard load, and preserved source/insert/overwrite behavior");
  }
  }
	  if (!studio_workspace_source.contains(QStringLiteral("objectName: \"activeEditTargetReturnToBaseButton\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("active-target-context:v4|inspector-visible:true")) ||
	      !studio_workspace_source.contains(QStringLiteral("const retainedLayerSelection = root.normalizedPhotoLayerSelection()")) ||
      !studio_workspace_source.contains(QStringLiteral("root.selectedPhotoLayerIndices = retainedLayerSelection")) ||
      !studio_workspace_source.contains(QStringLiteral("tooltip: \"Return to Base Image editing without closing the active layer or selection.\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.returnToBaseImageFromShortcut()"))) {
    return fail("Studio active target bar should expose a clear Return to Base Image control");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function adjustmentGroupFilterText(groupKey)")) ||
      !studio_workspace_source.contains(QStringLiteral("function adjustmentGroupEditStack(groupKey)")) ||
      !studio_workspace_source.contains(QStringLiteral("function activateLayerAdjustmentGroup(layerIndex, groupKey)")) ||
      !studio_workspace_source.contains(QStringLiteral("root.setActivePhotoLayerSelection(index, false)")) ||
      !studio_workspace_source.contains(QStringLiteral("root.editAdjustmentFilterText = root.adjustmentGroupFilterText(groupKey)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerChangedGroupBadgeTargetArea\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.activateLayerAdjustmentGroup(photoLayerRowRoot.layerIndex, parent.groupKey)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerMaskSetThumbnailTargetArea\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.selectMaskSetAndLayer(photoLayerMaskSetRowRoot.modelData, false)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskThumbnailTargetArea\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.selectMaskSetAndLayer(photoLayerNestedMaskRowRoot.modelData, false)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeLayerMaskChildThumbnailTargetArea\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.selectMaskSetAndLayer(activeLayerMaskChildRowRoot.modelData, false)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function outlinePathsHitTest(mouseX, mouseY, outlinePaths, slopPixels)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function selectActiveOutlineFromCanvas(mouseX, mouseY)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("objectName: \"maskCanvasActiveOutlineHitTarget\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("selectionSyncContract: \"canvas-outline-hit-test|selectMaskSetAndLayer|preserve-inspector:v1\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editRoot.selectMaskSetAndLayer(editRoot.activeMaskSetData(), false, true)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("signal activeOutlineClicked(real normalizedX, real normalizedY)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("function outlinePathsHitTest(mouseX, mouseY, outlinePaths, slopPixels)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("function selectActiveOutlineFromCanvas(mouseX, mouseY)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("SharedActiveOutlineHitTarget")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("selectionSyncContract: \"canvas-outline-hit-test|activeOutlineClicked|selectMaskSetAndLayer:v1\"")) ||
      !studio_image_viewport_source.contains(QStringLiteral("onActiveOutlineClicked: workspaceContext.selectMaskSetAndLayer(workspaceContext.activeMaskSetData(), false, true)"))) {
    return fail("Studio layer and mask rows should provide explicit synchronized click targets for badges and thumbnails");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function activateBaseImageLayerTarget()")) ||
      !studio_workspace_source.contains(QStringLiteral("function globalChangedAdjustmentGroups()")) ||
      !studio_workspace_source.contains(QStringLiteral("function globalAdjustmentSummary()")) ||
      !studio_workspace_source.contains(QStringLiteral("targetAreaObjectName: \"baseImageLayerRowTargetArea\"")) ||
      !studio_workspace_source.contains(QStringLiteral("clickRouteContract: \"base-image-row-click:activate-base-target:v1\"")) ||
      !layer_row_source.contains(QStringLiteral("function triggerBaseImageLayerRowClickForTest()")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: root.activateBaseImageLayerTarget()")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.baseImageThumbnailSource")) ||
      !studio_workspace_source.contains(QStringLiteral("thumbnailImageObjectName: \"baseImageLayerThumbnailImage\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string baseLayerThumbnailDprContract: thumbnailDprContract")) ||
      !studio_workspace_source.contains(QStringLiteral("thumbnailSourceSize: root.thumbnailSourceSizeForDisplay(thumbnailZoneSize, 96)")) ||
      !studio_workspace_source.contains(QStringLiteral("lockBadgeObjectName: \"baseImageLayerLockBadge\"")) ||
      !studio_workspace_source.contains(QStringLiteral("metadataObjectName: \"baseImageLayerMetadataLabel\"")) ||
      !studio_workspace_source.contains(QStringLiteral("subtitle: \"Original · \" + (baseLayerLocked ? \"Locked\" : \"Unlocked · Eraser available\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string baseLayerTargetKind: \"global-edits\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string baseLayerContract: \"base-image-layer:v2|locked:\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string changedAdjustmentGroupSignature: root.globalChangedAdjustmentGroupSignature()"))) {
    return fail("Studio Layers panel should expose Base Image as a selectable locked global-edit row");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function unlinkedMaskSets()")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskSetIdentity(maskSet)")) ||
      !studio_workspace_source.contains(QStringLiteral("function linkedMaskSetIdentities()")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskSetListPartitionSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("function photoLayerMaskSetIds(layer)")) ||
      !studio_workspace_source.contains(QStringLiteral("function photoLayerMaskSetNames(layer)")) ||
      !studio_workspace_source.contains(QStringLiteral("function normalizedPhotoLayerMaskSetValues(layer, listKey, singleKey)")) ||
      !studio_workspace_source.contains(QStringLiteral("function moveLayerMaskSet(layerIndex, maskSet, targetMaskIndex)")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.moveMaskSetInPhotoLayer")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property var unlinkedMaskSets: photoLayersInspectorGroup.inspectorActive ? root.unlinkedMaskSets() : []")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property string standaloneMaskListContract: \"standalone-mask-list:unlinked-only|count:\" + unlinkedMaskSets.length")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property string maskListPartitionSignature: photoLayersInspectorGroup.inspectorActive ? root.maskSetListPartitionSignature() : \"mask-list-partition:v1|inactive\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("visible: photoLayersInspectorGroup.inspectorActive && unlinkedMaskSets.length > 0")) ||
      !studio_workspace_source.contains(QStringLiteral("model: photoLayersMaskSetList.sectionCollapsed ? [] : photoLayersMaskSetList.unlinkedMaskSets")) ||
      !studio_workspace_source.contains(QStringLiteral("id: photoLayerNestedMaskList")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskList\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property var linkedMaskSets: root.maskSetsForAdjustmentLayer(layerIndex)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property var linkedMaskSetIdentities: root.maskSetIdentityList(linkedMaskSets)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string nestedMaskListContract: \"nested-mask-list:linked-only|layer:\"")) ||
      !studio_workspace_source.contains(QStringLiteral("visible: linkedMaskSets.length > 0")) ||
      !studio_workspace_source.contains(QStringLiteral("model: photoLayerNestedMaskList.maskSectionCollapsed ? [] : photoLayerNestedMaskList.linkedMaskSets")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskSourceIconName(kindName)")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskTypeIcon\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string combineModeSummary: String(modelData.combineModeSummary || \"Add\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string combineModeContract: String(modelData.combineModeContract || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string combineOrderLabel: (combineOrderIndex + 1).toString() + \" / \" + combineOrderCount.toString()")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskOrderLabel\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskMoveUpButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskMoveDownButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskChildRowContract: \"nested-mask-child:v1|indent:\""))) {
    return fail("Studio Layers panel should nest linked masks under layers and reserve the standalone list for unlinked masks");
  }
  if (!studio_workspace_source.contains(QStringLiteral("property bool standaloneMaskSectionCollapsed: false")) ||
      !studio_workspace_source.contains(QStringLiteral("property var collapsedLayerMaskSections: ({})")) ||
      !studio_workspace_source.contains(QStringLiteral("function photoLayerMaskSectionKey(layerIndex, layer)")) ||
      !studio_workspace_source.contains(QStringLiteral("function photoLayerMaskSectionCollapsed(layerIndex, layer)")) ||
      !studio_workspace_source.contains(QStringLiteral("function setPhotoLayerMaskSectionCollapsed(layerIndex, layer, collapsed)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string standaloneMaskSectionContract: \"mask-section:v1|scope:unlinked|count:\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayersMaskSetSectionHeader\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayersMaskSetSectionToggle\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSectionContract: \"mask-section:v1|scope:layer|key:\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskSectionHeader\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskSectionLabel\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskSectionToggle\"")) ||
      !studio_workspace_source.contains(QStringLiteral("model: photoLayerNestedMaskList.maskSectionCollapsed ? [] : photoLayerNestedMaskList.linkedMaskSets"))) {
    return fail("Studio Layers panel should expose collapsible mask sections for complex layer stacks");
  }
  if (!studio_workspace_source.contains(QStringLiteral("readonly property string finalAlphaSourceKind: String(modelData.mask_source_kind || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string finalAlphaTraceSignature: String(modelData.mask_alpha_trace_signature || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool finalAlphaComposite: finalAlphaSourceKind === \"composite\" &&")) ||
      !studio_workspace_source.contains(QStringLiteral("finalAlphaTraceSignature.indexOf(\"mask-thumbnail-composite\") >= 0"))) {
    return fail("Studio mask thumbnails should expose final composite alpha provenance");
  }
  if (!studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailRenderPath: String(layerData.thumbnail_render_stage_contract || layerData.thumbnailRenderStageContract || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailAlphaTraceSignature: String(layerData.mask_alpha_trace_signature || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailDisplaySource: String(layerData.thumbnailDisplaySource || thumbnailSource)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailEffectPreviewContract: String(layerData.thumbnailEffectPreviewContract || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailUsesEffectPreview: thumbnailEffectPreviewSource.length > 0 && thumbnailDisplaySource === thumbnailEffectPreviewSource")) ||
      !studio_workspace_source.contains(QStringLiteral("source: parent.thumbnailDisplaySource")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailInvalidationColorPipeline: String(layerData.thumbnailInvalidationColorPipelineVersion || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool layerThumbnailCompositePath: layerThumbnailAlphaTraceSignature.indexOf(\"layer-thumbnail-composite\") >= 0")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool layerThumbnailColorManaged: layerThumbnailInvalidationColorPipeline.length > 0")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailPathSignature: \"layer-thumbnail|stage:\" + layerThumbnailStage + \"|composite:\" + String(layerThumbnailCompositePath) + \"|color:\" + layerThumbnailInvalidationColorPipeline + \"|key:\" + layerThumbnailInvalidationKey"))) {
    return fail("Studio layer thumbnails should expose composited render path and color-management provenance");
  }
  if (!studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailInvalidationKey: String(layerData.thumbnailInvalidationKey || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailInvalidationRecipeHash: String(layerData.thumbnailInvalidationRecipeHash || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailInvalidationMaskAlphaHash: String(layerData.thumbnailInvalidationMaskAlphaHash || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailInvalidationBlendMode: String(layerData.thumbnailInvalidationBlendMode || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailInvalidationOpacity: String(layerData.thumbnailInvalidationOpacity || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool layerThumbnailInvalidationVisible: layerData.thumbnailInvalidationVisible === true")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailInvalidationKey: String(modelData.thumbnailInvalidationKey || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailInvalidationMaskAlphaHash: String(modelData.thumbnailInvalidationMaskAlphaHash || \"\")"))) {
    return fail("Studio thumbnail rows should expose thumbnail invalidation identity fields");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function thumbnailScreenDevicePixelRatio()")) ||
      !studio_workspace_source.contains(QStringLiteral("function thumbnailSourceSizeForDisplay(displayPixels, backingPixels)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property int screenAwareThumbnailBackingWidth: root.thumbnailSourceSizeForDisplay(32, thumbnailBackingWidth)")) ||
      !studio_workspace_source.contains(QStringLiteral("sourceSize.width: parent.screenAwareThumbnailBackingWidth")) ||
      !studio_workspace_source.contains(QStringLiteral("sourceSize.height: parent.screenAwareThumbnailBackingHeight"))) {
    return fail("Studio thumbnails should bind decode source size to current screen device pixel ratio");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function thumbnailDisplayState(data)")) ||
      !studio_workspace_source.contains(QStringLiteral("thumbnailStateUtils.thumbnailDisplayState(data)")) ||
      !studio_workspace_source.contains(QStringLiteral("thumbnailStateUtils.thumbnailStateSignature(data)")) ||
      !thumbnail_state_utils_source.contains(QStringLiteral("return \"progressive-updating\"")) ||
      !thumbnail_state_utils_source.contains(QStringLiteral("+ \"|display:\" + displayState")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailDisplayState: root.thumbnailDisplayState(layerData)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailDisplayState: photoLayerRowRoot.thumbnailDisplayState")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailDisplayState: photoLayerMaskSetRowRoot.thumbnailDisplayState")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailDisplayState: photoLayerNestedMaskRowRoot.thumbnailDisplayState"))) {
    return fail("Studio thumbnail rows should expose a stable display-state contract for progressive replacement");
  }
  if (!studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailProgressive: layerData.thumbnailProgressive === true")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailProgressiveReason: String(layerData.thumbnailProgressiveReason || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailProgressiveSignature: String(layerData.thumbnailProgressiveSignature || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailProgressive: modelData.thumbnailProgressive === true")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailProgressive: photoLayerMaskSetRowRoot.thumbnailProgressive")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailProgressive: photoLayerNestedMaskRowRoot.thumbnailProgressive"))) {
    return fail("Studio thumbnail rows should expose progressive last-good thumbnail state");
  }
  if (!studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailFailureStage: String(layerData.thumbnailFailureStage || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailFailureDetail: String(layerData.thumbnailFailureDetail || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailFailureSignature: String(layerData.thumbnailFailureSignature || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailFailureStage: String(modelData.thumbnailFailureStage || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailFailureStage: photoLayerMaskSetRowRoot.thumbnailFailureStage")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailFailureStage: photoLayerNestedMaskRowRoot.thumbnailFailureStage"))) {
    return fail("Studio thumbnail rows should expose contained thumbnail failure attribution");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function thumbnailRefreshQueued(data)")) ||
      !studio_workspace_source.contains(QStringLiteral("thumbnailStateUtils.thumbnailRefreshQueued(data)")) ||
      !studio_workspace_source.contains(QStringLiteral("thumbnailStateUtils.thumbnailQueueSignature(data)")) ||
      !thumbnail_state_utils_source.contains(QStringLiteral("function thumbnailRefreshQueued(data)")) ||
      !thumbnail_state_utils_source.contains(QStringLiteral("function thumbnailQueueSignature(data)")) ||
      !thumbnail_state_utils_source.contains(QStringLiteral("thumbnailQueueJobId")) ||
      !thumbnail_state_utils_source.contains(QStringLiteral("thumbnailQueueDeferBlockerSignature")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailRefreshQueued: root.thumbnailRefreshQueued(layerData)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string thumbnailQueueSignature: root.thumbnailQueueSignature(layerData)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailRefreshQueued: photoLayerRowRoot.thumbnailRefreshQueued")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailRefreshQueued: photoLayerMaskSetRowRoot.thumbnailRefreshQueued")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool thumbnailRefreshQueued: photoLayerNestedMaskRowRoot.thumbnailRefreshQueued"))) {
    return fail("Studio thumbnail rows should expose deferred thumbnail queue state");
  }
  if (!studio_workspace_source.contains(QStringLiteral("function thumbnailRetryActionSignature(data, rowKind)")) ||
      !studio_workspace_source.contains(QStringLiteral("thumbnailStateUtils.thumbnailStatusLabel(data, fallbackText)")) ||
      !studio_workspace_source.contains(QStringLiteral("thumbnailStateUtils.thumbnailRetryActionSignature(data, rowKind)")) ||
      !thumbnail_state_utils_source.contains(QStringLiteral("function thumbnailStatusLabel(data, fallbackText)")) ||
      !thumbnail_state_utils_source.contains(QStringLiteral("function thumbnailRetryActionSignature(data, rowKind)")) ||
      !thumbnail_state_utils_source.contains(QStringLiteral("Thumbnail stale / ")) ||
      !thumbnail_state_utils_source.contains(QStringLiteral("thumbnail-retry|row:")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerThumbnailRetryButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerMaskSetThumbnailRetryButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeLayerMaskChildThumbnailRetryButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskThumbnailRetryButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("onClicked: editorPreview.retryThumbnailRender()"))) {
    return fail("Studio thumbnail rows should expose a contained, non-blocking retry action for failed thumbnails");
  }
  const auto white_balance_section_index =
      studio_workspace_source.indexOf(QStringLiteral("objectName: \"whiteBalanceInspectorGroup\""));
  const auto light_section_index =
      studio_workspace_source.indexOf(QStringLiteral("objectName: \"lightInspectorGroup\""));
  const auto color_section_index =
      studio_workspace_source.indexOf(QStringLiteral("objectName: \"colorInspectorGroup\""));
  const auto levels_section_index =
      studio_workspace_source.indexOf(QStringLiteral("objectName: \"levelsInspectorGroup\""));
  const auto color_mix_section_index =
      studio_workspace_source.indexOf(QStringLiteral("objectName: \"colorMixInspectorGroup\""));
  const auto color_grading_section_index =
      studio_workspace_source.indexOf(QStringLiteral("objectName: \"colorGradingInspectorGroup\""));
  const auto tone_curve_section_index =
      studio_workspace_source.indexOf(QStringLiteral("objectName: \"toneCurveInspectorGroup\""));
  const auto presence_section_index =
      studio_workspace_source.indexOf(QStringLiteral("objectName: \"presenceInspectorGroup\""));
  const auto raw_section_index =
      studio_workspace_source.indexOf(QStringLiteral("objectName: \"rawInspectorGroup\""));
  const auto advanced_color_section_index =
      studio_workspace_source.indexOf(QStringLiteral("objectName: \"advancedColorInspectorGroup\""));
  if (white_balance_section_index < 0 || light_section_index < 0 || levels_section_index < 0 ||
      color_section_index < 0 || color_mix_section_index < 0 || color_grading_section_index < 0 ||
      tone_curve_section_index < 0 || presence_section_index < 0 ||
      raw_section_index < 0 || advanced_color_section_index < 0 ||
      !(white_balance_section_index < light_section_index &&
        light_section_index < raw_section_index && raw_section_index < levels_section_index &&
        levels_section_index < tone_curve_section_index && tone_curve_section_index < color_section_index &&
        color_section_index < color_mix_section_index && color_mix_section_index < color_grading_section_index &&
        color_grading_section_index < advanced_color_section_index &&
        advanced_color_section_index < presence_section_index) ||
      !studio_workspace_source.contains(QStringLiteral("title: \"White Balance\"")) ||
      !studio_workspace_source.contains(QStringLiteral("title: \"Light\"")) ||
      !studio_workspace_source.contains(QStringLiteral("title: \"Levels\"")) ||
      !studio_workspace_source.contains(QStringLiteral("title: \"Color\"")) ||
      !studio_workspace_source.contains(QStringLiteral("title: \"Color Mix\"")) ||
      !studio_workspace_source.contains(QStringLiteral("title: \"Color Grading\"")) ||
      !studio_workspace_source.contains(QStringLiteral("title: \"Tone Curve\"")) ||
      !studio_workspace_source.contains(QStringLiteral("title: \"Presence\""))) {
    return fail("Adjust inspector source should order sections by photographer workflow: white balance, light, raw, levels, tone curve, color, color mix, grading, advanced color, presence");
  }
  trace_smoke_progress("adjust inspector workflow order source checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_ADJUST_INSPECTOR_ORDER_SOURCE")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }
	  if (!studio_workspace_source.contains(QStringLiteral("function maskAlphaProviderSummary(data)")) ||
	      !studio_workspace_source.contains(QStringLiteral("return \"Selection preview unavailable\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("\"Selection preview / \" + (full ? \"detailed\" : \"preview\")")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property string maskAlphaProviderContract: String(modelData.mask_alpha_provider_contract || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool maskAlphaFullResolutionAvailable: modelData.mask_alpha_full_resolution_available === true")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool maskAlphaDeterministic: modelData.mask_alpha_deterministic === true")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskAlphaProviderSignature: String(modelData.mask_alpha_provider_signature || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskAlphaProviderSummary: root.maskAlphaProviderSummary(modelData)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailAlphaProviderContract: String(layerData.mask_alpha_provider_contract || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool layerThumbnailAlphaFullResolutionAvailable: layerData.mask_alpha_full_resolution_available === true")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string layerThumbnailAlphaProviderSummary: root.maskAlphaProviderSummary(layerData)"))) {
	    return fail("Studio selection and layer rows should keep alpha-provider details as diagnostics while visible copy stays product-facing");
	  }
	  if (!studio_workspace_source.contains(QStringLiteral("function maskSourceProvenanceSummary(data)")) ||
	      !studio_workspace_source.contains(QStringLiteral("function selectionRowFallbackStatus(data, relationText)")) ||
	      !studio_workspace_source.contains(QStringLiteral("Brush selection / ")) ||
	      !studio_workspace_source.contains(QStringLiteral("Path selection / ")) ||
	      !studio_workspace_source.contains(QStringLiteral("Smart selection / ")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceBrushContract: String(modelData.brushAlphaSourceContract || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property int maskSourceBrushStrokeCount: Math.max(0, Number(modelData.brushAlphaStrokeCount || 0))")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceBrushSignature: String(modelData.brushAlphaSourceSignature || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceShapeContract: String(modelData.shapeAlphaSourceContract || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool maskSourceShapeEditable: modelData.shapeAlphaEditable === true")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceShapeSignature: String(modelData.shapeAlphaSourceSignature || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceGeneratedContract: String(modelData.generatedAlphaSourceContract || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property bool maskSourceGeneratedConfidenceAvailable: modelData.generatedAlphaConfidenceAvailable === true")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceGeneratedRefinementSignature: String(modelData.generatedAlphaRefinementStateSignature || \"\")")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceProvenanceSummary: root.maskSourceProvenanceSummary(modelData)")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceProvenanceSummary: photoLayerMaskSetRowRoot.maskSourceProvenanceSummary")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property string maskSourceProvenanceSummary: photoLayerNestedMaskRowRoot.maskSourceProvenanceSummary")) ||
	      studio_workspace_source.contains(QStringLiteral("Rough fallback: editable, not model-exact")) ||
	      studio_workspace_source.contains(QStringLiteral("Alpha provider unavailable")) ||
	      studio_workspace_source.contains(QStringLiteral("Finalize Full Resolution")) ||
	      studio_workspace_source.contains(QStringLiteral("full-resolution retouch output")) ||
	      studio_workspace_source.contains(QStringLiteral("Editing mask")) ||
	      studio_workspace_source.contains(QStringLiteral("Unlinked")) ||
	      studio_workspace_source.contains(QStringLiteral(" / mask attachment")) ||
	      studio_workspace_source.contains(QStringLiteral("text: \"Edit Mask\"")) ||
	      studio_workspace_source.contains(QStringLiteral("title: \"Mask Controls\"")) ||
	      studio_workspace_source.contains(QStringLiteral("title: \"Linked Masks\"")) ||
	      studio_workspace_source.contains(QStringLiteral("text: \"Show Mask\"")) ||
	      studio_workspace_source.contains(QStringLiteral("text: \"Hide Mask\"")) ||
	      studio_workspace_source.contains(QStringLiteral("text: \"Mask\"")) ||
	      studio_workspace_source.contains(QStringLiteral("label: \"Mask\"")) ||
	      studio_workspace_source.contains(QStringLiteral("label: \"Masking\"")) ||
	      studio_workspace_source.contains(QStringLiteral("Full Res")) ||
	      studio_workspace_source.contains(QStringLiteral("return \"Adjustment Layer\"")) ||
	      studio_workspace_source.contains(QStringLiteral("return \"Photo Layer\"")) ||
	      studio_workspace_source.contains(QStringLiteral("label: \"Adjustment Layer\"")) ||
	      studio_workspace_source.contains(QStringLiteral("\"Selection adjustment layer\"")) ||
	      studio_workspace_source.contains(QStringLiteral("\"Select an adjustment layer\"")) ||
	      studio_workspace_source.contains(QStringLiteral("selection-backed adjustment layer")) ||
	      studio_workspace_source.contains(QStringLiteral("selected adjustment layers")) ||
	      studio_workspace_source.contains(QStringLiteral("default adjustment layer")) ||
	      !studio_workspace_source.contains(QStringLiteral("studioLayerTerminologyContract: \"studio-layer-terminology:v1|stack-object:Layer")) ||
	      !studio_workspace_source.contains(QStringLiteral("label: \"Layer\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("tooltipText: \"Add the default layer.\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("? \"Linked Layer\"")) ||
	      studio_workspace_source.contains(QStringLiteral("tooltipText: \"Mask actions.\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("text: \"Edit Selection\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("title: \"Preview & mask strength\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("title: \"Linked Selections\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("label: \"Mask enabled\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("text: \"Affected\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("text: \"Peaking\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("label: \"Edge Limit\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("tooltipText: \"Selection actions.\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("label: \"Density\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("label: \"Opacity\"")) ||
	      studio_workspace_source.contains(QStringLiteral("Duplicate With Masks")) ||
	      studio_workspace_source.contains(QStringLiteral("Duplicate Without Masks"))) {
	    return fail("Studio selection rows should expose diagnostic provenance without leaking implementation vocabulary into visible copy");
	  }
  trace_smoke_progress("copy vocabulary source checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_COPY_VOCABULARY_SOURCE")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");

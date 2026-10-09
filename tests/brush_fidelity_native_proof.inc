  if (arguments.contains("--brush-fidelity-proof")) {
    using namespace photo_editor::desktop;
    using Access = EditorPreviewControllerTestAccess;
    const QString id = "brush-fidelity-native";
    if (option_value(arguments, "--source").isEmpty()) {
      QImage fixture(1600, 1000, QImage::Format_RGB32);
      for (int y = 0; y < fixture.height(); ++y)
        for (int x = 0; x < fixture.width(); ++x)
          fixture.setPixel(x, y, qRgb(50 + x * 140 / fixture.width(),
              55 + y * 130 / fixture.height(), 130));
      if (!fixture.save(source_path)) return fail("Could not write the isolated brush fixture.");
    }
    catalog_assets.replaceAssetsForTesting({make_asset(id, source_path)});
    catalog_assets.selectIndexWithModifiers(0, 0);
    if (!QMetaObject::invokeMethod(root, "openSelectedPhotoInEdit") ||
        !wait_for_condition([&] { return editor_preview.assetId() == id &&
            Access::all_idle(editor_preview) && active_workspace(root); }, timeout_ms))
      return fail("Could not open isolated brush fidelity fixture.");
    auto* workspace = active_workspace(root);
    const bool cropped = arguments.contains("--brush-fidelity-crop");
    if (!QMetaObject::invokeMethod(workspace, "startMaskBrushSource", Q_ARG(QVariant, QVariant("add"))))
      return fail("Could not activate the native brush tool.");
    editor_preview.setActiveMaskBrushSize(0.04);
    editor_preview.setActiveMaskBrushHardness(0.45);
    editor_preview.setActiveMaskFeather(0.55);
    editor_preview.setActiveMaskBrushFlow(0.7);
    editor_preview.setActiveMaskBrushSmoothing(0);
    editor_preview.setActiveMaskBrushAutoMask(false);
    const auto initial_mask = editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap();
    if (initial_mask.value("selectionDespeckle").toDouble() != 0.0 ||
        initial_mask.value("selectionFillHoles").toDouble() != 0.0 ||
        initial_mask.value("selectionRemoveIslands").toDouble() != 0.0)
      return fail("A new hand-painted brush must not silently apply generated-matte cleanup.");
    process_layout_events(5);
    if (!wait_for_condition([&] { return Access::all_idle(editor_preview); }, timeout_ms))
      return fail("Brush setup did not settle.");
    if (cropped) {
      // Activate the adjustment target first: changing the selected layer also
      // switches its recipe controls, including crop, in the current editor.
      editor_preview.setCropPreset("9:16");
      if (!wait_for_condition([&] { return Access::all_idle(editor_preview); }, timeout_ms))
        return fail("The brush target's portrait crop did not settle.");
    }
    root_window->raise();
    root_window->requestActivate();
    process_layout_events(5);
    auto* viewport = find_visible_named_item(workspace, "editImageViewport");
    if (!viewport || viewport->property("paintedContentWidth").toDouble() <= 1.0)
      return fail("The native photo viewport must expose painted geometry.");
    const auto scene_point = [&](double x, double y) {
      return viewport->mapToScene(QPointF(viewport->property("paintedContentX").toDouble() +
          x * viewport->property("paintedContentWidth").toDouble(),
          viewport->property("paintedContentY").toDouble() +
          y * viewport->property("paintedContentHeight").toDouble())).toPoint();
    };
    QJsonArray captures;
    if (cropped && !wait_for_condition([&] {
          return std::abs(viewport->property("paintedContentWidth").toDouble() /
              viewport->property("paintedContentHeight").toDouble() - 9.0 / 16.0) < 0.005;
        }, timeout_ms)) {
      std::cerr << "crop_geometry painted=" << viewport->property("paintedContentWidth").toDouble() << ','
          << viewport->property("paintedContentHeight").toDouble()
          << " expected=" << viewport->property("expectedOutputPixelWidth").toDouble() << ','
          << viewport->property("expectedOutputPixelHeight").toDouble()
          << " native=" << editor_preview.displayPreviewNativeSize().width() << ','
          << editor_preview.displayPreviewNativeSize().height()
          << " crop=" << editor_preview.cropRectWidth() << ',' << editor_preview.cropRectHeight()
          << " failure=" << editor_preview.previewFailureDetail().toStdString() << '\n';
      save_capture_if_requested(root_window, capture_dir, "crop-geometry-failure", &captures);
      return fail("The native cropped photo did not publish its portrait geometry.");
    }
    std::vector<double> input_samples;
    const auto start = scene_point(0.2, 0.45);
    QTest::mouseMove(root_window, start, 20);
    QTest::mousePress(root_window, Qt::LeftButton, Qt::NoModifier, start);
    process_layout_events(1);
    if (!editor_preview.brushStrokeActive()) {
      save_capture_if_requested(root_window, capture_dir, "brush-press-failure", &captures);
      std::cerr << "brush_pointer_start=" << start.x() << ',' << start.y()
          << " viewport=" << viewport->width() << ',' << viewport->height()
          << " painted=" << viewport->property("paintedContentX").toDouble() << ','
          << viewport->property("paintedContentY").toDouble() << ','
          << viewport->property("paintedContentWidth").toDouble() << ','
          << viewport->property("paintedContentHeight").toDouble()
          << " mask=" << editor_preview.activeMaskKind().toStdString()
          << " tool=" << workspace->property("editTool").toString().toStdString()
          << " gesture=" << viewport->property("maskGestureKind").toString().toStdString() << '\n';
      return fail("Native pointer press did not start the brush.");
    }
    for (int i = 1; i <= 40; ++i) {
      const double x = 0.2 + i * 0.015;
      const double y = 0.45 + 0.10 * std::sin(i * 0.15);
      QElapsedTimer timer;
      timer.start();
      QTest::mouseMove(root_window, scene_point(x, y), 1);
      input_samples.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
      process_layout_events(1);
    }
    if (!wait_for_condition([&] { return Access::live_brush_size(editor_preview).isValid(); }, timeout_ms))
      return fail("The canonical live brush preview must arrive while dragging.");
    const auto live_points = Access::live_brush_points(editor_preview);
    const auto live_size = Access::live_brush_size(editor_preview);
    const double painted_aspect = viewport->property("paintedContentWidth").toDouble() /
        viewport->property("paintedContentHeight").toDouble();
    if (std::abs(static_cast<double>(live_size.width()) / live_size.height() - painted_aspect) > 0.005) {
      std::cerr << "brush_aspect raster=" << live_size.width() << ',' << live_size.height()
          << " painted=" << painted_aspect << " failure=" << editor_preview.previewFailureDetail().toStdString() << '\n';
      save_capture_if_requested(root_window, capture_dir, "brush-aspect-failure", &captures);
      return fail("The brush raster must match the displayed photo aspect, including crop.");
    }
    auto live_mask = editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap();
    live_mask.insert("brushStrokes", QVariantList{brush_stroke_variant(live_mask, live_points, 1)});
    const auto expected = mask_with_rebuilt_brush_matte(live_mask, live_size, "native-live-oracle")
        .value("segmentationData").toByteArray();
    if (expected.isEmpty() || !wait_for_condition([&] {
          return Access::live_brush_alpha(editor_preview) == expected; }, timeout_ms))
      return fail("Live native alpha must equal canonical stroke replay before release.");
    save_capture_if_requested(root_window, capture_dir, "soft-brush-live", &captures);
    QTest::mouseRelease(root_window, Qt::LeftButton, Qt::NoModifier, scene_point(0.8, 0.45 + 0.10 * std::sin(6.0)));
    if (!wait_for_condition([&] { return Access::pending_brush_commit_count(editor_preview) == 0 &&
        Access::all_idle(editor_preview); }, timeout_ms)) return fail("Native brush commit did not settle.");
    const auto after_stack = editor_preview.maskStack();
    const auto after = after_stack.at(editor_preview.activeMaskIndex()).toMap();
    const auto samples = after.value("brushStrokes").toList().back().toMap().value("samples").toList();
    if (samples.size() != live_points.size() ||
        mask_with_rebuilt_brush_matte(after, live_size, "native-release-oracle").value("segmentationData").toByteArray() != expected)
      return fail("Releasing the native mouse must preserve exact samples and soft coverage.");
    const QSize committed_size(after.value("segmentationWidth").toInt(), after.value("segmentationHeight").toInt());
    if (mask_with_rebuilt_brush_matte(after, committed_size, "native-committed-oracle").value("segmentationData") != after.value("segmentationData"))
      return fail("Committed alpha must match the live brush kernel.");
    for (const QSize output : {committed_size, cropped ? QSize(1125, 2000) : QSize(3200, 2000)}) {
      const auto pipeline_mask = compile_pipeline_local_mask(after,
          LocalMaskRasterPolicy::RebuildEditableAtOutputResolution, output);
      const auto canonical = mask_with_rebuilt_brush_matte(after, output, "native-output-oracle")
          .value("segmentationData").toByteArray();
      const QByteArray output_alpha(reinterpret_cast<const char*>(pipeline_mask.segmentation_mask.data()),
          static_cast<qsizetype>(pipeline_mask.segmentation_mask.size()));
      if (pipeline_mask.segmentation_width != output.width() ||
          pipeline_mask.segmentation_height != output.height() || output_alpha != canonical)
        return fail("Zoom and full-resolution output must use the exact brush kernel.");
    }
    if (!wait_for_condition([&] {
          return !editor_preview.activeMaskOutlinePaths().isEmpty() &&
              editor_preview.activeMaskOutlineTrace().value("outline_trace_source").toString() == "cache-hit";
        }, timeout_ms)) {
      std::cerr << "brush_outline_failure=" << QJsonDocument(QJsonObject::fromVariantMap(
          editor_preview.activeMaskOutlineTrace())).toJson(QJsonDocument::Compact).constData() << '\n';
      std::cerr << "brush_outline_worker=" << QJsonDocument(QJsonObject::fromVariantMap(
          Access::brush_outline_worker_state(editor_preview))).toJson(QJsonDocument::Compact).constData() << '\n';
      return fail("The final-alpha brush outline must arrive after release.");
    }
    auto* overlay = find_visible_named_item(workspace, "editImageViewportSharedMaskOverlay");
    QVariant mapped;
    if (!overlay || !QMetaObject::invokeMethod(overlay, "viewPointForImage", Q_RETURN_ARG(QVariant, mapped),
        Q_ARG(QVariant, QVariant(0.2)), Q_ARG(QVariant, QVariant(0.45))))
      return fail("The actual mask overlay must expose its coordinate mapping.");
    const auto mapped_point = mapped.value<QJSValue>().toVariant().toMap();
    const auto point = mapped_point.isEmpty() ? mapped.toMap() : mapped_point;
    if (!point.value("valid").toBool() ||
        std::abs(point.value("x").toDouble() / overlay->width() - 0.2) > 0.001 ||
        std::abs(point.value("y").toDouble() / overlay->height() - 0.45) > 0.001)
      return fail("The final brush outline must retain pointer alignment after crop.");
    save_capture_if_requested(root_window, capture_dir, "soft-brush-committed", &captures);
    const auto saved = Access::recipe_json(editor_preview);
    const auto parsed = parse_edit_recipe_json(saved);
    const auto mask_id = after.value("id").toString();
    QVariantMap saved_mask;
    for (const auto& value : parsed.mask_stack)
      if (value.toMap().value("id").toString() == mask_id) saved_mask = value.toMap();
    if (saved_mask.isEmpty() || mask_with_rebuilt_brush_matte(saved_mask, live_size, "saved")
            .value("segmentationData").toByteArray() != expected)
      return fail("Saved brush geometry must round-trip exact feathered coverage.");
    editor_preview.undo();
    if (!wait_for_condition([&] { return Access::all_idle(editor_preview); }, timeout_ms)) return fail("Brush undo did not settle.");
    editor_preview.redo();
    if (!wait_for_condition([&] { return Access::all_idle(editor_preview); }, timeout_ms) ||
        editor_preview.maskStack() != after_stack) return fail("Brush redo must restore exact mask bytes and stroke settings.");
    editor_preview.loadAsset(id, source_path, {}, {}, {}, {}, saved);
    if (!wait_for_condition([&] { return Access::all_idle(editor_preview); }, timeout_ms)) return fail("Reopening the saved brush did not settle.");
    QVariantMap reopened;
    const auto reopened_stack = editor_preview.maskStack();
    for (int index = 0; index < reopened_stack.size(); ++index) {
      if (reopened_stack.at(index).toMap().value("id").toString() != mask_id) continue;
      reopened = reopened_stack.at(index).toMap();
      editor_preview.setActiveMaskIndex(index);
      break;
    }
    if (reopened.isEmpty()) return fail("Reopened brush identity was lost.");
    if (mask_with_rebuilt_brush_matte(reopened, live_size, "reopened").value("segmentationData").toByteArray() != expected)
      return fail("Reopening must preserve the exact feathered mask.");
    viewport->setProperty("zoom", 2.0);
    process_layout_events(5);
    save_capture_if_requested(root_window, capture_dir, "soft-brush-zoomed", &captures);
    log_summary("brush_fidelity_native_pointer", summarize_samples(input_samples), 40);
    std::cout << QJsonDocument(QJsonObject{{"schema", "brush-fidelity:v2"}, {"ok", true},
        {"platform", QGuiApplication::platformName()}, {"native_pointer_events", 42},
        {"live_and_committed_alpha_exact", true}, {"samples_preserved", true},
        {"feather", 0.55}, {"hardness", 0.45}, {"undo_redo", true}, {"reopened", true},
        {"zoom", 2}, {"portrait_crop", cropped}, {"full_resolution_alpha_exact", true}, {"captures", captures}}).toJson().constData();
    return EXIT_SUCCESS;
  }

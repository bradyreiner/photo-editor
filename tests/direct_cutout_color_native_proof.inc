  if (arguments.contains("--direct-cutout-color-proof")) {
    using namespace photo_editor::desktop;
    using Access = EditorPreviewControllerTestAccess;
    catalog_assets.replaceAssetsForTesting({make_asset("direct-cutout-native", source_path)});
    catalog_assets.selectIndexWithModifiers(0, 0);
    if (!QMetaObject::invokeMethod(root, "openSelectedPhotoInEdit") ||
        !wait_for_condition([&] { return editor_preview.assetId() == "direct-cutout-native" &&
            Access::all_idle(editor_preview) && active_workspace(root); }, timeout_ms))
      return fail("Could not open the isolated cutout color fixture.");
    auto* workspace = active_workspace(root);
    const QImage source(source_path);
    const auto settle = [&] {
      return wait_for_condition([&] { return Access::all_idle(editor_preview) &&
          !editor_preview.photoPaintBusy(); }, timeout_ms);
    };
    const auto render = [&] {
      return render_output_image_with_backend(source, parse_edit_recipe_json(Access::recipe_json(editor_preview)),
          QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
    };
    editor_preview.addMask("rectangle");
    editor_preview.setActiveMaskFeather(0);
    editor_preview.setActiveMaskCenterX(0.25);
    editor_preview.setActiveMaskCenterY(0.5);
    editor_preview.setActiveMaskRadiusX(0.25);
    editor_preview.setActiveMaskRadiusY(0.5);
    const auto mask_id = editor_preview.maskSets().front().toMap().value("id").toString();
    QJsonArray captures;
    int typed_edits = 0;
    for (bool independent : {false, true}) {
      if (!editor_preview.selectMaskSet(mask_id) ||
          !editor_preview.copyPixelsFromSelection(-1, independent) || !settle())
        return fail("Create the linked or independent cutout without an adjustment layer.");
      // Mask creation can add its own neutral selection-adjustment fixture.
      // Remove it here to prove that the cutout works without that layer.
      for (int index = editor_preview.photoLayerCount() - 1; index >= 0; --index) {
        const auto layer = editor_preview.photoLayers().value(index).toMap();
        if (layer.value("kind").toString() != "adjustment") continue;
        if (layer.value("hasNonNeutralAdjustments").toBool() || !editor_preview.deletePhotoLayer(index))
          return fail("Could not isolate the cutout from neutral mask-creation adjustments.");
      }
      if (!settle()) return fail("The isolated cutout fixture must settle.");
      QVariant selected;
      if (!QMetaObject::invokeMethod(workspace, "setActivePhotoLayer", Q_RETURN_ARG(QVariant, selected),
              Q_ARG(QVariant, QVariant(0))) || !selected.toBool()) return fail("Select the cutout row.");
      process_layout_events(4);
      auto* adjust = find_named_item(workspace, "adjustInspectorTabButton");
      if (!adjust || !item_visible(adjust)) return fail("The Adjust tab must be visible.");
      QTest::mouseClick(root_window, Qt::LeftButton, Qt::NoModifier,
          adjust->mapToScene(QPointF(adjust->width() / 2, adjust->height() / 2)).toPoint());
      workspace->setProperty("editAdjustmentFilterText", "color");
      process_layout_events(5);
      auto* field = find_named_item(workspace, "saturationSliderValueField");
      if (!field || !item_visible(field) || !field->isEnabled())
        return fail("The selected cutout must expose an enabled Saturation value field.");
      const auto count = editor_preview.photoLayerCount();
      const auto before_recipe = parse_edit_recipe_json(Access::recipe_json(editor_preview));
      const auto before_pixels = render();
      save_capture_if_requested(root_window, capture_dir,
          independent ? "independent-before-color" : "linked-before-color", &captures);
      QTest::mouseClick(root_window, Qt::LeftButton, Qt::NoModifier,
          field->mapToScene(QPointF(field->width() / 2, field->height() / 2)).toPoint());
      QTest::keyClick(root_window, Qt::Key_A, Qt::ControlModifier);
      QTest::keyClick(root_window, Qt::Key_Minus);
      QTest::keyClick(root_window, Qt::Key_8);
      QTest::keyClick(root_window, Qt::Key_0);
      QTest::keyClick(root_window, Qt::Key_Return);
      ++typed_edits;
      if (!settle()) return fail("Direct cutout color edit did not settle.");
      const auto after_recipe = parse_edit_recipe_json(Access::recipe_json(editor_preview));
      const auto after_pixels = render();
      if (editor_preview.photoLayerCount() != count ||
          std::abs(editor_preview.photoLayers().front().toMap().value("saturation").toDouble() + 80) > 0.01 ||
          after_recipe.mask_stack != before_recipe.mask_stack ||
          after_recipe.saturation != before_recipe.saturation || after_pixels.isNull() || after_pixels == before_pixels) {
        save_capture_if_requested(root_window, capture_dir, "color-edit-failure", &captures);
        std::cerr << "count=" << editor_preview.photoLayerCount() << '/' << count
            << " layer_saturation=" << editor_preview.photoLayers().front().toMap().value("saturation").toDouble()
            << " global=" << after_recipe.saturation << '/' << before_recipe.saturation
            << " masks_same=" << (after_recipe.mask_stack == before_recipe.mask_stack)
            << " pixels_same=" << (after_pixels == before_pixels)
            << " field_text=" << field->property("text").toString().toStdString() << '\n';
        return fail("Typing Saturation must change the cutout's own recipe and pixels only.");
      }
      for (int y = 0; y < source.height(); ++y)
        for (int x = source.width() * 3 / 4; x < source.width(); ++x)
          if (after_pixels.pixel(x, y) != before_pixels.pixel(x, y))
            return fail("Color edit changed pixels outside the cutout.");
      for (const auto& layer : after_recipe.layers)
        if (layer.toMap().value("kind").toString() == "adjustment")
          return fail("Direct cutout colors must not create an adjustment layer.");
      save_capture_if_requested(root_window, capture_dir,
          independent ? "independent-after-color" : "linked-after-color", &captures);
      editor_preview.undo();
      if (!settle() || render() != before_pixels) return fail("Undo must restore the cutout pixels.");
      editor_preview.redo();
      if (!settle() || render() != after_pixels) return fail("Redo must restore the direct color edit.");
    }
    std::cout << QJsonDocument(QJsonObject{{"schema", "direct-cutout-color:v1"}, {"ok", true},
        {"platform", QGuiApplication::platformName()}, {"native_typed_edits", typed_edits},
        {"linked_and_independent", true}, {"outside_unchanged", true}, {"masks_unchanged", true},
        {"no_adjustment_layer", true}, {"undo_redo", true}, {"captures", captures}}).toJson().constData();
    return EXIT_SUCCESS;
  }

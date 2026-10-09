      const auto await_pixels = [&] {
        return wait_for_condition([&] {
          return !editor_preview.processing() && !editor_preview.fullRenderRunning() && !editor_preview.photoPaintBusy();
        }, 15000);
      };
      const auto layer_pixels = [&] {
        return photo_editor::desktop::rasterize_photo_paint_layer(editor_preview.photoLayers().at(0).toMap());
      };
      studio_window->resize(1440, 900);
      tool_viewport->setProperty("zoom", 0.75);
      process_layout_events(8);
      paint_overlay->setProperty("brushSize", 96.0);
      paint_overlay->setProperty("hardness", 0.72);
      paint_overlay->setProperty("brushOpacity", 1.0);
      paint_overlay->setProperty("flow", 1.0);
      paint_overlay->setProperty("pressureSize", false);
      paint_overlay->setProperty("clipSelection", false);
      if (!await_pixels()) return fail("Eraser proof requires a ready paint target");
      const auto image_origin = paint_overlay->mapToScene(QPointF(
          paint_overlay->property("imageX").toDouble(), paint_overlay->property("imageY").toDouble()));
      const auto image_midpoint = image_origin + QPointF(
          paint_overlay->property("imageWidth").toDouble() * 0.5,
          paint_overlay->property("imageHeight").toDouble() * 0.5);
      send_paint_pointer(QEvent::MouseButtonPress, image_midpoint);
      send_paint_pointer(QEvent::MouseButtonRelease, image_midpoint);
      if (!await_pixels()) return fail("Native paint must settle before erasing");
      const auto painted_pixels = layer_pixels();
      const QPoint center(paint_overlay->property("canvasWidth").toInt() / 2,
                          paint_overlay->property("canvasHeight").toInt() / 2);
      if (painted_pixels.isNull() || painted_pixels.pixelColor(center).alpha() < 240)
        return fail("The native Paint gesture must deposit visible pixels");
      auto* eraser_rail = find_named_item(studio_root, QStringLiteral("editRailEraserButton"));
      const auto layer_count = editor_preview.photoLayerCount();
      if (!click_studio_item(eraser_rail) || !wait_for_condition([&] {
            return edit->property("editTool").toString() == QStringLiteral("eraser") &&
                   !edit->property("editToolTransitionActive").toBool();
          }, 5000)) return fail("The dedicated Eraser icon should activate its tool");
      process_layout_events(8);
      auto* material = find_named_item(studio_root, QStringLiteral("photoPaintMaterialChooser"));
      if (!paint_overlay->property("eraserTool").toBool() || !paint_overlay->property("erase").toBool() ||
          !item_visible(paint_controls) || item_visible(paint_color) || item_visible(material) ||
          item_visible(paint_mode) || editor_preview.photoLayerCount() != layer_count ||
          editor_preview.photoLayers().at(0).toMap().value("id").toString() != paint_layer_id)
        return fail("Eraser should expose its settings and keep the existing pixel target");
      paint_overlay->setProperty("brushSize", 32.0);
      paint_overlay->setProperty("hardness", 1.0);
      paint_overlay->setProperty("pressureSize", false);
      send_paint_pointer(QEvent::MouseButtonPress, image_midpoint);
      send_paint_pointer(QEvent::MouseButtonRelease, image_midpoint);
      if (!await_pixels()) return fail("Native Eraser should finish without render errors");
      const auto erased_pixels = layer_pixels();
      if (erased_pixels.pixelColor(center).alpha() >= painted_pixels.pixelColor(center).alpha() ||
          erased_pixels.pixelColor(center + QPoint(30, 0)) != painted_pixels.pixelColor(center + QPoint(30, 0)))
        return fail("The native Eraser gesture should remove only pixels inside its brush");
      editor_preview.undo();
      if (!await_pixels() || layer_pixels() != painted_pixels) return fail("Undo should restore erased pixels exactly");
      editor_preview.redo();
      if (!await_pixels() || layer_pixels() != erased_pixels) return fail("Redo should reproduce erased pixels exactly");
      if (!capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("eraser-settings"), 1440, 900,
            {QStringLiteral("editRailEraserButton"), QStringLiteral("photoPaintControls"), QStringLiteral("photoPaintSize")},
            &studio_manifest)) return fail("Capture the dedicated Eraser settings");
      if (!click_studio_item(find_named_item(studio_root, QStringLiteral("editRailPaintButton"))) || !wait_for_condition([&] {
            return !edit->property("editToolTransitionActive").toBool() && !paint_overlay->property("eraserTool").toBool();
          }, 5000) || std::abs(paint_overlay->property("brushSize").toDouble() - 96) > 0.01 ||
          std::abs(paint_overlay->property("hardness").toDouble() - 0.72) > 0.01)
        return fail("Paint should retain its settings after Eraser use");
      if (!click_studio_item(find_named_item(studio_root, QStringLiteral("editRailEraserButton"))) || !wait_for_condition([&] {
            return !edit->property("editToolTransitionActive").toBool() && paint_overlay->property("eraserTool").toBool();
          }, 5000) || std::abs(paint_overlay->property("brushSize").toDouble() - 32) > 0.01 ||
          std::abs(paint_overlay->property("hardness").toDouble() - 1) > 0.01)
        return fail("Eraser should remember its independent settings");

      const auto width = paint_overlay->property("canvasWidth").toInt();
      const auto height = paint_overlay->property("canvasHeight").toInt();
      if (!editor_preview.addPhotoShape(QStringLiteral("rounded-rectangle"), width / 5, height / 5,
            width * 4 / 5, height * 4 / 5, width, height, QStringLiteral("#4879ff"), QStringLiteral("#ffffff"), 3))
        return fail("Create a shape for native fill controls");
      QVariant selected;
      if (!QMetaObject::invokeMethod(edit, "setActivePhotoLayerSelection", Q_RETURN_ARG(QVariant, selected),
            Q_ARG(QVariant, QVariant(0)), Q_ARG(QVariant, QVariant(false))) || !selected.toBool() ||
          !invoke_edit_tool(QStringLiteral("layers")) || !await_pixels())
        return fail("Show the selected shape's properties");
      const auto definition = [&] {
        return editor_preview.photoLayers().at(0).toMap().value("shapeDefinition").toMap();
      };
      const auto choose_combo = [&](QQuickItem* combo, int target_index) {
        if (!click_studio_item(combo)) return false;
        auto* popup = qvariant_cast<QObject*>(combo->property("popup"));
        if (!popup || !wait_for_condition([&] { return popup->property("opened").toBool(); }, 3000)) return false;
        process_layout_events(8);
        auto* marker = find_named_item(studio_root,
            combo->objectName() + QStringLiteral("SelectedMarker%1").arg(target_index));
        if (!marker || !click_studio_item(marker->parentItem())) return false;
        return wait_for_condition([&] {
          return !popup->property("visible").toBool() && combo->property("currentIndex").toInt() == target_index;
        }, 3000);
      };
      auto* fill_mode = find_named_item(studio_root, QStringLiteral("photoShapeFillMode"));
      if (!choose_combo(fill_mode, 2) || !wait_for_condition([&] {
            return definition().value("fillMode").toString() == QStringLiteral("linear");
          }, 3000)) return fail("The visible fill chooser should select a linear gradient");
      if (!editor_preview.setPhotoLayerShapeFill(0, {{"gradientStartColor", "#386de8"}, {"gradientEndColor", "#fdcb69"}}) ||
          !await_pixels()) return fail("Gradient controls should render their colors");
      auto* angle = find_named_item(studio_root, QStringLiteral("photoShapeGradientAngle"));
      const auto before_angle = editor_preview.photoLayers().at(0).toMap().value("pixelData").toByteArray();
      if (!item_visible(angle)) return fail("Linear gradients should expose angle settings");
      const auto bounds = scene_rect(angle);
      const auto start = QPointF(bounds.center().x(), bounds.bottom() - 8);
      const auto end = QPointF(bounds.right() - 15, start.y());
      send_paint_pointer(QEvent::MouseButtonPress, start);
      for (int step = 1; step <= 5; ++step) send_paint_pointer(QEvent::MouseMove, start + (end - start) * (step / 5.0));
      send_paint_pointer(QEvent::MouseButtonRelease, end);
      if (!await_pixels() || std::abs(definition().value("gradientAngle").toDouble()) < 10 ||
          editor_preview.photoLayers().at(0).toMap().value("pixelData").toByteArray() == before_angle)
        return fail("Dragging the actual angle slider should update gradient pixels");
      const auto after_angle = editor_preview.photoLayers().at(0).toMap().value("pixelData").toByteArray();
      editor_preview.undo();
      if (!await_pixels() || editor_preview.photoLayers().at(0).toMap().value("pixelData").toByteArray() != before_angle)
        return fail("One undo should restore the gradient before the slider gesture");
      editor_preview.redo();
      if (!await_pixels() || editor_preview.photoLayers().at(0).toMap().value("pixelData").toByteArray() != after_angle)
        return fail("Redo should restore the final gradient slider value");
      if (!capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("shape-linear-gradient"), 1440, 900,
            {QStringLiteral("photoShapeFillMode"), QStringLiteral("photoShapeGradientStart"), QStringLiteral("photoShapeGradientAngle")},
            &studio_manifest)) return fail("Capture native gradient settings");
      if (!choose_combo(fill_mode, 3) || !wait_for_condition([&] {
            return definition().value("fillMode").toString() == QStringLiteral("radial");
          }, 3000) || !await_pixels() ||
          !capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("shape-radial-gradient"), 1440, 900,
            {QStringLiteral("photoShapeGradientRadius"), QStringLiteral("photoShapeGradientCenterX")}, &studio_manifest))
        return fail("Radial gradients should expose their radius and center settings");
      if (!choose_combo(fill_mode, 4) || !wait_for_condition([&] {
            return definition().value("fillMode").toString() == QStringLiteral("texture");
          }, 3000)) return fail("The visible fill chooser should select a texture");
      process_layout_events(8);
      auto* texture_type = find_named_item(studio_root, QStringLiteral("photoShapeTextureType"));
      if (!choose_combo(texture_type, 2) || !wait_for_condition([&] {
            return definition().value("textureType").toString() == QStringLiteral("hatch");
          }, 3000)) {
        studio_window->grabWindow().save(QDir(proof_dir).filePath(QStringLiteral("texture-chooser-failure.png")));
        std::cerr << "Texture chooser: visible=" << item_visible(texture_type)
                  << " index=" << (texture_type ? texture_type->property("currentIndex").toInt() : -1)
                  << " definition=" << QJsonDocument::fromVariant(definition()).toJson(QJsonDocument::Compact).toStdString() << '\n';
        return fail("The visible texture chooser should select Hatch");
      }
      if (!await_pixels() || !capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("shape-texture"), 1440, 900,
            {QStringLiteral("photoShapeTextureType"), QStringLiteral("photoShapeTextureImport"), QStringLiteral("photoShapeTextureStrength")},
            &studio_manifest)) return fail("The visible texture controls should render a hatch fill");
      QImage texture_image(64, 64, QImage::Format_ARGB32);
      for (int y = 0; y < texture_image.height(); ++y) for (int x = 0; x < texture_image.width(); ++x)
        texture_image.setPixelColor(x, y, (x / 16 + y / 16) % 2 ? QColor("#fdcb69") : QColor("#386de8"));
      const auto texture_path = runtime_root.filePath(QStringLiteral("checker-texture.png"));
      if (!texture_image.save(texture_path) ||
          !editor_preview.setPhotoLayerShapeTexture(0, QUrl::fromLocalFile(texture_path).toString()) || !await_pixels() ||
          !capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("shape-image-texture"), 1440, 900,
            {QStringLiteral("photoShapeTextureType"), QStringLiteral("photoShapeTextureScale")}, &studio_manifest))
        return fail("Imported image textures should update the native shape and its controls");
      if (!editor_preview.setPhotoLayerLocked(0, true)) return fail("Lock the shape fixture");
      process_layout_events(8);
      fill_mode = find_named_item(studio_root, QStringLiteral("photoShapeFillMode"));
      if (!fill_mode || fill_mode->property("enabled").toBool()) return fail("Locked shapes should disable fill controls");
      QFile receipt(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
      if (!receipt.open(QIODevice::WriteOnly) || receipt.write(QJsonDocument(studio_manifest).toJson(QJsonDocument::Indented)) <= 0)
        return fail("Save the native Eraser and shape-fill evidence");
      std::cout << "Native Eraser and shape fills: pixels, independent settings, undo/redo, gradient slider and texture chooser passed.\n";
      return EXIT_SUCCESS;

    {
      // A real canvas gesture must publish painted pixels while the button is
      // still held. A control value or a moving cursor alone is not live paint.
      if (!QMetaObject::invokeMethod(edit, "setInterfaceDensity", Q_ARG(QVariant, QVariant(QStringLiteral("balanced")))))
        return fail("Paint interactions should use the balanced studio layout");
      studio_window->resize(1440, 900);
      process_layout_events(8);
      const auto original_zoom = tool_viewport->property("zoom");
      // Exercise the native canvas at fit scale, where it owns the visible
      // pixels rather than the pixel-inspection provider fallback.
      if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_TEXTURE")) {
        tool_viewport->setProperty("zoom", 0.5);
        process_layout_events(8);
        if (!editor_preview.displayPreviewNativeAvailable())
          return fail("The fit-scale paint proof requires a real native texture");
      }
      auto* material_chooser = find_named_item(studio_root, QStringLiteral("photoPaintMaterialChooser"));
      auto* material_popup = find_named_object(studio_root, QStringLiteral("photoPaintMaterialPopup"));
      auto* paint_cursor = find_named_item(studio_root, QStringLiteral("photoPaintCursor"));
      if (!material_chooser || !material_popup || !paint_cursor)
        return fail("Paint should expose a visible material chooser and precision cursor");
      paint_overlay->setProperty("brushSize", 48.0);
      paint_overlay->setProperty("hardness", 0.7);
      paint_overlay->setProperty("brushOpacity", 1.0);
      paint_overlay->setProperty("flow", 1.0);
      paint_overlay->setProperty("ink", QColor(QStringLiteral("#f7d49b")));
      paint_overlay->setProperty("pressureSize", false);
      {
        const bool native_hover = QGuiApplication::platformName() == QStringLiteral("cocoa");
        const auto original_pointer = QCursor::pos();
        const auto restore_pointer = qScopeGuard([&] { if (native_hover) QCursor::setPos(original_pointer); });
        const auto hover_origin = paint_overlay->mapToScene(QPointF(
            paint_overlay->property("imageX").toDouble(), paint_overlay->property("imageY").toDouble()));
        const auto hover_position = hover_origin + QPointF(
            paint_overlay->property("imageWidth").toDouble() * 0.62,
            paint_overlay->property("imageHeight").toDouble() * 0.75);
        const auto send_hover = [&](const QPointF& position) {
          if (native_hover) QCursor::setPos(studio_window->mapToGlobal(position.toPoint()));
          QEnterEvent enter(position, position,
              QPointF(studio_window->mapToGlobal(position.toPoint())));
          QCoreApplication::sendEvent(studio_window, &enter);
          QMouseEvent event(QEvent::MouseMove, position,
              QPointF(studio_window->mapToGlobal(position.toPoint())), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
          QCoreApplication::sendEvent(studio_window, &event);
          process_layout_events(2);
        };
        send_hover(hover_origin - QPointF(8, 8));
        send_hover(hover_position);
        if (!paint_cursor->isVisible() || paint_overlay->property("drawing").toBool() ||
            !capture_visual_proof(studio_window, studio_root, proof_dir,
                QStringLiteral("brush-hover-studio"), 1440, 900,
                {QStringLiteral("photoPaintCursor")}, &studio_manifest))
          return fail("The actual studio must show a brush outline on no-button hover before painting");
        send_hover(hover_origin - QPointF(8, 8));
        if (paint_cursor->isVisible())
          return fail("The studio brush outline should disappear outside the image");
      }
      process_layout_events(3);
      const QStringList material_keys{QStringLiteral("solid"), QStringLiteral("dry_ink"),
                                      QStringLiteral("graphite"), QStringLiteral("light_paint")};
      for (int material_index = 0; material_index < material_keys.size(); ++material_index) {
        if (!click_studio_item(material_chooser) ||
            !wait_for_condition([&] { return material_popup->property("opened").toBool(); }, 3000))
          return fail("The visible brush chooser should open from a native pointer click");
        auto* material_option = find_named_item(studio_root,
            QStringLiteral("photoPaintMaterialOption%1").arg(material_index));
        if (material_index == 0 && !capture_visual_proof(studio_window, studio_root, proof_dir,
              QStringLiteral("brush-material-picker"), 1440, 900,
              {QStringLiteral("photoPaintMaterialChooser"), QStringLiteral("photoPaintMaterialOption0"),
               QStringLiteral("photoPaintMaterialOption4")}, &studio_manifest))
          return fail("The visual material chooser should render every brush choice");
        if (!material_option || !click_studio_item(material_option) ||
            !wait_for_condition([&] { return !material_popup->property("visible").toBool(); }, 3000) ||
            paint_overlay->property("textureMaterial").toString() != material_keys.at(material_index))
          return fail("A visual brush choice should select its real paint material");
        if (!wait_for_condition([&] {
              return !editor_preview.processing() && !editor_preview.fullRenderRunning() && !editor_preview.photoPaintBusy();
            }, 15000))
          return fail("Painting should begin after the preceding preview is ready");
        const auto baseline_image = editor_preview_image_provider->image(QStringLiteral("edited"));
        if (baseline_image.isNull()) return fail("Live paint proof requires decoded preview pixels");
        const auto image_origin = paint_overlay->mapToScene(QPointF(
            paint_overlay->property("imageX").toDouble(), paint_overlay->property("imageY").toDouble()));
        const auto image_width = paint_overlay->property("imageWidth").toDouble();
        const auto image_height = paint_overlay->property("imageHeight").toDouble();
        const QPointF start = image_origin + QPointF(image_width * 0.26, image_height * 0.60);
        const QPointF end = start + QPointF(image_width * 0.40, image_height * 0.08);
        const QPointF stroke_midpoint = start + (end - start) * 0.5;
        const auto baseline_window = studio_window->grabWindow();
        const auto window_pixel = [&](const QImage& image, const QPointF& scene_point) {
          return image.pixelColor(qRound(scene_point.x() * image.width() / studio_window->width()),
                                  qRound(scene_point.y() * image.height() / studio_window->height()));
        };
        const auto baseline_midpoint = window_pixel(baseline_window, stroke_midpoint);
        const auto native_revision_before = editor_preview.displayPreviewNativeRevision();
        const bool native_canvas = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_TEXTURE");
        QElapsedTimer first_pixel_timer;
        first_pixel_timer.start();
        send_paint_pointer(QEvent::MouseButtonPress, start);
        if (!paint_overlay->property("drawing").toBool() || !wait_for_condition([&] {
              const auto draft = editor_preview_image_provider->image(QStringLiteral("edited"));
              return native_canvas ? editor_preview.displayPreviewNativeRevision() != native_revision_before
                                   : !draft.isNull() && draft != baseline_image;
            }, 5000)) {
          studio_window->grabWindow().save(QDir(proof_dir).filePath(QStringLiteral("paint-press-failure.png")));
          std::cerr << "Paint press: drawing=" << paint_overlay->property("drawing").toBool()
                    << " editable=" << paint_overlay->property("editable").toBool()
                    << " active=" << paint_overlay->property("active").toBool()
                    << " point=" << start.x() << ',' << start.y()
                    << " image=" << image_origin.x() << ',' << image_origin.y() << ',' << image_width << ',' << image_height
                    << " error=" << editor_preview.photoPaintError().toStdString() << '\n';
          return fail("A held brush press should publish actual painted pixels before release");
        }
        const auto first_pixel_ms = first_pixel_timer.nsecsElapsed() / 1e6;
        const auto first_draft = editor_preview_image_provider->image(QStringLiteral("edited"));
        const auto first_native_revision = editor_preview.displayPreviewNativeRevision();
        const auto quick_control_bounds = scene_rect(paint_size);
        for (int step = 1; step <= 12; ++step) {
          const auto t = static_cast<double>(step) / 12.0;
          send_paint_pointer(QEvent::MouseMove,
              start + (end - start) * t + QPointF(0, std::sin(t * 6.283185307179586) * image_height * 0.06));
        }
        const auto end_preview_x = qRound(0.66 * (baseline_image.width() - 1));
        const auto end_preview_y = qRound(0.68 * (baseline_image.height() - 1));
        if (!paint_overlay->property("drawing").toBool() || !wait_for_condition([&] {
              const auto draft = editor_preview_image_provider->image(QStringLiteral("edited"));
              if (native_canvas) return editor_preview.displayPreviewNativeRevision() != first_native_revision;
              return !draft.isNull() && draft != first_draft &&
                  draft.pixelColor(end_preview_x, end_preview_y) != baseline_image.pixelColor(end_preview_x, end_preview_y);
            }, 5000) || !paint_cursor->isVisible())
          return fail("Dragging a held brush should render through the latest pointer position before release");
        if (QLineF(quick_control_bounds.center(), scene_rect(paint_size).center()).length() > 1.0)
          return fail("Painting activity should not move the brush controls under the user's pointer");
        QElapsedTimer presentation_timer;
        presentation_timer.start();
        if (!wait_for_condition([&] {
              const auto frame = studio_window->grabWindow();
              if (frame.isNull()) return false;
              const auto actual = window_pixel(frame, stroke_midpoint);
              const auto delta = std::abs(actual.red() - baseline_midpoint.red()) +
                                 std::abs(actual.green() - baseline_midpoint.green()) +
                                 std::abs(actual.blue() - baseline_midpoint.blue());
              return delta > 24;
            }, 5000)) {
          studio_window->grabWindow().save(QDir(proof_dir).filePath(QStringLiteral("paint-presentation-failure.png")));
          std::cerr << "Paint presentation: nativeBefore=" << native_revision_before
                    << " nativeNow=" << editor_preview.displayPreviewNativeRevision()
                    << " nativeAvailable=" << editor_preview.displayPreviewNativeAvailable()
                    << " nativeSize=" << editor_preview.displayPreviewNativeSize().width() << 'x'
                    << editor_preview.displayPreviewNativeSize().height() << '\n';
          return fail("The displayed canvas must show the middle of the held stroke, not just updated provider pixels");
        }
        const auto presentation_after_decoded_ms = presentation_timer.nsecsElapsed() / 1e6;
        if (!capture_visual_proof(studio_window, studio_root, proof_dir,
              QStringLiteral("paint-live-%1").arg(material_keys.at(material_index)), 1440, 900,
              {QStringLiteral("photoPaintMaterialChooser"), QStringLiteral("photoPaintCursor")}, &studio_manifest))
          return fail("Each material should show its actual live stroke inside the studio");
        send_paint_pointer(QEvent::MouseButtonRelease, end);
        if (!wait_for_condition([&] {
              return !editor_preview.photoPaintBusy() && !editor_preview.processing() && !editor_preview.fullRenderRunning();
            }, 15000) || !editor_preview.photoPaintError().isEmpty() ||
            editor_preview.photoLayers().at(0).toMap().value(QStringLiteral("paintTiles")).toMap()
                .value(QStringLiteral("tiles")).toMap().isEmpty())
          return fail("A released live stroke should commit actual pixel tiles without errors");
        creative_interaction_metrics.append(QJsonObject{
            {QStringLiteral("interaction"), QStringLiteral("paint")},
            {QStringLiteral("material"), material_keys.at(material_index)},
            {QStringLiteral("first_changed_preview_ms"), first_pixel_ms},
            {QStringLiteral("preview_route"), native_canvas ? QStringLiteral("metal") : QStringLiteral("image_provider")},
            {QStringLiteral("displayed_canvas_caught_up_after_decoded_ms"), presentation_after_decoded_ms},
            {QStringLiteral("native_revision_before"), native_revision_before},
            {QStringLiteral("native_revision_after"), editor_preview.displayPreviewNativeRevision()},
            {QStringLiteral("native_texture_available"), editor_preview.displayPreviewNativeAvailable()},
            {QStringLiteral("displayed_midstroke_pixels_changed_before_release"), true},
            {QStringLiteral("pixels_changed_before_release"), true},
            {QStringLiteral("document_width"), editor_preview.sourceImageWidth()},
            {QStringLiteral("document_height"), editor_preview.sourceImageHeight()}});
        editor_preview.undo();
        if (!wait_for_condition([&] {
              if (editor_preview.photoPaintBusy() || editor_preview.processing() || editor_preview.fullRenderRunning()) return false;
              if (native_canvas) {
                const auto restored = window_pixel(studio_window->grabWindow(), stroke_midpoint);
                return std::abs(restored.red() - baseline_midpoint.red()) +
                       std::abs(restored.green() - baseline_midpoint.green()) +
                       std::abs(restored.blue() - baseline_midpoint.blue()) <= 6;
              }
              return !editor_preview.photoPaintBusy() && !editor_preview.processing() &&
                  editor_preview_image_provider->image(QStringLiteral("edited")) == baseline_image;
            }, 15000))
          return fail("One undo should restore the pixels before the whole brush gesture");
      }
      // The pressure ring follows the same diameter multiplier as the painter;
      // this checks the cursor logic, not physical tablet hardware feel.
      paint_overlay->setProperty("pressureSize", true);
      paint_overlay->setProperty("tabletDrawing", true);
      paint_overlay->setProperty("cursorPressure", 0.4);
      process_layout_events(2);
      const auto pressure_width = paint_overlay->property("nominalCursorWidth").toDouble() * 0.4;
      if (std::abs(paint_cursor->width() - pressure_width) > 1.0)
        return fail("The brush cursor should reflect pressure-sensitive diameter");
      paint_overlay->setProperty("panActive", true);
      process_layout_events(2);
      if (paint_cursor->isVisible()) return fail("The brush cursor should yield to canvas panning");
      paint_overlay->setProperty("panActive", false);
      paint_overlay->setProperty("tabletDrawing", false);
      paint_overlay->setProperty("cursorPressure", 1.0);
      paint_overlay->setProperty("textureMaterial", QStringLiteral("solid"));
      paint_overlay->setProperty("brushSize", initial_brush_size);
      paint_overlay->setProperty("ink", QColor(Qt::white));
      tool_viewport->setProperty("zoom", original_zoom);
    }

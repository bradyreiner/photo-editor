    {
      // Exercise the real Edit canvas with a semantic alpha matte whose outer
      // contour, small hole, and detached island must all survive tracing.
      editor_preview.addMask(QStringLiteral("object"));
      const auto active_mask_index = editor_preview.activeMaskIndex();
      const auto mask_stack = editor_preview.maskStack();
      if (active_mask_index < 0 || active_mask_index >= mask_stack.size())
        return fail("Outline visual proof should create an active semantic mask");
      auto fixture_mask = mask_stack.at(active_mask_index).toMap();
      constexpr int matte_width = 192;
      constexpr int matte_height = 144;
      QByteArray alpha(matte_width * matte_height, '\0');
      const auto radial_coverage = [&](const double x, const double y,
                                       const double center_x, const double center_y,
                                       const double radius) {
        const auto dx = (x - center_x) / radius;
        const auto dy = (y - center_y) / radius;
        const auto radial_distance = std::sqrt(dx * dx + dy * dy);
        const auto radial_pixel_scale = radius * static_cast<double>(matte_height - 1);
        return (1.0 - radial_distance) * radial_pixel_scale;
      };
      for (int y = 0; y < matte_height; ++y) {
        const auto normalized_y = static_cast<double>(y) / static_cast<double>(matte_height - 1);
        for (int x = 0; x < matte_width; ++x) {
          const auto normalized_x = static_cast<double>(x) / static_cast<double>(matte_width - 1);
          const auto distance_from_center = std::hypot(normalized_x - 0.48, normalized_y - 0.50);
          const auto ring_coverage = std::min(
              radial_coverage(normalized_x, normalized_y, 0.48, 0.50, 0.34),
              (distance_from_center - 0.045) * static_cast<double>(matte_height - 1));
          const auto island_coverage = radial_coverage(normalized_x, normalized_y, 0.90, 0.70, 0.035);
          const auto coverage = std::max(ring_coverage, island_coverage);
          alpha[y * matte_width + x] = static_cast<char>(qBound(
              0, qRound(127.5 + 100.0 * coverage), 255));
        }
      }
      fixture_mask.insert(QStringLiteral("kind"), QStringLiteral("object"));
      fixture_mask.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
      fixture_mask.insert(QStringLiteral("segmentationData"), alpha);
      fixture_mask.insert(QStringLiteral("segmentationWidth"), matte_width);
      fixture_mask.insert(QStringLiteral("segmentationHeight"), matte_height);
      fixture_mask.insert(QStringLiteral("pixelSelection"), false);
      fixture_mask.insert(QStringLiteral("enabled"), true);
      fixture_mask.insert(QStringLiteral("committed"), true);
      fixture_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
      for (const auto* key : {"feather", "selectionExpand", "selectionSmooth", "selectionFineEdge",
                             "selectionEdgeContrast", "selectionDecontaminate", "maskSetSelectionExpand",
                             "maskSetSelectionSmooth", "maskSetSelectionFineEdge", "maskSetSelectionEdgeContrast"})
        fixture_mask.insert(QString::fromLatin1(key), 0.0);
      for (const auto* key : {"opacity", "density", "maskSetOpacity", "maskSetDensity"})
        fixture_mask.insert(QString::fromLatin1(key), 1.0);
      for (const auto* key : {"invert", "selectionInvert", "maskSetSelectionInvert"})
        fixture_mask.insert(QString::fromLatin1(key), false);
      if (!photo_editor::desktop::EditorPreviewControllerTestAccess::
              set_active_mask_definition(editor_preview, fixture_mask)) {
        return fail("Outline visual proof should install a semantic raster fixture");
      }
      editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
      edit->setProperty("editTool", QStringLiteral("masks"));
      edit->setProperty("editInspectorTab", QStringLiteral("adjust"));
      auto* outline_viewport = find_named_item(edit, QStringLiteral("editImageViewport"));
      auto* outline_overlay = find_named_item(edit, QStringLiteral("editImageViewportSharedMaskOverlay"));
      if (!outline_viewport || !outline_overlay)
        return fail("Outline visual proof should find the rendered Edit mask overlay");
      outline_viewport->setProperty("zoom", 1.0);
      outline_viewport->setProperty("panX", 0.0);
      outline_viewport->setProperty("panY", 0.0);
      studio_root->setProperty("width", 1440);
      studio_root->setProperty("height", 900);
      studio_window->grabWindow();
      const auto wait_for_outline = [&]() {
        return wait_for_condition([&]() {
          QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
          return editor_preview.activeMaskOutlinePaths().size() >= 3 &&
                 item_visible(outline_overlay) && outline_overlay->width() > 80.0 &&
                 outline_overlay->height() > 80.0;
        }, 6000);
      };
      if (!wait_for_outline()) {
        std::cerr << "Outline fixture paths=" << editor_preview.activeMaskOutlinePaths().size()
                  << " mask=" << editor_preview.hasActiveMask()
                  << " visible=" << item_visible(outline_overlay)
                  << " geometry=" << outline_overlay->width() << 'x' << outline_overlay->height()
                  << " tool=" << edit->property("editTool").toString().toStdString()
                  << " source=" << editor_preview.activeMaskOutlineTrace().value(QStringLiteral("outline_trace_source")).toString().toStdString() << '\n';
        return fail("Outline visual proof should trace the ring, hole, and island contours");
      }

      const auto frame = [&]() {
        studio_window->requestUpdate();
        return studio_window->grabWindow();
      };
      const auto save_frame = [&](const QString& name, const QImage& image) {
        return !image.isNull() && image.save(QDir(proof_dir).filePath(name));
      };
      const auto screen_pixel = [&](const QImage& image, const double normalized_x,
                                    const double normalized_y) {
        const auto scene_point = outline_overlay->mapToScene(QPointF(
            normalized_x * outline_overlay->width(), normalized_y * outline_overlay->height()));
        const auto scale = studio_window->devicePixelRatio();
        const auto x = qBound(0, qRound(scene_point.x() * scale), image.width() - 1);
        const auto y = qBound(0, qRound(scene_point.y() * scale), image.height() - 1);
        return image.pixelColor(x, y);
      };
      const auto color_distance = [](const QColor& first, const QColor& second) {
        return std::abs(first.red() - second.red()) + std::abs(first.green() - second.green()) +
               std::abs(first.blue() - second.blue());
      };
      const auto difference_pixels = [&](const QImage& first, const QImage& second) {
        if (first.size() != second.size() || first.isNull()) return -1;
        const auto bounds = scene_rect(outline_viewport);
        const auto ratio = studio_window->devicePixelRatio();
        const int left = qBound(0, qFloor(bounds.left() * ratio), first.width());
        const int top = qBound(0, qFloor(bounds.top() * ratio), first.height());
        const int right = qBound(left, qCeil(bounds.right() * ratio), first.width());
        const int bottom = qBound(top, qCeil(bounds.bottom() * ratio), first.height());
        auto changed = 0;
        for (int y = top; y < bottom; y += 2) {
          for (int x = left; x < right; x += 2) {
            if (first.pixelColor(x, y) != second.pixelColor(x, y)) ++changed;
          }
        }
        return changed;
      };

      // Edit outlines should be visible without selection fill. Layers should
      // add an odd-even fill so the ring's hole remains transparent.
      editor_preview.setMaskPreviewMode(QStringLiteral("outline"));
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      const auto edit_outline_frame = frame();
      if (!save_frame(QStringLiteral("mask-outline-edit-fit.png"), edit_outline_frame))
        return fail("Outline visual proof should capture the actual Edit outline");

      // MaskView is the application's dedicated mask/layers canvas surface.
      // Render that QML view in the same live engine and inspect its shared
      // overlay with the exact same semantic matte.
      QQmlComponent mask_workspace_component(
          &engine,
          QUrl::fromLocalFile(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                                  .filePath(QStringLiteral("apps/desktop/qml/workspaces/MaskView.qml"))));
      if (mask_workspace_component.isError()) {
        for (const auto& error : mask_workspace_component.errors())
          std::cerr << error.toString().toStdString() << '\n';
        return fail("Outline visual proof should load the actual MaskView QML surface");
      }
      auto* mask_workspace = qobject_cast<QQuickItem*>(mask_workspace_component.create(engine.rootContext()));
      if (!mask_workspace)
        return fail("Outline visual proof should instantiate the actual MaskView canvas");
      mask_workspace->setParentItem(studio_window->contentItem());
      mask_workspace->setX(0.0);
      mask_workspace->setY(0.0);
      mask_workspace->setWidth(std::min(1024, studio_window->width()));
      mask_workspace->setHeight(std::min(820, studio_window->height()));
      mask_workspace->setVisible(true);
      editor_preview.setMaskPreviewMode(QStringLiteral("outline"));
      auto* layers_shared_overlay = find_named_item(mask_workspace, QStringLiteral("maskViewSharedMaskOverlay"));
      if (!wait_for_condition([&]() {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            layers_shared_overlay = find_named_item(mask_workspace, QStringLiteral("maskViewSharedMaskOverlay"));
            return layers_shared_overlay && item_visible(layers_shared_overlay) &&
                   layers_shared_overlay->width() > 120.0 && layers_shared_overlay->height() > 80.0;
          }, 6000)) {
        delete mask_workspace;
        return fail("The native MaskView should render the active semantic outline");
      }
      editor_preview.setMaskPreviewMode(QStringLiteral("hidden"));
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
      const auto mask_workspace_hidden_frame = frame();
      editor_preview.setMaskPreviewMode(QStringLiteral("outline"));
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
      const auto mask_workspace_frame = frame();
      const auto overlay_origin = layers_shared_overlay->mapToScene(QPointF(0.0, 0.0));
      const auto device_scale = studio_window->devicePixelRatio();
      const auto overlay_left = qBound(0, qFloor(overlay_origin.x() * device_scale), mask_workspace_frame.width());
      const auto overlay_top = qBound(0, qFloor(overlay_origin.y() * device_scale), mask_workspace_frame.height());
      const auto overlay_right = qBound(overlay_left, qCeil((overlay_origin.x() + layers_shared_overlay->width()) * device_scale),
                                        mask_workspace_frame.width());
      const auto overlay_bottom = qBound(overlay_top, qCeil((overlay_origin.y() + layers_shared_overlay->height()) * device_scale),
                                         mask_workspace_frame.height());
      auto workspace_outline_pixels = 0;
      for (int y = overlay_top; y < overlay_bottom; y += 2) {
        for (int x = overlay_left; x < overlay_right; x += 2) {
          if (mask_workspace_frame.pixelColor(x, y) != mask_workspace_hidden_frame.pixelColor(x, y))
            ++workspace_outline_pixels;
        }
      }
      if (!save_frame(QStringLiteral("mask-outline-layers-workspace.png"), mask_workspace_frame) ||
          workspace_outline_pixels < 30) {
        delete mask_workspace;
        return fail("The actual Layers workspace should render its own shared selection outline");
      }
      delete mask_workspace;
      edit->setProperty("editInspectorTab", QStringLiteral("layers"));
      editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      const auto layers_fit_frame = frame();
      if (!save_frame(QStringLiteral("mask-outline-layers-fit.png"), layers_fit_frame))
        return fail("Outline visual proof should capture the rendered Layers fill");
      const auto body_fill_delta = color_distance(
          screen_pixel(edit_outline_frame, 0.48, 0.34),
          screen_pixel(layers_fit_frame, 0.48, 0.34));
      const auto hole_fill_delta = color_distance(
          screen_pixel(edit_outline_frame, 0.48, 0.50),
          screen_pixel(layers_fit_frame, 0.48, 0.50));
      const auto island_fill_delta = color_distance(
          screen_pixel(edit_outline_frame, 0.90, 0.70),
          screen_pixel(layers_fit_frame, 0.90, 0.70));
      if (body_fill_delta < 24 || island_fill_delta < 24 || hole_fill_delta > 12) {
        std::cerr << "Outline fill samples: body=" << body_fill_delta
                  << " island=" << island_fill_delta << " hole=" << hole_fill_delta << '\n';
        return fail("Layers fill should render the ring and island while preserving the small hole");
      }

      outline_viewport->setProperty("zoom", 1.8);
      QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
      const auto layers_zoom_frame = frame();
      if (!save_frame(QStringLiteral("mask-outline-layers-zoom.png"), layers_zoom_frame) ||
          difference_pixels(layers_fit_frame, layers_zoom_frame) < 100)
        return fail("The rendered Layers outline should track a real zoom change");

      // Center a real contour at 20x zoom. Settled outlines must remain visible
      // and viewport-clipped without bending offscreen path segments.
      editor_preview.setMaskPreviewMode(QStringLiteral("outline"));
      outline_viewport->setProperty("zoom", 20.0);
      outline_viewport->setProperty("panX", 0.02 * outline_viewport->property("basePaintedContentWidth").toDouble() * 20.0);
      outline_viewport->setProperty("panY", -0.045 * outline_viewport->property("basePaintedContentHeight").toDouble() * 20.0);
      process_layout_events(8);
      auto* settled_canvas = find_named_item(edit, QStringLiteral("editImageViewportSharedActiveSelectionOutline"));
      if (!settled_canvas || !item_visible(settled_canvas) || settled_canvas->width() > 2048 || settled_canvas->height() > 2048)
        return fail("Settled outlines must remain visible with a bounded backing Canvas at deep zoom");
      const auto deep_zoom_frame = frame();
      settled_canvas->setOpacity(0.0);
      const auto deep_zoom_without_outline = frame();
      settled_canvas->setOpacity(1.0);
      if (!save_frame(QStringLiteral("mask-outline-deep-zoom.png"), deep_zoom_frame) ||
          difference_pixels(deep_zoom_frame, deep_zoom_without_outline) < 30)
        return fail("The actual deep-zoom contour must paint visible pixels");

      // Capture explicit dash offsets on the same live Canvas-backed outline.
      // We set phase then request the frame immediately so the receipt measures
      // the requested geometry rather than timing a preference/state change.
      outline_viewport->setProperty("zoom", 1.0);
      outline_viewport->setProperty("panX", 0.0);
      outline_viewport->setProperty("panY", 0.0);
      edit->setProperty("editInspectorTab", QStringLiteral("adjust"));
      editor_preview.setMaskPreviewMode(QStringLiteral("marching-ants"));
      // Freeze the workspace animator while driving this Canvas's phase.
      // Allow its zero-delay repaint timer to run before each native grab.
      outline_overlay->setProperty("outlineAnimationEnabled", true);
      outline_overlay->setProperty("outlinePhase", 0.0);
      process_layout_events(8);
      const auto ants_phase_zero = frame();
      outline_overlay->setProperty("outlinePhase", 7.0);
      process_layout_events(8);
      const auto ants_phase_seven = frame();
      if (!save_frame(QStringLiteral("mask-outline-ants-phase-0.png"), ants_phase_zero) ||
          !save_frame(QStringLiteral("mask-outline-ants-phase-7.png"), ants_phase_seven) ||
          difference_pixels(ants_phase_zero, ants_phase_seven) < 30) {
        return fail("Rendered marching ants should visibly differ between phase 0 and phase 7");
      }
      const auto semantic_outline_path_count = editor_preview.activeMaskOutlinePaths().size();
      editor_preview.removeActiveMask();
      editor_preview.startLassoSelection(false);
      for (const auto& point : {QPointF(0.20, 0.26), QPointF(0.34, 0.18), QPointF(0.60, 0.24),
                                QPointF(0.72, 0.48), QPointF(0.56, 0.74), QPointF(0.28, 0.70)})
        editor_preview.appendActivePolygonPoint(point.x(), point.y());
      if (!editor_preview.closeActiveLassoPath())
        return fail("The native lasso fixture should close its authored path");
      editor_preview.setMaskPreviewMode(QStringLiteral("outline"));
      edit->setProperty("editInspectorTab", QStringLiteral("layers"));
      if (!wait_for_condition([&]() {
            const auto trace = editor_preview.activeMaskOutlineTrace();
            return trace.value(QStringLiteral("outline_trace_source")).toString() != QStringLiteral("worker-pending") &&
                   !editor_preview.activeMaskOutlinePaths().isEmpty() && !editor_preview.processing();
          }, 10000))
        return fail("The native lasso outline should settle from final mask alpha");
      process_layout_events(8);
      const auto lasso_without_fill = frame();
      editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
      process_layout_events(8);
      const auto lasso_with_fill = frame();
      const auto lasso_inside_delta = color_distance(screen_pixel(lasso_without_fill, 0.46, 0.45),
                                                     screen_pixel(lasso_with_fill, 0.46, 0.45));
      const auto lasso_left_delta = color_distance(screen_pixel(lasso_without_fill, 0.06, 0.50),
                                                   screen_pixel(lasso_with_fill, 0.06, 0.50));
      const auto lasso_right_delta = color_distance(screen_pixel(lasso_without_fill, 0.94, 0.50),
                                                    screen_pixel(lasso_with_fill, 0.94, 0.50));
      if (lasso_inside_delta < 24 || lasso_left_delta > 12 || lasso_right_delta > 12 ||
          !save_frame(QStringLiteral("mask-outline-lasso-accuracy.png"), lasso_with_fill))
        return fail("The native lasso fill must stay inside its angled path on both sides");
      QVariantMap outline_receipt{
          {QStringLiteral("fixture"), QStringLiteral("semantic-raster-ring-hole-island")},
          {QStringLiteral("outline_path_count"), semantic_outline_path_count},
          {QStringLiteral("lasso_inside_rgb_delta"), lasso_inside_delta},
          {QStringLiteral("lasso_left_outside_rgb_delta"), lasso_left_delta},
          {QStringLiteral("lasso_right_outside_rgb_delta"), lasso_right_delta},
          {QStringLiteral("deep_zoom_outline_pixel_differences"), difference_pixels(deep_zoom_frame, deep_zoom_without_outline)},
          {QStringLiteral("layers_body_fill_rgb_delta"), body_fill_delta},
          {QStringLiteral("layers_island_fill_rgb_delta"), island_fill_delta},
          {QStringLiteral("layers_hole_rgb_delta"), hole_fill_delta},
          {QStringLiteral("zoom_frame_sampled_pixel_differences"), difference_pixels(layers_fit_frame, layers_zoom_frame)},
          {QStringLiteral("marching_phase_sampled_pixel_differences"), difference_pixels(ants_phase_zero, ants_phase_seven)},
          {QStringLiteral("fit_capture"), QStringLiteral("mask-outline-layers-fit.png")},
          {QStringLiteral("zoom_capture"), QStringLiteral("mask-outline-layers-zoom.png")},
          {QStringLiteral("edit_outline_capture"), QStringLiteral("mask-outline-edit-fit.png")},
      };
      std::cout << "Mask outline visual proof: "
                << QJsonDocument(QJsonObject::fromVariantMap(outline_receipt))
                       .toJson(QJsonDocument::Compact).toStdString() << '\n';
      editor_preview.setMaskPreviewMode(QStringLiteral("hidden"));
      outline_viewport->setProperty("zoom", 1.0);
      qunsetenv("PHOTO_EDITOR_DATABASE_URL");
      qunsetenv("PHOTO_EDITOR_CACHE_DIR");
      qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
      editor_preview.setImageProvider(nullptr);
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
      return EXIT_SUCCESS;
    }

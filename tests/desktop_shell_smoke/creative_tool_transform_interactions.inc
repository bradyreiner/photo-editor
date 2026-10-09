    {
      if (!QMetaObject::invokeMethod(edit, "setInterfaceDensity", Q_ARG(QVariant, QVariant(QStringLiteral("balanced")))) ||
          !invoke_edit_tool(QStringLiteral("layers")) || !select_layer_for_tools(text_layer_index))
        return fail("Text transform proof should select its isolated text layer");
      studio_window->resize(1440, 900);
      process_layout_events(8);
      const bool native_canvas = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_TEXTURE");
      const auto original_transform_zoom = tool_viewport->property("zoom");
      if (native_canvas) tool_viewport->setProperty("zoom", 0.5);
      auto* transform_overlay = find_named_item(studio_root, QStringLiteral("photoLayerTransformOverlay"));
      auto* bottom_right = find_named_item(studio_root, QStringLiteral("photoLayerTransformBottomRightHandle"));
      auto* top_left = find_named_item(studio_root, QStringLiteral("photoLayerTransformTopLeftHandle"));
      auto* top_right = find_named_item(studio_root, QStringLiteral("photoLayerTransformTopRightHandle"));
      auto* rotate_handle = find_named_item(studio_root, QStringLiteral("photoLayerTransformRotateHandle"));
      if (!transform_overlay || !bottom_right || !top_left || !top_right || !rotate_handle)
        return fail("Text should expose native transform handles");
      // Hide guides only for readback, so changing handle geometry cannot
      // substitute for actual typography pixels changing on the canvas.
      const auto grab_text_canvas = [&] {
        transform_overlay->setOpacity(0);
        const auto frame = studio_window->grabWindow();
        transform_overlay->setOpacity(1);
        const auto origin = tool_viewport->mapToScene(QPointF(
            tool_viewport->property("paintedContentX").toDouble(),
            tool_viewport->property("paintedContentY").toDouble()));
        const double factor = static_cast<double>(frame.width()) / studio_window->width();
        return frame.copy(QRect(qRound(origin.x() * factor), qRound(origin.y() * factor),
            qRound(tool_viewport->property("paintedContentWidth").toDouble() * factor),
            qRound(tool_viewport->property("paintedContentHeight").toDouble() * factor)));
      };
      const auto text_before_transform = editor_preview.photoLayers().at(text_layer_index).toMap();
      for (const double initial_rotation : {0.0, 37.0}) {
        if (!editor_preview.setPhotoLayerTransform(text_layer_index, 180, 160, 0.8, initial_rotation) ||
            !wait_for_condition([&] {
              return !editor_preview.processing() && !editor_preview.fullRenderRunning() &&
                  std::abs(transform_overlay->property("layerRotation").toDouble() - initial_rotation) < 0.001;
            }, 15000))
          return fail("The text fixture should settle at each initial rotation");
        process_layout_events(8);
        const auto start_layer = editor_preview.photoLayers().at(text_layer_index).toMap();
        const auto baseline_preview = editor_preview_image_provider->image(QStringLiteral("edited"));
        const auto baseline_canvas = grab_text_canvas();
        const auto baseline_native_revision = editor_preview.displayPreviewNativeRevision();
        const QPointF fixed_corner = scene_rect(top_left).center();
        const QPointF start = scene_rect(bottom_right).center();
        const QPointF end = fixed_corner + (start - fixed_corner) * 1.30;
        paint_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
        QElapsedTimer transform_timer;
        transform_timer.start();
        send_paint_pointer(QEvent::MouseButtonPress, start);
        for (int step = 1; step <= 8; ++step)
          send_paint_pointer(QEvent::MouseMove, start + (end - start) * (static_cast<double>(step) / 8.0));
        if (!transform_overlay->property("interactionActive").toBool() ||
            !wait_for_condition([&] {
              const auto preview = editor_preview_image_provider->image(QStringLiteral("edited"));
              if (native_canvas) return editor_preview.displayPreviewNativeRevision() != baseline_native_revision &&
                                         grab_text_canvas() != baseline_canvas;
              return !preview.isNull() && preview != baseline_preview;
            }, 5000))
          return fail("Scaling text should change rendered pixels before releasing its native handle");
        const auto changed_preview_ms = transform_timer.nsecsElapsed() / 1e6;
        const auto scaled_layer = editor_preview.photoLayers().at(text_layer_index).toMap();
        if (std::abs(scaled_layer.value(QStringLiteral("scale")).toDouble() - 1.04) > 0.015 ||
            QLineF(fixed_corner, scene_rect(top_left).center()).length() > 2.0)
          return fail("Text resize should track the pointer while keeping the opposite corner anchored, including rotated text");
        if (!capture_visual_proof(studio_window, studio_root, proof_dir,
              QStringLiteral("text-live-scale-%1deg").arg(qRound(initial_rotation)), 1440, 900,
              {QStringLiteral("photoLayerTransformFrame"), QStringLiteral("photoLayerTransformBottomRightHandle")},
              &studio_manifest))
          return fail("Text transform should visibly render its scaled text while held");
        send_paint_pointer(QEvent::MouseButtonRelease, end);
        if (!wait_for_condition([&] {
              return !transform_overlay->property("interactionActive").toBool() && !editor_preview.processing();
            }, 15000))
          return fail("Releasing text scale should finish its native edit transaction");
        editor_preview.undo();
        if (!wait_for_condition([&] {
              const auto layer = editor_preview.photoLayers().at(text_layer_index).toMap();
              return layer.value(QStringLiteral("x")) == start_layer.value(QStringLiteral("x")) &&
                  layer.value(QStringLiteral("y")) == start_layer.value(QStringLiteral("y")) &&
                  std::abs(layer.value(QStringLiteral("scale")).toDouble() - 0.8) < 0.001 &&
                  !editor_preview.processing();
            }, 15000))
          return fail("One undo should restore the entire text scale gesture");
        editor_preview.redo();
        if (!wait_for_condition([&] {
              return std::abs(editor_preview.photoLayers().at(text_layer_index).toMap()
                  .value(QStringLiteral("scale")).toDouble() - scaled_layer.value(QStringLiteral("scale")).toDouble()) < 0.001 &&
                  !editor_preview.processing();
            }, 15000))
          return fail("Redo should restore the completed text scale gesture");
        creative_interaction_metrics.append(QJsonObject{
            {QStringLiteral("interaction"), QStringLiteral("text_scale")},
            {QStringLiteral("initial_rotation_degrees"), initial_rotation},
            {QStringLiteral("held_drag_changed_preview_ms"), changed_preview_ms},
            {QStringLiteral("preview_route"), native_canvas ? QStringLiteral("metal") : QStringLiteral("image_provider")},
            {QStringLiteral("opposite_corner_drift_viewport_pixels"), QLineF(fixed_corner, scene_rect(top_left).center()).length()},
            {QStringLiteral("one_undo_and_redo"), true}, {QStringLiteral("pixels_changed_before_release"), true}});
      }
      // Shift rotation snaps around a stable center. Native modifiers exercise
      // the gesture path rather than calling transformEdited directly.
      process_layout_events(8);
      const auto center = transform_overlay->mapToScene(QPointF(
          transform_overlay->property("centerX").toDouble(), transform_overlay->property("centerY").toDouble()));
      const QPointF rotation_start = scene_rect(rotate_handle).center();
      const double radius = QLineF(center, rotation_start).length();
      const double angle = std::atan2(rotation_start.y() - center.y(), rotation_start.x() - center.x()) + 0.70;
      const QPointF rotation_end = center + QPointF(std::cos(angle) * radius, std::sin(angle) * radius);
      const auto send_shift_pointer = [&](QEvent::Type type, const QPointF& point) {
        QMouseEvent event(type, point, QPointF(studio_window->mapToGlobal(point.toPoint())),
            type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
            type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::ShiftModifier);
        event.setTimestamp(paint_pointer_timestamp);
        paint_pointer_timestamp += 16;
        QCoreApplication::sendEvent(studio_window, &event);
        process_layout_events(2);
      };
      const auto rotation_baseline = editor_preview_image_provider->image(QStringLiteral("edited"));
      const auto rotation_baseline_canvas = grab_text_canvas();
      const auto rotation_native_revision = editor_preview.displayPreviewNativeRevision();
      paint_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
      send_shift_pointer(QEvent::MouseButtonPress, rotation_start);
      for (int step = 1; step <= 8; ++step) {
        const double intermediate_angle = angle - 0.70 + 0.70 * step / 8.0;
        send_shift_pointer(QEvent::MouseMove, center + QPointF(std::cos(intermediate_angle) * radius,
                                                             std::sin(intermediate_angle) * radius));
      }
      const auto snapped_rotation = editor_preview.photoLayers().at(text_layer_index).toMap()
          .value(QStringLiteral("rotation")).toDouble();
      if (!transform_overlay->property("interactionActive").toBool() ||
          std::abs(snapped_rotation / 15.0 - std::round(snapped_rotation / 15.0)) > 0.001 ||
          !wait_for_condition([&] {
            if (native_canvas) return editor_preview.displayPreviewNativeRevision() != rotation_native_revision &&
                                       grab_text_canvas() != rotation_baseline_canvas;
            return editor_preview_image_provider->image(QStringLiteral("edited")) != rotation_baseline;
          }, 5000))
        return fail("Shift rotation should snap to 15 degrees and render text pixels during the drag");
      if (!capture_visual_proof(studio_window, studio_root, proof_dir,
            QStringLiteral("text-live-rotation"), 1440, 900,
            {QStringLiteral("photoLayerTransformFrame"), QStringLiteral("photoLayerTransformRotateHandle")}, &studio_manifest))
        return fail("Text rotation should visibly render before release");
      send_shift_pointer(QEvent::MouseButtonRelease, rotation_end);
      if (!wait_for_condition([&] { return !editor_preview.processing(); }, 15000))
        return fail("Text rotation should finish its preview");
      creative_interaction_metrics.append(QJsonObject{
          {QStringLiteral("interaction"), QStringLiteral("text_rotation")},
          {QStringLiteral("snapped_degrees"), snapped_rotation}, {QStringLiteral("pixels_changed_before_release"), true}});
      editor_preview.setPhotoLayerTransform(text_layer_index,
          text_before_transform.value(QStringLiteral("x")).toInt(), text_before_transform.value(QStringLiteral("y")).toInt(),
          text_before_transform.value(QStringLiteral("scale"), 1.0).toDouble(),
          text_before_transform.value(QStringLiteral("rotation"), 0.0).toDouble());
      if (!wait_for_condition([&] { return !editor_preview.processing(); }, 15000))
        return fail("Transform proof should restore its fixture for later studio checks");
      tool_viewport->setProperty("zoom", original_transform_zoom);
    }

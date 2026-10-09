      visual_window->resize(1440, 900);
      timeline_workspace->setProperty("timelineZoom", 72.0);
      timeline_workspace->setProperty("snappingEnabled", false);
      timeline_workspace->setProperty("rippleModeEnabled", false);
      QMetaObject::invokeMethod(timeline_workspace, "applyTimelineLayoutPreset", Q_ARG(QVariant, QVariant(QStringLiteral("edit"))));
      process_layout_events(10);
      const auto clip_name = QStringLiteral("timelineClip-Opening shot");
      ulong trim_timestamp = 30000;
      bool trim_pointer_down = false;
      const auto trim_pointer = [&](QEvent::Type type, QPointF point) {
        if (type == QEvent::MouseButtonPress) trim_pointer_down = true;
        if (type == QEvent::MouseButtonRelease) trim_pointer_down = false;
        QMouseEvent event(type, point, QPointF(visual_window->mapToGlobal(point.toPoint())),
                          type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                          trim_pointer_down ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
        trim_timestamp += 16;
        event.setTimestamp(trim_timestamp);
        QCoreApplication::sendEvent(visual_window, &event);
        process_layout_events(3);
      };
      const auto trim_handle = [&](const QString& edge) -> QQuickItem* {
        auto* clip = find_named_item(visual_root, clip_name);
        return clip ? find_named_item(clip, QStringLiteral("timelineClip%1TrimHandle").arg(edge)) : nullptr;
      };
      const auto trim_drag = [&](const QString& edge, double delta) {
        auto* handle = trim_handle(edge);
        if (!handle || !item_visible(handle)) return false;
        const auto start = scene_rect(handle).center();
        trim_timestamp += static_cast<ulong>(QApplication::doubleClickInterval() + 1);
        trim_pointer(QEvent::MouseButtonPress, start);
        if (!visual_window->mouseGrabberItem()) return false;
        const auto before = timeline_document.currentClip();
        for (int step = 1; step <= 8; ++step) trim_pointer(QEvent::MouseMove, start + QPointF(delta * step / 8, 0));
        // Pointer feedback must not serialize/replace the edit document on every move.
        if (timeline_document.currentClip().value(QStringLiteral("trimStartSeconds")) != before.value(QStringLiteral("trimStartSeconds")) ||
            timeline_document.currentClip().value(QStringLiteral("trimEndSeconds")) != before.value(QStringLiteral("trimEndSeconds"))) return false;
        trim_pointer(QEvent::MouseButtonRelease, start + QPointF(delta, 0));
        process_layout_events(8);
        return !timeline_workspace->property("trimFeedbackState").toMap().value(QStringLiteral("visible")).toBool();
      };
      const auto close_seconds = [](const QVariantMap& clip, const QString& key, double expected) {
        return std::abs(clip.value(key).toDouble() - expected) < 0.001;
      };
      auto* handle = trim_handle(QStringLiteral("Right"));
      if (!handle || !item_visible(handle)) return fail("Selected clip must expose a visible right trim edge");
      const auto click_position = scene_rect(handle).center();
      trim_pointer(QEvent::MouseButtonPress, click_position);
      trim_pointer(QEvent::MouseButtonRelease, click_position);
      if (!close_seconds(timeline_document.currentClip(), QStringLiteral("trimmedDurationSeconds"), 8.0)) return fail("Clicking a trim handle must not trim the clip");
      if (!trim_drag(QStringLiteral("Right"), -72.0) ||
          !close_seconds(timeline_document.currentClip(), QStringLiteral("trimmedDurationSeconds"), 7.0)) return fail("Right edge must follow the exact drag delta");
      timeline_document.undo();
      if (!close_seconds(timeline_document.currentClip(), QStringLiteral("trimmedDurationSeconds"), 8.0)) return fail("One undo must restore the right-edge gesture");
      timeline_document.redo();
      process_layout_events(8);
      if (!trim_drag(QStringLiteral("Left"), 72.0) ||
          !close_seconds(timeline_document.currentClip(), QStringLiteral("startSeconds"), 1.0) ||
          !close_seconds(timeline_document.currentClip(), QStringLiteral("trimmedDurationSeconds"), 6.0)) return fail("Left edge must follow its delta without shifting the out point");
      timeline_document.undo();
      process_layout_events(8);
      timeline_workspace->setProperty("rippleModeEnabled", true);
      if (!trim_drag(QStringLiteral("Left"), 72.0) ||
          !close_seconds(timeline_document.currentClip(), QStringLiteral("startSeconds"), 0.0) ||
          !close_seconds(timeline_document.currentClip(), QStringLiteral("trimmedDurationSeconds"), 6.0)) return fail("Ripple-in must apply one delta once rather than accumulate each pointer move");
      timeline_document.undo();
      timeline_workspace->setProperty("rippleModeEnabled", false);
      process_layout_events(8);
      handle = trim_handle(QStringLiteral("Right"));
      const auto cancel_position = scene_rect(handle).center();
      trim_pointer(QEvent::MouseButtonPress, cancel_position);
      trim_pointer(QEvent::MouseMove, cancel_position - QPointF(72, 0));
      auto* grabber = visual_window->mouseGrabberItem();
      if (!grabber) return fail("Trim handle must retain its pointer grab");
      grabber->ungrabMouse();
      trim_pointer(QEvent::MouseButtonRelease, cancel_position - QPointF(72, 0));
      if (!close_seconds(timeline_document.currentClip(), QStringLiteral("trimmedDurationSeconds"), 7.0) ||
          !timeline_workspace->property("clipEdgeTrimGesture").toMap().isEmpty()) return fail("Canceled edge trim must discard all preview work");
      timeline_workspace->setProperty("timelineZoom", 2.0);
      process_layout_events(8);
      auto* left = trim_handle(QStringLiteral("Left"));
      auto* right = trim_handle(QStringLiteral("Right"));
      if (!left || !right || scene_rect(left).intersects(scene_rect(right))) return fail("Very short screen-space clips must keep both trim edges distinct");
      timeline_workspace->setProperty("timelineZoom", 72.0);
      process_layout_events(8);
      if (!capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("timeline-trimmed-edit"), 1440, 900,
                                {clip_name, QStringLiteral("timelineClipLeftTrimHandle"), QStringLiteral("timelineClipRightTrimHandle")}, &manifest)) return fail("Native trim proof must capture the finished timeline");
      if (!timeline_document.saveTimeline()) return fail("Trimmed sequence must save");
      const auto trimmed_path = timeline_document.savedDocumentPath();
      timeline_document.closeTimeline();
      if (!timeline_document.loadTimeline(trimmed_path)) return fail("Trimmed sequence must reopen");
      timeline_document.selectClip(0);
      if (!close_seconds(timeline_document.currentClip(), QStringLiteral("trimmedDurationSeconds"), 7.0)) return fail("Native edge trim must survive save/reopen");
      std::cout << "Native exact in/out edge trim, ripple-in, pointer grab, cancellation, narrow clips, undo/redo, and save/reopen passed.\n";
      return EXIT_SUCCESS;

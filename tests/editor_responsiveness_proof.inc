// Included in the actual-app driver after the complete QML shell is ready.
{
  const auto proof_dir = option_value(arguments, "--responsiveness-proof-dir");
  if (!proof_dir.isEmpty()) {
    using Access = photo_editor::desktop::EditorPreviewControllerTestAccess;
    if (!brush_only || qEnvironmentVariableIsSet("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE"))
      return fail("Responsiveness proof requires --brush-only and enabled recovery persistence.");
    QDir().mkpath(proof_dir);
    const auto video = fixture_dir.filePath("responsiveness.mp4");
    if (!write_timeline_proof_video_file(source_path, video))
      return fail("Cannot create decoded-video fixture.");
    catalog_assets.replaceAssetsForTesting({make_asset("responsiveness-photo", source_path, {})});
    catalog_assets.selectIndex(0);
    QMetaObject::invokeMethod(root, "switchToWorkspaceIndex", Q_ARG(QVariant, root->property("editWorkspaceIndex")));
    root->setProperty("editWorkspaceMode", "adjust");
    if (!wait_for_condition([&] {
          return active_workspace(root) && editor_preview.hasAsset() &&
              editor_preview.assetId() == "responsiveness-photo" && Access::all_idle(editor_preview);
        }, timeout_ms)) return fail("Responsiveness photo did not settle.");
    if (arguments.contains("--responsiveness-history-visible"))
      active_workspace(root)->setProperty("editInspectorTab", "history");
    int frames = 0;
    const auto frame_connection = QObject::connect(root_window, &QQuickWindow::frameSwapped,
        root_window, [&] { ++frames; });
    const auto frame_guard = qScopeGuard([&] { QObject::disconnect(frame_connection); });
    QElapsedTimer heartbeat_clock;
    heartbeat_clock.start();
    double last_tick = 0.0;
    double max_gap = 0.0;
    QTimer heartbeat;
    heartbeat.setInterval(5);
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] {
      const auto now = heartbeat_clock.nsecsElapsed() / 1e6;
      max_gap = std::max(max_gap, now - last_tick);
      last_tick = now;
    });
    heartbeat.start();
    QJsonArray samples;
    const auto photo_idle = [&] {
      return !editor_preview.photoPaintBusy() && Access::all_idle(editor_preview) &&
          Access::pending_brush_commit_count(editor_preview) == 0;
    };
    const auto measure = [&](const QString& action, int iteration, auto operation, auto settled) {
      process_layout_events(4);
      max_gap = 0.0;
      last_tick = heartbeat_clock.nsecsElapsed() / 1e6;
      QElapsedTimer timer;
      timer.start();
      const auto before_frame = frames;
      if (!operation()) return false;
      const auto call_ms = timer.nsecsElapsed() / 1e6;
      root_window->requestUpdate();
      if (!wait_for_condition([&] { return frames > before_frame; }, timeout_ms)) return false;
      const auto first_frame_ms = timer.nsecsElapsed() / 1e6;
      if (!wait_for_condition(settled, timeout_ms)) return false;
      const auto settled_frame = frames;
      root_window->requestUpdate();
      if (!wait_for_condition([&] { return frames > settled_frame; }, timeout_ms)) return false;
      samples.append(QJsonObject{{"action", action}, {"iteration", iteration},
          {"call_ms", call_ms}, {"first_frame_ms", first_frame_ms},
          {"settled_ms", timer.nsecsElapsed() / 1e6}, {"heartbeat_max_gap_ms", max_gap},
          {"layer_stack", QJsonObject::fromVariantMap(Access::layer_stack_timings(editor_preview))}});
      return true;
    };
    if (!editor_preview.addPaintLayer() || !wait_for_condition(photo_idle, timeout_ms))
      return fail("Cannot create responsiveness paint layer.");
    const auto width = editor_preview.sourceImageWidth();
    const auto height = editor_preview.sourceImageHeight();
    if (arguments.contains("--responsiveness-zoom-out")) {
      auto* viewport = active_workspace(root)->findChild<QQuickItem*>("editImageViewport");
      if (!viewport || !viewport->setProperty("zoom", 4.0) ||
          !wait_for_condition([&] { return editor_preview.previewViewportActive() && photo_idle(); }, timeout_ms))
        return fail("Zoom-out proof could not enter the zoomed image viewport.");
    }
    for (int iteration = 0; iteration < iterations; ++iteration) {
      if (!measure("pixel_brush_commit", iteration, [&] {
            if (!editor_preview.beginPhotoPaintStroke(0, {{"size", 160}, {"color", "#ff6030"}, {"pressureSize", false}})) return false;
            for (int point = 0; point < 32; ++point)
              editor_preview.addPhotoPaintPoint(width * (0.1 + point * 0.025),
                  height * (0.15 + iteration * 0.1));
            return editor_preview.finishPhotoPaintStroke();
          }, photo_idle)) return fail("Pixel brush responsiveness measurement failed.");
      const auto before_adjustment = Access::recipe_json(editor_preview);
      if (!editor_preview.addAdjustmentLayer("Responsiveness exposure") ||
          !editor_preview.setPhotoLayerAdjustmentValue(0, "exposure", 0.7) ||
          !wait_for_condition(photo_idle, timeout_ms))
        return fail("Cannot prepare responsiveness adjustment layer.");
      if (!measure("delete_adjustment_layer", iteration,
            [&] { return editor_preview.deletePhotoLayer(0); }, photo_idle))
        return fail("Adjustment deletion responsiveness measurement failed.");
      if (editor_preview.photoLayerCount() != 1) return fail("Deletion lost paint layer.");
      if (Access::recipe_json(editor_preview) != before_adjustment)
        return fail("Adjustment deletion changed the underlying paint recipe.");
    }
    const auto paint_recipe = Access::recipe_json(editor_preview);
    editor_preview.undo();
    if (!wait_for_condition([&] { return editor_preview.photoLayerCount() == 2 && photo_idle(); }, timeout_ms))
      return fail("Deletion undo failed.");
    editor_preview.redo();
    if (!wait_for_condition([&] { return editor_preview.photoLayerCount() == 1 && photo_idle(); }, timeout_ms) ||
        Access::recipe_json(editor_preview) != paint_recipe) return fail("Deletion redo lost paint pixels.");
    const auto export_path = QDir(proof_dir).filePath("paint-export.png");
    if (!editor_preview.exportFlattenedPhoto(export_path) ||
        !wait_for_condition([&] { return !editor_preview.photoExportBusy(); }, 120000) ||
        !editor_preview.photoExportError().isEmpty()) return fail("Responsiveness paint export failed.");
    const QImage exported(export_path);
    const auto export_pixels = exported.convertToFormat(QImage::Format_RGBA64);
    if (export_pixels.isNull()) return fail("Responsiveness export has no pixels.");
    const auto export_hash = QString::fromLatin1(QCryptographicHash::hash(
        QByteArray::fromRawData(reinterpret_cast<const char*>(export_pixels.constBits()),
            export_pixels.sizeInBytes()), QCryptographicHash::Sha256).toHex());
    QJsonArray captures;
    save_capture_if_requested(root_window, proof_dir, "photo-after-brush-and-delete", &captures);
    QJsonObject history_hover;
    if (arguments.contains("--responsiveness-history-hover")) {
#include "editor_history_hover_proof.inc"
    }
    QJsonObject zoom_out;
    if (arguments.contains("--responsiveness-zoom-out")) {
#include "editor_zoom_out_proof.inc"
      return EXIT_SUCCESS;
    }
    timeline_document.createTimeline("Responsiveness video insertion");
    QMetaObject::invokeMethod(root, "switchToWorkspaceIndex", Q_ARG(QVariant, root->property("timelineWorkspaceIndex")));
    if (!wait_for_condition([&] {
          return active_workspace(root) && active_workspace(root)->objectName() == "timelineWorkspace";
        }, timeout_ms)) {
      const auto* workspace = active_workspace(root);
      auto* loader = root->findChild<QObject*>("timelineWorkspaceLoader");
      QJsonArray failed_captures;
      save_capture_if_requested(root_window, proof_dir, "timeline-route-failed", &failed_captures);
      return fail(QStringLiteral("Responsiveness Timeline did not open: workspace=%1 active=%2 route=%3 source=%4 status=%5")
          .arg(workspace ? workspace->objectName() : QStringLiteral("null"))
          .arg(timeline_document.hasActiveTimeline())
          .arg(root->property("currentWorkspace").toInt())
          .arg(loader ? loader->property("source").toUrl().toString() : QStringLiteral("null"))
          .arg(loader ? loader->property("status").toInt() : -1));
    }
    process_layout_events(8);
    for (int iteration = 0; iteration < iterations; ++iteration) {
      int clip_publications = 0;
      const auto connection = QObject::connect(&timeline_document,
          &photo_editor::desktop::TimelineDocumentController::clipsChanged,
          &heartbeat, [&] { ++clip_publications; });
      const auto expected = timeline_document.clipCount() + 1;
      if (!measure("append_video", iteration, [&] {
            timeline_document.addClipFromGallery(QStringLiteral("video-%1").arg(iteration),
                video, "Responsiveness video", 10.0, QUrl::fromLocalFile(source_path).toString());
            return timeline_document.clipCount() == expected;
          }, [&] { return timeline_document.currentClip().value("mediaAnalysisStatus").toString() == "ready"; }))
        return fail("Video insertion responsiveness measurement failed.");
      QObject::disconnect(connection);
      auto row = samples.last().toObject();
      row.remove("layer_stack");
      row.insert("clip_publications", clip_publications);
      samples.replace(samples.size() - 1, row);
    }
    save_capture_if_requested(root_window, proof_dir, "timeline-after-video-insertion", &captures);
    QJsonObject decoded_video;
    if (arguments.contains("--responsiveness-decoded-video")) {
      decoded_video = measure_timeline_continuous_scrub_preview(active_workspace(root), &timeline_playback);
      save_capture_if_requested(root_window, proof_dir, "timeline-after-decoded-scrub", &captures);
    }
    const auto native = root_window->rendererInterface()->graphicsApi() == QSGRendererInterface::Metal;
    QFile report(QDir(proof_dir).filePath("responsiveness.json"));
    if (!report.open(QIODevice::WriteOnly)) return fail("Cannot write responsiveness report.");
    report.write(QJsonDocument(QJsonObject{{"schema", "editor-responsiveness:v1"},
        {"platform", QGuiApplication::platformName()}, {"metal_scenegraph", native},
        {"source", source_path}, {"persistence_enabled", true},
        {"paint_recipe_undo_redo_exact", true}, {"paint_export_pixel_sha256", export_hash},
        {"export_width", exported.width()}, {"export_height", exported.height()},
        {"recovery_journal_bytes", Access::stop_checkpoint_helper_and_size(editor_preview)},
        {"video_frame_metrics", QJsonObject::fromVariantMap(timeline_playback.frameProcessingMetrics())},
        {"decoded_video_scrub", decoded_video},
        {"history_visible", arguments.contains("--responsiveness-history-visible")},
        {"history_hover", history_hover},
        {"zoom_out", zoom_out},
        {"helper", "isolated failing helper; recovery journal remains authoritative"},
        {"captures", captures}, {"samples", samples}}).toJson());
    return EXIT_SUCCESS;
  }
}

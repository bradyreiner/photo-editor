      using photo_editor::desktop::EditorPreviewControllerTestAccess;
      const auto typography_canvas_width = editor_preview.sourceImageWidth();
      const auto typography_canvas_height = editor_preview.sourceImageHeight();
      if (!editor_preview.addParagraphTextLayer(QStringLiteral("City light\nEvening edition"),
              60, 60, 460, 180, typography_canvas_width, typography_canvas_height) ||
          !QMetaObject::invokeMethod(edit, "editPhotoLayerInspectorTarget", Q_ARG(QVariant, QVariant(0))) ||
          !invoke_edit_tool(QStringLiteral("layers")))
        return fail("Typography proof should open the selected text in its real inspector");
      auto* text_editor = find_named_item(studio_root, QStringLiteral("photoLayerTextEditor"));
      auto* content = find_named_item(studio_root, QStringLiteral("photoLayerTextContentField"));
      auto* bold = find_named_item(studio_root, QStringLiteral("photoLayerTextBoldButton"));
      auto* geometry = find_named_item(studio_root, QStringLiteral("photoTypographyGeometryToggle"));
      if (!text_editor || !content || !bold || !geometry ||
          !wait_for_condition([&] { return item_visible(content) && item_visible(bold); }, 5000))
        return fail("Typography proof requires visible multiline copy and primary format controls");
      if (!click_studio_item(content) || !QMetaObject::invokeMethod(content, "forceInputFocus"))
        return fail("Typography copy should accept focus");
      content->setProperty("text", QStringLiteral("CITY LIGHT\nAn evening edition"));
      QMetaObject::invokeMethod(content, "clearInputFocus");
      if (editor_preview.photoLayers().at(0).toMap().value("text").toString() !=
          QStringLiteral("CITY LIGHT\nAn evening edition"))
        return fail("Inspector copy should persist both lines");
      if (!click_studio_item(bold) || !wait_for_condition([&] {
            return editor_preview.photoLayers().at(0).toMap().value("fontWeight").toString() == "bold";
          }, 3000))
        return fail("Primary Bold control should update the text layer");
      for (const auto& name : {QStringLiteral("photoTypographyItalicButton"),
                              QStringLiteral("photoTypographyUnderlineButton")}) {
        auto* button = find_named_item(studio_root, name);
        if (!button || !click_studio_item(button) || !wait_for_condition([&] {
              return button->property("active").toBool();
            }, 3000))
          return fail("Primary italic and underline controls should apply without opening advanced sections");
      }
      auto* font_trigger = find_named_item(studio_root, QStringLiteral("photoLayerTextFontField"));
      auto* font_search = find_named_item(studio_root, QStringLiteral("photoTypographyFamilyPickerSearch"));
      if (!font_trigger || !font_search || !click_studio_item(font_trigger) ||
          !wait_for_condition([&] { return font_search->hasActiveFocus(); }, 3000))
        return fail("The real inspector font picker should focus its search field");
      QKeyEvent search_font(QEvent::KeyPress, Qt::Key_G, Qt::NoModifier, QStringLiteral("Georgia"));
      QCoreApplication::sendEvent(studio_window, &search_font);
      process_layout_events(8);
      if (!studio_window->grabWindow().save(QDir(proof_dir).filePath("typography-font-search.png")))
        return fail("The font search should render previews");
      QKeyEvent choose_font(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
      QCoreApplication::sendEvent(studio_window, &choose_font);
      if (!wait_for_condition([&] {
            return editor_preview.photoLayerTypography(0).value("family").toString() == "Georgia";
          }, 3000))
        return fail("Keyboard font selection should change the selected text layer");
      const auto capture = [&](const QString& name, QSize size) {
        return capture_visual_proof(studio_window, studio_root, proof_dir, name, size.width(), size.height(),
            {QStringLiteral("creativeToolsSurface"), QStringLiteral("photoLayerTextEditor"),
             QStringLiteral("photoLayerTextContentField"), QStringLiteral("photoLayerTextBoldButton")}, &studio_manifest);
      };
      const auto wait_rendered_frame = [&] {
        const auto rendered = std::make_shared<int>(0);
        const auto connection = QObject::connect(studio_window, &QQuickWindow::frameSwapped, &engine,
            [rendered, studio_window] {
              ++*rendered;
              if (*rendered == 1) studio_window->update();
            }, Qt::QueuedConnection);
        studio_window->update();
        const auto ready = wait_for_condition([&] { return *rendered >= 2; }, 3000);
        QObject::disconnect(connection);
        return ready;
      };
      if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_TEXT_DRAG_PERF")) {
        const auto label = qEnvironmentVariable("PHOTO_EDITOR_SMOKE_TEXT_DRAG_PERF_LABEL");
        if (label != "baseline" && label != "candidate")
          return fail("Text-drag performance proof requires PHOTO_EDITOR_SMOKE_TEXT_DRAG_PERF_LABEL=baseline or candidate");
        auto* transform_overlay = find_named_item(studio_root, QStringLiteral("photoLayerTransformOverlay"));
        auto* move_handle = find_named_item(studio_root, QStringLiteral("photoLayerTransformMoveHandle"));
        if (!transform_overlay || !move_handle || !item_visible(move_handle) ||
            !wait_for_condition([&] { return !editor_preview.processing() && !editor_preview.fullRenderRunning(); }, 20000) ||
            !wait_rendered_frame())
          return fail("Text-drag performance proof requires a settled visible canvas move handle");

        photo_editor::desktop::observability::initialize();
        const auto source_path = qEnvironmentVariable("PHOTO_EDITOR_STUDIO_PHOTO_SOURCE");
        QFile source_file(source_path.isEmpty() ? photo_path : source_path);
        if (!source_file.open(QIODevice::ReadOnly))
          return fail("Text-drag performance proof could not read its fixed source fixture");
        const auto source_sha256 = QCryptographicHash::hash(source_file.readAll(), QCryptographicHash::Sha256).toHex();

        const auto layer_before = editor_preview.photoLayers().at(0).toMap();
        const auto style_before = editor_preview.photoLayerTypography(0);
        const auto recipe_before = EditorPreviewControllerTestAccess::recipe_json(editor_preview);
        const auto recipe_before_hash = QCryptographicHash::hash(recipe_before.toUtf8(), QCryptographicHash::Sha256).toHex();
        const auto start = scene_rect(move_handle).center();
        const auto overlay_opacity = transform_overlay->opacity();
        const auto capture_canvas_without_guides = [&] {
          transform_overlay->setOpacity(0.0);
          studio_window->update();
          const auto frame = studio_window->grabWindow();
          transform_overlay->setOpacity(overlay_opacity);
          if (frame.isNull() || !tool_viewport) return QImage{};
          const auto bounds = scene_rect(tool_viewport);
          const qreal factor = static_cast<qreal>(frame.width()) / studio_window->width();
          const QRect crop(qRound(bounds.x() * factor), qRound(bounds.y() * factor),
              qRound(bounds.width() * factor), qRound(bounds.height() * factor));
          return frame.copy(crop.intersected(frame.rect()));
        };
        const auto canvas_before = capture_canvas_without_guides();
        if (canvas_before.isNull()) return fail("Text-drag performance proof could not capture its baseline canvas");

        struct FrameSignalSamples final {
          QElapsedTimer timer;
          QMutex mutex;
          QVector<qint64> timestamps_ms;
        };
        // frameSwapped is emitted from Qt Quick's scene graph render thread.
        // Capture only a shared, mutex-protected timestamp sink there; keeping
        // it shared also protects an in-flight direct callback after disconnect.
        const auto frame_samples = std::make_shared<FrameSignalSamples>();
        auto& gesture_timer = frame_samples->timer;
        QJsonArray preview_updates;
        QJsonArray native_revision_updates;
        QJsonArray render_results;
        QSet<int> render_generations_recorded;
        const auto initial_native_revision = editor_preview.displayPreviewNativeRevision();
        const auto initial_render_generation = EditorPreviewControllerTestAccess::render_generation(editor_preview);
        auto last_native_revision = initial_native_revision;
        const auto initial_provider_image = editor_preview_image_provider->image(QStringLiteral("edited"));
        const auto initial_provider_cache_key = initial_provider_image.isNull()
            ? QStringLiteral("null") : QString::number(initial_provider_image.cacheKey());
        gesture_timer.start();
        const auto frame_connection = QObject::connect(studio_window, &QQuickWindow::frameSwapped, &engine,
            [frame_samples] {
              const auto at_ms = frame_samples->timer.nsecsElapsed() / 1000000;
              frame_samples->mutex.lock();
              frame_samples->timestamps_ms.append(at_ms);
              frame_samples->mutex.unlock();
            }, Qt::DirectConnection);
        const auto preview_connection = QObject::connect(&editor_preview, &photo_editor::desktop::EditorPreviewController::previewChanged,
            &engine, [&] {
              const auto at_ms = gesture_timer.nsecsElapsed() / 1000000.0;
              const auto native_revision = editor_preview.displayPreviewNativeRevision();
              const auto preview = editor_preview_image_provider->image(QStringLiteral("edited"));
              preview_updates.append(QJsonObject{
                  {QStringLiteral("at_ms"), at_ms},
                  {QStringLiteral("native_revision"), native_revision},
                  {QStringLiteral("provider_cache_key"), preview.isNull()
                      ? QStringLiteral("null") : QString::number(preview.cacheKey())}});
              if (native_revision > last_native_revision) {
                native_revision_updates.append(QJsonObject{
                    {QStringLiteral("at_ms"), at_ms},
                    {QStringLiteral("from_revision"), last_native_revision},
                    {QStringLiteral("to_revision"), native_revision},
                    {QStringLiteral("advance_count"), native_revision - last_native_revision}});
                last_native_revision = native_revision;
              }
              const auto ready_result = EditorPreviewControllerTestAccess::ready_interactive_render_result(editor_preview);
              if (ready_result && ready_result->generation > initial_render_generation &&
                  !render_generations_recorded.contains(ready_result->generation)) {
                render_generations_recorded.insert(ready_result->generation);
                render_results.append(QJsonObject{
                    {QStringLiteral("observed_at_ms"), at_ms},
                    {QStringLiteral("generation"), ready_result->generation},
                    {QStringLiteral("edit_transaction_generation"),
                     QString::number(static_cast<qulonglong>(ready_result->edit_transaction_generation))},
                    {QStringLiteral("async_job_generation"), ready_result->async_job.generation},
                    {QStringLiteral("backend"), ready_result->backend},
                    {QStringLiteral("max_edge"), ready_result->max_edge},
                    {QStringLiteral("output_width"), ready_result->output_width},
                    {QStringLiteral("output_height"), ready_result->output_height},
                    {QStringLiteral("full_resolution"), ready_result->full_resolution},
                    {QStringLiteral("draft_interactive"), ready_result->draft_interactive},
                    {QStringLiteral("render_reason"), ready_result->render_reason},
                    {QStringLiteral("render_mode"), ready_result->render_mode},
                    {QStringLiteral("interaction_mode"), ready_result->interaction_mode},
                    {QStringLiteral("total_elapsed_ms"), ready_result->total_elapsed_ms},
                    {QStringLiteral("output_elapsed_ms"), ready_result->adjustment_output_elapsed_ms},
                    {QStringLiteral("output_status"), ready_result->output_lut.status},
                    {QStringLiteral("gpu_to_cpu_bytes"), ready_result->output_lut.gpu_to_cpu_bytes},
                    {QStringLiteral("cpu_display_image"), !ready_result->preview_image.isNull()},
                    {QStringLiteral("native_surface"), ready_result->texture_upload_surface != nullptr},
                    {QStringLiteral("queue_wait_ms"), ready_result->queue_wait_ms},
                    {QStringLiteral("render_plan_overhead_ms"), ready_result->render_plan_overhead_ms},
                    {QStringLiteral("in_flight_render_stage_elapsed_ms"), ready_result->in_flight_render_stage_elapsed_ms},
                    {QStringLiteral("render_executor_cache_bypassed"), ready_result->render_executor_cache_bypassed},
                    {QStringLiteral("render_executor_cache_writes_skipped"), ready_result->render_executor_cache_writes_skipped},
                    {QStringLiteral("render_stage_elapsed_ms"),
                     QJsonObject::fromVariantMap(ready_result->render_stage_elapsed_ms)},
                    {QStringLiteral("render_stage_cache_hits"),
                     QJsonObject::fromVariantMap(ready_result->render_stage_cache_hits)}});
              }
            }, Qt::DirectConnection);

        paint_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
        send_pointer_to_window(studio_window, QEvent::MouseButtonPress, start);
        QJsonArray dispatched_moves;
        const QPointF drag_delta(84.0, 42.0);
        constexpr int move_count = 60;
        constexpr qint64 cadence_ns = 16000000;
        for (int step = 1; step <= move_count; ++step) {
          const auto target_ns = cadence_ns * step;
          while (gesture_timer.nsecsElapsed() < target_ns) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
            QThread::msleep(1);
          }
          const auto dispatch_start_ms = gesture_timer.nsecsElapsed() / 1000000.0;
          const auto point = start + drag_delta * (static_cast<qreal>(step) / move_count);
          send_pointer_to_window(studio_window, QEvent::MouseMove, point);
          dispatched_moves.append(QJsonObject{
              {QStringLiteral("step"), step},
              {QStringLiteral("target_ms"), static_cast<double>(target_ns) / 1000000.0},
              {QStringLiteral("dispatch_start_ms"), dispatch_start_ms},
              {QStringLiteral("dispatch_end_ms"), gesture_timer.nsecsElapsed() / 1000000.0}});
        }

        // Keep the native button held while the asynchronous draft render has
        // a chance to publish. The screenshot below reads the presented canvas,
        // not the moving transform outline.
        const auto final_move_ms = gesture_timer.nsecsElapsed() / 1000000.0;
        const auto minimum_hold_ms = final_move_ms + 1000.0;
        const auto hold_deadline_ms = final_move_ms + 2500.0;
        while (gesture_timer.nsecsElapsed() / 1000000.0 < hold_deadline_ms &&
               (gesture_timer.nsecsElapsed() / 1000000.0 < minimum_hold_ms ||
                (editor_preview.displayPreviewNativeRevision() == initial_native_revision && preview_updates.isEmpty()))) {
          QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
          QThread::msleep(2);
        }
        const auto held_canvas = capture_canvas_without_guides();
        const auto held_capture_ms = gesture_timer.nsecsElapsed() / 1000000.0;
        const auto native_revision_at_capture = editor_preview.displayPreviewNativeRevision();
        const auto held_capture_path = QDir(proof_dir).filePath(QStringLiteral("text-drag-%1-held.png").arg(label));
        const bool held_capture_saved = !held_canvas.isNull() && held_canvas.save(held_capture_path);
        send_pointer_to_window(studio_window, QEvent::MouseButtonRelease, start + drag_delta);
        const auto interaction_finished = wait_for_condition([&] {
          return !transform_overlay->property("interactionActive").toBool() &&
              !EditorPreviewControllerTestAccess::edit_transaction_active(editor_preview);
        }, 3000);
        const auto recipe_after = EditorPreviewControllerTestAccess::recipe_json(editor_preview);
        const auto recipe_after_hash = QCryptographicHash::hash(recipe_after.toUtf8(), QCryptographicHash::Sha256).toHex();
        const auto layer_after = editor_preview.photoLayers().at(0).toMap();
        const auto revision_after_release = editor_preview.displayPreviewNativeRevision();
        QObject::disconnect(preview_connection);
        QObject::disconnect(frame_connection);
        QVector<qint64> captured_frame_signal_ms;
        frame_samples->mutex.lock();
        captured_frame_signal_ms = frame_samples->timestamps_ms;
        frame_samples->mutex.unlock();
        const bool final_render_settled = wait_for_condition([&] {
          return !editor_preview.processing() && !editor_preview.fullRenderRunning();
        }, 20000) && wait_rendered_frame();
        const auto final_native_size = editor_preview.displayPreviewNativeSize();
        const auto final_native_revision = editor_preview.displayPreviewNativeRevision();
        const auto final_canvas = final_render_settled ? capture_canvas_without_guides() : QImage{};
        const bool final_capture_saved = !final_canvas.isNull() && final_canvas.save(
            QDir(proof_dir).filePath(QStringLiteral("text-drag-%1-final.png").arg(label)));

        int changed_canvas_pixels = -1;
        if (!canvas_before.isNull() && !held_canvas.isNull() && canvas_before.size() == held_canvas.size()) {
          changed_canvas_pixels = 0;
          for (int y = 0; y < canvas_before.height(); ++y) {
            const auto* before_line = reinterpret_cast<const QRgb*>(canvas_before.constScanLine(y));
            const auto* held_line = reinterpret_cast<const QRgb*>(held_canvas.constScanLine(y));
            for (int x = 0; x < canvas_before.width(); ++x)
              if (before_line[x] != held_line[x]) ++changed_canvas_pixels;
          }
        }

        const auto recipe_after_hash_text = QString::fromLatin1(recipe_after_hash);
        editor_preview.undo();
        const auto undo_exact = EditorPreviewControllerTestAccess::recipe_json(editor_preview) == recipe_before;
        const bool undo_settled = wait_for_condition([&] {
          return !editor_preview.processing() && !editor_preview.fullRenderRunning();
        }, 20000);
        editor_preview.redo();
        const bool redo_exact = EditorPreviewControllerTestAccess::recipe_json(editor_preview) == recipe_after;
        editor_preview.undo();
        const bool undo_again_exact = EditorPreviewControllerTestAccess::recipe_json(editor_preview) == recipe_before;
        const bool move_changed_layer = layer_after.value(QStringLiteral("x")) != layer_before.value(QStringLiteral("x")) ||
            layer_after.value(QStringLiteral("y")) != layer_before.value(QStringLiteral("y"));

        QJsonArray frame_swapped_ms;
        QJsonArray frame_intervals_ms;
        for (int i = 0; i < captured_frame_signal_ms.size(); ++i) {
          frame_swapped_ms.append(static_cast<double>(captured_frame_signal_ms.at(i)));
          if (i > 0)
            frame_intervals_ms.append(static_cast<double>(captured_frame_signal_ms.at(i) - captured_frame_signal_ms.at(i - 1)));
        }
        QJsonArray preview_intervals_ms;
        QJsonArray native_revision_intervals_ms;
        double first_preview_ms = -1.0;
        double previous_preview_ms = -1.0;
        int preview_updates_during_movement = 0;
        for (const auto& value : preview_updates) {
          const auto at_ms = value.toObject().value(QStringLiteral("at_ms")).toDouble();
          if (at_ms <= final_move_ms) ++preview_updates_during_movement;
          if (first_preview_ms < 0.0) first_preview_ms = at_ms;
          if (previous_preview_ms >= 0.0) preview_intervals_ms.append(at_ms - previous_preview_ms);
          previous_preview_ms = at_ms;
        }
        double first_native_update_ms = -1.0;
        double previous_native_update_ms = -1.0;
        double first_native_update_during_movement_ms = -1.0;
        int native_updates_during_movement = 0;
        for (const auto& value : native_revision_updates) {
          const auto update = value.toObject();
          const auto at_ms = update.value(QStringLiteral("at_ms")).toDouble();
          const auto advance_count = update.value(QStringLiteral("advance_count")).toInt();
          if (first_native_update_ms < 0.0) first_native_update_ms = at_ms;
          if (previous_native_update_ms >= 0.0)
            native_revision_intervals_ms.append(at_ms - previous_native_update_ms);
          previous_native_update_ms = at_ms;
          if (at_ms <= final_move_ms) {
            native_updates_during_movement += advance_count;
            if (first_native_update_during_movement_ms < 0.0)
              first_native_update_during_movement_ms = at_ms;
          }
        }

        QJsonArray frame_swapped_during_movement_ms;
        QJsonArray frame_intervals_during_movement_ms;
        qint64 previous_movement_frame_ms = -1;
        for (const auto frame_ms : captured_frame_signal_ms) {
          if (static_cast<double>(frame_ms) > final_move_ms) continue;
          frame_swapped_during_movement_ms.append(static_cast<double>(frame_ms));
          if (previous_movement_frame_ms >= 0)
            frame_intervals_during_movement_ms.append(static_cast<double>(frame_ms - previous_movement_frame_ms));
          previous_movement_frame_ms = frame_ms;
        }
        const bool native_texture_enabled = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_TEXTURE");
        const bool native_candidate_gate = label == QStringLiteral("candidate") && native_texture_enabled;
        QJsonObject fixture{
            {QStringLiteral("source_path"), source_path.isEmpty() ? photo_path : source_path},
            {QStringLiteral("source_sha256"), QString::fromLatin1(source_sha256)},
            {QStringLiteral("canvas_width"), typography_canvas_width},
            {QStringLiteral("canvas_height"), typography_canvas_height},
            {QStringLiteral("text"), QJsonValue::fromVariant(layer_before.value(QStringLiteral("text")))},
            {QStringLiteral("typography"), QJsonObject::fromVariantMap(style_before)},
            {QStringLiteral("x"), QJsonValue::fromVariant(layer_before.value(QStringLiteral("x")))},
            {QStringLiteral("y"), QJsonValue::fromVariant(layer_before.value(QStringLiteral("y")))},
            {QStringLiteral("pixel_width"), QJsonValue::fromVariant(layer_before.value(QStringLiteral("pixelWidth")))},
            {QStringLiteral("pixel_height"), QJsonValue::fromVariant(layer_before.value(QStringLiteral("pixelHeight")))},
            {QStringLiteral("scale"), QJsonValue::fromVariant(layer_before.value(QStringLiteral("scale")))},
            {QStringLiteral("rotation"), QJsonValue::fromVariant(layer_before.value(QStringLiteral("rotation")))}};
        const auto fixture_sha256 = QCryptographicHash::hash(
            QJsonDocument(fixture).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
        const QJsonObject result{
            {QStringLiteral("label"), label},
            {QStringLiteral("fixture"), fixture},
            {QStringLiteral("fixture_sha256"), QString::fromLatin1(fixture_sha256)},
            {QStringLiteral("gesture"), QStringLiteral("native-held-text-move")},
            {QStringLiteral("move_count"), move_count},
            {QStringLiteral("target_cadence_ms"), 16},
            {QStringLiteral("moves"), dispatched_moves},
            {QStringLiteral("preview_updates"), preview_updates},
            {QStringLiteral("preview_updates_during_movement"), preview_updates_during_movement},
            {QStringLiteral("native_revision_updates"), native_revision_updates},
            {QStringLiteral("render_results"), render_results},
            {QStringLiteral("native_updates_during_movement"), native_updates_during_movement},
            {QStringLiteral("first_native_update_ms"), first_native_update_ms},
            {QStringLiteral("first_native_update_during_movement_ms"), first_native_update_during_movement_ms},
            {QStringLiteral("native_revision_intervals_ms"), native_revision_intervals_ms},
            {QStringLiteral("final_move_ms"), final_move_ms},
            {QStringLiteral("preview_intervals_ms"), preview_intervals_ms},
            {QStringLiteral("first_preview_update_ms"), first_preview_ms},
            {QStringLiteral("frame_swapped_ms"), frame_swapped_ms},
            {QStringLiteral("frame_intervals_ms"), frame_intervals_ms},
            {QStringLiteral("frame_swapped_during_movement_ms"), frame_swapped_during_movement_ms},
            {QStringLiteral("frame_intervals_during_movement_ms"), frame_intervals_during_movement_ms},
            {QStringLiteral("frame_swapped_semantics"), QStringLiteral("scene_graph_frame_queued_for_present")},
            {QStringLiteral("native_revision_before"), initial_native_revision},
            {QStringLiteral("initial_render_generation"), initial_render_generation},
            {QStringLiteral("native_texture_path_enabled"), native_texture_enabled},
            {QStringLiteral("native_candidate_gate_enabled"), native_candidate_gate},
            {QStringLiteral("native_revision_at_held_capture"), native_revision_at_capture},
            {QStringLiteral("native_revision_after_release"), revision_after_release},
            {QStringLiteral("provider_cache_key_before"), initial_provider_cache_key},
            {QStringLiteral("held_capture_ms"), held_capture_ms},
            {QStringLiteral("held_canvas_capture"), QFileInfo(held_capture_path).fileName()},
            {QStringLiteral("held_canvas_changed_pixels"), changed_canvas_pixels},
            {QStringLiteral("native_revision_advanced_while_held"), native_revision_at_capture > initial_native_revision},
            {QStringLiteral("presented_canvas_changed_while_held"), changed_canvas_pixels > 0},
            {QStringLiteral("held_capture_saved"), held_capture_saved},
            {QStringLiteral("interaction_finished"), interaction_finished},
            {QStringLiteral("layer_position_changed"), move_changed_layer},
            {QStringLiteral("style_unchanged"), editor_preview.photoLayerTypography(0) == style_before},
            {QStringLiteral("scale_unchanged"), layer_after.value(QStringLiteral("scale")) == layer_before.value(QStringLiteral("scale"))},
            {QStringLiteral("rotation_unchanged"), layer_after.value(QStringLiteral("rotation")) == layer_before.value(QStringLiteral("rotation"))},
            {QStringLiteral("recipe_before_sha256"), QString::fromLatin1(recipe_before_hash)},
            {QStringLiteral("recipe_after_sha256"), recipe_after_hash_text},
            {QStringLiteral("undo_exact"), undo_exact},
            {QStringLiteral("redo_exact"), redo_exact},
            {QStringLiteral("undo_again_exact"), undo_again_exact},
            {QStringLiteral("final_render_settled"), final_render_settled},
            {QStringLiteral("final_native_revision"), final_native_revision},
            {QStringLiteral("final_native_width"), final_native_size.width()},
            {QStringLiteral("final_native_height"), final_native_size.height()},
            {QStringLiteral("final_capture_saved"), final_capture_saved},
            {QStringLiteral("undo_settled"), undo_settled},
            {QStringLiteral("observability_run_directory"), photo_editor::desktop::observability::runDirectory()}};
        const auto metrics_path = QDir(proof_dir).filePath(QStringLiteral("text-drag-perf-%1.json").arg(label));
        QFile metrics_file(metrics_path);
        const bool metrics_saved = metrics_file.open(QIODevice::WriteOnly) &&
            metrics_file.write(QJsonDocument(result).toJson(QJsonDocument::Indented)) > 0;
        const bool observability_flushed = photo_editor::desktop::observability::flush();
        if (!held_capture_saved || !metrics_saved || !observability_flushed || !move_changed_layer || !interaction_finished ||
            editor_preview.photoLayerTypography(0) != style_before ||
            layer_after.value(QStringLiteral("scale")) != layer_before.value(QStringLiteral("scale")) ||
            layer_after.value(QStringLiteral("rotation")) != layer_before.value(QStringLiteral("rotation")) ||
            changed_canvas_pixels <= 0 || !undo_exact || !undo_settled || !redo_exact || !undo_again_exact ||
            !final_render_settled || !final_capture_saved ||
            (native_candidate_gate && native_updates_during_movement < 2)) {
          qWarning() << "Text drag perf proof failed" << label << "moves" << dispatched_moves.size()
                     << "preview updates" << preview_updates.size() << "native revision"
                     << initial_native_revision << "->" << revision_after_release << "native updates during movement"
                     << native_updates_during_movement << "changed pixels"
                     << changed_canvas_pixels << "undo" << undo_exact << "settled" << undo_settled;
          return fail("Held text-drag performance proof must update presented text pixels and undo exactly");
        }
        std::cout << "Text-drag performance baseline: label=" << label.toStdString()
                  << " moves=" << dispatched_moves.size() << " previewUpdates=" << preview_updates.size()
                  << " nativeUpdatesDuringMovement=" << native_updates_during_movement
                  << " firstPreviewMs=" << first_preview_ms << " changedCanvasPixels=" << changed_canvas_pixels
                  << " metrics=" << metrics_path.toStdString() << '\n';
        return EXIT_SUCCESS;
      }
      const auto reveal_control = [&](QQuickItem* item) {
        auto* scroll = find_named_item(studio_root, QStringLiteral("creativeToolsScroll"));
        auto* viewport = scroll ? qobject_cast<QQuickItem*>(scroll->property("contentItem").value<QObject*>()) : nullptr;
        if (!viewport) return false;
        const auto position = item->mapToItem(viewport, QPointF(item->width()/2, item->height()/2));
        const auto maximum = std::max(0.0, viewport->property("contentHeight").toDouble() - viewport->height());
        viewport->setProperty("contentY", std::clamp(viewport->property("contentY").toDouble() +
            position.y() - viewport->height()/2, 0.0, maximum));
        process_layout_events(8);
        return scene_rect(scroll).contains(scene_rect(item));
      };
      for (const auto& density : densities) {
        if (!QMetaObject::invokeMethod(edit, "setInterfaceDensity", Q_ARG(QVariant, QVariant(density))))
          return fail("Typography proof should set each supported density");
        for (const auto& size : {QSize(1180, 768), QSize(1440, 900)}) {
          studio_window->resize(size);
          process_layout_events(8);
          auto* scroll = find_named_item(studio_root, QStringLiteral("creativeToolsScroll"));
          if (!capture(QStringLiteral("typography-%1-%2").arg(density).arg(size.width()), size))
            return fail("Typography inspector should render at each density");
          if (!scroll || !scene_rect(scroll).contains(scene_rect(content)) ||
              !scene_rect(scroll).contains(scene_rect(bold))) {
            qWarning() << "Typography fit" << density << size << "scroll" << scene_rect(scroll)
                       << "content" << scene_rect(content) << "bold" << scene_rect(bold);
            return fail("Essential typography must remain visible inside the inspector at all densities");
          }
        }
      }
      if (!QMetaObject::invokeMethod(edit, "setInterfaceDensity", Q_ARG(QVariant, QVariant(QStringLiteral("balanced")))))
        return fail("Typography proof should restore balanced density");
      studio_window->resize(1440, 900);
      process_layout_events(8);
      auto* geometry_scroll = find_named_item(studio_root, QStringLiteral("creativeToolsScroll"));
      auto* geometry_viewport = geometry_scroll
          ? qobject_cast<QQuickItem*>(geometry_scroll->property("contentItem").value<QObject*>()) : nullptr;
      if (!geometry_viewport)
        return fail("Expanded geometry proof should access its scroll viewport");
      const auto collapsed_geometry_content_height = geometry_viewport->property("contentHeight").toDouble();
      if (!reveal_control(geometry) || !click_studio_item(geometry) || !wait_for_condition([&] {
            return geometry->property("active").toBool();
          }, 3000))
        return fail("Optional box and perspective controls should open");
      process_layout_events(8);
      if (!wait_for_condition([&] {
            return geometry_viewport->property("contentHeight").toDouble() > collapsed_geometry_content_height + 1.0;
          }, 3000))
        return fail("Expanded geometry should update the inspector scroll extent");
      auto* box_width = find_named_item(studio_root, QStringLiteral("photoLayerTextBoxWidthSlider"));
      if (!box_width || !item_visible(box_width))
        return fail("Expanded geometry should expose text box dimensions");
      if (!reveal_control(box_width))
        return fail("Expanded geometry should scroll inside its viewport");
      if (!wait_rendered_frame())
        return fail("Expanded geometry should finish rendering");
      if (!capture_visual_proof(studio_window, studio_root, proof_dir, QStringLiteral("typography-geometry"),
            1440, 900, {QStringLiteral("creativeToolsSurface"), QStringLiteral("photoLayerTextBoxWidthSlider"),
                        QStringLiteral("photoLayerTextBoxHeightSlider")}, &studio_manifest))
        return fail("Expanded geometry should render inside its scroll viewport");
      if (!reveal_control(geometry) || !click_studio_item(geometry) || !wait_for_condition([&] {
            return !geometry->property("active").toBool();
          }, 3000))
        return fail("Geometry should collapse again");
      reveal_control(content);
      if (!click_studio_item(creative_tools_float) ||
          !wait_for_condition([&] { return content->window() != studio_window && item_visible(content); }, 5000))
        return fail("Typography controls should work when floated");
      auto* floating = content->window();
      if (floating->grabWindow().isNull() ||
          !floating->grabWindow().save(QDir(proof_dir).filePath("typography-floating.png")))
        return fail("Typography floating panel should render");
      if (!click_studio_item(creative_tools_float) ||
          !wait_for_condition([&] { return content->window() == studio_window; }, 5000))
        return fail("Typography should dock again");
      const auto drag_text_box = [&](const QString& screenshot) {
        auto* handle = find_named_item(studio_root, QStringLiteral("photoLayerTransformBottomRightHandle"));
        if (!handle || !item_visible(handle) || !wait_rendered_frame()) {
          qWarning() << "Text resize handle unavailable" << handle;
          return false;
        }
        const auto before_layer = editor_preview.photoLayers().at(0).toMap();
        const auto before_style = editor_preview.photoLayerTypography(0);
        const auto before_recipe = EditorPreviewControllerTestAccess::recipe_json(editor_preview);
        const auto start = scene_rect(handle).center();
        const QPointF delta(100, 70);
        paint_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
        send_pointer_to_window(studio_window, QEvent::MouseButtonPress, start);
        for (int step = 1; step <= 3; ++step)
          send_pointer_to_window(studio_window, QEvent::MouseMove, start + delta * (step/3.0));
        send_pointer_to_window(studio_window, QEvent::MouseButtonRelease, start + delta);
        if (!wait_for_condition([&] {
              return editor_preview.photoLayers().at(0).toMap().value("pixelWidth").toInt() >
                  before_layer.value("pixelWidth").toInt();
            }, 3000)) {
          qWarning() << "Text resize did not grow the box" << before_layer.value("pixelWidth")
                     << editor_preview.photoLayers().at(0).toMap().value("pixelWidth");
          qWarning() << "Drag bounds" << start << "delta" << delta << "handle" << scene_rect(handle);
          studio_window->grabWindow().save(QDir(proof_dir).filePath("typography-box-resize-failure.png"));
          return false;
        }
        const auto after_layer = editor_preview.photoLayers().at(0).toMap();
        auto expected_style = before_style;
        if (before_style.value("mode").toString() == QStringLiteral("point")) {
          expected_style.insert("mode", QStringLiteral("paragraph"));
          expected_style.insert("version", qMax(2, before_style.value("version").toInt()));
          if (!expected_style.contains("paragraphSpacing"))
            expected_style.insert("paragraphSpacing", 0);
        }
        if (editor_preview.photoLayerTypography(0) != expected_style ||
            after_layer.value("pixelHeight").toInt() <= before_layer.value("pixelHeight").toInt() ||
            after_layer.value("fontSize") != before_layer.value("fontSize") ||
            after_layer.value("scale") != before_layer.value("scale") ||
            after_layer.value("rotation") != before_layer.value("rotation")) {
          qWarning() << "Text resize changed styling" << before_style << editor_preview.photoLayerTypography(0)
                     << "font size" << before_layer.value("fontSize") << after_layer.value("fontSize")
                     << "scale" << before_layer.value("scale") << after_layer.value("scale")
                     << "dimensions" << after_layer.value("pixelWidth") << after_layer.value("pixelHeight");
          studio_window->grabWindow().save(QDir(proof_dir).filePath("typography-box-resize-failure.png"));
          return false;
        }
        const auto after_recipe = EditorPreviewControllerTestAccess::recipe_json(editor_preview);
        if (!wait_for_condition([&] { return !editor_preview.processing() && !editor_preview.fullRenderRunning(); }, 20000) ||
            !wait_rendered_frame() || !studio_window->grabWindow().save(QDir(proof_dir).filePath(screenshot))) return false;
        editor_preview.undo();
        if (EditorPreviewControllerTestAccess::recipe_json(editor_preview) != before_recipe) {
          qWarning() << "Text resize undo did not restore recipe";
          return false;
        }
        editor_preview.redo();
        if (EditorPreviewControllerTestAccess::recipe_json(editor_preview) != after_recipe) {
          qWarning() << "Text resize redo did not restore recipe";
          return false;
        }
        editor_preview.undo();
        return EditorPreviewControllerTestAccess::recipe_json(editor_preview) == before_recipe;
      };
      if (!drag_text_box(QStringLiteral("typography-paragraph-box-resize.png")))
        return fail("Native paragraph handles must resize the box while preserving type size and one-step undo");
      const auto paragraph_recipe = EditorPreviewControllerTestAccess::recipe_json(editor_preview);
      if (!editor_preview.addPointTextLayer(QStringLiteral("Point text"), 80, 80,
              typography_canvas_width, typography_canvas_height) ||
          !QMetaObject::invokeMethod(edit, "editPhotoLayerInspectorTarget", Q_ARG(QVariant, QVariant(0))) ||
          !wait_for_condition([&] { return editor_preview.photoLayerTypography(0).value("mode").toString() == "point"; }, 3000) ||
          !drag_text_box(QStringLiteral("typography-point-box-resize.png")))
        return fail("Native point-text handles must enable paragraph wrapping without scaling the letters");
      editor_preview.undo();
      if (EditorPreviewControllerTestAccess::recipe_json(editor_preview) != paragraph_recipe)
        return fail("Point-text resize proof must restore the preceding document exactly");
      QFile manifest_file(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
      if (!manifest_file.open(QIODevice::WriteOnly) ||
          manifest_file.write(QJsonDocument(studio_manifest).toJson(QJsonDocument::Indented)) <= 0)
        return fail("Typography proof should save its capture manifest");
      std::cout << "Real Studio typography: multiline copy, Bold/Italic/Underline, keyboard font search, densities, geometry, float/dock, paragraph/point box resize with fixed font size and grouped undo passed\n";
      return EXIT_SUCCESS;

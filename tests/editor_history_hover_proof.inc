// Actual history row handlers, QPA pointer input, and the production controller.
      auto* workspace = active_workspace(root);
      workspace->setProperty("editInspectorTab", "history");
      const bool gated = workspace->property("editHistoryHoverPreviewContract").isValid();
      const auto original_recipe = Access::recipe_json(editor_preview);
      const auto original_index = editor_preview.editHistoryCurrentIndex();
      QQuickItem* row = nullptr;
      QObject* hover = nullptr;
      int row_index = -1;
      const auto find_row = [&](const auto& self, QQuickItem* item) -> void {
        if (row || !item) return;
        if (item->objectName() == "editHistoryEntryRow" && item_visible(item)) {
          const auto data = item->property("modelData").toMap();
          const auto center = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
          if (data.value("restorable").toBool() && !data.value("current").toBool() &&
              center.x() > 0 && center.x() < root_window->width() &&
              center.y() > 0 && center.y() < root_window->height()) {
            row = item;
            row_index = data.value("index").toInt();
            for (auto* candidate : item->children()) {
              if (candidate->property("hovered").isValid()) { hover = candidate; break; }
            }
          }
        }
        for (auto* child : item->childItems()) self(self, child);
      };
      if (!wait_for_condition([&] { find_row(find_row, qobject_cast<QQuickItem*>(workspace)); return row && hover; }, timeout_ms))
        return fail("History hover proof could not find a visible restorable row.");
      const auto inside = row->mapToScene(QPointF(row->width() / 2, row->height() / 2));
      const QPointF outside(30, root_window->height() - 25);
      const auto pointer = [&](QEvent::Type type, QPointF position,
                               Qt::MouseButton button = Qt::NoButton,
                               Qt::MouseButtons buttons = Qt::NoButton) {
        QMouseEvent event(type, position, root_window->mapToGlobal(position.toPoint()),
                          button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(root_window, &event);
      };
      const auto pump_for = [&](int milliseconds) {
        QElapsedTimer timer; timer.start();
        return wait_for_condition([&] { return timer.elapsed() >= milliseconds; }, milliseconds + timeout_ms);
      };
      pointer(QEvent::MouseMove, outside);
      process_layout_events(3);
      if (!wait_for_condition(photo_idle, timeout_ms)) return fail("History hover fixture did not settle.");
      const auto before_token = Access::history_preview_token(editor_preview);
      QElapsedTimer burst; burst.start();
      for (int move = 0; move < 12; ++move) {
        pointer(QEvent::MouseMove, inside);
        if (!hover->property("hovered").toBool()) return fail("QPA pointer missed the history row.");
        pointer(QEvent::MouseMove, outside);
        if (hover->property("hovered").toBool()) return fail("QPA pointer did not leave the history row.");
      }
      const auto burst_ms = static_cast<double>(burst.nsecsElapsed()) / 1e6;
      const auto burst_requests = Access::history_preview_token(editor_preview) - before_token;
      pump_for(180);
      if (Access::history_preview_active(editor_preview) ||
          (gated && Access::history_preview_token(editor_preview) != before_token))
        return fail("A short history hover published a deferred preview after pointer leave.");
      if (!gated && burst_requests == 0) return fail("Reference history hover did not exercise preview requests.");

      pointer(QEvent::MouseMove, inside);
      pump_for(50);
      if (gated && Access::history_preview_active(editor_preview))
        return fail("History preview rendered before its dwell interval.");
      if (!wait_for_condition([&] { return Access::history_preview_active(editor_preview); }, timeout_ms))
        return fail("A sustained history hover never previewed its revision.");
      if (Access::recipe_json(editor_preview) != original_recipe ||
          editor_preview.editHistoryCurrentIndex() != original_index)
        return fail("History hover changed the editable recipe or undo position.");
      if (!wait_for_condition(photo_idle, timeout_ms)) return fail("History preview render did not settle.");
      save_capture_if_requested(root_window, proof_dir, "history-hover-preview", &captures);
      pointer(QEvent::MouseMove, outside);
      if (Access::history_preview_active(editor_preview)) return fail("History preview did not clear on leave.");

      pointer(QEvent::MouseMove, inside);
      workspace->setProperty("editInspectorTab", "adjust");
      pump_for(180);
      const bool hidden_canceled = !Access::history_preview_active(editor_preview);
      if (gated && !hidden_canceled) return fail("Hidden history panel published a stale preview.");
      if (!hidden_canceled) editor_preview.setEditHistoryPreviewIndex(-1);
      workspace->setProperty("editInspectorTab", "history");
      pointer(QEvent::MouseMove, outside);
      process_layout_events(3);
      pointer(QEvent::MouseMove, inside);
      pointer(QEvent::MouseButtonPress, inside, Qt::LeftButton, Qt::LeftButton);
      pointer(QEvent::MouseButtonRelease, inside, Qt::LeftButton, Qt::NoButton);
      if (!wait_for_condition([&] { return editor_preview.editHistoryCurrentIndex() == row_index && photo_idle(); }, timeout_ms))
        return fail("History row click failed to restore its revision.");
      pump_for(180);
      if (Access::history_preview_active(editor_preview)) return fail("Pending hover overwrote a clicked history restore.");
      pointer(QEvent::MouseMove, outside);
      if (!editor_preview.restoreEditHistoryIndex(original_index) ||
          !wait_for_condition(photo_idle, timeout_ms) || Access::recipe_json(editor_preview) != original_recipe)
        return fail("History hover proof failed exact recipe restoration.");
      history_hover = {{"qpa_pointer_input", true}, {"dwell_enabled", gated},
          {"rapid_enter_leave_pairs", 12}, {"rapid_request_token_delta", static_cast<qint64>(burst_requests)},
          {"rapid_dispatch_ms", burst_ms}, {"sustained_preview", true},
          {"leave_cleared", true}, {"hidden_canceled", hidden_canceled},
          {"click_restore_canceled", true}, {"recipe_and_history_exact", true}};

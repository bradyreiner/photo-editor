// Included inside the actual-QML photo loop, after opening the Masks workspace.
if (const int point_count = qEnvironmentVariableIntValue("PHOTO_EDITOR_BENCHMARK_LASSO_POINTS");
    point_count >= 16) {
  auto* workspace = active_workspace(root);
  QVariant started;
  if (!workspace || !QMetaObject::invokeMethod(workspace, "startMaskLassoFromShortcut",
          Q_RETURN_ARG(QVariant, started)) || !started.toBool())
    return fail(QStringLiteral("Could not start the actual QML lasso tool."));
  std::vector<double> append_samples;
  QVariantList expected_points;
  QElapsedTimer gesture_timer;
  gesture_timer.start();
  const auto first_frame = frame_swap_count;
  for (int point_index = 0; point_index < point_count; ++point_index) {
    const double angle = 6.283185307179586 * point_index / point_count;
    const double radius = 0.32 + 0.035 * std::sin(angle * 11.0);
    const double x = 0.5 + radius * std::cos(angle);
    const double y = 0.5 + radius * std::sin(angle);
    expected_points.append(QVariantMap{{"x", x}, {"y", y}});
    QElapsedTimer point_timer;
    point_timer.start();
    editor_preview.appendActivePolygonPoint(x, y);
    append_samples.push_back(static_cast<double>(point_timer.nsecsElapsed()) / 1e6);
    root_window->update();
    QEventLoop input_tick;
    QTimer::singleShot(static_cast<int>(std::max<qint64>(1, 16 - point_timer.elapsed())),
        &input_tick, &QEventLoop::quit);
    input_tick.exec();
  }
  if (editor_preview.activeMaskPolygonPoints() != expected_points)
    return fail(QStringLiteral("Lasso lost or changed raw pointer coordinates."));
  const auto open_mask = editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap();
  if (open_mask.value("lassoRawPoints").toList() != expected_points ||
      open_mask.value("lassoClosed").toBool())
    return fail(QStringLiteral("Live lasso metadata is inconsistent."));
  log_summary(QStringLiteral("lasso_point_controller"), summarize_samples(append_samples),
      point_count, {{"photo_iteration", iteration}, {"gesture_ms", gesture_timer.elapsed()},
                   {"frame_swaps", frame_swap_count - first_frame}});
  save_capture_if_requested(root_window, capture_dir,
      QStringLiteral("lasso-open-%1").arg(iteration), &captures);
  QElapsedTimer commit_timer;
  commit_timer.start();
  if (!editor_preview.closeActiveLassoPath() ||
      !QMetaObject::invokeMethod(workspace, "clearTransientMaskTools"))
    return fail(QStringLiteral("Lasso closure failed."));
  const auto closed_stack = editor_preview.maskStack();
  const auto closed_mask = closed_stack.at(editor_preview.activeMaskIndex()).toMap();
  if (!closed_mask.value("lassoClosed").toBool() ||
      closed_mask.value("lassoRawPoints").toList() != expected_points)
    return fail(QStringLiteral("Lasso closure changed the raw path."));
  log_stage(QStringLiteral("lasso_commit_controller"), commit_timer.elapsed(), iteration,
      {{"raw_points", point_count}, {"closed_points", editor_preview.activeMaskPolygonPointCount()},
       {"geometry_sha256", QString::fromLatin1(QCryptographicHash::hash(
           QJsonDocument::fromVariant(closed_mask.value("polygonPoints")).toJson(QJsonDocument::Compact),
           QCryptographicHash::Sha256).toHex())}});
  editor_preview.undo();
  if (!wait_for_condition([&] { return editor_preview.maskCount() == 0; }, timeout_ms))
    return fail(QStringLiteral("One undo did not remove the complete lasso gesture."));
  editor_preview.redo();
  if (!wait_for_condition([&] { return editor_preview.maskStack() == closed_stack; }, timeout_ms))
    return fail(QStringLiteral("Redo did not restore the exact lasso stack."));
  log_stage(QStringLiteral("lasso_contracts_passed"), 0, iteration,
      {{"exact_raw_path", true}, {"single_undo", true}, {"exact_redo", true}});
  continue;
}

bool run_timeline_graphics_track_reorder_workflow(
    const QString& workspace_path) {
  const auto check = [](bool condition, const char* message) {
    if (!condition) {
      std::cerr << "timeline_graphics_track_reorder: " << message << '\n';
    }
    return condition;
  };

  TimelineDocumentController controller;
  controller.setEditAccessPolicy([] { return true; });
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"),
       QStringLiteral("Graphics track reorder workflow")},
      {QStringLiteral("width"), 128},
      {QStringLiteral("height"), 96},
      {QStringLiteral("frameRate"), 24.0},
  });

  const QVector<QColor> colors{
      QColor(QStringLiteral("#ffff0000")),
      QColor(QStringLiteral("#ff00ff00")),
      QColor(QStringLiteral("#ff0000ff")),
  };
  QHash<QString, QColor> color_by_track;
  for (qsizetype index = 0; index < colors.size(); ++index) {
    const auto created = controller.createTimelineTrackForAutomation(
        QStringLiteral("graphics"),
        QStringLiteral("Stack layer %1").arg(index + 1), QString{});
    const auto track_id = created.value(QStringLiteral("trackId")).toString();
    if (!check(created.value(QStringLiteral("ok")).toBool() &&
                   !track_id.isEmpty(),
               "native graphics tracks should be created with stable IDs")) {
      return false;
    }
    const auto graphic = controller.createTimelineGraphicForAutomation(
        track_id, QStringLiteral("rectangle"), 0.0, 1.0,
        QVariantMap{
            {QStringLiteral("positionX"), 0.5},
            {QStringLiteral("positionY"), 0.5},
            {QStringLiteral("shapeWidth"), 1.0},
            {QStringLiteral("shapeHeight"), 1.0},
            {QStringLiteral("shapeStrokeWidth"), 0.0},
            {QStringLiteral("backgroundPadding"), 0.0},
            {QStringLiteral("backgroundColor"), colors.at(index).name(QColor::HexArgb)},
        });
    if (!check(graphic.value(QStringLiteral("ok")).toBool(),
               "each graphics layer should hold one full-canvas native color shape")) {
      return false;
    }
    color_by_track.insert(track_id, colors.at(index));
  }

  const auto graphics_order = [&]() {
    QVector<QVariantMap> rows;
    for (const auto& value : controller.timelineTracks()) {
      const auto row = value.toMap();
      if (row.value(QStringLiteral("trackRole")).toString() ==
          QStringLiteral("graphics")) {
        rows.push_back(row);
      }
    }
    std::sort(rows.begin(), rows.end(), [](const QVariantMap& left,
                                           const QVariantMap& right) {
      return left.value(QStringLiteral("laneIndex")).toInt() <
             right.value(QStringLiteral("laneIndex")).toInt();
    });
    return rows;
  };
  const auto id_at = [](const QVector<QVariantMap>& rows, qsizetype index) {
    return rows.at(index).value(QStringLiteral("trackId")).toString();
  };
  const auto export_center_pixel = [&](const QString& pattern,
                                       QColor* color) {
    QString render_error;
    if (!render_timeline_image_sequence(controller, pattern, &render_error)) {
      std::cerr << "timeline_graphics_track_reorder: stack render failed: "
                << render_error.toStdString() << '\n';
      return false;
    }
    const QImage frame(timeline_image_sequence_frame(pattern, 1));
    if (frame.isNull() || frame.size() != QSize(128, 96)) {
      std::cerr << "timeline_graphics_track_reorder: rendered stack frame is missing or has the wrong dimensions\n";
      return false;
    }
    *color = frame.pixelColor(frame.width() / 2, frame.height() / 2);
    return true;
  };
  const auto matches_native_color = [](const QColor& actual,
                                       const QColor& expected) {
    return qAbs(actual.red() - expected.red()) <= 8 &&
           qAbs(actual.green() - expected.green()) <= 8 &&
           qAbs(actual.blue() - expected.blue()) <= 8;
  };

  const auto initial_order = graphics_order();
  if (!check(initial_order.size() == colors.size(),
             "graphics stack readback should contain all three native layers")) {
    return false;
  }
  QColor initial_pixel;
  if (!export_center_pixel(
          QDir(workspace_path).filePath(
              QStringLiteral("graphics-track-order-before-%06d.png")),
          &initial_pixel) ||
      !check(matches_native_color(initial_pixel,
                                  color_by_track.value(id_at(
                                      initial_order, initial_order.size() - 1))),
             "the highest graphics lane should composite above the lower lanes")) {
    return false;
  }

  const auto source_id = id_at(initial_order, initial_order.size() - 1);
  const auto anchor_id = id_at(initial_order, 0);
  const auto media_track_id = [&]() {
    for (const auto& value : controller.timelineTracks()) {
      const auto row = value.toMap();
      if (row.value(QStringLiteral("trackRole")).toString() ==
          QStringLiteral("media")) {
        return row.value(QStringLiteral("trackId")).toString();
      }
    }
    return QString{};
  }();
  const auto toggle_track_lock = [&](const QString& id) {
    for (const auto& value : controller.timelineTracks()) {
      const auto row = value.toMap();
      if (row.value(QStringLiteral("trackId")).toString() == id) {
        controller.toggleTrackLocked(
            row.value(QStringLiteral("trackIndex")).toInt());
        return true;
      }
    }
    return false;
  };

  const auto invalid_missing =
      controller.moveTimelineGraphicsTrackForAutomation(
          QStringLiteral("missing-track"), anchor_id);
  const auto invalid_self =
      controller.moveTimelineGraphicsTrackForAutomation(source_id, source_id);
  const auto invalid_media_anchor =
      controller.moveTimelineGraphicsTrackForAutomation(source_id,
                                                        media_track_id);
  const auto locked_source_ready = toggle_track_lock(source_id);
  const auto invalid_locked_source =
      controller.moveTimelineGraphicsTrackForAutomation(source_id, anchor_id);
  const auto unlocked_source = toggle_track_lock(source_id);
  if (!check(!invalid_missing.value(QStringLiteral("ok")).toBool() &&
                 !invalid_self.value(QStringLiteral("ok")).toBool() &&
                 !invalid_media_anchor.value(QStringLiteral("ok")).toBool() &&
                 locked_source_ready && unlocked_source &&
                 !invalid_locked_source.value(QStringLiteral("ok")).toBool() &&
                 graphics_order() == initial_order,
             "missing, self, non-graphics, and locked tracks must reject without changing the stack")) {
    return false;
  }

  const auto moved = controller.moveTimelineGraphicsTrackForAutomation(
      source_id, anchor_id);
  const auto reordered = graphics_order();
  const auto no_op = controller.moveTimelineGraphicsTrackForAutomation(
      source_id, anchor_id);
  if (!check(moved.value(QStringLiteral("ok")).toBool() &&
                 moved.value(QStringLiteral("changed")).toBool() &&
                 moved.value(QStringLiteral("beforeTrackId")).toString() ==
                     anchor_id &&
                 reordered.size() == initial_order.size() &&
                 id_at(reordered, 0) == source_id &&
                 id_at(reordered, 1) == anchor_id &&
                 no_op.value(QStringLiteral("ok")).toBool() &&
                 !no_op.value(QStringLiteral("changed")).toBool(),
             "a stable-ID move should place the source immediately below its anchor and recognize repeats")) {
    return false;
  }

  QColor reordered_pixel;
  if (!export_center_pixel(
          QDir(workspace_path).filePath(
              QStringLiteral("graphics-track-order-after-%06d.png")),
          &reordered_pixel) ||
      !check(matches_native_color(reordered_pixel,
                                  color_by_track.value(id_at(
                                      reordered, reordered.size() - 1))) &&
                 initial_pixel != reordered_pixel,
             "the rendered top color should follow the changed graphics-track order")) {
    return false;
  }

  controller.undo();
  if (!check(graphics_order() == initial_order,
             "one undo should restore the complete graphics-track order")) {
    return false;
  }
  controller.redo();
  if (!check(graphics_order() == reordered,
             "one redo should reapply the complete graphics-track order")) {
    return false;
  }
  return true;
}

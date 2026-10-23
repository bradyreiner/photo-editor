int run_timeline_animation_ui_proof(
    QQuickWindow* window, QObject* root,
    photo_editor::desktop::TimelineDocumentController* document,
    const QString& image_path, const QString& output_dir) {
  if (!QDir().mkpath(output_dir)) return fail("Animation proof needs an output directory");
  const auto video = QDir(output_dir).filePath("animation-source.mp4");
  if (!write_timeline_proof_video_file(image_path, video)) return fail("Animation proof needs encoded video");
  document->createTimelineWithSettings({{"timelineName", "Motion study"}, {"width", 1920},
                                        {"height", 1080}, {"frameRate", 30.0}});
  document->addClipFromGallery("video:animation", video, "Opening shot", 10.0,
                               QUrl::fromLocalFile(image_path).toString());
  document->selectClip(0);
  QVariant routed;
  QMetaObject::invokeMethod(root, "switchToWorkspaceIndex", Q_RETURN_ARG(QVariant, routed),
                            Q_ARG(QVariant, root->property("timelineWorkspaceIndex")));
  if (!wait_for_condition([&] {
        auto* workspace = active_workspace(root);
        return workspace && !root->property("workspaceTransitionActive").toBool() &&
               workspace->property("timelineEditorPageLoaded").toBool() &&
               named_item_visible(workspace, "timelineProgramTransformOverlay") &&
               named_item_visible(workspace, "timelineProgramAnimationToolbar");
      }, 30000)) return fail("Selecting a video must expose Program handles and animation controls");
  auto* workspace = active_workspace(root);
  root->setProperty("width", 1440);
  root->setProperty("height", 900);
  workspace->setProperty("sourcePanelCollapsed", true);
  workspace->setProperty("effectsPanelCollapsed", true);
  window->requestActivate();
  process_layout_events(10);
  if (!window->grabWindow().save(QDir(output_dir).filePath("video-transform-controls.png")))
    return fail("Program transform controls must be capturable");

  ulong timestamp = 10000;
  const auto pointer = [&](QQuickWindow* target, QEvent::Type type, QPointF point,
                           Qt::MouseButtons buttons) {
    QMouseEvent event(type, point, QPointF(target->mapToGlobal(point.toPoint())),
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      buttons, Qt::NoModifier);
    event.setTimestamp(timestamp++);
    QCoreApplication::sendEvent(target, &event);
    process_layout_events(2);
  };
  const auto click = [&](QQuickWindow* target, const QString& name) {
    auto* item = find_visible_named_item(target, name);
    if (!item_visible(item) || !item->isEnabled()) return false;
    const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    timestamp += static_cast<ulong>(QApplication::doubleClickInterval() + 1);
    pointer(item->window(), QEvent::MouseButtonPress, point, Qt::LeftButton);
    pointer(item->window(), QEvent::MouseButtonRelease, point, Qt::NoButton);
    return true;
  };
  for (const auto& parameter : {"frameScale", "frameOffsetX", "frameOffsetY", "frameRotationDegrees"}) {
    workspace->setProperty("curveEditorParameter", parameter);
    if (!click(window, "timelineProgramAddKeyframeButton"))
      return fail("Transform parameters must be keyframeable from Program");
  }
  document->seekSequenceSeconds(2.0);
  process_layout_events(4);
  auto* move = find_visible_named_item(workspace, "timelineProgramTransformMoveArea");
  if (!item_visible(move)) return fail("A selected video must have a draggable Program frame");
  const auto point = move->mapToScene(QPointF(move->width() / 2, move->height() / 2));
  const auto initial_hash = document->deterministicDocumentHash();
  pointer(window, QEvent::MouseButtonPress, point, Qt::LeftButton);
  pointer(window, QEvent::MouseMove, point + QPointF(50, 25), Qt::LeftButton);
  pointer(window, QEvent::MouseButtonRelease, point + QPointF(50, 25), Qt::NoButton);
  if (std::abs(document->currentClip().value("frameOffsetX").toDouble()) < 0.01)
    return fail("Dragging Program must actually move the selected video");
  document->undo();
  if (document->deterministicDocumentHash() != initial_hash)
    return fail("One Undo must restore the complete Program gesture");
  document->setSelectedClipTransform(0.25, -0.1, 0.8, 8);
  workspace->setProperty("curveEditorParameter", "frameScale");
  if (!click(window, "timelineProgramCurveButton")) return fail("Program must open the graph editor");
  auto* curve = qobject_cast<QQuickWindow*>(find_named_object(root, "timelineKeyframeCurveWindow"));
  if (!curve || !wait_for_condition([&] {
        return curve->isVisible() && named_item_visible(curve, "timelineKeyframeCurveGraph");
      }, 5000)) return fail("The graph must load in its focused tool window");
  process_layout_events(8);
  auto* graph = find_visible_named_item(curve, "timelineKeyframeCurveGraph");
  if (graph->height() < 219 || !named_item_visible(curve, "timelineCurveFitValuesButton") ||
      !named_item_visible(curve, "timelineCurveTimeAxis"))
    return fail("The graph needs readable plotting space, a time axis, and value fitting");
  const auto qml_value = [](const QVariant& value) {
    return value.metaType() == QMetaType::fromType<QJSValue>()
        ? value.value<QJSValue>().toVariant() : value;
  };
  const auto bounds = qml_value(curve->property("displayBounds")).toMap();
  if (!curve->grabWindow().save(QDir(output_dir).filePath("video-keyframe-graph.png")))
    return fail("The video graph must be capturable");
  if (!bounds.contains("maximum") ||
      bounds.value("maximum").toDouble() - bounds.value("minimum").toDouble() > 1) {
    std::cerr << "Graph bounds=" << QJsonDocument(QJsonObject::fromVariantMap(bounds)).toJson().toStdString()
              << " keys=" << QJsonDocument::fromVariant(document->currentClip().value("keyframes")).toJson().toStdString()
              << " parameter=" << workspace->property("curveEditorParameter").toString().toStdString() << '\n';
    return fail("Value fitting should make a small scale animation legible");
  }
  const auto key_count = document->currentClip().value("keyframes").toList().size();
  const auto add_point = graph->mapToScene(QPointF(graph->width() * 0.6, graph->height() * 0.6));
  timestamp += static_cast<ulong>(QApplication::doubleClickInterval() + 1);
  pointer(curve, QEvent::MouseButtonPress, add_point, Qt::LeftButton);
  pointer(curve, QEvent::MouseButtonRelease, add_point, Qt::NoButton);
  pointer(curve, QEvent::MouseButtonPress, add_point, Qt::LeftButton);
  pointer(curve, QEvent::MouseButtonDblClick, add_point, Qt::LeftButton);
  pointer(curve, QEvent::MouseButtonRelease, add_point, Qt::NoButton);
  if (document->currentClip().value("keyframes").toList().size() != key_count + 1) {
    std::cerr << "graph keys before=" << key_count << " after="
              << document->currentClip().value("keyframes").toList().size()
              << " parameter=" << workspace->property("curveEditorParameter").toString().toStdString()
              << " point=" << add_point.x() << ',' << add_point.y() << '\n';
    return fail("Double-clicking the graph should add one editable keyframe");
  }
  document->setSelectedClipKeyframeInterpolation("frameScale", 2.0, "bezier");
  QMetaObject::invokeMethod(curve, "selectCurveSecond", Q_ARG(QVariant, 2.0), Q_ARG(QVariant, false));
  process_layout_events(6);
  auto* tangent = find_visible_named_item(curve, "timelineBezierOutHandle");
  if (!item_visible(tangent)) return fail("A selected Bezier keyframe must expose its tangent handle");
  const auto tangent_point = tangent->mapToScene(QPointF(tangent->width() / 2, tangent->height() / 2));
  const auto tangent_hash = document->deterministicDocumentHash();
  pointer(curve, QEvent::MouseButtonPress, tangent_point, Qt::LeftButton);
  pointer(curve, QEvent::MouseMove, tangent_point + QPointF(20, -12), Qt::LeftButton);
  pointer(curve, QEvent::MouseMove, tangent_point + QPointF(40, -24), Qt::LeftButton);
  pointer(curve, QEvent::MouseButtonRelease, tangent_point + QPointF(40, -24), Qt::NoButton);
  if (document->deterministicDocumentHash() == tangent_hash)
    return fail("Dragging a Bezier handle must author the curve");
  if (!curve->grabWindow().save(QDir(output_dir).filePath("video-bezier-graph.png")))
    return fail("The advanced graph must be capturable");
  document->undo();
  if (document->deterministicDocumentHash() != tangent_hash)
    return fail("One Undo must restore a complete tangent gesture");
  curve->close();
  const auto select_speed = [&](int index) {
    const auto item_name = "timelineProgramSpeedMenuPopupItem" + QString::number(index);
    auto* menu = find_named_object(workspace, "timelineProgramSpeedMenu");
    QSignalSpy triggered(menu, SIGNAL(menuItemTriggered(QString)));
    if (!click(window, "timelineProgramSpeedMenuPrimaryAction") ||
        !wait_for_condition([&] {
          auto* popup = find_named_object(workspace, "timelineProgramSpeedMenuPopup");
          return popup && popup->property("opacity").toDouble() > 0.999 &&
                 named_item_visible(window, item_name);
        }, 2000) ||
        !click(window, item_name)) return false;
    const auto closed = wait_for_condition([&] {
      auto* popup = find_named_object(workspace, "timelineProgramSpeedMenuPopup");
      return popup && !popup->property("visible").toBool();
    }, 2000);
    const auto expected = index == 3 ? QString("2") : QString("0.5");
    if (triggered.size() != 1 || triggered.first().first().toString() != expected) {
      std::cerr << "Speed menu expected=" << expected.toStdString() << " activations=" << triggered.size()
                << " actual=" << (triggered.isEmpty() ? std::string("none") : triggered.first().first().toString().toStdString()) << '\n';
      return false;
    }
    return closed;
  };
  if (!select_speed(3))
    return fail("The Program speed menu must offer a working 2x action");
  if (std::abs(document->currentClip().value("trimmedDurationSeconds").toDouble() - 5.0) > 0.05)
    return fail("2x video must occupy half the source duration");
  if (!select_speed(1))
    return fail("The Program speed menu must offer a working 0.5x action");
  if (std::abs(document->currentClip().value("trimmedDurationSeconds").toDouble() - 20.0) > 0.05)
    return fail("0.5x video must occupy twice the source duration");
  document->setSelectedClipPlaybackRate(1.0);
  for (const auto& parameter : {"frameScale", "frameOffsetX", "frameOffsetY", "frameRotationDegrees"})
    document->clearSelectedClipKeyframes(parameter);
  document->setSelectedClipTransform(0, 0, 1, 0);
  document->assignSelectedClipTransition("in", "Fade to Black", 0.5);
  document->assignSelectedClipTransition("out", "Fade to White", 0.5);
  workspace->setProperty("programTransformControlsVisible", false);
  if (window->rendererInterface()->graphicsApi() != QSGRendererInterface::Software) {
    auto* monitor = find_visible_named_item(workspace, "timelineProgramMonitor");
    if (!monitor) return fail("Fade preview needs the actual Program monitor");
    for (const auto& color : {QString("black"), QString("white")}) {
      document->seekSequenceSeconds(color == "black" ? 0.0 : 9.9666667);
      process_layout_events(12);
      const auto frame = window->grabWindow();
      const auto center = monitor->mapToScene(QPointF(monitor->width() / 2, monitor->height() / 2));
      const auto pixel = frame.pixelColor(qBound(0, qRound(center.x() * frame.width() / window->width()), frame.width() - 1),
                                          qBound(0, qRound(center.y() * frame.height() / window->height()), frame.height() - 1));
      frame.save(QDir(output_dir).filePath("program-fade-" + color + ".png"));
      if ((color == "black" && std::max({pixel.red(), pixel.green(), pixel.blue()}) > 12) ||
          (color == "white" && std::min({pixel.red(), pixel.green(), pixel.blue()}) < 230)) {
        std::cerr << "Program fade " << color.toStdString() << " pixel=" << pixel.name().toStdString() << '\n';
        return fail("Native Program must display the authored black and white fades");
      }
    }
  }
  document->clearSelectedClipTransition("in");
  document->clearSelectedClipTransition("out");
  workspace->setProperty("programTransformControlsVisible", true);
  document->seekSequenceSeconds(0.0);
  document->addTitleClip();
  document->updateSelectedTitle({{"text", "Made to move"}});
  document->setSelectedTitlePositionX(0.25);
  document->toggleSelectedClipKeyframe("titlePositionX");
  document->toggleSelectedClipKeyframe("opacity");
  const auto title_start = document->currentClip().value("startSeconds").toDouble();
  document->seekSequenceSeconds(title_start + 1.0);
  document->setSelectedTitlePositionX(0.65);
  document->setSelectedClipOpacity(0.0);
  process_layout_events(6);
  auto* opacity = find_named_object(workspace, "timelineProgramOpacityField");
  if (!opacity || opacity->property("text").toString() != "0%")
    return fail("Zero text opacity must be shown accurately");
  workspace->setProperty("curveEditorParameter", "titlePositionX");
  if (!click(window, "timelineProgramCurveButton")) return fail("Text position must open the same graph editor");
  QSignalSpy text_graph_frames(curve, &QQuickWindow::frameSwapped);
  curve->requestUpdate();
  if (!wait_for_condition([&] { return curve->isExposed() && !text_graph_frames.isEmpty(); }, 5000))
    return fail("The reopened text graph must expose and render before native readback");
  process_layout_events(4);
  if (qml_value(curve->property("curveKeyframes")).toList().size() != 2 ||
      !curve->grabWindow().save(QDir(output_dir).filePath("text-keyframe-graph.png")))
    return fail("Text position keyframes must use the shared graph");
  curve->close();
  document->seekSequenceSeconds(title_start);
  process_layout_events(4);
  if (!window->grabWindow().save(QDir(output_dir).filePath("text-animation-controls.png")))
    return fail("Text animation controls must be capturable");
  root->setProperty("width", 1180);
  root->setProperty("height", 768);
  workspace->setProperty("sourcePanelCollapsed", false);
  workspace->setProperty("effectsPanelCollapsed", false);
  if (!wait_for_condition([&] {
        return !named_item_visible(workspace, "timelineInspectorPanelLoading");
      }, 15000)) return fail("The narrow layout must finish loading its Inspector");
  process_layout_events(10);
  auto* toolbar = find_visible_named_item(workspace, "timelineProgramAnimationToolbar");
  if (!item_visible(toolbar) || toolbar->height() < 25)
    return fail("The animation toolbar must retain usable height in a narrow Program panel");
  for (auto* child : toolbar->childItems()) {
    if (child->isVisible() && child->width() > 0 &&
        (child->x() + child->width() > toolbar->width() + 1 ||
         child->y() + child->height() > toolbar->height() + 1))
      return fail("Animation controls must wrap inside a narrow Program panel");
  }
  if (!window->grabWindow().save(QDir(output_dir).filePath("text-animation-1180.png")))
    return fail("The narrow animation layout must be capturable");
  std::cout << "Timeline animation UI proof passed: Program transforms, keyframe graph, text, opacity, and speed\n";
  return EXIT_SUCCESS;
}

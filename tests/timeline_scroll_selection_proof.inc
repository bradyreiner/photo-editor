struct TimelineSelectionPointerDiagnostics final {
  QString phase;
  QRectF item_scene_rect;
  QRectF viewport_scene_rect;
  QRectF window_scene_rect;
  QRectF visible_intersection;
  QRectF candidate_body_scene_rect;
  QRectF visible_body_intersection;
  QVector<QJsonObject> clipped_ancestor_rects;
  QVector<QJsonObject> overlay_exclusions;
  QVector<QRectF> overlay_exclusion_scene_rects;
  qreal trim_handle_width = 0.0;
  qreal trim_safety_inset = 2.0;
  QPoint chosen_window_point;
  QPointF chosen_scene_point;
  QPointF chosen_item_point;
  bool chosen_point_valid = false;
};

QJsonObject timeline_selection_rect_json(const QRectF& rect) {
  return {{QStringLiteral("x"), rect.x()},
          {QStringLiteral("y"), rect.y()},
          {QStringLiteral("width"), rect.width()},
          {QStringLiteral("height"), rect.height()},
          {QStringLiteral("right"), rect.right()},
          {QStringLiteral("bottom"), rect.bottom()}};
}

QJsonObject timeline_selection_point_json(const QPointF& point) {
  return {{QStringLiteral("x"), point.x()}, {QStringLiteral("y"), point.y()}};
}

QString save_timeline_selection_pointer_failure_capture(
    QQuickWindow* window,
    const TimelineScrollBenchmarkConfig& config,
    const QString& phase) {
  if (window == nullptr || config.cache_dir.isEmpty() ||
      !QDir().mkpath(config.cache_dir))
    return {};
  const auto path = QDir(config.cache_dir).filePath(
      QStringLiteral("timeline-scroll-selection-%1-%2.png")
          .arg(phase, QUuid::createUuid().toString(QUuid::WithoutBraces)));
  const auto image = window->grabWindow();
  return !image.isNull() && image.save(path, "PNG") ? path : QString{};
}

void log_timeline_selection_pointer_failure(
    QQuickWindow* window,
    const TimelineScrollBenchmarkConfig& config,
    const TimelineSelectionPointerDiagnostics& diagnostics,
    const QString& failure,
    const QJsonObject& extra = {}) {
  QJsonArray ancestor_clips;
  for (const auto& clip : diagnostics.clipped_ancestor_rects)
    ancestor_clips.push_back(clip);
  QJsonArray overlay_exclusions;
  for (const auto& exclusion : diagnostics.overlay_exclusions)
    overlay_exclusions.push_back(exclusion);
  const auto capture_path = save_timeline_selection_pointer_failure_capture(
      window, config, diagnostics.phase);
  QJsonObject report{
      {QStringLiteral("phase"), diagnostics.phase},
      {QStringLiteral("failure"), failure},
      {QStringLiteral("itemSceneRect"), timeline_selection_rect_json(diagnostics.item_scene_rect)},
      {QStringLiteral("viewportSceneRect"), timeline_selection_rect_json(diagnostics.viewport_scene_rect)},
      {QStringLiteral("windowSceneRect"), timeline_selection_rect_json(diagnostics.window_scene_rect)},
      {QStringLiteral("visibleIntersectionSceneRect"), timeline_selection_rect_json(diagnostics.visible_intersection)},
      {QStringLiteral("candidateBodySceneRect"), timeline_selection_rect_json(diagnostics.candidate_body_scene_rect)},
      {QStringLiteral("visibleBodyIntersectionSceneRect"), timeline_selection_rect_json(diagnostics.visible_body_intersection)},
      {QStringLiteral("clippedAncestorRects"), ancestor_clips},
      {QStringLiteral("overlayExclusions"), overlay_exclusions},
      {QStringLiteral("trimHandleWidth"), diagnostics.trim_handle_width},
      {QStringLiteral("trimSafetyInset"), diagnostics.trim_safety_inset},
      {QStringLiteral("chosenPointValid"), diagnostics.chosen_point_valid},
      {QStringLiteral("chosenWindowPoint"),
       QJsonObject{{QStringLiteral("x"), diagnostics.chosen_window_point.x()},
                   {QStringLiteral("y"), diagnostics.chosen_window_point.y()}}},
      {QStringLiteral("chosenScenePoint"), timeline_selection_point_json(diagnostics.chosen_scene_point)},
      {QStringLiteral("chosenItemLocalPoint"), timeline_selection_point_json(diagnostics.chosen_item_point)},
      {QStringLiteral("windowFailurePng"), capture_path},
      {QStringLiteral("windowFailurePngSaved"), !capture_path.isEmpty()},
  };
  for (auto iterator = extra.constBegin(); iterator != extra.constEnd(); ++iterator)
    report.insert(iterator.key(), iterator.value());
  std::cerr << "TIMELINE_SELECTION_POINTER_FAILURE "
            << QJsonDocument(report).toJson(QJsonDocument::Compact).constData() << '\n';
}

bool timeline_selection_pointer_click_in_viewport(
    QQuickItem* item,
    QQuickItem* viewport,
    QQuickWindow* window,
    QObject* workspace,
    const TimelineScrollBenchmarkConfig& config,
    const QString& phase,
    TimelineSelectionPointerDiagnostics* output_diagnostics) {
  TimelineSelectionPointerDiagnostics diagnostics;
  diagnostics.phase = phase;
  const auto fail_geometry = [&](const QString& reason) {
    if (output_diagnostics != nullptr) *output_diagnostics = diagnostics;
    log_timeline_selection_pointer_failure(window, config, diagnostics, reason);
    return false;
  };
  if (item == nullptr || viewport == nullptr || window == nullptr || workspace == nullptr ||
      item->window() != window || viewport->window() != window ||
      !item_visible(item) || !item->property("enabled").toBool() ||
      !item_visible(viewport) || !window->isExposed()) {
    return fail_geometry(QStringLiteral("pointerTargetOrViewportUnavailable"));
  }

  diagnostics.item_scene_rect = item_scene_rect(item);
  diagnostics.viewport_scene_rect = item_scene_rect(viewport);
  diagnostics.window_scene_rect = window->contentItem() == nullptr
      ? QRectF(0.0, 0.0, static_cast<qreal>(window->width()),
              static_cast<qreal>(window->height()))
      : item_scene_rect(window->contentItem());
  diagnostics.visible_intersection = diagnostics.item_scene_rect
      .intersected(diagnostics.viewport_scene_rect)
      .intersected(diagnostics.window_scene_rect);

  QVector<QQuickItem*> clipped_ancestors;
  for (auto* ancestor = item; ancestor != nullptr; ancestor = ancestor->parentItem()) {
    if (!ancestor->isVisible())
      return fail_geometry(QStringLiteral("pointerTargetHasHiddenAncestor"));
    if (!ancestor->clip()) continue;
    const auto clip_scene_rect = item_scene_rect(ancestor);
    clipped_ancestors.push_back(ancestor);
    diagnostics.clipped_ancestor_rects.push_back({
        {QStringLiteral("objectName"), ancestor->objectName()},
        {QStringLiteral("sceneRect"), timeline_selection_rect_json(clip_scene_rect)},
    });
    diagnostics.visible_intersection =
        diagnostics.visible_intersection.intersected(clip_scene_rect);
  }
  if (!diagnostics.visible_intersection.isValid() ||
      diagnostics.visible_intersection.isEmpty()) {
    return fail_geometry(QStringLiteral("targetHasNoVisibleSceneIntersection"));
  }

  const auto trim_handle_property = item->property("trimHandleWidth");
  if (trim_handle_property.isValid()) {
    const auto requested_trim_width = trim_handle_property.toDouble();
    if (std::isfinite(requested_trim_width))
      diagnostics.trim_handle_width = qMax(0.0, requested_trim_width);
  }
  const qreal trim_exclusion_width =
      diagnostics.trim_handle_width + diagnostics.trim_safety_inset;
  if (item->width() <= trim_exclusion_width * 2.0) {
    return fail_geometry(QStringLiteral("clipHasNoSafeBodyBetweenTrimEdges"));
  }
  const QRectF item_local_bounds(0.0, 0.0, item->width(), item->height());
  const QRectF safe_body_local_bounds(
      trim_exclusion_width, 0.0,
      item->width() - trim_exclusion_width * 2.0, item->height());
  diagnostics.candidate_body_scene_rect = item->mapRectToScene(safe_body_local_bounds);
  diagnostics.visible_body_intersection = diagnostics.candidate_body_scene_rect
      .intersected(diagnostics.viewport_scene_rect)
      .intersected(diagnostics.window_scene_rect);
  for (auto* ancestor = item; ancestor != nullptr; ancestor = ancestor->parentItem()) {
    if (ancestor->clip()) {
      diagnostics.visible_body_intersection = diagnostics.visible_body_intersection
          .intersected(item_scene_rect(ancestor));
    }
  }

  const auto left_trim_scene_rect = item->mapRectToScene(
      QRectF(0.0, 0.0, trim_exclusion_width, item->height()));
  const auto right_trim_scene_rect = item->mapRectToScene(
      QRectF(item->width() - trim_exclusion_width, 0.0,
             trim_exclusion_width, item->height()));
  diagnostics.overlay_exclusion_scene_rects.push_back(left_trim_scene_rect);
  diagnostics.overlay_exclusions.push_back({
      {QStringLiteral("objectName"), QStringLiteral("timelineClipLeftTrimHandle")},
      {QStringLiteral("kind"), QStringLiteral("clipTrimEdge")},
      {QStringLiteral("sceneRect"), timeline_selection_rect_json(left_trim_scene_rect)},
  });
  diagnostics.overlay_exclusion_scene_rects.push_back(right_trim_scene_rect);
  diagnostics.overlay_exclusions.push_back({
      {QStringLiteral("objectName"), QStringLiteral("timelineClipRightTrimHandle")},
      {QStringLiteral("kind"), QStringLiteral("clipTrimEdge")},
      {QStringLiteral("sceneRect"), timeline_selection_rect_json(right_trim_scene_rect)},
  });

  const auto playhead_items = find_named_items(workspace, QStringLiteral("timelinePlayheadHitTarget"));
  if (playhead_items.isEmpty()) {
    diagnostics.overlay_exclusions.push_back({
        {QStringLiteral("objectName"), QStringLiteral("timelinePlayheadHitTarget")},
        {QStringLiteral("kind"), QStringLiteral("playheadHitTarget")},
        {QStringLiteral("found"), false},
    });
  }
  for (auto* playhead_item : playhead_items) {
    const auto is_visible_playhead = item_visible(playhead_item) &&
        playhead_item->window() == window;
    const auto playhead_scene_rect = item_scene_rect(playhead_item);
    diagnostics.overlay_exclusions.push_back({
        {QStringLiteral("objectName"), QStringLiteral("timelinePlayheadHitTarget")},
        {QStringLiteral("kind"), QStringLiteral("playheadHitTarget")},
        {QStringLiteral("visibleInWindow"), is_visible_playhead},
        {QStringLiteral("sceneRect"), timeline_selection_rect_json(playhead_scene_rect)},
    });
    if (is_visible_playhead)
      diagnostics.overlay_exclusion_scene_rects.push_back(playhead_scene_rect);
  }
  for (auto* scroll_bar : find_named_items(workspace, QStringLiteral("timelineHorizontalScrollBar"))) {
    if (item_visible(scroll_bar) && scroll_bar->window() == window) {
      const auto scroll_rect = item_scene_rect(scroll_bar);
      diagnostics.overlay_exclusion_scene_rects.push_back(scroll_rect);
      diagnostics.overlay_exclusions.push_back({
          {QStringLiteral("objectName"), QStringLiteral("timelineHorizontalScrollBar")},
          {QStringLiteral("kind"), QStringLiteral("scrollBar")},
          {QStringLiteral("sceneRect"), timeline_selection_rect_json(scroll_rect)},
      });
    }
  }
  diagnostics.visible_body_intersection = diagnostics.visible_body_intersection
      .intersected(diagnostics.visible_intersection);
  if (!diagnostics.visible_body_intersection.isValid() ||
      diagnostics.visible_body_intersection.isEmpty()) {
    return fail_geometry(QStringLiteral("clipBodyHasNoVisibleAreaAfterViewportAndAncestorClipping"));
  }

  // Prefer the center, then interior points in a small grid. The grid keeps
  // the click away from clipped boundaries and also handles transformed clip
  // ancestors whose scene bounding rectangles contain nonvisible corners.
  const QRectF candidate_area = diagnostics.visible_body_intersection.adjusted(
      0.5, 0.5, -0.5, -0.5);
  const QRectF& safe_area = candidate_area.isValid() && !candidate_area.isEmpty()
      ? candidate_area : diagnostics.visible_body_intersection;
  const QVector<QPointF> fractions{
      QPointF(0.5, 0.5), QPointF(0.25, 0.5), QPointF(0.75, 0.5),
      QPointF(0.5, 0.25), QPointF(0.5, 0.75), QPointF(0.25, 0.25),
      QPointF(0.75, 0.25), QPointF(0.25, 0.75), QPointF(0.75, 0.75),
  };
  for (const auto& fraction : fractions) {
    const QPointF scene_candidate(
        safe_area.left() + safe_area.width() * fraction.x(),
        safe_area.top() + safe_area.height() * fraction.y());
    const auto window_point = scene_candidate.toPoint();
    const QPointF snapped_scene_point(window_point);
    if (!diagnostics.window_scene_rect.contains(snapped_scene_point) ||
        !diagnostics.viewport_scene_rect.contains(snapped_scene_point) ||
        !diagnostics.visible_intersection.contains(snapped_scene_point) ||
        !diagnostics.visible_body_intersection.contains(snapped_scene_point))
      continue;
    const auto viewport_local_point = viewport->mapFromScene(snapped_scene_point);
    if (!QRectF(0.0, 0.0, viewport->width(), viewport->height())
             .contains(viewport_local_point))
      continue;
    if (window->contentItem() != nullptr) {
      const auto window_local_point = window->contentItem()->mapFromScene(snapped_scene_point);
      if (!QRectF(0.0, 0.0, window->contentItem()->width(),
                  window->contentItem()->height()).contains(window_local_point))
        continue;
    }
    const auto item_local_point = item->mapFromScene(snapped_scene_point);
    if (!item_local_bounds.contains(item_local_point) ||
        item_local_point.x() < trim_exclusion_width ||
        item_local_point.x() > item->width() - trim_exclusion_width)
      continue;
    bool inside_overlay_exclusion = false;
    for (const auto& exclusion : diagnostics.overlay_exclusion_scene_rects) {
      if (exclusion.contains(snapped_scene_point)) {
        inside_overlay_exclusion = true;
        break;
      }
    }
    if (inside_overlay_exclusion) continue;

    bool inside_all_clip_ancestors = true;
    for (auto* ancestor : clipped_ancestors) {
      const auto ancestor_local_point = ancestor->mapFromScene(snapped_scene_point);
      if (!QRectF(0.0, 0.0, ancestor->width(), ancestor->height())
               .contains(ancestor_local_point)) {
        inside_all_clip_ancestors = false;
        break;
      }
    }
    if (!inside_all_clip_ancestors) continue;

    diagnostics.chosen_window_point = window_point;
    diagnostics.chosen_scene_point = snapped_scene_point;
    diagnostics.chosen_item_point = item_local_point;
    diagnostics.chosen_point_valid = true;
    break;
  }
  if (!diagnostics.chosen_point_valid)
    return fail_geometry(QStringLiteral("visibleIntersectionHasNoInBoundsPointerPoint"));
  if (output_diagnostics != nullptr) *output_diagnostics = diagnostics;

  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    diagnostics.chosen_window_point);
  process_layout_events(5, phase.toUtf8().constData());
  return true;
}

int run_timeline_scroll_selection_proof(
    QObject* root,
    QQuickWindow* window,
    QObject* workspace,
    photo_editor::desktop::TimelineDocumentController* document,
    const TimelineScrollBenchmarkConfig& config,
    const QString& source_sha256_before) {
  if (root == nullptr || window == nullptr || workspace == nullptr || document == nullptr ||
      !window->isExposed()) {
    return fail(QStringLiteral(
        "Timeline selection proof requires the exposed full Timeline workspace and document."));
  }

  struct Target final {
    QVariantMap track;
    QVariantMap clip;
    int track_index = -1;
    double start_seconds = 0.0;
    double duration_seconds = 0.0;
    QString clip_id;
    QString display_label;
    QString media_type;
  };
  const auto tracks = document->timelineTracks();
  QHash<QString, int> label_counts;
  QVector<Target> candidates;
  for (int track_index = 0; track_index < tracks.size(); ++track_index) {
    const auto track = tracks.at(track_index).toMap();
    const auto track_kind = track.value(QStringLiteral("trackKind")).toString();
    if (track_kind != QStringLiteral("video") ||
        track.value(QStringLiteral("locked")).toBool() ||
        track.value(QStringLiteral("visible"), true).toBool() == false) {
      continue;
    }
    for (const auto& clip_value : track.value(QStringLiteral("clips")).toList()) {
      const auto clip = clip_value.toMap();
      const auto media_type = clip.value(QStringLiteral("mediaType")).toString();
      const auto clip_id = clip.value(QStringLiteral("clipId")).toString();
      const auto display_label = clip.value(QStringLiteral("displayLabel")).toString().trimmed();
      const auto start_seconds = clip.value(QStringLiteral("startSeconds")).toDouble();
      const auto duration_seconds = clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble();
      if ((media_type != QStringLiteral("video") && media_type != QStringLiteral("photo")) ||
          clip_id.isEmpty() || display_label.isEmpty() || !std::isfinite(start_seconds) ||
          !std::isfinite(duration_seconds) || duration_seconds < 0.5) {
        continue;
      }
      ++label_counts[display_label];
      auto clip_with_track = clip;
      clip_with_track.insert(QStringLiteral("trackId"),
                             track.value(QStringLiteral("trackId")).toString());
      candidates.push_back(Target{track, clip_with_track, track_index, start_seconds,
                                  duration_seconds, clip_id, display_label, media_type});
    }
  }
  std::sort(candidates.begin(), candidates.end(), [](const Target& first, const Target& second) {
    return first.duration_seconds > second.duration_seconds;
  });
  const auto target_iterator = std::find_if(
      candidates.cbegin(), candidates.cend(), [&](const Target& target) {
        return label_counts.value(target.display_label) == 1;
      });
  if (target_iterator == candidates.cend()) {
    return fail(QStringLiteral(
        "Timeline selection proof could not find a uniquely labeled video/photo clip of at least 0.5 seconds."));
  }
  const auto target = *target_iterator;
  const auto target_track_id = target.track.value(QStringLiteral("trackId")).toString();
  if (target_track_id.isEmpty())
    return fail(QStringLiteral("Timeline selection proof target has no track identity."));

  // Recovery may leave an unselected first clip as the Program primary even
  // when it is outside the playhead. Settle that read-only view before taking
  // the baseline: seeking later reconstructs the Program primary at this time.
  const auto recovered_primary_index = document->currentIndex();
  const bool settled_unselected_program = document->selectedClipIds().isEmpty();
  if (settled_unselected_program) {
    document->selectClip(-1);
    document->seekSequenceSeconds(document->sequencePlayheadSeconds());
    process_layout_events(5, "timeline-scroll-selection-settle-program");
  }

  auto* viewport = find_visible_named_item(workspace, QStringLiteral("timelineViewport"));
  auto* dock_state = find_named_object(workspace, QStringLiteral("timelinePanelDockState"));
  auto* workspace_settings = find_named_object(workspace, QStringLiteral("timelineWorkspaceSettings"));
  if (viewport == nullptr || dock_state == nullptr || workspace_settings == nullptr)
    return fail(QStringLiteral("Timeline selection proof could not find the viewport or dock state."));

  QVariant dock_snapshot_value;
  if (!timeline_discoverable_invoke(dock_state, "snapshot", {}, &dock_snapshot_value))
    return fail(QStringLiteral("Timeline selection proof could not snapshot its original panel layout."));
  const auto dock_snapshot_json = QString::fromUtf8(
      QJsonDocument::fromVariant(dock_snapshot_value).toJson(QJsonDocument::Compact));
  if (dock_snapshot_json.isEmpty())
    return fail(QStringLiteral("Timeline selection proof received an empty panel snapshot."));

  const auto initial_hash = document->deterministicDocumentHash();
  const auto initial_track_count = document->timelineTracks().size();
  const auto initial_clip_count = document->clipCount();
  const auto initial_duration = document->totalDurationSecondsValue();
  const auto initial_dirty = document->dirty();
  const auto initial_selection = document->selectedClipIds();
  const auto initial_current_index = document->property("currentIndex").toInt();
  const auto initial_playhead = document->sequencePlayheadSeconds();
  const auto initial_tool = document->activeTool();
  const auto initial_zoom = workspace->property("timelineZoom").toDouble();
  const auto initial_content_x = viewport->property("contentX").toDouble();
  const auto initial_content_y = viewport->property("contentY").toDouble();
  const auto initial_inspector_tab = workspace->property("inspectorTab");
  const auto initial_inspector_collapsed = workspace->property("effectsPanelCollapsed");
  const auto initial_inspected_track = workspace->property("inspectedTimelineTrackId");
  const auto initial_settings_scroll_x = workspace_settings->property("timelineScrollX");
  const auto initial_settings_scroll_y = workspace_settings->property("timelineScrollY");
  const auto initial_settings_zoom = workspace_settings->property("timelineZoom");
  const auto initial_settings_panel_snapshot = workspace_settings->property("panelDockSnapshotJson");
  const auto initial_settings_tab = workspace_settings->property("inspectorTab");
  const auto initial_settings_tabs_by_type =
      workspace_settings->property("inspectorTabsByTypeJson");
  const auto initial_recent_user_input = root->property("recentUserInput");
  const auto initial_source_file_sha256 = sha256_file(config.project_path);
  const auto initial_working_copy_sha256 = sha256_file(config.load_path);

  bool restored = false;
  const auto restore_state = [&]() {
    // Restore the authored selection first. Seeking to the saved playhead then
    // reconstructs the primary active clip/index without turning an initially
    // empty selectedClipIds list into a new explicit selection.
    document->selectClipsByIds(initial_selection);
    if (document->activeTool() != initial_tool)
      document->setActiveTool(initial_tool);
    document->seekSequenceSeconds(initial_playhead);
    workspace->setProperty("inspectedTimelineTrackId", initial_inspected_track);
    workspace->setProperty("inspectorTab", initial_inspector_tab);
    workspace->setProperty("effectsPanelCollapsed", initial_inspector_collapsed);
    const bool dock_restored = timeline_discoverable_invoke_without_result(
        dock_state, "restore", {dock_snapshot_json});
    workspace->setProperty("timelineZoom", initial_zoom);
    process_layout_events(5, "timeline-scroll-selection-restore-zoom");
    viewport->setProperty("contentX", initial_content_x);
    viewport->setProperty("contentY", initial_content_y);
    process_layout_events(5, "timeline-scroll-selection-restore-viewport");
    workspace_settings->setProperty("timelineScrollX", initial_settings_scroll_x);
    workspace_settings->setProperty("timelineScrollY", initial_settings_scroll_y);
    workspace_settings->setProperty("timelineZoom", initial_settings_zoom);
    workspace_settings->setProperty("panelDockSnapshotJson", initial_settings_panel_snapshot);
    workspace_settings->setProperty("inspectorTab", initial_settings_tab);
    workspace_settings->setProperty("inspectorTabsByTypeJson", initial_settings_tabs_by_type);
    root->setProperty("recentUserInput", initial_recent_user_input);
    return dock_restored;
  };
  const auto restore_guard = qScopeGuard([&] {
    if (!restored)
      restore_state();
  });

  auto panel_active = [&](const QString& dock_id, const QString& expected_panel) {
    QVariant active;
    return timeline_discoverable_invoke(
               dock_state, "activePanel", {dock_id}, &active) &&
           active.toString() == expected_panel;
  };
  auto focus_panel = [&](const QString& panel_id) {
    QVariant result;
    return timeline_discoverable_invoke(
               dock_state, "focusPanel", {panel_id}, &result) && result.toBool();
  };
  auto dock_is = [&](const QString& panel_id, const QString& expected_dock) {
    QVariant actual;
    return timeline_discoverable_invoke(
               dock_state, "dock", {panel_id}, &actual) && actual.toString() == expected_dock;
  };
  auto focus_library_and_effects = [&] {
    return focus_panel(QStringLiteral("media")) &&
           focus_panel(QStringLiteral("effects")) &&
           panel_active(QStringLiteral("left"), QStringLiteral("media")) &&
           panel_active(QStringLiteral("right"), QStringLiteral("effects"));
  };
  auto controls_and_effects_are_active = [&] {
    return dock_is(QStringLiteral("inspector"), QStringLiteral("left")) &&
           panel_active(QStringLiteral("left"), QStringLiteral("inspector")) &&
           dock_is(QStringLiteral("effects"), QStringLiteral("right")) &&
           panel_active(QStringLiteral("right"), QStringLiteral("effects")) &&
           named_item_visible(workspace, QStringLiteral("timelineEffectsRail")) &&
           named_item_visible(workspace, QStringLiteral("timelineEffectsLibraryPanel"));
  };
  const auto clip_item_for_target = [&]() -> QQuickItem* {
    const auto object_name = QStringLiteral("timelineClip-") + target.display_label;
    for (auto* item : find_named_items(workspace, object_name)) {
      if (!item_visible(item))
        continue;
      const auto model = item->property("modelData").toMap();
      if (model.value(QStringLiteral("clipId")).toString() == target.clip_id)
        return item;
    }
    return nullptr;
  };
  const auto track_id_of_header_item = [](QQuickItem* item) {
    for (auto* ancestor = item; ancestor != nullptr; ancestor = ancestor->parentItem()) {
      const auto track_id = ancestor->property("trackData").toMap()
                                .value(QStringLiteral("trackId")).toString();
      if (!track_id.isEmpty())
        return track_id;
    }
    return QString{};
  };
  const auto track_header_item = [&]() -> QQuickItem* {
    for (const auto& object_name : {QStringLiteral("timelineTrackCompactNameHitTarget"),
                                    QStringLiteral("timelineTrackHeaderName")}) {
      for (auto* item : find_named_items(workspace, object_name)) {
        if (item_visible(item) && track_id_of_header_item(item) == target_track_id)
          return item;
      }
    }
    return nullptr;
  };

  if (!focus_library_and_effects())
    return fail(QStringLiteral("Timeline selection proof must begin with Library left and Effects right."));
  if (!timeline_discoverable_invoke_without_result(workspace, "zoomToFit", {}))
    return fail(QStringLiteral("Timeline selection proof could not fit the dense timeline for selection setup."));
  workspace->setProperty("timelineZoom",
      qMin(600.0, qMax(1.0, workspace->property("timelineZoomMax").toDouble())));
  process_layout_events(8, "timeline-scroll-selection-zoom-target");
  const auto target_midpoint = target.start_seconds + target.duration_seconds * 0.5;
  document->seekSequenceSeconds(target_midpoint);
  document->selectClipsByIds({});
  if (initial_tool != QStringLiteral("select"))
    document->setActiveTool(QStringLiteral("select"));
  viewport->setProperty("contentX", qMax(
      0.0, (target_midpoint * workspace->property("timelineZoom").toDouble()) -
               viewport->width() * 0.5));
  if (!timeline_discoverable_invoke_without_result(
          workspace, "ensureTimelineClipTrackVisible", {target.clip})) {
    // The target may already be in view; the following materialization check
    // distinguishes that from an unavailable track.
  }
  process_layout_events(12, "timeline-scroll-selection-materialize-target");
  auto* clip_item = clip_item_for_target();
  if (clip_item == nullptr) {
    TimelineSelectionPointerDiagnostics missing_clip_pointer;
    missing_clip_pointer.phase = QStringLiteral("clip-body-first-selection");
    log_timeline_selection_pointer_failure(
        window, config, missing_clip_pointer,
        QStringLiteral("targetClipBodyWasNotMaterialized"),
        QJsonObject{{QStringLiteral("expectedClipId"), target.clip_id},
                    {QStringLiteral("displayLabel"), target.display_label},
                    {QStringLiteral("trackId"), target_track_id}});
    return fail(QStringLiteral(
        "Timeline selection proof could not materialize the unique target clip in the viewport."));
  }

  TimelineSelectionPointerDiagnostics first_clip_pointer;
  if (!timeline_selection_pointer_click_in_viewport(
          clip_item, viewport, window, workspace, config,
          QStringLiteral("clip-body-first-selection"), &first_clip_pointer))
    return fail(QStringLiteral("The actual timeline clip body did not accept pointer input."));
  const auto clip_body_selected = wait_for_condition(
      [&] { return document->selectedClipIds().contains(target.clip_id); },
      3000, "timeline-scroll-selection-clip-body");
  const auto clip_body_opened_controls = clip_body_selected && controls_and_effects_are_active();
  if (!clip_body_selected || !clip_body_opened_controls) {
    QVariant left_active;
    timeline_discoverable_invoke(
        dock_state, "activePanel", {QStringLiteral("left")}, &left_active);
    std::cerr << "Timeline clip-body selection: expectedClipId="
              << target.clip_id.toStdString() << " selected="
              << document->selectedClipIds().join(',').toStdString()
              << " leftActive=" << left_active.toString().toStdString()
              << " rightEffects=" << panel_active(QStringLiteral("right"), QStringLiteral("effects"))
              << '\n';
    log_timeline_selection_pointer_failure(
        window, config, first_clip_pointer,
        QStringLiteral("clipBodyPressDidNotSelectAndRevealControls"),
        QJsonObject{
            {QStringLiteral("expectedClipId"), target.clip_id},
            {QStringLiteral("selectedClipIds"), QJsonArray::fromStringList(document->selectedClipIds())},
            {QStringLiteral("leftActivePanel"), left_active.toString()},
            {QStringLiteral("rightEffectsActive"),
             panel_active(QStringLiteral("right"), QStringLiteral("effects"))},
        });
    return fail(QStringLiteral(
        "Clicking a real clip body must select that clip and reveal left Controls while retaining right Effects."));
  }

  QVariant media_visible_result;
  if (!focus_panel(QStringLiteral("media")) ||
      !timeline_discoverable_invoke(
          dock_state, "activePanel", {QStringLiteral("left")}, &media_visible_result) ||
      media_visible_result.toString() != QStringLiteral("media")) {
    return fail(QStringLiteral("The left Media/Library panel could not be focused before reselecting the clip."));
  }
  clip_item = clip_item_for_target();
  TimelineSelectionPointerDiagnostics second_clip_pointer;
  if (!timeline_selection_pointer_click_in_viewport(
          clip_item, viewport, window, workspace, config,
          QStringLiteral("clip-body-reselect-from-library"), &second_clip_pointer))
    return fail(QStringLiteral("Reselecting the same visible clip body did not receive pointer input."));
  const auto same_clip_reopened_controls = wait_for_condition(
      [&] {
        return document->selectedClipIds().contains(target.clip_id) &&
               panel_active(QStringLiteral("left"), QStringLiteral("inspector"));
      }, 3000, "timeline-scroll-selection-same-clip-reveal");
  if (!same_clip_reopened_controls || !controls_and_effects_are_active()) {
    QVariant left_active;
    timeline_discoverable_invoke(
        dock_state, "activePanel", {QStringLiteral("left")}, &left_active);
    log_timeline_selection_pointer_failure(
        window, config, second_clip_pointer,
        QStringLiteral("reselectedClipBodyDidNotReopenControls"),
        QJsonObject{
            {QStringLiteral("expectedClipId"), target.clip_id},
            {QStringLiteral("selectedClipIds"), QJsonArray::fromStringList(document->selectedClipIds())},
            {QStringLiteral("leftActivePanel"), left_active.toString()},
            {QStringLiteral("rightEffectsActive"),
             panel_active(QStringLiteral("right"), QStringLiteral("effects"))},
        });
    return fail(QStringLiteral(
        "Clicking the already selected clip from Library must reopen left Controls."));
  }

  document->seekSequenceSeconds(target_midpoint);
  process_layout_events(4, "timeline-scroll-selection-track-playhead");
  const auto before_track_header_selection = document->selectedClipIds();
  auto* track_header = track_header_item();
  if (track_header == nullptr || !timeline_discoverable_pointer_click(track_header))
    return fail(QStringLiteral(
        "Timeline selection proof could not pointer-click the visible header for the target track."));
  const auto track_header_selected_clip = wait_for_condition(
      [&] {
        return document->selectedClipIds().contains(target.clip_id) &&
               panel_active(QStringLiteral("left"), QStringLiteral("inspector"));
      }, 3000, "timeline-scroll-selection-track-header");
  const auto inspected_track_id = workspace->property("inspectedTimelineTrackId").toString();
  if (!track_header_selected_clip || inspected_track_id != target_track_id ||
      !controls_and_effects_are_active()) {
    std::cerr << "Timeline track-header inspection: targetTrack="
              << target_track_id.toStdString() << " inspectedTrack="
              << inspected_track_id.toStdString() << " expectedClip="
              << target.clip_id.toStdString() << " selection="
              << document->selectedClipIds().join(',').toStdString() << " priorSelection="
              << before_track_header_selection.join(',').toStdString() << '\n';
    return fail(QStringLiteral(
        "Clicking the target track header must inspect that track's clip at the playhead and reveal Controls."));
  }

  workspace->setProperty("inspectorTab", QStringLiteral("animate"));
  process_layout_events(5, "timeline-scroll-selection-review-layout");
  auto* channel_list = find_visible_named_item(workspace, QStringLiteral("timelineAnimateChannelList"));
  auto* first_channel = find_visible_named_item(workspace, QStringLiteral("timelineAnimateChannel-0"));
  auto* toolbar = find_visible_named_item(workspace, QStringLiteral("timelineToolBar"));
  if (channel_list == nullptr || first_channel == nullptr || toolbar == nullptr ||
      channel_list->height() < 48.0 ||
      item_scene_rect(channel_list).intersected(item_scene_rect(first_channel)).height() < 16.0 ||
      toolbar->height() > 44.0) {
    window->grabWindow().save(QDir(config.cache_dir).filePath(
        QStringLiteral("timeline-workspace-layout-failure.png")));
    return fail(QStringLiteral("The desktop workspace must show animation properties immediately and keep its timeline toolbar to one row."));
  }
  const auto layout_snapshot_path = QDir(config.cache_dir).filePath(
      QStringLiteral("timeline-workspace-layout-review.png"));
  if (!window->grabWindow().save(layout_snapshot_path))
    return fail(QStringLiteral("The reviewed full-project workspace snapshot must be captured."));

  if (!restore_state())
    return fail(QStringLiteral("Timeline selection proof could not restore the original dock layout."));
  restored = true;
  const auto restored_view =
      std::abs(workspace->property("timelineZoom").toDouble() - initial_zoom) < 0.01 &&
      std::abs(viewport->property("contentX").toDouble() - initial_content_x) < 1.0 &&
      std::abs(viewport->property("contentY").toDouble() - initial_content_y) < 1.0;
  const auto document_hash_after = document->deterministicDocumentHash();
  const auto track_count_after = document->timelineTracks().size();
  const auto clip_count_after = document->clipCount();
  const auto duration_after = document->totalDurationSecondsValue();
  const auto source_file_sha256_after = sha256_file(config.project_path);
  const auto working_copy_sha256_after = sha256_file(config.load_path);
  const bool document_unchanged = document_hash_after == initial_hash &&
      document_hash_after == config.expected_document_hash &&
      track_count_after == initial_track_count && track_count_after == config.expected_tracks &&
      clip_count_after == initial_clip_count && clip_count_after == config.expected_clips &&
      std::abs(duration_after - initial_duration) < 1.0e-6 &&
      std::abs(duration_after - config.expected_duration_seconds) < 1.0e-6;
  const bool selection_restored = document->selectedClipIds() == initial_selection &&
      document->property("currentIndex").toInt() == initial_current_index &&
      std::abs(document->sequencePlayheadSeconds() - initial_playhead) < 0.001 &&
      document->activeTool() == initial_tool;
  const bool source_unchanged = source_file_sha256_after == source_sha256_before &&
      source_file_sha256_after.compare(config.expected_file_sha256, Qt::CaseInsensitive) == 0 &&
      working_copy_sha256_after == initial_working_copy_sha256;
  if (!restored_view || !document_unchanged || !selection_restored ||
      document->dirty() != initial_dirty || !source_unchanged) {
    const QJsonObject selection_diagnostics{
        {QStringLiteral("selectedClipIdsBefore"), QJsonArray::fromStringList(initial_selection)},
        {QStringLiteral("primaryIndexBefore"), initial_current_index},
        {QStringLiteral("playheadSecondsBefore"), initial_playhead},
        {QStringLiteral("selectedClipIdsAfter"), QJsonArray::fromStringList(document->selectedClipIds())},
        {QStringLiteral("primaryIndexAfter"), document->property("currentIndex").toInt()},
        {QStringLiteral("playheadSecondsAfter"), document->sequencePlayheadSeconds()},
    };
    const auto selection_diagnostics_json = QString::fromUtf8(
        QJsonDocument(selection_diagnostics).toJson(QJsonDocument::Compact));
    std::cerr << "TIMELINE_SELECTION_RESTORE_DIAGNOSTICS "
              << selection_diagnostics_json.toStdString() << '\n';
    return fail(QStringLiteral(
        "Timeline pointer selection proof did not restore the strict scroll baseline "
        "(viewRestored=%1 documentUnchanged=%2 selectionRestored=%3 dirtyBefore=%4 dirtyAfter=%5 "
        "sourceUnchanged=%6 hashBefore=%7 hashAfter=%8 tracksBefore=%9 tracksAfter=%10 "
        "clipsBefore=%11 clipsAfter=%12 durationBefore=%13 durationAfter=%14 "
        "selectionDiagnostics=%15).")
        .arg(restored_view)
        .arg(document_unchanged)
        .arg(selection_restored)
        .arg(initial_dirty)
        .arg(document->dirty())
        .arg(source_unchanged)
        .arg(initial_hash, document_hash_after)
        .arg(initial_track_count)
        .arg(track_count_after)
        .arg(initial_clip_count)
        .arg(clip_count_after)
        .arg(initial_duration, 0, 'f', 6)
        .arg(duration_after, 0, 'f', 6)
        .arg(selection_diagnostics_json));
  }

  const bool offscreen = QGuiApplication::platformName() == QStringLiteral("offscreen");
  const QJsonObject receipt{
      {QStringLiteral("schema"), QStringLiteral("timeline-scroll-selection-proof:v1")},
      {QStringLiteral("passed"), true},
      {QStringLiteral("readOnly"), true},
      {QStringLiteral("workspaceLayoutSnapshot"), layout_snapshot_path},
      {QStringLiteral("visibleAnimationChannelsVerified"), true},
      {QStringLiteral("singleRowToolbarVerified"), true},
      {QStringLiteral("settledUnselectedProgramBeforeBaseline"), settled_unselected_program},
      {QStringLiteral("recoveredPrimaryIndex"), recovered_primary_index},
      {QStringLiteral("baselinePrimaryIndex"), initial_current_index},
      {QStringLiteral("documentSaveCalled"), false},
      {QStringLiteral("clipBodyPointerSelectedAndOpenedControls"), clip_body_opened_controls},
      {QStringLiteral("reselectFromLibraryReopenedControls"), same_clip_reopened_controls},
      {QStringLiteral("trackHeaderPointerSelectedExpectedClip"), track_header_selected_clip},
      {QStringLiteral("rightEffectsRetained"), true},
      {QStringLiteral("selectedClipId"), target.clip_id},
      {QStringLiteral("displayLabel"), target.display_label},
      {QStringLiteral("mediaType"), target.media_type},
      {QStringLiteral("trackId"), target_track_id},
      {QStringLiteral("documentHashBefore"), initial_hash},
      {QStringLiteral("documentHashAfter"), document_hash_after},
      {QStringLiteral("trackCount"), track_count_after},
      {QStringLiteral("clipCount"), clip_count_after},
      {QStringLiteral("durationSeconds"), duration_after},
      {QStringLiteral("sourceProjectSha256Before"), source_sha256_before},
      {QStringLiteral("sourceProjectSha256After"), source_file_sha256_after},
      {QStringLiteral("measurementScope"), offscreen
          ? QStringLiteral("offscreen Qt Quick pointer routing; no native presentation")
          : QStringLiteral("native-platform QtTest pointer routing; no physical-input or presented-frame claim")},
      {QStringLiteral("qtPlatform"), QGuiApplication::platformName()},
      {QStringLiteral("nativePresentedPixelsVerified"), false},
      {QStringLiteral("physicalPerformanceQualified"), false},
  };
  auto receipt_path = QDir(config.cache_dir).filePath(
      QStringLiteral("timeline-scroll-selection-proof.json"));
  if (QFileInfo::exists(receipt_path)) {
    receipt_path = QDir(config.cache_dir).filePath(
        QStringLiteral("timeline-scroll-selection-proof-%1.json")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
  }
  const auto receipt_bytes = QJsonDocument(receipt).toJson(QJsonDocument::Indented);
  QSaveFile receipt_file(receipt_path);
  if (!receipt_file.open(QIODevice::WriteOnly) ||
      receipt_file.write(receipt_bytes) != receipt_bytes.size() || !receipt_file.commit()) {
    return fail(QStringLiteral("Timeline selection proof could not write its receipt to %1.")
                    .arg(receipt_path));
  }
  std::cout << "TIMELINE_SCROLL_SELECTION_PROOF "
            << QJsonDocument(receipt).toJson(QJsonDocument::Compact).constData()
            << " receipt=" << receipt_path.toStdString() << '\n';
  return EXIT_SUCCESS;
}

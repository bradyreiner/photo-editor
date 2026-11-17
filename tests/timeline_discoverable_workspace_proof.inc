QString timeline_discoverable_video_fixture(const QString& photo_path, const QString& output_path) {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty() || photo_path.isEmpty() || output_path.isEmpty()) return {};
  QProcess process;
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(ffmpeg, {
      QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
      QStringLiteral("-loop"), QStringLiteral("1"), QStringLiteral("-i"), photo_path,
      QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
      QStringLiteral("sine=frequency=440:duration=10"), QStringLiteral("-t"), QStringLiteral("10"),
      QStringLiteral("-r"), QStringLiteral("30"), QStringLiteral("-vf"),
      QStringLiteral("scale=640:-2"), QStringLiteral("-map"), QStringLiteral("0:v:0"),
      QStringLiteral("-map"), QStringLiteral("1:a:0"), QStringLiteral("-c:v"),
      QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("ultrafast"),
      QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-c:a"),
      QStringLiteral("aac"), QStringLiteral("-shortest"), QStringLiteral("-y"), output_path,
  });
  if (!process.waitForStarted(5000) || !process.waitForFinished(60000) ||
      process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
      QFileInfo(output_path).size() <= 1024) {
    std::cerr << "Timeline native proof media generation failed: "
              << process.readAllStandardError().toStdString() << '\n';
    return {};
  }
  return output_path;
}

bool timeline_discoverable_invoke(
    QObject* object, const char* method, const QVariantList& arguments, QVariant* result) {
  if (object == nullptr) return false;
  QVariant local_result;
  auto* return_value = result == nullptr ? &local_result : result;
  bool invoked = false;
  switch (arguments.size()) {
    case 0:
      invoked = QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, *return_value));
      break;
    case 1:
      invoked = QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, *return_value),
                                           Q_ARG(QVariant, arguments[0]));
      break;
    case 2:
      invoked = QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, *return_value),
                                           Q_ARG(QVariant, arguments[0]), Q_ARG(QVariant, arguments[1]));
      break;
    case 3:
      invoked = QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, *return_value),
                                           Q_ARG(QVariant, arguments[0]), Q_ARG(QVariant, arguments[1]),
                                           Q_ARG(QVariant, arguments[2]));
      break;
    default:
      return false;
  }
  if (invoked && return_value->metaType() == QMetaType::fromType<QJSValue>())
    *return_value = return_value->value<QJSValue>().toVariant();
  return invoked;
}

bool timeline_discoverable_invoke_without_result(
    QObject* object, const char* method, const QVariantList& arguments) {
  if (object == nullptr) return false;
  switch (arguments.size()) {
    case 0:
      return QMetaObject::invokeMethod(object, method);
    case 1:
      return QMetaObject::invokeMethod(object, method, Q_ARG(QVariant, arguments[0]));
    case 2:
      return QMetaObject::invokeMethod(
          object, method, Q_ARG(QVariant, arguments[0]), Q_ARG(QVariant, arguments[1]));
    case 3:
      return QMetaObject::invokeMethod(
          object, method, Q_ARG(QVariant, arguments[0]), Q_ARG(QVariant, arguments[1]),
          Q_ARG(QVariant, arguments[2]));
    default:
      return false;
  }
}

QVariantMap timeline_discoverable_clip(
    photo_editor::desktop::TimelineDocumentController* document,
    const QString& label,
    const QString& required_track_kind = QString()) {
  if (document == nullptr) return {};
  for (const auto& track_value : document->sequenceTracks()) {
    const auto track = track_value.toMap();
    if (!required_track_kind.isEmpty() &&
        track.value(QStringLiteral("trackKind")).toString() != required_track_kind)
      continue;
    for (const auto& clip_value : track.value(QStringLiteral("clips")).toList()) {
      const auto clip = clip_value.toMap();
      if (clip.value(QStringLiteral("displayLabel")).toString() == label) return clip;
    }
  }
  return {};
}

QVariantMap timeline_discoverable_track(
    photo_editor::desktop::TimelineDocumentController* document, const QString& track_id) {
  if (document == nullptr) return {};
  for (const auto& track_value : document->sequenceTracks()) {
    const auto track = track_value.toMap();
    if (track.value(QStringLiteral("trackId")).toString() == track_id) return track;
  }
  return {};
}

QQuickItem* timeline_discoverable_video_clip_item(QObject* workspace, const QString& label) {
  const auto object_name = QStringLiteral("timelineClip-") + label;
  for (auto* item : find_named_items(workspace, object_name)) {
    if (!item_visible(item)) continue;
    const auto model = item->property("modelData").toMap();
    if (model.value(QStringLiteral("trackKind")).toString() == QStringLiteral("video"))
      return item;
  }
  return nullptr;
}

QVector<QQuickItem*> timeline_discoverable_items_with_text(
    QObject* root, const QString& text) {
  QVector<QQuickItem*> matches;
  QSet<const QObject*> visited;
  const auto visit = [&](const auto& self, QObject* candidate) -> void {
    if (candidate == nullptr || visited.contains(candidate)) return;
    visited.insert(candidate);
    if (auto* item = qobject_cast<QQuickItem*>(candidate)) {
      if (item->property("text").toString().contains(text, Qt::CaseInsensitive))
        matches.push_back(item);
      for (auto* child : item->childItems()) self(self, child);
    }
    if (auto* window = qobject_cast<QQuickWindow*>(candidate))
      self(self, window->contentItem());
    for (auto* child : candidate->children()) self(self, child);
  };
  visit(visit, root);
  return matches;
}

bool timeline_discoverable_report_visible_no_selection_overlay(
    QObject* root,
    QQuickWindow* main_window,
    photo_editor::desktop::TimelineDocumentController* document) {
  if (root == nullptr || main_window == nullptr || document == nullptr) return false;
  bool visible_overlay_found = false;
  auto overlays = find_named_items(root, QStringLiteral("timelineInspectorNoSelectionState"));
  QSet<QQuickItem*> visited_overlays;
  for (auto* overlay : overlays) visited_overlays.insert(overlay);
  for (auto* text_item : timeline_discoverable_items_with_text(
           root, QStringLiteral("Select a clip to inspect"))) {
    if (!visited_overlays.contains(text_item)) {
      visited_overlays.insert(text_item);
      overlays.push_back(text_item);
    }
  }
  for (auto* overlay : overlays) {
    if (overlay->window() != main_window || !overlay->isVisible()) continue;
    visible_overlay_found = true;
    const auto rect = item_scene_rect(overlay);
    std::cerr << "Visible no-selection Inspector overlay in main window: objectName='"
              << overlay->objectName().toStdString() << "' bounds=" << rect.x() << ','
              << rect.y() << ' ' << rect.width() << 'x' << rect.height()
              << " document.currentIndex=" << document->currentIndex() << '\n';
    for (auto* owner = overlay; owner != nullptr; owner = owner->parentItem()) {
      const auto has_selection = owner->property("hasSelection");
      const auto selected_index = owner->property("selectedClipIndex");
      const auto has_clip = owner->property("hasClip");
      const auto surface_mode = owner->property("surfaceMode");
      if (!has_selection.isValid() && !selected_index.isValid() &&
          !has_clip.isValid() && !surface_mode.isValid())
        continue;
      std::cerr << "  Inspector owner objectName='" << owner->objectName().toStdString()
                << "' hasSelection="
                << (has_selection.isValid() ? has_selection.toString().toStdString() : "<unset>")
                << " selectedClipIndex="
                << (selected_index.isValid() ? selected_index.toString().toStdString() : "<unset>")
                << " hasClip="
                << (has_clip.isValid() ? has_clip.toString().toStdString() : "<unset>")
                << " surfaceMode="
                << (surface_mode.isValid() ? surface_mode.toString().toStdString() : "<unset>");
      const auto controller_value = owner->property("documentController");
      auto* controller = controller_value.value<QObject*>();
      if (controller != nullptr)
        std::cerr << " documentController.currentIndex="
                  << controller->property("currentIndex").toString().toStdString();
      std::cerr << '\n';
    }
  }
  return visible_overlay_found;
}

bool timeline_discoverable_pointer_click(QQuickItem* item, int modifiers = Qt::NoModifier) {
  if (!item_visible(item) || !item->property("enabled").toBool()) return false;
  auto* window = item->window();
  if (window == nullptr || !window->isExposed()) return false;
  const auto point = item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0)).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, static_cast<Qt::KeyboardModifiers>(modifiers), point);
  process_layout_events(5, "timeline-discoverable-pointer-click");
  return true;
}

bool timeline_discoverable_pointer_drag(
    QQuickItem* item, const QPointF& from, const QPointF& to, int modifiers = Qt::NoModifier) {
  if (!item_visible(item)) return false;
  auto* window = item->window();
  if (window == nullptr || !window->isExposed()) return false;
  const auto start = item->mapToScene(from).toPoint();
  const auto finish = item->mapToScene(to).toPoint();
  QTest::mouseMove(window, start, 20);
  QTest::mousePress(window, Qt::LeftButton, static_cast<Qt::KeyboardModifiers>(modifiers), start);
  QTest::mouseMove(window, finish, 60);
  QTest::mouseRelease(window, Qt::LeftButton, static_cast<Qt::KeyboardModifiers>(modifiers), finish);
  process_layout_events(6, "timeline-discoverable-pointer-drag");
  return true;
}

int run_timeline_discoverable_workspace_proof(
    QQuickWindow* window,
    QObject* root,
    photo_editor::desktop::AppShellController* app_shell,
    photo_editor::desktop::TimelineDocumentController* document,
    photo_editor::desktop::TimelinePlaybackController* playback,
    photo_editor::desktop::CatalogAssetModel* catalog,
    const QString& source_path,
    const QString& output_dir) {
  using namespace photo_editor::desktop;
  if (window == nullptr || root == nullptr || app_shell == nullptr || document == nullptr ||
      playback == nullptr || catalog == nullptr || output_dir.isEmpty() ||
      !QDir().mkpath(output_dir))
    return fail(QStringLiteral("Discoverable Timeline proof needs the real desktop shell and an output directory."));
#ifndef Q_OS_MACOS
  return fail(QStringLiteral("Discoverable Timeline native proof requires an exposed macOS desktop window."));
#else
  if (QGuiApplication::platformName() != QStringLiteral("cocoa"))
    return fail(QStringLiteral("Discoverable Timeline proof requires the Cocoa native window backend."));
#endif

  app_shell->setDarkMode(true);
  root->setProperty("recentUserInput", true);
  activate_native_test_application();
  window->show();
  window->requestActivate();
  if (!wait_for_condition([&] { return window->isActive() && window->isExposed(); },
                          10000, "timeline-discoverable-native-window"))
    return fail(QStringLiteral("Timeline native proof requires the actual application window to be active and exposed."));

  const auto video_path = timeline_discoverable_video_fixture(
      source_path, QDir(output_dir).filePath(QStringLiteral("discoverable-workspace-av.mp4")));
  const QImage photo(source_path);
  if (video_path.isEmpty() || photo.isNull())
    return fail(QStringLiteral("Discoverable Timeline proof requires its generated ten-second A/V clip and source photo."));
  auto video_asset = make_asset(QStringLiteral("discoverable-video"), video_path);
  video_asset.media_type = QStringLiteral("video");
  video_asset.duration_seconds = 10.0;
  video_asset.duration_text = QStringLiteral("0:10");
  auto photo_asset = make_asset(QStringLiteral("discoverable-photo"), source_path);
  photo_asset.media_type = QStringLiteral("photo");
  catalog->replaceAssetsForTesting({video_asset, photo_asset});
  document->createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Discoverable Workspace Proof")},
      {QStringLiteral("width"), 1080}, {QStringLiteral("height"), 1350},
      {QStringLiteral("frameRate"), 30.0}});
  document->addClipFromGallery(QStringLiteral("discoverable-photo"), source_path,
      QStringLiteral("Discoverable Still"), 2.0, QUrl::fromLocalFile(source_path).toString());
  document->addClipFromGallery(QStringLiteral("discoverable-video"), video_path,
      QStringLiteral("Discoverable Video"), 8.0, QUrl::fromLocalFile(source_path).toString());
  if (!wait_for_condition([&] {
        const auto clip = document->currentClip();
        return clip.value(QStringLiteral("displayLabel")).toString() == QStringLiteral("Discoverable Video") &&
               (clip.value(QStringLiteral("mediaAnalysisStatus")).toString() == QStringLiteral("ready") ||
                clip.value(QStringLiteral("mediaAnalysisStatus")).toString() == QStringLiteral("failed"));
      }, 30000, "timeline-discoverable-media-ready"))
    return fail(QStringLiteral("The generated video did not reach a terminal media-analysis state."));
  const auto video_clip = timeline_discoverable_clip(document, QStringLiteral("Discoverable Video"),
                                                       QStringLiteral("video"));
  const auto still_clip = timeline_discoverable_clip(document, QStringLiteral("Discoverable Still"),
                                                       QStringLiteral("video"));
  if (video_clip.isEmpty() || still_clip.isEmpty() ||
      !document->currentClip().value(QStringLiteral("hasAudio")).toBool())
    return fail(QStringLiteral("The fixture must contain a real photo, an analyzed ten-second video, and its audio stream."));
  const auto video_id = video_clip.value(QStringLiteral("clipId")).toString();
  const auto still_id = still_clip.value(QStringLiteral("clipId")).toString();
  const auto video_index = video_clip.value(QStringLiteral("index")).toInt();
  document->selectClip(video_index);
  document->seekSequenceSeconds(5.0);

  QVariant routed;
  if (!QMetaObject::invokeMethod(root, "switchToWorkspaceIndex", Q_RETURN_ARG(QVariant, routed),
          Q_ARG(QVariant, root->property("timelineWorkspaceIndex"))) || !routed.toBool() ||
      !wait_for_condition([&] {
        auto* active = active_workspace(root);
        return active != nullptr && active->objectName() == QStringLiteral("timelineWorkspace") &&
               active->property("timelineEditorPageLoaded").toBool() &&
               !root->property("workspaceTransitionActive").toBool() &&
               named_item_visible(root, QStringLiteral("timelineHeaderBar")) &&
               named_item_visible(root, QStringLiteral("timelineViewport"));
      }, 30000, "timeline-discoverable-route"))
    return fail(QStringLiteral("The native proof could not route to the actual Timeline editor."));
  auto* workspace = active_workspace(root);
  auto* dock_state = find_named_object(workspace, QStringLiteral("timelinePanelDockState"));
  auto* settings = find_named_object(workspace, QStringLiteral("timelineWorkspaceSettings"));
  if (workspace == nullptr || dock_state == nullptr || settings == nullptr)
    return fail(QStringLiteral("The Timeline workspace must expose its live dock state and persisted preferences."));

  const auto screen = window->screen() != nullptr ? window->screen() : QGuiApplication::primaryScreen();
  if (screen == nullptr || screen->availableGeometry().width() < 1100 ||
      screen->availableGeometry().height() < 700)
    return fail(QStringLiteral("The native display must provide at least a 1100 by 700 usable desktop."));
  const auto available = screen->availableGeometry();
  const auto describe_native_window = [&] {
    const auto geometry = window->geometry();
    const auto screen_geometry = screen->geometry();
    const auto screen_available = screen->availableGeometry();
    return QStringLiteral(
               "active=%1 exposed=%2 visible=%3 size=%4x%5 geometry=%6,%7 %8x%9 "
               "screen=%10,%11 %12x%13 available=%14,%15 %16x%17")
        .arg(window->isActive())
        .arg(window->isExposed())
        .arg(window->isVisible())
        .arg(window->width())
        .arg(window->height())
        .arg(geometry.x())
        .arg(geometry.y())
        .arg(geometry.width())
        .arg(geometry.height())
        .arg(screen_geometry.x())
        .arg(screen_geometry.y())
        .arg(screen_geometry.width())
        .arg(screen_geometry.height())
        .arg(screen_available.x())
        .arg(screen_available.y())
        .arg(screen_available.width())
        .arg(screen_available.height());
  };
  QJsonArray captures;
  const auto save_frame = [&](const QString& name) {
    if (!window->isActive() || !window->isExposed() || !window->isVisible()) {
      window->requestActivate();
      if (!wait_for_condition(
              [&] { return window->isActive() && window->isExposed() && window->isVisible(); },
              5000, "timeline-discoverable-capture-window-activation")) {
        std::cerr << "Timeline capture window activation failed before " << name.toStdString()
                  << ": " << describe_native_window().toStdString() << '\n';
        return false;
      }
    }
    const auto previous_capture_count = captures.size();
    save_capture_if_requested(window, output_dir, name, &captures);
    const auto saved = captures.size() > previous_capture_count &&
        captures.last().toObject().value(QStringLiteral("name")).toString() == name;
    if (!saved) {
      std::cerr << "Timeline capture helper saved no frame for " << name.toStdString()
                << ": " << describe_native_window().toStdString() << '\n';
    }
    return saved;
  };
  const auto click_named = [&](const QString& name) {
    return timeline_discoverable_pointer_click(find_visible_named_item(root, name));
  };
  const auto has_visible = [&](const QString& name) {
    return named_item_visible(workspace, name);
  };
  const auto panel_ids = QStringList{
      QStringLiteral("media"), QStringLiteral("source"), QStringLiteral("effects"),
      QStringLiteral("inspector"), QStringLiteral("color"), QStringLiteral("mixer"),
      QStringLiteral("keyframes"), QStringLiteral("titles"), QStringLiteral("captions"),
      QStringLiteral("transcript"), QStringLiteral("scopes")};
  const auto open_panel = [&](const QString& id) {
    const auto index = panel_ids.indexOf(id);
    if (index < 0 || !click_named(QStringLiteral("timelinePanelsMenuButtonPrimaryAction")))
      return false;
    const auto row = QStringLiteral("timelinePanelsMenuButtonPopupItem%1").arg(index);
    return wait_for_condition([&] { return has_visible(row); }, 3000) && click_named(row);
  };
  const auto panels_menu_fits = [&] {
    if (!click_named(QStringLiteral("timelinePanelsMenuButtonPrimaryAction"))) return false;
    for (int index = 0; index < panel_ids.size(); ++index) {
      const auto name = QStringLiteral("timelinePanelsMenuButtonPopupItem%1").arg(index);
      if (!wait_for_condition([&] { return has_visible(name); }, 3000)) return false;
      auto* row = find_visible_named_item(workspace, name);
      const auto bounds = item_scene_rect(row);
      if (bounds.left() < 0 || bounds.right() > window->width() + 1 ||
          bounds.top() < 0 || bounds.bottom() > window->height() + 1) return false;
    }
    QTest::keyClick(window, Qt::Key_Escape);
    return wait_for_condition([&] {
      auto* menu = find_named_object(workspace, QStringLiteral("timelinePanelsMenuButtonPopup"));
      return menu && !menu->property("visible").toBool();
    }, 3000);
  };
  const auto tool_ids = QStringList{
      QStringLiteral("select"), QStringLiteral("track-select-backward"),
      QStringLiteral("track-select-forward"), QStringLiteral("razor"),
      QStringLiteral("trim"), QStringLiteral("ripple"), QStringLiteral("roll"),
      QStringLiteral("slip"), QStringLiteral("slide"), QStringLiteral("rate-stretch"),
      QStringLiteral("pen"), QStringLiteral("hand"), QStringLiteral("zoom")};
  const auto surface_contains = [&](QQuickItem* item) {
    if (!item_visible(item)) return false;
    const auto visible = item_scene_rect(item).intersected(
        QRectF(0.0, 0.0, window->width(), window->height()));
    return visible.width() >= qMin(item->width(), 72.0) && visible.height() >= qMin(item->height(), 24.0);
  };

  window->resize(1180, 768);
  process_layout_events(12, "timeline-discoverable-compact-layout");
  if (!window->isActive() || !window->isExposed())
    activate_native_test_application();
  window->requestActivate();
  if (!wait_for_condition([&] {
        return window->isActive() && window->isExposed() && window->isVisible();
      }, 5000, "timeline-discoverable-compact-window-activation")) {
    std::cerr << "Timeline window was not active after the compact resize: "
              << describe_native_window().toStdString() << '\n';
    return fail(QStringLiteral("The compact native Timeline capture requires an active exposed app window."));
  }
  if (!wait_for_condition([&] {
        return has_visible(QStringLiteral("timelinePanelStrip")) &&
               has_visible(QStringLiteral("timelineToolPalette"));
      }, 10000, "timeline-discoverable-compact-controls"))
    return fail(QStringLiteral("The compact native workspace must expose its panel strip and complete tool palette."));
  if (!panels_menu_fits())
    return fail(QStringLiteral("Every named Timeline panel must fit in the header Panels menu."));

  for (const auto& id : tool_ids) {
    if (!surface_contains(find_visible_named_item(workspace, QStringLiteral("timelineTool_") + id)))
      return fail(QStringLiteral("Every direct Timeline tool must be visible at 1180 by 768: ") + id);
  }
  if (!save_frame(QStringLiteral("timeline-discoverable-1180-panels-tools")))
    return fail(QStringLiteral("The compact native Timeline capture could not be saved."));
  if (timeline_discoverable_report_visible_no_selection_overlay(root, window, document))
    return fail(QStringLiteral(
        "A selected timeline clip must not display the Inspector's no-selection state in the main window."));

  // Simultaneous panels must have one contextual Find owner per native window.
  if (!open_panel(QStringLiteral("inspector")) ||
      !timeline_discoverable_invoke_without_result(workspace, "openInspectorTab", {QStringLiteral("animate")}) ||
      !wait_for_condition([&] { return has_visible(QStringLiteral("timelineAnimateSearchField")); }, 5000))
    return fail(QStringLiteral("The Inspector must expose its real animation search before the keyboard check."));
  QTest::keySequence(window, QKeySequence(QKeySequence::Find));
  if (!wait_for_condition([&] { auto* field = find_visible_named_item(workspace, QStringLiteral("timelineAnimateSearchField"));
        return field && field->hasActiveFocus(); }, 3000)) {
    auto* shortcut = find_named_object(workspace, QStringLiteral("timelineContextualFindShortcut"));
    auto* inspector = find_named_object(workspace, QStringLiteral("timelineEffectsRail"));
    auto* library = find_named_object(workspace, QStringLiteral("timelineEffectsLibraryPanel"));
    std::cerr << "Context Find diagnostic: nativeSequence=" << QKeySequence(QKeySequence::Find).toString().toStdString()
              << " shortcut=" << (shortcut != nullptr)
              << " enabled=" << (shortcut && shortcut->property("enabled").toBool())
              << " inspectorLocal=" << (inspector && inspector->property("findShortcutEnabled").toBool())
              << " libraryLocal=" << (library && library->property("findShortcutEnabled").toBool())
              << " lastPanel=" << workspace->property("lastFocusedTimelinePanel").toString().toStdString()
              << " activeFocus=" << (window->activeFocusItem() ? window->activeFocusItem()->objectName().toStdString() : "none")
              << " windowActive=" << window->isActive() << '\n';
    return fail(QStringLiteral("Find must focus animation search while Inspector and Effects coexist."));
  }
  if (!open_panel(QStringLiteral("effects")))
    return fail(QStringLiteral("The Effects panel must remain directly focusable for contextual search."));
  QTest::keySequence(window, QKeySequence(QKeySequence::Find));
  if (!wait_for_condition([&] { auto* field = find_visible_named_item(workspace, QStringLiteral("timelineEffectKeyboardSearch"));
        return field && field->hasActiveFocus(); }, 3000))
    return fail(QStringLiteral("Find must focus Effects search without an ambiguous shortcut."));
  document->selectClip(-1);
  if (!wait_for_condition([&] { auto* list = find_visible_named_item(workspace, QStringLiteral("timelineEffectLibraryList"));
        return list && list->property("count").toInt() > 0; }, 3000))
    return fail(QStringLiteral("Effects discovery must remain browsable without a selected clip."));
  document->selectClip(video_index);
  document->seekSequenceSeconds(5.0);

  for (const auto& id : panel_ids) {
    if (!open_panel(id))
      return fail(QStringLiteral("The visible Timeline panel control did not receive pointer input: ") + id);
    QVariant visible;
    QVariant active;
    QVariant dock;
    if (!timeline_discoverable_invoke(dock_state, "visible", {id}, &visible) ||
        !timeline_discoverable_invoke(dock_state, "dock", {id}, &dock) ||
        !timeline_discoverable_invoke(dock_state, "activePanel", {dock}, &active) ||
        !visible.toBool() || active.toString() != id)
      return fail(QStringLiteral("Pointer focus must reveal and activate each named Timeline panel: ") + id);
  }

  const auto max_wide_width = qMin(1920, available.width());
  const auto max_wide_height = qMin(1080, available.height());
  window->resize(max_wide_width, max_wide_height);
  window->requestActivate();
  process_layout_events(12, "timeline-discoverable-native-wide-layout");
  if (!wait_for_condition([&] { return window->isActive() && window->isExposed(); },
                          5000, "timeline-discoverable-wide-active"))
    return fail(QStringLiteral("The wide Timeline workspace must remain a native exposed window."));
  if (!panels_menu_fits())
    return fail(QStringLiteral("Every named Timeline panel must fit in the header Panels menu."));

  for (const auto& id : tool_ids) {
    if (!surface_contains(find_visible_named_item(workspace, QStringLiteral("timelineTool_") + id)))
      return fail(QStringLiteral("The wide native workspace lost a direct tool: ") + id);
  }
  if (!save_frame(QStringLiteral("timeline-discoverable-native-wide")))
    return fail(QStringLiteral("The wide native Timeline capture could not be saved."));

  // Use the public dock-state model only for the initial layout. The move,
  // float, dock, focus, and edit gestures below use native pointer events.
  const auto setup_call = [&](const char* method, const QVariantList& args) {
    QVariant result;
    return timeline_discoverable_invoke(dock_state, method, args, &result) && result.toBool();
  };
  if (!setup_call("movePanel", {QStringLiteral("effects"), QStringLiteral("right"), 0}) ||
      !setup_call("movePanel", {QStringLiteral("color"), QStringLiteral("right"), 1}) ||
      !setup_call("movePanel", {QStringLiteral("mixer"), QStringLiteral("bottom"), 0}) ||
      !setup_call("setVisible", {QStringLiteral("effects"), true}) ||
      !setup_call("setVisible", {QStringLiteral("color"), true}) ||
      !setup_call("setVisible", {QStringLiteral("mixer"), true}))
    return fail(QStringLiteral("The actual dock state could not prepare Effects, Color, and Mixer for native interaction."));
  for (const auto& id : {QStringLiteral("media"), QStringLiteral("source"),
                         QStringLiteral("inspector"), QStringLiteral("keyframes"),
                         QStringLiteral("titles"), QStringLiteral("captions"),
                         QStringLiteral("transcript"), QStringLiteral("scopes")})
    (void)setup_call("setVisible", {id, false});
  (void)setup_call("focusPanel", {QStringLiteral("effects")});
  process_layout_events(10, "timeline-discoverable-prepare-effects");
  if (!click_named(QStringLiteral("timelineDockTab_effects")) ||
      !click_named(QStringLiteral("timelineDockMoveButton_right")) ||
      !wait_for_condition([&] { return has_visible(QStringLiteral("timelineDockMove_left")); },
                          3000, "timeline-discoverable-move-menu") ||
      !click_named(QStringLiteral("timelineDockMove_left")))
    return fail(QStringLiteral("The Effects panel must move to the left dock through its native Move menu."));
  QVariant effects_dock;
  if (!timeline_discoverable_invoke(dock_state, "dock", {QStringLiteral("effects")}, &effects_dock) ||
      effects_dock.toString() != QStringLiteral("left"))
    return fail(QStringLiteral("The native Effects Move action did not update its live dock state."));
  if (!click_named(QStringLiteral("timelineDockFloatButton_left")) ||
      !wait_for_condition([&] {
        QVariant dock;
        return timeline_discoverable_invoke(
                   dock_state, "dock", {QStringLiteral("effects")}, &dock) &&
               dock.toString() == QStringLiteral("floating");
      }, 5000, "timeline-discoverable-float-action"))
    return fail(QStringLiteral("The Effects Float action must move the panel into its floating dock."));
  auto* effects_floating_window = qobject_cast<QQuickWindow*>(
      find_named_object(root, QStringLiteral("timelineFloatingPanel_effects")));
  if (effects_floating_window == nullptr || !effects_floating_window->isVisible() ||
      !wait_for_condition([&] { return effects_floating_window->isExposed(); },
                          5000, "timeline-discoverable-floating-window"))
    return fail(QStringLiteral("Floating Effects must create an exposed native panel window."));
  if (!timeline_discoverable_pointer_click(
          find_visible_named_item(effects_floating_window,
                                  QStringLiteral("timelineDockFloatButton_floating"))) ||
      !wait_for_condition([&] {
        QVariant dock;
        return timeline_discoverable_invoke(
                   dock_state, "dock", {QStringLiteral("effects")}, &dock) &&
               dock.toString() == QStringLiteral("left");
      }, 5000, "timeline-discoverable-dock-action"))
    return fail(QStringLiteral("The floating Effects Dock action must restore its previous left dock."));
  const auto surface_item_is_rendered = [&](QQuickItem* item, qreal minimum_width, qreal minimum_height) {
    if (!item_visible(item)) return false;
    const auto visible = item_scene_rect(item).intersected(
        QRectF(0.0, 0.0, window->width(), window->height()));
    return visible.width() >= qMin(item->width(), minimum_width) &&
           visible.height() >= qMin(item->height(), minimum_height);
  };
  const auto audio_mixer_is_rendered = [&] {
    auto* popup = find_named_object(workspace, QStringLiteral("timelineStudioAudioMixer"));
    auto* dock = find_named_item(workspace, QStringLiteral("timelineStudioAudioDock"));
    auto* content = popup != nullptr
        ? qobject_cast<QQuickItem*>(popup->property("contentItem").value<QObject*>())
        : nullptr;
    auto* title = find_named_item(workspace, QStringLiteral("timelineStudioAudioMixerTitle"));
    auto* master = find_named_item(workspace, QStringLiteral("timelineMasterMixerStrip"));
    auto* track_fader = find_named_item(
        workspace, QStringLiteral("timelineTrackGainFader_audio-1"));
    return popup != nullptr && popup->property("visible").toBool() &&
           surface_item_is_rendered(dock, 80.0, 60.0) &&
           surface_item_is_rendered(content, 80.0, 60.0) &&
           surface_item_is_rendered(title, 80.0, 16.0) &&
           surface_item_is_rendered(master, 80.0, 80.0) &&
           surface_item_is_rendered(track_fader, 20.0, 80.0);
  };
  if (!open_panel(QStringLiteral("color")) ||
      !open_panel(QStringLiteral("mixer")) ||
      !wait_for_condition([&] {
        const auto* effect_list = find_visible_named_item(
            workspace, QStringLiteral("timelineEffectLibraryList"));
        return has_visible(QStringLiteral("timelineEffectsLibraryPanel")) &&
               effect_list != nullptr && effect_list->property("count").toInt() > 0 &&
               has_visible(QStringLiteral("timelineColorConsole")) &&
               has_visible(QStringLiteral("timelineColorConsolePrimariesPage")) &&
               has_visible(QStringLiteral("timelineColorConsole_exposure")) &&
               audio_mixer_is_rendered();
      }, 20000, "timeline-discoverable-three-real-panels")) {
    auto* mixer_popup = find_named_object(workspace, QStringLiteral("timelineStudioAudioMixer"));
    auto* mixer_dock = find_named_item(workspace, QStringLiteral("timelineStudioAudioDock"));
    auto* mixer_content = mixer_popup != nullptr
        ? qobject_cast<QQuickItem*>(mixer_popup->property("contentItem").value<QObject*>())
        : nullptr;
    auto* mixer_title = find_named_item(workspace, QStringLiteral("timelineStudioAudioMixerTitle"));
    auto* mixer_master_strip = find_named_item(workspace, QStringLiteral("timelineMasterMixerStrip"));
    auto* mixer_track_fader = find_named_item(
        workspace, QStringLiteral("timelineTrackGainFader_audio-1"));
    auto* effect_panel = find_named_item(workspace, QStringLiteral("timelineEffectsLibraryPanel"));
    auto* effect_list = find_named_item(workspace, QStringLiteral("timelineEffectLibraryList"));
    auto* color_panel = find_named_item(workspace, QStringLiteral("timelineColorConsole"));
    auto* color_page = find_named_item(workspace, QStringLiteral("timelineColorConsolePrimariesPage"));
    auto* color_exposure = find_named_item(workspace, QStringLiteral("timelineColorConsole_exposure"));
    QVariant mixer_dock_visible;
    QVariant mixer_dock_id;
    const auto mixer_is_in_dock_model =
        timeline_discoverable_invoke(dock_state, "visible", {QStringLiteral("mixer")},
                                     &mixer_dock_visible) &&
        timeline_discoverable_invoke(dock_state, "dock", {QStringLiteral("mixer")},
                                     &mixer_dock_id);
    const auto describe_item = [&](const char* label, QQuickItem* item) {
      const auto rect = item_scene_rect(item);
      std::cerr << " " << label << "=" << (item != nullptr)
                << ":visible=" << (item != nullptr && item->isVisible())
                << ":size=" << (item != nullptr ? item->width() : 0.0) << 'x'
                << (item != nullptr ? item->height() : 0.0)
                << ":bounds=" << rect.x() << ',' << rect.y() << ' '
                << rect.width() << 'x' << rect.height();
    };
    std::cerr << "Timeline triad render gate failed: effectsPanel="
              << (effect_panel != nullptr && effect_panel->isVisible())
              << " effectRows=" << (effect_list != nullptr ? effect_list->property("count").toInt() : -1)
              << " colorPanel=" << (color_panel != nullptr && color_panel->isVisible())
              << " colorPage=" << (color_page != nullptr && color_page->isVisible())
              << " colorExposure=" << (color_exposure != nullptr && color_exposure->isVisible())
              << " mixerDockState=" << mixer_is_in_dock_model
              << ":visible=" << mixer_dock_visible.toBool()
              << ":dock=" << mixer_dock_id.toString().toStdString()
              << " popup=" << (mixer_popup != nullptr)
              << ":visible=" << (mixer_popup != nullptr && mixer_popup->property("visible").toBool())
              << " window=" << describe_native_window().toStdString();
    describe_item("effectsPanelBounds", effect_panel);
    describe_item("effectsListBounds", effect_list);
    describe_item("colorPanelBounds", color_panel);
    describe_item("colorPageBounds", color_page);
    describe_item("colorExposureBounds", color_exposure);
    describe_item("dock", mixer_dock);
    describe_item("content", mixer_content);
    describe_item("title", mixer_title);
    describe_item("master", mixer_master_strip);
    describe_item("trackFader", mixer_track_fader);
    std::cerr << '\n';
    QJsonArray triad_failure_captures;
    save_capture_if_requested(
        window, output_dir, QStringLiteral("timeline-discoverable-three-real-panels-failure"),
        &triad_failure_captures);
    return fail(QStringLiteral("The Mixer must be an open native popup with visible dock content, master strip, and track fader."));
  }
  auto* effect_list = find_visible_named_item(workspace, QStringLiteral("timelineEffectLibraryList"));
  if (effect_list == nullptr || effect_list->property("count").toInt() <= 0)
    return fail(QStringLiteral("The Effects panel must render real effect discovery rows instead of an empty placeholder."));
  auto* effect_library_panel = find_named_object(
      workspace, QStringLiteral("timelineEffectsLibraryPanel"));
  QVariant visible_effects_value;
  if (!timeline_discoverable_invoke(effect_library_panel, "buildEffectDiscoveryItems", {},
                                    &visible_effects_value) ||
      visible_effects_value.toList().isEmpty())
    return fail(QStringLiteral("The Effects browser must expose its filtered effect rows for layout review."));
  const auto first_effect_name = visible_effects_value.toList().first().toMap()
                                     .value(QStringLiteral("name")).toString();
  auto* first_effect_row = find_visible_named_item(
      workspace, QStringLiteral("timelineEffectLibraryRow-") + first_effect_name);
  if (first_effect_row == nullptr ||
      std::abs(first_effect_row->height() - first_effect_row->property("compactRowHeight").toDouble()) > 0.5 ||
      effect_list->height() < first_effect_row->height() * 2.0)
    return fail(QStringLiteral("The Effects browser must fit compact live-thumbnail rows in its visible viewport."));
  if (!save_frame(QStringLiteral("timeline-discoverable-effects-color-mixer")))
    return fail(QStringLiteral("The coexisting real panel capture could not be saved."));

  QVariant dock_snapshot;
  if (!timeline_discoverable_invoke(dock_state, "snapshot", {}, &dock_snapshot))
    return fail(QStringLiteral("The live Timeline dock state could not be snapshotted."));
  const auto dock_snapshot_json = QString::fromUtf8(
      QJsonDocument::fromVariant(dock_snapshot).toJson(QJsonDocument::Compact));
  if (dock_snapshot_json.isEmpty())
    return fail(QStringLiteral("The live Timeline dock state snapshot was empty."));

  const auto initial_hash = document->deterministicDocumentHash();
  const auto initial_dirty = document->dirty();
  const auto initial_selection = document->selectedClipIds();
  const auto initial_playhead = document->sequencePlayheadSeconds();
  auto* viewport = find_visible_named_item(workspace, QStringLiteral("timelineViewport"));
  auto* palette = find_named_object(workspace, QStringLiteral("timelineToolPalette"));
  if (viewport == nullptr || palette == nullptr)
    return fail(QStringLiteral("The active native Timeline workspace lost its viewport or direct tool palette."));

  if (!open_panel(QStringLiteral("source")))
    return fail(QStringLiteral("The Source panel must focus from its labeled Panels menu entry."));
  QTest::keyClick(window, Qt::Key_F);
  if (!wait_for_condition([&] {
        return document->hasSourceClip() &&
               document->sourceClip().value(QStringLiteral("clipId")).toString() == video_id;
      }, 5000, "timeline-discoverable-match-frame"))
    return fail(QStringLiteral("The Match Frame keyboard command must load the selected timeline clip into Source."));
  if (document->dirty() != initial_dirty || document->selectedClipIds() != initial_selection ||
      std::abs(document->sequencePlayheadSeconds() - initial_playhead) > 0.001)
    return fail(QStringLiteral("Match Frame must preserve document dirty state, selection, and Program playhead."));
  if (!save_frame(QStringLiteral("timeline-discoverable-source-match-frame")))
    return fail(QStringLiteral("The Source Match Frame capture could not be saved."));

  // Restore the authored panel arrangement using its persisted snapshot and
  // let an actual workspace round trip recreate the retained layout.
  auto* workspace_settings = find_named_object(workspace, QStringLiteral("timelineWorkspaceSettings"));
  QVariant save_layout_result;
  if (workspace_settings == nullptr ||
      !timeline_discoverable_invoke(workspace, "saveCurrentTimelineWorkspaceLayout", {}, &save_layout_result) ||
      !save_layout_result.toBool())
    return fail(QStringLiteral("Timeline dock and viewport preferences could not be saved."));
  if (!wait_for_condition([&] {
        const auto persisted = workspace_settings->property("panelDockSnapshotJson").toString();
        const auto parsed = QJsonDocument::fromJson(persisted.toUtf8()).object();
        return !persisted.isEmpty() &&
               parsed.value(QStringLiteral("panels")).toObject()
                       .value(QStringLiteral("effects")).toObject()
                       .value(QStringLiteral("dock")).toString() == QStringLiteral("left") &&
               parsed.value(QStringLiteral("panels")).toObject()
                       .value(QStringLiteral("color")).toObject()
                       .value(QStringLiteral("dock")).toString() == QStringLiteral("right") &&
               parsed.value(QStringLiteral("panels")).toObject()
                       .value(QStringLiteral("mixer")).toObject()
                       .value(QStringLiteral("dock")).toString() == QStringLiteral("bottom");
      }, 5000, "timeline-discoverable-dock-persistence"))
    return fail(QStringLiteral("Timeline preferences must persist the actual Effects, Color, and Mixer dock placements."));
  const auto maximum_scroll = qMax(0.0, viewport->property("contentWidth").toDouble() - viewport->width());
  const auto requested_scroll = maximum_scroll * 0.45;
  viewport->setProperty("contentX", requested_scroll);
  process_layout_events(8, "timeline-discoverable-scroll-save");
  const auto saved_scroll = workspace_settings->property("timelineScrollX").toDouble();
  if (maximum_scroll > 40.0 && std::abs(saved_scroll - requested_scroll) > 2.0)
    return fail(QStringLiteral("Timeline hand/scroll position did not update the preferred saved scroll offset."));
  if (!QMetaObject::invokeMethod(root, "switchToWorkspaceIndex", Q_RETURN_ARG(QVariant, routed),
          Q_ARG(QVariant, root->property("libraryWorkspaceIndex"))) || !routed.toBool() ||
      !wait_for_condition([&] { return active_workspace(root) != nullptr &&
          active_workspace(root)->objectName() != QStringLiteral("timelineWorkspace"); },
          10000, "timeline-discoverable-switch-away"))
    return fail(QStringLiteral("The native workspace did not leave Timeline for its layout recreation check."));
  if (!QMetaObject::invokeMethod(root, "switchToWorkspaceIndex", Q_RETURN_ARG(QVariant, routed),
          Q_ARG(QVariant, root->property("timelineWorkspaceIndex"))) || !routed.toBool() ||
      !wait_for_condition([&] { return active_workspace(root) != nullptr &&
          active_workspace(root)->objectName() == QStringLiteral("timelineWorkspace") &&
          !root->property("workspaceTransitionActive").toBool() &&
          named_item_visible(root, QStringLiteral("timelineViewport")); },
          15000, "timeline-discoverable-switch-back"))
    return fail(QStringLiteral("The Timeline workspace could not recreate from its saved dock and scroll preferences."));
  workspace = active_workspace(root);
  dock_state = find_named_object(workspace, QStringLiteral("timelinePanelDockState"));
  workspace_settings = find_named_object(workspace, QStringLiteral("timelineWorkspaceSettings"));
  viewport = find_visible_named_item(workspace, QStringLiteral("timelineViewport"));
  if (dock_state == nullptr || workspace_settings == nullptr || viewport == nullptr ||
      workspace_settings->property("timelineScrollX").toDouble() + 2.0 < saved_scroll)
    return fail(QStringLiteral("Timeline did not restore its preferred scroll or dock settings after workspace recreation."));
  for (const auto& expected : {QPair<QString, QString>{QStringLiteral("effects"), QStringLiteral("left")},
                               QPair<QString, QString>{QStringLiteral("color"), QStringLiteral("right")},
                               QPair<QString, QString>{QStringLiteral("mixer"), QStringLiteral("bottom")}}) {
    QVariant actual;
    if (!timeline_discoverable_invoke(dock_state, "dock", {expected.first}, &actual) ||
        actual.toString() != expected.second)
      return fail(QStringLiteral("The recreated workspace did not restore the saved Effects/Color/Mixer dock arrangement."));
  }

  const auto route_focus_again = [&] {
    window->requestActivate();
    return wait_for_condition([&] { return window->isActive() && window->isExposed(); },
                              5000, "timeline-discoverable-refocus");
  };
  if (!route_focus_again())
    return fail(QStringLiteral("The recreated Timeline window did not regain native focus."));
  workspace = active_workspace(root);
  viewport = find_visible_named_item(workspace, QStringLiteral("timelineViewport"));
  palette = find_named_object(workspace, QStringLiteral("timelineToolPalette"));
  if (viewport == nullptr || palette == nullptr)
    return fail(QStringLiteral("Timeline viewport state was not restored after its workspace round trip."));

  // Track Select routes from the actual pointer lane and time.
  if (!timeline_discoverable_pointer_click(
          find_visible_named_item(workspace, QStringLiteral("timelineTool_track-select-forward"))) ||
      document->activeTool() != QStringLiteral("track-select"))
    return fail(QStringLiteral("The forward Track Select palette button did not receive native pointer input."));
  const auto zoom_before_tools = workspace->property("timelineZoom").toDouble();
  viewport->setProperty("contentX", 0.0);
  process_layout_events(3, "timeline-discoverable-track-select-position");
  auto* still_item = timeline_discoverable_video_clip_item(workspace, QStringLiteral("Discoverable Still"));
  if (still_item == nullptr)
    return fail(QStringLiteral("Track Select requires the authored still clip to be visible on its actual video lane."));
  const auto track_point = still_item->mapToScene(
      QPointF(qBound(12.0, zoom_before_tools * 0.5, still_item->width() - 12.0),
              still_item->height() * 0.5)).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, track_point);
  process_layout_events(5, "timeline-discoverable-track-select-pointer");
  if (!document->selectedClipIds().contains(still_id) ||
      !document->selectedClipIds().contains(video_id)) {
    std::cerr << "Track Select pointer=" << track_point.x() << ',' << track_point.y()
              << " activeTool=" << document->activeTool().toStdString()
              << " selection=" << document->selectedClipIds().join(',').toStdString()
              << " expected=" << still_id.toStdString() << ',' << video_id.toStdString()
              << " scroll=" << viewport->property("contentX").toDouble() << ','
              << viewport->property("contentY").toDouble() << '\n';
    return fail(QStringLiteral("Track Select must select the clips at and after the actual pointer time on the clicked track."));
  }
  document->selectClip(video_index);
  document->seekSequenceSeconds(5.0);

  // Hand drag and Zoom click are viewport interactions only.
  const auto viewport_hash = document->deterministicDocumentHash();
  const auto viewport_dirty = document->dirty();
  const auto viewport_selection = document->selectedClipIds();
  const auto viewport_playhead = document->sequencePlayheadSeconds();
  const auto zoom_before = workspace->property("timelineZoom").toDouble();
  const auto max_x = qMax(0.0, viewport->property("contentWidth").toDouble() - viewport->width());
  const auto pan_start_x = qMin(viewport->width() - 24.0,
                                qMax(24.0, 10.05 * zoom_before - max_x * 0.5));
  viewport->setProperty("contentX", max_x * 0.5);
  if (!timeline_discoverable_pointer_click(
          find_visible_named_item(workspace, QStringLiteral("timelineTool_hand"))) ||
      document->activeTool() != QStringLiteral("hand"))
    return fail(QStringLiteral("The Hand palette button did not activate from a native pointer click."));
  const auto hand_hash = document->deterministicDocumentHash();
  const auto content_x_before_pan = viewport->property("contentX").toDouble();
  if (!timeline_discoverable_pointer_drag(viewport,
          QPointF(pan_start_x, qMax(48.0, viewport->height() * 0.45)),
          QPointF(pan_start_x - 60.0, qMax(48.0, viewport->height() * 0.45))) ||
      std::abs(viewport->property("contentX").toDouble() - content_x_before_pan) < 20.0)
    return fail(QStringLiteral("Dragging the Hand tool over the real Timeline canvas must pan its viewport."));
  if (document->deterministicDocumentHash() != hand_hash)
    return fail(QStringLiteral("A Hand gesture must preserve every serialized document field."));
  if (!timeline_discoverable_pointer_click(
          find_visible_named_item(workspace, QStringLiteral("timelineTool_zoom"))) ||
      document->activeTool() != QStringLiteral("zoom"))
    return fail(QStringLiteral("The Zoom palette button did not activate from a native pointer click."));
  const auto zoom_hash = document->deterministicDocumentHash();
  const auto zoom_click = viewport->mapToScene(
      QPointF(viewport->width() * 0.52, qMax(48.0, viewport->height() * 0.45))).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, zoom_click);
  process_layout_events(5, "timeline-discoverable-zoom-pointer");
  if (std::abs(workspace->property("timelineZoom").toDouble() - zoom_before) < 0.1)
    return fail(QStringLiteral("A Zoom-tool click on the real Timeline canvas must change magnification."));
  if (document->deterministicDocumentHash() != zoom_hash)
    return fail(QStringLiteral("A Zoom gesture must preserve every serialized document field."));
  // Tool choice is a serialized editor preference. Restore it through the
  // actual palette before comparing the complete document with its baseline.
  if (!timeline_discoverable_pointer_click(find_visible_named_item(
          workspace, QStringLiteral("timelineTool_track-select-forward"))) ||
      document->activeTool() != QStringLiteral("track-select"))
    return fail(QStringLiteral("The previous tool must be restorable through the visible palette."));
  if (document->deterministicDocumentHash() != viewport_hash || document->dirty() != viewport_dirty ||
      document->selectedClipIds() != viewport_selection ||
      std::abs(document->sequencePlayheadSeconds() - viewport_playhead) > 0.001)
    return fail(QStringLiteral("Hand and Zoom must preserve document content, dirty state, selection, and playhead."));
  if (!save_frame(QStringLiteral("timeline-discoverable-hand-zoom")))
    return fail(QStringLiteral("The Hand and Zoom native interaction capture could not be saved."));

  // Exercise Rate Stretch by dragging the selected video's real right edge.
  if (!timeline_discoverable_invoke_without_result(workspace, "zoomToFit", {}))
    return fail(QStringLiteral("The Timeline viewport could not zoom to the authored edit."));
  viewport->setProperty("contentX", 0.0);
  document->selectClip(video_index);
  const auto rate_before_clip = timeline_discoverable_clip(
      document, QStringLiteral("Discoverable Video"), QStringLiteral("video"));
  const auto rate_before = rate_before_clip.value(QStringLiteral("playbackRate")).toDouble();
  const auto duration_before = rate_before_clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble();
  auto* rate_button = find_visible_named_item(workspace, QStringLiteral("timelineTool_rate-stretch"));
  auto* rate_clip_item = timeline_discoverable_video_clip_item(workspace, QStringLiteral("Discoverable Video"));
  if (!rate_button || !rate_clip_item ||
      !timeline_discoverable_pointer_click(rate_button))
    return fail(QStringLiteral("Rate Stretch needs its visible tool and selected video clip in the real Timeline."));
  rate_clip_item = timeline_discoverable_video_clip_item(workspace, QStringLiteral("Discoverable Video"));
  const auto rate_start = QPointF(rate_clip_item ? rate_clip_item->width() - 4.0 : 0.0,
                                  rate_clip_item ? rate_clip_item->height() * 0.5 : 0.0);
  const auto rate_end = rate_start + QPointF(qMax(48.0, zoom_before * 0.8), 0.0);
  if (!rate_clip_item || !timeline_discoverable_pointer_drag(rate_clip_item, rate_start, rate_end))
    return fail(QStringLiteral("The selected video Rate Stretch handle did not receive native pointer input."));
  const auto rate_after_clip = timeline_discoverable_clip(
      document, QStringLiteral("Discoverable Video"), QStringLiteral("video"));
  const auto duration_after = rate_after_clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble();
  const auto rate_after = rate_after_clip.value(QStringLiteral("playbackRate")).toDouble();
  if (duration_after <= duration_before + 0.25 || rate_after >= rate_before ||
      !document->canUndo())
    return fail(QStringLiteral("A native Rate Stretch drag must change the selected video's duration and playback rate."));
  document->undo();
  const auto after_rate_undo = timeline_discoverable_clip(
      document, QStringLiteral("Discoverable Video"), QStringLiteral("video"));
  if (std::abs(after_rate_undo.value(QStringLiteral("trimmedDurationSeconds")).toDouble() - duration_before) > 0.02 ||
      std::abs(after_rate_undo.value(QStringLiteral("playbackRate")).toDouble() - rate_before) > 0.02)
    return fail(QStringLiteral("One undo should restore the video duration and rate changed by the pointer gesture."));
  document->redo();
  if (!save_frame(QStringLiteral("timeline-discoverable-rate-stretch")))
    return fail(QStringLiteral("The native Rate Stretch capture could not be saved."));

  // Rate Stretch owns the entire clip, including the left trim hit area.
  // Keep the playhead away from that edge so its independent scrub handle
  // cannot intercept the intended clip gesture.
  document->seekSequenceSeconds(5.0);
  process_layout_events(3, "timeline-discoverable-left-rate-target");
  const auto left_rate_before = timeline_discoverable_clip(
      document, QStringLiteral("Discoverable Video"), QStringLiteral("video"));
  rate_clip_item = timeline_discoverable_video_clip_item(workspace, QStringLiteral("Discoverable Video"));
  if (!rate_clip_item || !timeline_discoverable_pointer_drag(rate_clip_item,
          QPointF(2.0, rate_clip_item->height() * 0.5),
          QPointF(42.0, rate_clip_item->height() * 0.5)))
    return fail(QStringLiteral("The Rate Stretch tool must receive native input over the left trim hit area."));
  const auto left_rate_after = timeline_discoverable_clip(
      document, QStringLiteral("Discoverable Video"), QStringLiteral("video"));
  for (const auto& field : {"startSeconds", "trimStartSeconds", "trimEndSeconds"}) {
    if (std::abs(left_rate_after.value(field).toDouble() - left_rate_before.value(field).toDouble()) > 0.001)
      return fail(QStringLiteral("Rate Stretch over the left edge must preserve start and source trim boundaries."));
  }
  if (left_rate_after.value("trimmedDurationSeconds").toDouble() <=
      left_rate_before.value("trimmedDurationSeconds").toDouble() + 0.1)
    return fail(QStringLiteral("The left-edge Rate Stretch gesture must change duration through the rate tool."));
  document->undo();
  if (std::abs(timeline_discoverable_clip(document, QStringLiteral("Discoverable Video"), QStringLiteral("video"))
          .value("trimmedDurationSeconds").toDouble() - left_rate_before.value("trimmedDurationSeconds").toDouble()) > 0.02)
    return fail(QStringLiteral("One Undo must restore the left-edge Rate Stretch gesture."));

  // Pen target setup only exposes the existing Gain envelope. A native click
  // on its real lane must add one editable track keyframe and one undo step.
  const auto audio_track = timeline_discoverable_track(document, QStringLiteral("audio-1"));
  if (audio_track.isEmpty())
    return fail(QStringLiteral("The analyzed video must create its paired audio track for the Pen proof."));
  if (!timeline_discoverable_invoke_without_result(workspace, "toggleTrackAutomationLane",
          {audio_track, QStringLiteral("audioGainDb")}))
    return fail(QStringLiteral("The actual audio Gain lane could not be exposed for Pen."));
  process_layout_events(8, "timeline-discoverable-audio-gain-lane");
  QPointer<QQuickItem> audio_gain_lane;
  for (auto* candidate : find_named_items(workspace, QStringLiteral("timelineTrackAutomationLane"))) {
    if (item_visible(candidate) &&
        candidate->property("parameter").toString() == QStringLiteral("audioGainDb")) {
      audio_gain_lane = candidate;
      break;
    }
  }
  if (audio_gain_lane == nullptr ||
      !timeline_discoverable_pointer_click(
          find_visible_named_item(workspace, QStringLiteral("timelineTool_pen"))) ||
      document->activeTool() != QStringLiteral("pen"))
    return fail(QStringLiteral("Pen needs a visible real audioGainDb automation lane and the active Pen tool."));
  // Opening a track envelope can place it below the compact viewport. Reveal
  // the actual lane before sending native input, and reject clipped targets.
  const auto lane_y = audio_gain_lane->mapToItem(viewport, QPointF()).y()
                      + viewport->property("contentY").toDouble();
  viewport->setProperty("contentY", qBound(0.0,
      lane_y - qMax(0.0, (viewport->height() - audio_gain_lane->height()) * 0.5),
      qMax(0.0, viewport->property("contentHeight").toDouble() - viewport->height())));
  process_layout_events(5, "timeline-discoverable-pen-lane-reveal");
  const auto audio_track_id = audio_track.value(QStringLiteral("trackId")).toString();
  const auto before_automation = timeline_discoverable_track(document, audio_track_id)
                                     .value(QStringLiteral("automationKeyframes")).toList();
  const auto pen_seconds = qMin(5.5, document->totalDurationSecondsValue() - 0.1);
  const auto pen_point = audio_gain_lane->mapToScene(
      QPointF(pen_seconds * workspace->property("timelineZoom").toDouble(),
              audio_gain_lane->height() * 0.28)).toPoint();
  if (!item_scene_rect(viewport).contains(pen_point) ||
      !QRectF(0.0, 0.0, window->width(), window->height()).contains(pen_point))
    return fail(QStringLiteral("The Pen target must lie inside both the real track viewport and the native window."));
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pen_point);
  process_layout_events(5, "timeline-discoverable-pen-pointer");
  const auto after_automation = timeline_discoverable_track(document, audio_track_id)
                                   .value(QStringLiteral("automationKeyframes")).toList();
  QVariantMap pen_keyframe;
  for (const auto& value : after_automation) {
    const auto candidate = value.toMap();
    if (candidate.value(QStringLiteral("parameter")).toString() == QStringLiteral("audioGainDb") &&
        std::abs(candidate.value(QStringLiteral("seconds")).toDouble() - pen_seconds) < 0.05) {
      pen_keyframe = candidate;
      break;
    }
  }
  if (pen_keyframe.isEmpty() || after_automation.size() <= before_automation.size() ||
      !std::isfinite(pen_keyframe.value(QStringLiteral("value")).toDouble()) ||
      !document->canUndo())
    return fail(QStringLiteral("A native Pen click must write a real audioGainDb keyframe on the selected track."));
  document->undo();
  if (timeline_discoverable_track(document, audio_track_id)
          .value(QStringLiteral("automationKeyframes")).toList().size() != before_automation.size())
    return fail(QStringLiteral("One undo should remove the exact audioGainDb Pen keyframe."));
  document->redo();
  if (timeline_discoverable_track(document, audio_track_id)
          .value(QStringLiteral("automationKeyframes")).toList().size() != after_automation.size())
    return fail(QStringLiteral("One redo should restore the audioGainDb Pen keyframe."));
  if (!save_frame(QStringLiteral("timeline-discoverable-pen-audio-gain")))
    return fail(QStringLiteral("The native Pen keyframe capture could not be saved."));

  const auto automation_point = [&]() -> QQuickItem* {
    audio_gain_lane = nullptr;
    for (auto* lane : find_named_items(workspace, QStringLiteral("timelineTrackAutomationLane"))) {
      if (item_visible(lane) && lane->property("parameter").toString() == "audioGainDb") {
        audio_gain_lane = lane;
        break;
      }
    }
    if (!audio_gain_lane) return nullptr;
    for (auto* child : audio_gain_lane->childItems()) {
      const auto point = child->property("modelData").toMap();
      if (child->isVisible() && point.value("parameter").toString() == "audioGainDb")
        return child;
    }
    return nullptr;
  };
  if (!click_named(QStringLiteral("timelineTool_zoom")) || document->activeTool() != "zoom")
    return fail(QStringLiteral("Zoom must remain available after authoring an envelope."));
  auto* point = automation_point();
  if (!point || !item_scene_rect(viewport).contains(item_scene_rect(point).center()))
    return fail(QStringLiteral("The authored envelope handle must be inside the viewport for the hit-priority regression."));
  const auto zoom_point_hash = document->deterministicDocumentHash();
  const auto zoom_point_playhead = document->sequencePlayheadSeconds();
  const auto zoom_point_scale = workspace->property("timelineZoom").toDouble();
  if (!timeline_discoverable_pointer_click(point) ||
      workspace->property("timelineZoom").toDouble() <= zoom_point_scale ||
      document->deterministicDocumentHash() != zoom_point_hash ||
      std::abs(document->sequencePlayheadSeconds() - zoom_point_playhead) > 0.001)
    return fail(QStringLiteral("Zoom on a keyframe handle must zoom without editing or seeking."));
  if (!click_named(QStringLiteral("timelineTool_hand")) || document->activeTool() != "hand")
    return fail(QStringLiteral("Hand must remain available over authored envelope handles."));
  point = automation_point();
  const auto hand_point_hash = document->deterministicDocumentHash();
  const auto hand_point_scroll = viewport->property("contentX").toDouble();
  if (!point || !timeline_discoverable_pointer_drag(point,
          QPointF(point->width() * 0.5, point->height() * 0.5),
          QPointF(point->width() * 0.5 - 36.0, point->height() * 0.5)) ||
      viewport->property("contentX").toDouble() <= hand_point_scroll + 10.0 ||
      document->deterministicDocumentHash() != hand_point_hash ||
      std::abs(document->sequencePlayheadSeconds() - zoom_point_playhead) > 0.001)
    return fail(QStringLiteral("Hand on a keyframe handle must pan without editing or seeking."));
  if (!click_named(QStringLiteral("timelineTool_pen")))
    return fail(QStringLiteral("The Pen tool must be restorable after the navigation hit-priority checks."));

  const auto persisted_dock_json = workspace_settings->property("panelDockSnapshotJson").toString();
  const auto final_scroll = workspace_settings->property("timelineScrollX").toDouble();
  if (captures.size() != 7 || persisted_dock_json.isEmpty() ||
      document->selectedClipIds().isEmpty() || !window->isActive() || !window->isExposed())
    return fail(QStringLiteral("Native Timeline proof needs seven captures, persisted dock/scroll state, selection, and an active exposed window."));
  const QJsonObject receipt{
      {QStringLiteral("schema"), QStringLiteral("timeline-discoverable-workspace-native-proof:v1")},
      {QStringLiteral("nativePlatform"), QGuiApplication::platformName()},
      {QStringLiteral("windowActive"), window->isActive()},
      {QStringLiteral("windowExposed"), window->isExposed()},
      {QStringLiteral("windowWidth"), window->width()},
      {QStringLiteral("windowHeight"), window->height()},
      {QStringLiteral("wideDisplayAvailable"), available.width() >= 1500},
      {QStringLiteral("panelButtons"), panel_ids.size()},
      {QStringLiteral("directTools"), tool_ids.size()},
      {QStringLiteral("effectsColorMixerCoexist"), true},
      {QStringLiteral("effectsLibraryRows"), effect_list->property("count").toInt()},
      {QStringLiteral("sourceMatchFrameClipId"), document->sourceClip().value(QStringLiteral("clipId")).toString()},
      {QStringLiteral("trackSelectIds"), QJsonArray::fromStringList(document->selectedClipIds())},
      {QStringLiteral("rateStretchDurationBefore"), duration_before},
      {QStringLiteral("rateStretchDurationAfter"), duration_after},
      {QStringLiteral("rateStretchPlaybackRateAfter"), rate_after},
      {QStringLiteral("penAudioGainKeyframe"), QJsonObject::fromVariantMap(pen_keyframe)},
      {QStringLiteral("savedScrollX"), final_scroll},
      {QStringLiteral("savedDockSnapshotBytes"), persisted_dock_json.size()},
      {QStringLiteral("documentDirty"), document->dirty()},
      {QStringLiteral("documentHash"), document->deterministicDocumentHash()},
      {QStringLiteral("captures"), captures}};
  QFile receipt_file(QDir(output_dir).filePath(QStringLiteral("timeline-discoverable-workspace-proof.json")));
  if (!receipt_file.open(QIODevice::WriteOnly) ||
      receipt_file.write(QJsonDocument(receipt).toJson(QJsonDocument::Indented)) <= 0)
    return fail(QStringLiteral("The native Timeline workspace proof receipt could not be written."));
  std::cout << QJsonDocument(receipt).toJson(QJsonDocument::Compact).constData();
  return EXIT_SUCCESS;
}

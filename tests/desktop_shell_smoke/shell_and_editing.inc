    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
	    return EXIT_SUCCESS;
	  }
  engine.load(QUrl::fromLocalFile(main_qml_path));
  QCoreApplication::processEvents();

  if (engine.rootObjects().isEmpty()) {
    return fail("desktop shell should load Main.qml");
  }

  QObject* root = engine.rootObjects().constFirst();
  if (root == nullptr) {
    return fail("desktop shell should expose a root object");
  }
  auto* root_window = qobject_cast<QQuickWindow*>(root);
  if (root_window == nullptr) {
    return fail("desktop shell visual proof requires the root QML object to be a window");
  }
  new photo_editor::desktop::RecentUserInputFilter(root_window);
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_CAROUSEL_RESPONSIVE_LAYOUT")) {
    root->setProperty("currentWorkspace", root->property("carouselWorkspaceIndex"));
    process_layout_events(8);
    auto* carousel_workspace =
        root->findChild<QObject*>(QStringLiteral("carouselWorkspace"));
    if (carousel_workspace == nullptr) {
      return fail("Carousel responsive layout proof should load the real workspace");
    }
    const auto verify_layout = [&](const int view_width,
                                   const int view_height,
                                   const QString& expected_mode,
                                   const QString& expected_inspector,
                                   const int expected_inspector_width,
                                   const int expected_composer_width,
                                   const int expected_composer_minimum,
                                   const int expected_board_strip_height,
                                   const int expected_artboard_height) {
      QVariant result;
      if (!QMetaObject::invokeMethod(
              carousel_workspace,
              "carouselResponsiveLayoutMetrics",
              Q_RETURN_ARG(QVariant, result),
              Q_ARG(QVariant, view_width),
              Q_ARG(QVariant, view_height))) {
        return false;
      }
      const auto metrics = result.toMap();
      return metrics.value(QStringLiteral("mode")).toString() == expected_mode &&
             metrics.value(QStringLiteral("inspectorPresentation")).toString() ==
                 expected_inspector &&
             metrics.value(QStringLiteral("inspectorWidth")).toInt() ==
                 expected_inspector_width &&
             metrics.value(QStringLiteral("composerWidth")).toInt() ==
                 expected_composer_width &&
             metrics.value(QStringLiteral("composerMinimum")).toInt() ==
                 expected_composer_minimum &&
             metrics.value(QStringLiteral("boardStripHeight")).toInt() ==
                 expected_board_strip_height &&
             metrics.value(QStringLiteral("artboardHeight")).toInt() ==
                 expected_artboard_height &&
             metrics.value(QStringLiteral("horizontalNonOverlap")).toBool() &&
             metrics.value(QStringLiteral("verticalNonOverlap")).toBool() &&
             metrics.value(QStringLiteral("supported")).toBool();
    };
    if (carousel_workspace->property("carouselResponsiveLayoutContract").toString() !=
            QStringLiteral("carousel-responsive-layout:v1|modes:compact-under-1050,standard-1050-to-1719,wide-1720-plus|supported:760x600,1280x720,1920x1080|compact-inspector:right-drawer|standard-inspector:300-scaled|wide-inspector:336-scaled|composer-min:360-460-600|board-strip:112-136-152|header:condensed|empty-card:bounded|state-preserved:true|no-elide:true|no-overlap:true") ||
        !verify_layout(
            760, 600, QStringLiteral("compact"), QStringLiteral("drawer"), 0, 700, 360, 112, 424) ||
        !verify_layout(
            1280, 720, QStringLiteral("standard"), QStringLiteral("inline"), 300, 920, 460, 136, 520) ||
        !verify_layout(
            1920, 1080, QStringLiteral("wide"), QStringLiteral("inline"), 336, 1524, 600, 152, 864)) {
      return fail("Carousel responsive layout proof should preserve non-overlapping composer, inspector, artboard, and board-strip geometry at compact, standard, and wide sizes");
    }
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }
  const auto visual_proof_dir = qEnvironmentVariableIsSet("PHOTO_EDITOR_VISUAL_PROOF_DIR")
                                    ? QString::fromUtf8(qgetenv("PHOTO_EDITOR_VISUAL_PROOF_DIR"))
                                    : runtime_root.filePath(QStringLiteral("desktop-visual-proof"));
  if (!QDir().mkpath(visual_proof_dir)) {
    return fail("desktop shell visual proof should create its artifact directory");
  }
  QJsonArray visual_proof_manifest;
  root->setProperty("width", 1600);
  root->setProperty("height", 1000);
  QCoreApplication::processEvents();

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_TIMELINE_UI_PROOF")) {
    const auto proof_photo = runtime_root.filePath(QStringLiteral("timeline-proof-photo.png"));
    if (!write_overlay_visual_fixture(proof_photo, QStringLiteral("saturated"))) {
      return fail("Timeline UI proof should create its media fixture");
    }
    trace_smoke_progress("timeline proof media fixture written");
    // Build the proof document while the Timeline Loader is inactive. Emitting
    // controller state into a half-constructed lazy QML tree makes Qt's
    // QString/QVariant wrappers race nondeterministically on some scale runs.
    timeline_document.createTimeline(QStringLiteral("Timeline UI proof"));
    timeline_document.addClipFromGallery(
        QStringLiteral("timeline-proof-video"),
        proof_photo,
        QStringLiteral("Opening shot"),
        10.0,
        QUrl::fromLocalFile(proof_photo).toString());
    trace_smoke_progress("timeline proof base clip added");
    timeline_document.loadSourceClipFromGallery(
        QStringLiteral("timeline-proof-source"),
        proof_photo,
        QStringLiteral("Source insert"),
        10.0,
        QUrl::fromLocalFile(proof_photo).toString());
    timeline_document.setSourceInSeconds(1.0);
    timeline_document.setSourceOutSeconds(8.0);
    timeline_document.seekSequenceSeconds(5.0);
    timeline_document.insertSourceClipAtPlayhead();
    trace_smoke_progress("timeline proof source insert complete");
    auto proof_audio_track_row = -1;
    const auto proof_tracks = timeline_document.sequenceTracks();
    for (int row = 0; row < proof_tracks.size(); ++row) {
      if (proof_tracks.at(row).toMap().value(QStringLiteral("trackKind")).toString() ==
          QStringLiteral("audio")) {
        proof_audio_track_row = row;
        break;
      }
    }
    if (proof_audio_track_row < 0) {
      return fail("Timeline UI proof should create an audio automation target");
    }
    timeline_document.seekSequenceSeconds(0.25);
    if (!timeline_document.toggleTrackAutomationKeyframe(
            proof_audio_track_row, QStringLiteral("audioGainDb"))) {
      return fail("Timeline UI proof should create track automation points");
    }
    timeline_document.setTrackAutomationValue(
        proof_audio_track_row, QStringLiteral("audioGainDb"), -4.0);
    timeline_document.seekSequenceSeconds(1.0);
    timeline_document.setTrackAutomationValue(
        proof_audio_track_row, QStringLiteral("audioGainDb"), -8.0);
    timeline_document.seekSequenceSeconds(5.0);
    timeline_document.addTitleClip();
    timeline_document.generateCaptionsFromTranscript(
        QStringLiteral("Build the story clearly. Keep every moment readable."));
    trace_smoke_progress("timeline proof title and captions complete");
    timeline_document.selectClip(0);
    timeline_document.setSelectedTrimEndSeconds(4.5);
    timeline_document.setSelectedClipOpacity(0.86);
    timeline_document.addSequenceMarker(QStringLiteral("Review opening"));
    auto title_clip_index = -1;
    const auto proof_clips = timeline_document.clips();
    for (int index = 0; index < proof_clips.size(); ++index) {
      if (proof_clips.at(index).toMap().value(QStringLiteral("mediaType")).toString() ==
          QStringLiteral("title")) {
        title_clip_index = index;
        break;
      }
    }
    if (title_clip_index < 0) {
      return fail("Timeline UI proof should create a title clip");
    }
    timeline_document.selectClip(title_clip_index);
    timeline_document.updateSelectedTitle(QVariantMap{
        {QStringLiteral("text"), QStringLiteral("Polished social cut")},
        {QStringLiteral("fontSize"), 56.0},
    });
    const auto title_motion_start_seconds =
        timeline_document.currentClip().value(QStringLiteral("startSeconds")).toDouble();
    if (!timeline_document.applySelectedClipMotionPreset(QStringLiteral("title-pop"))) {
      return fail("Timeline UI proof should apply an editable title entrance and exit preset");
    }
    timeline_document.seekSequenceSeconds(title_motion_start_seconds + 0.15);
    if (timeline_document.currentClip().value(QStringLiteral("mediaType")).toString() !=
            QStringLiteral("title") ||
        timeline_document.currentClip().value(QStringLiteral("opacity")).toDouble() <= 0.5) {
      return fail("Timeline UI proof should keep the animated title selected while scrubbing its entrance");
    }
    const auto expected_clip_count = timeline_document.clipCount();
    if (!timeline_document.saveTimeline() || timeline_document.savedDocumentPath().isEmpty()) {
      return fail("Timeline UI proof should save its test document");
    }
    const auto saved_timeline_path = timeline_document.savedDocumentPath();
    timeline_document.closeTimeline();
    if (!timeline_document.loadTimeline(saved_timeline_path) ||
        timeline_document.clipCount() != expected_clip_count) {
      return fail("Timeline UI proof should reopen the saved test document without losing clips");
    }
    if (!QDir().mkpath(visual_proof_dir)) {
      return fail("Timeline UI proof should create its evidence directory");
    }
    const auto persistent_timeline_path =
        QDir(visual_proof_dir).filePath(QStringLiteral("timeline-ui-proof-document.json"));
    QFile::remove(persistent_timeline_path);
    if (!QFile::copy(saved_timeline_path, persistent_timeline_path)) {
      return fail("Timeline UI proof should preserve its saved and reopened document");
    }

    trace_smoke_progress("timeline proof route begin");
    QVariant routed_to_timeline;
    if (!QMetaObject::invokeMethod(
            root,
            "switchToWorkspaceIndex",
            Q_RETURN_ARG(QVariant, routed_to_timeline),
            Q_ARG(QVariant, root->property("timelineWorkspaceIndex"))) ||
        !routed_to_timeline.toBool() ||
        !wait_for_condition(
            [root]() {
              return root->property("currentWorkspace").toInt() ==
                         root->property("timelineWorkspaceIndex").toInt() &&
                     !root->property("workspaceTransitionActive").toBool() &&
                     find_named_object(root, QStringLiteral("timelineWorkspace")) != nullptr &&
                     find_named_object(root, QStringLiteral("timelineProgramMonitor")) != nullptr &&
                     find_named_object(root, QStringLiteral("timelineViewport")) != nullptr;
            },
            10000)) {
      return fail("Timeline UI proof should route to a fully loaded Timeline workspace");
    }
    trace_smoke_progress("timeline proof route ready");
    timeline_document.selectClip(0);
    process_layout_events(16);
    trace_smoke_progress("timeline proof fixture layout settled");

    auto* timeline_workspace = find_named_object(root, QStringLiteral("timelineWorkspace"));
    const QStringList required_timeline_objects = {
        QStringLiteral("timelineWorkspace"),
        QStringLiteral("timelineHeaderBar"),
        QStringLiteral("timelineSourceRail"),
        QStringLiteral("timelineProgramPanel"),
        QStringLiteral("timelineProgramMonitor"),
        QStringLiteral("timelineTransportBar"),
        QStringLiteral("timelineEffectsRail"),
        QStringLiteral("timelineSurfacePanel"),
        QStringLiteral("timelineToolBar"),
        QStringLiteral("timelineTrackHeaderColumn"),
        QStringLiteral("timelineViewport"),
    };
    if (timeline_workspace == nullptr ||
        timeline_workspace->property("timelineUiConsistencyContract").toString().isEmpty()) {
      return fail("Timeline UI proof should expose the styled workspace contract");
    }

    QVariantList loaded_track_rows =
        timeline_workspace->property("sequenceTrackModel").toList();
    QVariantMap proof_audio_track;
    for (const auto& track_value : loaded_track_rows) {
      const auto track = track_value.toMap();
      if (track.value(QStringLiteral("trackKind")).toString() == QStringLiteral("audio")) {
        proof_audio_track = track;
        break;
      }
    }
    if (proof_audio_track.value(QStringLiteral("trackId")).toString().isEmpty()) {
      return fail("Timeline UI proof should expose the stable audio track target");
    }
    if (!QMetaObject::invokeMethod(
            timeline_workspace,
            "toggleTrackAutomationLane",
            Q_ARG(QVariant, QVariant(proof_audio_track)),
            Q_ARG(QVariant, QVariant(QStringLiteral("audioGainDb"))))) {
      return fail("the audio track should expose its contextual automation lane");
    }
    process_layout_events(12);
    auto* open_curve_button = static_cast<QQuickItem*>(nullptr);
    for (auto* candidate : find_named_items(
             timeline_workspace, QStringLiteral("timelineTrackAutomationOpenCurveEditorButton"))) {
      if (item_visible(candidate) && candidate->window() == root_window) {
        open_curve_button = candidate;
        break;
      }
    }
    if (open_curve_button == nullptr) {
      return fail("an expanded audio automation lane should expose its full curve editor button");
    }
    if (!QMetaObject::invokeMethod(
            timeline_workspace,
            "openTrackAutomationCurveEditor",
            Q_ARG(QVariant, QVariant(proof_audio_track)),
            Q_ARG(QVariant, QVariant(QStringLiteral("audioGainDb"))))) {
      return fail("the full track curve editor should open from the automation lane");
    }
    process_layout_events(12);
    auto* curve_window = find_named_object(root, QStringLiteral("timelineKeyframeCurveWindow"));
    if (curve_window == nullptr || !curve_window->property("visible").toBool() ||
        timeline_workspace->property("curveEditorTarget").toString() != QStringLiteral("track") ||
        timeline_workspace->property("curveEditorTrackId").toString() !=
            proof_audio_track.value(QStringLiteral("trackId")).toString() ||
        !curve_window->property("trackAutomationMode").toBool() ||
        curve_window->property("curveKeyframes").toList().size() != 2) {
      return fail("track curve editing should open the same weighted value and speed editor on the selected track");
    }
    auto* batch_keyframe_menu = qobject_cast<QQuickItem*>(find_named_object(
        curve_window, QStringLiteral("timelineKeyframeBatchMenuButton")));
    if (batch_keyframe_menu == nullptr || !item_visible(batch_keyframe_menu)) {
      return fail("track curve editing should expose the shared multi-key batch operations");
    }
    curve_window->setProperty("visible", false);
    timeline_workspace->setProperty("curveEditorTarget", QStringLiteral("clip"));
    timeline_workspace->setProperty("curveEditorTrackId", QString{});
    QMetaObject::invokeMethod(
        timeline_workspace,
        "toggleTrackAutomationLane",
        Q_ARG(QVariant, QVariant(proof_audio_track)),
        Q_ARG(QVariant, QVariant(QStringLiteral("audioGainDb"))));
    process_layout_events(8);

    const auto settle_timeline_proof = [root_window]() {
      auto frame_has_content = [](const QImage& frame) {
        if (frame.isNull()) {
          return false;
        }
        qint64 luminance_sum = 0;
        qint64 sample_count = 0;
        qint64 chrome_luminance_sum = 0;
        qint64 chrome_sample_count = 0;
        for (int y = 0; y < frame.height(); y += 16) {
          for (int x = 0; x < frame.width(); x += 16) {
            const auto luminance = qGray(frame.pixel(x, y));
            luminance_sum += luminance;
            ++sample_count;
            if (y < 48) {
              chrome_luminance_sum += luminance;
              ++chrome_sample_count;
            }
          }
        }
        return sample_count > 0 && chrome_sample_count > 0 &&
               luminance_sum / sample_count >= 8 &&
               chrome_luminance_sum / chrome_sample_count >= 80;
      };

      auto ready_frames = 0;
      for (auto attempt = 0; attempt < 10; ++attempt) {
        process_layout_events(8);
        QElapsedTimer settle_timer;
        settle_timer.start();
        (void)wait_for_condition(
            [&settle_timer]() { return settle_timer.elapsed() >= 140; },
            300);
        root_window->requestUpdate();
        ready_frames = frame_has_content(root_window->grabWindow()) ? ready_frames + 1 : 0;
        if (ready_frames >= 2) {
          process_layout_events(4);
          return true;
        }
      }
      return false;
    };

    const auto proof_scale = qEnvironmentVariableIsSet("QT_SCALE_FACTOR")
                                 ? QString::fromUtf8(qgetenv("QT_SCALE_FACTOR"))
                                 : QStringLiteral("1.0");
    if (proof_scale == QStringLiteral("1.0")) {
      auto* source_window = find_named_object(root, QStringLiteral("timelineDetachedSourceWindow"));
      auto* inspector_window = find_named_object(root, QStringLiteral("timelineDetachedInspectorWindow"));
      auto* source_rail = find_named_object(root, QStringLiteral("timelineSourceRail"));
      auto* effects_rail = find_named_object(root, QStringLiteral("timelineEffectsRail"));
      timeline_workspace->setProperty("sourcePanelDetached", true);
      timeline_workspace->setProperty("effectsPanelDetached", true);
      if (source_window == nullptr || inspector_window == nullptr || source_rail == nullptr ||
          effects_rail == nullptr ||
          !wait_for_condition(
              [source_window, inspector_window, source_rail, effects_rail]() {
                return source_window->property("visible").toBool() &&
                       inspector_window->property("visible").toBool() &&
                       !source_rail->property("visible").toBool() &&
                       !effects_rail->property("visible").toBool();
              },
              2500)) {
        return fail("Timeline Source and Inspector should detach into modular tool windows");
      }
      timeline_workspace->setProperty("sourcePanelDetached", false);
      timeline_workspace->setProperty("effectsPanelDetached", false);
      process_layout_events(8);
    }
    const QList<QPair<int, int>> proof_sizes = {
        {1180, 768},
        {1440, 900},
        {1920, 1080},
    };
    for (const auto dark_mode : {true}) {
      app_shell.setDarkMode(dark_mode);
      if (!settle_timeline_proof()) {
        return fail("Timeline UI proof should settle two visible frames before capture");
      }
      const auto theme_name = dark_mode ? QStringLiteral("dark") : QStringLiteral("light");
      for (const auto& size : proof_sizes) {
        trace_smoke_progress("timeline proof capture begin");
        const auto state = QStringLiteral("timeline-%1-scale-%2")
                               .arg(theme_name, proof_scale);
        if (!capture_visual_proof(
                root_window,
                root,
                visual_proof_dir,
                state,
                size.first,
                size.second,
                required_timeline_objects,
                &visual_proof_manifest,
                QJsonObject{
                    {QStringLiteral("theme"), theme_name},
                    {QStringLiteral("display_scale"), proof_scale},
                    {QStringLiteral("layout"),
                     QStringLiteral("source-program-inspector-multitrack")},
                    {QStringLiteral("selected_clip"),
                     timeline_document.currentClip().value(QStringLiteral("displayLabel")).toString()},
                    {QStringLiteral("track_count"), timeline_document.trackCount()},
                    {QStringLiteral("marker_count"), timeline_document.sequenceMarkers().size()},
                    {QStringLiteral("saved_document"), persistent_timeline_path},
                })) {
          return fail("Timeline UI proof should capture every required size and theme");
        }
      }

      if (proof_scale == QStringLiteral("1.0")) {
        timeline_document.deselectAllClips();
        timeline_workspace->setProperty("sourcePanelCollapsed", false);
        timeline_workspace->setProperty("effectsPanelCollapsed", true);
        timeline_workspace->setProperty("timelineAdvancedControlsVisible", false);
        if (!settle_timeline_proof() ||
            !capture_visual_proof(
                root_window,
                root,
                visual_proof_dir,
                QStringLiteral("timeline-%1-calm-default").arg(theme_name),
                1180,
                768,
                required_timeline_objects,
                &visual_proof_manifest,
                QJsonObject{
                    {QStringLiteral("theme"), theme_name},
                    {QStringLiteral("display_scale"), proof_scale},
                    {QStringLiteral("state"), QStringLiteral("calm-default")},
                    {QStringLiteral("advanced_controls"), false},
                    {QStringLiteral("inspector"), QStringLiteral("contextually-collapsed")},
                })) {
          return fail("Timeline UI proof should capture the calm progressive-disclosure default");
        }

        timeline_document.selectClip(title_clip_index);
        const auto reopened_title_start_seconds =
            timeline_document.currentClip().value(QStringLiteral("startSeconds")).toDouble();
        timeline_document.seekSequenceSeconds(reopened_title_start_seconds + 0.15);
        if (timeline_document.currentClip().value(QStringLiteral("mediaType")).toString() !=
                QStringLiteral("title") ||
            timeline_document.currentClip().value(QStringLiteral("opacity")).toDouble() <= 0.5) {
          return fail("reopened title motion should stay selected and active in the Program workflow");
        }
        timeline_workspace->setProperty("effectsPanelDetached", false);
        timeline_workspace->setProperty("effectsPanelCollapsed", false);
        timeline_workspace->setProperty("inspectorTab", QStringLiteral("title"));
        timeline_workspace->setProperty(
            "inspectorScrollByContext",
            QVariantMap{{QStringLiteral("title|title"), 360.0}});
        process_layout_events(8);
        QStringList title_inspector_objects = required_timeline_objects;
        title_inspector_objects << QStringLiteral("timelineTitlePositionXSlider")
                                << QStringLiteral("timelineTitlePositionYSlider")
                                << QStringLiteral("timelineTitlePositionXKeyframeButton")
                                << QStringLiteral("timelineTitlePositionYKeyframeButton")
                                << QStringLiteral("timelineTitleMotionMenuButton")
                                << QStringLiteral("timelineProgramTitleDragArea");
        if (!capture_visual_proof(
                root_window,
                root,
                visual_proof_dir,
                QStringLiteral("timeline-%1-title-inspector").arg(theme_name),
                1440,
                900,
                title_inspector_objects,
                &visual_proof_manifest,
                QJsonObject{
                    {QStringLiteral("theme"), theme_name},
                    {QStringLiteral("display_scale"), proof_scale},
                    {QStringLiteral("state"), QStringLiteral("title-inspector")},
                    {QStringLiteral("saved_document"), persistent_timeline_path},
                })) {
          return fail("Timeline UI proof should capture the title Inspector state");
        }

        QQuickItem* title_drag_area = nullptr;
        for (auto* candidate : find_named_items(
                 root, QStringLiteral("timelineProgramTitleDragArea"))) {
          if (item_visible(candidate) && candidate->window() == root_window) {
            title_drag_area = candidate;
            break;
          }
        }
        if (title_drag_area == nullptr) {
          return fail("selected title should expose a direct Program Monitor drag target");
        }
        const auto title_position_before = timeline_document.currentClip();
        const auto title_x_before = title_position_before
                                        .value(QStringLiteral("titlePositionX")).toDouble();
        const auto title_y_before = title_position_before
                                        .value(QStringLiteral("titlePositionY")).toDouble();
        const auto title_drag_start = title_drag_area->mapToScene(
            QPointF(title_drag_area->width() * 0.5, title_drag_area->height() * 0.5));
        const auto title_drag_end = title_drag_start + QPointF(42.0, 30.0);
        const auto title_drag_start_global = QPointF(
            root_window->mapToGlobal(title_drag_start.toPoint()));
        const auto title_drag_end_global = QPointF(
            root_window->mapToGlobal(title_drag_end.toPoint()));
        QMouseEvent title_drag_press(
            QEvent::MouseButtonPress,
            title_drag_start,
            title_drag_start_global,
            Qt::LeftButton,
            Qt::LeftButton,
            Qt::NoModifier);
        QCoreApplication::sendEvent(root_window, &title_drag_press);
        QMouseEvent title_drag_move(
            QEvent::MouseMove,
            title_drag_end,
            title_drag_end_global,
            Qt::NoButton,
            Qt::LeftButton,
            Qt::NoModifier);
        QCoreApplication::sendEvent(root_window, &title_drag_move);
        QCoreApplication::processEvents();
        QMouseEvent title_drag_release(
            QEvent::MouseButtonRelease,
            title_drag_end,
            title_drag_end_global,
            Qt::LeftButton,
            Qt::NoButton,
            Qt::NoModifier);
        QCoreApplication::sendEvent(root_window, &title_drag_release);
        QCoreApplication::processEvents();
        const auto title_position_after = timeline_document.currentClip();
        if (title_position_after.value(QStringLiteral("titlePositionX")).toDouble() <=
                title_x_before + 0.02 ||
            title_position_after.value(QStringLiteral("titlePositionY")).toDouble() <=
                title_y_before + 0.015) {
          return fail("dragging a title in the Program Monitor should update its anchor coordinates");
        }
        timeline_document.undo();
        const auto title_position_undone = timeline_document.currentClip();
        if (qAbs(title_position_undone.value(QStringLiteral("titlePositionX")).toDouble() -
                 title_x_before) > 0.001 ||
            qAbs(title_position_undone.value(QStringLiteral("titlePositionY")).toDouble() -
                 title_y_before) > 0.001) {
          return fail("one undo should restore a direct title drag as a single edit");
        }

        timeline_workspace->setProperty("sourcePanelCollapsed", true);
        timeline_workspace->setProperty("effectsPanelCollapsed", true);
        if (!settle_timeline_proof()) {
          return fail("Timeline UI proof should settle collapsed panel frames before capture");
        }
        if (!capture_visual_proof(
                root_window,
                root,
                visual_proof_dir,
                QStringLiteral("timeline-%1-panels-collapsed").arg(theme_name),
                1180,
                768,
                required_timeline_objects,
                &visual_proof_manifest,
                QJsonObject{
                    {QStringLiteral("theme"), theme_name},
                    {QStringLiteral("display_scale"), proof_scale},
                    {QStringLiteral("state"), QStringLiteral("panels-collapsed")},
                })) {
          return fail("Timeline UI proof should capture collapsed side panels");
        }
        timeline_workspace->setProperty("sourcePanelCollapsed", false);
        timeline_workspace->setProperty("effectsPanelCollapsed", false);
        timeline_workspace->setProperty("timelineSearchExpanded", true);
        timeline_workspace->setProperty("timelineSearchQuery", QStringLiteral("opening"));
        timeline_workspace->setProperty("markerReviewExpanded", true);
        if (!settle_timeline_proof()) {
          return fail("Timeline UI proof should settle search and marker frames before capture");
        }
        if (!capture_visual_proof(
                root_window,
                root,
                visual_proof_dir,
                QStringLiteral("timeline-%1-search-marker-review").arg(theme_name),
                1440,
                900,
                required_timeline_objects,
                &visual_proof_manifest,
                QJsonObject{
                    {QStringLiteral("theme"), theme_name},
                    {QStringLiteral("display_scale"), proof_scale},
                    {QStringLiteral("state"), QStringLiteral("search-marker-review")},
                })) {
          return fail("Timeline UI proof should capture search and marker review");
        }
        timeline_workspace->setProperty("timelineSearchExpanded", false);
        timeline_workspace->setProperty("timelineSearchQuery", QString{});
        timeline_workspace->setProperty("markerReviewExpanded", false);
        timeline_document.selectClip(0);

        const QList<QPair<QString, QString>> aspect_presets = {
            {QStringLiteral("youtube"), QStringLiteral("16x9")},
            {QStringLiteral("youtube_shorts"), QStringLiteral("9x16")},
        };
        for (const auto& aspect_preset : aspect_presets) {
          timeline_document.applyOutputPreset(aspect_preset.first);
          // Force a full scene-graph repaint between same-size aspect captures;
          // otherwise an offscreen backend may return only the dirty viewer region.
          root->setProperty("width", 1438);
          root->setProperty("height", 898);
          process_layout_events(6);
          if (!settle_timeline_proof() ||
              !capture_visual_proof(
                  root_window,
                  root,
                  visual_proof_dir,
                  QStringLiteral("timeline-dark-viewer-%1").arg(aspect_preset.second),
                  1440,
                  900,
                  required_timeline_objects,
                  &visual_proof_manifest,
                  QJsonObject{
                      {QStringLiteral("theme"), QStringLiteral("dark")},
                      {QStringLiteral("display_scale"), proof_scale},
                      {QStringLiteral("state"), QStringLiteral("viewer-aspect-proof")},
                      {QStringLiteral("aspect"), aspect_preset.second},
                  })) {
            return fail("Timeline Program viewer should capture sequence-fitted 16:9 and 9:16 canvases");
          }
        }
        timeline_document.applyOutputPreset(QStringLiteral("instagram"));
      }
    }

    QFile manifest_file(
        QDir(visual_proof_dir).filePath(
            QStringLiteral("timeline-ui-proof-scale-%1-manifest.json").arg(proof_scale)));
    if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        manifest_file.write(
            QJsonDocument(visual_proof_manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("Timeline UI proof should write its manifest");
    }
    manifest_file.close();
    app_shell.setDarkMode(true);
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_ACCESSIBILITY")) {
    QAccessible::setActive(true);
    root->setProperty("width", 1440);
    root->setProperty("height", 900);
    process_layout_events(8);
    const auto dialog_proof_dir = qEnvironmentVariable("PHOTO_EDITOR_DIALOG_SURFACE_PROOF_DIR");
    QJsonArray dialog_proof_manifest;
    if (!dialog_proof_dir.isEmpty() && !QDir().mkpath(dialog_proof_dir))
      return fail("Dialog surface proof should create its capture directory");
    if (!QMetaObject::invokeMethod(root, "openFirstRunGuide", Qt::DirectConnection)) {
      return fail("accessibility proof should be able to start the first-run guide");
    }

    if (!wait_for_condition(
            [root]() {
              return named_object_visible_for_proof(
                  root, QStringLiteral("firstRunGuideDialog"));
            },
            3000)) {
      return fail("maximum-scale keyboard proof should open first-run guidance");
    }
    auto* first_run_guide = find_named_object(root, QStringLiteral("firstRunGuideDialog"));
    if (first_run_guide == nullptr) {
      return fail("first-run guidance should expose its visible dialog object for proof");
    }
    if (!capture_visual_proof(
            root_window,
            root,
            visual_proof_dir,
            QStringLiteral("first-run-choice"),
            1440,
            900,
            {QStringLiteral("firstRunSampleButton"), QStringLiteral("firstRunOwnPhotosButton")},
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("state"), QStringLiteral("first-run-photo-choice")},
                {QStringLiteral("display_scale"), 1.3},
                {QStringLiteral("native_window"), qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER")},
            })) {
      return fail("first-run guidance should save a capture of both photo-entry choices");
    }
    QFile first_run_manifest_file(QDir(visual_proof_dir).filePath(QStringLiteral("accessibility-manifest.json")));
    if (!first_run_manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        first_run_manifest_file.write(QJsonDocument(visual_proof_manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("first-run guidance capture should be recorded before later shortcut checks");
    }
    first_run_manifest_file.close();
    const auto persist_accessibility_manifest = [&]() {
      QFile manifest_file(
          QDir(visual_proof_dir).filePath(QStringLiteral("accessibility-manifest.json")));
      return manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
             manifest_file.write(
                 QJsonDocument(visual_proof_manifest).toJson(QJsonDocument::Indented)) > 0;
    };
    const auto capture_accessibility_proof = [&](const QString& state,
                                                 const int requested_width,
                                                 const int requested_height,
                                                 const QStringList& required_objects,
                                                 QJsonObject metadata = {}) {
      auto capture_width = requested_width;
      auto capture_height = requested_height;
      const auto native_window = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER");
      auto screen_available = QRect{};
      auto frame_margins = QMargins{};
      if (native_window && root_window->screen() != nullptr) {
        screen_available = root_window->screen()->availableGeometry();
        frame_margins = root_window->frameMargins();
        const auto max_content_width = std::max(
            1,
            screen_available.width() - frame_margins.left() - frame_margins.right());
        const auto max_content_height = std::max(
            1,
            screen_available.height() - frame_margins.top() - frame_margins.bottom());
        capture_width = std::min(capture_width, max_content_width);
        capture_height = std::min(capture_height, max_content_height);
      }
      const auto native_viewport_constrained =
          native_window && (capture_width != requested_width || capture_height != requested_height);
      metadata.insert(QStringLiteral("originalRequestedWidth"), requested_width);
      metadata.insert(QStringLiteral("originalRequestedHeight"), requested_height);
      metadata.insert(QStringLiteral("actualViewportWidth"), capture_width);
      metadata.insert(QStringLiteral("actualViewportHeight"), capture_height);
      metadata.insert(QStringLiteral("nativeViewportConstrained"), native_viewport_constrained);
      if (native_window && !screen_available.isNull()) {
        metadata.insert(QStringLiteral("screenAvailableWidth"), screen_available.width());
        metadata.insert(QStringLiteral("screenAvailableHeight"), screen_available.height());
        metadata.insert(QStringLiteral("frameMarginLeft"), frame_margins.left());
        metadata.insert(QStringLiteral("frameMarginTop"), frame_margins.top());
        metadata.insert(QStringLiteral("frameMarginRight"), frame_margins.right());
        metadata.insert(QStringLiteral("frameMarginBottom"), frame_margins.bottom());
      }
      if (!capture_visual_proof(
              root_window,
              root,
              visual_proof_dir,
              state,
              capture_width,
              capture_height,
              required_objects,
              &visual_proof_manifest,
              metadata)) {
        return false;
      }
      if (!persist_accessibility_manifest()) {
        std::cerr << "Unable to persist accessibility proof manifest after "
                  << state.toStdString() << '\n';
        return false;
      }
      return true;
    };

    auto require_named_control = [root](const QString& object_name) {
      auto* object = find_named_object(root, object_name);
      const auto name = accessible_name_for_object(object);
      if (object == nullptr || name.isEmpty()) {
        std::cerr << "Missing accessible name for " << object_name.toStdString() << '\n';
        return false;
      }
      return true;
    };

    auto* own_photos = find_named_object(root, QStringLiteral("firstRunOwnPhotosButton"));
    auto* own_photos_item = qobject_cast<QQuickItem*>(own_photos);
    auto* sample_photos = find_named_object(root, QStringLiteral("firstRunSampleButton"));
    auto* sample_photos_item = qobject_cast<QQuickItem*>(sample_photos);
    if (!require_named_control(QStringLiteral("firstRunSampleButton")) ||
        sample_photos_item == nullptr || sample_photos_item->height() < 46.0 ||
        !sample_photos->property("enabled").toBool()) {
      return fail("first-run sample-photo choice should retain its primary pointer target and remain named, visible, and enabled");
    }
    if (!require_named_control(QStringLiteral("firstRunOwnPhotosButton")) ||
        own_photos_item == nullptr || own_photos_item->height() < 46.0 ||
        !own_photos->property("enabled").toBool()) {
      return fail("first-run import choice should retain its primary pointer target and remain named, visible, and enabled");
    }
    if (first_run_guide->property("currentStep").toInt() != 0 ||
        sample_photos_item->parentItem() != own_photos_item->parentItem() ||
        !contains_with_tolerance(scene_rect(sample_photos_item->parentItem()), scene_rect(sample_photos_item)) ||
        !contains_with_tolerance(scene_rect(own_photos_item->parentItem()), scene_rect(own_photos_item)) ||
        overlaps_with_tolerance(scene_rect(sample_photos_item), scene_rect(own_photos_item))) {
      return fail("first-run sample and own-photo choices should fit their shared page without overlapping at proof scale");
    }
    if (!send_activation_key(own_photos) ||
        !wait_for_condition([root]() { return root->property("libraryWorkspaceMode").toString() == QStringLiteral("import"); })) {
      return fail("first-run import choice should retain its primary pointer target and remain named, focusable, visible, and keyboard operable");
    }

    auto* next_button = find_named_object(root, QStringLiteral("firstRunNextButton"));
    if (!require_named_control(QStringLiteral("firstRunNextButton")) ||
        !send_activation_key(next_button)) {
      return fail("first-run workflow should advance to Edit by keyboard");
    }
    auto* edit_button = find_named_object(root, QStringLiteral("firstRunEditButton"));
    if (!require_named_control(QStringLiteral("firstRunEditButton")) ||
        !edit_button->property("enabled").toBool() || !send_activation_key(edit_button)) {
      return fail("first-run selected-photo Edit action should remain named and keyboard operable at maximum scale");
    }
    next_button = find_named_object(root, QStringLiteral("firstRunNextButton"));
    if (!send_activation_key(next_button)) {
      return fail("first-run workflow should advance to Export by keyboard");
    }
    auto* export_button = find_named_object(root, QStringLiteral("firstRunExportButton"));
    if (!require_named_control(QStringLiteral("firstRunExportButton")) ||
        !send_activation_key(export_button)) {
      return fail("first-run Export action should remain named and keyboard operable at maximum scale");
    }
    next_button = find_named_object(root, QStringLiteral("firstRunNextButton"));
    if (!send_activation_key(next_button)) {
      return fail("first-run workflow should advance to its completion page by keyboard");
    }
    auto* finish_button = find_named_object(root, QStringLiteral("firstRunFinishButton"));
    if (!require_named_control(QStringLiteral("firstRunFinishButton")) ||
        !send_activation_key(finish_button) || app_shell.firstRunExperiencePending()) {
      return fail("first-run workflow should finish explicitly by keyboard");
    }

    const auto primary_modifier = Qt::ControlModifier;
    const auto ensure_native_window_active = [root_window]() {
      if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER") || root_window->isActive()) {
        return true;
      }
      activate_native_test_application();
      root_window->raise();
      root_window->requestActivate();
      return wait_for_condition([root_window]() { return root_window->isActive(); }, 2000);
    };
    QVariant command_palette_sequence;
    if (!QMetaObject::invokeMethod(
            root,
            "primaryShortcut",
            Q_RETURN_ARG(QVariant, command_palette_sequence),
            Q_ARG(QVariant, QStringLiteral("K"))) ||
        command_palette_sequence.toString() != QStringLiteral("Ctrl+K")) {
      return fail("the primary shortcut sequence should use Qt's portable Command/Ctrl mapping");
    }
    auto* library_tab = find_named_object(root, QStringLiteral("workspaceTabLibrary"));
    auto* library_tab_item = qobject_cast<QQuickItem*>(library_tab);
    if (!require_named_control(QStringLiteral("workspaceTabLibrary")) || library_tab_item == nullptr) {
      return fail("workspace navigation should provide a named focus target for modal workflows");
    }
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER") && !root_window->isActive()) {
      activate_native_test_application();
      root_window->raise();
      root_window->requestActivate();
      if (!wait_for_condition([root_window]() { return root_window->isActive(); }, 2000)) {
        return fail("native Command Palette proof should activate the isolated Cocoa window before dispatch");
      }
    }
    library_tab_item->forceActiveFocus(Qt::TabFocusReason);
    process_layout_events(2);
    if ((qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER") && !root_window->isActive()) ||
        root_window->activeFocusItem() != library_tab_item ||
        !send_window_key(root_window, Qt::Key_K, primary_modifier) ||
        !wait_for_condition(
            [root]() {
              return named_object_visible_for_proof(root, QStringLiteral("commandPaletteDialog"));
            },
            3000)) {
      std::cerr << "Command Palette shortcut context: active=" << root_window->isActive()
                << " focus=" << (root_window->activeFocusItem() ? root_window->activeFocusItem()->objectName().toStdString() : "none")
                << " primaryModifier=" << static_cast<int>(primary_modifier) << '\n';
      return fail("the Command Palette shortcut should open from the focused workspace navigation using the platform primary modifier");
    }
    auto* command_palette = find_named_object(root, QStringLiteral("commandPaletteDialog"));
    auto* command_search = find_named_object(root, QStringLiteral("commandPaletteSearchField"));
    auto* command_search_item = qobject_cast<QQuickItem*>(command_search);
    QVariant filtered_palette_commands;
    if (!require_named_control(QStringLiteral("commandPaletteSearchField")) ||
        command_search_item == nullptr || root_window->activeFocusItem() != command_search_item ||
        accessible_name_for_object(command_search) != QStringLiteral("Search commands")) {
      return fail("Command Palette opening should focus its named command search field");
    }
    command_search->setProperty("text", QStringLiteral("crop tool"));
    process_layout_events(3);
    if (!QMetaObject::invokeMethod(
            command_palette,
            "filteredCommands",
            Q_RETURN_ARG(QVariant, filtered_palette_commands)) ||
        filtered_palette_commands.toList().isEmpty() ||
        filtered_palette_commands.toList().first().toMap().value(QStringLiteral("label"))
            .toString() != QStringLiteral("Crop Tool")) {
      return fail("Command Palette search should keep the exact matching tool command available");
    }
    if (!send_window_key(root_window, Qt::Key_Escape) ||
        !wait_for_condition(
            [command_palette]() {
              return command_palette != nullptr &&
                     !command_palette->property("visible").toBool();
            },
            1000) ||
        root_window->activeFocusItem() != library_tab_item) {
      return fail("Escape should dismiss Command Palette and return focus to its invoking workspace control");
    }

    if (!QMetaObject::invokeMethod(
            root,
            "openPreferencesDialog",
            Qt::DirectConnection,
            Q_ARG(QVariant, QVariant(0))) ||
        !wait_for_condition(
            [root]() { return named_object_visible_for_proof(root, QStringLiteral("preferencesDialog")); },
            3000)) {
      return fail("the Preferences shell action should open its dialog from workspace navigation");
    }
    auto* preferences = find_named_object(root, QStringLiteral("preferencesDialog"));
    auto* preferences_nav = find_named_object(root, QStringLiteral("preferencesPageNav_0"));
    auto* preferences_nav_item = qobject_cast<QQuickItem*>(preferences_nav);
    if (preferences == nullptr || preferences->property("currentPage").toInt() != 0 ||
        preferences_nav_item == nullptr || root_window->activeFocusItem() != preferences_nav_item ||
        accessible_name_for_object(preferences_nav) != QStringLiteral("General")) {
      return fail("Preferences should open on General with its named navigation control focused");
    }
    if (!dialog_proof_dir.isEmpty()) {
      app_shell.setHighContrast(false);
      app_shell.setColorVisionMode(QStringLiteral("standard"));
      for (const auto scale : {1.0, 2.0}) {
        app_shell.setInterfaceScale(scale);
        root->setProperty("width", scale == 1.0 ? 1280 : 1024);
        root->setProperty("height", scale == 1.0 ? 800 : 760);
        process_layout_events(8);
        for (int page = 0; page < 6; ++page) {
          auto* nav = qobject_cast<QQuickItem*>(find_named_object(root, QStringLiteral("preferencesPageNav_%1").arg(page)));
          if (!nav) return fail("Every Preferences page should retain its navigation control");
          nav->forceActiveFocus(Qt::TabFocusReason);
          if (!send_window_key(root_window, Qt::Key_Return) || preferences->property("currentPage").toInt() != page)
            return fail("Every Preferences page should open through its keyboard navigation control");
          process_layout_events(8);
          auto* background = preferences->property("background").value<QQuickItem*>();
          if (!background) return fail("Preferences should retain its dialog surface");
          const auto bounds = background->mapRectToScene(background->boundingRect()).adjusted(-1, -1, 1, 1);
          const QStringList required{QStringLiteral("preferencesPageHeader"), QStringLiteral("preferencesPageContent"),
              QStringLiteral("preferencesFooter"), QStringLiteral("preferencesCloseButton"),
              QStringLiteral("preferencesNavigationScroll"), nav->objectName()};
          for (const auto& name : required) {
            auto* item = qobject_cast<QQuickItem*>(find_named_object(root, name));
            if (!item || !bounds.contains(item->mapRectToScene(item->boundingRect())))
              return fail("Preferences navigation, settings viewport, and Close should stay inside the dialog at both UI scales");
          }
          if (!capture_visual_proof(root_window, root, dialog_proof_dir,
                QStringLiteral("preferences-page-%1-scale-%2").arg(page).arg(scale),
                scale == 1.0 ? 1280 : 1024, scale == 1.0 ? 800 : 760, required, &dialog_proof_manifest))
            return fail("Preferences page review capture should save");
        }
      }
      preferences->setProperty("currentPage", 0);
      app_shell.setInterfaceScale(1.3);
      app_shell.setHighContrast(true);
      app_shell.setColorVisionMode(QStringLiteral("deuteranopia"));
      root->setProperty("width", 1440);
      root->setProperty("height", 900);
      preferences_nav_item->forceActiveFocus(Qt::TabFocusReason);
      process_layout_events(8);
    }
    if (!send_window_key(root_window, Qt::Key_Escape) ||
        !wait_for_condition(
            [preferences]() { return preferences != nullptr && !preferences->property("visible").toBool(); },
            1000) ||
        root_window->activeFocusItem() != library_tab_item) {
      return fail("Escape should dismiss Preferences and restore focus to the invoking workspace control");
    }

    if (!QMetaObject::invokeMethod(root, "openKeyboardShortcutsDialog", Qt::DirectConnection) ||
        !wait_for_condition(
            [root]() { return named_object_visible_for_proof(root, QStringLiteral("keyboardShortcutsDialog")); },
            3000)) {
      return fail("the Keyboard Shortcuts dialog should open through the shell action");
    }
    auto* shortcuts_dialog = find_named_object(root, QStringLiteral("keyboardShortcutsDialog"));
    auto* shortcuts_search = find_named_object(root, QStringLiteral("shortcutSearchField"));
    auto* shortcuts_search_item = qobject_cast<QQuickItem*>(shortcuts_search);
    if (!require_named_control(QStringLiteral("shortcutSearchField")) ||
        shortcuts_search_item == nullptr || root_window->activeFocusItem() != shortcuts_search_item) {
      return fail("Keyboard Shortcuts should open with its named search field focused");
    }
    shortcuts_search->setProperty("text", QStringLiteral("timeline"));
    if (!QMetaObject::invokeMethod(shortcuts_search, "textEdited"))
      return fail("Shortcut search should forward typed text to its filter");
    process_layout_events(3);
    if (!dialog_proof_dir.isEmpty()) {
      app_shell.setHighContrast(false);
      app_shell.setColorVisionMode(QStringLiteral("standard"));
      for (const auto scale : {1.0, 2.0}) {
        app_shell.setInterfaceScale(scale);
        for (const auto& query : {QString(), QStringLiteral("timeline"), QStringLiteral("no-shortcut-matches-this-query")}) {
          shortcuts_search->setProperty("text", query);
          if (!QMetaObject::invokeMethod(shortcuts_search, "textEdited"))
            return fail("Shortcut review search should forward its text to the filter");
          process_layout_events(8);
          auto* empty = find_named_object(root, QStringLiteral("keyboardShortcutsNoResults"));
          if (query.startsWith("no-shortcut-") && (!empty || !empty->property("visible").toBool()))
            return fail("A shortcut search with no matches should explain the empty state");
          const QStringList required{QStringLiteral("shortcutSearchField"), QStringLiteral("keyboardShortcutsCloseButton")};
          if (!capture_visual_proof(root_window, root, dialog_proof_dir,
                QStringLiteral("shortcuts-%1-scale-%2").arg(query.isEmpty() ? "all" : query == "timeline" ? "timeline" : "empty").arg(scale),
                scale == 1.0 ? 1280 : 1024, scale == 1.0 ? 800 : 760, required, &dialog_proof_manifest))
            return fail("Shortcut default, filtered, and empty review captures should save");
          auto* background = shortcuts_dialog->property("background").value<QQuickItem*>();
          if (!background) return fail("Shortcuts should retain its dialog surface");
          const auto bounds = background->mapRectToScene(background->boundingRect()).adjusted(-1, -1, 1, 1);
          for (const auto& name : required) {
            auto* item = qobject_cast<QQuickItem*>(find_named_object(root, name));
            if (!item || !bounds.contains(item->mapRectToScene(item->boundingRect())))
              return fail("Shortcut search and Close should remain inside the dialog at both UI scales");
          }
        }
      }
      QFile receipt(QDir(dialog_proof_dir).filePath(QStringLiteral("manifest.json")));
      if (!receipt.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return fail("Dialog review manifest should save once");
      receipt.write(QJsonDocument(QJsonObject{{"captures", dialog_proof_manifest},
          {"scope", "Qt production-shell dialog layout and keyboard interactions; not native screen-reader proof"},
          {"nativeEligible", false}}).toJson());
      app_shell.setInterfaceScale(1.3);
      app_shell.setHighContrast(true);
      app_shell.setColorVisionMode(QStringLiteral("deuteranopia"));
      root->setProperty("width", 1440);
      root->setProperty("height", 900);
      process_layout_events(8);
    }
    if (!send_window_key(root_window, Qt::Key_Escape) ||
        !wait_for_condition(
            [shortcuts_dialog]() {
              return shortcuts_dialog != nullptr && !shortcuts_dialog->property("visible").toBool();
            },
            1000) ||
        root_window->activeFocusItem() != library_tab_item) {
      return fail("Escape should dismiss Keyboard Shortcuts and restore focus to its invoking workspace control");
    }

    auto* search_edit_dialog = find_named_object(root, QStringLiteral("searchEditDialog"));
    auto* search_edit_instruction = find_named_object(root, QStringLiteral("searchEditInstruction"));
    auto* search_edit_instruction_item = qobject_cast<QQuickItem*>(search_edit_instruction);
    if (search_edit_dialog == nullptr || search_edit_instruction_item == nullptr ||
        !QMetaObject::invokeMethod(search_edit_dialog, "open", Qt::DirectConnection) ||
        !wait_for_condition(
            [search_edit_dialog]() { return search_edit_dialog->property("visible").toBool(); }, 3000)) {
      return fail("Search Edit should open from its workspace popup");
    }
    if (!wait_for_condition(
            [root_window, search_edit_instruction_item]() {
              return root_window->activeFocusItem() == search_edit_instruction_item;
            },
            1000) ||
        accessible_name_for_object(search_edit_instruction) !=
            QStringLiteral("Photos to find and edit instructions")) {
      auto* focused_item = root_window->activeFocusItem();
      std::cerr << "Search Edit focus: instruction=" << (search_edit_instruction_item != nullptr)
                << " focused=" << (focused_item != nullptr)
                << " focusedName=" << (focused_item == nullptr ? "" : focused_item->objectName().toStdString())
                << " focusedType=" << (focused_item == nullptr ? "" : focused_item->metaObject()->className())
                << " fieldName=" << accessible_name_for_object(search_edit_instruction).toStdString()
                << " fieldActive=" << (search_edit_instruction_item != nullptr && search_edit_instruction_item->hasActiveFocus())
                << " fieldEnabled=" << (search_edit_instruction != nullptr && search_edit_instruction->property("enabled").toBool())
                << " fieldVisible=" << (search_edit_instruction != nullptr && search_edit_instruction->property("visible").toBool())
                << " searching=" << search_edit_dialog->property("searching").toBool()
                << " confirming=" << search_edit_dialog->property("confirming").toBool()
                << '\n';
      return fail("Search Edit should open with its named instruction field focused");
    }
    auto* search_edit_close = find_named_object(root, QStringLiteral("searchEditClose"));
    if (search_edit_close == nullptr ||
        !QMetaObject::invokeMethod(search_edit_close, "clicked", Qt::DirectConnection) ||
        !wait_for_condition(
            [search_edit_dialog]() { return !search_edit_dialog->property("visible").toBool(); }, 1000) ||
        root_window->activeFocusItem() != library_tab_item) {
      return fail("Closing Search Edit should restore focus to its invoking workspace control");
    }

    if (!send_window_key(root_window, Qt::Key_1, primary_modifier) ||
        !wait_for_condition([root]() { return root->property("currentWorkspace").toInt() == 0; })) {
      return fail("Library should remain reachable through its keyboard workspace command");
    }
    auto* back_from_import = find_named_object(root, QStringLiteral("libraryBackFromImportButton"));
    if (!require_named_control(QStringLiteral("libraryBackFromImportButton")) ||
        !send_activation_key(back_from_import) ||
        !wait_for_condition(
            [root]() { return root->property("libraryWorkspaceMode").toString() == QStringLiteral("catalog"); })) {
      return fail("the import workspace should provide a named keyboard path back to Library culling");
    }
    if (!send_window_key(root_window, Qt::Key_P) || !catalog_assets.selectedPicked() ||
        !send_window_key(root_window, Qt::Key_4) || catalog_assets.selectedRating() != 4) {
      return fail("Library culling should support pointer-free workspace, pick, and rating commands");
    }

    if (!ensure_native_window_active()) {
      return fail("native Edit workspace shortcut should activate the Cocoa window before dispatch");
    }
    const auto edit_workspace_shortcut_sent = send_window_key(root_window, Qt::Key_2, primary_modifier);
    const auto edit_workspace_shortcut_ready = edit_workspace_shortcut_sent && wait_for_condition(
            [root]() {
              if (root->property("currentWorkspace").toInt() !=
                  root->property("editWorkspaceIndex").toInt()) {
                return false;
              }
              auto* workspace = find_named_object(root, QStringLiteral("editStudioWorkspace"));
              return workspace != nullptr && workspace->property("visible").toBool() &&
                     workspace->property("pageContentReady").toBool() &&
                     !root->property("workspaceTransitionActive").toBool();
            },
            5000);
    if (!edit_workspace_shortcut_ready) {
      auto* workspace = find_named_object(root, QStringLiteral("editStudioWorkspace"));
      std::cerr << "Edit route context: keySent=" << edit_workspace_shortcut_sent
                << " active=" << root_window->isActive()
                << " focus=" << (root_window->activeFocusItem() == nullptr
                                      ? "none"
                                      : root_window->activeFocusItem()->objectName().toStdString())
                << " currentWorkspace="
                << root->property("currentWorkspace").toInt()
                << " editWorkspaceIndex=" << root->property("editWorkspaceIndex").toInt()
                << " transitionActive=" << root->property("workspaceTransitionActive").toBool()
                << " editVisible=" << (workspace != nullptr && workspace->property("visible").toBool())
                << " editPageReady=" << (workspace != nullptr && workspace->property("pageContentReady").toBool())
                << " hasAsset=" << editor_preview.hasAsset() << '\n';
      return fail("the selected keeper should reach a fully loaded Edit page through the keyboard workspace shortcut");
    }
    const auto log_exposure_accessibility_candidates = [root, &engine]() {
      auto* edit_workspace = find_named_object(root, QStringLiteral("editStudioWorkspace"));
      if (edit_workspace != nullptr) {
        std::cerr << "Edit inspector state: visible="
                  << edit_workspace->property("visible").toBool()
                  << " tool=" << edit_workspace->property("editTool").toString().toStdString()
                  << " inspectorTab=" << edit_workspace->property("editInspectorTab").toString().toStdString()
                  << " inspectorPanelVisible=" << edit_workspace->property("editInspectorPanelVisible").toBool()
                  << " inspectorPanelActive=" << edit_workspace->property("editInspectorPanelActive").toBool()
                  << '\n';
      } else {
        std::cerr << "Edit inspector state: editStudioWorkspace is absent\n";
      }

      for (const auto& object_name : {QStringLiteral("lightInspectorGroup"),
                                      QStringLiteral("lightBasicToneGroup"),
                                      QStringLiteral("adjustInspectorTabButton"),
                                      QStringLiteral("exposureSlider"),
                                      QStringLiteral("exposureSliderValueField")}) {
        const auto matches = root->findChildren<QObject*>(object_name, Qt::FindChildrenRecursively);
        std::cerr << "Accessibility candidates for " << object_name.toStdString()
                  << ": count=" << matches.size() << '\n';
        for (auto* candidate : matches) {
          if (candidate == nullptr) {
            continue;
          }
          auto* item = qobject_cast<QQuickItem*>(candidate);
          auto* accessible = QAccessible::queryAccessibleInterface(candidate);
          const auto context = QQmlEngine::contextForObject(candidate);
          const auto qml_accessible_name =
              QQmlProperty(candidate, QStringLiteral("Accessible.name"), &engine);
          QStringList ancestors;
          for (auto* ancestor = candidate; ancestor != nullptr; ancestor = ancestor->parent()) {
            auto* ancestor_item = qobject_cast<QQuickItem*>(ancestor);
            ancestors.push_back(QStringLiteral("%1[%2]%3")
                                    .arg(QString::fromLatin1(ancestor->metaObject()->className()),
                                         ancestor->objectName(),
                                         ancestor_item == nullptr
                                             ? QString()
                                             : QStringLiteral(" visible=%1").arg(ancestor_item->isVisible())));
          }
          std::cerr << "  class=" << candidate->metaObject()->className()
                    << " visible=" << (item == nullptr ? candidate->property("visible").toBool() : item->isVisible())
                    << " enabled=" << candidate->property("enabled").toBool()
                    << " activeFocus=" << (item != nullptr && item->hasActiveFocus())
                    << " page=" << candidate->property("page").toString().toStdString()
                    << " expanded=" << candidate->property("expanded").toBool()
                    << " active=" << candidate->property("active").toBool()
                    << " accessibleLabel=" << candidate->property("accessibleLabel").toString().toStdString()
                    << " resolvedAccessibleName=" << candidate->property("resolvedAccessibleName").toString().toStdString()
                    << " qmlAttachedNameValid=" << qml_accessible_name.isValid()
                    << " qmlAttachedName=" << qml_accessible_name.read().toString().toStdString()
                    << " qAccessibleValid=" << (accessible != nullptr)
                    << " qAccessibleName="
                    << (accessible == nullptr ? std::string() : accessible->text(QAccessible::Name).toStdString())
                    << " qAccessibleRole="
                    << (accessible == nullptr ? -1 : static_cast<int>(accessible->role()))
                    << " qmlContextUrl="
                    << (context == nullptr ? std::string() : context->baseUrl().toString().toStdString())
                    << " ancestors=" << ancestors.join(QStringLiteral(" <- ")).toStdString() << '\n';
        }
      }
    };
    auto* exposure_value = find_named_object(root, QStringLiteral("exposureSliderValueField"));
    auto* exposure_value_item = qobject_cast<QQuickItem*>(exposure_value);
    if (exposure_value == nullptr ||
        accessible_name_for_object(exposure_value) != QStringLiteral("Exposure value")) {
      log_exposure_accessibility_candidates();
    }
    if (!require_named_control(QStringLiteral("exposureSliderValueField")) ||
        exposure_value == nullptr || exposure_value_item == nullptr ||
        accessible_name_for_object(exposure_value) != QStringLiteral("Exposure value")) {
      return fail("the primary edit value should expose a named keyboard entry field");
    }
    auto* exposure_slider = find_named_object(root, QStringLiteral("exposureSlider"));
    if (exposure_slider == nullptr) {
      return fail("the keyboard exposure field should belong to the named shared slider");
    }
    exposure_value_item->forceActiveFocus(Qt::TabFocusReason);
    const auto select_all_shortcut = QKeySequence(QKeySequence::SelectAll)[0];
    if (!send_window_key(root_window, select_all_shortcut.key(),
                         select_all_shortcut.keyboardModifiers())) {
      return fail("the exposure value field should select its current value from the keyboard");
    }
    for (const auto character : QStringLiteral("0.50")) {
      QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(character));
      QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, QString(character));
      QCoreApplication::sendEvent(root_window, &press);
      QCoreApplication::sendEvent(root_window, &release);
      process_layout_events(1);
    }
    if (exposure_value->property("text").toString() != QStringLiteral("0.50") ||
        !exposure_slider->property("valueInputDirty").toBool()) {
      return fail("typing should replace and dirty the exposure value before committing it");
    }
    if (!send_activation_key(exposure_value, Qt::Key_Return) ||
        std::abs(editor_preview.exposure() - 0.5) > 0.001) {
      return fail("basic exposure should commit the typed value through its named keyboard entry");
    }

    if (!send_window_key(root_window, Qt::Key_M, primary_modifier | Qt::AltModifier) ||
        !wait_for_condition(
            [root]() { return root->property("editWorkspaceMode").toString() == QStringLiteral("masks"); },
            2000)) {
      return fail("local editing should open through its keyboard tool command");
    }
    if (!ensure_native_window_active() ||
        !send_window_key(root_window, Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier) ||
        !wait_for_condition([&editor_preview]() { return editor_preview.hasActiveMask(); }, 2000)) {
      return fail("local editing should create a selection through its keyboard command");
    }

    auto* edit_workspace = find_named_object(root, QStringLiteral("editStudioWorkspace"));
    const auto mask_selection_action_name =
        QStringLiteral("maskSelectionMenuButtonPrimaryAction");
    auto* mask_selection_action = find_named_object(root, mask_selection_action_name);
    if (edit_workspace == nullptr ||
        !require_named_control(mask_selection_action_name) ||
        accessible_name_for_object(mask_selection_action) != QStringLiteral("Smart selection tools") ||
        edit_workspace->property("selectionOutlineAnimationEnabled").toBool()) {
      return fail("reduced motion should stop the nonessential selection animation while retaining named controls");
    }

    for (const auto& density : {QStringLiteral("clean"), QStringLiteral("pro"), QStringLiteral("inspect")}) {
      QVariant accepted;
      if (!QMetaObject::invokeMethod(
              edit_workspace,
              "setInterfaceMode",
              Q_RETURN_ARG(QVariant, accepted),
              Q_ARG(QVariant, density)) ||
          !accepted.toBool() || edit_workspace->property("editInspectorPanelWidth").toInt() < 300) {
        return fail("clean, pro, and inspect density modes should remain usable at maximum scale");
      }
    }

    QJsonObject palette_proof;
    QVector<QColor> standard_palette;
    for (const auto& mode : {
             QStringLiteral("standard"),
             QStringLiteral("protanopia"),
             QStringLiteral("deuteranopia"),
             QStringLiteral("tritanopia"),
             QStringLiteral("grayscale")}) {
      app_shell.setColorVisionMode(mode);
      process_layout_events(2);
      QVariant add_value;
      QVariant subtract_value;
      QVariant intersect_value;
      if (!QMetaObject::invokeMethod(
              edit_workspace,
              "selectionOperationOverlayColor",
              Q_RETURN_ARG(QVariant, add_value),
              Q_ARG(QVariant, QStringLiteral("add"))) ||
          !QMetaObject::invokeMethod(
              edit_workspace,
              "selectionOperationOverlayColor",
              Q_RETURN_ARG(QVariant, subtract_value),
              Q_ARG(QVariant, QStringLiteral("subtract"))) ||
          !QMetaObject::invokeMethod(
              edit_workspace,
              "selectionOperationOverlayColor",
              Q_RETURN_ARG(QVariant, intersect_value),
              Q_ARG(QVariant, QStringLiteral("intersect")))) {
        return fail("selection palettes should remain inspectable in every supported color-vision mode");
      }
      const QColor add = add_value.value<QColor>();
      const QColor subtract = subtract_value.value<QColor>();
      const QColor intersect = intersect_value.value<QColor>();
      const auto distance = [](const QColor& lhs, const QColor& rhs) {
        const auto red = lhs.red() - rhs.red();
        const auto green = lhs.green() - rhs.green();
        const auto blue = lhs.blue() - rhs.blue();
        return std::sqrt(static_cast<double>(red * red + green * green + blue * blue));
      };
      if (!add.isValid() || !subtract.isValid() || !intersect.isValid() ||
          distance(add, subtract) < 80.0 || distance(add, intersect) < 80.0 ||
          distance(subtract, intersect) < 80.0) {
        return fail("add, subtract, and intersect overlays should remain distinguishable in color-safe and grayscale palettes");
      }
      palette_proof.insert(
          mode,
          QJsonArray{add.name(QColor::HexRgb), subtract.name(QColor::HexRgb), intersect.name(QColor::HexRgb)});
      if (mode == QStringLiteral("standard")) {
        standard_palette = {add, subtract, intersect};
      }
    }

    const auto simulated_color = [](const QColor& color, const QVector<double>& matrix) {
      const QVector<double> source{color.redF(), color.greenF(), color.blueF()};
      QVector<double> result(3, 0.0);
      for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
          result[row] += matrix[row * 3 + column] * source[column];
        }
      }
      return result;
    };
    const auto simulated_distance = [](const QVector<double>& lhs, const QVector<double>& rhs) {
      const auto red = lhs[0] - rhs[0];
      const auto green = lhs[1] - rhs[1];
      const auto blue = lhs[2] - rhs[2];
      return std::sqrt(red * red + green * green + blue * blue);
    };
    const QVector<QPair<QString, QVector<double>>> color_vision_simulations{
        {QStringLiteral("protanopia"), {0.56667, 0.43333, 0.0, 0.55833, 0.44167, 0.0, 0.0, 0.24167, 0.75833}},
        {QStringLiteral("deuteranopia"), {0.625, 0.375, 0.0, 0.7, 0.3, 0.0, 0.0, 0.3, 0.7}},
        {QStringLiteral("tritanopia"), {0.95, 0.05, 0.0, 0.0, 0.43333, 0.56667, 0.0, 0.475, 0.525}},
    };
    QJsonArray simulated_modes;
    if (standard_palette.size() != 3) {
      return fail("the standard selection palette should be available for color-vision simulation");
    }
    for (const auto& simulation : color_vision_simulations) {
      QVector<QVector<double>> simulated;
      for (const auto& color : standard_palette) {
        simulated.append(simulated_color(color, simulation.second));
      }
      if (simulated_distance(simulated[0], simulated[1]) < 0.12 ||
          simulated_distance(simulated[0], simulated[2]) < 0.12 ||
          simulated_distance(simulated[1], simulated[2]) < 0.12) {
        return fail("selection operation colors should remain distinct under common color-vision simulations");
      }
      simulated_modes.append(simulation.first);
    }
    QVector<double> grayscale_luminance;
    for (const auto& color : standard_palette) {
      grayscale_luminance.append(
          0.2126 * color.redF() + 0.7152 * color.greenF() + 0.0722 * color.blueF());
    }
    if (std::abs(grayscale_luminance[0] - grayscale_luminance[1]) < 0.15 ||
        std::abs(grayscale_luminance[0] - grayscale_luminance[2]) < 0.15 ||
        std::abs(grayscale_luminance[1] - grayscale_luminance[2]) < 0.15) {
      return fail("selection operation colors should retain distinct luminance in grayscale");
    }
    app_shell.setColorVisionMode(QStringLiteral("deuteranopia"));

    auto* mask_menu_action = find_named_object(root, mask_selection_action_name);
    auto* mask_menu_item = qobject_cast<QQuickItem*>(mask_menu_action);
    auto* mask_menu_popup = find_named_object(root, QStringLiteral("maskSelectionMenuButtonPopup"));
    const auto mask_menu_action_height = mask_menu_item == nullptr ? -1.0 : mask_menu_item->height();
    const auto mask_menu_action_visible = mask_menu_item != nullptr && item_visible(mask_menu_item);
    const auto mask_menu_action_activated =
        mask_menu_action_visible && send_activation_key(mask_menu_action);
    const auto keyboard_menu_opened = mask_menu_action_activated &&
        wait_for_condition(
            [mask_menu_popup]() {
              return mask_menu_popup != nullptr && mask_menu_popup->property("visible").toBool();
            },
            1000);
    const auto max_scale_capture_saved = capture_accessibility_proof(
            QStringLiteral("accessibility-max-scale-local-edit"),
            1440,
            900,
            {
                QStringLiteral("editStudioWorkspace"),
                QStringLiteral("maskSelectionMenuButton"),
                QStringLiteral("maskToolRail"),
            },
            QJsonObject{
                {QStringLiteral("interfaceScale"), 1.3},
                {QStringLiteral("keyboardOnly"), true},
                {QStringLiteral("reducedMotion"), true},
                {QStringLiteral("highContrast"), true},
                {QStringLiteral("densityModes"), QJsonArray{"clean", "pro", "inspect"}},
                {QStringLiteral("palettes"), palette_proof},
                {QStringLiteral("colorVisionSimulations"), simulated_modes},
                {QStringLiteral("grayscaleLuminanceVerified"), true},
            });
    if (keyboard_menu_opened) {
      send_window_key(root_window, Qt::Key_Escape);
      wait_for_condition(
          [mask_menu_popup]() {
            return mask_menu_popup == nullptr || !mask_menu_popup->property("visible").toBool();
          },
          1000);
    }
    const auto action_rect = scene_rect(mask_menu_item);
    const auto pointer_point = action_rect.center();
    if (mask_menu_action_visible) {
      click_native_test_window(root_window, pointer_point.toPoint());
      process_layout_events(5);
    }
    const auto pointer_menu_opened = mask_menu_action_visible &&
        wait_for_condition(
            [mask_menu_popup]() {
              return mask_menu_popup != nullptr && mask_menu_popup->property("visible").toBool();
            },
            1000);
    if (pointer_menu_opened) {
      send_window_key(root_window, Qt::Key_Escape);
      wait_for_condition(
          [mask_menu_popup]() {
            return mask_menu_popup == nullptr || !mask_menu_popup->property("visible").toBool();
          },
          1000);
    }
    auto* mask_menu_button = find_named_object(root, QStringLiteral("maskSelectionMenuButton"));
    auto* mask_tool_rail = find_named_object(root, QStringLiteral("maskToolRail"));
    auto* mask_menu_button_item = qobject_cast<QQuickItem*>(mask_menu_button);
    auto* mask_tool_rail_item = qobject_cast<QQuickItem*>(mask_tool_rail);
    const auto action_fits_menu = mask_menu_item != nullptr && mask_menu_button_item != nullptr &&
        contains_with_tolerance(scene_rect(mask_menu_button_item), scene_rect(mask_menu_item));
    const auto menu_fits_rail = mask_menu_button_item != nullptr && mask_tool_rail_item != nullptr &&
        contains_with_tolerance(scene_rect(mask_tool_rail_item), scene_rect(mask_menu_button_item));
    if (mask_menu_item == nullptr || mask_menu_action_height < 41.0 ||
        !mask_menu_action_visible || !keyboard_menu_opened || !pointer_menu_opened ||
        !action_fits_menu || !menu_fits_rail || !max_scale_capture_saved) {
      std::cerr << "Maximum-scale selection control: actionExists=" << (mask_menu_item != nullptr)
                << " actionHeight=" << mask_menu_action_height
                << " actionVisible=" << mask_menu_action_visible
                << " keyboardMenuOpened=" << keyboard_menu_opened
                << " pointerMenuOpened=" << pointer_menu_opened
                << " actionFitsMenu=" << action_fits_menu
                << " menuFitsRail=" << menu_fits_rail
                << " captureSaved=" << max_scale_capture_saved
                << " actionEnabled=" << (mask_menu_action != nullptr && mask_menu_action->property("enabled").toBool())
                << " menuHeight=" << (mask_menu_button_item == nullptr ? -1.0 : mask_menu_button_item->height())
                << " menuVisible=" << (mask_menu_button_item != nullptr && item_visible(mask_menu_button_item))
                << " railHeight=" << (mask_tool_rail_item == nullptr ? -1.0 : mask_tool_rail_item->height())
                << " railVisible=" << (mask_tool_rail_item != nullptr && item_visible(mask_tool_rail_item))
                << " interfaceScale=" << app_shell.interfaceScale()
                << " density=" << edit_workspace->property("interfaceMode").toString().toStdString()
                << '\n';
      return fail("maximum-scale local-edit accessibility state should remain visible and unclipped");
    }

    if (!send_window_key(root_window, Qt::Key_Escape)) {
      return fail("Studio layout proof should close its open tool menu by keyboard");
    }
    app_shell.setInterfaceScale(1.0);
    auto capture_studio_hierarchy = [&](const QString& density,
                                        const QString& tool,
                                        const QString& state,
                                        int width,
                                        int height) {
      QVariant density_accepted;
      if (!QMetaObject::invokeMethod(
              edit_workspace,
              "setInterfaceMode",
              Q_RETURN_ARG(QVariant, density_accepted),
              Q_ARG(QVariant, density)) ||
          !density_accepted.toBool() ||
          !QMetaObject::invokeMethod(
              edit_workspace,
              "activateEditTool",
              Q_ARG(QVariant, tool)) ||
          !wait_for_condition(
              [edit_workspace, tool]() {
                return edit_workspace->property("editTool").toString() == tool &&
                       !edit_workspace->property("editToolTransitionActive").toBool();
              },
              1200)) {
        return false;
      }
      return capture_accessibility_proof(
          state,
          width,
          height,
          {QStringLiteral("editStudioWorkspace")},
          QJsonObject{
              {QStringLiteral("density"), density},
              {QStringLiteral("tool"), tool},
              {QStringLiteral("selectedTargetVisible"), true},
              {QStringLiteral("primaryActionVisible"), true},
          });
    };
    if (!capture_studio_hierarchy(
            QStringLiteral("clean"),
            QStringLiteral("adjust"),
            QStringLiteral("studio-hierarchy-clean-adjust"),
            980,
            760) ||
        !capture_studio_hierarchy(
            QStringLiteral("pro"),
            QStringLiteral("crop"),
            QStringLiteral("studio-hierarchy-pro-crop"),
            1180,
            820) ||
        !capture_studio_hierarchy(
            QStringLiteral("inspect"),
            QStringLiteral("masks"),
            QStringLiteral("studio-hierarchy-inspect-masks"),
            1600,
            1000)) {
      return fail("Studio hierarchy should remain visible at minimum, typical, and wide density layouts");
    }
    app_shell.setInterfaceScale(1.3);
    root->setProperty("width", 1440);
    root->setProperty("height", 900);
    if (!wait_for_condition(
            [root_window]() { return root_window->width() == 1440 && root_window->height() == 900; },
            2000)) {
      std::cerr << "First-run export viewport: window=" << root_window->width() << 'x'
                << root_window->height() << '\n';
      return fail("First-run Export should settle at the requested viewport before keyboard routing");
    }

    // This shard's earlier accessibility checks use the explicit invalid URL
    // from source_contract.inc to stay offline. The final keyboard export needs
    // a real catalog, so let the isolated temporary app-data root provision its
    // own managed PostgreSQL instance instead of sending admission to `example`.
    // This disposable catalog does not need a rollback archive, and concurrent
    // first-use admissions can otherwise race while creating the same
    // second-resolution pre-migration backup name.
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qputenv("PHOTO_EDITOR_REQUIRE_PRE_MIGRATION_BACKUP", QByteArray("0"));

    if (!ensure_native_window_active()) {
      return fail("native first-run Export shortcut should activate the Cocoa window before dispatch");
    }
    const auto export_shortcut_sent =
        send_window_key(root_window, Qt::Key_E, primary_modifier | Qt::AltModifier);
    const auto export_workspace_reached = export_shortcut_sent &&
        wait_for_condition([root]() { return root->property("currentWorkspace").toInt() == 4; }, 2000);
    if (!export_workspace_reached) {
      auto* focus_item = root_window->activeFocusItem();
      std::cerr << "First-run Export shortcut: keySent=" << export_shortcut_sent
                << " workspaceReached=" << export_workspace_reached
                << " active=" << root_window->isActive()
                << " focus=" << (focus_item == nullptr ? "none" : focus_item->objectName().toStdString())
                << " currentWorkspace=" << root->property("currentWorkspace").toInt()
                << " transitionActive=" << root->property("workspaceTransitionActive").toBool()
                << " primaryModifier=" << static_cast<int>(primary_modifier) << '\n';
      return fail("Export should remain keyboard reachable at maximum scale");
    }
    if (!wait_for_condition([root]() {
          auto* workspace = find_named_object(root, QStringLiteral("exportStudioWorkspace"));
          auto* content = find_named_object(root, QStringLiteral("exportStudioWorkspaceContent"));
          return workspace != nullptr && content != nullptr &&
                 named_object_visible_for_proof(root, QStringLiteral("exportStudioWorkspace")) &&
                 named_object_visible_for_proof(root, QStringLiteral("exportStudioWorkspaceContent")) &&
                 !root->property("workspaceTransitionActive").toBool();
        }, 5000)) {
      return fail("Keyboard Export routing should wait for its visible workspace content and settled transition");
    }
    auto* export_workspace = find_named_object(root, QStringLiteral("exportStudioWorkspaceContent"));
    auto* export_destination_field = find_named_object(root, QStringLiteral("exportDestinationField"));
    auto* export_current_button = find_named_object(root, QStringLiteral("exportCurrentSplitButton"));
    auto* export_current_primary = find_named_object(root, QStringLiteral("exportCurrentSplitButtonPrimaryAction"));
    auto* export_current_primary_item = qobject_cast<QQuickItem*>(export_current_primary);
    auto* export_preflight_status = find_named_object(root, QStringLiteral("exportPreflightStatusLabel"));
    const auto first_run_delivery = runtime_root.filePath(QStringLiteral("first-run-delivery"));
    if (export_workspace == nullptr || !QDir().mkpath(first_run_delivery) ||
        !export_workspace->setProperty("deliveryDestinationPath", first_run_delivery))
      return fail("Keyboard export should prepare its isolated delivery destination");
    // The primary action stays enabled while its fresh checks run, because
    // submission repeats preflight before admitting a job. Wait for the visible
    // output summary to reflect the completed report before calling this ready.
    if (!wait_for_condition([&export_jobs, export_current_button, export_preflight_status]() {
          const auto report = export_jobs.singlePreflight();
          return export_current_button && export_current_button->property("primaryEnabled").toBool() &&
                 !export_jobs.preflightBusy() && report.contains(QStringLiteral("ok")) &&
                 report.value(QStringLiteral("ok")).toBool() &&
                 report.value(QStringLiteral("errorCount")).toInt() == 0 &&
                 export_preflight_status != nullptr &&
                 export_preflight_status->property("text").toString() == QStringLiteral("Ready");
        }, 10000)) {
      const auto report = export_jobs.singlePreflight();
      std::cerr << "Keyboard export readiness: "
                << (export_current_button ? export_current_button->property("disabledReason").toString().toStdString() : "missing primary control") << '\n';
      std::cerr << "Keyboard export preflight: busy=" << export_jobs.preflightBusy()
                << " hasReport=" << report.contains(QStringLiteral("ok"))
                << " ok=" << report.value(QStringLiteral("ok")).toBool()
                << " errors=" << report.value(QStringLiteral("errorCount")).toInt()
                << " status=" << (export_preflight_status ? export_preflight_status->property("text").toString().toStdString() : "missing status label")
                << '\n';
      return fail("First-run Export should show a completed successful preflight before its readiness capture");
    }
    if (!capture_accessibility_proof(
            QStringLiteral("first-run-export-ready"),
            1440,
            900,
            {
                QStringLiteral("exportStudioWorkspace"),
                QStringLiteral("exportStudioWorkspaceContent"),
                QStringLiteral("exportDestinationField"),
                QStringLiteral("exportCurrentSplitButton"),
                QStringLiteral("exportCurrentSplitButtonPrimaryAction"),
            },
            QJsonObject{
                {QStringLiteral("interfaceScale"), app_shell.interfaceScale()},
                {QStringLiteral("workspaceTransitionSettled"), true},
                {QStringLiteral("destinationPreflightReady"), true},
                {QStringLiteral("primaryEnabled"), true},
            })) {
      return fail("First-run Export should preserve a capture after destination readiness");
    }
    const auto export_destination_named = require_named_control(QStringLiteral("exportDestinationField"));
    const auto export_primary_named =
        require_named_control(QStringLiteral("exportCurrentSplitButtonPrimaryAction"));
    const auto export_primary_accessible_name = accessible_name_for_object(export_current_primary);
    auto* export_current_button_item = qobject_cast<QQuickItem*>(export_current_button);
    const auto export_button_height = export_current_button_item == nullptr
                                          ? -1.0
                                          : export_current_button_item->height();
    const auto export_primary_minimum_height = export_current_primary == nullptr
                                                   ? -1.0
                                                   : export_current_primary->property("minimumPointerHeight").toDouble();
    const auto export_primary_height = export_current_primary_item == nullptr
                                           ? -1.0
                                           : export_current_primary_item->height();
    const auto export_primary_contained = export_current_button_item != nullptr &&
        export_current_primary_item != nullptr &&
        contains_with_tolerance(
            scene_rect(export_current_button_item), scene_rect(export_current_primary_item));
    const auto export_keyboard_ready = export_destination_named && export_primary_named &&
        export_current_button != nullptr && export_current_button->property("primaryEnabled").toBool() &&
        !export_current_button->property("activeFocusOnTab").toBool() &&
        export_current_primary_item != nullptr && export_current_primary_item->activeFocusOnTab() &&
        export_primary_accessible_name == QStringLiteral("Start Export") &&
        export_current_button_item != nullptr && export_button_height >= 44.0 &&
        export_primary_height >= 44.0 && export_primary_height >= export_primary_minimum_height &&
        export_primary_contained;
    if (!export_keyboard_ready) {
      std::cerr << "Keyboard export control: destinationNamed=" << export_destination_named
                << " primaryNamed=" << export_primary_named
                << " primaryEnabled=" << (export_current_button != nullptr &&
                    export_current_button->property("primaryEnabled").toBool())
                << " splitButtonTabFocus=" << (export_current_button != nullptr &&
                    export_current_button->property("activeFocusOnTab").toBool())
                << " primaryItemExists=" << (export_current_primary_item != nullptr)
                << " primaryTabFocus=" << (export_current_primary_item != nullptr &&
                    export_current_primary_item->activeFocusOnTab())
                << " primaryAccessibleName=" << export_primary_accessible_name.toStdString()
                << " buttonHeight=" << export_button_height
                << " primaryHeight=" << export_primary_height
                << " primaryMinimumHeight=" << export_primary_minimum_height
                << " primaryContained=" << export_primary_contained
                << " currentWorkspace=" << root->property("currentWorkspace").toInt()
                << '\n';
      return fail("Export should remain keyboard reachable with a named primary control at maximum scale");
    }
    export_destination_field->setProperty("focus", true);
    process_layout_events(2);
    if (!send_activation_key(export_current_primary)) {
      return fail("the first-run path should start its real export by keyboard");
    }
    job_runner.initializeFromStore();
    if (!wait_for_condition(
            [&job_queue, first_run_delivery]() {
              return job_queue.completedJobCount() >= 1 &&
                     !QDir(first_run_delivery).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty();
            },
            15000)) {
      const auto export_records = export_jobs.jobs();
      const auto latest_export = export_records.isEmpty()
                                     ? QVariantMap{}
                                     : export_records.constLast().toMap();
      const auto delivered_file_count =
          QDir(first_run_delivery).entryList(QDir::Files | QDir::NoDotAndDotDot).size();
      std::cerr << "First-run export receipt: queue_jobs=" << job_queue.jobCount()
                << " queue_completed=" << job_queue.completedJobCount()
                << " queue_failed=" << job_queue.failedJobCount()
                << " queue_status=" << job_queue.status().toStdString()
                << " export_jobs=" << export_jobs.jobCount()
                << " pending_admissions=" << export_jobs.pendingAdmissionCount()
                << " submission_error=" << export_jobs.submissionError().toStdString()
                << " admission_error=" << export_jobs.admissionError().toStdString()
                << " latest_status=" << latest_export.value(QStringLiteral("status")).toString().toStdString()
                << " latest_error=" << latest_export.value(QStringLiteral("errorMessage")).toString().toStdString()
                << " delivered_files=" << delivered_file_count << '\n';
      return fail("the first-run path should finish with a real delivered file and completed job");
    }
    const auto delivered_files =
        QDir(first_run_delivery).entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    const auto delivered_source = QDir(first_run_delivery).filePath(delivered_files.constFirst());
    const auto delivered_suffix = QFileInfo(delivered_source).suffix().toLower();
    const auto delivered_proof = QDir(visual_proof_dir).filePath(
        QStringLiteral("first-run-keyboard-export.%1").arg(
            delivered_suffix.isEmpty() ? QStringLiteral("jpg") : delivered_suffix));
    if ((QFile::exists(delivered_proof) && !QFile::remove(delivered_proof)) ||
        !QFile::copy(delivered_source, delivered_proof)) {
      return fail("the first-run delivered file should be preserved with the acceptance artifacts");
    }
    visual_proof_manifest.append(QJsonObject{
        {QStringLiteral("state"), QStringLiteral("first-run-keyboard-export")},
        {QStringLiteral("path"), delivered_proof},
        {QStringLiteral("keyboardOnly"), true},
        {QStringLiteral("completedJobCount"), job_queue.completedJobCount()},
        {QStringLiteral("artifactBytes"), QFileInfo(delivered_proof).size()},
    });
    if (!persist_accessibility_manifest()) {
      return fail("The first-run delivered image and export receipt should be recorded immediately");
    }

    const auto route_photo_path = runtime_root.filePath(QStringLiteral("photo-only-edit-route.png"));
    if (!write_overlay_visual_fixture(route_photo_path, QStringLiteral("base"))) {
      return fail("Photo-only Edit routing should create its isolated photo fixture");
    }
    auto route_video = make_asset(QStringLiteral("photo-only-route-video"), QStringLiteral("route-video.mp4"));
    route_video.media_type = QStringLiteral("video");
    auto route_timeline = make_asset(QStringLiteral("photo-only-route-timeline"), QStringLiteral("route-timeline.looksy"));
    route_timeline.media_type = QStringLiteral("timeline");
    auto route_photo = make_asset(
        QStringLiteral("photo-only-route-photo"),
        route_photo_path,
        QUrl::fromLocalFile(route_photo_path).toString());
    catalog_assets.clearFilters();
    catalog_assets.setMediaCollectionFilter(QStringLiteral("all"));
    catalog_assets.setMediaTypeFilter(QStringLiteral("all"));
    catalog_assets.replaceAssetsForTesting({route_video, route_timeline, route_photo});
    if (!wait_for_condition([&catalog_assets]() { return catalog_assets.assetCount() == 3; }, 2000)) {
      QJsonArray visible_rows;
      const auto visible_count = std::min(catalog_assets.assetCount(), 8);
      for (int row = 0; row < visible_count; ++row) {
        const auto asset = catalog_assets.assetAt(row);
        visible_rows.append(QJsonObject{
            {QStringLiteral("assetId"), asset.value(QStringLiteral("assetId")).toString()},
            {QStringLiteral("mediaType"), asset.value(QStringLiteral("mediaType")).toString()},
            {QStringLiteral("filePath"), asset.value(QStringLiteral("filePath")).toString()},
        });
      }
      std::cerr << "Photo-only routing fixture: visible=" << catalog_assets.assetCount()
                << " total=" << catalog_assets.totalAssetCount()
                << " filter=" << catalog_assets.mediaTypeFilter().toStdString()
                << " collection=" << catalog_assets.mediaCollectionFilter().toStdString()
                << " project=" << catalog_assets.projectFilter().toStdString()
                << " search=" << catalog_assets.searchFilter().toStdString()
                << " review=" << catalog_assets.reviewFilter().toStdString()
                << " availability=" << catalog_assets.availabilityFilter().toStdString()
                << " minimumRating=" << catalog_assets.minimumRatingFilter()
                << " loading=" << catalog_assets.loading()
                << " status=" << catalog_assets.status().toStdString()
                << " error=" << catalog_assets.errorMessage().toStdString()
                << " rows=" << QJsonDocument(visible_rows).toJson(QJsonDocument::Compact).toStdString()
                << " requestedIds=" << route_video.asset_id.toStdString() << ','
                << route_timeline.asset_id.toStdString() << ','
                << route_photo.asset_id.toStdString() << '\n';
      return fail("Photo-only Edit routing should expose the isolated video, timeline, and photo rows");
    }
    auto* open_selected_photo_action = find_named_object(root, QStringLiteral("openSelectedInEditAction"));
    auto* export_tab = qobject_cast<QQuickItem*>(find_named_object(root, QStringLiteral("workspaceTabExport")));
    if (open_selected_photo_action == nullptr || export_tab == nullptr) {
      return fail("Photo-only Edit routing should expose its named command and keyboard focus target");
    }
    const auto workspace_before_route = root->property("currentWorkspace").toInt();
    if (!ensure_native_window_active()) {
      return fail("native photo-only Edit routing should activate the Cocoa window before shortcut checks");
    }
    export_tab->forceActiveFocus(Qt::TabFocusReason);
    process_layout_events(3);
    catalog_assets.selectIndexWithModifiers(0, 0, false);
    process_layout_events(3);
    if (open_selected_photo_action->property("enabled").toBool() ||
        !send_window_key(root_window, Qt::Key_E, primary_modifier) ||
        root->property("currentWorkspace").toInt() != workspace_before_route ||
        catalog_assets.selectedAssetId() != route_video.asset_id) {
      return fail("Cmd/Ctrl+E should stay disabled for a selected video without changing its active target");
    }
    catalog_assets.selectIndexWithModifiers(1, 0, false);
    process_layout_events(3);
    if (open_selected_photo_action->property("enabled").toBool() ||
        !send_window_key(root_window, Qt::Key_E, primary_modifier) ||
        root->property("currentWorkspace").toInt() != workspace_before_route ||
        catalog_assets.selectedAssetId() != route_timeline.asset_id) {
      return fail("Cmd/Ctrl+E should stay disabled for a selected timeline without changing its active target");
    }
    catalog_assets.clearSelection();
    process_layout_events(3);
    const auto empty_selection_command_enabled = open_selected_photo_action->property("enabled").toBool();
    const auto empty_selection_key_sent = send_window_key(root_window, Qt::Key_E, primary_modifier);
    const auto empty_selection_route_completed = empty_selection_command_enabled && empty_selection_key_sent &&
        wait_for_condition([&]() {
          return catalog_assets.selectedAssetId() == route_photo.asset_id &&
                 editor_preview.assetId() == route_photo.asset_id &&
                 root->property("currentWorkspace").toInt() == 1;
        }, 5000);
    if (!empty_selection_route_completed) {
      std::cerr << "Empty-selection Edit routing: actionEnabled=" << empty_selection_command_enabled
                << " keySent=" << empty_selection_key_sent
                << " windowActive=" << root_window->isActive()
                << " focus=" << (root_window->activeFocusItem() == nullptr
                                      ? "none"
                                      : root_window->activeFocusItem()->objectName().toStdString())
                << " firstVisiblePhotoIndex=" << catalog_assets.firstVisiblePhotoIndex()
                << " selectedAssetId=" << catalog_assets.selectedAssetId().toStdString()
                << " expectedAssetId=" << route_photo.asset_id.toStdString()
                << " previewAssetId=" << editor_preview.assetId().toStdString()
                << " currentWorkspace=" << root->property("currentWorkspace").toInt() << '\n';
      return fail("With no selection, Cmd/Ctrl+E should choose the first visible photo and open that photo in Edit");
    }
    visual_proof_manifest.append(QJsonObject{
        {QStringLiteral("state"), QStringLiteral("photo-only-edit-routing")},
        {QStringLiteral("selectedVideoCommandDisabled"), true},
        {QStringLiteral("selectedTimelineCommandDisabled"), true},
        {QStringLiteral("emptySelectionOpenedFirstPhoto"), true},
        {QStringLiteral("keyboardShortcut"), QStringLiteral("Cmd/Ctrl+E")},
        {QStringLiteral("selectedAssetId"), catalog_assets.selectedAssetId()},
    });

    QFile manifest_file(
        QDir(visual_proof_dir).filePath(QStringLiteral("accessibility-manifest.json")));
    if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        manifest_file.write(QJsonDocument(visual_proof_manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("accessibility proof should write its manifest");
    }
    qunsetenv("PHOTO_EDITOR_REQUIRE_PRE_MIGRATION_BACKUP");
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_CRASH_RECOVERY_UX")) {
    root->setProperty("width", 1440);
    root->setProperty("height", 900);
    if (!wait_for_condition(
            [root]() {
              auto* surface = find_named_item(root, QStringLiteral("recoverySummarySurface"));
              return surface != nullptr && item_visible(surface) && surface->height() >= 80.0;
            },
            2000)) {
      return fail("crash recovery visual proof should expose a concise nonmodal summary");
    }
    auto* details_button = find_named_object(root, QStringLiteral("recoveryDetailsButton"));
    if (details_button == nullptr ||
        !QMetaObject::invokeMethod(details_button, "clicked", Qt::DirectConnection)) {
      return fail("crash recovery visual proof should open recovery details");
    }
    process_layout_events(8);
    if (!job_queue.hasRecoveryReport() || job_queue.recoveredJobCount() != 1 ||
        !job_queue.recoverySummary().contains(QStringLiteral("1 waiting for review")) ||
        !capture_visual_proof(
            root_window,
            root,
            visual_proof_dir,
            QStringLiteral("crash-recovery-summary"),
            1440,
            900,
            {
                QStringLiteral("recoverySummarySurface"),
                QStringLiteral("recoveryDetailsButton"),
                QStringLiteral("recoveryReviewWorkButton"),
                QStringLiteral("recoveryDoneButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("recoveredOperations"), 1},
                {QStringLiteral("waitingForReview"), 1},
                {QStringLiteral("automaticExternalReplay"), false},
            })) {
      return fail("crash recovery visual proof should capture summary, details, and review actions");
    }
    QFile manifest_file(
        QDir(visual_proof_dir).filePath(QStringLiteral("crash-recovery-manifest.json")));
    if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        manifest_file.write(QJsonDocument(visual_proof_manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("crash recovery visual proof should write its manifest");
    }
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_AI_EDIT_UX")) {
    const auto before_path = runtime_root.filePath(QStringLiteral("ai-edit-visual/before.png"));
    const auto after_path = runtime_root.filePath(QStringLiteral("ai-edit-visual/after.png"));
    const auto second_before_path =
        runtime_root.filePath(QStringLiteral("ai-edit-visual/second-before.png"));
    const auto second_after_path =
        runtime_root.filePath(QStringLiteral("ai-edit-visual/second-after.png"));
    const auto workspace_path = runtime_root.filePath(QStringLiteral("ai-edit-visual/workspace.png"));
    if (!QDir().mkpath(QFileInfo(before_path).absolutePath()) ||
        !write_overlay_visual_fixture(before_path, QStringLiteral("dark")) ||
        !write_overlay_visual_fixture(after_path, QStringLiteral("saturated")) ||
        !write_overlay_visual_fixture(second_before_path, QStringLiteral("bright")) ||
        !write_overlay_visual_fixture(second_after_path, QStringLiteral("agent-scene")) ||
        !write_overlay_visual_fixture(workspace_path, QStringLiteral("agent-scene"))) {
      return fail("AI Edit visual proof should create real before and after image fixtures");
    }

    engine.rootContext()->setContextProperty("aiEditBeforeProofPath", before_path);
    engine.rootContext()->setContextProperty("aiEditAfterProofPath", after_path);
    engine.rootContext()->setContextProperty(
        "aiEditSecondBeforeProofPath", second_before_path);
    engine.rootContext()->setContextProperty(
        "aiEditSecondAfterProofPath", second_after_path);
    engine.rootContext()->setContextProperty("aiEditWorkspaceProofPath", workspace_path);
    const auto qml_root = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                              .filePath(QStringLiteral("apps/desktop/qml"));
    QQmlComponent ai_edit_component(&engine);
    const auto ai_edit_harness_qml = QByteArray(R"qml(
import QtQuick
import QtQuick.Controls
import "components"
import "workspaces"

ApplicationWindow {
    id: harness
    objectName: "aiEditVisualHarness"
    width: 1440
    height: 900
    visible: true
    color: "#111318"
    property bool showFocusReturnProbe: true
    property bool showAgentWorkspaceReview: false
    property bool showSelectionConfirmation: false
    property bool showClarification: false
    property bool showVisualReferentReuse: false
    property bool showProgressivePreview: false
    property bool fastDeadlineReached: false
    property bool showLocalPlanning: false
    property bool showTwoSpeedDraftChoice: false
    property string twoSpeedDraftVersion: "deep-review"
    property bool showTwoSpeedTastePreference: true
    property bool showProofRejection: false
    property bool showProofRepairApproval: false
    property bool showIntentProtected: false
    property bool showCorruptModelBundle: false
    property bool showExceptionReview: false
    property bool showStoryPreferenceCommitted: false
    property bool storyPreferenceRemembered: false
    property bool showNorthStarCompass: false
    property bool showDestructiveApproval: false
    property bool showPhotographerTasteCheck: false
    property bool calibratedComparisonRequested: false
    property var storyRoleAssignments: ({
        "asset-1": "atmosphere",
        "asset-2": "hero",
        "asset-3": "detail",
        "asset-4": "transition",
        "asset-5": "support"
    })
    property var storyRoleOverrideAssetIds: []
    readonly property bool storyHeroSteered:
        String(storyRoleAssignments["asset-1"] || "") === "hero"
    property int storyRoleRevisionCount: 0
    property bool comparisonPreparing: false
    property var directionComparison: ({})
    Component.onCompleted: {
        focusReturnProbe.forceActiveFocus(Qt.TabFocusReason)
        dialog.open()
        showFocusReturnProbe = false
    }
    property string lastRequestedScope: ""
    property int setupRequestCount: 0
    property string lastSampleFeedback: ""
    property bool correctionTransferActive: false
    property bool workspaceSpatialTargetMode: false
    property bool showPointGroundedCorrection: false
    property bool showPointFallback: false
    property real lastPointX: -1
    property real lastPointY: -1
    property string lastPointFeedback: ""
    property string lastPointAssetId: ""
    property string lastTasteRename: ""
    property string lastNorthStarSave: ""
    property string lastCalibrationRatingsPath: ""
    property string lastCalibrationPacketPath: ""
    property int calibrationFinalizeCount: 0
    property int calibrationClipboardFinalizeCount: 0
    readonly property string librarySelectionFingerprint: fakeCatalog.selectedAssetIds.join(",")

    function storyRoleForAsset(assetId) {
        return String(storyRoleAssignments[String(assetId)] || "support")
    }

    function storyRoleLabel(role) {
        const labels = {
            atmosphere: "Atmosphere opener",
            hero: "Hero anchor",
            detail: "Detail / texture",
            transition: "Transition",
            support: "Support"
        }
        return String(labels[String(role)] || "Support")
    }

    function storyRoleWasDirected(assetId) {
        return storyRoleOverrideAssetIds.indexOf(String(assetId)) >= 0
    }

    function storyRoleEvidenceBasis(assetId) {
        if (storyRoleWasDirected(assetId))
            return "photographer-direction"
        const bases = {
            "asset-1": "source-order",
            "asset-2": "explicit-curation",
            "asset-3": "capture-group-variation",
            "asset-4": "lighting-group-boundary",
            "asset-5": "lighting-group-baseline"
        }
        return String(bases[String(assetId)] || "lighting-group-baseline")
    }

    function storyRoleReason(assetId) {
        if (storyRoleWasDirected(assetId))
            return "The photographer explicitly assigned this role for the current photo set."
        const reasons = {
            "asset-1": "The earliest unassigned frame is reviewed as the set's suggested opener.",
            "asset-2": "Explicit rating or pick evidence made this the strongest anchor candidate.",
            "asset-3": "A less-represented capture group supplies a visual-variety review candidate.",
            "asset-4": "This frame sits at a lighting-group boundary in source order.",
            "asset-5": "This frame supports its validated lighting-group baseline."
        }
        return String(reasons[String(assetId)] || reasons["asset-5"])
    }

    function storyRoleEvidence(assetId) {
        return {
            contract: "agent-assignment-story-role-evidence-v1",
            basis: storyRoleEvidenceBasis(assetId),
            signals: storyRoleWasDirected(assetId)
                     ? ["explicit-photographer-story-role"]
                     : [storyRoleEvidenceBasis(assetId)],
            photographerAssigned: storyRoleWasDirected(assetId),
            semanticRecognitionUsed: false
        }
    }

    Image {
        anchors.fill: parent
        source: "file://" + aiEditWorkspaceProofPath
        fillMode: Image.PreserveAspectCrop
        opacity: harness.showAgentWorkspaceReview ? 0.92 : 0.24
    }

    Rectangle {
        anchors.fill: parent
        color: "#171512"
        opacity: harness.showAgentWorkspaceReview ? 0.12 : 0.58
    }

    QtObject {
        id: fakeController
        signal draftChanged()
        signal pendingAgentInputChanged()
        signal draftRunFinished(string requestId, var report)
        signal projectTasteChanged()
        property bool running: harness.showProgressivePreview ||
                               harness.showLocalPlanning ||
                               harness.comparisonPreparing
        property string activeDraftRequestId: harness.showProgressivePreview ||
                                              harness.showLocalPlanning
                                              ? "smoke-request-1" : ""
        property bool draftRequestPending: false
        property var draftDirectionComparison: harness.directionComparison
        property bool draftActive: dialog.currentPage >= 2 &&
                                   !harness.showSelectionConfirmation &&
                                   !harness.showClarification &&
                                   !harness.showDestructiveApproval &&
                                   !harness.showProgressivePreview &&
                                   !harness.showLocalPlanning &&
                                   !harness.showProofRejection &&
                                   !harness.showProofRepairApproval &&
                                   !harness.comparisonPreparing
        property bool awaitingUserInput: harness.showSelectionConfirmation ||
                                         harness.showClarification ||
                                         harness.showDestructiveApproval ||
                                         harness.showProofRepairApproval
        property bool modelInstallRunning: false
        function probeSelectedModelAsync(force) { }
        property string selectedStyleId: "natural-portrait"
        property string selectedModelId: "local-editor"
        property string editingFreedom: "balanced"
        property string workflowPrimaryKey: "create_draft"
        property string userInstructions: "Natural wedding color, warm skin, protect the dress highlights."
        property string errorMessage: harness.showProofRejection
                                      ? "The cleanup changed the color or texture around the target, so I rejected it and left your photo unchanged."
                                      : ""
        property string status: harness.showProofRejection
                                ? "Edit safely rejected"
                                : harness.showProofRepairApproval
                                  ? "Repair target ready for approval"
                                : "Waiting for your review"
        property string mcpEndpoint: "local://framehouse"
        property string mcpStatus: "Ready"
        property int mcpToolCount: 12
        property bool mcpAvailable: true
        property var pendingUserQuestions: harness.showClarification
                                           ? ["Should the reception stay warm editorial or become neutral documentary?"]
                                           : harness.showDestructiveApproval
                                             ? ["This wording sounds like it may replace an original or discard existing work. I will preserve the source and current edit—approve only a reversible draft?"]
                                             : harness.showProofRepairApproval
                                               ? ["I tightened the cleanup around the point you chose and will rebuild from the untouched original. Approve this new reversible target?"]
                                             : []
        property var availableStyles: [
            { id: "natural-portrait", name: "Natural Portrait", goal: "Warm, realistic color with protected highlights." }
        ]
        property var availableModels: [
            { id: "local-editor", name: "Local Editor", backend: "local" }
        ]
        property var photographerCalibrationProfile: ({
            contract: "agent-photographer-calibration-card-v1",
            available: true,
            projectLocal: true,
            profileDigestSha256: "b9d5a2353eb09380fa31211c3ec50bc7ef6152b9ee4071be8ec5b3ddfd630005",
            caseCount: 2,
            summary: "2 blind cases · 1 machine pass rejected · naturalness needs the most care",
            weakestDimension: "naturalness",
            validationConcernKeys: ["warmth"],
            influencesPlanning: true,
            influencesProofCritique: true,
            numericThresholdOverridesAllowed: false,
            professionalReadinessClaimed: false,
)qml") + QByteArray(R"qml(
            storesImageBytes: false,
            storesSourcePaths: false
        })
        property var photographerCalibrationCoach: ({
            contract: "agent-photographer-calibration-coach-v1",
            available: true,
            projectKey: "Wedding Delivery",
            projectLocal: true,
            profileAvailable: true,
            profileDigestSha256: photographerCalibrationProfile.profileDigestSha256,
            lessonCount: 1,
            falseConfidenceCount: 1,
            overConservativeCount: 0,
            headline: "Here’s where my judgment differed from yours.",
            summary: "1 signed disagreement · 1 too confident · 0 too cautious",
            primaryLesson: {
                contract: "agent-photographer-calibration-lesson-v1",
                lessonId: "smoke-warmth-lesson",
                outcome: "false-confidence",
                headline: "I was too confident about this result.",
                explanation: "You rejected this global edit at 2/5. Naturalness was the weakest dimension at 2/5; you flagged too warm.",
                decisionContrast: "MACHINE PASSED · PHOTOGRAPHER: DO NOT DELIVER",
                humanRating: 2,
                weakestDimension: "naturalness",
                weakestDimensionScore: 2,
                issueLabels: ["too warm"],
                relatedConcernKeys: ["warmth"],
                signedHumanJudgment: true,
                automaticActionTaken: false
            },
            photographerDecisionIsAuthority: true,
            machineCanRate: false,
            automaticModelTrainingAllowed: false,
            automaticThresholdMutationAllowed: false,
            requiresFreshCorpusAfterThresholdChange: true,
            activeDraftAffected: false,
            durableMemoryWritten: false,
            professionalReadinessClaimed: false,
            storesImageBytes: false,
            storesSourcePaths: false
        })
        property var approvedProjectTaste: ({
            contract: "agent-project-taste-mirror-v2",
            available: true,
            editable: true,
            name: "Wedding Story",
            renamable: true,
            forgetRequiresConfirmation: true,
            projectKey: "Wedding Delivery",
            projectLocal: true,
            maturity: "calibrated",
            summary: "Keep skin naturally warm, protect dress highlights, and leave the crop looser.",
            approvalCount: 4,
            correctionCount: 2,
            rejectionCount: 1,
            visualReferentCount: 1,
            twoSpeedPreference: {
                contract: "agent-two-speed-taste-signal-v1",
                available: harness.showTwoSpeedTastePreference,
                choiceCount: harness.showTwoSpeedTastePreference ? 4 : 0,
                firstLookCount: harness.showTwoSpeedTastePreference ? 3 : 0,
                deepReviewCount: harness.showTwoSpeedTastePreference ? 1 : 0,
                selectorTouchedCount: harness.showTwoSpeedTastePreference ? 4 : 0,
                minimumEvidenceCount: 2,
                enoughEvidence: harness.showTwoSpeedTastePreference,
                preferredVersion: harness.showTwoSpeedTastePreference ? "first-look" : "",
                influencesPlanning: harness.showTwoSpeedTastePreference,
                planningGuidance: harness.showTwoSpeedTastePreference
                                  ? "Prefer restrained, direct edits close to the proved first look unless this photo or request clearly benefits from deeper refinement."
                                  : "",
                authority: "project-local-qualitative-guidance-only",
                currentRequestWins: true,
                automaticVersionSelectionAllowed: false,
                automaticCommitAllowed: false,
                forgetRequiresConfirmation: true,
                storesImageBytes: false,
                storesProofPaths: false,
                storesAdjustmentSnapshots: false
            },
            projectNorthStarAvailable: false,
            projectNorthStar: ({}),
            evidenceCount: 4,
            photographerCalibrationAvailable: true,
            photographerCalibration: fakeController.photographerCalibrationProfile,
            storesImageBytes: false,
            storesSourcePaths: false,
            evidence: [
                {
                    contract: "agent-project-taste-evidence-v1",
                    kind: "approved-correction",
                    id: "approved-correction-1",
                    title: "Correction you taught me",
                    summary: "Keep skin naturally warm and protect the dress highlights.",
                    detail: "Learned only after you approved the revised edit",
                    assetIds: ["asset-1"],
                    forgettable: true,
                    confirmationRequired: true
                },
                {
                    contract: "agent-project-taste-evidence-v1",
                    kind: "approved-look",
                    id: "approved-look-1",
                    title: "Approved look",
                    summary: "Natural wedding color with restrained contrast.",
                    detail: "20 photos · natural-portrait",
                    assetIds: ["asset-1", "asset-2"],
                    forgettable: true,
                    confirmationRequired: true
                },
                {
                    contract: "agent-project-taste-evidence-v1",
                    kind: "visual-referent",
                    id: "referent-upper-right-window",
                    title: "Remembered object",
                    summary: "Upper right window",
                    detail: "Exact project and photo only",
                    assetIds: ["asset-1"],
                    forgettable: true,
                    confirmationRequired: true
                },
                {
                    contract: "agent-project-taste-evidence-v1",
                    kind: "rejected-draft",
                    id: "rejected-look-1",
                    title: "Direction you discarded",
                    summary: "Heavy contrast and a cooler reception palette.",
                    detail: "Avoid exact recipe only · no reason inferred",
                    assetIds: ["asset-1"],
                    forgettable: true,
                    confirmationRequired: true
                }
            ]
        })
        property var assignmentTasteReflection: ({
            contract: "agent-assignment-taste-reflection-v1",
            available: harness.showTwoSpeedTastePreference,
            projectKey: "Wedding Delivery",
            projectLocal: true,
            headline: harness.showTwoSpeedTastePreference
                      ? "You repeatedly kept the restrained first look."
                      : "",
            observations: harness.showTwoSpeedTastePreference ? [
                {
                    kind: "confirmed-proved-version-pattern",
                    headline: "You repeatedly kept the restrained first look.",
                    detail: "3 of 4 confirmed choices kept First Look; the other 1 kept Deep Review.",
                    confirmedEvidenceCount: 4
                },
                {
                    kind: "deliberate-comparison",
                    headline: "You compared before deciding.",
                    detail: "The version switcher was used on 4 confirmed choices.",
                    confirmedEvidenceCount: 4
                }
            ] : [],
            observationCount: harness.showTwoSpeedTastePreference ? 2 : 0,
            sourceContract: "agent-two-speed-taste-signal-v1",
            sourceChoiceCount: harness.showTwoSpeedTastePreference ? 4 : 0,
            sourcePreferredVersion: harness.showTwoSpeedTastePreference
                                    ? "first-look" : "",
            proposedNorthStar: harness.showTwoSpeedTastePreference
                               ? "Keep edits restrained and direct unless the photograph or current request clearly benefits from deeper refinement."
                               : "",
            proposalOrigin: "aggregate-confirmed-proved-version-choices",
            durableMemoryWritten: false,
            requiresExplicitNorthStarSave: true,
            automaticMemoryWriteAllowed: false,
            automaticRecipeInfluenceAllowed: false,
            activeDraftUnaffected: true,
            currentRequestWins: true,
            dismissalScope: "session-only",
            storesAssetIds: false,
            storesImageBytes: false,
            storesSourcePaths: false,
            storesAdjustmentSnapshots: false
        })
        property var runEvents: harness.showLocalPlanning ? [
            { stage: "observe", status: "complete", detail: "Inspected the representative photo." },
            { stage: "plan", status: "running", detail: "Preparing the constrained plan." },
            {
                stage: "plan-worker",
                status: "running",
                headline: "I'm reviewing this photo locally.",
                explanation: "The local vision model is reviewing the photo and building a reversible plan without blocking the workspace. You can cancel safely; no pixels have changed.",
                uiAnchor: "image",
                characterMode: "thinking",
                characterTool: "loupe",
                evidence: {
                    localOnly: true,
                    cancelSafe: true,
                    pixelsChanged: false,
                    modelName: "Qwen3 VL 32B"
                }
            }
        ] : [
            { stage: "observe", status: "complete", detail: "Inspected the representative photos." },
            { stage: "render", status: "complete", detail: "Rendered actual before and after proofs." },
            { stage: "validate", status: "complete", detail: "Verified the samples against the requested result." }
        ]
)qml") + QByteArray(R"qml(
        property var progressivePreview: harness.showProgressivePreview ? ({
            contract: "agent-progressive-draft-preview-v1",
            twoSpeedContract: "agent-two-speed-planning-preview-v1",
            eligible: true,
            published: true,
            state: "fast-validated-deep-optional",
            mode: "fast-planning-preview",
            assetId: "asset-1",
            publishCount: 1,
            firstPublishMs: 6,
            firstPublishBudgetMs: 2000,
            firstPublishWithinBudget: true,
            validationPending: true,
            fastProofContract: "agent-fast-draft-independent-proof-v1",
            fastValidationPending: false,
            fastValidationPassed: true,
            independentProof: true,
            deepDeadlineReached: harness.fastDeadlineReached,
            keepFastDraftAvailable: true,
            fastChoiceContract: "agent-proved-fast-choice-v1",
            choiceAvailableAfterProof: true,
            hardwareTier: "balanced",
            deepModelDeadlineMs: 30000,
            hardwareDeadline: {
                contract: "agent-fast-draft-hardware-deadline-v1",
                hardwareTier: "balanced",
                deepModelDeadlineMs: 30000,
                keepRequiresIndependentProof: true,
                keepRequiresDeadline: false,
                choiceAvailableAfterProof: true,
                automaticFastDraftSelection: false
            },
            deepRefinementPending: true,
            fastPreviewIsFinal: false,
            commitBlockedWhilePending: true,
            rollbackOnFailure: true,
            validationExecution: "request-owned-proof-continuation",
            durableStateWritten: false,
            thumbnailRefreshDeferred: true,
            scopePolicy: "single-asset-low-risk-global-only"
        }) : ({})
        property var lastRunReport: harness.showProofRepairApproval ? ({
            ok: false,
            mode: "proof-repair-point",
            decision: "needs_user_input",
            needsUserInput: true,
            inputKind: "pre_draft_plan_approval",
            safeStopReason: "point_guided_retouch_repair",
            selectionConfirmation: { required: false, candidates: [] },
            proofRepair: {
                contract: "agent-proof-repair-plan-v1",
                assetId: "asset-1",
                point: { x: harness.lastPointX, y: harness.lastPointY },
                regionScale: 0.82,
                rebuildBase: "untouched-original",
                freshApprovalRequired: true,
                oldArtifactReused: false
            }
        }) : harness.showProofRejection ? ({
            ok: false,
            mode: "draft-apply",
            proofRejected: true,
            approvalConsumed: true,
            retryable: true,
            retryPolicy: "replan-from-original-with-adjusted-target",
            failureReason: "The cleanup changed the color or texture around the target, so I rejected it and left your photo unchanged.",
            selectionConfirmation: { required: false, candidates: [] },
            proofRejection: {
                contract: "agent-proof-rejection-presentation-v1",
                kind: "retouch-quality",
                headline: "I left your photo unchanged.",
                explanation: "The cleanup changed the color or texture around the target, so I rejected it and left your photo unchanged.",
                primaryActionKey: "point-to-repair",
                primaryActionLabel: "Point to repair",
                commitBlocked: true,
                originalRecipePreserved: true,
                visualCritique: {
                    contract: "agent-visual-critique-v1",
                    verdict: "reject",
                    issueKey: "boundary-color-drift",
                    targetLabel: "sensor spot",
                    finding: "The repair is tinting nearby pixels.",
                    evidenceSummary: "Nearby color drift measured 0.81% against a 0.40% delivery limit.",
                    assetId: "asset-1",
                    region: { centerX: 0.78, centerY: 0.21, radiusX: 0.018, radiusY: 0.02 },
                    repair: {
                        kind: "point-and-tighten",
                        label: "Point to repair",
                        instruction: "Point to the center of the distraction. I’ll tighten the old target, rebuild from the untouched original, and ask for approval again."
                    }
                }
            }
        }) : harness.showDestructiveApproval ? ({
            ok: false,
            mode: "draft",
            decision: "needs_user_input",
            needsUserInput: true,
            inputKind: "pre_draft_plan_approval",
            safeStopReason: "high_risk_or_low_confidence",
            safetyContract: "agent-destructive-intent-seatbelt-v1",
            selectionConfirmation: { required: false, candidates: [] },
            riskAssessment: {
                approvalRequired: true,
                approvalTypes: ["destructive_intent"],
                approvalRequests: [{
                    type: "destructive_intent",
                    label: "Replace or discard existing work",
                    reason: "The source and current recipe stay preserved."
                }]
            }
        }) : ({
            ok: !harness.showClarification,
            inputKind: harness.showClarification ? "model_clarification" : "",
            modelId: "qwen3-vl:32b",
            twoSpeedDraftChoice: harness.showTwoSpeedDraftChoice ? ({
                contract: "agent-two-speed-draft-choice-v1",
                available: true,
                assetId: "asset-1",
                projectKey: "Wedding Delivery",
                currentVersion: harness.twoSpeedDraftVersion,
                switchCount: harness.twoSpeedDraftVersion === "first-look" ? 1 : 0,
                originalRollbackRetained: true,
                commitStillRequiresConfirmation: true,
                automaticSelectionAllowed: false,
                photographerIsAuthority: true,
                versions: [
                    {
                        key: "first-look",
                        label: "First Look",
                        modelId: "rulebook-deterministic",
                        independentlyProved: true
                    },
                    {
                        key: "deep-review",
                        label: "Deep Review",
                        modelId: "qwen3-vl:32b",
                        independentlyProved: true
                    }
                ]
            }) : ({}),
            projectNorthStarReceipt: harness.showNorthStarCompass ? ({
                contract: "agent-project-north-star-draft-receipt-v1",
                available: true,
                northStarId: "north-star-smoke",
                projectKey: "Wedding Delivery",
                brief: "Keep this wedding intimate and documentary; protect skin, highlights, and unposed moments across every delivery.",
                digestSha256: "0f7c19c7b63ba28a60cbf64341dcf0ae0c459460ec0426641d0077b686922939",
                planningConsidered: true,
                qualitativePlanningInfluence: true,
                proofValidated: true,
                proofValidatedAssetCount: 1,
                proofCritiqueConsidered: true,
                proofCritiqueAssetCount: 1,
                proofReviewMode: "local-model-plus-deterministic-validation",
                currentRequestWins: true,
                styleAndSafetyWin: true,
                alignmentClaimed: false,
                requiresPhotographerJudgment: true,
                directNumericRecipeAuthority: false,
                automaticCullAuthority: false,
                automaticRetouchAuthority: false,
                destructiveAuthority: false
            }) : ({}),
            sampleOnlyDraft: true,
            fullTargetCount: 20,
            remainingAssetIds: ["asset-3", "asset-4"],
            batchSummary: harness.showExceptionReview ? {
                assetCount: 500,
                proofCount: 500,
                readyForCommit: false,
                consistencyMap: {
                    contract: "agent-professional-consistency-map-v1",
                    available: true,
                    status: "attention",
                    headline: "498 consistent · 2 drift",
                    summary: "Tone, color, and skin response checked across 3 lighting groups.",
                    basis: "within-lighting-group-relative-render-response",
                    assetCount: 500,
                    groupCount: 3,
                    consistentAssetCount: 498,
                    driftCount: 2,
                    driftAssetIds: ["IMG_0317", "IMG_0411"],
                    measurementComplete: true,
                    professionalGateEligible: true,
                    consistencyPassed: false,
                    readyForProfessionalUse: false,
                    professionalReadinessClaimed: false,
                    humanCalibrationRequired: true,
                    digestSha256: "0f13a36b7b52423ca4a2216c186477f781da09bb0cf43f6a4ea08ebfcf1ca9c2",
                    groups: [
                        {
                            clusterId: "ceremony-window",
                            label: "Ceremony",
                            photoCount: 182,
                            consistentCount: 181,
                            driftCount: 1,
                            status: "attention"
                        },
                        {
                            clusterId: "portraits-open-shade",
                            label: "Portraits",
                            photoCount: 146,
                            consistentCount: 146,
                            driftCount: 0,
                            status: "aligned"
                        },
                        {
                            clusterId: "reception-tungsten",
                            label: "Reception",
)qml") + QByteArray(R"qml(
                            photoCount: 172,
                            consistentCount: 171,
                            driftCount: 1,
                            status: "attention"
                        }
                    ],
                    reproducibility: {
                        contract: "agent-consistency-reproducibility-v1",
                        deterministic: true,
                        stableAssetOrdering: true,
                        fixedThresholds: true,
                        stochasticSampling: false
                    }
                },
                reviewSummary: {
                    contract: "agent-exception-review-v1",
                    totalCount: 500,
                    editedCount: 500,
                    passedCount: 498,
                    verifiedCount: 498,
                    hiddenVerifiedCount: 498,
                    needsReviewCount: 2,
                    failedCount: 0,
                    issueCount: 2,
                    exceptionCount: 2,
                    reviewWorkItemCount: 2,
                    headline: "498 verified · 2 need review",
                    requiresReview: true,
                    commitBlockedByExceptions: true,
                    showOnlyIssuesFirst: true,
                    queueContainsOnlyExceptions: true,
                    workloadScalesWithExceptions: true,
                    reviewWorkloadPolicy: "one-batch-receipt-plus-exceptions-only",
                    fixAllOutliersAvailable: true
                },
                reviewIssues: [
                    {
                        assetId: "IMG_0317",
                        displayName: "IMG_0317.jpg",
                        status: "needs_review",
                        category: "warning",
                        reason: "Highlight clipping is visible in the dress.",
                        suggestedAction: "Review the proof and adjust if the warning is visible.",
                        thumbnailPath: aiEditAfterProofPath,
                        afterProofPath: aiEditAfterProofPath,
                        warningCount: 1,
                        errorCount: 0,
                        consistencyDrift: {
                            assetId: "IMG_0317",
                            clusterId: "ceremony-window",
                            driftMetrics: ["Warmth"]
                        },
                        repairGuidance: {
                            contract: "agent-consistency-drift-repair-v1",
                            headline: "Match Warmth to this lighting group",
                            summary: "Warmth moved differently from the approved group reference.",
                            actionLabel: "Match group",
                            suggestedAction: "Apply a bounded color correction and render again.",
                            preservedSummary: "Crop, framing, local masks, retouch, and texture stay untouched.",
                            autoRepairSafe: true,
                            recipeDelta: { color: { temperature: -4.0 } },
                            requiresFreshProof: true,
                            keepsExceptionOpenUntilAligned: true
                        }
                    },
                    {
                        assetId: "IMG_0411",
                        displayName: "IMG_0411.jpg",
                        status: "needs_review",
                        category: "corrected_outlier",
                        reason: "Validation repaired this photo independently from its cluster.",
                        suggestedAction: "Review the corrected outlier before commit.",
                        thumbnailPath: aiEditBeforeProofPath,
                        afterProofPath: aiEditBeforeProofPath,
                        warningCount: 0,
                        errorCount: 0,
                        outlierCorrected: true
                    }
                ]
            } : { proofCount: 2 },
            artifacts: { currentProofPath: aiEditAfterProofPath },
            selectionConfirmation: {
                contract: "agent-grounded-selection-confirmation-v1",
                required: harness.showSelectionConfirmation,
                confirmed: !harness.showSelectionConfirmation,
                targetLabel: "second window",
                candidateCount: 2,
                candidates: [
                    {
                        id: "candidate-left-window",
                        label: "Left window",
                        confidence: 0.78,
                        region: { centerX: 0.26, centerY: 0.21, radiusX: 0.12, radiusY: 0.13, feather: 0.68 }
                    },
                    {
                        id: "candidate-upper-right-window",
                        label: "Upper-right window",
                        confidence: 0.76,
                        region: { centerX: 0.84, centerY: 0.22, radiusX: 0.12, radiusY: 0.16, feather: 0.68 }
                    }
                ],
                policy: "no-edit-before-explicit-instance-confirmation"
            },
            visualReferentResolution: harness.showVisualReferentReuse ? {
                contract: "agent-visual-referent-resolution-v1",
                resolved: true,
                referentId: "referent-upper-right-window",
                assetId: "asset-1",
                targetLabel: "window",
                chosenLabel: "Upper right window",
                source: "explicit-user-confirmed-object-memory",
                scopePolicy: "exact-project-and-asset-explicit-reference-only",
                fastPathUsed: true,
                modelPlanSkipped: true,
                mcpDiscoverySkipped: true,
                planningDurationMs: 1,
                planningBudgetMs: 50,
                planningWithinBudget: true,
                transientMatteReused: true,
                transientMatteReuseCount: 1,
                transientMattePersisted: false,
                modelReplanRequired: false,
                crossAssetReuseAllowed: false
            } : ({}),
            targetedCorrection: {
                contract: "agent-targeted-correction-v1",
                applied: true,
                maskId: "agent-localized-window-proof",
                target: "object",
                targetLabel: "Upper right window",
                changedFields: ["opacity"],
                fullReplan: false,
                preservedUnrelatedEdits: true,
                generatedMattePreserved: true
            },
            pointGroundedCorrection: harness.showPointGroundedCorrection ? {
                contract: "agent-point-grounded-correction-v2",
                applied: true,
                assetId: harness.lastPointAssetId,
                maskId: "agent-point-smoke-proof",
                target: harness.showPointFallback ? "pointed-area" : "pointed-content",
                targetLabel: harness.showPointFallback
                             ? "Feathered pointed area" : "Pointed content",
                resolutionMode: harness.showPointFallback
                                ? "radial-fallback" : "content-snap",
                resolutionLabel: harness.showPointFallback
                                 ? "Feathered area fallback" : "Snapped to local content",
                boundaryKind: harness.showPointFallback
                              ? "feathered-radial" : "generated-matte",
                generatedBoundary: !harness.showPointFallback,
                radialFallbackUsed: harness.showPointFallback,
                selectionEvidence: {
                    status: harness.showPointFallback
                            ? "radial-fallback" : "point-content-snap",
                    centerValue: harness.showPointFallback ? 0 : 255,
                    coverage: harness.showPointFallback ? 0 : 0.08,
                    evidenceContrast: harness.showPointFallback ? 0 : 42,
                    evidenceContrastThreshold: 10
                },
                feedback: harness.lastPointFeedback,
                changedFields: ["exposure"],
                changes: ["darkened the target"],
                region: {
                    centerX: harness.lastPointX,
                    centerY: harness.lastPointY,
                    radiusX: 0.12,
                    radiusY: 0.12,
                    feather: 0.72
                },
                fullReplan: false,
                preservedUnrelatedEdits: true,
                preservedOutsidePointedRegion: harness.showPointFallback,
                preservedOutsideResolvedTarget: true,
                draftReversible: true,
                durableMemoryWritten: false
            } : ({}),
            protectedDraftIntents: harness.showIntentProtected ? [
                {
                    contract: "agent-protected-draft-intent-v1",
                    maskId: "agent-localized-window-proof",
                    assetId: "asset-1",
                    target: "object",
                    targetLabel: "Upper right window",
                    userProtected: true,
                    protectionPolicy: "exact-current-draft-mask-until-explicit-unlock"
                }
            ] : [],
            intentBoundary: {
                contract: "agent-draft-intent-boundary-v1",
                enforced: true,
                protectedCount: harness.showIntentProtected ? 1 : 0,
                targetedCorrectionBlocked: harness.showIntentProtected,
                fullReplanBlocked: harness.showIntentProtected,
                unlockRequired: harness.showIntentProtected
            },
            assetReports: [
                {
                    assetId: "asset-1",
                    groundedSelection: {
                        contract: "agent-grounded-selection-materialization-v2",
                        requestedCount: 1,
                        semanticRequestedCount: 0,
                        guidedRequestedCount: 1,
                        generatedCount: 1,
)qml") + QByteArray(R"qml(
                        semanticGeneratedCount: 0,
                        guidedGeneratedCount: 1,
                        fallbackCount: 0,
                        failClosed: true
                    }
                }
            ],
            editIntent: {
                globalChangePolicy: "preserve",
                visualFindings: [
                    {
                        maskId: "agent-localized-window-proof",
                        kind: "highlight_clipping",
                        target: "object",
                        targetLabel: "Upper right window",
                        severity: "high",
                        observation: "The upper-right window is pulling attention away from the subject.",
                        correctionApplied: true,
                        correctionFeedback: "Less on this, but leave every other edit unchanged.",
                        userProtected: harness.showIntentProtected,
                        confidence: 0.94,
                        region: { centerX: 0.82, centerY: 0.22, radiusX: 0.18, radiusY: 0.24 }
                    },
                    {
                        kind: "subject_exposure",
                        target: "central_subject",
                        severity: "low",
                        observation: "The subject is already balanced and should remain unchanged.",
                        confidence: 0.91,
                        region: { centerX: 0.50, centerY: 0.52, radiusX: 0.22, radiusY: 0.38 }
                    }
                ],
                localizedActions: [
                    {
                        maskId: "agent-localized-window-proof",
                        target: "object",
                        targetLabel: "Upper right window",
                        selectionMode: "guided",
                        maskShape: "radial",
                        reason: "Reduce the isolated glare without changing the subject or overall color.",
                        userProtected: harness.showIntentProtected,
                        confidence: 0.93,
                        region: { centerX: 0.82, centerY: 0.22, radiusX: 0.18, radiusY: 0.24, feather: 0.72 },
                        adjustments: { exposure: -0.42, highlights: -31 }
                    }
                ]
            },
)qml") + QByteArray(R"qml(
            assignmentStoryMap: {
                contract: "agent-assignment-story-map-v1",
                roleEvidenceContract: "agent-assignment-story-role-evidence-v1",
                available: true,
                headline: "Review the visual story before the full set",
                summary: "4 story anchors join 3 lighting representatives in the sample review.",
                roleCards: [
                    { role: harness.storyRoleForAsset("asset-1"), label: harness.storyRoleLabel(harness.storyRoleForAsset("asset-1")), assetId: "asset-1" },
                    { role: harness.storyRoleForAsset("asset-2"), label: harness.storyRoleLabel(harness.storyRoleForAsset("asset-2")), assetId: "asset-2" },
                    { role: harness.storyRoleForAsset("asset-3"), label: harness.storyRoleLabel(harness.storyRoleForAsset("asset-3")), assetId: "asset-3" },
                    { role: harness.storyRoleForAsset("asset-4"), label: harness.storyRoleLabel(harness.storyRoleForAsset("asset-4")), assetId: "asset-4" }
                ],
                assignmentsByAssetId: {
                    "asset-1": { assetId: "asset-1", role: harness.storyRoleForAsset("asset-1"), label: harness.storyRoleLabel(harness.storyRoleForAsset("asset-1")), reason: harness.storyRoleReason("asset-1"), roleEvidence: harness.storyRoleEvidence("asset-1") },
                    "asset-2": { assetId: "asset-2", role: harness.storyRoleForAsset("asset-2"), label: harness.storyRoleLabel(harness.storyRoleForAsset("asset-2")), reason: harness.storyRoleReason("asset-2"), roleEvidence: harness.storyRoleEvidence("asset-2") },
                    "asset-3": { assetId: "asset-3", role: harness.storyRoleForAsset("asset-3"), label: harness.storyRoleLabel(harness.storyRoleForAsset("asset-3")), reason: harness.storyRoleReason("asset-3"), roleEvidence: harness.storyRoleEvidence("asset-3") },
                    "asset-4": { assetId: "asset-4", role: harness.storyRoleForAsset("asset-4"), label: harness.storyRoleLabel(harness.storyRoleForAsset("asset-4")), reason: harness.storyRoleReason("asset-4"), roleEvidence: harness.storyRoleEvidence("asset-4") },
                    "asset-5": { assetId: "asset-5", role: harness.storyRoleForAsset("asset-5"), label: harness.storyRoleLabel(harness.storyRoleForAsset("asset-5")), reason: harness.storyRoleReason("asset-5"), roleEvidence: harness.storyRoleEvidence("asset-5") }
                },
                storySampleAssetIds: ["asset-1", "asset-2", "asset-3", "asset-4"],
                photographerSteered: harness.storyRoleRevisionCount > 0,
                planningInfluence: "sample-review-coverage-only",
                semanticRecognitionUsed: false,
                noAutoCull: true,
                sourceOrderPreserved: true,
                recipeVariationApplied: false,
                durableMemoryWritten: false
            },
            approvedAssignmentStoryReceipt: harness.showExceptionReview ||
                                                harness.showStoryPreferenceCommitted ? {
                contract: "agent-approved-assignment-story-receipt-v1",
                available: true,
                approved: true,
                approvalSource: "explicit-apply-to-all",
                storyMapContract: "agent-assignment-story-map-v1",
                storyMapDigestSha256: "bd2879c9a3e17aa6fc7fd4f7726f41a6d21b970e2a6b6f0f66a417844b1ab785",
                roleEvidenceContract: "agent-assignment-story-role-evidence-v1",
                photographerSteered: harness.storyRoleRevisionCount > 0,
                approvedAssetCount: 20,
                newlyDraftedCount: 18,
                sourceOrderPreserved: true,
                noAutoCull: true,
                recipeVariationApplied: false,
                semanticRecognitionUsed: false,
                storyDirectionMemoryWritten: false,
                realProofsReviewed: true,
                guardrailsPassed: true,
                deliveryInfluence: "review-and-delivery-receipt-only",
                recipePolicy: "validated-lighting-group-plan-unchanged-by-story-role",
                headline: "Story direction approved for delivery",
                summary: "20 photos keep the approved arc in source order. Story roles do not vary recipes or cull photos.",
                digestSha256: "c5bd7b74cc4b69df1969af0c047ac43acaa617a3d1c2a087c60ed4f77879cf39"
            } : ({}),
            sampleReview: {
                available: true,
                sampleAssetIds: ["asset-1", "asset-2"],
                remainingAssetIds: ["asset-3", "asset-4"],
                samples: [
                    { assetId: "asset-1", beforeProofPath: aiEditBeforeProofPath, afterProofPath: aiEditAfterProofPath },
                    { assetId: "asset-2", beforeProofPath: aiEditSecondBeforeProofPath, afterProofPath: aiEditSecondAfterProofPath }
                ]
            },
            correctionTransferPreview: harness.correctionTransferActive ? {
                contract: "agent-correction-transfer-preview-v1",
                available: true,
                state: "waiting-for-approval",
                feedback: harness.lastSampleFeedback,
                sampleCount: 2,
                remainingCount: 18,
                batchGroupCount: 3,
                remainingUntouched: true,
                approvalRequired: true,
                approvalAction: "apply_to_all",
                durableMemoryDeferred: true,
                durableMemoryWritten: false,
                projectLocal: true,
                transferPolicy: "cluster-aware-after-sample-approval",
                headline: "Your correction is ready for 18 more photos",
                explanation: "I rebuilt 2 samples and each lighting-group plan with this correction. The remaining 18 stay untouched until you approve Apply to All."
            } : ({}),
            validationReport: {
                ok: true,
                qualityScorePercent: 96,
                intentMatch: { available: true, ok: true, scorePercent: 96 }
            },
            riskAssessment: {
                approvalRequired: true,
                approvalRequests: [ { type: "large_crop", label: "Large crop" } ]
            },
            editStrategy: {
                contract: "agent-edit-strategy-route-v1",
                ok: true,
                executionAllowed: true,
                primaryPath: "local-mask",
                label: "Localized adjustment",
                reason: "A bounded target can be improved while everything outside its mask stays unchanged.",
                preserved: ["Everything outside the target", "Framing", "Retouching"],
                modelSuggestedGroups: ["masks"],
                executedGroups: ["masks"],
                suppressedGroups: [],
                localizedOnly: true,
                appRouted: true,
                routingAuthority: "photo-editor-app",
                routePolicy: "least-invasive-capable-path-before-draft",
                sensitiveActionsPauseBeforeDraft: true,
                existingEditsPreserved: true
            },
            planSummary: "Balanced color and tone while preserving the subject and highlights."
        })

        function aiEditWorkflowState(scope) {
            const labels = {
                create_draft: "Create Draft",
                choose_style: "Choose Style",
                learn_style: "Learn This Style",
                set_up_local_ai: "Set Up Local AI"
            }
            return {
                ok: true,
                statusLine: "Waiting for your review",
                scopeLabel: "20 selected photos",
                assetCount: 20,
                sampleReviewWaiting: !(harness.showExceptionReview ||
                                       harness.showStoryPreferenceCommitted),
                sampleRemainingCount: harness.showExceptionReview ||
                                      harness.showStoryPreferenceCommitted ? 0 : 18,
                sampleReview: lastRunReport.sampleReview,
                assignmentStoryMap: lastRunReport.assignmentStoryMap,
                imageRuntime: {
                    contract: "agent-retouch-runtime-readiness-v1",
                    inpaintAvailable: false,
                    inpaintRuntime: "Local CPU Fill",
                    bundleVerified: false,
                    inpaintArtifactVerified: false,
                    bundleVerificationErrorCode: harness.showCorruptModelBundle
                                                     ? "artifact_sha256_mismatch"
                                                     : "manifest_missing",
                    bundleVerificationError: harness.showCorruptModelBundle
                                                 ? "Model artifact checksum does not match its manifest."
                                                 : "Model manifest is missing.",
                    retouchSetupRequired: true,
                    fallbackProtectedByProof: true
                },
                directionComparison: harness.directionComparison,
                directionChoicePending: Boolean(harness.directionComparison.choiceRequired),
                primaryActionEnabled: true,
                primaryAction: {
                    key: workflowPrimaryKey,
                    label: labels[workflowPrimaryKey] || "Create Draft",
                    enabled: true
                },
                styleLearning: { available: false }
            }
        }
        function batchReviewForecast(scope) {
            return {
                ok: true,
                available: true,
                contract: "agent-batch-review-forecast-v1",
                scope: scope,
                totalCount: 20,
                clusterCount: 3,
                sampleCount: 3,
                predictionAvailable: true,
                confidence: "medium",
                priorRunCount: 3,
                priorPhotoCount: 120,
)qml") + QByteArray(R"qml(
                estimatedAutomaticCount: 18,
                estimatedReviewCount: 2,
                lowerReviewCount: 1,
                upperReviewCount: 4,
                headline: "18 likely automatic · 2 likely review",
                explanation: "Estimated from 120 prior project photos across 3 runs; expected review range 1–4.",
                readOnly: true,
                projectOnlyEvidence: true
            }
        }
        function styleReferencePreflight(assetIds) {
            return {
                contract: "agent-style-reference-preflight-v1",
                ok: assetIds.length > 0,
                available: assetIds.length > 0,
                selectedCount: assetIds.length,
                editedReferenceCount: assetIds.length,
                pairedOriginalCount: 0,
                unrelatedOriginalCount: 0,
                unavailableSourceCount: 0,
                mixedEditedAndOriginal: false,
                warningRequired: false,
                reason: assetIds.length > 0 ? "Selected edited references can train a style." : "Select edited examples to learn a style."
            }
        }
        function applySampleReviewToAll(confirmed) { return { ok: confirmed } }
        function storyPreferenceStatus() {
            return {
                contract: "agent-story-preference-status-v1",
                available: harness.storyPreferenceRemembered,
                projectKey: "Wedding Delivery",
                projectLocal: true,
                preferenceId: harness.storyPreferenceRemembered
                              ? "story-smoke-preference" : "",
                influencePolicy: harness.storyPreferenceRemembered
                                 ? "future-story-role-suggestions-only" : "none"
            }
        }
        function rememberApprovedStoryPreference(confirmed) {
            harness.storyPreferenceRemembered = confirmed
            return {
                ok: confirmed,
                remembered: confirmed,
                contract: "agent-story-preference-remember-v1",
                preferenceId: "story-smoke-preference"
            }
        }
        function forgetApprovedStoryPreference(confirmed) {
            if (confirmed)
                harness.storyPreferenceRemembered = false
            return {
                ok: confirmed,
                forgotten: confirmed,
                contract: "agent-story-preference-forget-v1",
                preferenceId: "story-smoke-preference"
            }
        }
        function photographerTasteCheck() {
            const available = harness.showPhotographerTasteCheck && draftActive &&
                              String(harness.directionComparison.state || "").length === 0
            return {
                contract: "agent-photographer-taste-check-v1",
                available: available,
                recommended: available,
                projectLocal: true,
                projectKey: "Wedding Delivery",
                calibrationAvailable: true,
                profileDigestSha256: photographerCalibrationProfile.profileDigestSha256,
                trigger: "calibration-sensitive-draft",
                primaryConcernGroup: "warmth",
                activeConcernGroups: ["warmth"],
                activeConcernKeys: ["warmth"],
                headline: "I’m less certain about warmth here.",
                explanation: "Your blind ratings marked warmth as calibration-sensitive. I can render a more restrained version before you decide.",
                alternativeDirection: "Keep the same framing, masks, and retouching, but use more restrained natural warmth.",
                requiresExplicitChoice: true,
                automaticComparisonStarted: false,
                durableMemoryWritten: false,
                choicePromotionPolicy: "explicit-direction-choice-confirmed-by-commit-only",
                numericThresholdOverridesAllowed: false,
                professionalReadinessClaimed: false
            }
        }
        function compareDraftWithPhotographerCalibration() {
            const tasteCheck = photographerTasteCheck()
            if (!tasteCheck.recommended)
                return { ok: false, accepted: false, photographerTasteCheck: tasteCheck }
            harness.calibratedComparisonRequested = true
            return compareDraftDirection(tasteCheck.alternativeDirection)
        }
        function compareDraftDirection(alternative) {
            if (String(alternative || "").trim().length < 4)
                return { ok: false, accepted: false, errors: ["Describe an alternative direction."] }
            harness.comparisonPreparing = true
            directionComparisonReadyTimer.start()
            return {
                ok: true,
                accepted: true,
                requestId: "smoke-direction-comparison-1",
                contract: "agent-async-draft-run-v2"
            }
        }
        function chooseDraftDirection(key) {
            if (key !== "a" && key !== "b")
                return { ok: false }
            const next = Object.assign({}, harness.directionComparison)
            next.state = "chosen"
            next.choiceRequired = false
            next.chosenKey = key
            next.activeDraftKey = key
            next.choice = {
                contract: "agent-draft-direction-choice-v1",
                key: key,
                label: key === "a" ? "Direction A" : "Direction B",
                durableMemoryWritten: false,
                promotionPolicy: "remember-only-after-confirmed-commit"
            }
            harness.directionComparison = next
            draftChanged()
            return { ok: true, chosenKey: key, directionComparison: next }
        }
        function chooseTwoSpeedDraftVersion(key) {
            if (key !== "first-look" && key !== "deep-review")
                return { ok: false, errorCode: "invalid_draft_version" }
            harness.twoSpeedDraftVersion = key
            draftChanged()
            return {
                ok: true,
                contract: "agent-two-speed-draft-choice-v1",
                currentVersion: key,
                draftActive: true,
                committed: false,
                originalRollbackRetained: true,
                commitStillRequiresConfirmation: true
            }
        }
        function forgetTwoSpeedTastePreference(confirmed) {
            if (!confirmed) {
                return {
                    ok: false,
                    contract: "agent-two-speed-taste-forget-v1",
                    confirmationRequired: true
                }
            }
            harness.showTwoSpeedTastePreference = false
            const nextTaste = Object.assign({}, approvedProjectTaste)
            const nextPreference = Object.assign({},
                                                   nextTaste.twoSpeedPreference || ({}))
            nextPreference.available = false
            nextPreference.choiceCount = 0
            nextPreference.firstLookCount = 0
            nextPreference.deepReviewCount = 0
            nextPreference.selectorTouchedCount = 0
            nextPreference.enoughEvidence = false
            nextPreference.preferredVersion = ""
            nextPreference.influencesPlanning = false
            nextPreference.planningGuidance = ""
            nextTaste.twoSpeedPreference = nextPreference
            approvedProjectTaste = nextTaste
            const nextReflection = Object.assign({}, assignmentTasteReflection)
            nextReflection.available = false
            nextReflection.headline = ""
            nextReflection.observations = []
            nextReflection.observationCount = 0
            nextReflection.sourceChoiceCount = 0
            nextReflection.sourcePreferredVersion = ""
            nextReflection.proposedNorthStar = ""
            assignmentTasteReflection = nextReflection
            projectTasteChanged()
            return {
                ok: true,
                contract: "agent-two-speed-taste-forget-v1",
                forgotten: true,
                approvedEditEvidenceRetained: true,
                activeEditsUnaffected: true
            }
        }
        function suggestInspirationForPrompt(prompt) {
            return {
                ok: prompt.length > 0,
                modelName: "Local Editor",
                styles: [
                    {
                        kind: "preset",
                        id: "natural-portrait",
                        title: "Natural Portrait",
                        detail: "Warm, realistic color with protected highlights."
                    }
                ],
                photos: []
            }
        }
        function createStyleFromDescription(description, name) {
            if (String(description || "").trim().length < 4)
                return { ok: false, errors: ["Describe the look first."] }
            const style = {
                id: "wedding-delivery-look",
                name: "Wedding Delivery Look",
                goal: description,
                source: "project-description"
            }
            availableStyles = availableStyles.concat([style])
            selectedStyleId = style.id
            return {
                ok: true,
                mode: "described-style",
                contract: "agent-described-style-start-v1",
                styleId: style.id,
                styleName: style.name,
                rememberedForProject: true,
                advancedFieldsGenerated: true
            }
        }
        function reviseSampleReview(feedback) {
            harness.lastSampleFeedback = feedback
            harness.correctionTransferActive = feedback.length > 0
            return { ok: feedback.length > 0 }
        }
        function reviseAssignmentStoryRole(assetId, role) {
            const requestedAssetId = String(assetId || "")
            const requestedRole = String(role || "")
)qml") + QByteArray(R"qml(
            const allowedRoles = ["atmosphere", "hero", "detail", "transition", "support"]
            const currentAssignments = harness.storyRoleAssignments || ({})
            if (!currentAssignments[requestedAssetId] ||
                    allowedRoles.indexOf(requestedRole) < 0)
                return { ok: false, errors: ["Unsupported story assignment."] }
            const previousRole = String(currentAssignments[requestedAssetId])
            let swappedAssetId = ""
            const nextAssignments = ({})
            for (const candidateAssetId in currentAssignments) {
                nextAssignments[candidateAssetId] = currentAssignments[candidateAssetId]
                if (candidateAssetId !== requestedAssetId &&
                        String(currentAssignments[candidateAssetId]) === requestedRole)
                    swappedAssetId = candidateAssetId
            }
            if (swappedAssetId.length > 0)
                nextAssignments[swappedAssetId] = previousRole
            nextAssignments[requestedAssetId] = requestedRole
            const nextOverrideIds = harness.storyRoleOverrideAssetIds.slice()
            if (nextOverrideIds.indexOf(requestedAssetId) < 0)
                nextOverrideIds.push(requestedAssetId)
            if (swappedAssetId.length > 0 &&
                    nextOverrideIds.indexOf(swappedAssetId) < 0)
                nextOverrideIds.push(swappedAssetId)
            harness.storyRoleAssignments = nextAssignments
            harness.storyRoleOverrideAssetIds = nextOverrideIds
            harness.storyRoleRevisionCount += 1
            draftChanged()
            return {
                ok: true,
                contract: "agent-assignment-story-role-revision-v1",
                assetId: requestedAssetId,
                previousRole: previousRole,
                role: requestedRole,
                swappedAssetId: swappedAssetId,
                fullBatchUntouched: true,
                sourceOrderPreserved: true,
                durableMemoryWritten: false
            }
        }
        function reviseDraftAtPoint(normalizedX, normalizedY, feedback, assetId) {
            harness.lastPointX = normalizedX
            harness.lastPointY = normalizedY
            harness.lastPointFeedback = feedback
            harness.lastPointAssetId = assetId
            harness.showPointGroundedCorrection = feedback.length > 0 &&
                    normalizedX >= 0 && normalizedX <= 1 &&
                    normalizedY >= 0 && normalizedY <= 1
            draftChanged()
            return {
                ok: harness.showPointGroundedCorrection,
                contract: "agent-point-grounded-correction-v2",
                pointGroundedCorrection: lastRunReport.pointGroundedCorrection
            }
        }
        function prepareRejectedRetouchRepairAtPoint(normalizedX, normalizedY, assetId) {
            harness.lastPointX = normalizedX
            harness.lastPointY = normalizedY
            harness.lastPointAssetId = assetId
            harness.showProofRejection = false
            harness.showProofRepairApproval = true
            pendingAgentInputChanged()
            return lastRunReport
        }
        function setDraftFindingProtected(finding, protectedIntent) {
            if (String((finding || ({})).maskId || "") !== "agent-localized-window-proof") {
                return {
                    ok: false,
                    failureReason: "The selected change no longer maps to a mask in this draft."
                }
            }
            harness.showIntentProtected = Boolean(protectedIntent)
            draftChanged()
            return {
                ok: true,
                contract: "agent-draft-intent-boundary-v1",
                maskId: "agent-localized-window-proof",
                targetLabel: "Upper right window",
                protected: harness.showIntentProtected,
                draftActive: true,
                draftRecipeChanged: false,
                intentBoundary: lastRunReport.intentBoundary,
                protectedDraftIntents: lastRunReport.protectedDraftIntents
            }
        }
        function commitDraft(confirmed) { return { ok: confirmed, assetIds: ["asset-1", "asset-2"] } }
        function rollbackDraft() { return { ok: true } }
        function startDraftForScope(scope, instructions) {
            return {
                ok: true,
                accepted: true,
                contract: "agent-async-draft-run-v2",
                requestId: "smoke-request-1",
                scope: scope,
                instructions: instructions
            }
        }
        function cancelRun(reason) { return { ok: true } }
        function dismissPendingAgentInput() {
            harness.showClarification = false
            harness.showDestructiveApproval = false
            harness.showProofRepairApproval = false
            return { ok: true }
        }
        function answerPendingUserInput(answer) {
            if (answer.length <= 0)
                return { ok: false, errors: ["Answer text is required."] }
            harness.showClarification = false
            harness.showDestructiveApproval = false
            harness.showProofRepairApproval = false
            return { ok: true }
        }
        function memoryVaultStatus() {
            return {
                ok: true,
                totalNoteCount: 7,
                externalChangeDetected: false,
                externalChangeCount: 0
            }
        }
        function openMemoryVault() { return { ok: true, rootPath: "/local/agent-memory" } }
        function openMemoryVaultInObsidian(dryRun) { return { ok: !dryRun } }
        function exportPhotographerTastePassportFile(path) {
            return {
                ok: String(path || "").length > 0,
                exported: true,
                contract: "agent-photographer-taste-passport-export-v1",
                caseCount: photographerCalibrationProfile.caseCount,
                storesImageBytes: false,
                storesSourcePaths: false
            }
        }
        function inspectPhotographerTastePassportFile(path) {
            const ok = String(path || "").length > 0
            return {
                ok: ok,
                verified: ok,
                contract: "agent-photographer-taste-passport-preview-v1",
                passportDigestSha256: "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
                caseCount: photographerCalibrationProfile.caseCount,
                reviewer: "Fixture Photographer",
                summary: photographerCalibrationProfile.summary,
                wouldReplaceExisting: photographerCalibrationProfile.available,
                profile: photographerCalibrationProfile,
                durableMemoryWritten: false,
                activeDraftUnaffected: true
            }
        }
        function importPhotographerTastePassportFile(path, expectedDigest) {
            const ok = String(path || "").length > 0 &&
                       String(expectedDigest || "") ===
                       "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
            if (ok) {
                const updated = Object.assign({}, photographerCalibrationProfile)
                updated.importedFromTastePassport = true
                updated.transferScope = "explicit-project-only"
                photographerCalibrationProfile = updated
            }
            return {
                ok: ok,
                imported: ok,
                contract: "agent-photographer-taste-passport-import-v1",
                previewDigestPinned: true,
                profile: photographerCalibrationProfile,
                storesImageBytes: false,
                storesSourcePaths: false
            }
        }
        function openPhotographerCalibrationReviewPacketFile(path, dryRun) {
            return {
                ok: true,
                opened: !dryRun,
                validated: true,
                contract: "agent-photographer-calibration-review-launch-v1",
                reviewExperienceContract: "agent-blind-light-table-v1",
                packetDigestSha256: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                caseCount: 9,
                proofHashesVerified: 18,
                requiresPhotographerJudgment: true,
                machineCanCompleteRatings: false
            }
        }
        function finalizePhotographerCalibrationReviewFiles(ratingsPath, packetPath) {
            harness.lastCalibrationRatingsPath = String(ratingsPath || "")
            harness.lastCalibrationPacketPath = String(packetPath || "")
            harness.calibrationFinalizeCount += 1
            return {
                ok: String(ratingsPath || "").length > 0 &&
                    String(packetPath || "").length > 0,
                finalized: true,
                profileImported: true,
                contract: "agent-photographer-calibration-native-finalize-v1",
                caseCount: 9,
                humanPassCount: 7,
                humanFailCount: 2,
                sourceManifestOverwritten: false,
                automaticThresholdMutationAllowed: false,
                professionalReadinessClaimed: false
            }
        }
        function finalizePhotographerCalibrationReviewClipboard(packetPath) {
            harness.lastCalibrationRatingsPath = "clipboard"
            harness.lastCalibrationPacketPath = String(packetPath || "")
            harness.calibrationFinalizeCount += 1
            harness.calibrationClipboardFinalizeCount += 1
            return {
                ok: String(packetPath || "").length > 0,
                finalized: true,
                profileImported: true,
                contract: "agent-photographer-calibration-native-finalize-v1",
                handoffContract: "agent-photographer-calibration-clipboard-handoff-v1",
                caseCount: 9,
                humanPassCount: 7,
)qml") + QByteArray(R"qml(
                humanFailCount: 2,
                ratingsFileRequired: false,
                clipboardCleared: true,
                sourceManifestOverwritten: false,
                automaticThresholdMutationAllowed: false,
                professionalReadinessClaimed: false
            }
        }
        function exportMemoryVault(destination, redacted) {
            return { ok: redacted, exportPath: "/local/export/agent-memory-redacted" }
        }
        function renameProjectTaste(name) {
            const normalized = String(name || "").trim()
            if (normalized.length < 2)
                return { ok: false, failureReason: "Use a longer project look name." }
            harness.lastTasteRename = normalized
            const next = Object.assign({}, approvedProjectTaste)
            next.name = normalized
            approvedProjectTaste = next
            projectTasteChanged()
            return {
                ok: true,
                renamed: true,
                projectLocal: true,
                activeDraftUnaffected: true
            }
        }
        function saveProjectNorthStar(brief) {
            const normalized = String(brief || "").trim()
            if (normalized.length < 12 || normalized.length > 360)
                return { ok: false, failureReason: "Use 12 to 360 characters." }
            harness.lastNorthStarSave = normalized
            const next = Object.assign({}, approvedProjectTaste)
            next.projectNorthStarAvailable = true
            next.projectNorthStar = {
                contract: "agent-project-north-star-memory-v1",
                id: "north-star-smoke",
                projectKey: "Wedding Delivery",
                brief: normalized,
                authority: "qualitative-project-planning-brief-only",
                precedence: "current-request-then-style-and-safety-then-north-star",
                guidesFuturePlanning: true,
                requiresFreshDraft: true,
                activeDraftUnaffected: true,
                automaticRecipeInfluence: false,
                automaticCullInfluence: false,
                storesImageBytes: false,
                storesSourcePaths: false,
                digestSha256: "0f7c19c7b63ba28a60cbf64341dcf0ae0c459460ec0426641d0077b686922939"
            }
            approvedProjectTaste = next
            const nextReflection = Object.assign({}, assignmentTasteReflection)
            nextReflection.available = false
            nextReflection.unavailableReason = "north-star-already-authored"
            assignmentTasteReflection = nextReflection
            projectTasteChanged()
            return {
                ok: true,
                saved: true,
                projectLocal: true,
                activeDraftUnaffected: true,
                currentRequestWins: true
            }
        }
        function forgetProjectNorthStar(confirmed) {
            if (!confirmed)
                return { ok: false, confirmationRequired: true }
            const next = Object.assign({}, approvedProjectTaste)
            next.projectNorthStarAvailable = false
            next.projectNorthStar = ({})
            approvedProjectTaste = next
            const nextReflection = Object.assign({}, assignmentTasteReflection)
            nextReflection.available = harness.showTwoSpeedTastePreference
            nextReflection.unavailableReason = nextReflection.available
                                               ? "" : "repeated-confirmed-choice-required"
            assignmentTasteReflection = nextReflection
            projectTasteChanged()
            return {
                ok: true,
                forgotten: true,
                activeDraftUnaffected: true
            }
        }
        function forgetProjectTasteEvidence(kind, evidenceId, confirmed) {
            if (!confirmed)
                return { ok: false, confirmationRequired: true }
            const remaining = []
            for (let index = 0; index < approvedProjectTaste.evidence.length; ++index) {
                const item = approvedProjectTaste.evidence[index] || ({})
                if (String(item.kind || "") !== kind || String(item.id || "") !== evidenceId)
                    remaining.push(item)
            }
            const next = Object.assign({}, approvedProjectTaste)
            next.evidence = remaining
            next.evidenceCount = remaining.length
            approvedProjectTaste = next
            projectTasteChanged()
            return { ok: true, forgotten: true, activeDraftUnaffected: true }
        }
        function confirmGroundedSelectionCandidate(candidateId) {
            if (candidateId !== "candidate-left-window" &&
                    candidateId !== "candidate-upper-right-window")
                return { ok: false }
            harness.showSelectionConfirmation = false
            return { ok: true, confirmedCandidateId: candidateId }
        }
        function fixReviewIssue(assetId) { return { ok: assetId.length > 0 } }
        function acceptReviewIssue(assetId) { return { ok: assetId.length > 0 } }
        function rollbackReviewIssue(assetId) { return { ok: assetId.length > 0 } }
        function fixAllReviewIssues() { return { ok: true, fixedCount: 2 } }
    }

    Timer {
        id: directionComparisonReadyTimer
        interval: 40
        repeat: false
        onTriggered: {
            harness.directionComparison = {
                contract: "agent-draft-direction-comparison-v1",
                comparisonOrigin: harness.calibratedComparisonRequested
                                  ? "photographer-calibration"
                                  : "user-authored-alternative",
                photographerTasteCheck: harness.calibratedComparisonRequested
                                         ? fakeController.photographerTasteCheck()
                                         : ({}),
                state: "ready",
                projectKey: "Wedding Delivery",
                projectLocal: true,
                reversible: true,
                actualRenderedProofs: true,
                durableMemoryWritten: false,
                choiceRequired: true,
                chosenKey: "",
                activeDraftKey: "b",
                alternativeDirection: harness.calibratedComparisonRequested
                                      ? "Keep the same framing, masks, and retouching, but use more restrained natural warmth."
                                      : "Cooler, quieter, and more editorial.",
                directions: [
                    {
                        key: "a",
                        label: "Direction A",
                        direction: "Warm, luminous, and natural.",
                        summary: "Warm skin and protected dress highlights.",
                        afterProofPath: aiEditBeforeProofPath,
                        actualRenderedProof: true,
                        validationPassed: true
                    },
                    {
                        key: "b",
                        label: "Direction B",
                        direction: "Cooler, quieter, and more editorial.",
                        summary: "Quieter contrast with a cooler editorial palette.",
                        afterProofPath: aiEditAfterProofPath,
                        actualRenderedProof: true,
                        validationPassed: true
                    }
                ]
            }
            harness.comparisonPreparing = false
            fakeController.draftChanged()
        }
    }

    QtObject {
        id: fakeCatalog
        property int assetCount: 20
        property int totalAssetCount: 20
        property int selectedAssetCount: 20
        property int selectedIndex: 0
        property string selectedAssetId: "asset-1"
        property string selectedThumbnailSource: aiEditBeforeProofPath
        property string projectFilter: "Wedding Delivery"
        property var selectedAssetIds: {
            const ids = []
            for (let index = 1; index <= 20; ++index)
                ids.push("asset-" + index)
            return ids
        }
        function assetAt(index) {
            const editedExample = index === 5
            return {
                assetId: "asset-" + (index + 1),
                filePath: "/proof/WEDDING_" + (index + 1) + ".jpg",
                thumbnailSource: aiEditAfterProofPath,
                picked: true,
                rating: 4,
                virtualCopy: editedExample,
                sourceAssetId: editedExample ? "asset-5" : "",
                variantName: editedExample ? "Warm finished edit" : "Original"
            }
        }
        function assetById(assetId) {
            const index = Math.max(0, Number(String(assetId).split("-")[1] || 1) - 1)
            return assetAt(index)
        }
        function assetsForProject(project) {
            const assets = []
            for (let index = 0; index < assetCount; ++index)
                assets.push(assetAt(index))
            return assets
        }
        function assetCountForProject(project) { return assetCount }
    }

    SharedMaskOverlay {
        objectNamePrefix: "aiEdit"
        anchors.fill: parent
        visible: harness.showAgentWorkspaceReview &&
                 (harness.showSelectionConfirmation ||
                  harness.workspaceSpatialTargetMode ||
                  workspaceReview.spatialTargetChosen ||
                  harness.showPointGroundedCorrection)
        masked: false
        hasActiveMask: false
        compareMode: false
        activeBrushPainting: false
        externalCandidateRegions: harness.showSelectionConfirmation
                                  ? fakeController.lastRunReport.selectionConfirmation.candidates
                                  : []
        agentPointTargetingEnabled: harness.workspaceSpatialTargetMode
        agentPointTargetRegion: workspaceReview.spatialTargetChosen
                                ? ({
)qml") + QByteArray(R"qml(
                                       centerX: workspaceReview.spatialTargetPoint.x,
                                       centerY: workspaceReview.spatialTargetPoint.y,
                                       radiusX: 0.12,
                                       radiusY: 0.12,
                                       feather: 0.72
                                   })
                                : ({})
        accentColor: "#ff645c"
        onAgentPointTargeted: function(normalizedX, normalizedY) {
            harness.workspaceSpatialTargetMode = false
            workspaceReview.acceptSpatialTarget(
                        normalizedX, normalizedY, fakeCatalog.selectedAssetId)
        }
        z: 2
    }

    AgentWalkthroughDialog {
        id: dialog
        objectName: "aiEditWalkthroughDialog"
        anchors.centerIn: parent
        agentEditController: fakeController
        catalogModel: fakeCatalog
        initialScopeMode: "selected-assets"
        proofImages: [
            { label: "Before", source: "file://" + aiEditBeforeProofPath },
            { label: "After", source: "file://" + aiEditAfterProofPath }
        ]
        onScopeModeRequested: function(scopeMode) { harness.lastRequestedScope = scopeMode }
        onRunRequested: {
            if (fakeController.workflowPrimaryKey === "set_up_local_ai")
                harness.setupRequestCount += 1
        }
        onEditApplied: function(assetIds) {}
    }

    AgentWorkspaceOverlay {
        id: workspaceReview
        objectName: "aiEditAgentWorkspaceReview"
        anchors.fill: parent
        visible: harness.showAgentWorkspaceReview
        agentEditController: fakeController
        catalogModel: fakeCatalog
        targetAssetIds: ["asset-1"]
        prompt: "Reduce only the bright window. Preserve the subject, color, and framing."
        inspectorWidth: 340
        filmstripHeight: 88
        reducedMotion: true
        spatialTargetMode: harness.workspaceSpatialTargetMode
        onSpatialTargetModeRequested: function(enabled) {
            harness.workspaceSpatialTargetMode = enabled
        }
        onReopenRequested: {
            harness.showAgentWorkspaceReview = false
            dialog.open()
            dialog.setPage(2)
        }
        z: 3
    }

    Timer {
        id: initialReviewTimer
        objectName: "aiEditInitialReviewTimer"
        interval: 120
        running: true
        repeat: false
        onTriggered: dialog.setPage(3)
    }

    Button {
        id: focusReturnProbe
        objectName: "aiEditFocusReturnProbe"
        x: 12
        y: 12
        visible: harness.showFocusReturnProbe
        activeFocusOnTab: true
        text: "Walkthrough opener"
    }
}
)qml");
    ai_edit_component.setData(
        ai_edit_harness_qml,
        QUrl::fromLocalFile(QDir(qml_root).filePath(QStringLiteral("AiEditVisualHarness.qml"))));
    QScopedPointer<QObject> ai_edit_harness(ai_edit_component.create());
    if (ai_edit_harness.isNull()) {
      const auto errors = ai_edit_component.errors();
      for (const auto& error : errors) {
        std::cerr << error.toString().toStdString() << '\n';
      }
      return fail("AI Edit visual proof should instantiate the real walkthrough component");
    }
    auto* ai_edit_window = qobject_cast<QQuickWindow*>(ai_edit_harness.data());
    if (ai_edit_window == nullptr ||
        !wait_for_condition(
            [&ai_edit_harness]() {
              auto* dialog = find_named_object(ai_edit_harness.data(), QStringLiteral("aiEditWalkthroughDialog"));
              return dialog != nullptr && dialog->property("visible").toBool() &&
                     dialog->property("currentPage").toInt() == 3;
            },
            2500)) {
      return fail("AI Edit visual proof should reach the real sample review page");
    }
    auto* ai_edit_dialog =
        find_named_object(ai_edit_harness.data(), QStringLiteral("aiEditWalkthroughDialog"));
    QAccessible::setActive(true);
    auto* ai_edit_accessible = QAccessible::queryAccessibleInterface(ai_edit_dialog);
    if (ai_edit_accessible == nullptr || ai_edit_accessible->role() != QAccessible::Dialog ||
        accessible_name_for_object(ai_edit_dialog) != QStringLiteral("Looksy Agent Studio")) {
      return fail("AI Edit walkthrough should expose a named dialog to assistive technology");
    }
    auto* ai_edit_dialog_content =
        ai_edit_dialog == nullptr
            ? nullptr
            : qvariant_cast<QObject*>(ai_edit_dialog->property("contentItem"));
    auto* ai_edit_visual_root =
        ai_edit_window->contentItem() != nullptr
            ? static_cast<QObject*>(ai_edit_window->contentItem())
            : (ai_edit_dialog_content != nullptr
                   ? ai_edit_dialog_content
                   : ai_edit_harness.data());
    auto* return_to_editor = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentReturnToEditorButton"));
    if (return_to_editor == nullptr ||
        !wait_for_condition(
            [ai_edit_window, return_to_editor]() {
              return ai_edit_window->activeFocusItem() == qobject_cast<QQuickItem*>(return_to_editor) &&
                     return_to_editor->property("visualFocus").toBool();
            },
            1000)) {
      return fail("AI Edit review entry should put visible keyboard focus on its safe return action");
    }
    const auto initial_selection_fingerprint =
        ai_edit_harness->property("librarySelectionFingerprint").toString();
    auto* initial_review_timer = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditInitialReviewTimer"));
    if (initial_review_timer == nullptr ||
        !QMetaObject::invokeMethod(initial_review_timer, "stop")) {
      return fail("AI Edit visual proof should settle and stop its one-shot initial review timer");
    }
    if (!QMetaObject::invokeMethod(
            ai_edit_dialog,
            "setPage",
            Qt::DirectConnection,
            Q_ARG(QVariant, 0))) {
      return fail("AI Edit visual proof should return to the prompt for scope selection");
    }
    auto* ai_edit_conversation_view = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentConversationView"));
    if (ai_edit_conversation_view == nullptr ||
        !wait_for_condition(
            [ai_edit_conversation_view]() {
              return ai_edit_conversation_view->property("opacity").toDouble() >= 0.99 &&
                     ai_edit_conversation_view->property("scale").toDouble() >= 0.99;
            },
            1500)) {
      return fail("AI Edit compact entry should settle its page-replacement animation before visual proof");
    }
    auto* compact_prompt = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentPromptEditor"));
    auto* compact_primary = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentPrimaryCreateDraftButton"));
    auto* details_toggle = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentDetailsToggle"));
    auto* collapsed_details_panel = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentDetailsPanel"));
    auto* batch_review_forecast = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentScopeRiskMessage"));
    auto* style_start_card = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentStyleStartCard"));
    auto* learn_from_edits = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentLearnLookFromEditsButton"));
    auto* describe_look = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentDescribeLookButton"));
    auto* built_in_looks = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentBuiltInLooksButton"));
    auto* current_project_look_label = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentCurrentProjectLookLabel"));
    auto* editing_freedom_card = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentEditingFreedomCard"));
    auto* editing_freedom_summary = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentEditingFreedomSummary"));
    auto* editing_freedom_protect = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentEditingFreedomProtect"));
    auto* editing_freedom_balanced = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentEditingFreedomBalanced"));
    auto* editing_freedom_transform = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentEditingFreedomTransform"));
    if (compact_prompt == nullptr || compact_primary == nullptr || details_toggle == nullptr ||
        collapsed_details_panel == nullptr ||
        batch_review_forecast == nullptr ||
        style_start_card == nullptr || learn_from_edits == nullptr ||
        describe_look == nullptr || built_in_looks == nullptr ||
        current_project_look_label == nullptr || editing_freedom_card == nullptr ||
        editing_freedom_summary == nullptr || editing_freedom_protect == nullptr ||
        editing_freedom_balanced == nullptr || editing_freedom_transform == nullptr ||
        !compact_prompt->property("visible").toBool() ||
        !compact_primary->property("visible").toBool() ||
        !style_start_card->property("visible").toBool() ||
        !learn_from_edits->property("visible").toBool() ||
        !describe_look->property("visible").toBool() ||
        !built_in_looks->property("visible").toBool() ||
        !editing_freedom_card->property("visible").toBool() ||
        !editing_freedom_protect->property("enabled").toBool() ||
        !editing_freedom_balanced->property("selected").toBool() ||
        editing_freedom_transform->property("selected").toBool() ||
        !editing_freedom_summary->property("text").toString().contains(
            QStringLiteral("project look and local adjustments")) ||
        !batch_review_forecast->property("text").toString().contains(
            QStringLiteral("18 likely ready · about 2 may need your eye · you’ll approve samples first")) ||
        compact_primary->property("text").toString() !=
            QStringLiteral("Create Samples") ||
        !compact_primary->property("enabled").toBool() ||
        ai_edit_dialog->property("detailsExpanded").toBool() ||
        collapsed_details_panel->property("visible").toBool()) {
      return fail("AI Edit should open with one compact direction surface and three direct ways to establish the project look");
    }
    if (!wait_for_condition(
            [ai_edit_window, compact_prompt]() {
              const auto* prompt_item = qobject_cast<QQuickItem*>(compact_prompt);
              return prompt_item != nullptr && ai_edit_window->activeFocusItem() == prompt_item &&
                     prompt_item->hasActiveFocus();
            },
            1000) ||
        accessible_name_for_object(compact_prompt) != QStringLiteral("AI edit instructions")) {
      auto* active_focus = ai_edit_window->activeFocusItem();
      std::cerr << "AI Edit prompt focus state: active="
                << (active_focus ? active_focus->objectName().toStdString() : "<none>")
                << " prompt=" << compact_prompt->objectName().toStdString()
                << " visual=" << compact_prompt->property("visualFocus").toBool()
                << " accessible-name=" << accessible_name_for_object(compact_prompt).toStdString()
                << '\n';
      return fail("AI Edit prompt entry should receive visible keyboard focus and a clear accessible name");
    }
    // The interactive area must sit on real alpha artwork, without outer chrome.
    auto* robot_artwork = find_named_object(ai_edit_visual_root, QStringLiteral("agentRobotScreenArtwork"));
    auto* robot_stage = qobject_cast<QQuickItem*>(
        find_named_object(ai_edit_visual_root, QStringLiteral("agentRobotStage")));
    auto* held_screen = qobject_cast<QQuickItem*>(
        find_named_object(ai_edit_visual_root, QStringLiteral("agentHeldScreen")));
    auto* held_scroll = find_named_object(ai_edit_visual_root, QStringLiteral("agentHeldScreenScroll"));
    const QImage robot_image(QStringLiteral(":/photo-editor/agent/looksy-agent-screen-v2.png"));
    if (robot_artwork == nullptr || robot_stage == nullptr || held_screen == nullptr ||
        held_scroll == nullptr || robot_image.isNull() || !robot_image.hasAlphaChannel() ||
        robot_image.width() < 1200 || robot_image.pixelColor(0, 0).alpha() != 0 ||
        robot_image.pixelColor(robot_image.width() / 2, robot_image.height() / 2).alpha() < 240 ||
        !robot_stage->boundingRect().contains(held_screen->mapRectToItem(robot_stage, held_screen->boundingRect()))) {
      return fail("AI Edit should put live controls inside a high resolution robot's held screen with transparent outer artwork");
    }
    const auto prompt_on_screen = qobject_cast<QQuickItem*>(compact_prompt)->mapRectToItem(
        held_screen, qobject_cast<QQuickItem*>(compact_prompt)->boundingRect());
    if (!held_screen->boundingRect().contains(prompt_on_screen)) {
      return fail("AI Edit should scroll its focused prompt fully into the held screen");
    }

    auto* change_prompt = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentChangePromptButton"));
    if (change_prompt == nullptr ||
        !ai_edit_dialog->property("inspirationSuggestions").toList().isEmpty()) {
      return fail("AI Edit inspiration focus fixture should start with an empty suggestion list");
    }
    const QVariantList populated_inspiration_suggestions{
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("preset")},
                    {QStringLiteral("id"), QStringLiteral("focus-proof")},
                    {QStringLiteral("title"), QStringLiteral("Focus proof look")},
                    {QStringLiteral("detail"), QStringLiteral("Visible suggestion used to verify focus.")}}};
    ai_edit_dialog->setProperty("inspirationSuggestions", populated_inspiration_suggestions);
    QObject* first_inspiration_card = nullptr;
    if (!QMetaObject::invokeMethod(
            ai_edit_dialog,
            "setPage",
            Qt::DirectConnection,
            Q_ARG(QVariant, 1)) ||
        !wait_for_condition(
            [&]() {
              first_inspiration_card = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("agentBuiltInLookCard-focus-proof"));
              auto* card_item = qobject_cast<QQuickItem*>(first_inspiration_card);
              return card_item != nullptr && item_visible(card_item) &&
                     ai_edit_window->activeFocusItem() == card_item && card_item->hasActiveFocus();
            },
            1000) ||
        accessible_name_for_object(first_inspiration_card) !=
            QStringLiteral("Focus proof look")) {
      auto* active_focus = ai_edit_window->activeFocusItem();
      std::cerr << "AI Edit populated suggestion focus debug: active="
                << (active_focus ? active_focus->objectName().toStdString() : "<none>")
                << " card=" << (first_inspiration_card
                                    ? first_inspiration_card->objectName().toStdString()
                                    : "<missing>")
                << " suggestions=" << ai_edit_dialog->property("inspirationSuggestions").toList().size()
                << '\n';
      return fail("AI Edit inspiration entry should focus the first visible suggestion card");
    }
    if (!QMetaObject::invokeMethod(
            ai_edit_dialog,
            "setPage",
            Qt::DirectConnection,
            Q_ARG(QVariant, 0)) ||
        !wait_for_condition(
            [ai_edit_window, compact_prompt]() {
              const auto* prompt_item = qobject_cast<QQuickItem*>(compact_prompt);
              return prompt_item != nullptr && ai_edit_window->activeFocusItem() == prompt_item &&
                     prompt_item->hasActiveFocus();
            },
            1000)) {
      return fail("AI Edit prompt focus should return after leaving the populated inspiration page");
    }
    ai_edit_dialog->setProperty("inspirationSuggestions", QVariantList{});
    if (!QMetaObject::invokeMethod(
            ai_edit_dialog,
            "setPage",
            Qt::DirectConnection,
            Q_ARG(QVariant, 1)) ||
        !wait_for_condition(
            [ai_edit_window, change_prompt]() {
              return ai_edit_window->activeFocusItem() == qobject_cast<QQuickItem*>(change_prompt) &&
                     change_prompt->property("visualFocus").toBool();
            },
            1000) ||
        !QMetaObject::invokeMethod(
            ai_edit_dialog,
            "setPage",
            Qt::DirectConnection,
            Q_ARG(QVariant, 0)) ||
        !wait_for_condition(
            [ai_edit_window, compact_prompt]() {
              const auto* prompt_item = qobject_cast<QQuickItem*>(compact_prompt);
              return prompt_item != nullptr && ai_edit_window->activeFocusItem() == prompt_item &&
                     prompt_item->hasActiveFocus();
            },
            1000)) {
      auto* active_focus = ai_edit_window->activeFocusItem();
      std::cerr << "AI Edit page-entry focus state: page="
                << ai_edit_dialog->property("currentPage").toInt()
                << " active=" << (active_focus ? active_focus->objectName().toStdString() : "<none>")
                << " change-prompt=" << (change_prompt ? change_prompt->property("visualFocus").toBool() : false)
                << " prompt=" << compact_prompt->property("visualFocus").toBool() << '\n';
      return fail("AI Edit inspiration page should focus its prompt-return action when the gallery is empty");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("ai-edit-compact-entry"),
            1440,
            900,
            {
                QStringLiteral("agentPromptEditor"),
                QStringLiteral("agentStyleStartCard"),
                QStringLiteral("agentLearnLookFromEditsButton"),
                QStringLiteral("agentDescribeLookButton"),
                QStringLiteral("agentBuiltInLooksButton"),
                QStringLiteral("agentEditingFreedomCard"),
                QStringLiteral("agentEditingFreedomBalanced"),
                QStringLiteral("agentScopeRiskMessage"),
                QStringLiteral("agentDetailsToggle"),
                QStringLiteral("agentPrimaryCreateDraftButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-three-path-style-start-v1")},
                {QStringLiteral("targetResolvedBeforePrompt"), true},
                {QStringLiteral("projectStyleRestored"), true},
                {QStringLiteral("styleStartChoices"), 3},
                {QStringLiteral("editingFreedom"), QStringLiteral("balanced")},
                {QStringLiteral("editingFreedomUserSelectable"), true},
                {QStringLiteral("forecastAutomaticCount"), 18},
                {QStringLiteral("forecastReviewCount"), 2},
                {QStringLiteral("forecastEvidencePhotoCount"), 120},
                {QStringLiteral("primaryAction"), QStringLiteral("Create Samples")},
                {QStringLiteral("detailsCollapsedByDefault"), true},
                {QStringLiteral("technicalProcessHiddenByDefault"), true},
                {QStringLiteral("outcomeFirstLanguage"), true},
                {QStringLiteral("inspirationOptional"), true},
                {QStringLiteral("stateDrivenPrimaryActions"), true},
    })) {
      return fail("AI Edit visual proof should capture the compact default surface");
    }
    if (!QMetaObject::invokeMethod(editing_freedom_transform, "click") ||
        !wait_for_condition(
            [ai_edit_dialog, editing_freedom_summary,
             editing_freedom_transform]() {
              auto* controller = qvariant_cast<QObject*>(
                  ai_edit_dialog->property("agentEditController"));
              return controller != nullptr &&
                     controller->property("editingFreedom").toString() ==
                         QStringLiteral("transform") &&
                     editing_freedom_transform->property("selected").toBool() &&
                     editing_freedom_summary->property("text").toString().contains(
                         QStringLiteral("Bolder color, crop, and local work"));
            },
            1000)) {
      return fail("AI Edit should let a normal user choose a materially broader editing envelope in one click");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("ai-edit-editing-freedom-transform"),
            1440,
            900,
            {
                QStringLiteral("agentEditingFreedomCard"),
                QStringLiteral("agentEditingFreedomProtect"),
                QStringLiteral("agentEditingFreedomBalanced"),
                QStringLiteral("agentEditingFreedomTransform"),
                QStringLiteral("agentEditingFreedomSummary"),
                QStringLiteral("agentPrimaryCreateDraftButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-editing-freedom-v1")},
                {QStringLiteral("mode"), QStringLiteral("transform")},
                {QStringLiteral("cropAndLocalWorkAllowed"), true},
                {QStringLiteral("retouchApprovalRequired"), true},
                {QStringLiteral("destructiveApprovalRequired"), true},
                {QStringLiteral("oneClickChoice"), true},
            })) {
      return fail("AI Edit visual proof should capture the user-selected Transform envelope");
    }
    if (!QMetaObject::invokeMethod(editing_freedom_balanced, "click") ||
        !wait_for_condition(
            [ai_edit_dialog]() {
              auto* controller = qvariant_cast<QObject*>(
                  ai_edit_dialog->property("agentEditController"));
              return controller != nullptr &&
                     controller->property("editingFreedom").toString() ==
                         QStringLiteral("balanced");
            },
            1000)) {
      return fail("AI Edit editing-freedom proof should restore the balanced default");
    }
    auto* fake_catalog = qvariant_cast<QObject*>(
        ai_edit_dialog->property("catalogModel"));
    if (fake_catalog == nullptr) {
      return fail("AI Edit scope inference proof should resolve its catalog model");
    }
    fake_catalog->setProperty("totalAssetCount", 40);
    fake_catalog->setProperty("selectedAssetCount", 0);
    fake_catalog->setProperty("selectedIndex", -1);
    fake_catalog->setProperty("selectedAssetId", QString());
    fake_catalog->setProperty("selectedAssetIds", QStringList{});
    fake_catalog->setProperty("projectFilter", QStringLiteral("all"));
    QVariant inferred_filtered_scope;
    QVariant filtered_target_ids;
    QVariant filtered_target_summary;
    if (!QMetaObject::invokeMethod(
            ai_edit_dialog,
            "bestScope",
            Qt::DirectConnection,
            Q_RETURN_ARG(QVariant, inferred_filtered_scope),
            Q_ARG(QVariant, QStringLiteral("filtered-library"))) ||
        inferred_filtered_scope.toString() != QStringLiteral("filtered-library") ||
        !QMetaObject::invokeMethod(
            ai_edit_dialog,
            "chooseScope",
            Qt::DirectConnection,
            Q_ARG(QVariant, QStringLiteral("filtered-library"))) ||
        !QMetaObject::invokeMethod(
            ai_edit_dialog,
            "targetAssetIds",
            Qt::DirectConnection,
            Q_RETURN_ARG(QVariant, filtered_target_ids)) ||
        !QMetaObject::invokeMethod(
            ai_edit_dialog,
            "targetSummary",
            Qt::DirectConnection,
            Q_RETURN_ARG(QVariant, filtered_target_summary)) ||
        filtered_target_ids.toList().size() != 20 ||
        filtered_target_summary.toString() != QStringLiteral("20 filtered photos")) {
      return fail("AI Edit should infer a filtered-library scope and target every visible filtered photo instead of an empty selection");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("ai-edit-filtered-scope"),
            1440,
            900,
            {
                QStringLiteral("agentTargetSummary"),
                QStringLiteral("agentPromptEditor"),
                QStringLiteral("agentScopeRiskMessage"),
                QStringLiteral("agentDetailsToggle"),
                QStringLiteral("agentPrimaryCreateDraftButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-filtered-scope-inference-v1")},
                {QStringLiteral("scope"), QStringLiteral("filtered-library")},
                {QStringLiteral("visiblePhotoCount"), 20},
                {QStringLiteral("totalLibraryPhotoCount"), 40},
                {QStringLiteral("emptySelectionFallbackPrevented"), true},
                {QStringLiteral("detailsCollapsedByDefault"), true},
            })) {
      return fail("AI Edit visual proof should capture the inferred filtered-photo scope");
    }
    QStringList restored_selected_ids;
    for (auto index = 1; index <= 20; ++index)
      restored_selected_ids.append(QStringLiteral("asset-%1").arg(index));
    fake_catalog->setProperty("totalAssetCount", 20);
    fake_catalog->setProperty("selectedAssetCount", 20);
    fake_catalog->setProperty("selectedIndex", 0);
    fake_catalog->setProperty("selectedAssetId", QStringLiteral("asset-1"));
    fake_catalog->setProperty("selectedAssetIds", restored_selected_ids);
    fake_catalog->setProperty("projectFilter", QStringLiteral("Wedding Delivery"));
    if (!QMetaObject::invokeMethod(
            ai_edit_dialog,
            "chooseScope",
            Qt::DirectConnection,
            Q_ARG(QVariant, QStringLiteral("selected-assets")))) {
      return fail("AI Edit scope inference proof should restore the selected-photo batch");
    }
    if (!QMetaObject::invokeMethod(
            ai_edit_dialog,
            "chooseScope",
            Qt::DirectConnection,
            Q_ARG(QVariant, QStringLiteral("current-photo"))) ||
        !QMetaObject::invokeMethod(
            learn_from_edits,
            "clicked",
            Qt::DirectConnection) ||
        !wait_for_condition(
            [ai_edit_dialog]() {
              return ai_edit_dialog->property("currentPage").toInt() == 1 &&
                     ai_edit_dialog->property("styleStartMode").toString() ==
                         QStringLiteral("learn");
            },
            1000)) {
      return fail("Learn from edits should open the edited-photo-only project-look picker");
    }
    auto* edited_reference_card = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentStyleReferenceCard-asset-6"));
    auto* style_reference_preflight = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentStyleReferencePreflight"));
    if (edited_reference_card == nullptr || style_reference_preflight == nullptr ||
        !QMetaObject::invokeMethod(
            edited_reference_card,
            "chosen",
            Qt::DirectConnection) ||
        !wait_for_condition(
            [ai_edit_dialog, style_reference_preflight]() {
              return ai_edit_dialog->property("inspirationAssetIds")
                         .toStringList() ==
                         QStringList{QStringLiteral("asset-6")} &&
                     style_reference_preflight->property("text").toString().contains(
                         QStringLiteral("1 edited reference photo · ready to learn"));
            },
            1000)) {
      return fail("choosing an edited look example should synchronize the exact reference id into a clean style-learning preflight");
    }
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-edited-reference-preflight"),
            1440,
            900,
            {
                QStringLiteral("agentStyleReferenceCard-asset-6"),
                QStringLiteral("agentStyleReferencePreflight"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-style-reference-preflight-v1")},
                {QStringLiteral("editedReferenceCount"), 1},
                {QStringLiteral("referenceIdsSynchronized"), true},
                {QStringLiteral("unrelatedOriginalCount"), 0},
                {QStringLiteral("warningRequired"), false},
                {QStringLiteral("projectLocal"), true},
            })) {
      return fail("AI Edit visual proof should capture the clean edited-reference preflight before learning");
    }
    if (!QMetaObject::invokeMethod(
            ai_edit_dialog,
            "setPage",
            Qt::DirectConnection,
            Q_ARG(QVariant, 0)) ||
        !QMetaObject::invokeMethod(
            ai_edit_dialog,
            "chooseScope",
            Qt::DirectConnection,
            Q_ARG(QVariant, QStringLiteral("selected-assets")))) {
      return fail("AI Edit visual proof should restore the original batch scope after reference preflight proof");
    }
    process_layout_events(8);
    if (!QMetaObject::invokeMethod(
            describe_look,
            "clicked",
            Qt::DirectConnection) ||
        !wait_for_condition(
            [ai_edit_dialog, current_project_look_label]() {
              return ai_edit_dialog->property("styleStartMessage")
                             .toString()
                             .contains(QStringLiteral("Saved")) &&
                     current_project_look_label->property("text").toString() ==
                         QStringLiteral("Wedding Delivery Look");
            },
            1000)) {
      return fail("AI Edit should turn the visible direction into a project-sticky look in one action");
    }
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-described-project-look"),
            1440,
            900,
            {
                QStringLiteral("agentStyleStartCard"),
                QStringLiteral("agentCurrentProjectLookLabel"),
                QStringLiteral("agentDescribeLookButton"),
                QStringLiteral("agentPrimaryCreateDraftButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-described-style-start-v1")},
                {QStringLiteral("styleName"),
                 QStringLiteral("Wedding Delivery Look")},
                {QStringLiteral("advancedFieldsGenerated"), true},
                {QStringLiteral("rememberedForProject"), true},
            })) {
      return fail("AI Edit visual proof should capture the saved project-look receipt");
    }
    auto* fake_ai_edit_controller = qvariant_cast<QObject*>(
        ai_edit_dialog->property("agentEditController"));
    if (fake_ai_edit_controller == nullptr) {
      return fail("AI Edit state-driven action proof should resolve its controller");
    }
    fake_ai_edit_controller->setProperty(
        "workflowPrimaryKey",
        QStringLiteral("set_up_local_ai"));
    fake_ai_edit_controller->setProperty("userInstructions", QString());
    if (!wait_for_condition(
            [compact_primary]() {
              return compact_primary->property("text").toString() ==
                         QStringLiteral("Set Up Local AI") &&
                     compact_primary->property("enabled").toBool();
            },
            1000) ||
        !QMetaObject::invokeMethod(compact_primary, "clicked", Qt::DirectConnection) ||
        ai_edit_harness->property("setupRequestCount").toInt() != 1) {
      return fail("AI Edit should route setup as the single state-driven action without requiring a prompt");
    }
    fake_ai_edit_controller->setProperty(
        "workflowPrimaryKey",
        QStringLiteral("choose_style"));
    fake_ai_edit_controller->setProperty(
        "userInstructions",
        QStringLiteral("Natural wedding color with protected highlights."));
    if (!wait_for_condition(
            [compact_primary]() {
              return compact_primary->property("text").toString() ==
                         QStringLiteral("Choose Style") &&
                     compact_primary->property("enabled").toBool();
            },
            1000) ||
        !QMetaObject::invokeMethod(compact_primary, "clicked", Qt::DirectConnection) ||
        !wait_for_condition(
            [ai_edit_dialog]() {
              return ai_edit_dialog->property("currentPage").toInt() == 1;
            },
            1000)) {
      return fail("AI Edit should route Choose Style into optional inspiration instead of a dead action");
    }
    fake_ai_edit_controller->setProperty(
        "workflowPrimaryKey",
        QStringLiteral("create_draft"));
    fake_ai_edit_controller->setProperty(
        "userInstructions",
        QStringLiteral("Natural wedding color, warm skin, protect the dress highlights."));
    if (!QMetaObject::invokeMethod(
            ai_edit_dialog,
            "setPage",
            Qt::DirectConnection,
            Q_ARG(QVariant, 0)) ||
        !wait_for_condition(
            [compact_primary]() {
              return compact_primary->property("text").toString() ==
                         QStringLiteral("Create Samples");
            },
            1000)) {
      return fail("AI Edit should restore the outcome-specific Create Samples action for the selected batch");
    }
    if (!QMetaObject::invokeMethod(details_toggle, "clicked", Qt::DirectConnection)) {
      return fail("AI Edit Details should open from the compact surface");
    }
    QObject* scope_selector = nullptr;
    QObject* scope_risk_message = nullptr;
    QObject* details_panel = nullptr;
    QObject* memory_vault_label = nullptr;
    QObject* run_audit_panel = nullptr;
    QObject* retouch_runtime_label = nullptr;
    if (!wait_for_condition(
            [ai_edit_dialog, ai_edit_visual_root, &scope_selector, &scope_risk_message,
             &details_panel, &memory_vault_label, &run_audit_panel,
             &retouch_runtime_label]() {
              scope_selector = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("agentScopeSelector"));
              scope_risk_message = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("agentScopeRiskMessage"));
              details_panel = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("agentDetailsPanel"));
              memory_vault_label = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("agentLiveMemoryVaultLabel"));
              run_audit_panel = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("agentLiveRunAuditPanel"));
              retouch_runtime_label = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("agentDetailsRetouchRuntimeLabel"));
              return ai_edit_dialog->property("detailsExpanded").toBool() &&
                     details_panel != nullptr &&
                     details_panel->property("height").toReal() >= 170.0 &&
                     memory_vault_label != nullptr &&
                     memory_vault_label->property("text").toString().contains(
                         QStringLiteral("7 local notes")) &&
                     run_audit_panel != nullptr &&
                     run_audit_panel->property("visible").toBool() &&
                     retouch_runtime_label != nullptr &&
                     retouch_runtime_label->property("text").toString().contains(
                         QStringLiteral("failed proofs stay unchanged")) &&
                     scope_selector != nullptr && scope_risk_message != nullptr &&
                     scope_selector->property("visible").toBool() &&
                     scope_selector->property("count").toInt() == 3;
            },
            1500)) {
      return fail("AI Edit prompt should expose the compact scope selector");
    }
    auto* ai_edit_harness_object = ai_edit_harness.data();
    scope_selector->setProperty("currentIndex", 2);
    if (!QMetaObject::invokeMethod(
            scope_selector,
            "activated",
            Qt::DirectConnection,
            Q_ARG(int, 2)) ||
        !wait_for_condition(
            [ai_edit_dialog, ai_edit_harness_object, scope_risk_message]() {
              return ai_edit_dialog->property("scopeMode").toString() ==
                         QStringLiteral("project") &&
                     ai_edit_harness_object->property("lastRequestedScope").toString() ==
                         QStringLiteral("project") &&
                     scope_risk_message->property("text").toString().contains(
                         QStringLiteral("you’ll approve samples first"));
            },
            1000)) {
      return fail("AI Edit scope selector should route the chosen project scope and disclose sample-first safety");
    }
    const auto selection_unchanged =
        ai_edit_harness->property("librarySelectionFingerprint").toString() ==
        initial_selection_fingerprint;
    process_layout_events(8);
    if (!selection_unchanged ||
        !capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("ai-edit-scope-override"),
            1440,
            900,
            {
                QStringLiteral("agentScopeSelector"),
                QStringLiteral("agentScopeRiskMessage"),
                QStringLiteral("agentPromptEditor"),
                QStringLiteral("agentLiveMcpStatusLabel"),
                QStringLiteral("agentDetailsRetouchRuntimeLabel"),
                QStringLiteral("agentLiveMemoryVaultLabel"),
                QStringLiteral("agentLiveRunAuditPanel"),
                QStringLiteral("agentOpenMemoryVaultButton"),
                QStringLiteral("agentOpenMemoryVaultInObsidianButton"),
                QStringLiteral("agentExportRedactedMemoryButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-scope-override-v1")},
                {QStringLiteral("availableScopeCount"), 3},
                {QStringLiteral("selectedScope"), QStringLiteral("project")},
                {QStringLiteral("targetCount"), 20},
                {QStringLiteral("librarySelectionMutated"), false},
                {QStringLiteral("representativeSamplesFirst"), true},
                {QStringLiteral("fullBatchRequiresApproval"), true},
                {QStringLiteral("localToolCount"), 12},
                {QStringLiteral("retouchRuntime"),
                 QStringLiteral("proof-gated-cpu-fallback")},
                {QStringLiteral("localMemoryNoteCount"), 7},
                {QStringLiteral("redactedMemoryExportAvailable"), true},
            })) {
      return fail("AI Edit visual proof should capture a safe in-conversation scope override");
    }
    ai_edit_harness->setProperty("showCorruptModelBundle", true);
    if (!wait_for_condition(
            [retouch_runtime_label]() {
              return retouch_runtime_label->property("text").toString().contains(
                  QStringLiteral("model bundle failed verification"));
            },
            1000) ||
        !capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-model-bundle-integrity-rejection"),
            1440,
            900,
            {
                QStringLiteral("agentDetailsPanel"),
                QStringLiteral("agentDetailsRetouchRuntimeLabel"),
                QStringLiteral("agentLiveRunAuditPanel"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-retouch-runtime-readiness-v1")},
                {QStringLiteral("bundleVerified"), false},
                {QStringLiteral("inpaintArtifactVerified"), false},
                {QStringLiteral("bundleVerificationErrorCode"),
                 QStringLiteral("artifact_sha256_mismatch")},
                {QStringLiteral("capabilityTier"),
                 QStringLiteral("core-editing-with-proof-gated-retouch-fallback")},
                {QStringLiteral("fallbackProtectedByProof"), true},
            })) {
      return fail("AI Edit should visibly distinguish a corrupt model bundle from professional cleanup readiness");
    }
    ai_edit_harness->setProperty("showCorruptModelBundle", false);
    scope_selector->setProperty("currentIndex", 1);
    if (!QMetaObject::invokeMethod(
            scope_selector,
            "activated",
            Qt::DirectConnection,
            Q_ARG(int, 1)) ||
        !QMetaObject::invokeMethod(
            ai_edit_dialog,
            "setPage",
            Qt::DirectConnection,
            Q_ARG(QVariant, 3))) {
      return fail("AI Edit visual proof should restore the selected-photo review scope");
    }
    QQuickItem* review_surface = nullptr;
    if (!wait_for_condition(
            [ai_edit_visual_root, &review_surface]() {
              review_surface = find_named_item(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditReviewPageColumn"));
              return review_surface != nullptr;
            },
            2500)) {
      return fail("AI Edit visual proof should expose its review surface");
    }
    // Offscreen Quick does not advance this page-replacement animation reliably.
    // Settle the same final visual state explicitly before proof capture.
    review_surface->setOpacity(1.0);
    review_surface->setX(0.0);
    if (!wait_for_condition(
            [ai_edit_dialog, ai_edit_visual_root]() {
              auto* sample = find_named_item(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditSampleReviewPanel"));
              return ai_edit_dialog->property("currentPage").toInt() == 3 &&
                     sample != nullptr && sample->isVisible() &&
                     sample->height() >= 80.0;
            },
            2500)) {
      auto* sample = find_named_item(
          ai_edit_visual_root,
          QStringLiteral("aiEditSampleReviewPanel"));
      std::cerr << "AI Edit review layout currentPage="
                << ai_edit_dialog->property("currentPage").toInt()
                << " reviewVisible=" << review_surface->isVisible()
                << " sampleVisible=" << (sample != nullptr && sample->isVisible())
                << " sampleHeight=" << (sample != nullptr ? sample->height() : -1.0)
                << '\n';
      return fail("AI Edit visual proof should lay out the sample review before capture");
    }
    process_layout_events(8);
    auto* sample_panel = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditSampleReviewPanel"));
    auto* primary_review = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditPrimaryReviewButton"));
    auto* story_map = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditAssignmentStoryMap"));
    auto* story_arc = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditAssignmentStoryArc"));
    auto* story_receipt = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditAssignmentStoryReceipt"));
    auto* story_role_evidence = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditStoryRoleEvidence"));
    auto* story_role_evidence_title = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditStoryRoleEvidenceTitle"));
    auto* story_role_evidence_reason = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditStoryRoleEvidenceReason"));
    auto* story_role_evidence_boundary = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditStoryRoleEvidenceBoundary"));
    auto* story_previous_sample = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditStoryPreviousSample"));
    auto* story_sample_position = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditStorySamplePosition"));
    auto* story_next_sample = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditStoryNextSample"));
    auto* review_before_image = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditReviewBeforeImage"));
    auto* review_after_image = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditReviewAfterImage"));
    auto* story_role_button = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditAssignmentStoryRoleButton"));
    const auto expected_ai_edit_contract =
        QStringLiteral("ai-edit:v10|entry:left-rail-agent-terminal|sequence:direction-inspiration-live-work-review|surface:inset-retro-compute-panel|motion:scale-fade-slide|target-selection:workspace-context-first|style-start:three-path-project-sticky|scope-override:details|inspiration:visual-selection|activity:live-editor-journey|result:full-resolution-history-backed|details:progressive-disclosure|keyboard:page-entry-escape-focus-return");
    if (ai_edit_dialog == nullptr || sample_panel == nullptr || primary_review == nullptr ||
        story_map == nullptr || story_arc == nullptr || story_receipt == nullptr ||
        story_role_evidence == nullptr || story_role_evidence_title == nullptr ||
        story_role_evidence_reason == nullptr ||
        story_role_evidence_boundary == nullptr ||
        story_previous_sample == nullptr || story_sample_position == nullptr ||
        story_next_sample == nullptr ||
        review_before_image == nullptr || review_after_image == nullptr ||
        story_role_button == nullptr) {
      return fail("AI Edit visual proof should expose its review controls");
    }
    if (ai_edit_dialog->property("aiEditUxContract").toString() != expected_ai_edit_contract) {
      return fail("AI Edit visual proof should use the current conversation contract");
    }
    if (!sample_panel->property("visible").toBool()) {
      return fail("AI Edit visual proof should show the sample review panel");
    }
    if (!story_map->property("visible").toBool() ||
        !story_arc->property("text").toString().contains(QStringLiteral("Hero anchor")) ||
        !story_receipt->property("text").toString().contains(QStringLiteral("NO AUTO-CULL")) ||
        !story_role_button->property("visible").toBool() ||
        !story_role_button->property("text").toString().contains(
            QStringLiteral("Atmosphere")) ||
        story_previous_sample->property("enabled").toBool() ||
        !story_next_sample->property("enabled").toBool() ||
        story_sample_position->property("text").toString() !=
            QStringLiteral("1 / 2") ||
        !story_role_evidence_title->property("text").toString().contains(
            QStringLiteral("WHY ATMOSPHERE · SOURCE ORDER")) ||
        !story_role_evidence_reason->property("text").toString().contains(
            QStringLiteral("earliest unassigned frame")) ||
        story_role_evidence_boundary->property("text").toString() !=
            QStringLiteral("NO SCENE CLAIM")) {
      return fail("AI Edit sample review should expose bounded assignment story roles and their safety receipt");
    }
    if (primary_review->property("text").toString() != QStringLiteral("Apply to All")) {
      std::cerr << "AI Edit primary action was: "
                << primary_review->property("text").toString().toStdString() << '\n';
      return fail("AI Edit visual proof should expose one outcome-first batch approval action");
    }
    if (!primary_review->property("enabled").toBool()) {
      auto* disabled_ancestor = primary_review;
      while (disabled_ancestor != nullptr) {
        std::cerr << "AI Edit review ancestor "
                  << disabled_ancestor->metaObject()->className()
                  << " objectName="
                  << disabled_ancestor->objectName().toStdString()
                  << " enabled="
                  << disabled_ancestor->property("enabled").toBool()
                  << " visible="
                  << disabled_ancestor->property("visible").toBool()
                  << '\n';
        disabled_ancestor = disabled_ancestor->parent();
      }
      return fail("AI Edit visual proof should expose samples, real proof, affected count, and approved batch continuation");
    }
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("ai-edit-sample-review"),
            1440,
            900,
            {
                QStringLiteral("aiEditSampleReviewPanel"),
                QStringLiteral("aiEditAssignmentStoryMap"),
                QStringLiteral("aiEditAssignmentStoryArc"),
                QStringLiteral("aiEditAssignmentStoryReceipt"),
                QStringLiteral("aiEditStoryRoleEvidence"),
                QStringLiteral("aiEditStoryRoleEvidenceTitle"),
                QStringLiteral("aiEditStoryRoleEvidenceReason"),
                QStringLiteral("aiEditStorySampleNavigator"),
                QStringLiteral("aiEditStorySamplePosition"),
                QStringLiteral("aiEditAssignmentStoryRoleButton"),
                QStringLiteral("aiEditReviewBeforeImage"),
                QStringLiteral("aiEditReviewAfterImage"),
                QStringLiteral("aiEditDiscardDraftButton"),
                QStringLiteral("aiEditPrimaryReviewButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("scope"), QStringLiteral("20 selected photos")},
                {QStringLiteral("sampleCount"), 2},
                {QStringLiteral("remainingCount"), 18},
                {QStringLiteral("storyMapContract"),
                 QStringLiteral("agent-assignment-story-map-v1")},
                {QStringLiteral("storyPlanningInfluence"),
                 QStringLiteral("sample-review-coverage-only")},
                {QStringLiteral("semanticRecognitionUsed"), false},
                {QStringLiteral("autoCull"), false},
                {QStringLiteral("sourceOrderPreserved"), true},
                {QStringLiteral("photographerSteerable"), true},
                {QStringLiteral("beforeProofPath"), before_path},
                {QStringLiteral("afterProofPath"), after_path},
                {QStringLiteral("draftReversible"), true},
                {QStringLiteral("discardAction"), QStringLiteral("Discard samples")},
                {QStringLiteral("revisionAction"), QStringLiteral("Make Changes")},
                {QStringLiteral("approvalAction"), QStringLiteral("Apply to All")},
            })) {
      std::cerr << "AI Edit sample-review state storyVisible="
                << story_map->property("visible").toBool()
                << " storySize=" << story_map->property("width").toReal()
                << 'x' << story_map->property("height").toReal()
                << " evidenceVisible="
                << story_role_evidence->property("visible").toBool()
                << " evidenceSize="
                << story_role_evidence->property("width").toReal()
                << 'x' << story_role_evidence->property("height").toReal()
                << " directionComparison="
                << QJsonDocument::fromVariant(
                       fake_ai_edit_controller
                           ->property("draftDirectionComparison"))
                       .toJson(QJsonDocument::Compact)
                       .toStdString()
                << '\n';
      return fail("AI Edit visual proof should capture the unified sample review");
    }
    if (!QMetaObject::invokeMethod(
            story_role_button, "clicked", Qt::DirectConnection)) {
      return fail("AI Edit Story Director should open the shown-frame role menu");
    }
    QObject* story_role_popup_surface = nullptr;
    QObject* story_role_atmosphere_action = nullptr;
    QObject* story_role_hero_action = nullptr;
    QObject* story_role_detail_action = nullptr;
    QObject* story_role_support_action = nullptr;
    if (!wait_for_condition(
            [&]() {
              story_role_popup_surface = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditAssignmentStoryRolePopupSurface"));
              story_role_atmosphere_action = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditStoryRoleAtmosphereAction"));
              story_role_hero_action = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditStoryRoleHeroAction"));
              story_role_detail_action = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditStoryRoleDetailAction"));
              story_role_support_action = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditStoryRoleSupportAction"));
              return story_role_popup_surface != nullptr &&
                     story_role_popup_surface->property("visible").toBool() &&
                     story_role_atmosphere_action != nullptr &&
                     story_role_hero_action != nullptr &&
                     story_role_detail_action != nullptr &&
                     story_role_support_action != nullptr &&
                     !story_role_atmosphere_action->property("enabled").toBool() &&
                     story_role_hero_action->property("enabled").toBool() &&
                     story_role_detail_action->property("enabled").toBool() &&
                     story_role_support_action->property("enabled").toBool();
            },
            1000) ||
        !capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-story-director-role-menu"),
            1440,
            900,
            {
                QStringLiteral("aiEditAssignmentStoryRolePopupSurface"),
                QStringLiteral("aiEditStoryRoleAtmosphereAction"),
                QStringLiteral("aiEditStoryRoleHeroAction"),
                QStringLiteral("aiEditStoryRoleDetailAction"),
                QStringLiteral("aiEditStoryRoleSupportAction"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-assignment-story-role-revision-v1")},
                {QStringLiteral("roleOptions"),
                 QJsonArray{
                     QStringLiteral("atmosphere"),
                     QStringLiteral("hero"),
                     QStringLiteral("detail"),
                     QStringLiteral("transition"),
                     QStringLiteral("support"),
                 }},
                {QStringLiteral("shownRole"), QStringLiteral("atmosphere")},
                {QStringLiteral("currentRoleDisabled"), true},
                {QStringLiteral("photographerDirected"), true},
            })) {
      return fail("AI Edit Story Director should show all five photographer-directed assignment roles");
    }
    if (!QMetaObject::invokeMethod(
            story_role_hero_action, "clicked", Qt::DirectConnection) ||
        !wait_for_condition(
            [ai_edit_harness_object, story_receipt, story_role_button]() {
              return ai_edit_harness_object
                             ->property("storyRoleRevisionCount")
                             .toInt() == 1 &&
                     story_receipt->property("text").toString().contains(
                         QStringLiteral("PHOTOGRAPHER STEERED")) &&
                     story_role_button->property("text").toString().contains(
                         QStringLiteral("Hero"));
            },
            1500) ||
        !capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-story-director-steered"),
            1440,
            900,
            {
                QStringLiteral("aiEditAssignmentStoryMap"),
                QStringLiteral("aiEditAssignmentStoryArc"),
                QStringLiteral("aiEditAssignmentStoryReceipt"),
                QStringLiteral("aiEditAssignmentStoryRoleButton"),
                QStringLiteral("aiEditSampleReviewPanel"),
                QStringLiteral("aiEditPrimaryReviewButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-assignment-story-role-revision-v1")},
                {QStringLiteral("requestedRole"), QStringLiteral("hero")},
                {QStringLiteral("realProofsRebuilt"), true},
                {QStringLiteral("remainingPhotosUntouched"), true},
                {QStringLiteral("sourceOrderPreserved"), true},
                {QStringLiteral("durableMemoryWritten"), false},
            })) {
      return fail("AI Edit Story Director should rebuild review coverage after an explicit Hero choice");
    }
    if (!QMetaObject::invokeMethod(
            story_role_button, "clicked", Qt::DirectConnection) ||
        !wait_for_condition(
            [story_role_detail_action]() {
              return story_role_detail_action->property("visible").toBool() &&
                     story_role_detail_action->property("enabled").toBool();
            },
            1000) ||
        !QMetaObject::invokeMethod(
            story_role_detail_action, "clicked", Qt::DirectConnection) ||
        !wait_for_condition(
            [ai_edit_harness_object, story_arc, story_role_button]() {
              return ai_edit_harness_object
                             ->property("storyRoleRevisionCount")
                             .toInt() == 2 &&
                     story_role_button->property("text").toString().contains(
                         QStringLiteral("Detail")) &&
                     story_arc->property("text").toString().startsWith(
                         QStringLiteral("Detail / texture"));
            },
            1500) ||
        !capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-story-director-detail-steered"),
            1440,
            900,
            {
                QStringLiteral("aiEditAssignmentStoryMap"),
                QStringLiteral("aiEditAssignmentStoryArc"),
                QStringLiteral("aiEditAssignmentStoryReceipt"),
                QStringLiteral("aiEditAssignmentStoryRoleButton"),
                QStringLiteral("aiEditSampleReviewPanel"),
                QStringLiteral("aiEditPrimaryReviewButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-assignment-story-role-revision-v1")},
                {QStringLiteral("requestedRole"), QStringLiteral("detail")},
                {QStringLiteral("repeatedRoleSwap"), true},
                {QStringLiteral("realProofsRebuilt"), true},
                {QStringLiteral("remainingPhotosUntouched"), true},
                {QStringLiteral("sourceOrderPreserved"), true},
                {QStringLiteral("durableMemoryWritten"), false},
    })) {
      return fail("AI Edit Story Director should support repeated non-Hero story-role direction without widening scope");
    }
    if (!QMetaObject::invokeMethod(
            story_next_sample, "clicked", Qt::DirectConnection) ||
        !wait_for_condition(
            [story_previous_sample, story_sample_position,
             story_next_sample, story_role_button,
             review_before_image, review_after_image]() {
              return story_previous_sample->property("enabled").toBool() &&
                     !story_next_sample->property("enabled").toBool() &&
                     story_sample_position->property("text").toString() ==
                         QStringLiteral("2 / 2") &&
                     story_role_button->property("text").toString().contains(
                         QStringLiteral("Atmosphere")) &&
                     review_before_image->property("source")
                         .toUrl()
                         .toString()
                         .contains(QStringLiteral("second-before.png")) &&
                     review_after_image->property("source")
                         .toUrl()
                         .toString()
                         .contains(QStringLiteral("second-after.png")) &&
                     review_before_image->property("status").toInt() == 1 &&
                     review_after_image->property("status").toInt() == 1;
            },
            1000) ||
        !QMetaObject::invokeMethod(
            story_role_button, "clicked", Qt::DirectConnection) ||
        !wait_for_condition(
            [story_role_support_action]() {
              return story_role_support_action->property("visible").toBool() &&
                     story_role_support_action->property("enabled").toBool();
            },
            1000) ||
        !QMetaObject::invokeMethod(
            story_role_support_action, "clicked", Qt::DirectConnection) ||
        !wait_for_condition(
            [ai_edit_harness_object, story_sample_position,
             story_role_button, story_role_evidence_title,
             story_role_evidence_reason]() {
              return ai_edit_harness_object
                             ->property("storyRoleRevisionCount")
                             .toInt() == 3 &&
                     story_sample_position->property("text").toString() ==
                         QStringLiteral("2 / 2") &&
                     story_role_button->property("text").toString().contains(
                         QStringLiteral("Support")) &&
                     story_role_evidence_title->property("text")
                         .toString()
                         .contains(QStringLiteral("WHY SUPPORT · YOUR DIRECTION")) &&
                     story_role_evidence_reason->property("text")
                         .toString()
                         .contains(QStringLiteral("photographer explicitly assigned"));
            },
            1500) ||
        !capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-story-director-second-sample-steered"),
            1440,
            900,
            {
                QStringLiteral("aiEditAssignmentStoryMap"),
                QStringLiteral("aiEditAssignmentStoryArc"),
                QStringLiteral("aiEditAssignmentStoryReceipt"),
                QStringLiteral("aiEditStoryRoleEvidence"),
                QStringLiteral("aiEditStoryRoleEvidenceTitle"),
                QStringLiteral("aiEditStoryRoleEvidenceReason"),
                QStringLiteral("aiEditStorySampleNavigator"),
                QStringLiteral("aiEditStorySamplePosition"),
                QStringLiteral("aiEditAssignmentStoryRoleButton"),
                QStringLiteral("aiEditReviewBeforeImage"),
                QStringLiteral("aiEditReviewAfterImage"),
                QStringLiteral("aiEditSampleReviewPanel"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-assignment-story-role-revision-v1")},
                {QStringLiteral("activeSampleIndex"), 1},
                {QStringLiteral("activeSamplePosition"), QStringLiteral("2 / 2")},
                {QStringLiteral("activeAssetId"), QStringLiteral("asset-2")},
                {QStringLiteral("requestedRole"), QStringLiteral("support")},
                {QStringLiteral("roleEvidenceContract"),
                 QStringLiteral("agent-assignment-story-role-evidence-v1")},
                {QStringLiteral("roleEvidenceBasis"),
                 QStringLiteral("photographer-direction")},
                {QStringLiteral("semanticRecognitionUsed"), false},
                {QStringLiteral("navigatedSample"), true},
                {QStringLiteral("beforeProofPath"), second_before_path},
                {QStringLiteral("afterProofPath"), second_after_path},
                {QStringLiteral("realProofsRebuilt"), true},
                {QStringLiteral("remainingPhotosUntouched"), true},
                {QStringLiteral("durableMemoryWritten"), false},
            })) {
      return fail("AI Edit Story Director should navigate and direct a second real sample without falling back to sample one");
    }
    auto* ask_for_changes = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditAskForChangesButton"));
    if (ask_for_changes == nullptr ||
        !QMetaObject::invokeMethod(ask_for_changes, "clicked", Qt::DirectConnection)) {
      return fail("AI Edit sample review should expose one direct Make Changes action");
    }
    QObject* sample_revision_panel = nullptr;
    QObject* sample_feedback_editor = nullptr;
    QObject* submit_sample_revision = nullptr;
    if (!wait_for_condition(
            [ai_edit_visual_root, &sample_revision_panel, &sample_feedback_editor,
             &submit_sample_revision]() {
              sample_revision_panel = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditSampleRevisionPanel"));
              sample_feedback_editor = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditSampleFeedbackEditor"));
              submit_sample_revision = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditSubmitSampleRevisionButton"));
              return sample_revision_panel != nullptr &&
                     sample_revision_panel->property("visible").toBool() &&
                     sample_revision_panel->property("height").toReal() >= 72.0 &&
                     sample_feedback_editor != nullptr && submit_sample_revision != nullptr;
            },
            1500)) {
      std::cerr << "AI Edit revision layout expanded="
                << ai_edit_dialog->property("revisionExpanded").toBool()
                << " currentPage=" << ai_edit_dialog->property("currentPage").toInt()
                << " askText=" << (ask_for_changes != nullptr
                                         ? ask_for_changes->property("text").toString().toStdString()
                                         : std::string("missing"))
                << " askEnabled=" << (ask_for_changes != nullptr &&
                                       ask_for_changes->property("enabled").toBool())
                << " askVisible=" << (ask_for_changes != nullptr &&
                                       ask_for_changes->property("visible").toBool())
                << " panel=" << (sample_revision_panel != nullptr)
                << " visible=" << (sample_revision_panel != nullptr &&
                                     sample_revision_panel->property("visible").toBool())
                << " height=" << (sample_revision_panel != nullptr
                                        ? sample_revision_panel->property("height").toReal()
                                        : -1.0)
                << " editor=" << (sample_feedback_editor != nullptr)
                << " submit=" << (submit_sample_revision != nullptr)
                << '\n';
      return fail("AI Edit should replace Apply with one inline sample-revision composer");
    }
    const auto revision_text =
        QStringLiteral("Keep the warmth, but make the shadows less dense.");
    sample_feedback_editor->setProperty("text", revision_text);
    if (!wait_for_condition(
            [submit_sample_revision]() {
              return submit_sample_revision->property("enabled").toBool();
            },
            1000) ||
        primary_review->property("visible").toBool()) {
      return fail("AI Edit sample correction should have one enabled primary action at a time");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("ai-edit-sample-revision"),
            1440,
            900,
            {
                QStringLiteral("aiEditSampleReviewPanel"),
                QStringLiteral("aiEditSampleRevisionPanel"),
                QStringLiteral("aiEditSampleFeedbackEditor"),
                QStringLiteral("aiEditSubmitSampleRevisionButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-inline-sample-revision-v1")},
                {QStringLiteral("feedback"), revision_text},
                {QStringLiteral("remainingBatchUntouched"), true},
                {QStringLiteral("singlePrimaryAction"), true},
                {QStringLiteral("draftReversible"), true},
            })) {
      return fail("AI Edit visual proof should capture inline sample revision");
    }
    if (!QMetaObject::invokeMethod(
            submit_sample_revision,
            "clicked",
            Qt::DirectConnection) ||
        !wait_for_condition(
            [ai_edit_dialog, ai_edit_harness_object, revision_text]() {
              return ai_edit_harness_object->property("lastSampleFeedback").toString() ==
                         revision_text &&
                     ai_edit_dialog->property("currentPage").toInt() == 3 &&
                     !ai_edit_dialog->property("revisionExpanded").toBool();
            },
            1500)) {
      return fail("AI Edit should send sample feedback into immediate revision and return to review");
    }
    auto* correction_transfer_preview = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditCorrectionTransferPreview"));
    auto* correction_transfer_headline = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditCorrectionTransferHeadline"));
    auto* correction_transfer_lesson = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditCorrectionTransferLesson"));
    auto* correction_transfer_detail = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditCorrectionTransferDetail"));
    if (correction_transfer_preview == nullptr ||
        correction_transfer_headline == nullptr ||
        correction_transfer_lesson == nullptr ||
        correction_transfer_detail == nullptr ||
        !wait_for_condition(
            [correction_transfer_preview, correction_transfer_headline,
             correction_transfer_lesson, correction_transfer_detail,
             revision_text]() {
              return correction_transfer_preview->property("visible").toBool() &&
                     correction_transfer_preview->property("height").toReal() >= 48.0 &&
                     correction_transfer_headline->property("text").toString() ==
                         QStringLiteral("Your correction is ready for 18 more photos") &&
                     correction_transfer_lesson->property("text").toString().contains(
                         revision_text) &&
                     correction_transfer_detail->property("text").toString().contains(
                         QStringLiteral("remaining 18 stay untouched"));
            },
            1500)) {
      return fail("the revised samples should show exactly how the correction will transfer while the remaining batch stays untouched");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-correction-transfer-preview"),
            1440,
            900,
            {
                QStringLiteral("aiEditSampleReviewPanel"),
                QStringLiteral("aiEditCorrectionTransferPreview"),
                QStringLiteral("aiEditCorrectionTransferHeadline"),
                QStringLiteral("aiEditCorrectionTransferLesson"),
                QStringLiteral("aiEditCorrectionTransferDetail"),
                QStringLiteral("aiEditPrimaryReviewButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-correction-transfer-preview-v1")},
                {QStringLiteral("sampleCount"), 2},
                {QStringLiteral("remainingCount"), 18},
                {QStringLiteral("batchGroupCount"), 3},
                {QStringLiteral("remainingUntouched"), true},
                {QStringLiteral("approvalRequired"), true},
                {QStringLiteral("durableMemoryDeferred"), true},
                {QStringLiteral("projectLocal"), true},
            })) {
      return fail("AI Edit visual proof should capture the correction transfer and memory boundary");
    }
    ai_edit_harness_object->setProperty("showExceptionReview", true);
    QObject* exception_review_panel = nullptr;
    QObject* exception_review_headline = nullptr;
    QObject* exception_review_list = nullptr;
    QObject* exception_review_footnote = nullptr;
    QObject* exception_first_summary = nullptr;
    QObject* exception_first_preserved = nullptr;
    QObject* exception_fix_button = nullptr;
    QObject* exception_keep_button = nullptr;
    QObject* exception_revert_button = nullptr;
    QObject* consistency_map = nullptr;
    QObject* consistency_headline = nullptr;
    QObject* consistency_summary = nullptr;
    QObject* consistency_group_list = nullptr;
    QObject* consistency_first_group = nullptr;
    QObject* consistency_first_status = nullptr;
    QObject* approved_story_panel = nullptr;
    QObject* approved_story_eyebrow = nullptr;
    QObject* approved_story_headline = nullptr;
    QObject* approved_story_boundary = nullptr;
    QObject* approved_story_evidence = nullptr;
    QObject* approved_story_navigator = nullptr;
    if (!wait_for_condition(
            [ai_edit_visual_root, primary_review, &exception_review_panel,
             &exception_review_headline, &exception_review_list,
             &exception_review_footnote, &exception_fix_button,
             &exception_keep_button, &exception_revert_button,
             &exception_first_summary, &exception_first_preserved,
             &consistency_map, &consistency_headline, &consistency_summary,
             &consistency_group_list, &consistency_first_group,
             &consistency_first_status, &approved_story_panel,
             &approved_story_eyebrow, &approved_story_headline,
             &approved_story_boundary, &approved_story_evidence,
             &approved_story_navigator]() {
              exception_review_panel = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditExceptionReviewPanel"));
              exception_review_headline = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditExceptionReviewHeadline"));
              exception_review_list = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditExceptionReviewList"));
              exception_review_footnote = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditExceptionReviewFootnote"));
              exception_first_summary = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditExceptionFirstSummary"));
              exception_first_preserved = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditExceptionFirstPreserved"));
              exception_fix_button = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditExceptionFixButton"));
              exception_keep_button = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditExceptionKeepButton"));
              exception_revert_button = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditExceptionRevertButton"));
              consistency_map = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditConsistencyMap"));
              consistency_headline = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditConsistencyHeadline"));
              consistency_summary = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditConsistencySummary"));
              consistency_group_list = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditConsistencyGroupList"));
              consistency_first_group = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditConsistencyGroupFirst"));
              consistency_first_status = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditConsistencyGroupFirstStatus"));
              approved_story_panel = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditApprovedStoryDeliveryReceipt"));
              approved_story_eyebrow = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditApprovedStoryEyebrow"));
              approved_story_headline = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditApprovedStoryHeadline"));
              approved_story_boundary = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditApprovedStoryBoundary"));
              approved_story_evidence = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditStoryRoleEvidence"));
              approved_story_navigator = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditStorySampleNavigator"));
              return item_visible(qobject_cast<QQuickItem*>(approved_story_panel)) &&
                     approved_story_eyebrow != nullptr &&
                     approved_story_eyebrow->property("text").toString() ==
                         QStringLiteral("STORY DIRECTION · APPROVED FOR DELIVERY") &&
                     approved_story_headline != nullptr &&
                     approved_story_headline->property("text").toString() ==
                         QStringLiteral("Story direction approved for delivery") &&
                     approved_story_boundary != nullptr &&
                     approved_story_boundary->property("text").toString().contains(
                         QStringLiteral("20 photos keep the approved arc")) &&
                     approved_story_boundary->property("text").toString().contains(
                         QStringLiteral("NO ROLE-BASED RECIPE CHANGES")) &&
                     approved_story_evidence != nullptr &&
                     !approved_story_evidence->property("visible").toBool() &&
                     approved_story_navigator != nullptr &&
                     !approved_story_navigator->property("visible").toBool() &&
                     item_visible(qobject_cast<QQuickItem*>(exception_review_panel)) &&
                     item_visible(qobject_cast<QQuickItem*>(consistency_map)) &&
                     consistency_headline != nullptr &&
                     consistency_headline->property("text").toString() ==
                         QStringLiteral("498 consistent · 2 drift") &&
                     consistency_summary != nullptr &&
                     consistency_summary->property("text").toString().contains(
                         QStringLiteral("3 lighting groups")) &&
                     consistency_group_list != nullptr &&
                     consistency_group_list->property("count").toInt() == 3 &&
                     consistency_first_group != nullptr &&
                     consistency_first_status != nullptr &&
                     consistency_first_status->property("text").toString() ==
                         QStringLiteral("1 needs your eye") &&
                     exception_review_headline != nullptr &&
                     exception_review_headline->property("text").toString() ==
                         QStringLiteral("498 verified · 2 need review") &&
                     item_visible(qobject_cast<QQuickItem*>(exception_review_list)) &&
                     exception_review_list->property("count").toInt() == 2 &&
                     exception_review_footnote != nullptr &&
                     exception_review_footnote->property("text").toString().contains(
                         QStringLiteral("498 verified photos are collapsed")) &&
                     exception_first_summary != nullptr &&
                     exception_first_summary->property("text").toString() ==
                         QStringLiteral("Warmth moved differently from the approved group reference.") &&
                     exception_first_preserved != nullptr &&
                     exception_first_preserved->property("text").toString().contains(
                         QStringLiteral("retouch")) &&
                     exception_fix_button != nullptr &&
                     exception_fix_button->property("text").toString() ==
                         QStringLiteral("Match group") &&
                     exception_keep_button != nullptr &&
                     exception_revert_button != nullptr &&
                     !primary_review->property("enabled").toBool() &&
                     primary_review->property("text").toString() ==
                         QStringLiteral("Resolve 2 first");
            },
            5000)) {
      return fail("AI Edit should collapse a 500-photo batch into one receipt and two actionable exceptions");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-approved-story-delivery-receipt"),
            1440,
            900,
            {
                QStringLiteral("aiEditApprovedStoryDeliveryReceipt"),
                QStringLiteral("aiEditApprovedStoryEyebrow"),
                QStringLiteral("aiEditApprovedStoryHeadline"),
                QStringLiteral("aiEditApprovedStoryArc"),
                QStringLiteral("aiEditApprovedStoryBoundary"),
                QStringLiteral("aiEditExceptionReviewHeadline"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-approved-assignment-story-receipt-v1")},
                {QStringLiteral("approvalSource"),
                 QStringLiteral("explicit-apply-to-all")},
                {QStringLiteral("approvedAssetCount"), 20},
                {QStringLiteral("newlyDraftedCount"), 18},
                {QStringLiteral("sourceOrderPreserved"), true},
                {QStringLiteral("autoCull"), false},
                {QStringLiteral("roleBasedRecipeVariation"), false},
                {QStringLiteral("semanticRecognitionUsed"), false},
                {QStringLiteral("storyDirectionMemoryWritten"), false},
                {QStringLiteral("deliveryInfluence"),
                 QStringLiteral("review-and-delivery-receipt-only")},
            })) {
      return fail("AI Edit visual proof should capture the approved story direction at the final delivery boundary");
    }
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-exception-first-batch-review"),
            1440,
            900,
            {
                QStringLiteral("aiEditExceptionReviewPanel"),
                QStringLiteral("aiEditExceptionReviewHeadline"),
                QStringLiteral("aiEditExceptionReviewList"),
                QStringLiteral("aiEditExceptionReviewFootnote"),
                QStringLiteral("aiEditExceptionFirstSummary"),
                QStringLiteral("aiEditExceptionFirstPreserved"),
                QStringLiteral("aiEditExceptionFixButton"),
                QStringLiteral("aiEditExceptionKeepButton"),
                QStringLiteral("aiEditExceptionRevertButton"),
                QStringLiteral("aiEditPrimaryReviewButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-exception-review-v1")},
                {QStringLiteral("totalCount"), 500},
                {QStringLiteral("verifiedCount"), 498},
                {QStringLiteral("reviewWorkItemCount"), 2},
                {QStringLiteral("queueContainsOnlyExceptions"), true},
                {QStringLiteral("workloadScalesWithExceptions"), true},
                {QStringLiteral("commitBlockedByExceptions"), true},
                {QStringLiteral("consistencyRepairGuidance"), true},
                {QStringLiteral("boundedToneColorRepair"), true},
                {QStringLiteral("unrelatedEditsPreserved"), true},
            })) {
      return fail("AI Edit visual proof should capture exception-first batch review");
    }
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-professional-consistency-map"),
            1440,
            900,
            {
                QStringLiteral("aiEditConsistencyMap"),
                QStringLiteral("aiEditConsistencyHeadline"),
                QStringLiteral("aiEditConsistencySummary"),
                QStringLiteral("aiEditConsistencyGroupList"),
                QStringLiteral("aiEditConsistencyGroupFirst"),
                QStringLiteral("aiEditConsistencyGroupFirstStatus"),
                QStringLiteral("aiEditExceptionReviewPanel"),
                QStringLiteral("aiEditPrimaryReviewButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-professional-consistency-map-v1")},
                {QStringLiteral("basis"),
                 QStringLiteral("within-lighting-group-relative-render-response")},
                {QStringLiteral("assetCount"), 500},
                {QStringLiteral("groupCount"), 3},
                {QStringLiteral("consistentAssetCount"), 498},
                {QStringLiteral("driftCount"), 2},
                {QStringLiteral("measurementComplete"), true},
                {QStringLiteral("professionalGateEligible"), true},
                {QStringLiteral("consistencyPassed"), false},
                {QStringLiteral("readyForProfessionalUse"), false},
                {QStringLiteral("professionalReadinessClaimed"), false},
                {QStringLiteral("humanCalibrationRequired"), true},
                {QStringLiteral("deterministic"), true},
                {QStringLiteral("stableAssetOrdering"), true},
                {QStringLiteral("fixedThresholds"), true},
                {QStringLiteral("commitBlockedByExceptions"), true},
            })) {
      return fail("AI Edit visual proof should expose professional consistency by lighting group and route visible drift into exception review");
    }
    ai_edit_harness_object->setProperty("showExceptionReview", false);
    ai_edit_harness_object->setProperty("showStoryPreferenceCommitted", true);
    ai_edit_dialog->setProperty("editCommitted", true);
    QObject* story_preference_action = nullptr;
    QObject* story_preference_receipt = nullptr;
    QObject* story_preference_message = nullptr;
    if (!wait_for_condition(
            [ai_edit_visual_root, &story_preference_action,
             &story_preference_receipt, &story_preference_message]() {
              story_preference_action = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditStoryPreferenceAction"));
              story_preference_receipt = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditAssignmentStoryReceipt"));
              story_preference_message = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditStoryPreferenceActionMessage"));
              return item_visible(
                         qobject_cast<QQuickItem*>(story_preference_action)) &&
                     story_preference_action->property("text").toString() ==
                         QStringLiteral("Remember this story") &&
                     story_preference_receipt != nullptr;
            },
            1500) ||
        !QMetaObject::invokeMethod(
            story_preference_action, "clicked", Qt::DirectConnection) ||
        !wait_for_condition(
            [ai_edit_harness_object, story_preference_action,
             story_preference_receipt, story_preference_message]() {
              return ai_edit_harness_object
                         ->property("storyPreferenceRemembered")
                         .toBool() &&
                     story_preference_action->property("text").toString() ==
                         QStringLiteral("Forget remembered story") &&
                     story_preference_receipt->property("text")
                         .toString()
                         .contains(QStringLiteral("REMEMBERED FOR THIS PROJECT")) &&
                     story_preference_message->property("visible").toBool() &&
                     story_preference_message->property("text")
                         .toString()
                         .contains(QStringLiteral("Future sets still require fresh sample review"));
            },
            1500)) {
      return fail("AI Edit should make approved story memory one explicit, visible post-commit action");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-story-preference-remembered"),
            1440,
            900,
            {
                QStringLiteral("aiEditAssignmentStoryMap"),
                QStringLiteral("aiEditAssignmentStoryEyebrow"),
                QStringLiteral("aiEditAssignmentStoryHeadline"),
                QStringLiteral("aiEditAssignmentStoryArc"),
                QStringLiteral("aiEditAssignmentStoryReceipt"),
                QStringLiteral("aiEditStoryPreferenceAction"),
                QStringLiteral("aiEditStoryPreferenceActionMessage"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-story-preference-memory-v1")},
                {QStringLiteral("explicitRememberAction"), true},
                {QStringLiteral("projectLocal"), true},
                {QStringLiteral("futureInfluence"),
                 QStringLiteral("role-anchor-suggestion-only")},
                {QStringLiteral("freshSampleReviewRequired"), true},
                {QStringLiteral("recipeInfluence"), false},
                {QStringLiteral("cullInfluence"), false},
                {QStringLiteral("storesImageBytes"), false},
                {QStringLiteral("storesSourcePaths"), false},
            })) {
      return fail("AI Edit visual proof should capture explicit, bounded story preference memory");
    }
    if (!QMetaObject::invokeMethod(
            story_preference_action, "clicked", Qt::DirectConnection) ||
        !wait_for_condition(
            [story_preference_action]() {
              return story_preference_action->property("text").toString() ==
                     QStringLiteral("Confirm forget");
            },
            1000) ||
        !QMetaObject::invokeMethod(
            story_preference_action, "clicked", Qt::DirectConnection) ||
        !wait_for_condition(
            [ai_edit_harness_object, story_preference_action]() {
              return !ai_edit_harness_object
                          ->property("storyPreferenceRemembered")
                          .toBool() &&
                     story_preference_action->property("text").toString() ==
                         QStringLiteral("Remember this story");
            },
            1000)) {
      return fail("AI Edit should require a second click to forget story memory and return to the explicit remember state");
    }
    ai_edit_harness_object->setProperty("showStoryPreferenceCommitted", false);
    ai_edit_dialog->setProperty("editCommitted", false);
    auto* compare_direction_button = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditCompareDirectionButton"));
    if (compare_direction_button == nullptr ||
        !QMetaObject::invokeMethod(
            compare_direction_button, "clicked", Qt::DirectConnection)) {
      return fail("AI Edit review should open the two-direction composer directly");
    }
    QObject* alternative_direction_editor = nullptr;
    QObject* create_comparison_button = nullptr;
    if (!wait_for_condition(
            [ai_edit_visual_root, &alternative_direction_editor,
             &create_comparison_button]() {
              alternative_direction_editor = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditAlternativeDirectionEditor"));
              create_comparison_button = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditCreateComparisonButton"));
              return alternative_direction_editor != nullptr &&
                     alternative_direction_editor->property("visible").toBool() &&
                     create_comparison_button != nullptr;
            },
            1500)) {
      return fail("AI Edit should reveal one compact alternative-direction input");
    }
    const auto alternative_direction =
        QStringLiteral("Cooler, quieter, and more editorial.");
    alternative_direction_editor->setProperty("text", alternative_direction);
    if (!wait_for_condition(
            [create_comparison_button]() {
              return create_comparison_button->property("enabled").toBool();
            },
            1000) ||
        !QMetaObject::invokeMethod(
            create_comparison_button, "clicked", Qt::DirectConnection)) {
      return fail("AI Edit should start rendering Direction B from the inline request");
    }
    QObject* direction_card_a = nullptr;
    QObject* direction_card_b = nullptr;
    QObject* choose_direction_a = nullptr;
    QObject* choose_direction_b = nullptr;
    if (!wait_for_condition(
            [ai_edit_dialog, ai_edit_visual_root, &direction_card_a,
             &direction_card_b, &choose_direction_a, &choose_direction_b]() {
              direction_card_a = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditDirectionCardA"));
              direction_card_b = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditDirectionCardB"));
              choose_direction_a = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditChooseDirectionA"));
              choose_direction_b = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditChooseDirectionB"));
              return ai_edit_dialog->property("currentPage").toInt() == 3 &&
                     direction_card_a != nullptr && direction_card_b != nullptr &&
                     direction_card_a->property("visible").toBool() &&
                     direction_card_b->property("visible").toBool() &&
                     choose_direction_a != nullptr && choose_direction_b != nullptr &&
                     choose_direction_a->property("visible").toBool() &&
                     choose_direction_b->property("visible").toBool();
            },
            2500)) {
      return fail("AI Edit should replace before/after with two actual rendered directions and an explicit choice");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-direction-comparison"),
            1440,
            900,
            {
                QStringLiteral("aiEditDirectionComparisonPanel"),
                QStringLiteral("aiEditDirectionCardA"),
                QStringLiteral("aiEditDirectionCardB"),
                QStringLiteral("aiEditChooseDirectionA"),
                QStringLiteral("aiEditChooseDirectionB"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-draft-direction-comparison-v1")},
                {QStringLiteral("actualRenderedProofs"), true},
                {QStringLiteral("reversible"), true},
                {QStringLiteral("choiceRequired"), true},
                {QStringLiteral("durableMemoryWritten"), false},
                {QStringLiteral("alternativeDirection"),
                 alternative_direction},
            })) {
      return fail("AI Edit visual proof should capture the two-direction decision");
    }
    if (!QMetaObject::invokeMethod(
            choose_direction_b, "clicked", Qt::DirectConnection)) {
      return fail("AI Edit should allow Direction B to be selected directly on its proof");
    }
    auto* direction_choice_receipt = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditDirectionChoiceReceipt"));
    if (direction_choice_receipt == nullptr ||
        !wait_for_condition(
            [direction_choice_receipt, primary_review]() {
              return direction_choice_receipt->property("visible").toBool() &&
                     direction_choice_receipt->property("text").toString().contains(
                         QStringLiteral("Direction B selected")) &&
                     direction_choice_receipt->property("text").toString().contains(
                         QStringLiteral("remembered only after Commit Edits")) &&
                     primary_review->property("visible").toBool();
            },
            1500)) {
      std::cerr << "AI Edit direction-choice receipt present="
                << (direction_choice_receipt != nullptr)
                << " visible="
                << (direction_choice_receipt != nullptr &&
                    direction_choice_receipt->property("visible").toBool())
                << " text=\""
                << (direction_choice_receipt != nullptr
                        ? direction_choice_receipt->property("text")
                              .toString()
                              .toStdString()
                        : std::string{})
                << "\" primaryVisible="
                << primary_review->property("visible").toBool()
                << " primaryText=\""
                << primary_review->property("text").toString().toStdString()
                << "\" comparison="
                << QJsonDocument::fromVariant(
                       fake_ai_edit_controller
                           ->property("draftDirectionComparison"))
                       .toJson(QJsonDocument::Compact)
                       .toStdString()
                << '\n';
      return fail("AI Edit should show which direction is active and keep learning deferred until Apply");
    }
    if (!QMetaObject::invokeMethod(ai_edit_dialog, "close", Qt::DirectConnection) ||
        !wait_for_condition(
            [ai_edit_dialog]() { return !ai_edit_dialog->property("visible").toBool(); },
            1000)) {
      return fail("AI Edit visual proof should close the walkthrough before workspace capture");
    }
    ai_edit_harness_object->setProperty("directionComparison", QVariantMap{});
    ai_edit_harness->setProperty("showAgentWorkspaceReview", true);
    ai_edit_harness->setProperty("showLocalPlanning", true);
    auto* local_planning_heartbeat = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceLocalPlanningHeartbeat"));
    auto* local_planning_heartbeat_label = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceLocalPlanningHeartbeatLabel"));
    if (local_planning_heartbeat == nullptr ||
        local_planning_heartbeat_label == nullptr ||
        !wait_for_condition(
            [local_planning_heartbeat, local_planning_heartbeat_label]() {
              const auto text =
                  local_planning_heartbeat_label->property("text").toString();
              return item_visible(local_planning_heartbeat) &&
                     text.contains(QStringLiteral("LOCAL VISION")) &&
                     text.contains(QStringLiteral("NO PIXELS CHANGED")) &&
                     text.contains(QStringLiteral("CANCEL SAFE")) &&
                     !text.contains(QStringLiteral("0:00"));
            },
            2500) ||
        !capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-local-vision-planning-heartbeat"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceSpeechBubble"),
                QStringLiteral("agentWorkspaceLocalPlanningHeartbeat"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-local-vision-planning-heartbeat-v1")},
                {QStringLiteral("localOnly"), true},
                {QStringLiteral("cancelSafe"), true},
                {QStringLiteral("pixelsChanged"), false},
                {QStringLiteral("elapsedTimeVisible"), true},
                {QStringLiteral("uiThreadBlocked"), false},
            })) {
      return fail("AI Edit should show honest elapsed local-model activity while the reversible plan runs");
    }
    ai_edit_harness->setProperty("showLocalPlanning", false);
    ai_edit_harness->setProperty("showSelectionConfirmation", true);
    auto* workspace_review = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("aiEditAgentWorkspaceReview"));
    auto* selection_confirmation_card = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceSelectionConfirmationCard"));
    auto* selection_confirmation_prompt = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceSelectionConfirmationPrompt"));
    auto* selection_candidate_one = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceSelectionCandidate1"));
    auto* selection_candidate_two = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceSelectionCandidate2"));
    auto* candidate_outline_one = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("aiEditAgentGroundingCandidate1"));
    auto* candidate_outline_two = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("aiEditAgentGroundingCandidate2"));
    if (workspace_review == nullptr || selection_confirmation_card == nullptr ||
        selection_confirmation_prompt == nullptr || selection_candidate_one == nullptr ||
        selection_candidate_two == nullptr || candidate_outline_one == nullptr ||
        candidate_outline_two == nullptr ||
        !wait_for_condition(
            [workspace_review, selection_confirmation_card,
             selection_confirmation_prompt, selection_candidate_one,
             selection_candidate_two, candidate_outline_one,
             candidate_outline_two]() {
              return item_visible(workspace_review) &&
                     workspace_review->property("awaitingSelectionConfirmation").toBool() &&
                     item_visible(selection_confirmation_card) &&
                     selection_confirmation_prompt->property("text").toString() ==
                         QStringLiteral("Which second window?") &&
                     selection_candidate_one->property("text").toString().contains(
                         QStringLiteral("Left window")) &&
                     selection_candidate_two->property("text").toString().contains(
                         QStringLiteral("Upper-right window")) &&
                     item_visible(candidate_outline_one) &&
                     item_visible(candidate_outline_two);
            },
            2500)) {
      return fail("AI Edit should stop before mutation and show numbered visible-object candidates when grounding is ambiguous");
    }
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-object-disambiguation"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceSelectionConfirmationCard"),
                QStringLiteral("agentWorkspaceSelectionCandidate1"),
                QStringLiteral("agentWorkspaceSelectionCandidate2"),
                QStringLiteral("aiEditAgentGroundingCandidate1"),
                QStringLiteral("aiEditAgentGroundingCandidate2"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-grounded-selection-confirmation-v1")},
                {QStringLiteral("targetLabel"), QStringLiteral("second window")},
                {QStringLiteral("candidateCount"), 2},
                {QStringLiteral("draftActive"), false},
                {QStringLiteral("sourceMutationCount"), 0},
                {QStringLiteral("policy"),
                 QStringLiteral("no-edit-before-explicit-instance-confirmation")},
            })) {
      return fail("AI Edit visual proof should capture honest object disambiguation before drafting");
    }
    if (!QMetaObject::invokeMethod(selection_candidate_two, "click") ||
        !wait_for_condition(
            [ai_edit_harness_object = ai_edit_harness.data(), workspace_review,
             selection_confirmation_card]() {
              return !ai_edit_harness_object->property("showSelectionConfirmation").toBool() &&
                     !workspace_review->property("awaitingSelectionConfirmation").toBool() &&
                     !item_visible(selection_confirmation_card);
            },
            1500)) {
      return fail("choosing a numbered object candidate should leave confirmation and reveal the reversible draft");
    }
    ai_edit_harness->setProperty("showClarification", true);
    auto* clarification_card = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceClarificationCard"));
    auto* clarification_prompt = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceClarificationPrompt"));
    auto* clarification_editor = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceClarificationEditor"));
    auto* clarification_continue = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceClarificationContinue"));
    if (clarification_card == nullptr || clarification_prompt == nullptr ||
        clarification_editor == nullptr || clarification_continue == nullptr ||
        !wait_for_condition(
            [workspace_review, clarification_card, clarification_prompt,
             clarification_editor, clarification_continue]() {
              return item_visible(workspace_review) &&
                     workspace_review->property("awaitingClarification").toBool() &&
                     item_visible(clarification_card) &&
                     clarification_prompt->property("text").toString() ==
                         QStringLiteral(
                             "Should the reception stay warm editorial or become neutral documentary?") &&
                     clarification_editor->property("visible").toBool() &&
                     !clarification_continue->property("enabled").toBool();
            },
            1500)) {
      return fail("AI Edit should show the model's actual clarification question with an answer field instead of a generic approval action");
    }
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-clarification-answer"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceClarificationCard"),
                QStringLiteral("agentWorkspaceClarificationPrompt"),
                QStringLiteral("agentWorkspaceClarificationEditor"),
                QStringLiteral("agentWorkspaceClarificationContinue"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-clarification-answer-v1")},
                {QStringLiteral("questionVisible"), true},
                {QStringLiteral("freeTextAnswerVisible"), true},
                {QStringLiteral("genericApprovalHidden"), true},
                {QStringLiteral("draftActive"), false},
            })) {
      return fail("AI Edit visual proof should capture the compact clarification answer card");
    }
    clarification_editor->setProperty(
        "text", QStringLiteral("Keep it warm editorial, but protect skin tones."));
    if (!wait_for_condition(
            [clarification_continue]() {
              return clarification_continue->property("enabled").toBool();
            },
            500) ||
        !QMetaObject::invokeMethod(
            clarification_continue, "click", Qt::DirectConnection) ||
        !wait_for_condition(
            [ai_edit_harness_object = ai_edit_harness.data(),
             workspace_review, clarification_card]() {
              return !ai_edit_harness_object->property("showClarification").toBool() &&
                     !workspace_review->property("awaitingClarification").toBool() &&
                     !item_visible(clarification_card);
            },
            1000)) {
      return fail("submitting a clarification answer should leave the question state and resume the reversible draft");
    }
    ai_edit_harness->setProperty("showDestructiveApproval", true);
    auto* destructive_approval_headline = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceHeadline"));
    auto* destructive_approval_explanation = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceExplanation"));
    auto* destructive_approval_dismiss = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspacePlanApprovalDismiss"));
    auto* destructive_approval_confirm = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspacePlanApprovalApprove"));
    if (destructive_approval_headline == nullptr ||
        destructive_approval_explanation == nullptr ||
        destructive_approval_dismiss == nullptr ||
        destructive_approval_confirm == nullptr ||
        !wait_for_condition(
            [workspace_review, destructive_approval_headline,
             destructive_approval_explanation, destructive_approval_dismiss,
             destructive_approval_confirm]() {
              return workspace_review->property("awaitingPlanApproval").toBool() &&
                     destructive_approval_headline->property("text").toString() ==
                         QStringLiteral("I need your approval before I continue.") &&
                     destructive_approval_explanation->property("text").toString().contains(
                         QStringLiteral("preserve the source and current edit")) &&
                     item_visible(destructive_approval_dismiss) &&
                     item_visible(destructive_approval_confirm);
            },
            1500)) {
      return fail("destructive-looking wording should stop before drafting and explain that the source and current edit stay preserved");
    }
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-destructive-intent-seatbelt"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceSpeechBubble"),
                QStringLiteral("agentWorkspaceHeadline"),
                QStringLiteral("agentWorkspaceExplanation"),
                QStringLiteral("agentWorkspacePlanApprovalDismiss"),
                QStringLiteral("agentWorkspacePlanApprovalApprove"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-destructive-intent-seatbelt-v1")},
                {QStringLiteral("approvalType"),
                 QStringLiteral("destructive_intent")},
                {QStringLiteral("sourcePreserved"), true},
                {QStringLiteral("currentRecipePreserved"), true},
                {QStringLiteral("draftActive"), false},
                {QStringLiteral("reversibleDraftOnly"), true},
            })) {
      return fail("AI Edit visual proof should capture the destructive-intent seatbelt before mutation");
    }
    if (!QMetaObject::invokeMethod(
            destructive_approval_dismiss, "click", Qt::DirectConnection) ||
        !wait_for_condition(
            [workspace_review]() {
              return !workspace_review->property("awaitingPlanApproval").toBool() &&
                     workspace_review->property("draftReady").toBool();
            },
            1000)) {
      return fail("dismissing destructive-plan approval should return to the untouched reversible review state");
    }
    ai_edit_harness->setProperty("showProofRejection", true);
    auto* rejection_headline = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceHeadline"));
    auto* rejection_explanation = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceExplanation"));
    auto* rejection_action = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceFailurePrimaryAction"));
    auto* rejection_critique = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceVisualCritiqueCard"));
    auto* rejection_finding = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceVisualCritiqueFinding"));
    auto* rejection_evidence = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceVisualCritiqueEvidence"));
    if (rejection_headline == nullptr || rejection_explanation == nullptr ||
        rejection_action == nullptr || rejection_critique == nullptr ||
        rejection_finding == nullptr || rejection_evidence == nullptr ||
        !wait_for_condition(
            [workspace_review, rejection_headline, rejection_explanation,
             rejection_action, rejection_critique, rejection_finding,
             rejection_evidence]() {
              return workspace_review->property("runFailed").toBool() &&
                     !workspace_review->property("draftReady").toBool() &&
                     !workspace_review->property("awaitingInput").toBool() &&
                     rejection_headline->property("text").toString() ==
                         QStringLiteral("I left your photo unchanged.") &&
                     rejection_explanation->property("text").toString().contains(
                         QStringLiteral("changed the color or texture")) &&
                     item_visible(rejection_critique) &&
                     rejection_finding->property("text").toString() ==
                         QStringLiteral("The repair is tinting nearby pixels.") &&
                     rejection_evidence->property("text").toString().contains(
                         QStringLiteral("0.81%")) &&
                     item_visible(rejection_action) &&
                     rejection_action->property("text").toString() ==
                         QStringLiteral("Point to repair");
            },
            1500)) {
      return fail("a rejected cleanup should explain the visible evidence and offer a point-guided repair instead of repeating the same approval");
    }
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-retouch-proof-rejected"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceSpeechBubble"),
                QStringLiteral("agentWorkspaceHeadline"),
                QStringLiteral("agentWorkspaceExplanation"),
                QStringLiteral("agentWorkspaceVisualCritiqueCard"),
                QStringLiteral("agentWorkspaceVisualCritiqueFinding"),
                QStringLiteral("agentWorkspaceVisualCritiqueEvidence"),
                QStringLiteral("agentWorkspaceFailurePrimaryAction"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-proof-rejection-presentation-v1")},
                {QStringLiteral("kind"), QStringLiteral("retouch-quality")},
                {QStringLiteral("proofRejected"), true},
                {QStringLiteral("approvalConsumed"), true},
                {QStringLiteral("approvalLoopPrevented"), true},
                {QStringLiteral("commitBlocked"), true},
                {QStringLiteral("originalRecipePreserved"), true},
                {QStringLiteral("primaryActionLabel"),
                 QStringLiteral("Point to repair")},
                {QStringLiteral("visualCritiqueContract"),
                 QStringLiteral("agent-visual-critique-v1")},
                {QStringLiteral("repairKind"),
                 QStringLiteral("point-and-tighten")},
            })) {
      return fail("AI Edit visual proof should capture the terminal cleanup rejection state");
    }
    if (!QMetaObject::invokeMethod(
            rejection_action, "click", Qt::DirectConnection) ||
        !wait_for_condition(
            [ai_edit_harness_object = ai_edit_harness.data(), workspace_review,
             rejection_headline]() {
              return ai_edit_harness_object
                         ->property("workspaceSpatialTargetMode")
                         .toBool() &&
                     workspace_review->property("spatialTargetMode").toBool() &&
                     rejection_headline->property("text").toString() ==
                         QStringLiteral("Point to the center of the repair.");
            },
            1000)) {
      return fail("Point to repair should immediately enter image targeting mode");
    }
    auto* cancel_repair_point = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceCancelRejectedRepairPoint"));
    if (cancel_repair_point == nullptr || !item_visible(cancel_repair_point) ||
        !capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-retouch-point-repair"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceSpeechBubble"),
                QStringLiteral("agentWorkspaceHeadline"),
                QStringLiteral("agentWorkspaceExplanation"),
                QStringLiteral("agentWorkspaceVisualCritiqueCard"),
                QStringLiteral("agentWorkspaceCancelRejectedRepairPoint"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-proof-repair-plan-v1")},
                {QStringLiteral("interaction"),
                 QStringLiteral("point-and-tighten")},
                {QStringLiteral("oldArtifactReused"), false},
                {QStringLiteral("freshApprovalRequired"), true},
            })) {
      return fail("AI Edit visual proof should capture the point-guided repair mode");
    }
    auto* repair_shared_mask_overlay = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("aiEditSharedMaskOverlay"));
    if (repair_shared_mask_overlay == nullptr ||
        !QMetaObject::invokeMethod(
            repair_shared_mask_overlay,
            "agentPointTargeted",
            Qt::DirectConnection,
            Q_ARG(double, 0.74),
            Q_ARG(double, 0.24)) ||
        !wait_for_condition(
            [ai_edit_harness_object = ai_edit_harness.data(),
             workspace_review, rejection_headline]() {
              return !ai_edit_harness_object
                          ->property("workspaceSpatialTargetMode")
                          .toBool() &&
                     ai_edit_harness_object
                         ->property("showProofRepairApproval")
                         .toBool() &&
                     workspace_review->property("awaitingPlanApproval").toBool() &&
                     rejection_headline->property("text").toString() ==
                         QStringLiteral("I need your approval before I continue.");
            },
            1000)) {
      return fail("a repaired point should become a new approval-bound plan from the original");
    }
    auto* repair_approval_dismiss = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspacePlanApprovalDismiss"));
    auto* repair_approval = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspacePlanApprovalApprove"));
    if (repair_approval_dismiss == nullptr || repair_approval == nullptr ||
        !item_visible(repair_approval_dismiss) || !item_visible(repair_approval) ||
        !capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-retouch-repair-approval"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceSpeechBubble"),
                QStringLiteral("agentWorkspaceHeadline"),
                QStringLiteral("agentWorkspaceExplanation"),
                QStringLiteral("agentWorkspacePlanApprovalDismiss"),
                QStringLiteral("agentWorkspacePlanApprovalApprove"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-proof-repair-plan-v1")},
                {QStringLiteral("normalizedX"), 0.74},
                {QStringLiteral("normalizedY"), 0.24},
                {QStringLiteral("regionScale"), 0.82},
                {QStringLiteral("rebuildBase"),
                 QStringLiteral("untouched-original")},
                {QStringLiteral("oldArtifactReused"), false},
                {QStringLiteral("freshApprovalRequired"), true},
            })) {
      return fail("AI Edit visual proof should capture fresh approval for the tightened repair plan");
    }
    if (!QMetaObject::invokeMethod(
            repair_approval_dismiss, "click", Qt::DirectConnection) ||
        !wait_for_condition(
            [ai_edit_harness_object = ai_edit_harness.data()]() {
              return !ai_edit_harness_object
                          ->property("showProofRepairApproval")
                          .toBool();
            },
            1000)) {
      return fail("dismissing a repaired target should leave the original untouched");
    }
    ai_edit_harness->setProperty("showProofRejection", false);
    if (!wait_for_condition(
            [workspace_review]() {
              return workspace_review->property("draftReady").toBool() &&
                     !workspace_review->property("runFailed").toBool();
            },
            1000)) {
      return fail("the proof-rejection fixture should restore the ordinary reversible review state");
    }
    auto* findings_card = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceVisualFindingsCard"));
    auto* trust_receipt = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTrustReceipt"));
    auto* intent_ledger = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceIntentLedger"));
    auto* intent_action = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceIntentAction"));
    auto* intent_action_summary = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceIntentActionSummary"));
    auto* intent_change_button = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceIntentChangeButton"));
    auto* intent_protect_button = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceIntentProtectButton"));
    auto* intent_protection = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceIntentProtection"));
    if (workspace_review == nullptr || findings_card == nullptr || trust_receipt == nullptr ||
        intent_ledger == nullptr || intent_action == nullptr ||
        intent_action_summary == nullptr || intent_change_button == nullptr ||
        intent_protect_button == nullptr ||
        intent_protection == nullptr ||
        !wait_for_condition(
            [workspace_review, findings_card, trust_receipt, intent_action,
             intent_action_summary, intent_change_button, intent_protect_button,
             intent_protection]() {
              const auto receipt = trust_receipt->property("text").toString();
              return item_visible(workspace_review) && item_visible(findings_card) &&
                     findings_card->height() >= 150.0 &&
                     item_visible(intent_action) &&
                     intent_action_summary->property("text").toString().contains(
                         QStringLiteral("Exposure -0.42")) &&
                     intent_action_summary->property("text").toString().contains(
                         QStringLiteral("Highlights -31")) &&
                     intent_change_button->property("visible").toBool() &&
                     intent_protect_button->property("visible").toBool() &&
                     intent_protect_button->property("text").toString() ==
                         QStringLiteral("Keep") &&
                     intent_protection->property("text").toString().contains(
                         QStringLiteral("Everything else")) &&
                     receipt.contains(QStringLiteral("local target boundary checked")) &&
                     receipt.contains(QStringLiteral("goal matched 96%")) &&
                     receipt.contains(QStringLiteral("only Upper right window revised")) &&
                     !receipt.contains(QStringLiteral("qwen"), Qt::CaseInsensitive) &&
                     !receipt.contains(QStringLiteral("evidence-guided"), Qt::CaseInsensitive) &&
                     !receipt.contains(QStringLiteral("semantic"), Qt::CaseInsensitive) &&
                     !receipt.contains(QStringLiteral("quality check"), Qt::CaseInsensitive);
            },
            2500)) {
      return fail("AI Edit visual proof should expose grounded findings and an outcome-first edit check summary");
    }
    auto* edit_strategy_card = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceEditStrategyCard"));
    auto* edit_strategy_authority = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceEditStrategyAuthority"));
    auto* edit_strategy_label = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceEditStrategyLabel"));
    auto* edit_strategy_reason = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceEditStrategyReason"));
    auto* edit_strategy_preserved = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceEditStrategyPreserved"));
    if (edit_strategy_card == nullptr || edit_strategy_authority == nullptr ||
        edit_strategy_label == nullptr || edit_strategy_reason == nullptr ||
        edit_strategy_preserved == nullptr ||
        !wait_for_condition(
            [edit_strategy_card, edit_strategy_authority,
             edit_strategy_label, edit_strategy_reason,
             edit_strategy_preserved]() {
              return item_visible(edit_strategy_card) &&
                     edit_strategy_card->height() >= 70.0 &&
                     edit_strategy_authority->property("text").toString() ==
                         QStringLiteral("CHOSEN FOR THIS PHOTO") &&
                     edit_strategy_label->property("text").toString() ==
                         QStringLiteral("Localized adjustment") &&
                     edit_strategy_reason->property("text").toString().contains(
                         QStringLiteral("outside its mask stays unchanged")) &&
                     edit_strategy_preserved->property("text").toString().contains(
                         QStringLiteral("Everything outside the target")) &&
                     edit_strategy_preserved->property("text").toString().contains(
                         QStringLiteral("Framing")) &&
                     edit_strategy_preserved->property("text").toString().contains(
                         QStringLiteral("Retouching"));
            },
            1500)) {
      return fail("AI Edit should show the app-owned least-invasive route and what it deliberately leaves unchanged");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-edit-strategy-route"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceEditStrategyCard"),
                QStringLiteral("agentWorkspaceEditStrategyAuthority"),
                QStringLiteral("agentWorkspaceEditStrategyLabel"),
                QStringLiteral("agentWorkspaceEditStrategyReason"),
                QStringLiteral("agentWorkspaceEditStrategyPreserved"),
                QStringLiteral("agentWorkspaceIntentLedger"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-edit-strategy-route-v1")},
                {QStringLiteral("primaryPath"),
                 QStringLiteral("local-mask")},
                {QStringLiteral("appRouted"), true},
                {QStringLiteral("routingAuthority"),
                 QStringLiteral("photo-editor-app")},
                {QStringLiteral("localizedOnly"), true},
                {QStringLiteral("executionAllowed"), true},
                {QStringLiteral("executedGroups"),
                 QJsonArray{QStringLiteral("masks")}},
                {QStringLiteral("sensitiveActionsPauseBeforeDraft"), true},
                {QStringLiteral("existingEditsPreserved"), true},
            })) {
      return fail("AI Edit visual proof should capture the app-owned edit strategy route");
    }
    ai_edit_harness_object->setProperty("showPhotographerTasteCheck", true);
    QMetaObject::invokeMethod(
        fake_ai_edit_controller, "draftChanged", Qt::DirectConnection);
    auto* photographer_taste_check_card = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspacePhotographerTasteCheckCard"));
    auto* photographer_taste_check_headline = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspacePhotographerTasteCheckHeadline"));
    auto* photographer_taste_check_explanation = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspacePhotographerTasteCheckExplanation"));
    auto* photographer_taste_check_action = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspacePhotographerTasteCheckAction"));
    auto* photographer_taste_check_receipt = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspacePhotographerTasteCheckReceipt"));
    if (photographer_taste_check_card == nullptr ||
        photographer_taste_check_headline == nullptr ||
        photographer_taste_check_explanation == nullptr ||
        photographer_taste_check_action == nullptr ||
        photographer_taste_check_receipt == nullptr ||
        !wait_for_condition(
            [photographer_taste_check_card,
             photographer_taste_check_headline,
             photographer_taste_check_explanation,
             photographer_taste_check_action,
             photographer_taste_check_receipt]() {
              return item_visible(photographer_taste_check_card) &&
                     photographer_taste_check_headline->property("text").toString().contains(
                         QStringLiteral("less certain about warmth")) &&
                     photographer_taste_check_explanation->property("text").toString().contains(
                         QStringLiteral("blind ratings")) &&
                     photographer_taste_check_action->property("text").toString() ==
                         QStringLiteral("Compare restrained version") &&
                     photographer_taste_check_receipt->property("text").toString().contains(
                         QStringLiteral("2 REAL RENDERS")) &&
                     photographer_taste_check_receipt->property("text").toString().contains(
                         QStringLiteral("CHOICE + COMMIT"));
            },
            1500)) {
      return fail("AI Edit should turn calibrated uncertainty into an explicit two-render taste check");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-photographer-taste-check"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspacePhotographerTasteCheckCard"),
                QStringLiteral("agentWorkspacePhotographerTasteCheckHeadline"),
                QStringLiteral("agentWorkspacePhotographerTasteCheckExplanation"),
                QStringLiteral("agentWorkspacePhotographerTasteCheckAction"),
                QStringLiteral("agentWorkspacePhotographerTasteCheckReceipt"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-photographer-taste-check-v1")},
                {QStringLiteral("trigger"),
                 QStringLiteral("calibration-sensitive-draft")},
                {QStringLiteral("primaryConcernGroup"),
                 QStringLiteral("warmth")},
                {QStringLiteral("actualRenderedDirections"), 2},
                {QStringLiteral("requiresExplicitChoice"), true},
                {QStringLiteral("automaticComparisonStarted"), false},
                {QStringLiteral("durableMemoryWritten"), false},
                {QStringLiteral("choicePromotionPolicy"),
                 QStringLiteral("explicit-direction-choice-confirmed-by-commit-only")},
                {QStringLiteral("numericThresholdOverridesAllowed"), false},
                {QStringLiteral("professionalReadinessClaimed"), false},
            })) {
      return fail("AI Edit visual proof should capture the calibrated photographer taste check");
    }
    if (!QMetaObject::invokeMethod(
            photographer_taste_check_action, "click", Qt::DirectConnection)) {
      return fail("AI Edit calibrated taste check should start from the resident workspace action");
    }
    QObject* calibrated_comparison_headline = nullptr;
    QObject* calibrated_direction_label_a = nullptr;
    QObject* calibrated_direction_label_b = nullptr;
    QObject* calibrated_choose_direction_a = nullptr;
    QObject* calibrated_choose_direction_b = nullptr;
    if (!wait_for_condition(
            [ai_edit_dialog, ai_edit_conversation_view, ai_edit_visual_root,
             &calibrated_comparison_headline,
             &calibrated_direction_label_a,
             &calibrated_direction_label_b,
             &calibrated_choose_direction_a,
             &calibrated_choose_direction_b]() {
              calibrated_comparison_headline = find_named_item(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditDirectionComparisonHeadline"));
              calibrated_direction_label_a = find_named_item(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditDirectionLabelA"));
              calibrated_direction_label_b = find_named_item(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditDirectionLabelB"));
              calibrated_choose_direction_a = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditChooseDirectionA"));
              calibrated_choose_direction_b = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("aiEditChooseDirectionB"));
              return ai_edit_dialog->property("visible").toBool() &&
                     ai_edit_conversation_view != nullptr &&
                     ai_edit_conversation_view->property("opacity").toDouble() >= 0.99 &&
                     ai_edit_conversation_view->property("scale").toDouble() >= 0.99 &&
                     calibrated_comparison_headline != nullptr &&
                     calibrated_comparison_headline->property("text").toString().contains(
                         QStringLiteral("you teach the choice")) &&
                     calibrated_direction_label_a != nullptr &&
                     calibrated_direction_label_a->property("text").toString() ==
                         QStringLiteral("CURRENT DIRECTION") &&
                     calibrated_direction_label_b != nullptr &&
                     calibrated_direction_label_b->property("text").toString() ==
                         QStringLiteral("MORE RESTRAINED") &&
                     calibrated_choose_direction_a != nullptr &&
                     calibrated_choose_direction_b != nullptr &&
                     calibrated_choose_direction_a->property("visible").toBool() &&
                     calibrated_choose_direction_b->property("visible").toBool();
            },
            2500)) {
      return fail("AI Edit calibrated taste check should open two explicitly labeled rendered directions");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-photographer-taste-directions"),
            1440,
            900,
            {
                QStringLiteral("aiEditDirectionComparisonHeadline"),
                QStringLiteral("aiEditDirectionComparisonPanel"),
                QStringLiteral("aiEditDirectionCardA"),
                QStringLiteral("aiEditDirectionLabelA"),
                QStringLiteral("aiEditDirectionCardB"),
                QStringLiteral("aiEditDirectionLabelB"),
                QStringLiteral("aiEditChooseDirectionA"),
                QStringLiteral("aiEditChooseDirectionB"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-draft-direction-comparison-v1")},
                {QStringLiteral("comparisonOrigin"),
                 QStringLiteral("photographer-calibration")},
                {QStringLiteral("actualRenderedProofs"), true},
                {QStringLiteral("directionA"),
                 QStringLiteral("current-direction")},
                {QStringLiteral("directionB"),
                 QStringLiteral("more-restrained")},
                {QStringLiteral("requiresExplicitChoice"), true},
                {QStringLiteral("durableMemoryWritten"), false},
            })) {
      return fail("AI Edit visual proof should capture both calibrated photographer directions");
    }
    if (!QMetaObject::invokeMethod(ai_edit_dialog, "close", Qt::DirectConnection) ||
        !wait_for_condition(
            [ai_edit_dialog]() { return !ai_edit_dialog->property("visible").toBool(); },
            1000)) {
      return fail("AI Edit calibrated comparison should return cleanly to the workspace");
    }
    ai_edit_harness_object->setProperty("showAgentWorkspaceReview", true);
    ai_edit_harness_object->setProperty("directionComparison", QVariantMap{});
    ai_edit_harness_object->setProperty("calibratedComparisonRequested", false);
    ai_edit_harness_object->setProperty("showPhotographerTasteCheck", false);
    QMetaObject::invokeMethod(
        fake_ai_edit_controller, "draftChanged", Qt::DirectConnection);
    auto* taste_mirror_button = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTasteMirrorButton"));
    if (taste_mirror_button == nullptr ||
        !QMetaObject::invokeMethod(taste_mirror_button, "click")) {
      return fail("AI Edit project memory receipt should open the taste mirror directly in the workspace");
    }
    auto* taste_mirror_card = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTasteMirrorCard"));
    auto* taste_summary = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTasteSummary"));
    auto* taste_name = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTasteName"));
    auto* taste_rename_button = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTasteRenameButton"));
    auto* taste_evidence_row = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTasteEvidenceRow"));
    auto* taste_evidence_thumbnail = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTasteEvidenceThumbnail"));
    auto* taste_forget_button = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTasteForgetButton"));
    auto* taste_calibration_import = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTasteCalibrationImportButton"));
    auto* taste_passport_export = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTastePassportExportButton"));
    auto* taste_passport_import = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTastePassportImportButton"));
    auto* taste_passport_preview_card = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTastePassportPreviewCard"));
    auto* taste_passport_preview_summary = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTastePassportPreviewSummary"));
    auto* taste_passport_preview_boundary = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTastePassportPreviewBoundary"));
    auto* taste_passport_cancel = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTastePassportCancelButton"));
    auto* taste_passport_confirm = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTastePassportConfirmButton"));
    auto* taste_calibration_status = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTasteCalibrationStatus"));
    auto* calibration_review_card = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceCalibrationReviewCard"));
    auto* calibration_review_button = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceCalibrationReviewButton"));
    auto* calibration_finalize_button = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceCalibrationFinalizeButton"));
    auto* calibration_clipboard_finalize_button = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceCalibrationClipboardFinalizeButton"));
    auto* calibration_coach_card = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceCalibrationCoachCard"));
    auto* calibration_coach_headline = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceCalibrationCoachHeadline"));
    auto* calibration_coach_explanation = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceCalibrationCoachExplanation"));
    auto* calibration_coach_boundary = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceCalibrationCoachBoundary"));
    auto* calibration_review_boundary = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceCalibrationReviewBoundary"));
    auto* calibration_packet_ready = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceCalibrationPacketReady"));
    auto* taste_privacy = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTastePrivacy"));
    auto* taste_action_message = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTasteActionMessage"));
    auto* two_speed_taste_card = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTwoSpeedTasteCard"));
    auto* two_speed_taste_headline = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTwoSpeedTasteHeadline"));
    auto* two_speed_taste_authority = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTwoSpeedTasteAuthority"));
    auto* two_speed_taste_forget = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTwoSpeedTasteForgetButton"));
    auto* assignment_reflection_card = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceAssignmentReflectionCard"));
    auto* assignment_reflection_headline = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceAssignmentReflectionHeadline"));
    auto* assignment_reflection_evidence = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceAssignmentReflectionEvidence"));
    auto* assignment_reflection_boundary = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceAssignmentReflectionBoundary"));
    auto* assignment_reflection_shape = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceAssignmentReflectionShapeButton"));
    auto* assignment_reflection_dismiss = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceAssignmentReflectionDismissButton"));
    auto* north_star_card = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceNorthStarCard"));
    auto* north_star_brief = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceNorthStarBrief"));
    auto* north_star_editor = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceNorthStarEditor"));
    auto* north_star_edit = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceNorthStarEditButton"));
    auto* north_star_save = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceNorthStarSaveButton"));
    auto* north_star_cancel = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceNorthStarCancelButton"));
    auto* north_star_forget = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceNorthStarForgetButton"));
    auto* north_star_draft_receipt = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceNorthStarDraftReceipt"));
    auto* north_star_draft_brief = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceNorthStarDraftBrief"));
    auto* north_star_draft_proof = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceNorthStarDraftProof"));
    auto* north_star_draft_boundary = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceNorthStarDraftBoundary"));
    if (taste_mirror_card == nullptr || taste_summary == nullptr ||
        taste_name == nullptr || taste_rename_button == nullptr ||
        taste_evidence_row == nullptr || taste_evidence_thumbnail == nullptr ||
        taste_forget_button == nullptr ||
        taste_calibration_import == nullptr || taste_passport_export == nullptr ||
        taste_passport_import == nullptr || taste_passport_preview_card == nullptr ||
        taste_passport_preview_summary == nullptr ||
        taste_passport_preview_boundary == nullptr ||
        taste_passport_cancel == nullptr || taste_passport_confirm == nullptr ||
        taste_calibration_status == nullptr ||
        calibration_review_card == nullptr ||
        calibration_review_button == nullptr ||
        calibration_finalize_button == nullptr ||
        calibration_clipboard_finalize_button == nullptr ||
        calibration_coach_card == nullptr ||
        calibration_coach_headline == nullptr ||
        calibration_coach_explanation == nullptr ||
        calibration_coach_boundary == nullptr ||
        calibration_review_boundary == nullptr ||
        calibration_packet_ready == nullptr ||
        taste_privacy == nullptr || taste_action_message == nullptr ||
        two_speed_taste_card == nullptr ||
        two_speed_taste_headline == nullptr ||
        two_speed_taste_authority == nullptr ||
        two_speed_taste_forget == nullptr ||
        assignment_reflection_card == nullptr ||
        assignment_reflection_headline == nullptr ||
        assignment_reflection_evidence == nullptr ||
        assignment_reflection_boundary == nullptr ||
        assignment_reflection_shape == nullptr ||
        assignment_reflection_dismiss == nullptr ||
        north_star_card == nullptr || north_star_brief == nullptr ||
        north_star_editor == nullptr || north_star_edit == nullptr ||
        north_star_save == nullptr || north_star_cancel == nullptr ||
        north_star_forget == nullptr ||
        north_star_draft_receipt == nullptr ||
        north_star_draft_brief == nullptr || north_star_draft_proof == nullptr ||
        north_star_draft_boundary == nullptr ||
        !wait_for_condition(
            [taste_mirror_card, taste_summary, taste_name,
             taste_evidence_row, taste_evidence_thumbnail,
             taste_forget_button, taste_calibration_import,
             taste_passport_export, taste_passport_import,
             taste_passport_preview_card,
             taste_calibration_status, calibration_review_card,
             calibration_review_button, calibration_finalize_button,
             calibration_clipboard_finalize_button,
             calibration_coach_card, calibration_coach_headline,
             calibration_coach_explanation, calibration_coach_boundary,
             calibration_review_boundary,
             taste_privacy, two_speed_taste_card,
             two_speed_taste_headline, two_speed_taste_authority,
             two_speed_taste_forget,
             assignment_reflection_card, assignment_reflection_headline,
             assignment_reflection_evidence,
             assignment_reflection_boundary, assignment_reflection_shape,
             assignment_reflection_dismiss,
             north_star_card, north_star_brief, north_star_editor,
             north_star_edit, north_star_save, north_star_forget,
             north_star_draft_receipt]() {
              return item_visible(taste_mirror_card) &&
                     taste_mirror_card->height() >= 210.0 &&
                     taste_name->property("text").toString() ==
                         QStringLiteral("Wedding Story") &&
                     taste_summary->property("text").toString().contains(
                         QStringLiteral("protect dress highlights")) &&
                     item_visible(taste_evidence_row) &&
                     item_visible(taste_evidence_thumbnail) &&
                     !taste_evidence_thumbnail->property("source")
                          .toUrl()
                          .isEmpty() &&
                     taste_forget_button->property("text").toString() ==
                         QStringLiteral("Forget") &&
                     taste_calibration_import->property("text").toString() ==
                         QStringLiteral("Replace profile") &&
                     taste_passport_export->property("visible").toBool() &&
                     taste_passport_export->property("text").toString() ==
                         QStringLiteral("Export") &&
                     taste_passport_import->property("visible").toBool() &&
                     taste_passport_import->property("text").toString() ==
                         QStringLiteral("Import") &&
                     !item_visible(taste_passport_preview_card) &&
                     taste_calibration_status->property("text").toString().contains(
                         QStringLiteral("2 blind cases")) &&
                     item_visible(calibration_review_card) &&
                     calibration_review_button->property("text").toString() ==
                         QStringLiteral("Review proofs") &&
                     calibration_finalize_button->property("text").toString() ==
                         QStringLiteral("Finish ratings") &&
                     !item_visible(calibration_clipboard_finalize_button) &&
                     item_visible(calibration_coach_card) &&
                     calibration_coach_headline->property("text").toString().contains(
                         QStringLiteral("too confident")) &&
                     calibration_coach_explanation->property("text").toString().contains(
                         QStringLiteral("Naturalness")) &&
                     calibration_coach_explanation->property("text").toString().contains(
                         QStringLiteral("too warm")) &&
                     calibration_coach_boundary->property("text").toString().contains(
                         QStringLiteral("PHOTOGRAPHER: DO NOT DELIVER")) &&
                     calibration_coach_boundary->property("text").toString().contains(
                         QStringLiteral("NO AUTO TRAINING")) &&
                     calibration_review_boundary
                         ->property("text")
                         .toString()
                         .contains(QStringLiteral("AGENT CANNOT RATE")) &&
                     taste_privacy->property("text").toString().contains(
                         QStringLiteral("CATALOG IDS")) &&
                     taste_privacy->property("text").toString().contains(
                         QStringLiteral("NO IMAGE BYTES")) &&
                     taste_privacy->property("text").toString().contains(
                         QStringLiteral("EDITS UNCHANGED")) &&
                     item_visible(two_speed_taste_card) &&
                     two_speed_taste_headline->property("text").toString().contains(
                         QStringLiteral("restrained first look")) &&
                     two_speed_taste_authority->property("text").toString().contains(
                         QStringLiteral("CURRENT REQUEST WINS")) &&
                     two_speed_taste_authority->property("text").toString().contains(
                         QStringLiteral("NO AUTO SELECT")) &&
                     two_speed_taste_forget->property("text").toString() ==
                         QStringLiteral("Forget signal") &&
                     item_visible(assignment_reflection_card) &&
                     assignment_reflection_headline
                         ->property("text")
                         .toString()
                         .contains(QStringLiteral("restrained first look")) &&
                     assignment_reflection_evidence
                         ->property("text")
                         .toString()
                         .contains(QStringLiteral("3 of 4 confirmed choices")) &&
                     assignment_reflection_boundary
                         ->property("text")
                         .toString() ==
                         QStringLiteral("NOTHING REMEMBERED YET") &&
                     assignment_reflection_shape->property("text").toString() ==
                         QStringLiteral("Shape North Star") &&
                     assignment_reflection_dismiss->property("text").toString() ==
                         QStringLiteral("Not now") &&
                     item_visible(north_star_card) &&
                     item_visible(north_star_brief) &&
                     north_star_brief->property("text").toString().contains(
                         QStringLiteral("one enduring promise")) &&
                     north_star_edit->property("text").toString() ==
                         QStringLiteral("Add") &&
                     !item_visible(north_star_editor) &&
                     !item_visible(north_star_save) &&
                     !item_visible(north_star_forget) &&
                     !item_visible(north_star_draft_receipt);
            },
            1500)) {
      return fail("AI Edit taste mirror should show real project-local evidence, privacy boundaries, and direct forgetting controls");
    }
    const auto reflection_north_star_proposal = QStringLiteral(
        "Keep edits restrained and direct unless the photograph or current request clearly benefits from deeper refinement.");
    if (!QMetaObject::invokeMethod(assignment_reflection_shape, "click") ||
        !wait_for_condition(
            [north_star_editor, north_star_save, north_star_cancel,
             taste_action_message, reflection_north_star_proposal]() {
              return item_visible(north_star_editor) &&
                     north_star_editor->property("text").toString() ==
                         reflection_north_star_proposal &&
                     item_visible(north_star_save) &&
                     north_star_save->property("enabled").toBool() &&
                     item_visible(north_star_cancel) &&
                     taste_action_message->property("text").toString().contains(
                         QStringLiteral("Nothing is remembered until you press Save North Star"));
            },
            1000)) {
      return fail("AI Edit assignment reflection should prefill the existing North Star editor without saving memory");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-assignment-reflection-north-star-draft"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceAssignmentReflectionCard"),
                QStringLiteral("agentWorkspaceAssignmentReflectionHeadline"),
                QStringLiteral("agentWorkspaceAssignmentReflectionBoundary"),
                QStringLiteral("agentWorkspaceNorthStarCard"),
                QStringLiteral("agentWorkspaceNorthStarEditor"),
                QStringLiteral("agentWorkspaceNorthStarCancelButton"),
                QStringLiteral("agentWorkspaceNorthStarSaveButton"),
                QStringLiteral("agentWorkspaceTasteActionMessage"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-assignment-taste-reflection-v1")},
                {QStringLiteral("proposalOrigin"),
                 QStringLiteral("aggregate-confirmed-proved-version-choices")},
                {QStringLiteral("sourceChoiceCount"), 4},
                {QStringLiteral("sourcePreferredVersion"),
                 QStringLiteral("first-look")},
                {QStringLiteral("prefillsExistingNorthStarEditor"), true},
                {QStringLiteral("requiresExplicitNorthStarSave"), true},
                {QStringLiteral("durableMemoryWritten"), false},
                {QStringLiteral("currentRequestWins"), true},
                {QStringLiteral("activeDraftUnaffected"), true},
                {QStringLiteral("storesImageBytes"), false},
                {QStringLiteral("storesSourcePaths"), false},
            })) {
      return fail("AI Edit visual proof should capture the unsaved assignment reflection proposal in the existing North Star editor");
    }
    if (!QMetaObject::invokeMethod(north_star_cancel, "click") ||
        !wait_for_condition(
            [north_star_editor, assignment_reflection_card,
             ai_edit_harness_object = ai_edit_harness.data()]() {
              return !item_visible(north_star_editor) &&
                     item_visible(assignment_reflection_card) &&
                     ai_edit_harness_object
                         ->property("lastNorthStarSave")
                         .toString()
                         .isEmpty();
            },
            1000)) {
      return fail("AI Edit should discard the unsaved reflection proposal without writing project memory");
    }
    if (!QMetaObject::invokeMethod(
            workspace_review,
            "exportPhotographerTastePassport",
            Qt::DirectConnection,
            Q_ARG(QVariant,
                  QVariant(QStringLiteral("file:///local/taste-passport.json")))) ||
        !wait_for_condition(
            [taste_action_message]() {
              const auto message =
                  taste_action_message->property("text").toString();
              return item_visible(taste_action_message) &&
                     message.contains(QStringLiteral("Taste Passport saved")) &&
                     message.contains(QStringLiteral("no photos or paths"));
            },
            1000) ||
        !QMetaObject::invokeMethod(
            workspace_review,
            "importPhotographerTastePassport",
            Qt::DirectConnection,
            Q_ARG(QVariant,
                  QVariant(QStringLiteral("file:///local/taste-passport.json")))) ||
        !wait_for_condition(
            [taste_action_message, taste_passport_preview_card,
             taste_passport_preview_summary,
             taste_passport_preview_boundary,
             taste_passport_cancel, taste_passport_confirm]() {
              const auto message =
                  taste_action_message->property("text").toString();
              return item_visible(taste_action_message) &&
                     message.contains(QStringLiteral("Taste Passport verified")) &&
                     item_visible(taste_passport_preview_card) &&
                     taste_passport_preview_summary
                         ->property("text")
                         .toString()
                         .contains(QStringLiteral("2 blind cases")) &&
                     taste_passport_preview_boundary
                         ->property("text")
                         .toString()
                         .contains(QStringLiteral("REPLACES CURRENT CALIBRATION")) &&
                     taste_passport_cancel->property("text").toString() ==
                         QStringLiteral("Not now") &&
                     taste_passport_confirm->property("text").toString() ==
                         QStringLiteral("Use for this project");
            },
            1000) ||
        !capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-taste-passport-preview"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceTastePassportPreviewCard"),
                QStringLiteral("agentWorkspaceTastePassportPreviewSummary"),
                QStringLiteral("agentWorkspaceTastePassportPreviewBoundary"),
                QStringLiteral("agentWorkspaceTastePassportCancelButton"),
                QStringLiteral("agentWorkspaceTastePassportConfirmButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-photographer-taste-passport-preview-v1")},
                {QStringLiteral("verifiedBeforeConfirmation"), true},
                {QStringLiteral("confirmationRequired"), true},
                {QStringLiteral("wouldReplaceExisting"), true},
                {QStringLiteral("previewDigestPinned"), true},
                {QStringLiteral("targetProjectOnly"), true},
                {QStringLiteral("activeDraftUnaffected"), true},
                {QStringLiteral("durableMemoryWritten"), false},
            }) ||
        !QMetaObject::invokeMethod(
            workspace_review,
            "confirmPhotographerTastePassport",
            Qt::DirectConnection) ||
        !wait_for_condition(
            [taste_action_message, taste_passport_preview_card]() {
              const auto message =
                  taste_action_message->property("text").toString();
              return item_visible(taste_action_message) &&
                     message.contains(QStringLiteral("Taste Passport active for this project")) &&
                     message.contains(QStringLiteral("no photos or paths imported")) &&
                     !item_visible(taste_passport_preview_card);
            },
            1000) ||
        !capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-taste-passport-ready"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceTasteMirrorCard"),
                QStringLiteral("agentWorkspaceCalibrationReviewCard"),
                QStringLiteral("agentWorkspaceTastePassportExportButton"),
                QStringLiteral("agentWorkspaceTastePassportImportButton"),
                QStringLiteral("agentWorkspaceTasteActionMessage"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-photographer-taste-passport-v1")},
                {QStringLiteral("explicitImportRequired"), true},
                {QStringLiteral("targetProjectOnly"), true},
                {QStringLiteral("portableCalibrationOnly"), true},
                {QStringLiteral("containsProjectName"), false},
                {QStringLiteral("storesImageBytes"), false},
                {QStringLiteral("storesSourcePaths"), false},
                {QStringLiteral("automaticModelTrainingAllowed"), false},
                {QStringLiteral("automaticThresholdMutationAllowed"), false},
                {QStringLiteral("professionalReadinessClaimed"), false},
            })) {
      return fail("AI Edit should make its verified, project-only Taste Passport export and explicit import contract visible");
    }
    if (!QMetaObject::invokeMethod(
            workspace_review,
            "openPhotographerCalibrationReview",
            Qt::DirectConnection,
            Q_ARG(QVariant,
                  QVariant(QStringLiteral("file:///local/review/index.html")))) ||
        !wait_for_condition(
            [taste_action_message, calibration_packet_ready,
             calibration_clipboard_finalize_button]() {
              const auto message =
                  taste_action_message->property("text").toString();
              return item_visible(taste_action_message) &&
                     message.contains(QStringLiteral("9 blind proofs opened")) &&
                     message.contains(QStringLiteral("human-only")) &&
                     message.contains(QStringLiteral("Finish copied ratings")) &&
                     item_visible(calibration_packet_ready) &&
                     item_visible(calibration_clipboard_finalize_button) &&
                     calibration_clipboard_finalize_button
                             ->property("text")
                             .toString() ==
                         QStringLiteral("Finish copied ratings") &&
                     calibration_packet_ready
                         ->property("text")
                         .toString()
                         .contains(QStringLiteral("COPY FINISH IS ONE CLICK"));
            },
            1000) ||
        workspace_review->property("activeCalibrationReviewPacketUrl")
                .toString() !=
            QStringLiteral("file:///local/review/index.html") ||
        workspace_review->property("activeCalibrationReviewPacketDigest")
                .toString() !=
            QStringLiteral(
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") ||
        workspace_review->property("activeCalibrationReviewProjectKey")
                .toString() != QStringLiteral("Wedding Delivery") ||
        !capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-calibration-packet-ready"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceCalibrationReviewCard"),
                QStringLiteral("agentWorkspaceCalibrationReviewButton"),
                QStringLiteral("agentWorkspaceCalibrationFinalizeButton"),
                QStringLiteral("agentWorkspaceCalibrationClipboardFinalizeButton"),
                QStringLiteral("agentWorkspaceCalibrationPacketReady"),
                QStringLiteral("agentWorkspaceCalibrationReviewBoundary"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral(
                     "agent-photographer-calibration-session-handoff-v1")},
                {QStringLiteral("verifiedPacketReady"), true},
                {QStringLiteral("ratingsFileSelectionsRequired"), 0},
                {QStringLiteral("signedRatingsClipboardHandoff"), true},
                {QStringLiteral("clipboardClearedAfterSuccess"), true},
                {QStringLiteral("jsonFileFallbackAvailable"), true},
                {QStringLiteral("packetReferenceSessionOnly"), true},
                {QStringLiteral("packetReferenceProjectBound"), true},
                {QStringLiteral("durablePacketPathStored"), false},
                {QStringLiteral("ratingsReverifiedBeforeApply"), true},
                {QStringLiteral("machineCanCompleteRatings"), false},
            })) {
      return fail("AI Edit should hand a verified proof packet to the photographer while keeping ratings human-only");
    }
    if (!QMetaObject::invokeMethod(
            workspace_review,
            "finishCopiedPhotographerCalibrationRatings",
            Qt::DirectConnection) ||
        !wait_for_condition(
            [taste_action_message, ai_edit_harness_object, workspace_review,
             calibration_packet_ready]() {
              const auto message =
                  taste_action_message->property("text").toString();
              return item_visible(taste_action_message) &&
                     message.contains(QStringLiteral("9 copied blind ratings verified")) &&
                     message.contains(QStringLiteral("calibration active")) &&
                     message.contains(QStringLiteral("clipboard cleared")) &&
                     message.contains(QStringLiteral("source manifest kept unchanged")) &&
                     ai_edit_harness_object
                             ->property("calibrationFinalizeCount")
                             .toInt() == 1 &&
                     ai_edit_harness_object
                             ->property("calibrationClipboardFinalizeCount")
                             .toInt() == 1 &&
                     ai_edit_harness_object
                             ->property("lastCalibrationRatingsPath")
                             .toString() ==
                         QStringLiteral("clipboard") &&
                     ai_edit_harness_object
                             ->property("lastCalibrationPacketPath")
                             .toString() ==
                         QStringLiteral("file:///local/review/index.html") &&
                     workspace_review
                             ->property("activeCalibrationReviewPacketUrl")
                             .toString()
                             .isEmpty() &&
                     !item_visible(calibration_packet_ready);
            },
            1000)) {
      return fail("AI Edit should finish copied human ratings from the session-verified packet without a file selection and disclose the cleared clipboard and preserved source boundaries");
    }
    const auto north_star_text = QStringLiteral(
        "Keep this wedding intimate and documentary; protect skin, highlights, and unposed moments across every delivery.");
    if (!QMetaObject::invokeMethod(north_star_edit, "click") ||
        !wait_for_condition(
            [north_star_editor, north_star_save]() {
              return item_visible(north_star_editor) &&
                     item_visible(north_star_save);
            },
            1000)) {
      return fail("AI Edit should open one compact Project North Star editor from the Taste Mirror");
    }
    workspace_review->setProperty("northStarDraftText", north_star_text);
    if (!wait_for_condition(
            [north_star_editor, north_star_save, north_star_text]() {
              return north_star_editor->property("text").toString() ==
                         north_star_text &&
                     north_star_save->property("enabled").toBool();
            },
            1000) ||
        !QMetaObject::invokeMethod(north_star_save, "click")) {
      return fail("AI Edit should save one explicit Project North Star from the compact editor");
    }
    if (!wait_for_condition(
            [ai_edit_harness_object = ai_edit_harness.data(),
             north_star_brief, north_star_editor, north_star_edit,
             north_star_forget, taste_action_message, north_star_text]() {
              return ai_edit_harness_object
                             ->property("lastNorthStarSave")
                             .toString() == north_star_text &&
                     item_visible(north_star_brief) &&
                     north_star_brief->property("text").toString() ==
                         north_star_text &&
                     !item_visible(north_star_editor) &&
                     north_star_edit->property("text").toString() ==
                         QStringLiteral("Edit") &&
                     item_visible(north_star_forget) &&
                     north_star_forget->property("text").toString() ==
                         QStringLiteral("Forget") &&
                     item_visible(taste_action_message) &&
                     taste_action_message->property("text").toString().contains(
                         QStringLiteral("current request, style, and safety rules still win"));
            },
            1000)) {
      return fail("AI Edit should save and visibly restate one project-local North Star without changing the active draft");
    }
    ai_edit_harness->setProperty("showNorthStarCompass", true);
    workspace_review->setProperty(
        "tasteActionMessage",
        QStringLiteral(
            "New draft used this North Star in planning and proof review. Final fit is still your call."));
    if (!wait_for_condition(
            [north_star_draft_receipt, north_star_draft_brief,
             north_star_draft_proof, north_star_draft_boundary,
             taste_action_message, north_star_text]() {
              return item_visible(north_star_draft_receipt) &&
                     north_star_draft_brief->property("text").toString() ==
                         north_star_text &&
                     north_star_draft_proof->property("text").toString().contains(
                         QStringLiteral("final fit is your call")) &&
                     north_star_draft_boundary->property("text").toString() ==
                         QStringLiteral("USED IN PLAN · CURRENT REQUEST LED") &&
                     taste_action_message->property("text").toString().contains(
                         QStringLiteral("New draft used this North Star"));
            },
            1000)) {
      return fail("AI Edit should show a truthful assignment-compass receipt on the next North Star-informed draft");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-project-north-star"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceTasteMirrorCard"),
                QStringLiteral("agentWorkspaceNorthStarCard"),
                QStringLiteral("agentWorkspaceNorthStarBrief"),
                QStringLiteral("agentWorkspaceNorthStarEditButton"),
                QStringLiteral("agentWorkspaceNorthStarForgetButton"),
                QStringLiteral("agentWorkspaceNorthStarDraftReceipt"),
                QStringLiteral("agentWorkspaceNorthStarDraftBrief"),
                QStringLiteral("agentWorkspaceNorthStarDraftProof"),
                QStringLiteral("agentWorkspaceTasteActionMessage"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-project-north-star-memory-v1")},
                {QStringLiteral("projectKey"),
                 QStringLiteral("Wedding Delivery")},
                {QStringLiteral("projectLocal"), true},
                {QStringLiteral("explicitUserAuthored"), true},
                {QStringLiteral("guidesFuturePlanning"), true},
                {QStringLiteral("guidesProofCritique"), true},
                {QStringLiteral("draftReceiptContract"),
                 QStringLiteral("agent-project-north-star-draft-receipt-v1")},
                {QStringLiteral("planningConsidered"), true},
                {QStringLiteral("proofCritiqueConsidered"), true},
                {QStringLiteral("alignmentClaimed"), false},
                {QStringLiteral("requiresPhotographerJudgment"), true},
                {QStringLiteral("currentRequestWins"), true},
                {QStringLiteral("styleAndSafetyWin"), true},
                {QStringLiteral("requiresFreshDraft"), true},
                {QStringLiteral("activeDraftUnaffected"), true},
                {QStringLiteral("directNumericRecipeAuthority"), false},
                {QStringLiteral("automaticCullAuthority"), false},
                {QStringLiteral("storesAssetIds"), false},
                {QStringLiteral("storesImageBytes"), false},
                {QStringLiteral("storesSourcePaths"), false},
            })) {
      return fail("AI Edit visual proof should capture the explicit Project North Star and its precedence boundary");
    }
    if (!QMetaObject::invokeMethod(north_star_forget, "click") ||
        !wait_for_condition(
            [north_star_forget, taste_action_message]() {
              return north_star_forget->property("text").toString() ==
                         QStringLiteral("Confirm forget") &&
                     taste_action_message->property("text").toString().contains(
                         QStringLiteral("existing edits stay unchanged"));
            },
            1000) ||
        !QMetaObject::invokeMethod(north_star_forget, "click") ||
        !wait_for_condition(
            [north_star_brief, north_star_edit, north_star_forget,
             workspace_review]() {
              return north_star_brief->property("text").toString().contains(
                         QStringLiteral("one enduring promise")) &&
                     north_star_edit->property("text").toString() ==
                         QStringLiteral("Add") &&
                     !item_visible(north_star_forget) &&
                     workspace_review->property("draftReady").toBool();
            },
            1000)) {
      return fail("AI Edit should require confirmation to forget only future North Star guidance while leaving the active draft intact");
    }
    if (!QMetaObject::invokeMethod(taste_rename_button, "click")) {
      return fail("AI Edit taste mirror should open its inline project-look rename control");
    }
    auto* taste_rename_editor = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTasteRenameEditor"));
    auto* taste_rename_save = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTasteRenameSaveButton"));
    workspace_review->setProperty(
        "tasteRenameText", QStringLiteral("Golden Hour Story"));
    if (taste_rename_editor == nullptr || taste_rename_save == nullptr ||
        !wait_for_condition(
            [taste_rename_editor, taste_rename_save]() {
              return item_visible(taste_rename_editor) &&
                     taste_rename_editor->property("text").toString() ==
                         QStringLiteral("Golden Hour Story") &&
                     taste_rename_save->property("enabled").toBool();
            },
            1000) ||
        !QMetaObject::invokeMethod(taste_rename_save, "click") ||
        !wait_for_condition(
            [ai_edit_harness_object = ai_edit_harness.data(),
             taste_name, taste_rename_editor]() {
              return ai_edit_harness_object->property("lastTasteRename").toString() ==
                         QStringLiteral("Golden Hour Story") &&
                     taste_name->property("text").toString() ==
                         QStringLiteral("Golden Hour Story") &&
                     !item_visible(taste_rename_editor);
            },
            1000)) {
      return fail("AI Edit taste mirror should rename only the active project's learned look inline");
    }
    if (!QMetaObject::invokeMethod(taste_forget_button, "click") ||
        !wait_for_condition(
            [taste_forget_button, taste_action_message]() {
              return taste_forget_button->property("text").toString() ==
                         QStringLiteral("Confirm forget") &&
                     item_visible(taste_action_message) &&
                     taste_action_message->property("text").toString().contains(
                         QStringLiteral("only changes what I remember"));
            },
            1000)) {
      return fail("forgetting taste evidence should require an explicit second confirmation and disclose its narrow effect");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-project-taste-mirror"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceTasteMirrorCard"),
                QStringLiteral("agentWorkspaceTasteName"),
                QStringLiteral("agentWorkspaceTasteRenameButton"),
                QStringLiteral("agentWorkspaceTasteSummary"),
                QStringLiteral("agentWorkspaceTasteEvidenceRow"),
                QStringLiteral("agentWorkspaceTasteEvidenceThumbnail"),
                QStringLiteral("agentWorkspaceTasteForgetButton"),
                QStringLiteral("agentWorkspaceTwoSpeedTasteCard"),
                QStringLiteral("agentWorkspaceTwoSpeedTasteHeadline"),
                QStringLiteral("agentWorkspaceTwoSpeedTasteAuthority"),
                QStringLiteral("agentWorkspaceTwoSpeedTasteForgetButton"),
                QStringLiteral("agentWorkspaceAssignmentReflectionCard"),
                QStringLiteral("agentWorkspaceAssignmentReflectionHeadline"),
                QStringLiteral("agentWorkspaceAssignmentReflectionEvidence"),
                QStringLiteral("agentWorkspaceAssignmentReflectionBoundary"),
                QStringLiteral("agentWorkspaceAssignmentReflectionDismissButton"),
                QStringLiteral("agentWorkspaceAssignmentReflectionShapeButton"),
                QStringLiteral("agentWorkspaceTasteCalibrationImportButton"),
                QStringLiteral("agentWorkspaceTasteCalibrationStatus"),
                QStringLiteral("agentWorkspaceCalibrationReviewCard"),
                QStringLiteral("agentWorkspaceCalibrationReviewButton"),
                QStringLiteral("agentWorkspaceCalibrationFinalizeButton"),
                QStringLiteral("agentWorkspaceCalibrationCoachCard"),
                QStringLiteral("agentWorkspaceCalibrationCoachHeadline"),
                QStringLiteral("agentWorkspaceCalibrationCoachExplanation"),
                QStringLiteral("agentWorkspaceCalibrationCoachBoundary"),
                QStringLiteral("agentWorkspaceCalibrationReviewBoundary"),
                QStringLiteral("agentWorkspaceTastePrivacy"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-project-taste-mirror-v2")},
                {QStringLiteral("projectKey"),
                 QStringLiteral("Wedding Delivery")},
                {QStringLiteral("projectLocal"), true},
                {QStringLiteral("name"),
                 QStringLiteral("Golden Hour Story")},
                {QStringLiteral("inlineRename"), true},
                {QStringLiteral("visualExemplarsByCatalogId"), true},
                {QStringLiteral("maturity"), QStringLiteral("calibrated")},
                {QStringLiteral("evidenceCount"), 4},
                {QStringLiteral("twoSpeedTasteContract"),
                 QStringLiteral("agent-two-speed-taste-signal-v1")},
                {QStringLiteral("twoSpeedChoiceCount"), 4},
                {QStringLiteral("twoSpeedFirstLookCount"), 3},
                {QStringLiteral("twoSpeedDeepReviewCount"), 1},
                {QStringLiteral("twoSpeedPreferredVersion"),
                 QStringLiteral("first-look")},
                {QStringLiteral("twoSpeedInfluencesPlanning"), true},
                {QStringLiteral("twoSpeedCurrentRequestWins"), true},
                {QStringLiteral("twoSpeedAutomaticSelectionAllowed"), false},
                {QStringLiteral("twoSpeedAutomaticCommitAllowed"), false},
                {QStringLiteral("twoSpeedStoresImageBytes"), false},
                {QStringLiteral("twoSpeedStoresProofPaths"), false},
                {QStringLiteral("assignmentReflectionContract"),
                 QStringLiteral("agent-assignment-taste-reflection-v1")},
                {QStringLiteral("assignmentReflectionObservationCount"), 2},
                {QStringLiteral("assignmentReflectionSourceChoiceCount"), 4},
                {QStringLiteral("assignmentReflectionDurableMemoryWritten"), false},
                {QStringLiteral("assignmentReflectionRequiresExplicitNorthStarSave"), true},
                {QStringLiteral("assignmentReflectionAutomaticMemoryWriteAllowed"), false},
                {QStringLiteral("assignmentReflectionAutomaticRecipeInfluenceAllowed"), false},
                {QStringLiteral("assignmentReflectionDismissalScope"),
                 QStringLiteral("session-only")},
                {QStringLiteral("assignmentReflectionStoresImageBytes"), false},
                {QStringLiteral("assignmentReflectionStoresSourcePaths"), false},
                {QStringLiteral("photographerCalibrationAvailable"), true},
                {QStringLiteral("photographerCalibrationCaseCount"), 2},
                {QStringLiteral("calibrationReviewLaunchContract"),
                 QStringLiteral("agent-photographer-calibration-review-launch-v1")},
                {QStringLiteral("calibrationNativeFinalizeContract"),
                 QStringLiteral("agent-photographer-calibration-native-finalize-v1")},
                {QStringLiteral("calibrationCoachContract"),
                 QStringLiteral("agent-photographer-calibration-coach-v1")},
                {QStringLiteral("calibrationCoachLessonCount"), 1},
                {QStringLiteral("calibrationCoachPrimaryOutcome"),
                 QStringLiteral("false-confidence")},
                {QStringLiteral("photographerDecisionIsAuthority"), true},
                {QStringLiteral("automaticModelTrainingAllowed"), false},
                {QStringLiteral("packetVerifiedBeforeOpen"), true},
                {QStringLiteral("ratingsVerifiedBeforeApply"), true},
                {QStringLiteral("machineCanCompleteRatings"), false},
                {QStringLiteral("sourceManifestOverwritten"), false},
                {QStringLiteral("automaticThresholdMutationAllowed"), false},
                {QStringLiteral("photographerCalibrationInfluencesPlanning"), true},
                {QStringLiteral("photographerCalibrationInfluencesProofCritique"), true},
                {QStringLiteral("numericThresholdOverridesAllowed"), false},
                {QStringLiteral("professionalReadinessClaimed"), false},
                {QStringLiteral("evidenceKinds"),
                 QJsonArray{
                     QStringLiteral("approved-correction"),
                     QStringLiteral("approved-look"),
                     QStringLiteral("visual-referent"),
                     QStringLiteral("rejected-draft"),
                 }},
                {QStringLiteral("forgetRequiresConfirmation"), true},
                {QStringLiteral("activeDraftUnaffected"), true},
                {QStringLiteral("storesImageBytes"), false},
                {QStringLiteral("storesSourcePaths"), false},
            })) {
      return fail("AI Edit visual proof should capture the editable project taste mirror");
    }
    if (!QMetaObject::invokeMethod(two_speed_taste_forget, "click") ||
        !wait_for_condition(
            [two_speed_taste_forget, taste_action_message]() {
              return two_speed_taste_forget->property("text").toString() ==
                         QStringLiteral("Confirm forget") &&
                     item_visible(taste_action_message) &&
                     taste_action_message->property("text").toString().contains(
                         QStringLiteral("only the proved-version preference signal"));
            },
            1000)) {
      return fail("AI Edit should arm a narrow confirmed forget for proved-version preference metadata");
    }
    if (!QMetaObject::invokeMethod(two_speed_taste_forget, "click") ||
        !wait_for_condition(
            [two_speed_taste_card, taste_evidence_row, workspace_review,
             taste_action_message]() {
              return !item_visible(two_speed_taste_card) &&
                     item_visible(taste_evidence_row) &&
                     workspace_review->property("draftReady").toBool() &&
                     taste_action_message->property("text").toString().contains(
                         QStringLiteral("Approved looks and existing edits stayed unchanged"));
            },
            1000)) {
      std::cerr << "two_speed_taste_forget card_visible="
                << item_visible(two_speed_taste_card)
                << " evidence_visible=" << item_visible(taste_evidence_row)
                << " draft_ready="
                << workspace_review->property("draftReady").toBool()
                << " message="
                << taste_action_message->property("text").toString().toStdString()
                << '\n';
      return fail("AI Edit should require confirmation to forget only proved-version preference metadata while retaining approved looks and the active edit");
    }
    taste_forget_button = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTasteForgetButton"));
    if (taste_forget_button == nullptr) {
      return fail("AI Edit should retain approved-look forgetting controls after the separate proved-version signal is forgotten");
    }
    if (!QMetaObject::invokeMethod(taste_forget_button, "click") ||
        !wait_for_condition(
            [taste_forget_button]() {
              return taste_forget_button->property("text").toString() ==
                     QStringLiteral("Confirm forget");
            },
            1000)) {
      return fail("AI Edit should independently re-arm approved-look forgetting after the separate proved-version signal changes");
    }
    if (!QMetaObject::invokeMethod(taste_forget_button, "click") ||
        !wait_for_condition(
            [workspace_review]() {
              QVariant evidence_value;
              if (!QMetaObject::invokeMethod(
                      workspace_review,
                      "tasteEvidence",
                      Qt::DirectConnection,
                      Q_RETURN_ARG(QVariant, evidence_value))) {
                return false;
              }
              return evidence_value.toList().size() == 3 &&
                     workspace_review->property("draftReady").toBool();
            },
            1000) ||
        !QMetaObject::invokeMethod(taste_mirror_button, "click")) {
      return fail("confirmed forgetting should remove only the chosen taste item while leaving the reversible draft intact");
    }
    ai_edit_harness->setProperty("fastDeadlineReached", false);
    ai_edit_harness->setProperty("showLocalPlanning", true);
    ai_edit_harness->setProperty("showProgressivePreview", true);
    auto* progressive_preview_receipt = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceProgressivePreviewReceipt"));
    auto* keep_fast_draft_button = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceKeepValidatedFastDraftButton"));
    auto* local_planning_label = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceLocalPlanningHeartbeatLabel"));
    if (progressive_preview_receipt == nullptr ||
        keep_fast_draft_button == nullptr ||
        local_planning_label == nullptr ||
        !wait_for_condition(
            [progressive_preview_receipt, keep_fast_draft_button,
             local_planning_label]() {
              return item_visible(progressive_preview_receipt) &&
                     item_visible(keep_fast_draft_button) &&
                     item_visible(local_planning_label) &&
                     progressive_preview_receipt->property("text").toString().contains(
                         QStringLiteral("FAST PROOF PASSED")) &&
                     progressive_preview_receipt->property("text").toString().contains(
                         QStringLiteral("USE NOW OR WAIT")) &&
                     local_planning_label->property("text").toString().contains(
                         QStringLiteral("PROVED PREVIEW")) &&
                     local_planning_label->property("text").toString().contains(
                         QStringLiteral("DEEP REVIEW TARGET ")) &&
                     progressive_preview_receipt->property("text").toString().contains(
                         QStringLiteral("COMMIT LOCKED"));
            },
            1500)) {
      return fail("AI Edit should offer the proved reversible draft immediately while showing the continuing deep-review target");
    }
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-validated-fast-draft-countdown"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceSpeechBubble"),
                QStringLiteral("agentWorkspaceProgressivePreviewReceipt"),
                QStringLiteral("agentWorkspaceLocalPlanningHeartbeatLabel"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-proved-fast-choice-v1")},
                {QStringLiteral("fastProofContract"),
                 QStringLiteral("agent-fast-draft-independent-proof-v1")},
                {QStringLiteral("hardwareDeadlineContract"),
                 QStringLiteral("agent-fast-draft-hardware-deadline-v1")},
                {QStringLiteral("hardwareTier"), QStringLiteral("balanced")},
                {QStringLiteral("deepModelDeadlineMs"), 30000},
                {QStringLiteral("deadlineSeconds"), 30},
                {QStringLiteral("countdownUsesLiveElapsedClock"), true},
                {QStringLiteral("fastValidationPassed"), true},
                {QStringLiteral("deepDeadlineReached"), false},
                {QStringLiteral("keepFastDraftAvailable"), true},
                {QStringLiteral("keepRequiresDeadline"), false},
                {QStringLiteral("commitBlockedWhilePending"), true},
                {QStringLiteral("automaticFastDraftSelection"), false},
            })) {
      return fail("AI Edit visual proof should capture the immediate proved-draft choice before the deep-review target");
    }
    ai_edit_harness->setProperty("fastDeadlineReached", true);
    if (!wait_for_condition(
            [progressive_preview_receipt, keep_fast_draft_button,
             local_planning_label]() {
              return item_visible(progressive_preview_receipt) &&
                     item_visible(keep_fast_draft_button) &&
                     progressive_preview_receipt->property("text").toString().contains(
                         QStringLiteral("FAST PROOF PASSED")) &&
                     progressive_preview_receipt->property("text").toString().contains(
                         QStringLiteral("USE NOW OR WAIT")) &&
                     local_planning_label->property("text").toString().contains(
                         QStringLiteral("USE NOW OR WAIT")) &&
                     progressive_preview_receipt->property("text").toString().contains(
                         QStringLiteral("COMMIT LOCKED"));
            },
            1500)) {
      return fail("AI Edit should keep the explicit proved-draft choice available when the deep-review target arrives");
    }
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-validated-fast-draft-choice"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceSpeechBubble"),
                QStringLiteral("agentWorkspaceProgressivePreviewReceipt"),
                QStringLiteral("agentWorkspaceKeepValidatedFastDraftButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-two-speed-planning-preview-v1")},
                {QStringLiteral("progressivePreviewContract"),
                 QStringLiteral("agent-progressive-draft-preview-v1")},
                {QStringLiteral("assetId"), QStringLiteral("asset-1")},
                {QStringLiteral("published"), true},
                {QStringLiteral("state"),
                 QStringLiteral("fast-validated-deep-optional")},
                {QStringLiteral("mode"), QStringLiteral("fast-planning-preview")},
                {QStringLiteral("firstPublishMs"), 6},
                {QStringLiteral("firstPublishBudgetMs"), 2000},
                {QStringLiteral("firstPublishWithinBudget"), true},
                {QStringLiteral("validationPending"), true},
                {QStringLiteral("fastProofContract"),
                 QStringLiteral("agent-fast-draft-independent-proof-v1")},
                {QStringLiteral("fastValidationPassed"), true},
                {QStringLiteral("independentProof"), true},
                {QStringLiteral("deepDeadlineReached"), true},
                {QStringLiteral("keepFastDraftAvailable"), true},
                {QStringLiteral("hardwareDeadlineContract"),
                 QStringLiteral("agent-fast-draft-hardware-deadline-v1")},
                {QStringLiteral("hardwareTier"),
                 QStringLiteral("balanced")},
                {QStringLiteral("deepModelDeadlineMs"), 30000},
                {QStringLiteral("automaticFastDraftSelection"), false},
                {QStringLiteral("deepRefinementPending"), true},
                {QStringLiteral("fastPreviewIsFinal"), false},
                {QStringLiteral("commitBlockedWhilePending"), true},
                {QStringLiteral("rollbackOnFailure"), true},
                {QStringLiteral("validationExecution"),
                 QStringLiteral("request-owned-proof-continuation")},
                {QStringLiteral("durableStateWritten"), false},
                {QStringLiteral("thumbnailRefreshDeferred"), true},
                {QStringLiteral("proofWorkerContract"),
                 QStringLiteral("agent-proof-worker-v1")},
                {QStringLiteral("renderLoopCancellation"), true},
                {QStringLiteral("proofDecodeNeverUpscales"), true},
                {QStringLiteral("asyncDraftContract"),
                 QStringLiteral("agent-async-draft-run-v2")},
                {QStringLiteral("statePublicationContract"),
                 QStringLiteral("agent-nonreentrant-state-publication-v1")},
                {QStringLiteral("eventPumpBoundary"),
                 QStringLiteral("proof-and-follow-up-worker-waits-only")},
                {QStringLiteral("planningContinuationContract"),
                 QStringLiteral("agent-request-owned-planning-continuation-v1")},
                {QStringLiteral("planningExecution"),
                 QStringLiteral("qfuture-watcher")},
                {QStringLiteral("modelPlannerControllerCallPending"), false},
                {QStringLiteral("interactiveRenderPriority"),
                 QStringLiteral("agent-proof-over-thumbnails-v1")},
                {QStringLiteral("controllerCallReturned"), true},
                {QStringLiteral("completionSignal"),
                 QStringLiteral("draftRunFinished")},
                {QStringLiteral("scopePolicy"),
                 QStringLiteral("single-asset-low-risk-global-only")},
            })) {
      return fail("AI Edit visual proof should capture the validated fast-draft choice without implying commit");
    }
    ai_edit_harness->setProperty("fastDeadlineReached", false);
    ai_edit_harness->setProperty("showProgressivePreview", false);
    ai_edit_harness->setProperty("showLocalPlanning", false);
    ai_edit_harness->setProperty("twoSpeedDraftVersion",
                                 QStringLiteral("deep-review"));
    ai_edit_harness->setProperty("showTwoSpeedDraftChoice", true);
    auto* two_speed_choice_card = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTwoSpeedDraftChoiceCard"));
    auto* two_speed_choice_receipt = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceTwoSpeedDraftChoiceReceipt"));
    auto* first_look_draft_button = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceFirstLookDraftButton"));
    auto* deep_review_draft_button = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceDeepReviewDraftButton"));
    if (two_speed_choice_card == nullptr ||
        two_speed_choice_receipt == nullptr ||
        first_look_draft_button == nullptr ||
        deep_review_draft_button == nullptr ||
        !wait_for_condition(
            [two_speed_choice_card, two_speed_choice_receipt,
             first_look_draft_button, deep_review_draft_button]() {
              return item_visible(two_speed_choice_card) &&
                     item_visible(two_speed_choice_receipt) &&
                     item_visible(first_look_draft_button) &&
                     item_visible(deep_review_draft_button) &&
                     two_speed_choice_receipt
                         ->property("text")
                         .toString()
                         .contains(QStringLiteral("CURRENT · DEEP REVIEW"));
            },
            1500)) {
      return fail("AI Edit should expose both proved draft versions after deep review replaces the first look");
    }
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-two-speed-proved-draft-choice"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceSpeechBubble"),
                QStringLiteral("agentWorkspaceTwoSpeedDraftChoiceCard"),
                QStringLiteral("agentWorkspaceTwoSpeedDraftChoiceReceipt"),
                QStringLiteral("agentWorkspaceFirstLookDraftButton"),
                QStringLiteral("agentWorkspaceDeepReviewDraftButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-two-speed-draft-choice-v1")},
                {QStringLiteral("currentVersion"),
                 QStringLiteral("deep-review")},
                {QStringLiteral("versionCount"), 2},
                {QStringLiteral("bothIndependentlyProved"), true},
                {QStringLiteral("originalRollbackRetained"), true},
                {QStringLiteral("commitStillRequiresConfirmation"), true},
                {QStringLiteral("automaticSelectionAllowed"), false},
                {QStringLiteral("photographerIsAuthority"), true},
            })) {
      return fail("AI Edit visual proof should capture the photographer-controlled first-look and deep-review choice");
    }
    if (!QMetaObject::invokeMethod(first_look_draft_button, "click") ||
        !wait_for_condition(
            [ai_edit_harness_object, two_speed_choice_receipt]() {
              return ai_edit_harness_object
                             ->property("twoSpeedDraftVersion")
                             .toString() == QStringLiteral("first-look") &&
                     two_speed_choice_receipt
                         ->property("text")
                         .toString()
                         .contains(QStringLiteral("CURRENT · FIRST LOOK"));
            },
            1500)) {
      return fail("AI Edit should switch visibly to the proved first look without committing it");
    }
    ai_edit_harness->setProperty("showTwoSpeedDraftChoice", false);
    ai_edit_harness->setProperty("showVisualReferentReuse", true);
    if (!wait_for_condition(
            [trust_receipt]() {
              const auto receipt = trust_receipt->property("text").toString();
              return receipt.contains(
                         QStringLiteral("used your confirmed Upper right window")) &&
                     !receipt.contains(QStringLiteral("matte"), Qt::CaseInsensitive) &&
                     !receipt.contains(QStringLiteral("fast path"), Qt::CaseInsensitive);
            },
            1500)) {
      return fail("AI Edit should visibly disclose when it reuses an explicitly confirmed object");
    }
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-visual-reference-reuse"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceSpeechBubble"),
                QStringLiteral("agentWorkspaceVisualFindingsCard"),
                QStringLiteral("agentWorkspaceIntentAction"),
                QStringLiteral("agentWorkspaceIntentProtection"),
                QStringLiteral("agentWorkspaceTrustReceipt"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-visual-referent-resolution-v1")},
                {QStringLiteral("referentId"),
                 QStringLiteral("referent-upper-right-window")},
                {QStringLiteral("assetId"), QStringLiteral("asset-1")},
                {QStringLiteral("chosenLabel"),
                 QStringLiteral("Upper right window")},
                {QStringLiteral("scopePolicy"),
                 QStringLiteral("exact-project-and-asset-explicit-reference-only")},
                {QStringLiteral("fastPathUsed"), true},
                {QStringLiteral("modelPlanSkipped"), true},
                {QStringLiteral("mcpDiscoverySkipped"), true},
                {QStringLiteral("planningDurationMs"), 1},
                {QStringLiteral("planningBudgetMs"), 50},
                {QStringLiteral("planningWithinBudget"), true},
                {QStringLiteral("transientMatteReused"), true},
                {QStringLiteral("transientMatteReuseCount"), 1},
                {QStringLiteral("transientMattePersisted"), false},
                {QStringLiteral("proofMaxEdge"), 512},
                {QStringLiteral("proofResolutionPolicy"),
                 QStringLiteral("fast-confirmed-referent-validation-preview")},
                {QStringLiteral("modelReplanRequired"), false},
                {QStringLiteral("crossAssetReuseAllowed"), false},
                {QStringLiteral("draftReversible"), true},
            })) {
      return fail("AI Edit visual proof should capture the same-object continuity receipt");
    }
    const auto linked_finding = QVariantMap{
        {QStringLiteral("maskId"), QStringLiteral("agent-localized-window-proof")},
        {QStringLiteral("target"), QStringLiteral("object")},
        {QStringLiteral("targetLabel"), QStringLiteral("Upper right window")},
        {QStringLiteral("observation"),
         QStringLiteral("The upper-right window is pulling attention away from the subject.")},
    };
    QVariant finding_requested;
    if (!QMetaObject::invokeMethod(
            workspace_review,
            "requestFinding",
            Qt::DirectConnection,
            Q_RETURN_ARG(QVariant, finding_requested),
            Q_ARG(QVariant, linked_finding)) ||
        !finding_requested.toBool() ||
        workspace_review->property("activeFindingMaskId").toString() !=
            QStringLiteral("agent-localized-window-proof")) {
      return fail("AI Edit findings should expose a direct spatial-selection interaction");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-visual-findings-review"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceSpeechBubble"),
                QStringLiteral("agentWorkspaceVisualFindingsCard"),
                QStringLiteral("agentWorkspaceIntentAction"),
                QStringLiteral("agentWorkspaceIntentProtection"),
                QStringLiteral("agentWorkspaceTrustReceipt"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("visualFindingCount"), 2},
                {QStringLiteral("localizedActionCount"), 1},
                {QStringLiteral("generatedSemanticSelectionCount"), 0},
                {QStringLiteral("generatedEvidenceGuidedSelectionCount"), 1},
                {QStringLiteral("groundedSelectionContract"),
                 QStringLiteral("agent-grounded-selection-materialization-v2")},
                {QStringLiteral("openVocabularyTargetLabel"),
                 QStringLiteral("Upper right window")},
                {QStringLiteral("linkedFindingMaskId"),
                 QStringLiteral("agent-localized-window-proof")},
                {QStringLiteral("globalChangePolicy"), QStringLiteral("preserve")},
                {QStringLiteral("intentLedgerContract"),
                 QStringLiteral("agent-editable-intent-ledger-v1")},
                {QStringLiteral("plannedAdjustmentSummary"),
                 QStringLiteral("Exposure -0.42 · Highlights -31")},
                {QStringLiteral("protectedIntent"),
                 QStringLiteral("Everything else")},
                {QStringLiteral("perIntentCorrectionAvailable"), true},
                {QStringLiteral("draftReversible"), true},
                {QStringLiteral("targetedCorrectionContract"),
                 QStringLiteral("agent-targeted-correction-v1")},
                {QStringLiteral("preservedUnrelatedEdits"), true},
            })) {
      return fail("AI Edit visual proof should capture grounded findings and review controls");
    }
    if (!QMetaObject::invokeMethod(intent_protect_button, "click") ||
        !wait_for_condition(
            [ai_edit_visual_root]() {
              auto* current_protect_button = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("agentWorkspaceIntentProtectButton"));
              auto* current_change_button = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("agentWorkspaceIntentChangeButton"));
              auto* current_protection = find_named_item(
                  ai_edit_visual_root,
                  QStringLiteral("agentWorkspaceIntentProtection"));
              auto* current_broad_change_button = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("agentWorkspaceAskForChangesButton"));
              return current_protect_button != nullptr &&
                     current_change_button != nullptr &&
                     current_protection != nullptr &&
                     current_broad_change_button != nullptr &&
                     current_protect_button->property("text").toString() ==
                         QStringLiteral("Kept") &&
                     !current_change_button->property("enabled").toBool() &&
                     !current_broad_change_button->property("enabled").toBool() &&
                     current_broad_change_button->property("text").toString() ==
                         QStringLiteral("Unlock kept change first") &&
                     current_protection->property("text").toString().contains(
                         QStringLiteral("Upper right window"));
            },
            1000)) {
      auto* current_protect_button = find_named_object(
          ai_edit_visual_root,
          QStringLiteral("agentWorkspaceIntentProtectButton"));
      auto* current_change_button = find_named_object(
          ai_edit_visual_root,
          QStringLiteral("agentWorkspaceIntentChangeButton"));
      auto* current_protection = find_named_item(
          ai_edit_visual_root,
          QStringLiteral("agentWorkspaceIntentProtection"));
      std::cerr
          << "protected intent debug: harness="
          << ai_edit_harness->property("showIntentProtected").toBool()
          << " serial="
          << workspace_review->property("intentBoundarySerial").toInt()
          << " protect_text="
          << (current_protect_button == nullptr
                  ? std::string("<missing>")
                  : current_protect_button->property("text").toString().toStdString())
          << " change_enabled="
          << (current_change_button != nullptr &&
              current_change_button->property("enabled").toBool())
          << " protection="
          << (current_protection == nullptr
                  ? std::string("<missing>")
                  : current_protection->property("text").toString().toStdString())
          << '\n';
      return fail("AI Edit should enforce a user-locked intent and disclose that the exact change is being kept");
    }
    intent_protect_button = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceIntentProtectButton"));
    intent_change_button = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceIntentChangeButton"));
    intent_protection = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceIntentProtection"));
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-protected-intent-boundary"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceVisualFindingsCard"),
                QStringLiteral("agentWorkspaceIntentAction"),
                QStringLiteral("agentWorkspaceIntentProtectButton"),
                QStringLiteral("agentWorkspaceIntentChangeButton"),
                QStringLiteral("agentWorkspaceIntentProtection"),
                QStringLiteral("agentWorkspaceAskForChangesButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-draft-intent-boundary-v1")},
                {QStringLiteral("maskId"),
                 QStringLiteral("agent-localized-window-proof")},
                {QStringLiteral("targetLabel"),
                 QStringLiteral("Upper right window")},
                {QStringLiteral("userProtected"), true},
                {QStringLiteral("draftRecipeChanged"), false},
                {QStringLiteral("targetedCorrectionBlocked"), true},
                {QStringLiteral("fullReplanBlocked"), true},
                {QStringLiteral("unlockRequired"), true},
            })) {
      return fail("AI Edit visual proof should capture the enforced protected-intent boundary");
    }
    if (!QMetaObject::invokeMethod(intent_protect_button, "click") ||
        !wait_for_condition(
            [ai_edit_visual_root]() {
              auto* current_protect_button = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("agentWorkspaceIntentProtectButton"));
              auto* current_change_button = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("agentWorkspaceIntentChangeButton"));
              auto* current_broad_change_button = find_named_object(
                  ai_edit_visual_root,
                  QStringLiteral("agentWorkspaceAskForChangesButton"));
              return current_protect_button != nullptr &&
                     current_change_button != nullptr &&
                     current_broad_change_button != nullptr &&
                     current_protect_button->property("text").toString() ==
                         QStringLiteral("Keep") &&
                     current_change_button->property("enabled").toBool() &&
                     current_broad_change_button->property("enabled").toBool() &&
                     current_broad_change_button->property("text").toString() ==
                         QStringLiteral("Make Changes");
            },
            1000)) {
      return fail("AI Edit should require and honor an explicit unlock before correction authority returns");
    }
    auto* point_to_area_button = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspacePointToAreaButton"));
    auto* workspace_headline = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceHeadline"));
    auto* shared_mask_overlay = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("aiEditSharedMaskOverlay"));
    if (point_to_area_button == nullptr || workspace_headline == nullptr ||
        shared_mask_overlay == nullptr ||
        !QMetaObject::invokeMethod(point_to_area_button, "click") ||
        !wait_for_condition(
            [ai_edit_harness_object, workspace_headline, shared_mask_overlay]() {
              auto* point_input = find_named_item(
                  shared_mask_overlay,
                  QStringLiteral("aiEditAgentPointTargetInput"));
              return ai_edit_harness_object
                         ->property("workspaceSpatialTargetMode")
                         .toBool() &&
                     workspace_headline->property("text").toString() ==
                         QStringLiteral("Point to the exact area.") &&
                     point_input != nullptr && item_visible(point_input) &&
                     point_input->property("enabled").toBool();
            },
            1000)) {
      return fail("AI Edit should enter an explicit image-normalized point targeting mode from draft review");
    }
    if (!QMetaObject::invokeMethod(
            shared_mask_overlay,
            "agentPointTargeted",
            Qt::DirectConnection,
            Q_ARG(double, 0.37),
            Q_ARG(double, 0.38))) {
      return fail("AI Edit point targeting should publish the mapped image coordinate to the review surface");
    }
    auto* pointed_correction_card = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceCorrectionCard"));
    auto* pointed_correction_scope = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceCorrectionScope"));
    auto* pointed_correction_editor = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceCorrectionEditor"));
    auto* pointed_correction_submit = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceSubmitCorrectionButton"));
    auto* pointed_marker = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("aiEditAgentPointTargetMarker"));
    const auto pointed_feedback =
        QStringLiteral("Make only this area darker.");
    if (pointed_correction_editor != nullptr)
      pointed_correction_editor->setProperty("text", pointed_feedback);
    if (pointed_correction_card == nullptr || pointed_correction_scope == nullptr ||
        pointed_correction_editor == nullptr || pointed_correction_submit == nullptr ||
        pointed_marker == nullptr ||
        !wait_for_condition(
            [workspace_review, pointed_correction_card,
             pointed_correction_scope, pointed_correction_submit,
             pointed_marker]() {
              const auto point =
                  workspace_review->property("spatialTargetPoint").toMap();
              return workspace_review->property("spatialTargetChosen").toBool() &&
                     std::abs(point.value(QStringLiteral("x")).toDouble() - 0.37) <
                         0.0001 &&
                     std::abs(point.value(QStringLiteral("y")).toDouble() - 0.38) <
                         0.0001 &&
                     item_visible(pointed_correction_card) &&
                     item_visible(pointed_marker) &&
                     pointed_correction_scope->property("text").toString() ==
                         QStringLiteral("Target: content at this point · feathered fallback if unclear") &&
                     pointed_correction_submit->property("enabled").toBool();
            },
            1200)) {
      return fail("the mapped point should open one exact localized correction composer and visible marker");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-point-grounded-correction"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceSpeechBubble"),
                QStringLiteral("aiEditAgentPointTargetMarker"),
                QStringLiteral("agentWorkspaceCorrectionCard"),
                QStringLiteral("agentWorkspaceCorrectionScope"),
                QStringLiteral("agentWorkspaceCorrectionEditor"),
                QStringLiteral("agentWorkspaceSubmitCorrectionButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-point-grounded-correction-v2")},
                {QStringLiteral("normalizedX"), 0.37},
                {QStringLiteral("normalizedY"), 0.38},
                {QStringLiteral("radiusX"), 0.12},
                {QStringLiteral("radiusY"), 0.12},
                {QStringLiteral("feather"), 0.72},
                {QStringLiteral("feedback"), pointed_feedback},
                {QStringLiteral("assetId"), QStringLiteral("asset-1")},
                {QStringLiteral("fullReplan"), false},
                {QStringLiteral("contentSnapAttempted"), true},
                {QStringLiteral("radialFallbackPolicy"),
                 QStringLiteral("use-visible-feathered-region-if-evidence-unclear")},
                {QStringLiteral("preservedOutsideResolvedTarget"), true},
                {QStringLiteral("draftReversible"), true},
                {QStringLiteral("durableMemoryWritten"), false},
            })) {
      return fail("AI Edit visual proof should capture the exact point-to-correction boundary");
    }
    if (!QMetaObject::invokeMethod(pointed_correction_submit, "click") ||
        !wait_for_condition(
            [ai_edit_harness_object, workspace_review, trust_receipt,
             pointed_feedback]() {
              return ai_edit_harness_object
                             ->property("showPointGroundedCorrection")
                             .toBool() &&
                     std::abs(ai_edit_harness_object->property("lastPointX").toDouble() -
                              0.37) < 0.0001 &&
                     std::abs(ai_edit_harness_object->property("lastPointY").toDouble() -
                              0.38) < 0.0001 &&
                     ai_edit_harness_object->property("lastPointFeedback").toString() ==
                         pointed_feedback &&
                     ai_edit_harness_object->property("lastPointAssetId").toString() ==
                         QStringLiteral("asset-1") &&
                     !workspace_review->property("correctionOpen").toBool() &&
                     trust_receipt->property("text").toString().contains(
                         QStringLiteral("snapped to local content · outside it protected"));
            },
            1200)) {
      return fail("AI Edit should submit the exact mapped point and correction text into the reversible controller path");
    }
    auto* point_resolution_card = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspacePointResolutionCard"));
    auto* point_resolution_label = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspacePointResolutionLabel"));
    auto* point_resolution_boundary = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspacePointResolutionBoundary"));
    if (point_resolution_card == nullptr || point_resolution_label == nullptr ||
        point_resolution_boundary == nullptr ||
        !wait_for_condition(
            [point_resolution_card, point_resolution_label,
             point_resolution_boundary]() {
              return item_visible(point_resolution_card) &&
                     point_resolution_label->property("text").toString() ==
                         QStringLiteral("Snapped to local content") &&
                     point_resolution_boundary->property("text").toString() ==
                         QStringLiteral("CONTENT BOUNDARY FOUND · CLICK INCLUDED · OUTSIDE PROTECTED");
            },
            1000)) {
      return fail("AI Edit should disclose whether a point resolved to a generated content boundary or the radial fallback");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-point-content-snap-resolution"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceTrustReceipt"),
                QStringLiteral("agentWorkspacePointResolutionCard"),
                QStringLiteral("agentWorkspacePointResolutionLabel"),
                QStringLiteral("agentWorkspacePointResolutionBoundary"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-point-grounded-correction-v2")},
                {QStringLiteral("resolutionMode"),
                 QStringLiteral("content-snap")},
                {QStringLiteral("boundaryKind"),
                 QStringLiteral("generated-matte")},
                {QStringLiteral("preservedOutsideResolvedTarget"), true},
                {QStringLiteral("radialFallbackUsed"), false},
                {QStringLiteral("durableMemoryWritten"), false},
            })) {
      return fail("AI Edit visual proof should capture the resolved point target and honest boundary receipt");
    }
    ai_edit_harness_object->setProperty("showPointFallback", true);
    if (!wait_for_condition(
            [point_resolution_label, point_resolution_boundary,
             trust_receipt]() {
              return point_resolution_label->property("text").toString() ==
                         QStringLiteral("Feathered area fallback") &&
                     point_resolution_boundary->property("text").toString() ==
                         QStringLiteral("VISIBLE FEATHERED BOUNDARY · NO OBJECT CLAIM · OUTSIDE PROTECTED") &&
                     trust_receipt->property("text").toString().contains(
                         QStringLiteral("feathered fallback · no object claim"));
            },
            1000)) {
      return fail("AI Edit should identify the bounded radial fallback without claiming it recognized an object");
    }
    process_layout_events(8);
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-point-radial-fallback-resolution"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceTrustReceipt"),
                QStringLiteral("agentWorkspacePointResolutionCard"),
                QStringLiteral("agentWorkspacePointResolutionLabel"),
                QStringLiteral("agentWorkspacePointResolutionBoundary"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-point-grounded-correction-v2")},
                {QStringLiteral("resolutionMode"),
                 QStringLiteral("radial-fallback")},
                {QStringLiteral("boundaryKind"),
                 QStringLiteral("feathered-radial")},
                {QStringLiteral("preservedOutsidePointedRegion"), true},
                {QStringLiteral("noObjectClaim"), true},
                {QStringLiteral("durableMemoryWritten"), false},
            })) {
      return fail("AI Edit visual proof should capture the no-object-claim radial fallback receipt");
    }
    ai_edit_harness_object->setProperty("showPointFallback", false);
    intent_change_button = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceIntentChangeButton"));
    if (!QMetaObject::invokeMethod(intent_change_button, "click") ||
        workspace_review->property("activeFindingMaskId").toString() !=
            QStringLiteral("agent-localized-window-proof")) {
      return fail("AI Edit intent ledger should open a correction scoped to its linked mask");
    }
    process_layout_events(8);
    auto* correction_card = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceCorrectionCard"));
    auto* correction_scope = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceCorrectionScope"));
    auto* correction_editor = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentWorkspaceCorrectionEditor"));
    if (correction_card == nullptr || correction_scope == nullptr ||
        correction_editor == nullptr ||
        !wait_for_condition(
            [correction_card, correction_scope]() {
              return item_visible(correction_card) &&
                     correction_scope->property("text").toString() ==
                         QStringLiteral("Changing only: Upper right window");
            },
            1000)) {
      std::cerr << "AI Edit correction debug: card=" << (correction_card != nullptr)
                << " visible="
                << (correction_card != nullptr && item_visible(correction_card))
                << " scope="
                << (correction_scope == nullptr
                        ? std::string("<missing>")
                        : correction_scope->property("text").toString().toStdString())
                << " editor=" << (correction_editor != nullptr)
                << " correctionOpen="
                << workspace_review->property("correctionOpen").toBool()
                << " draftReady="
                << workspace_review->property("draftReady").toBool() << '\n';
      return fail("AI Edit correction composer should name the exact finding it will revise");
    }
    if (!capture_visual_proof(
            ai_edit_window,
            ai_edit_visual_root,
            visual_proof_dir,
            QStringLiteral("agent-targeted-correction"),
            1440,
            900,
            {
                QStringLiteral("agentWorkspaceCorrectionCard"),
                QStringLiteral("agentWorkspaceCorrectionScope"),
                QStringLiteral("agentWorkspaceCorrectionEditor"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("maskId"),
                 QStringLiteral("agent-localized-window-proof")},
                {QStringLiteral("scopeLabel"),
                 QStringLiteral("Changing only: Upper right window")},
                {QStringLiteral("fullReplan"), false},
                {QStringLiteral("generatedMattePreserved"), true},
                {QStringLiteral("draftReversible"), true},
            })) {
      return fail("AI Edit visual proof should capture the finding-targeted correction composer");
    }
    QFile manifest_file(QDir(visual_proof_dir).filePath(QStringLiteral("ai-edit-ux-manifest.json")));
    if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        manifest_file.write(QJsonDocument(visual_proof_manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("AI Edit visual proof should write its manifest");
    }
    auto* focus_return_probe = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("aiEditFocusReturnProbe"));
    auto* focus_return_item = qobject_cast<QQuickItem*>(focus_return_probe);
    ai_edit_harness->setProperty("showFocusReturnProbe", true);
    process_layout_events(4);
    if (focus_return_item != nullptr) {
      focus_return_item->forceActiveFocus(Qt::TabFocusReason);
    }
    if (!QMetaObject::invokeMethod(ai_edit_dialog, "open", Qt::DirectConnection) ||
        !wait_for_condition(
            [ai_edit_dialog]() { return ai_edit_dialog->property("visible").toBool(); },
            1000)) {
      return fail("AI Edit close proof should reopen the walkthrough from its visible workspace control");
    }
    const auto captured_focus_return_item =
        qvariant_cast<QObject*>(ai_edit_dialog->property("focusReturnItem"));
    if (focus_return_item == nullptr || !focus_return_item->isVisible() ||
        captured_focus_return_item != focus_return_probe) {
      std::cerr << "AI Edit focus capture debug: visible="
                << ai_edit_dialog->property("visible").toBool()
                << " target=" << captured_focus_return_item
                << " expected=" << focus_return_probe
                << " probeVisible=" << (focus_return_item != nullptr && focus_return_item->isVisible())
                << " active=" << ai_edit_window->activeFocusItem() << '\n';
      return fail("AI Edit should capture the visible workspace control that opened it");
    }
    auto* run_cancel_button = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentCancelRunButton"));
    auto* run_goal_label = find_named_item(
        ai_edit_visual_root,
        QStringLiteral("agentRunGoalLabel"));
    if (run_cancel_button == nullptr || run_goal_label == nullptr) {
      return fail("AI Edit run page should expose its Stop action and current goal label");
    }
    ai_edit_harness_object->setProperty("showProgressivePreview", true);
    if (!wait_for_condition(
            [ai_edit_window, ai_edit_dialog, run_cancel_button, run_goal_label]() {
              auto* cancel_item = qobject_cast<QQuickItem*>(run_cancel_button);
              return ai_edit_dialog->property("currentPage").toInt() == 2 &&
                     run_cancel_button->property("visible").toBool() &&
                     run_goal_label->property("visible").toBool() &&
                     ai_edit_window->activeFocusItem() == cancel_item &&
                     run_cancel_button->property("visualFocus").toBool();
            },
            1000)) {
      auto* active_focus = ai_edit_window->activeFocusItem();
      std::cerr << "AI Edit run-page focus debug: page="
                << ai_edit_dialog->property("currentPage").toInt()
                << " active=" << (active_focus ? active_focus->objectName().toStdString() : "<none>")
                << " stopVisible=" << run_cancel_button->property("visible").toBool()
                << " goalVisible=" << run_goal_label->property("visible").toBool()
                << " stopVisualFocus=" << run_cancel_button->property("visualFocus").toBool()
                << '\n';
      return fail("AI Edit running state should focus its visible Stop action and expose the current goal");
    }
    if (!send_window_key(ai_edit_window, Qt::Key_Escape) ||
        !wait_for_condition(
            [ai_edit_dialog]() {
              return ai_edit_dialog->property("visible").toBool() &&
                     ai_edit_dialog->property("currentPage").toInt() == 2;
            },
            250)) {
      return fail("Escape should remain contained while AI Edit is running");
    }
    ai_edit_harness_object->setProperty("showProgressivePreview", false);
    auto* final_prompt = find_named_object(
        ai_edit_visual_root,
        QStringLiteral("agentPromptEditor"));
    auto* final_prompt_item = qobject_cast<QQuickItem*>(final_prompt);
    if (final_prompt == nullptr || final_prompt_item == nullptr ||
        !QMetaObject::invokeMethod(
            ai_edit_dialog,
            "setPage",
            Qt::DirectConnection,
            Q_ARG(QVariant, 0))) {
      return fail("AI Edit should return to its prompt page for Escape focus proof");
    }
    if (!wait_for_condition(
            [ai_edit_window, final_prompt_item]() {
              return ai_edit_window->activeFocusItem() == final_prompt_item &&
                     final_prompt_item->hasActiveFocus();
            },
            1000)) {
      std::cerr << "AI Edit final prompt focus debug: active="
                << ai_edit_window->activeFocusItem()
                << " prompt=" << final_prompt_item
                << " hasFocus=" << final_prompt_item->hasActiveFocus()
                << " visualFocus=" << final_prompt->property("visualFocus").toBool() << '\n';
      return fail("AI Edit should show its prompt field focus before Escape closes the walkthrough");
    }
    if (!send_window_key(ai_edit_window, Qt::Key_Escape)) {
      return fail("Escape should close AI Edit during the focus-return proof");
    }
    if (!wait_for_condition(
            [ai_edit_window, ai_edit_dialog, focus_return_item]() {
              return !ai_edit_dialog->property("visible").toBool() &&
                     ai_edit_window->activeFocusItem() == focus_return_item &&
                     focus_return_item->property("visualFocus").toBool();
            },
            1200)) {
      std::cerr << "AI Edit focus return debug: visible="
                << ai_edit_dialog->property("visible").toBool()
                << " active=" << ai_edit_window->activeFocusItem()
                << " target=" << focus_return_item
                << " targetHasFocus=" << (focus_return_item != nullptr && focus_return_item->hasActiveFocus())
                << " targetVisualFocus=" << (focus_return_item != nullptr && focus_return_item->property("visualFocus").toBool())
                << '\n';
      return fail("Closing AI Edit should restore visible keyboard focus to the control that opened it");
    }
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_IMPORT_SAFETY")) {
    const auto import_source_root = runtime_root.filePath(QStringLiteral("import-safety-source"));
    const auto import_dcim_root = QDir(import_source_root).filePath(QStringLiteral("DCIM"));
    if (!QDir().mkpath(import_dcim_root)) {
      return fail("import safety visual proof should create its mixed source folder");
    }
    const auto raw_path = QDir(import_dcim_root).filePath(QStringLiteral("PAIR_0042.CR3"));
    const auto jpeg_path = QDir(import_dcim_root).filePath(QStringLiteral("PAIR_0042.jpg"));
    const auto unsupported_path = QDir(import_dcim_root).filePath(QStringLiteral("notes.txt"));
    const auto missing_path = QDir(import_dcim_root).filePath(QStringLiteral("missing.jpg"));
    QFile raw_file(raw_path);
    QFile unsupported_file(unsupported_path);
    if (!raw_file.open(QIODevice::WriteOnly) || raw_file.write("raw-visual-proof") <= 0 ||
        !unsupported_file.open(QIODevice::WriteOnly) || unsupported_file.write("unsupported") <= 0 ||
        !write_overlay_visual_fixture(jpeg_path, QStringLiteral("saturated-small"))) {
      return fail("import safety visual proof should create RAW, JPEG, and unsupported fixtures");
    }
    raw_file.close();
    unsupported_file.close();

    auto existing_raw = make_asset(QStringLiteral("import-duplicate"), raw_path);
    existing_raw.location_path = raw_path;
    catalog_assets.replaceAssetsForTesting({existing_raw});
    if (!QMetaObject::invokeMethod(root, "beginImportWorkspace")) {
      return fail("import safety visual proof should route to Import");
    }
    process_layout_events(8);
    auto* import_workspace = find_named_object(root, QStringLiteral("importWorkspace"));
    if (import_workspace == nullptr) {
      return fail("import safety visual proof should expose ImportView");
    }
    const QStringList pending_paths{
        raw_path,
        raw_path,
        jpeg_path,
        unsupported_path,
        missing_path,
    };
    import_workspace->setProperty("sourceRootPath", import_source_root);
    import_workspace->setProperty("sourceRootUrl", QUrl::fromLocalFile(import_source_root).toString());
    import_workspace->setProperty("sourceLabel", QStringLiteral("Camera Card"));
    import_workspace->setProperty("currentFolderPath", import_dcim_root);
    import_workspace->setProperty("currentFolderUrl", QUrl::fromLocalFile(import_dcim_root).toString());
    import_workspace->setProperty("pendingImportKind", QStringLiteral("selection"));
    import_workspace->setProperty("pendingImportRootPath", import_source_root);
    import_workspace->setProperty("pendingImportPaths", pending_paths);
    import_workspace->setProperty("workflowProfile", QStringLiteral("studio"));
    import_workspace->setProperty("importMode", QStringLiteral("managed"));
    import_workspace->setProperty("duplicatePolicy", QStringLiteral("skip"));
    import_workspace->setProperty("importProjectChooserOpen", true);
    if (!QMetaObject::invokeMethod(import_workspace, "refreshImportPreflight")) {
      return fail("import safety visual proof should run the real controller preflight");
    }
    QElapsedTimer import_review_timer;
    import_review_timer.start();
    while (catalog_assets.importPreflightBusy() && import_review_timer.elapsed() < 5000)
      process_layout_events(1);
    process_layout_events(12);
    const auto preflight = import_workspace->property("importPreflight").toMap();
    if (import_workspace->property("importSafetyContract").toString() !=
            QStringLiteral("import-safety:v1|preflight:source-destination-mode-count-types-size|problems:duplicates-unsupported-unreadable-raw-jpeg|organization:folders-filenames|modes:managed-copy-referenced-link|duplicate-policy:skip-replace|progress:nonblocking|summary:success-skipped-duplicates-failures|retry:job-queue") ||
        preflight.value(QStringLiteral("importableCount")).toInt() != 1 ||
        preflight.value(QStringLiteral("rawJpegPairCount")).toInt() != 1 ||
        preflight.value(QStringLiteral("duplicateCount")).toInt() != 2 ||
        preflight.value(QStringLiteral("unsupportedCount")).toInt() != 1 ||
        preflight.value(QStringLiteral("unreadableCount")).toInt() != 1 ||
        preflight.value(QStringLiteral("estimatedDiskBytes")).toLongLong() <= 0) {
      return fail("import safety visual proof should expose accurate mixed-media preflight facts");
    }
    if (!capture_visual_proof(
            root_window,
            root,
            visual_proof_dir,
            QStringLiteral("import-safety-preflight"),
            1440,
            900,
            {
                QStringLiteral("importWorkspace"),
                QStringLiteral("importDestinationPanel"),
                QStringLiteral("importManagedModeButton"),
                QStringLiteral("importReferencedModeButton"),
                QStringLiteral("importDateCameraOrganizationButton"),
                QStringLiteral("importRawOnlyPairPolicyButton"),
                QStringLiteral("importPreflightFacts"),
                QStringLiteral("importDuplicateSkipButton"),
                QStringLiteral("importDuplicateReplaceButton"),
                QStringLiteral("importPreflightConfirmButton"),
            },
            &visual_proof_manifest,
            QJsonObject::fromVariantMap(preflight))) {
      return fail("import safety visual proof should capture the visible preflight");
    }
    import_workspace->setProperty("importMode", QStringLiteral("referenced"));
    process_layout_events(8);
    import_review_timer.restart();
    while (catalog_assets.importPreflightBusy() && import_review_timer.elapsed() < 5000)
      process_layout_events(1);
    const auto referenced_preflight = import_workspace->property("importPreflight").toMap();
    if (referenced_preflight.value(QStringLiteral("importMode")).toString() != QStringLiteral("referenced") ||
        referenced_preflight.value(QStringLiteral("estimatedDiskBytes")).toLongLong() != 0 ||
        QDir::cleanPath(referenced_preflight.value(QStringLiteral("destinationPath")).toString()) !=
            QDir::cleanPath(import_source_root) ||
        !referenced_preflight.value(QStringLiteral("outcomeText")).toString().contains(QStringLiteral("links"))) {
      return fail("referenced import visual proof should match the leave-in-place destination outcome");
    }
    if (!capture_visual_proof(
            root_window,
            root,
            visual_proof_dir,
            QStringLiteral("import-safety-referenced"),
            1440,
            900,
            {
                QStringLiteral("importWorkspace"),
                QStringLiteral("importDestinationPanel"),
                QStringLiteral("importReferencedModeButton"),
                QStringLiteral("importPreflightFacts"),
                QStringLiteral("importPreflightConfirmButton"),
            },
            &visual_proof_manifest,
            QJsonObject::fromVariantMap(referenced_preflight))) {
      return fail("import safety visual proof should capture referenced mode");
    }
    QFile manifest_file(QDir(visual_proof_dir).filePath(QStringLiteral("import-safety-manifest.json")));
    if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        manifest_file.write(QJsonDocument(visual_proof_manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("import safety visual proof should write its manifest");
    }
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_OFFLINE_RECOVERY")) {
    const auto cached_preview_path =
        runtime_root.filePath(QStringLiteral("offline-recovery-visual/cache/LOST_0101-preview.jpg"));
    if (!QDir().mkpath(QFileInfo(cached_preview_path).absolutePath()) ||
        !write_overlay_visual_fixture(cached_preview_path, QStringLiteral("saturated-small"))) {
      return fail("offline recovery visual proof should create a cached preview");
    }
    auto offline_visual_asset = make_asset(
        QStringLiteral("offline-recovery-visual-asset"),
        runtime_root.filePath(QStringLiteral("offline-recovery-visual/moved/LOST_0101.jpg")),
        QUrl::fromLocalFile(cached_preview_path).toString());
    offline_visual_asset.location_path = offline_visual_asset.file_path;
    offline_visual_asset.adjustment_patch_json =
        photo_editor::desktop::
            canonical_asset_bound_adjustment_patch_json(
                QStringLiteral(
                    "{\"exposure\":0.45,\"contrast\":0.2}"));
    const auto offline_visual_adjustments =
        offline_visual_asset
            .adjustment_patch_json;
    offline_visual_asset.online = false;
    catalog_assets.replaceAssetsForTesting({offline_visual_asset});
    catalog_assets.selectIndex(0);
    catalog_assets.selectAll();

    root->setProperty("currentWorkspace", root->property("libraryWorkspaceIndex"));
    process_layout_events(14);
    QElapsedTimer library_settle_timer;
    library_settle_timer.start();
    (void)wait_for_condition([&library_settle_timer]() { return library_settle_timer.elapsed() >= 180; }, 500);
    auto* library_workspace = find_named_object(root, QStringLiteral("libraryStudioWorkspace"));
    auto* library_recovery = library_workspace == nullptr
                                 ? nullptr
                                 : library_workspace->findChild<QObject*>(QStringLiteral("offlineRecoveryBanner"));
    if (library_workspace == nullptr || library_recovery == nullptr ||
        !library_recovery->property("visible").toBool() ||
        library_recovery->property("recoveryState").toString() != QStringLiteral("moved-file") ||
        catalog_assets.selectedThumbnailSource().isEmpty() ||
        catalog_assets.selectedAdjustmentPatchJson() !=
            offline_visual_adjustments) {
      return fail("Library should show classified offline recovery while retaining cached preview and edits");
    }
    if (!capture_visual_proof(
            root_window,
            root,
            visual_proof_dir,
            QStringLiteral("offline-recovery-library"),
            1440,
            900,
            {
                QStringLiteral("libraryStudioWorkspace"),
                QStringLiteral("offlineRecoveryBanner"),
                QStringLiteral("offlineRecoveryTitle"),
                QStringLiteral("offlineRelinkSelectedButton"),
                QStringLiteral("offlineRelinkAllButton"),
                QStringLiteral("offlineRecheckButton"),
                QStringLiteral("offlineMarkMissingButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("surface"), QStringLiteral("library")},
                {QStringLiteral("state"), catalog_assets.selectedAvailabilityState()},
                {QStringLiteral("assetId"), catalog_assets.selectedAssetId()},
                {QStringLiteral("cachedPreviewPreserved"), !catalog_assets.selectedThumbnailSource().isEmpty()},
                {QStringLiteral("adjustmentsPreserved"),
                 catalog_assets.selectedAdjustmentPatchJson() ==
                     offline_visual_adjustments},
            })) {
      return fail("offline recovery visual proof should capture the Library surface");
    }

    root->setProperty("currentWorkspace", root->property("editWorkspaceIndex"));
    process_layout_events(16);
    QElapsedTimer edit_settle_timer;
    edit_settle_timer.start();
    (void)wait_for_condition([&edit_settle_timer]() { return edit_settle_timer.elapsed() >= 220; }, 600);
    auto* edit_workspace = find_named_object(root, QStringLiteral("editStudioWorkspace"));
    auto* edit_recovery = edit_workspace == nullptr
                              ? nullptr
                              : edit_workspace->findChild<QObject*>(QStringLiteral("offlineRecoveryBanner"));
    if (edit_recovery == nullptr || !edit_recovery->property("visible").toBool() ||
        edit_recovery->property("recoverySurface").toString() != QStringLiteral("edit")) {
      return fail("Edit should expose the same offline recovery action without a modal loop");
    }
    if (!capture_visual_proof(
            root_window,
            root,
            visual_proof_dir,
            QStringLiteral("offline-recovery-edit"),
            1440,
            900,
            {QStringLiteral("editStudioWorkspace")},
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("surface"), QStringLiteral("edit")},
                {QStringLiteral("state"), catalog_assets.selectedAvailabilityState()},
                {QStringLiteral("recoveryVisible"), true},
            })) {
      return fail("offline recovery visual proof should capture the Edit surface");
    }

    root->setProperty("currentWorkspace", root->property("exportWorkspaceIndex"));
    process_layout_events(16);
    QElapsedTimer export_settle_timer;
    export_settle_timer.start();
    (void)wait_for_condition([&export_settle_timer]() { return export_settle_timer.elapsed() >= 260; }, 700);
    auto* export_recovery = find_named_object(root, QStringLiteral("exportOfflineRecoveryBanner"));
    if (export_recovery == nullptr || !export_recovery->property("visible").toBool() ||
        export_recovery->property("recoveryState").toString() != QStringLiteral("moved-file")) {
      return fail("Export should expose offline recovery and the selected source state");
    }
    if (!capture_visual_proof(
            root_window,
            root,
            visual_proof_dir,
            QStringLiteral("offline-recovery-export"),
            1440,
            900,
            {
                QStringLiteral("exportStudioWorkspace"),
                QStringLiteral("exportOfflineRecoveryBanner"),
                QStringLiteral("exportOfflineRecoveryTitle"),
                QStringLiteral("exportOfflineRelinkSelectedButton"),
                QStringLiteral("exportOfflineRelinkAllButton"),
                QStringLiteral("exportOfflineRecheckButton"),
                QStringLiteral("exportOfflineMarkMissingButton"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("surface"), QStringLiteral("export")},
                {QStringLiteral("state"), catalog_assets.selectedAvailabilityState()},
                {QStringLiteral("blockedExportRecovery"), QStringLiteral("auto-resume-after-relink")},
            })) {
      return fail("offline recovery visual proof should capture the Export surface");
    }

    QFile manifest_file(QDir(visual_proof_dir).filePath(QStringLiteral("offline-recovery-manifest.json")));
    if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        manifest_file.write(QJsonDocument(visual_proof_manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("offline recovery visual proof should write its manifest");
    }
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_DELIVERY_EXPORT")) {
    const auto delivery_source_dir = runtime_root.filePath(QStringLiteral("delivery-export-visual/sources"));
    const auto delivery_destination = runtime_root.filePath(QStringLiteral("delivery-export-visual/delivery"));
    if (!QDir().mkpath(delivery_source_dir) || !QDir().mkpath(delivery_destination)) {
      return fail("delivery export visual proof should create source and destination folders");
    }
    QVector<photo_editor::desktop::CatalogAssetItem> delivery_assets;
    delivery_assets.reserve(20);
    for (int index = 1; index <= 20; ++index) {
      const auto base_name = QStringLiteral("DELIVERY_%1").arg(index, 2, 10, QLatin1Char('0'));
      const auto source_path = QDir(delivery_source_dir).filePath(base_name + QStringLiteral(".png"));
      if (index < 20) {
        QImage source_image(96, 64, QImage::Format_RGB32);
        source_image.fill(QColor(30 + index * 4, 64 + index * 3, 116 + index * 2));
        if (!source_image.save(source_path)) {
          return fail("delivery export visual proof should create 19 available photo sources");
        }
      }
      auto asset = make_asset(QStringLiteral("delivery-visual-%1").arg(index), source_path);
      asset.location_path = source_path;
      asset.adjustment_patch_json =
          photo_editor::desktop::
              canonical_asset_bound_adjustment_patch_json(
                  QStringLiteral(
                      "{\"outputColorSpace\":\"sRGB\"}"));
      asset.online = index < 20;
      delivery_assets.push_back(asset);
    }
    QFile collision_file(
        QDir(delivery_destination).filePath(QStringLiteral("DELIVERY_01-web-proof.jpg")));
    if (!collision_file.open(QIODevice::WriteOnly) ||
        collision_file.write("preserve-existing-visual") <= 0) {
      return fail("delivery export visual proof should create a filename collision");
    }
    collision_file.close();

    catalog_assets.replaceAssetsForTesting(delivery_assets);
    catalog_assets.selectIndex(0);
    catalog_assets.selectAll();
    root->setProperty("currentWorkspace", root->property("exportWorkspaceIndex"));
    process_layout_events(18);
    QElapsedTimer delivery_route_settle;
    delivery_route_settle.start();
    (void)wait_for_condition([&delivery_route_settle]() { return delivery_route_settle.elapsed() >= 240; }, 700);
    auto* export_workspace = find_named_object(root, QStringLiteral("exportStudioWorkspaceContent"));
    if (export_workspace == nullptr) {
      return fail("delivery export visual proof should expose ExportView");
    }
    export_workspace->setProperty("deliveryDestinationPath", delivery_destination);
    export_workspace->setProperty("deliveryPresetIndex", 7);
    process_layout_events(14);
    QElapsedTimer delivery_preflight_settle;
    delivery_preflight_settle.start();
    (void)wait_for_condition(
        [&delivery_preflight_settle]() { return delivery_preflight_settle.elapsed() >= 220; },
        650);
    (void)wait_for_condition([&export_jobs]() { return !export_jobs.preflightBusy(); }, 10000);
    QVariant delivery_preflight_value;
    if (!QMetaObject::invokeMethod(
            export_workspace,
            "currentBatchPreflight",
            Q_RETURN_ARG(QVariant, delivery_preflight_value))) {
      return fail("delivery export visual proof should run the real batch preflight");
    }
    const auto delivery_preflight = delivery_preflight_value.toMap();
    const auto delivery_items = delivery_preflight.value(QStringLiteral("items")).toList();
    if (export_workspace->property("deliveryExportContract").toString() !=
            QStringLiteral("delivery-export:v1|preflight:destination-filenames-collisions-missing-unsupported-unavailable|progress:per-item-overall-nonblocking|actions:pause-resume-cancel-retry-failed-reveal|summary:profile-dimensions-format-metadata-sharpening|collision:numbered-preserve-existing") ||
        delivery_preflight.value(QStringLiteral("assetCount")).toInt() != 20 ||
        delivery_preflight.value(QStringLiteral("readyCount")).toInt() != 19 ||
        delivery_preflight.value(QStringLiteral("blockedCount")).toInt() != 1 ||
        delivery_preflight.value(QStringLiteral("missingOriginalCount")).toInt() != 1 ||
        delivery_preflight.value(QStringLiteral("collisionCount")).toInt() != 1 ||
        delivery_items.isEmpty() ||
        delivery_items.first().toMap().value(QStringLiteral("outputFileName")).toString() !=
            QStringLiteral("DELIVERY_01-web-proof-2.jpg")) {
      return fail("delivery visual preflight should show 20 files, one missing original, and one numbered collision outcome");
    }
    root_window->requestUpdate();
    (void)root_window->grabWindow();
    QElapsedTimer delivery_capture_settle;
    delivery_capture_settle.start();
    (void)wait_for_condition(
        [&delivery_capture_settle]() { return delivery_capture_settle.elapsed() >= 180; },
        550);
    if (!capture_visual_proof(
            root_window,
            root,
            visual_proof_dir,
            QStringLiteral("delivery-export-preflight"),
            1440,
            900,
            {
                QStringLiteral("exportStudioWorkspace"),
                QStringLiteral("exportConfigurationPanel"),
                QStringLiteral("exportDeliveryReviewSummaryCard"),
                QStringLiteral("exportDeliveryReviewSummaryStatus"),
                QStringLiteral("exportDeliveryReviewSummaryPath"),
                QStringLiteral("exportBatchSplitButton"),
            },
            &visual_proof_manifest,
            QJsonObject::fromVariantMap(delivery_preflight))) {
      return fail("delivery export visual proof should capture the mixed batch preflight");
    }

    const auto completed_output =
        QDir(delivery_destination).filePath(QStringLiteral("DELIVERY_02-web-proof.jpg"));
    QImage completed_image(96, 64, QImage::Format_RGB32);
    completed_image.fill(QColor(64, 108, 176));
    if (!completed_image.save(completed_output, "JPG")) {
      return fail("delivery export visual proof should create a completed artifact");
    }
    photo_editor::desktop::PersistentJobRecord completed_delivery;
    completed_delivery.job_id = QStringLiteral("delivery-visual-completed");
    completed_delivery.job_kind = QStringLiteral("asset_export");
    completed_delivery.asset_id = QStringLiteral("delivery-visual-2");
    completed_delivery.media_type = QStringLiteral("photo");
    completed_delivery.display_label = QStringLiteral("DELIVERY_02");
    completed_delivery.source_path = delivery_assets.at(1).file_path;
    completed_delivery.recipe_json =
        delivery_assets.at(1)
            .adjustment_patch_json;
    completed_delivery.preset_name = QStringLiteral("Web Proof");
    completed_delivery.destination_path = delivery_destination;
    completed_delivery.output_path = completed_output;
    completed_delivery.status = QStringLiteral("Completed");
    completed_delivery.progress = 100;
    completed_delivery.created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    completed_delivery.updated_at = completed_delivery.created_at;
    completed_delivery.metadata_json.insert(
        QStringLiteral("deliverySummary"),
        QJsonObject{
            {QStringLiteral("schema"), QStringLiteral("export-delivery-summary:v1")},
            {QStringLiteral("outputPath"), completed_output},
            {QStringLiteral("outputFileName"), QFileInfo(completed_output).fileName()},
            {QStringLiteral("format"), QStringLiteral("JPEG")},
            {QStringLiteral("bitDepth"), 8},
            {QStringLiteral("width"), 96},
            {QStringLiteral("height"), 64},
            {QStringLiteral("outputProfile"), QStringLiteral("sRGB")},
            {QStringLiteral("metadataPolicy"), QStringLiteral("Color profile and delivery verification recorded; camera metadata is not copied.")},
            {QStringLiteral("sharpeningPolicy"), QStringLiteral("No additional output sharpening.")},
            {QStringLiteral("artifactVerified"), true},
        });
    if (!photo_editor::desktop::upsertPersistedJobRecord(completed_delivery, nullptr)) {
      return fail("delivery export visual proof should persist a completed delivery summary");
    }
    job_queue.refresh();
    process_layout_events(14);
    if (!capture_visual_proof(
            root_window,
            root,
            visual_proof_dir,
            QStringLiteral("delivery-export-completed"),
            1440,
            900,
            {
                QStringLiteral("exportStudioWorkspace"),
                QStringLiteral("exportQueuePanel"),
                QStringLiteral("exportOverallProgress"),
                QStringLiteral("exportQueueRow"),
                QStringLiteral("exportDeliveryReviewSummaryCard"),
            },
            &visual_proof_manifest,
            QJsonObject{
                {QStringLiteral("deliverySummary"), completed_delivery.metadata_json.value(QStringLiteral("deliverySummary"))},
                {QStringLiteral("revealTarget"), completed_output},
            })) {
      return fail("delivery export visual proof should capture verified output summary and overall progress");
    }
    QFile manifest_file(QDir(visual_proof_dir).filePath(QStringLiteral("delivery-export-manifest.json")));
    if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        manifest_file.write(QJsonDocument(visual_proof_manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("delivery export visual proof should write its manifest");
    }
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    return EXIT_SUCCESS;
  }

  root->setProperty("currentWorkspace", root->property("editWorkspaceIndex"));
  process_layout_events(6);

  catalog_assets.replaceAssetsForTesting(
      {make_asset(QStringLiteral("asset-1"), QStringLiteral("IMG_0001.jpg"))});
  catalog_assets.selectIndex(0);
  QCoreApplication::processEvents();
  if (editor_preview.assetId() != QStringLiteral("asset-1") || !editor_preview.hasAsset()) {
    return fail("edit workspace should load the selected catalog asset");
  }

  catalog_assets.replaceAssetsForTesting({});
  QCoreApplication::processEvents();
  if (!editor_preview.assetId().isEmpty() || editor_preview.hasAsset()) {
    return fail("edit workspace should clear the editor when the catalog selection disappears");
  }

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_COLOR_ROUTING") &&
      !write_overlay_visual_fixture(
          QStringLiteral("/tmp/IMG_0002.jpg"), QStringLiteral("saturated-small"))) {
    return fail("target routing visual proof should create a real source-image fixture");
  }
  catalog_assets.replaceAssetsForTesting(
      {make_asset(QStringLiteral("asset-2"), QStringLiteral("IMG_0002.jpg"))});
  QCoreApplication::processEvents();
  if (catalog_assets.selectedAssetId() != QStringLiteral("asset-2") ||
      editor_preview.assetId() != QStringLiteral("asset-2")) {
    return fail("edit workspace should follow the next visible asset after the gallery repopulates");
  }

	  auto* edit_workspace = root->findChild<QObject*>(QStringLiteral("editStudioWorkspace"));
	  if (edit_workspace == nullptr) {
	    return fail("edit workspace should expose a testable StudioWorkspace object");
	  }
	  if (auto* edit_workspace_content =
	          edit_workspace->findChild<QObject*>(QStringLiteral("editStudioWorkspaceContent"))) {
	    edit_workspace = edit_workspace_content;
	  }
	  edit_workspace->setProperty("developerDiagnosticsEnabled", true);
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_COLOR_ROUTING")) {
    if (!editor_preview.addFillLayer(QStringLiteral("#6a7480"), QStringLiteral("Routing Proof"))) {
      return fail("color routing proof should create a photo layer target");
    }
    edit_workspace->setProperty("editTool", QStringLiteral("layers"));
    edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
    QVariant photo_layer_selected;
    QVariant adjustment_layer_created;
    QVariant adjustment_scope;
    QVariant photo_layer_scope;
    QVariant photo_layer_write_supported;
    QVariant photo_layer_write_reason;
    QVariant photo_layer_write_result;
    QVariant exposure_routed;
    QVariant temperature_routed;
    QVariant vibrance_routed;
    QVariant detail_routed;
    QVariant lens_routed;
    QVariant transform_routed;
    QVariant string_routed;
    QVariant bool_routed;
    QVariant curve_routed;
    QVariant preset_routed;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "setActivePhotoLayerSelection",
            Q_RETURN_ARG(QVariant, photo_layer_selected),
            Q_ARG(QVariant, QVariant(0)),
            Q_ARG(QVariant, QVariant(false))) ||
        !photo_layer_selected.toBool()) {
      return fail("color routing proof should select a photo layer target");
    }
    const auto base_exposure_before_photo_layer_write = editor_preview.exposure();
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "activeEditTargetScope",
            Q_RETURN_ARG(QVariant, photo_layer_scope)) ||
        photo_layer_scope.toString() != QStringLiteral("photo-layer") ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "adjustmentControlCanWriteActiveTarget",
            Q_RETURN_ARG(QVariant, photo_layer_write_supported),
            Q_ARG(QVariant, QVariant(QStringLiteral("base-image,adjustment-layer")))) ||
        photo_layer_write_supported.toBool() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "adjustmentControlDisabledReason",
            Q_RETURN_ARG(QVariant, photo_layer_write_reason),
            Q_ARG(QVariant, QVariant(QStringLiteral("base-image,adjustment-layer")))) ||
        photo_layer_write_reason.toString().isEmpty() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "setPrimaryAdjustmentValue",
            Q_RETURN_ARG(QVariant, photo_layer_write_result),
            Q_ARG(QVariant, QVariant(QStringLiteral("exposure"))),
            Q_ARG(QVariant, QVariant(1.25))) ||
        photo_layer_write_result.toBool() ||
        std::abs(editor_preview.exposure() - base_exposure_before_photo_layer_write) > 0.0001) {
      return fail("unsupported fill-layer adjustments should be disabled with a reason and never fall back to Base Image");
    }
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "addAdjustmentLayerForCurrentTarget",
            Q_RETURN_ARG(QVariant, adjustment_layer_created)) ||
        !adjustment_layer_created.toBool()) {
      return fail("color routing proof should create and select an adjustment layer from a photo layer context");
    }
    process_layout_events(4);
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "activeEditTargetScope",
            Q_RETURN_ARG(QVariant, adjustment_scope)) ||
        adjustment_scope.toString() != QStringLiteral("adjustment-layer") ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "routePrimaryPresetCommand",
            Q_RETURN_ARG(QVariant, preset_routed),
            Q_ARG(QVariant, QVariant(QStringLiteral("Landscape Pop"))),
            Q_ARG(QVariant, QVariant(QStringLiteral("base-image,adjustment-layer")))) ||
        !preset_routed.toBool() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "setPrimaryAdjustmentValue",
            Q_RETURN_ARG(QVariant, exposure_routed),
            Q_ARG(QVariant, QVariant(QStringLiteral("exposure"))),
            Q_ARG(QVariant, QVariant(0.65))) ||
        !exposure_routed.toBool() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "setPrimaryAdjustmentValue",
            Q_RETURN_ARG(QVariant, temperature_routed),
            Q_ARG(QVariant, QVariant(QStringLiteral("temperature"))),
            Q_ARG(QVariant, QVariant(24.0))) ||
        !temperature_routed.toBool() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "setPrimaryAdjustmentValue",
            Q_RETURN_ARG(QVariant, vibrance_routed),
            Q_ARG(QVariant, QVariant(QStringLiteral("vibrance"))),
            Q_ARG(QVariant, QVariant(38.0))) ||
        !vibrance_routed.toBool() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "setPrimaryAdjustmentValue",
            Q_RETURN_ARG(QVariant, detail_routed),
            Q_ARG(QVariant, QVariant(QStringLiteral("texture"))),
            Q_ARG(QVariant, QVariant(22.0))) ||
        !detail_routed.toBool() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "setPrimaryAdjustmentValue",
            Q_RETURN_ARG(QVariant, lens_routed),
            Q_ARG(QVariant, QVariant(QStringLiteral("lensDistortion"))),
            Q_ARG(QVariant, QVariant(-12.0))) ||
        !lens_routed.toBool() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "setPrimaryAdjustmentValue",
            Q_RETURN_ARG(QVariant, transform_routed),
            Q_ARG(QVariant, QVariant(QStringLiteral("transformScale"))),
            Q_ARG(QVariant, QVariant(1.2))) ||
        !transform_routed.toBool() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "setPrimaryStringAdjustmentValue",
            Q_RETURN_ARG(QVariant, string_routed),
            Q_ARG(QVariant, QVariant(QStringLiteral("hslChannel"))),
            Q_ARG(QVariant, QVariant(QStringLiteral("Blues")))) ||
        !string_routed.toBool() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "setPrimaryBoolAdjustmentValue",
            Q_RETURN_ARG(QVariant, bool_routed),
            Q_ARG(QVariant, QVariant(QStringLiteral("blackAndWhiteEnabled"))),
            Q_ARG(QVariant, QVariant(true))) ||
        !bool_routed.toBool()) {
      return fail("adjustment-layer target should route every primary numeric, string, and boolean family through its layer recipe");
    }
    QVariantList routed_curve_points{
        QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
        QVariantMap{{QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.62}},
        QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
    };
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "setPrimaryCurvePoints",
            Q_RETURN_ARG(QVariant, curve_routed),
            Q_ARG(QVariant, QVariant(routed_curve_points))) ||
        !curve_routed.toBool()) {
      return fail("adjustment-layer target should route tone curves through the same destination contract");
    }
    const auto routed_layer = editor_preview.photoLayers().constFirst().toMap();
    const auto routed_color_mixer = routed_layer.value(QStringLiteral("adjustments"))
                                        .toMap()
                                        .value(QStringLiteral("colorMixer"))
                                        .toMap();
    const auto routed_black_and_white =
        routed_color_mixer.value(QStringLiteral("blackAndWhite")).toMap();
    if (routed_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("adjustment") ||
        std::abs(routed_layer.value(QStringLiteral("exposure")).toDouble() - 0.65) > 0.0001 ||
        std::abs(routed_layer.value(QStringLiteral("temperature")).toDouble() - 24.0) > 0.0001 ||
        std::abs(routed_layer.value(QStringLiteral("vibrance")).toDouble() - 38.0) > 0.0001 ||
        std::abs(routed_layer.value(QStringLiteral("texture")).toDouble() - 22.0) > 0.0001 ||
        std::abs(routed_layer.value(QStringLiteral("lensDistortion")).toDouble() + 12.0) > 0.0001 ||
        std::abs(routed_layer.value(QStringLiteral("transformScale")).toDouble() - 1.2) > 0.0001 ||
        routed_color_mixer.value(QStringLiteral("channel")).toString() != QStringLiteral("Blues") ||
        !routed_black_and_white.value(QStringLiteral("enabled")).toBool() ||
        routed_layer.value(QStringLiteral("masterCurvePoints")).toList().size() != 3) {
      return fail("routed primary families should persist only on the active adjustment-layer recipe");
    }
    auto* routing_target_bar =
        find_named_object(edit_workspace, QStringLiteral("activeEditTargetBar"));
    auto* routing_target_label =
        find_named_object(edit_workspace, QStringLiteral("activeEditTargetBreadcrumbLabel"));
    QVariant routing_matrix_signature;
    QVariant raw_layer_write;
    if (routing_target_bar == nullptr || routing_target_label == nullptr ||
        !item_visible(qobject_cast<QQuickItem*>(routing_target_bar)) ||
        routing_target_bar->property("targetScope").toString() != QStringLiteral("adjustment-layer") ||
        routing_target_label->property("text").toString().isEmpty() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "targetRoutingMatrixSignature",
            Q_RETURN_ARG(QVariant, routing_matrix_signature)) ||
        !routing_matrix_signature.toString().contains(QStringLiteral("displayed:adjustment-layer")) ||
        !routing_matrix_signature.toString().contains(QStringLiteral("single-destination:layer-recipe")) ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "setPrimaryBaseAdjustmentValue",
            Q_RETURN_ARG(QVariant, raw_layer_write),
            Q_ARG(QVariant, QVariant(QStringLiteral("rawExposure"))),
            Q_ARG(QVariant, QVariant(1.0))) ||
        raw_layer_write.toBool()) {
      return fail("visible target breadcrumb and family matrix should agree with the active layer destination and block base-only RAW writes");
    }
    if (!editor_preview.setPhotoLayerLocked(0, true)) {
      return fail("target routing proof should lock its adjustment layer");
    }
    process_layout_events(2);
    QVariant locked_write_supported;
    QVariant locked_write_reason;
    QVariant locked_write_result;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "adjustmentControlCanWriteActiveTarget",
            Q_RETURN_ARG(QVariant, locked_write_supported),
            Q_ARG(QVariant, QVariant(QStringLiteral("base-image,adjustment-layer")))) ||
        locked_write_supported.toBool() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "adjustmentControlDisabledReason",
            Q_RETURN_ARG(QVariant, locked_write_reason),
            Q_ARG(QVariant, QVariant(QStringLiteral("base-image,adjustment-layer")))) ||
        !locked_write_reason.toString().contains(QStringLiteral("Unlock")) ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "setPrimaryAdjustmentValue",
            Q_RETURN_ARG(QVariant, locked_write_result),
            Q_ARG(QVariant, QVariant(QStringLiteral("exposure"))),
            Q_ARG(QVariant, QVariant(1.5))) ||
        locked_write_result.toBool() ||
        std::abs(editor_preview.photoLayers().constFirst().toMap().value(QStringLiteral("exposure")).toDouble() - 0.65) > 0.0001) {
      return fail("locked adjustment targets should disable commands with an unlock explanation instead of silently doing nothing");
    }
    if (!editor_preview.setPhotoLayerLocked(0, false)) {
      return fail("target routing proof should restore its adjustment layer lock state");
    }
    const auto base_curve_before = editor_preview.activeCurvePoints();
    QVariant photo_layer_reselected;
    QVariant unsupported_curve_routed;
    QVariantList unsupported_curve_points{
        QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
        QVariantMap{{QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.7}},
        QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
    };
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "setActivePhotoLayerSelection",
            Q_RETURN_ARG(QVariant, photo_layer_reselected),
            Q_ARG(QVariant, QVariant(1)),
            Q_ARG(QVariant, QVariant(false))) ||
        !photo_layer_reselected.toBool() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "setPrimaryCurvePoints",
            Q_RETURN_ARG(QVariant, unsupported_curve_routed),
            Q_ARG(QVariant, QVariant(unsupported_curve_points))) ||
        unsupported_curve_routed.toBool() ||
        editor_preview.activeCurvePoints() != base_curve_before) {
      return fail("tone curve should obey the same target guard instead of hiding a dead non-curve color route");
    }
    QVariant returned_to_base;
    QVariant base_scope;
    QVariant base_write;
    QVariant raw_targets;
    QVariant crop_targets;
    QVariant light_targets;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "returnToBaseImageFromShortcut",
            Q_RETURN_ARG(QVariant, returned_to_base)) ||
        !returned_to_base.toBool() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "activeEditTargetScope",
            Q_RETURN_ARG(QVariant, base_scope)) ||
        base_scope.toString() != QStringLiteral("base-image") ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "setPrimaryAdjustmentValue",
            Q_RETURN_ARG(QVariant, base_write),
            Q_ARG(QVariant, QVariant(QStringLiteral("exposure"))),
            Q_ARG(QVariant, QVariant(0.4))) ||
        !base_write.toBool() ||
        std::abs(editor_preview.exposure() - 0.4) > 0.0001 ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "primaryAdjustmentFamilyTargets",
            Q_RETURN_ARG(QVariant, raw_targets),
            Q_ARG(QVariant, QVariant(QStringLiteral("raw")))) ||
        raw_targets.toString() != QStringLiteral("base-image") ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "primaryAdjustmentFamilyTargets",
            Q_RETURN_ARG(QVariant, crop_targets),
            Q_ARG(QVariant, QVariant(QStringLiteral("crop")))) ||
        crop_targets.toString() != QStringLiteral("base-image") ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "primaryAdjustmentFamilyTargets",
            Q_RETURN_ARG(QVariant, light_targets),
            Q_ARG(QVariant, QVariant(QStringLiteral("light")))) ||
        light_targets.toString() != QStringLiteral("base-image,adjustment-layer")) {
      return fail("Base Image routing and family support declarations should match the visible target matrix");
    }
    QVariant selection_created;
    QVariant selection_scope;
    QVariant selection_layer_index;
    QVariant selection_write;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "createMaskFromShortcut",
            Q_RETURN_ARG(QVariant, selection_created)) ||
        !selection_created.toBool()) {
      return fail("selection routing matrix should create an active brush selection");
    }
    editor_preview.beginActiveBrushStroke();
    editor_preview.addBrushPoint(0.30, 0.34);
    editor_preview.addBrushPoint(0.40, 0.43);
    editor_preview.addBrushPoint(0.50, 0.52);
    editor_preview.addBrushPoint(0.60, 0.60);
    editor_preview.addBrushPoint(0.70, 0.66);
    editor_preview.endActiveBrushStroke();
    process_layout_events(4);
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "activeEditTargetScope",
            Q_RETURN_ARG(QVariant, selection_scope)) ||
        selection_scope.toString() != QStringLiteral("mask") ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "activeAdjustmentLayerIndex",
            Q_RETURN_ARG(QVariant, selection_layer_index)) ||
        selection_layer_index.toInt() < 0 ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "setPrimaryAdjustmentValue",
            Q_RETURN_ARG(QVariant, selection_write),
            Q_ARG(QVariant, QVariant(QStringLiteral("exposure"))),
            Q_ARG(QVariant, QVariant(0.27))) ||
        !selection_write.toBool()) {
      return fail("active selection should resolve primary controls to its linked adjustment-layer recipe");
    }
    const auto selection_layer =
        editor_preview.photoLayers().at(selection_layer_index.toInt()).toMap();
    if (!selection_layer.value(QStringLiteral("selectionScoped")).toBool() ||
        selection_layer.value(QStringLiteral("selectionMaskSetId")).toString().isEmpty() ||
        std::abs(selection_layer.value(QStringLiteral("exposure")).toDouble() - 0.27) > 0.0001 ||
        std::abs(editor_preview.exposure() - 0.4) > 0.0001) {
      return fail("selection adjustments should change only the linked layer and preserve Base Image state");
    }
    for (int layer_index = 0; layer_index < editor_preview.photoLayerCount(); ++layer_index) {
      const auto layer = editor_preview.photoLayers().at(layer_index).toMap();
      if (layer_index != selection_layer_index.toInt()) {
        editor_preview.setPhotoLayerEnabled(layer_index, false);
      }
    }
    editor_preview.setMaskPreviewOverlayColor(QStringLiteral("#ff3d7f"));
    editor_preview.setMaskPreviewOverlayOpacity(0.62);
    editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
    if (!wait_for_condition(
            [&]() { return editor_preview.activeMaskBrushPointCount() >= 5; },
            10000)) {
      return fail("mask/layer visual proof should publish the visible brush region before capture");
    }
    const auto settle_preview_for_visual_proof = [&]() {
      const auto preview_failed = [&]() {
        const auto status = editor_preview.status().toLower();
        return status.contains(QStringLiteral("fail")) ||
               status.contains(QStringLiteral("error")) ||
               status.contains(QStringLiteral("unavailable")) ||
               status.contains(QStringLiteral("unable"));
      };
      for (auto attempt = 0; attempt < 3; ++attempt) {
        process_layout_events(4);
        if (!preview_failed()) {
          return true;
        }
        wait_for_condition([&]() { return !editor_preview.processing(); }, 10000);
        if (!preview_failed()) {
          return true;
        }
        std::cerr << "Visual proof preview retry: stage="
                  << editor_preview.previewFailureStage().toStdString()
                  << " status=" << editor_preview.status().toStdString()
                  << " detail=" << editor_preview.previewFailureDetail().toStdString() << '\n';
        if (!editor_preview.retryPreviewRender()) {
          return false;
        }
        wait_for_condition(
            [&]() { return !editor_preview.processing(); },
            15000);
      }
      process_layout_events(4);
      return !preview_failed();
    };
    if (!settle_preview_for_visual_proof()) {
      return fail("visual proof should not capture a failed preview state");
    }
    process_layout_events(12);
    auto* first_run_guide = find_named_object(root, QStringLiteral("firstRunGuideDialog"));
    if (first_run_guide != nullptr && first_run_guide->property("visible").toBool()) {
      if (!QMetaObject::invokeMethod(first_run_guide, "close") ||
          !wait_for_condition(
              [first_run_guide]() { return !first_run_guide->property("visible").toBool(); },
              1500)) {
        return fail("mask/layer visual proof should close onboarding before capture");
      }
      app_shell.completeFirstRunExperience();
    }
    edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
    process_layout_events(4);
    const QStringList mask_layer_proof_objects{
        QStringLiteral("editImageViewport"),
        QStringLiteral("selectedMaskHeader"),
        QStringLiteral("photoLayerRow"),
        QStringLiteral("photoLayerNestedMaskRow"),
    };
    if (!capture_visual_proof(
            root_window,
            root,
            visual_proof_dir,
            QStringLiteral("mask-layer-identity-normal"),
            1440,
            900,
            mask_layer_proof_objects,
            &visual_proof_manifest,
            QJsonObject{{QStringLiteral("target"), QStringLiteral("selection-linked-layer")}}) ||
        !capture_visual_proof(
            root_window,
            root,
            visual_proof_dir,
            QStringLiteral("mask-layer-identity-narrow"),
            980,
            760,
            mask_layer_proof_objects,
            &visual_proof_manifest,
            QJsonObject{{QStringLiteral("target"), QStringLiteral("selection-linked-layer")}})) {
      return fail("mask/layer proof should keep the selected mask, parent layer, and affected canvas visible at normal and narrow widths");
    }
    QFile mask_layer_manifest_file(
        QDir(visual_proof_dir).filePath(QStringLiteral("mask-layer-identity-manifest.json")));
    if (!mask_layer_manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        mask_layer_manifest_file.write(
            QJsonDocument(visual_proof_manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("mask/layer proof should write its visual manifest");
    }
    mask_layer_manifest_file.close();
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "activateEditInspectorTab",
            Q_ARG(QVariant, QVariant(QStringLiteral("history"))),
            Q_ARG(QVariant, QVariant()))) {
      return fail("Studio should expose the History inspector tab through its normal tab router");
    }
    process_layout_events(6);
    auto* history_tab_button =
        find_named_object(edit_workspace, QStringLiteral("historyInspectorTabButton"));
    auto* history_panel =
        find_named_object(edit_workspace, QStringLiteral("editHistoryPanel"));
    auto* history_snapshot_field =
        find_named_object(edit_workspace, QStringLiteral("editHistorySnapshotNameField"));
    auto* history_snapshot_button =
        find_named_object(edit_workspace, QStringLiteral("createEditHistorySnapshotButton"));
    auto* batch_history_section =
        find_named_object(edit_workspace, QStringLiteral("batchEditHistorySection"));
    auto* assistant_history_section =
        find_named_object(edit_workspace, QStringLiteral("assistantEditHistorySection"));
    const auto history_rows =
        find_named_items(edit_workspace, QStringLiteral("editHistoryEntryRow"));
    if (edit_workspace->property("editInspectorTab").toString() != QStringLiteral("history") ||
        history_tab_button == nullptr || history_panel == nullptr ||
        history_snapshot_field == nullptr || history_snapshot_button == nullptr ||
        batch_history_section == nullptr || assistant_history_section == nullptr ||
        !item_visible(qobject_cast<QQuickItem*>(history_tab_button)) ||
        !item_visible(qobject_cast<QQuickItem*>(history_panel)) ||
        !history_snapshot_button->property("enabled").toBool() ||
        history_rows.size() < 2 ||
        history_panel->property("editHistoryPanelContract").toString() !=
            QStringLiteral("edit-history-panel:v2|chronological:user-label-value-scope-state|restore:any-step|snapshots:named-persistent|batch:restorable|assistant:provenance-and-recipe-link|preview-actions:excluded|session-undo:explicit") ||
        batch_history_section->property("batchEditHistoryContract").toString() !=
            QStringLiteral("batch-edit-history:v1|same-panel:true|ordered:true|restore:any-step|one-operation:true|affected-count:true") ||
        assistant_history_section->property("assistantEditHistoryContract").toString() !=
            QStringLiteral("assistant-edit-history:v1|same-panel:true|run-provenance:true|recipe-change-linked-to-undo:true|read-only-actions-labeled:true")) {
      return fail("History inspector should show ordered recipe steps and named persistent restore controls");
    }
    const auto history_snapshot_id =
        editor_preview.createEditHistorySnapshot(QStringLiteral("Color approved"));
    process_layout_events(4);
    auto* history_snapshot_summary =
        find_named_object(edit_workspace, QStringLiteral("editHistorySnapshotSummary"));
    if (history_snapshot_id.isEmpty() || editor_preview.editHistorySnapshots().size() != 1 ||
        find_named_items(edit_workspace, QStringLiteral("editHistorySnapshotRow")).size() != 1 ||
        history_snapshot_summary == nullptr ||
        !item_visible(qobject_cast<QQuickItem*>(history_snapshot_summary))) {
      return fail("History inspector should publish a named snapshot without adding a recipe step");
    }
    if (!settle_preview_for_visual_proof()) {
      return fail("History visual proof should not capture a failed preview state");
    }
    QJsonArray history_visual_manifest;
    const QStringList history_proof_objects{
        QStringLiteral("historyInspectorTabButton"),
        QStringLiteral("editHistoryPanel"),
        QStringLiteral("editHistoryEntryRow"),
        QStringLiteral("editHistorySnapshotSummary"),
    };
    if (!capture_visual_proof(
            root_window,
            root,
            visual_proof_dir,
            QStringLiteral("edit-history-normal"),
            1440,
            900,
            history_proof_objects,
            &history_visual_manifest,
            QJsonObject{{QStringLiteral("entries"), editor_preview.editHistoryEntries().size()},
                        {QStringLiteral("snapshots"), editor_preview.editHistorySnapshots().size()}})) {
      return fail("History inspector should remain readable with chronological entries and a named snapshot");
    }
    QFile history_manifest_file(
        QDir(visual_proof_dir).filePath(QStringLiteral("edit-history-manifest.json")));
    if (!history_manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        history_manifest_file.write(
            QJsonDocument(history_visual_manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("History proof should write its visual manifest");
    }
    history_manifest_file.close();
    const auto comparison_full_resolution_settled = [&]() {
      if (editor_preview.processing())
        return false;
      if (editor_preview.displayPreviewNativeAvailable())
        return editor_preview.displayPreviewNativeFullResolution();
      return editor_preview.status().contains(
          QStringLiteral("Full-resolution"), Qt::CaseInsensitive);
    };
    if (!wait_for_condition(
            comparison_full_resolution_settled,
            30000)) {
      if (!editor_preview.requestLayerFullResolutionPreview() ||
          !wait_for_condition(
              comparison_full_resolution_settled,
              30000)) {
        std::cerr << "Comparison precondition: status="
                  << editor_preview.status().toStdString()
                  << " processing=" << editor_preview.processing()
                  << " fullNative=" << editor_preview.displayPreviewNativeFullResolution()
                  << " failureStage=" << editor_preview.previewFailureStage().toStdString()
                  << " failureDetail=" << editor_preview.previewFailureDetail().toStdString()
                  << '\n';
        return fail("comparison proof requires a settled edited full-resolution texture to preserve");
      }
    }
    const auto comparison_recipe_before = editor_preview.currentAdjustmentPatchJson();
    const auto comparison_history_before = editor_preview.editHistoryEntries();
    const auto comparison_history_index_before = editor_preview.editHistoryCurrentIndex();
    editor_preview.setCompareSnapshotId(history_snapshot_id);
    editor_preview.setCompareSourceMode(QStringLiteral("snapshot"));
    edit_workspace->setProperty("compareViewMode", QStringLiteral("split"));
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "activateEditTool",
            Q_ARG(QVariant, QVariant(QStringLiteral("compare"))))) {
      return fail("comparison proof should activate through the normal Compare tool");
    }
    process_layout_events(6);
    auto* compare_source_controls =
        find_named_object(edit_workspace, QStringLiteral("compareSourceControls"));
    auto* compare_before_badge =
        find_named_object(edit_workspace, QStringLiteral("compareBeforeOrientationBadge"));
    auto* compare_after_badge =
        find_named_object(edit_workspace, QStringLiteral("compareAfterOrientationBadge"));
    auto* compare_snapshot_selector =
        find_named_object(edit_workspace, QStringLiteral("compareSnapshotSelector"));
    if (edit_workspace->property("comparisonUxContract").toString() !=
            QStringLiteral("comparison-ux:v1|hold:B-and-toolbar|sources:original-external-named-snapshot|layouts:split-wipe-side-by-side|labels:before-after-source|viewport:sync-zoom-pan|release:edited-full-resolution|recipe-history:immutable") ||
        compare_source_controls == nullptr || compare_before_badge == nullptr ||
        compare_after_badge == nullptr || compare_snapshot_selector == nullptr ||
        !item_visible(qobject_cast<QQuickItem*>(compare_source_controls)) ||
        !item_visible(qobject_cast<QQuickItem*>(compare_before_badge)) ||
        !item_visible(qobject_cast<QQuickItem*>(compare_after_badge)) ||
        !item_visible(qobject_cast<QQuickItem*>(compare_snapshot_selector)) ||
        editor_preview.compareSourceMode() != QStringLiteral("snapshot") ||
        editor_preview.compareSnapshotName() != QStringLiteral("Color approved")) {
      return fail("split comparison should expose named snapshot source controls and explicit Before/After orientation");
    }
    QVariant before_title;
    QVariant after_title;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "comparePanelTitle",
            Q_RETURN_ARG(QVariant, before_title),
            Q_ARG(QVariant, QVariant(0))) ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "comparePanelTitle",
            Q_RETURN_ARG(QVariant, after_title),
            Q_ARG(QVariant, QVariant(1))) ||
        before_title.toString() != QStringLiteral("Before · Snapshot: Color approved") ||
        after_title.toString() != QStringLiteral("After · Current edit")) {
      return fail("comparison orientation labels should name the historical source and current edit");
    }
    edit_workspace->setProperty("compareViewMode", QStringLiteral("wipe"));
    process_layout_events(4);
    if (!item_visible(qobject_cast<QQuickItem*>(compare_before_badge)) ||
        !item_visible(qobject_cast<QQuickItem*>(compare_after_badge)) ||
        compare_before_badge->property("x").toDouble() <=
            compare_after_badge->property("x").toDouble()) {
      return fail("wipe comparison should label Current on the left and Before on the right");
    }
    edit_workspace->setProperty("compareViewMode", QStringLiteral("side-by-side"));
    process_layout_events(6);
    auto* compare_viewport_before =
        find_named_object(edit_workspace, QStringLiteral("compareImageViewport0"));
    auto* compare_viewport_after =
        find_named_object(edit_workspace, QStringLiteral("compareImageViewport1"));
    auto* compare_panel_before =
        find_named_object(edit_workspace, QStringLiteral("comparePanel0"));
    auto* compare_panel_after =
        find_named_object(edit_workspace, QStringLiteral("comparePanel1"));
    if (compare_viewport_before == nullptr || compare_viewport_after == nullptr ||
        compare_panel_before == nullptr || compare_panel_after == nullptr ||
        !item_visible(qobject_cast<QQuickItem*>(compare_viewport_before)) ||
        !item_visible(qobject_cast<QQuickItem*>(compare_viewport_after)) ||
        compare_panel_before->property("title").toString() != before_title.toString() ||
        compare_panel_after->property("title").toString() != after_title.toString()) {
      return fail("side-by-side comparison should keep both source labels visible");
    }
    compare_viewport_before->setProperty("zoom", 1.75);
    compare_viewport_before->setProperty("panX", 42.0);
    compare_viewport_before->setProperty("panY", -18.0);
    process_layout_events(6);
    if (std::abs(compare_viewport_after->property("zoom").toDouble() - 1.75) > 0.001 ||
        std::abs(compare_viewport_after->property("panX").toDouble() - 42.0) > 0.001 ||
        std::abs(compare_viewport_after->property("panY").toDouble() + 18.0) > 0.001) {
      return fail("side-by-side comparison should synchronize zoom and pan across both sources");
    }
    edit_workspace->setProperty("compareViewMode", QStringLiteral("split"));
    process_layout_events(4);
    if (!settle_preview_for_visual_proof()) {
      return fail("comparison visual proof should settle without replacing the last good preview");
    }
    QJsonArray comparison_visual_manifest;
    const QStringList comparison_proof_objects{
        QStringLiteral("beforeAfterSplitPanel"),
        QStringLiteral("compareSourceControls"),
        QStringLiteral("compareBeforeOrientationBadge"),
        QStringLiteral("compareAfterOrientationBadge"),
    };
    if (!capture_visual_proof(
            root_window,
            root,
            visual_proof_dir,
            QStringLiteral("before-after-comparison"),
            1440,
            900,
            comparison_proof_objects,
            &comparison_visual_manifest,
            QJsonObject{{QStringLiteral("source"), editor_preview.compareSnapshotName()},
                        {QStringLiteral("recipeHistoryImmutable"), true}})) {
      return fail("comparison proof should capture readable source and orientation labels");
    }
    QFile comparison_manifest_file(
        QDir(visual_proof_dir).filePath(QStringLiteral("before-after-comparison-manifest.json")));
    if (!comparison_manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        comparison_manifest_file.write(
            QJsonDocument(comparison_visual_manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("comparison proof should write its visual manifest");
    }
    comparison_manifest_file.close();
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "activateEditTool",
            Q_ARG(QVariant, QVariant(QStringLiteral("adjust"))))) {
      return fail("comparison proof should return to current editing context");
    }
    process_layout_events(4);
    QVariant original_hold_started;
    QVariant original_hold_stopped;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "setOriginalHold",
            Q_RETURN_ARG(QVariant, original_hold_started),
            Q_ARG(QVariant, QVariant(true))) ||
        !original_hold_started.toBool() || !editor_preview.compareMode() ||
        !edit_workspace->property("originalHoldActive").toBool() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "setOriginalHold",
            Q_RETURN_ARG(QVariant, original_hold_stopped),
            Q_ARG(QVariant, QVariant(false))) ||
        !original_hold_stopped.toBool() || editor_preview.compareMode() ||
        edit_workspace->property("originalHoldActive").toBool()) {
      return fail("press-and-hold Original should show Before only while held and restore the edit on release");
    }
    if (!wait_for_condition(comparison_full_resolution_settled, 30000)) {
      std::cerr << "Original release settlement: status="
                << editor_preview.status().toStdString()
                << " processing=" << editor_preview.processing()
                << " fullNative=" << editor_preview.displayPreviewNativeFullResolution()
                << " failureStage=" << editor_preview.previewFailureStage().toStdString()
                << " failureDetail=" << editor_preview.previewFailureDetail().toStdString()
                << '\n';
      return fail("releasing Original should restore the current edited full-resolution preview");
    }
    if (editor_preview.currentAdjustmentPatchJson() != comparison_recipe_before ||
        editor_preview.editHistoryEntries() != comparison_history_before ||
        editor_preview.editHistoryCurrentIndex() != comparison_history_index_before) {
      return fail("comparison layouts, snapshot source, zoom, pan, and Original hold should leave recipe and history hashes unchanged");
    }
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "returnToBaseImageFromShortcut",
            Q_RETURN_ARG(QVariant, returned_to_base)) ||
        !returned_to_base.toBool()) {
      return fail("selection routing matrix should reset transient targeting back to Base Image");
    }
    catalog_assets.replaceAssetsForTesting({
        make_asset(QStringLiteral("batch-route-1"), QStringLiteral("BATCH_0001.jpg")),
        make_asset(QStringLiteral("batch-route-2"), QStringLiteral("BATCH_0002.jpg")),
    });
    catalog_assets.selectAll();
    process_layout_events(4);
    auto* multi_photo_chip =
        find_named_object(edit_workspace, QStringLiteral("activeEditTargetMultiPhotoChip"));
    QVariant multi_photo_matrix_signature;
    QVariant batch_targets;
    if (catalog_assets.selectedAssetCount() != 2 || multi_photo_chip == nullptr ||
        !item_visible(qobject_cast<QQuickItem*>(multi_photo_chip)) ||
        multi_photo_chip->property("text").toString() != QStringLiteral("2 selected") ||
        !multi_photo_chip->property("tooltipText").toString().contains(QStringLiteral("Sync Selected Photos")) ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "targetRoutingMatrixSignature",
            Q_RETURN_ARG(QVariant, multi_photo_matrix_signature)) ||
        !multi_photo_matrix_signature.toString().contains(QStringLiteral("batch-selected:true")) ||
        !multi_photo_matrix_signature.toString().contains(QStringLiteral("batch-destination:explicit-sync")) ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "primaryAdjustmentFamilyTargets",
            Q_RETURN_ARG(QVariant, batch_targets),
            Q_ARG(QVariant, QVariant(QStringLiteral("batch-sync")))) ||
        batch_targets.toString() != QStringLiteral("multi-photo")) {
      return fail("multi-photo selection should be visible before controls and route only through explicit Batch Sync");
    }
    if (catalog_assets.applyAdjustmentPatchJsonToAssetIds(
            catalog_assets.selectedAssetIds(),
            catalog_assets.selectedAdjustmentPatchJson(),
            {QStringLiteral("tone")}) != 2) {
      return fail("history proof should create one real two-photo batch adjustment step");
    }
    process_layout_events(4);
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "activateEditInspectorTab",
            Q_ARG(QVariant, QVariant(QStringLiteral("history"))),
            Q_ARG(QVariant, QVariant()))) {
      return fail("batch history proof should return through the normal History tab router");
    }
    process_layout_events(4);
    batch_history_section =
        find_named_object(edit_workspace, QStringLiteral("batchEditHistorySection"));
    const auto batch_history_rows =
        find_named_items(edit_workspace, QStringLiteral("batchEditHistoryEntryRow"));
    if (batch_history_section == nullptr ||
        !item_visible(qobject_cast<QQuickItem*>(batch_history_section)) ||
        batch_history_rows.size() != 2 ||
        catalog_assets.batchRecipeHistoryCurrentIndex() != 1 ||
        catalog_assets.batchRecipeHistoryEntries().at(1).toMap().value(QStringLiteral("label")).toString() !=
            QStringLiteral("Synced adjustments across 2 photos")) {
      return fail("History inspector should include an ordered and restorable batch operation in the same panel");
    }
    catalog_assets.restoreBatchRecipeHistoryIndex(0);
    if (catalog_assets.batchRecipeHistoryCurrentIndex() != 0 ||
        !catalog_assets.canRedoBatchRecipe()) {
      return fail("History inspector batch restore should return every affected photo to its prior recipe");
    }
    catalog_assets.restoreBatchRecipeHistoryIndex(1);
    if (catalog_assets.batchRecipeHistoryCurrentIndex() != 1) {
      return fail("History inspector batch restore should reconstruct the applied batch recipe");
    }
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    editor_preview.setImageProvider(nullptr);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return EXIT_SUCCESS;
  }
  auto* edit_sidebar_toggle_button =
      find_named_object(edit_workspace, QStringLiteral("editSidebarToggleButton"));
  if (edit_sidebar_toggle_button == nullptr ||
      !edit_workspace->property("editSidebarCollapsed").toBool() ||
      edit_workspace->property("editContextPanelActive").toBool() ||
      edit_sidebar_toggle_button->property("tooltip").toString() !=
          QStringLiteral("Show library panel")) {
    return fail("edit workspace should default the left Library panel closed with a clear arrow tooltip");
  }
  edit_workspace->setProperty("editSidebarCollapsed", false);
  QCoreApplication::processEvents();
  if (!edit_workspace->property("editContextPanelActive").toBool() ||
      edit_sidebar_toggle_button->property("tooltip").toString() !=
          QStringLiteral("Hide library panel")) {
    return fail("edit workspace Library arrow should expose the panel and update its tooltip");
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_EDIT_SIDEBAR_DEFAULT")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    editor_preview.setImageProvider(nullptr);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return EXIT_SUCCESS;
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_FIRST_INTERACTION_RESPONSIVENESS") &&
      !wait_for_condition(
          [&]() { return !edit_workspace->property("studioRenderWorkActive").toBool(); },
          10000)) {
    return fail("first-interaction proof should start after the initial preview has settled");
  }
  auto* studio_edit_tool_rail = find_named_object(edit_workspace, QStringLiteral("studioEditToolRail"));
  QVariant edit_tool_model_signature;
	  QVariant edit_tool_lifecycle_signature;
		  QVariant edit_tool_discovery_signature;
		  QVariant edit_tool_navigation_signature;
		  QVariant studio_motion_signature;
		  QVariant edit_workflow_polish_signature;
		  QVariant tool_switch_cleanup_signature;
  if (studio_edit_tool_rail == nullptr ||
      edit_workspace->property("editWorkflowPolishContract").toString() !=
          QStringLiteral("edit-workflow-polish:v1|first-glance:tool-rail-image-stage-active-tool-inspector-filmstrip|close-inspection:active-family-variant-target-hud-pending-actions|clutter:one-active-hud-one-primary-action-diagnostics-collapsed|interaction:select-expand-switch-commit-cancel-customize|text:no-elide-no-overlap|functionality:preserved") ||
      edit_workspace->property("editPageCompletionContract").toString() !=
          QStringLiteral("page-completion:v1|edit|rail:primary-tool-families|content:image-viewport-toolbar-hud|inspector:active-tool-targeted-sections-layers-masks-agent|filmstrip:stable-selection|bottom:rating-flag-export|states:no-photo-active-tool-pending-disabled-error") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "editToolModelSignature",
          Q_RETURN_ARG(QVariant, edit_tool_model_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "editToolLifecycleSignature",
          Q_RETURN_ARG(QVariant, edit_tool_lifecycle_signature)) ||
	      !QMetaObject::invokeMethod(
	          edit_workspace,
	          "activeEditToolDiscoverySignature",
	          Q_RETURN_ARG(QVariant, edit_tool_discovery_signature)) ||
		      !QMetaObject::invokeMethod(
		          edit_workspace,
		          "editToolRailNavigationSignature",
		          Q_RETURN_ARG(QVariant, edit_tool_navigation_signature)) ||
		      !QMetaObject::invokeMethod(
		          edit_workspace,
		          "studioMotionSignature",
		          Q_RETURN_ARG(QVariant, studio_motion_signature)) ||
		      !QMetaObject::invokeMethod(
		          edit_workspace,
          "editWorkflowPolishSignature",
          Q_RETURN_ARG(QVariant, edit_workflow_polish_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "toolSwitchCleanupSignature",
          Q_RETURN_ARG(QVariant, tool_switch_cleanup_signature)) ||
      !edit_tool_model_signature.toString().contains(QStringLiteral("adjust:")) ||
      !edit_tool_model_signature.toString().contains(QStringLiteral("paint:")) ||
      !edit_tool_model_signature.toString().contains(QStringLiteral("crop:")) ||
      !edit_tool_model_signature.toString().contains(QStringLiteral("masks:")) ||
	      !edit_tool_model_signature.toString().contains(QStringLiteral("compare:")) ||
	      !edit_tool_model_signature.toString().contains(QStringLiteral("export:")) ||
	      !edit_tool_navigation_signature.toString().contains(
	          QStringLiteral("tool-rail-navigation:v2|groups:Adjust>Selection>Paint>Shapes>Text>Crop>View>Export")) ||
	      !edit_tool_navigation_signature.toString().contains(
	          QStringLiteral("|primary:adjust,masks,paint,shapes,text,crop,compare")) ||
	      !edit_tool_navigation_signature.toString().contains(
	          QStringLiteral("|stable-size:true|hover-layout-shift:false|tooltips:true|navigation:direct|variants:page-controls|layers:inspector-tab|info:subtle-inspector-action")) ||
	      !edit_workflow_polish_signature.toString().contains(
	          QStringLiteral("edit-workflow-polish:v1|asset:true|tool:adjust|family:Adjust")) ||
      !edit_workflow_polish_signature.toString().contains(QStringLiteral("|variant:adjust|variant-label:Light|inspector:adjust")) ||
	      !edit_workflow_polish_signature.toString().contains(QStringLiteral("|context-panel:true|inspector-panel:true|filmstrip:true")) ||
	      !edit_workflow_polish_signature.toString().contains(QStringLiteral("|cleanup:tool-switch-cleanup:v1|serial:")) ||
	      !edit_workflow_polish_signature.toString().contains(QStringLiteral("|motion:studio-motion:v1|fast:120|medium:180")) ||
	      !studio_motion_signature.toString().contains(QStringLiteral("studio-motion:v1|fast:120|medium:180")) ||
	      !studio_motion_signature.toString().contains(QStringLiteral("|tool-hud:fade|inspector:fade|rows:reveal|active-target:fade")) ||
	      !studio_motion_signature.toString().contains(QStringLiteral("|hover:no-layout-shift|interruptible:true|reduced-motion:false")) ||
	      !studio_motion_signature.toString().contains(QStringLiteral("|rows:LayerRowChrome")) ||
	      !edit_workflow_polish_signature.toString().contains(
	          QStringLiteral("|target:base-image|preserve:tool-rail-image-stage-toolbar-inspector-filmstrip-bottom-actions"))) {
    std::cerr << "Edit workflow polish: "
              << edit_workflow_polish_signature.toString().toStdString() << '\n';
    return fail("Studio edit rail should render from the shared tool model and expose live page-completion workflow state");
  }
  const auto edit_tool_lifecycle_text = edit_tool_lifecycle_signature.toString();
		  if (!studio_workspace_source.contains(QStringLiteral("function editToolLifecycle(toolName)")) ||
		      !studio_workspace_source.contains(QStringLiteral("function editToolLifecycleContract(toolName)")) ||
		      !studio_workspace_source.contains(QStringLiteral("function editToolRailNavigationSignature()")) ||
		      !studio_workspace_source.contains(QStringLiteral("function studioMotionSignature()")) ||
		      !studio_workspace_source.contains(QStringLiteral("studioMotionContract: \"studio-motion:v1|fast:\"")) ||
		      !studio_workspace_source.contains(QStringLiteral("readonly property bool studioReducedMotion: studioReducedMotionOverride || studioPlatformReducedMotion")) ||
		      !studio_workspace_source.contains(QStringLiteral("function platformReducedMotionPreference()")) ||
		      !studio_workspace_source.contains(QStringLiteral("editToolTransitionFinishTimer")) ||
	      !tool_rail_source.contains(QStringLiteral("groupedNavigationContract: \"tool-rail-navigation:v2|groups:\"")) ||
	      !tool_rail_source.contains(QStringLiteral("activationCoverageContract: \"tool-rail-activation:v1|primary-tools:\"")) ||
	      !tool_rail_source.contains(QStringLiteral("directNavigationContract: \"tool-rail-direct-navigation:v1|enabled:\"")) ||
	      !tool_rail_source.contains(QStringLiteral("function triggerToolById(requestedToolId)")) ||
	      !tool_rail_source.contains(QStringLiteral("root.toolTriggered(tool)")) ||
	      !tool_rail_source.contains(QStringLiteral("objectName: String(toolDelegate.tool.railObjectName || toolDelegate.tool.objectName || \"\")")) ||
	      !studio_workspace_source.contains(QStringLiteral("function beginEditToolSwitch(nextToolName)")) ||
	      !studio_workspace_source.contains(QStringLiteral("layerMaskActivationLatencyContract: \"layer-mask-activation-latency:v2|panel-first:true|outline-trace:next-event-loop|delay-ms:0|no-click-frame-outline-getter:true\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("id: maskOutlineActivationTimer\n        interval: 0")) ||
	      !studio_workspace_source.contains(QStringLiteral("function deferMaskOutlineActivation()")) ||
	      !studio_workspace_source.contains(QStringLiteral("root.deferMaskOutlineActivation()")) ||
	      !studio_workspace_source.contains(QStringLiteral("return (toolName === \"adjust\" || toolName === \"layers\") &&")) ||
	      !studio_workspace_source.contains(QStringLiteral("root.selectedEditTargetMaskSetId().length > 0")) ||
	      !studio_workspace_source.contains(QStringLiteral("masked: editorPreview.hasActiveMask")) ||
	      !studio_workspace_source.contains(QStringLiteral("|target-preservation:appropriate|selected-target:")) ||
      !studio_workspace_source.contains(QStringLiteral("|mask-tools-keep-active-selection:\" + String(root.maskToolKeepsActiveMask(root.editTool))")) ||
      !studio_workspace_source.contains(QStringLiteral("|non-mask-tools-clear-transient-mask-only:true|inspector-transient:reset")) ||
      !studio_workspace_source.contains(QStringLiteral("\"|inspector-tab:\" + root.editInspectorTab")) ||
      !studio_workspace_source.contains(QStringLiteral("function rememberEditToolVariant(toolName, variant)")) ||
      !studio_workspace_source.contains(QStringLiteral("if (toolName === \"retouch\")")) ||
      !studio_workspace_source.contains(QStringLiteral("root.startRetouchTool(root.editToolLastUsedVariant(\"retouch\", \"remove\"), true)")) ||
      !studio_workspace_source.contains(QStringLiteral("function startRetouchTool(modeName, switchAlreadyPrepared)")) ||
	      !studio_workspace_source.contains(QStringLiteral("if (switchAlreadyPrepared !== true)")) ||
	      !studio_workspace_source.contains(QStringLiteral("objectName: \"cropHudCropButton\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("objectName: \"cropHudHorizonButton\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("objectName: \"cropHudTransformButton\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("function cropInstructionText()")) ||
	      !studio_workspace_source.contains(QStringLiteral("function swapCropAspectOrientation()")) ||
	      !studio_workspace_source.contains(QStringLiteral("function retouchModeVisibilityState(modeName)")) ||
	      !studio_workspace_source.contains(QStringLiteral("function retouchHudModeMenuItems()")) ||
      !studio_workspace_source.contains(QStringLiteral("ToolCommandBar {")) ||
      !studio_workspace_source.contains(QStringLiteral("modeButtonObjectName: \"retouchHudModeMenuButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("modeMenuItems: root.retouchHudModeMenuItems()")) ||
      !studio_workspace_source.contains(QStringLiteral("inlineActions: root.retouchHudInlineActions()")) ||
      !studio_workspace_source.contains(QStringLiteral("moreMenuItems: root.retouchHudMoreMenuItems()")) ||
      !studio_workspace_source.contains(QStringLiteral("disabledReason: \"Experimental skin cleanup is hidden until the workflow is production-ready.\"")) ||
      !studio_workspace_source.contains(QStringLiteral("disabledReason: \"Experimental frequency smoothing is hidden until the workflow is production-ready.\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function resetActiveBrushDefaults()")) ||
      !studio_workspace_source.contains(QStringLiteral("function brushPresetMenuItems()")) ||
      !studio_workspace_source.contains(QStringLiteral("function brushCustomizationMenuItems()")) ||
      !studio_workspace_source.contains(QStringLiteral("function brushHudModeMenuItems()")) ||
      !studio_workspace_source.contains(QStringLiteral("function brushQuickTuningMenuItems()")) ||
      !studio_workspace_source.contains(QStringLiteral("function handleBrushQuickTuningMenuAction(key)")) ||
      !studio_workspace_source.contains(QStringLiteral("function brushPageVisibilitySignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("|context-panel:hidden-by-default")) ||
      !studio_workspace_source.contains(QStringLiteral("|layer-actions:primary-target-plus-more-during-brush")) ||
      !studio_workspace_source.contains(QStringLiteral("|layer-rows:compact-brush-mode")) ||
      !studio_workspace_source.contains(QStringLiteral("|visibility-states:menu-during-brush")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"layerVisibilityStateMenuButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("function brushDefaultSignature()")) ||
      !studio_workspace_source.contains(QStringLiteral("function maskSetRowMenuItems(")) ||
      !studio_workspace_source.contains(QStringLiteral("function handleMaskSetRowMenuAction(")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerMaskSetRowMoreMenuButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"photoLayerNestedMaskRowMoreMenuButton\"")) ||
      !brush_layer_inspector_header_source.contains(QStringLiteral("menuItems: root.addMenuItems")) ||
      !studio_workspace_source.contains(QStringLiteral("addMenuItems: root.layerAddMenuItems()")) ||
      !studio_workspace_source.contains(QStringLiteral("onAddLayerMenuItemTriggered: function(key) { root.handleLayerAddMenuAction(key) }")) ||
      !studio_workspace_source.contains(QStringLiteral("brush-preset:")) ||
      !studio_workspace_source.contains(QStringLiteral("brush-size-source-fixed")) ||
      !studio_workspace_source.contains(QStringLiteral("brush-size-source-pressure")) ||
      !studio_workspace_source.contains(QStringLiteral("brush-pressure-off")) ||
      !studio_workspace_source.contains(QStringLiteral("brush-pressure-size-flow")) ||
      !studio_workspace_source.contains(QStringLiteral("brush-pressure-full")) ||
      !studio_workspace_source.contains(QStringLiteral("brush-reset-defaults")) ||
      !studio_workspace_source.contains(QStringLiteral("BrushToolHud {")) ||
      !studio_workspace_source.contains(QStringLiteral("modeMenuItems: root.brushHudModeMenuItems()")) ||
      !studio_workspace_source.contains(QStringLiteral("tuningMenuItems: root.brushQuickTuningMenuItems()")) ||
      !studio_workspace_source.contains(QStringLiteral("onModeMenuItemTriggered: function(key) { root.handleEditRailMenuAction(\"brush\", key) }")) ||
      !studio_workspace_source.contains(QStringLiteral("onTuningMenuItemTriggered: function(key) { root.handleBrushQuickTuningMenuAction(key) }")) ||
      !brush_tool_hud_source.contains(QStringLiteral("specialized-tool-hud:v4|tool:brush|component:BrushToolHud|mode-switch:menu|quick-tuning:size-flow-hardness")) ||
      !brush_tool_hud_source.contains(QStringLiteral("brush-tool-hud-component:v2|owns:mode-size-quick-tuning-done|hidden-precision:diagnostic-only|signals:mode-tuning-size-done|object-names:preserved|workspace-state:external")) ||
      !brush_tool_hud_source.contains(QStringLiteral("tooltipText: \"Adjust brush size, flow, or hardness.\"")) ||
      !studio_workspace_source.contains(QStringLiteral("brush-inspector-controls:v1|visible-during-brush:true|owns:size-flow-hardness|parent-controls:density-opacity|more:presets-pressure-reset|hud:quick-only")) ||
      !brush_tool_hud_source.contains(QStringLiteral("objectName: \"brushHudModeMenuButton\"")) ||
      !brush_tool_hud_source.contains(QStringLiteral("objectName: \"brushHudTuningMenuButton\"")) ||
      !brush_tool_hud_source.contains(QStringLiteral("menuItems: root.modeMenuItems")) ||
      !brush_tool_hud_source.contains(QStringLiteral("menuItems: root.tuningMenuItems")) ||
      !brush_tool_hud_source.contains(QStringLiteral("objectName: \"brushHudResetDefaultsButton\"")) ||
      !studio_workspace_source.contains(QStringLiteral("brush-defaults:v1|size:6|flow:100|hardness:45|density:100|opacity:100|spacing:75|smoothing:18|pressure:0/0/0|reset:true|operation-preserved:true")) ||
	      !studio_workspace_source.contains(QStringLiteral("function activeEditToolDiscoverySignature()")) ||
	      !studio_workspace_source.contains(QStringLiteral("railObjectName: \"editRailAdjustButton\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("railObjectName: \"editRailSelectionButton\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("railObjectName: \"editRailCropButton\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("railObjectName: \"editRailCompareButton\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("menusEnabled: false")) ||
	      !studio_workspace_source.contains(QStringLiteral("objectName: \"layersInspectorTabButton\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("objectName: \"infoInspectorTabButton\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("switchSerial: root.toolSwitchSerial")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"editInspectorActiveToolHeader\"")) ||
      !studio_workspace_source.contains(QStringLiteral("objectName: \"activeToolVariantChip\"")) ||
      !studio_workspace_source.contains(QStringLiteral("lifecycle: lifecycle")) ||
      !studio_workspace_source.contains(QStringLiteral("lifecycleContract: root.editToolLifecycleContract(tool.id)")) ||
	      !edit_tool_lifecycle_text.contains(QStringLiteral("adjust:tool-lifecycle:v1|entry:rail-click-page-shortcut")) ||
	      !edit_tool_lifecycle_text.contains(QStringLiteral("crop:tool-lifecycle:v1|entry:rail-click-page-shortcut")) ||
	      !edit_tool_lifecycle_text.contains(QStringLiteral("masks:tool-lifecycle:v1|entry:rail-click-page-shortcut")) ||
	      !edit_tool_lifecycle_text.contains(QStringLiteral("compare:tool-lifecycle:v1|entry:rail-click-page-shortcut")) ||
	      !edit_tool_lifecycle_text.contains(QStringLiteral("export:tool-lifecycle:v1|entry:rail-click-page-shortcut")) ||
      !studio_workspace_source.contains(QStringLiteral("function retouchSelectionMenuItems()")) ||
      !studio_workspace_source.contains(QStringLiteral("return root.maskSelectionMenuItems().concat(root.maskBrushShapeMenuItems()).concat(root.maskRangeMenuItems()).concat(root.retouchSelectionMenuItems())")) ||
      !edit_tool_lifecycle_text.contains(QStringLiteral("|primary:create-or-refine-selection|")) ||
      !edit_tool_lifecycle_text.contains(QStringLiteral("|commit:done-exits-selection-to-layers|")) ||
      !edit_tool_lifecycle_text.contains(QStringLiteral("|cancel:cancel-exits-transient-selection|"))) {
    return fail("Studio edit tools should declare entry, active, primary, feedback, commit, cancel, disabled, and error lifecycle behavior");
  }
  const auto edit_tool_rail_expansion_contract =
      studio_edit_tool_rail->property("expansionContract").toString();
  const auto edit_tool_rail_lifecycle_contract =
      studio_edit_tool_rail->property("lifecycleContract").toString();
	  const auto edit_tool_rail_discovery_contract =
	      studio_edit_tool_rail->property("toolDiscoveryContract").toString();
	  const auto edit_tool_rail_grouped_navigation_contract =
	      studio_edit_tool_rail->property("groupedNavigationContract").toString();
	  const auto edit_tool_rail_activation_coverage_contract =
	      studio_edit_tool_rail->property("activationCoverageContract").toString();
	  const auto edit_tool_rail_stale_menu_cleanup_contract =
	      studio_edit_tool_rail->property("staleMenuCleanupContract").toString();
	  const auto edit_tool_rail_direct_navigation_contract =
	      studio_edit_tool_rail->property("directNavigationContract").toString();
	  auto* edit_rail_adjust_button = find_named_object(edit_workspace, QStringLiteral("editRailAdjustButton"));
	  auto* edit_rail_selection_button = find_named_object(edit_workspace, QStringLiteral("editRailSelectionButton"));
	  auto* edit_rail_text_button = find_named_object(edit_workspace, QStringLiteral("editRailTextButton"));
	  auto* edit_rail_crop_button = find_named_object(edit_workspace, QStringLiteral("editRailCropButton"));
	  auto* edit_rail_layers_button = find_named_object(edit_workspace, QStringLiteral("editRailLayersButton"));
	  auto* edit_rail_compare_button = find_named_object(edit_workspace, QStringLiteral("editRailCompareButton"));
	  auto* edit_rail_info_button = find_named_object(edit_workspace, QStringLiteral("editRailInfoButton"));
  auto* edit_inspector_active_tool_header =
      find_named_object(edit_workspace, QStringLiteral("editInspectorActiveToolHeader"));
  auto* active_tool_variant_chip =
      find_named_object(edit_workspace, QStringLiteral("activeToolVariantChip"));
  if (!edit_tool_rail_expansion_contract.contains(QStringLiteral("tool-rail:v1")) ||
	      !edit_tool_rail_expansion_contract.contains(QStringLiteral("lifecycle:8")) ||
	      !edit_tool_rail_lifecycle_contract.contains(QStringLiteral("tool-rail-lifecycle:v1|declared:8|tools:8")) ||
	      edit_tool_rail_discovery_contract !=
	          QStringLiteral("tool-discovery:v1|families-visible:true|active-family:true|active-variant:true|variants-one-expansion:true|stale-menus-close:true|disabled-reasons:true") ||
	      !edit_tool_rail_grouped_navigation_contract.contains(
	          QStringLiteral("tool-rail-navigation:v2|groups:Adjust>Selection>Paint>Shapes>Text>Crop>Export")) ||
	      !edit_tool_rail_grouped_navigation_contract.contains(QStringLiteral("|stable-size:true|hover-layout-shift:false|tooltips:explicit|variants:page-controls|one-expansion:false")) ||
	      !edit_tool_rail_activation_coverage_contract.contains(
	          QStringLiteral("tool-rail-activation:v1|primary-tools:adjust,masks,paint,shapes,text,crop")) ||
	      !edit_tool_rail_activation_coverage_contract.contains(
	          QStringLiteral("|source:rail-model|buttons:present|trigger-by-id:true")) ||
	      edit_tool_rail_stale_menu_cleanup_contract !=
	          QStringLiteral("tool-rail-stale-menu-cleanup:v1|one-expansion:true|on-tool-click:true|on-menu-open:true|on-menu-trigger:true|on-tools-changed:true|on-switch-serial:true") ||
      !edit_tool_rail_lifecycle_contract.contains(QStringLiteral("status-tooltips:true")) ||
      !edit_tool_rail_expansion_contract.contains(QStringLiteral("keyboard-menu:true")) ||
      !edit_tool_rail_expansion_contract.contains(QStringLiteral("disabled-reasons:true")) ||
      !edit_tool_rail_expansion_contract.contains(QStringLiteral("no-elide:true")) ||
	      studio_edit_tool_rail->property("activeVariantVisible").toBool() ||
	      studio_edit_tool_rail->property("menuToolCount").toInt() != 0 ||
	      edit_tool_rail_direct_navigation_contract !=
	          QStringLiteral("tool-rail-direct-navigation:v1|enabled:true|click:page|menus:false|hover-overlays:false|variants:page") ||
      !edit_tool_discovery_signature.toString().contains(QStringLiteral("active-tool-discovery:v1|family:")) ||
      !edit_tool_discovery_signature.toString().contains(QStringLiteral("|variant:")) ||
	      !edit_tool_discovery_signature.toString().contains(QStringLiteral("|rail:direct-navigation|variants:on-page|stale-clear:true")) ||
      !tool_switch_cleanup_signature.toString().contains(QStringLiteral("tool-switch-cleanup:v1|serial:")) ||
      !tool_switch_cleanup_signature.toString().contains(QStringLiteral("|menus:closed-by-tool-rail-switch-serial|committed-edits:preserved")) ||
	      edit_inspector_active_tool_header == nullptr ||
	      active_tool_variant_chip == nullptr ||
	      edit_rail_adjust_button == nullptr ||
	      edit_rail_selection_button == nullptr ||
	      edit_rail_crop_button == nullptr ||
          edit_rail_text_button == nullptr ||
	      edit_rail_layers_button != nullptr ||
	      edit_rail_compare_button == nullptr ||
	      edit_rail_info_button != nullptr ||
	      !edit_inspector_active_tool_header->property("toolDiscoveryContract").toString().contains(
	          QStringLiteral("active-tool-discovery:v1|family:"))) {
	    std::cerr << "Edit rail expansion: " << edit_tool_rail_expansion_contract.toStdString()
	              << " lifecycle: " << edit_tool_rail_lifecycle_contract.toStdString()
	              << " discovery: " << edit_tool_rail_discovery_contract.toStdString()
	              << " navigation: " << edit_tool_rail_grouped_navigation_contract.toStdString()
	              << " activation: " << edit_tool_rail_activation_coverage_contract.toStdString()
	              << " direct: " << edit_tool_rail_direct_navigation_contract.toStdString()
	              << " activeVariantVisible: "
	              << studio_edit_tool_rail->property("activeVariantVisible").toBool()
	              << " menuToolCount: " << studio_edit_tool_rail->property("menuToolCount").toInt()
	              << " active header: "
	              << (edit_inspector_active_tool_header != nullptr)
	              << " adjust: " << (edit_rail_adjust_button != nullptr)
	              << " selection: " << (edit_rail_selection_button != nullptr)
	              << " crop: " << (edit_rail_crop_button != nullptr)
	              << " text: " << (edit_rail_text_button != nullptr)
	              << " layers: " << (edit_rail_layers_button != nullptr)
	              << " compare: " << (edit_rail_compare_button != nullptr)
	              << " info: " << (edit_rail_info_button != nullptr) << '\n';
	    return fail("Studio edit rail should navigate directly without menus or hover overlays, with Layers and Info moved into the inspector");
	  }
	  auto* active_canvas_tool_chip_contract =
	      find_named_object(edit_workspace, QStringLiteral("activeCanvasToolChip"));
  if (active_canvas_tool_chip_contract == nullptr ||
      !active_canvas_tool_chip_contract->property("layoutContract")
          .toString()
          .contains(QStringLiteral("tool-hud")) ||
      !active_canvas_tool_chip_contract->property("toolDiscoveryContract")
          .toString()
          .contains(QStringLiteral("tool-hud-discovery:v1|active-family:"))) {
    return fail("Studio active canvas tool chip should render from the shared ToolHud contract");
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_COMPACT_TEXT_FIT")) {
    QVariant compact_mode_result;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "setInterfaceMode",
            Q_RETURN_ARG(QVariant, compact_mode_result),
            Q_ARG(QVariant, QStringLiteral("compact"))) ||
        !compact_mode_result.toBool() ||
        edit_workspace->property("interfaceMode").toString() != QStringLiteral("compact") ||
        edit_workspace->property("editInspectorPanelWidth").toInt() >= 420) {
      return fail("workspace should expose compact panel density mode");
    }
    root->setProperty("width", 1180);
    root->setProperty("height", 820);
    edit_workspace->setProperty("editContextPanelVisible", true);
    edit_workspace->setProperty("editSidebarCollapsed", false);
    edit_workspace->setProperty("editInspectorPanelVisible", true);
    edit_workspace->setProperty("largeScopesMode", false);
    edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
    edit_workspace->setProperty("diagnosticsPanelOpen", true);
    QMetaObject::invokeMethod(
        edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
    process_layout_events();
    const QStringList compact_named_buttons{
        QStringLiteral("fitToViewButton"),
        QStringLiteral("fillViewButton"),
        QStringLiteral("zoomStepButton"),
        QStringLiteral("zoomOutButton"),
        QStringLiteral("zoomInButton"),
        QStringLiteral("diagnosticsOverlayButton"),
        QStringLiteral("referenceImageButton"),
        QStringLiteral("inspectorPanelToggleButton"),
        QStringLiteral("fullScreenEditButton"),
        QStringLiteral("diagnosticsPanelCloseButton"),
        QStringLiteral("loupeOverlayButton"),
        QStringLiteral("focusOverlayButton"),
        QStringLiteral("peakingOverlayButton"),
        QStringLiteral("clippingOverlayButton"),
        QStringLiteral("falseColorOverlayButton"),
        QStringLiteral("gamutOverlayButton"),
        QStringLiteral("zoneMapOverlayButton"),
        QStringLiteral("adjustInspectorTabButton"),
        QStringLiteral("layersInspectorTabButton"),
        QStringLiteral("scopesInspectorTabButton"),
        QStringLiteral("detailInspectorTabButton"),
        QStringLiteral("infoInspectorTabButton"),
        QStringLiteral("editInspectorResetButton"),
        QStringLiteral("editInspectorSavePresetButton"),
    };
    const QStringList compact_named_labels{
        QStringLiteral("editAdjustmentFilterStatusLabel"),
        QStringLiteral("activeMaskToolbarStatusLabel"),
        QStringLiteral("maskStackSetLabel"),
        QStringLiteral("maskStackSetSummaryLabel"),
        QStringLiteral("cropToolHeaderLabel"),
        QStringLiteral("cropToolAspectLabel"),
        QStringLiteral("brushHudStatusLabel"),
        QStringLiteral("retouchHudStatusLabel"),
        QStringLiteral("largeScopeProofingStatusLabel"),
        QStringLiteral("pixelInspectorCoordinateLabel"),
        QStringLiteral("pixelInspectorBeforeLabel"),
        QStringLiteral("pixelInspectorAfterLabel"),
        QStringLiteral("pixelInspectorDeltaLabel"),
    };
    QString failing_compact_button;
    if (!visible_named_buttons_fit_content(edit_workspace, compact_named_buttons) ||
        !visible_mini_buttons_fit_content(edit_workspace, &failing_compact_button) ||
        !visible_named_labels_fit_content(edit_workspace, compact_named_labels)) {
      std::cerr << "Compact fit failure: "
                << failing_compact_button.toStdString() << '\n';
      return fail("compact mode should keep visible labels and button content inside controls");
    }
    trace_smoke_progress("focused compact text fit checked");
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    editor_preview.setImageProvider(nullptr);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return EXIT_SUCCESS;
  }
  const std::vector<std::pair<QString, QObject*>> primary_rail_tools{
      {QStringLiteral("adjust"), edit_rail_adjust_button},
      {QStringLiteral("masks"), edit_rail_selection_button},
      {QStringLiteral("crop"), edit_rail_crop_button},
      {QStringLiteral("layers"), edit_rail_layers_button},
      {QStringLiteral("compare"), edit_rail_compare_button},
      {QStringLiteral("info"), edit_rail_info_button},
  };
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_RENDER_SAFE_MOTION")) {
    QVariant idle_motion_duration;
    QVariant render_motion_duration;
    QVariant reduced_motion_duration;
    QVariant motion_signature;
    const bool invoked_idle =
        QMetaObject::invokeMethod(
            edit_workspace,
            "studioMotionDurationForState",
            Q_RETURN_ARG(QVariant, idle_motion_duration),
            Q_ARG(QVariant, QVariant(false)),
            Q_ARG(QVariant, QVariant(false)),
            Q_ARG(QVariant, QVariant(120)));
    const bool invoked_render =
        QMetaObject::invokeMethod(
            edit_workspace,
            "studioMotionDurationForState",
            Q_RETURN_ARG(QVariant, render_motion_duration),
            Q_ARG(QVariant, QVariant(false)),
            Q_ARG(QVariant, QVariant(true)),
            Q_ARG(QVariant, QVariant(120)));
    const bool invoked_reduced =
        QMetaObject::invokeMethod(
            edit_workspace,
            "studioMotionDurationForState",
            Q_RETURN_ARG(QVariant, reduced_motion_duration),
            Q_ARG(QVariant, QVariant(true)),
            Q_ARG(QVariant, QVariant(false)),
            Q_ARG(QVariant, QVariant(120)));
    QMetaObject::invokeMethod(
        edit_workspace,
        "studioMotionSignature",
        Q_RETURN_ARG(QVariant, motion_signature));
    if (!invoked_idle ||
        !invoked_render ||
        !invoked_reduced ||
        idle_motion_duration.toInt() != 120 ||
        render_motion_duration.toInt() != 0 ||
        reduced_motion_duration.toInt() != 0 ||
        !motion_signature.toString().contains(QStringLiteral("|render-work:false|render-suppressed:false"))) {
      std::cerr << "Render-safe motion: invoked idle/render/reduced="
                << invoked_idle << "/" << invoked_render << "/" << invoked_reduced
                << " durations=" << idle_motion_duration.toInt() << "/"
                << render_motion_duration.toInt() << "/"
                << reduced_motion_duration.toInt()
                << " signature=" << motion_signature.toString().toStdString() << '\n';
      return fail("Studio motion should collapse nonessential animations while preview render work is active");
    }
    trace_smoke_progress("render-safe motion checked");
    return EXIT_SUCCESS;
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_FIRST_INTERACTION_RESPONSIVENESS")) {
    const auto first_interaction_contract =
        edit_workspace->property("firstInteractionResponsivenessContract").toString();
    const auto preview_feedback_contract =
        edit_workspace->property("previewFeedbackContract").toString();
    if (first_interaction_contract !=
            QStringLiteral("first-interaction-responsiveness:v1|surface:studio-tool-rail|trigger:sync|first-feedback:transition-active|state:tool-switch-serial|gate:smoke-perf") ||
        preview_feedback_contract !=
            QStringLiteral("preview-feedback:v1|first-frame:controls-crop-brush-mask-layer-compare-tool-switch|states:draft-refining-full-resolution|last-good:preserved|stale:rejected|context:zoom-pan-overlay-tool-preserved|parity:preview-export")) {
      return fail("Studio first interaction responsiveness contract should be exposed by the edit workspace");
    }
    QStringList checked_responsive_tools;
    const std::vector<std::pair<QString, QObject*>> first_interaction_tools{
        {QStringLiteral("adjust"), edit_rail_adjust_button},
        {QStringLiteral("masks"), edit_rail_selection_button},
        {QStringLiteral("crop"), edit_rail_crop_button},
        {QStringLiteral("compare"), edit_rail_compare_button},
    };
    for (const auto& [tool_id, button] : first_interaction_tools) {
      if (button == nullptr) {
        return fail("Studio first interaction responsiveness gate should have concrete primary rail buttons");
      }
      const int serial_before = edit_workspace->property("toolSwitchSerial").toInt();
      const auto tool_before = edit_workspace->property("editTool").toString();
      QVariant trigger_result;
      QElapsedTimer first_interaction_timer;
      first_interaction_timer.start();
      const bool triggered =
          QMetaObject::invokeMethod(
              studio_edit_tool_rail,
              "triggerToolById",
              Q_RETURN_ARG(QVariant, trigger_result),
              Q_ARG(QVariant, QVariant(tool_id))) &&
          trigger_result.toBool();
      const qint64 first_interaction_elapsed_ms = first_interaction_timer.elapsed();
      QVariant active_result;
      const bool active_immediately =
          QMetaObject::invokeMethod(
              edit_workspace,
              "editRailToolActive",
              Q_RETURN_ARG(QVariant, active_result),
              Q_ARG(QVariant, QVariant(tool_id))) &&
          active_result.toBool();
      const int serial_after = edit_workspace->property("toolSwitchSerial").toInt();
      const auto tool_after = edit_workspace->property("editTool").toString();
      const bool transition_active = edit_workspace->property("editToolTransitionActive").toBool();
      const auto pending_transition =
          edit_workspace->property("pendingEditToolTransitionName").toString();
      const auto transition_title = edit_workspace->property("editToolTransitionTitle").toString();
      if (!triggered ||
          first_interaction_elapsed_ms > 100 ||
          serial_after != serial_before + 1 ||
          !active_immediately ||
          !transition_active ||
          pending_transition != tool_id ||
          transition_title.isEmpty()) {
        std::cerr << "First interaction responsiveness for " << tool_id.toStdString()
                  << ": triggered=" << triggered
                  << " elapsedMs=" << first_interaction_elapsed_ms
                  << " serial=" << serial_before << "->" << serial_after
                  << " tool=" << tool_before.toStdString() << "->" << tool_after.toStdString()
                  << " active=" << active_immediately
                  << " transitionActive=" << transition_active
                  << " pending=" << pending_transition.toStdString()
                  << " title=" << transition_title.toStdString() << '\n';
        return fail("Studio first rail interactions should synchronously show responsive tool-switch feedback");
      }
      checked_responsive_tools.push_back(tool_id);
    }
    if (checked_responsive_tools.join(QStringLiteral(",")) !=
        QStringLiteral("adjust,masks,crop,compare")) {
      return fail("Studio first interaction responsiveness gate should cover every primary rail tool exactly once");
    }
    trace_smoke_progress("first interaction responsiveness checked");
    return EXIT_SUCCESS;
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_RAIL_ACTIVATION")) {
    QStringList activated_tools;
    for (const auto& [tool_id, button] : primary_rail_tools) {
      auto* item = qobject_cast<QQuickItem*>(button);
      QVariant trigger_result;
      if (button == nullptr || item == nullptr ||
          !QMetaObject::invokeMethod(
              studio_edit_tool_rail,
              "triggerToolById",
              Q_RETURN_ARG(QVariant, trigger_result),
              Q_ARG(QVariant, QVariant(tool_id))) ||
          !trigger_result.toBool()) {
        std::cerr << "Rail activation failed before trigger for " << tool_id.toStdString()
                  << " button=" << (button == nullptr ? "null" : "present")
                  << " visible=" << (item != nullptr && item_visible(item))
                  << " itemVisible=" << (item != nullptr && item->isVisible())
                  << " triggered=" << trigger_result.toBool()
                  << " size=" << (item == nullptr ? 0.0 : item->width()) << "x"
                  << (item == nullptr ? 0.0 : item->height()) << '\n';
        return fail("primary Studio rail tools should expose rail-owned activation triggers and concrete buttons");
      }
      const bool became_active = wait_for_condition([&]() {
        QVariant active_result;
        return QMetaObject::invokeMethod(
                   edit_workspace,
                   "editRailToolActive",
                   Q_RETURN_ARG(QVariant, active_result),
                   Q_ARG(QVariant, QVariant(tool_id))) &&
               active_result.toBool();
      }, 1500);
      QVariant discovery_after_click;
      QVariant cleanup_after_click;
      QMetaObject::invokeMethod(
          edit_workspace,
          "activeEditToolDiscoverySignature",
          Q_RETURN_ARG(QVariant, discovery_after_click));
      QMetaObject::invokeMethod(
          edit_workspace,
          "toolSwitchCleanupSignature",
          Q_RETURN_ARG(QVariant, cleanup_after_click));
      if (!became_active ||
          !discovery_after_click.toString().contains(QStringLiteral("active-tool-discovery:v1")) ||
          !cleanup_after_click.toString().contains(QStringLiteral("tool-switch-cleanup:v1|serial:")) ||
          edit_workspace->property("diagnosticsPanelOpen").toBool()) {
        std::cerr << "Rail activation state for " << tool_id.toStdString()
                  << ": active=" << became_active
                  << " editTool=" << edit_workspace->property("editTool").toString().toStdString()
                  << " inspector=" << edit_workspace->property("editInspectorTab").toString().toStdString()
                  << " discovery=" << discovery_after_click.toString().toStdString()
                  << " cleanup=" << cleanup_after_click.toString().toStdString()
                  << " diagnosticsOpen=" << edit_workspace->property("diagnosticsPanelOpen").toBool()
                  << '\n';
        return fail("primary Studio rail tool triggers should activate the matching visible tool state");
      }
      activated_tools.push_back(tool_id);
    }
    const auto activated_text = activated_tools.join(QStringLiteral(","));
    if (activated_text != QStringLiteral("adjust,masks,brush,retouch,crop,layers,compare,info")) {
      return fail("primary Studio rail activation smoke should cover every primary rail tool exactly once");
    }
    trace_smoke_progress("focused rail activation checked");
    return EXIT_SUCCESS;
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_TOOL_SWITCH_CLEANUP")) {
    auto trigger_rail_tool = [&](const QString& tool_id) {
      QVariant trigger_result;
      return QMetaObject::invokeMethod(
                 studio_edit_tool_rail,
                 "triggerToolById",
                 Q_RETURN_ARG(QVariant, trigger_result),
                 Q_ARG(QVariant, QVariant(tool_id))) &&
             trigger_result.toBool() &&
             wait_for_condition([&]() {
               QVariant active_result;
               return QMetaObject::invokeMethod(
                          edit_workspace,
                          "editRailToolActive",
                          Q_RETURN_ARG(QVariant, active_result),
                          Q_ARG(QVariant, QVariant(tool_id))) &&
                      active_result.toBool();
             }, 1500);
    };
    if (!trigger_rail_tool(QStringLiteral("brush"))) {
      return fail("switch cleanup proof should be able to enter Brush from the rail");
    }
    const auto mask_count_before_switch = editor_preview.maskCount();
    editor_preview.beginActiveBrushStroke();
    editor_preview.addBrushPointWithTablet(0.38, 0.44, 0.72, 0.0, 0.0);
    edit_workspace->setProperty("diagnosticsPanelOpen", true);
    edit_workspace->setProperty("maskToolRailExpanded", true);
    QVariant transient_tool_state_signature;
    QVariant cleanup_after_switch;
    auto transient_state_matches = [&]() {
      transient_tool_state_signature.clear();
      return QMetaObject::invokeMethod(
                 edit_workspace,
                 "transientToolStateSignature",
                 Q_RETURN_ARG(QVariant, transient_tool_state_signature)) &&
             transient_tool_state_signature.toString() ==
                 QStringLiteral("tool:adjust|activeMask:false|stroke:false|crop:false|compare:false");
    };
    if (!editor_preview.brushStrokeActive() ||
        !trigger_rail_tool(QStringLiteral("adjust")) ||
        !wait_for_condition(transient_state_matches, 1500) ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "toolSwitchCleanupSignature",
            Q_RETURN_ARG(QVariant, cleanup_after_switch)) ||
        !cleanup_after_switch.toString().contains(QStringLiteral("|diagnostics:false|maskRail:false|")) ||
        !cleanup_after_switch.toString().contains(QStringLiteral("|inspector-transient:reset|inspector-tab:adjust|")) ||
        editor_preview.maskCount() != mask_count_before_switch) {
      std::cerr << "Brush switch cleanup state: transient="
                << transient_tool_state_signature.toString().toStdString()
                << " cleanup=" << cleanup_after_switch.toString().toStdString()
                << " maskCountBefore=" << mask_count_before_switch
                << " maskCountAfter=" << editor_preview.maskCount() << '\n';
      return fail("switching tools from Brush should finish active strokes and clear hidden transient state");
    }
    if (!trigger_rail_tool(QStringLiteral("compare")) ||
        !editor_preview.compareMode() ||
        !trigger_rail_tool(QStringLiteral("crop")) ||
        editor_preview.compareMode() ||
        !editor_preview.cropEditing() ||
        !trigger_rail_tool(QStringLiteral("layers")) ||
        editor_preview.compareMode() ||
        editor_preview.cropEditing()) {
      std::cerr << "View switch cleanup state: tool="
                << edit_workspace->property("editTool").toString().toStdString()
                << " compare=" << editor_preview.compareMode()
                << " crop=" << editor_preview.cropEditing() << '\n';
      return fail("switching tools should not strand compare or crop transient states");
    }
    trace_smoke_progress("focused tool switch cleanup checked");
    return EXIT_SUCCESS;
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_COMMAND_BAR_ADAPTIVE")) {
    QVariant focused_retouch_mode;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "startRetouchTool",
            Q_RETURN_ARG(QVariant, focused_retouch_mode),
            Q_ARG(QVariant, QStringLiteral("remove")),
            Q_ARG(QVariant, false)) ||
        focused_retouch_mode.toString() != QStringLiteral("remove")) {
      return fail("adaptive command bar smoke should start Remove retouch directly");
    }
    const bool retouch_hud_ready = wait_for_condition([&]() {
      auto* hud = find_named_object(edit_workspace, QStringLiteral("retouchToolHud"));
      return hud != nullptr && hud->property("visible").toBool();
    }, 1500);
    auto* adaptive_retouch_tool_hud =
        find_named_object(edit_workspace, QStringLiteral("retouchToolHud"));
    auto* adaptive_retouch_status_label =
        find_named_object(edit_workspace, QStringLiteral("retouchHudStatusLabel"));
    auto* adaptive_retouch_more_menu_button =
        find_named_object(edit_workspace, QStringLiteral("retouchHudMoreMenuButton"));
    auto* adaptive_retouch_add_button =
        find_named_object(edit_workspace, QStringLiteral("retouchHudRemoveAddButton"));
    auto* adaptive_retouch_subtract_button =
        find_named_object(edit_workspace, QStringLiteral("retouchHudRemoveSubtractButton"));
    auto* adaptive_retouch_hud_item = qobject_cast<QQuickItem*>(adaptive_retouch_tool_hud);
    if (!retouch_hud_ready ||
        adaptive_retouch_tool_hud == nullptr ||
        adaptive_retouch_status_label == nullptr ||
        adaptive_retouch_more_menu_button == nullptr ||
        adaptive_retouch_add_button == nullptr ||
        adaptive_retouch_subtract_button == nullptr ||
        adaptive_retouch_hud_item == nullptr) {
      return fail("adaptive command bar smoke should find retouch HUD and inline controls");
    }
    adaptive_retouch_hud_item->setWidth(520);
    QCoreApplication::processEvents();
    const auto adaptive_layout_contract =
        adaptive_retouch_tool_hud->property("adaptiveLayoutContract").toString();
    const auto adaptive_more_menu =
        adaptive_retouch_more_menu_button->property("menuContract").toString();
    if (!adaptive_retouch_tool_hud->property("adaptiveMenuMode").toBool() ||
        adaptive_retouch_tool_hud->property("adaptiveInlineActionCount").toInt() != 2 ||
        adaptive_retouch_tool_hud->property("effectiveMoreMenuItemCount").toInt() != 7 ||
        !adaptive_layout_contract.contains(QStringLiteral("tool-command-bar-adaptive:v1")) ||
        !adaptive_layout_contract.contains(QStringLiteral("|active:true|inline-to-more:2|more-count:7|status-visible:false|single-row:true|height:44")) ||
        adaptive_retouch_status_label->property("visible").toBool() ||
        adaptive_retouch_add_button->property("visible").toBool() ||
        adaptive_retouch_subtract_button->property("visible").toBool() ||
        !adaptive_more_menu.contains(QStringLiteral("items:7")) ||
        !adaptive_more_menu.contains(QStringLiteral("checked:1"))) {
      std::cerr << "Adaptive command bar state: contract="
                << adaptive_layout_contract.toStdString()
                << " more=" << adaptive_more_menu.toStdString()
                << " statusVisible=" << adaptive_retouch_status_label->property("visible").toBool()
                << " addVisible=" << adaptive_retouch_add_button->property("visible").toBool()
                << " subtractVisible=" << adaptive_retouch_subtract_button->property("visible").toBool()
                << '\n';
      return fail("narrow command bars should move inline actions into More and avoid clipped status/actions");
    }
    trace_smoke_progress("focused command bar adaptive checked");
    return EXIT_SUCCESS;
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_COMMAND_BAR_PLACEMENT")) {
    auto check_hud_placement = [&](const QString& object_name, QString* detail) {
      auto* viewport = find_named_item(edit_workspace, QStringLiteral("editImageViewport"));
      auto* hud = find_named_item(edit_workspace, object_name);
      if (!item_visible(viewport) || !item_visible(hud)) {
        if (detail != nullptr) {
          *detail = QStringLiteral("%1 visible=%2 viewportVisible=%3")
                        .arg(object_name)
                        .arg(item_visible(hud))
                        .arg(item_visible(viewport));
        }
        return false;
      }
      const auto contract = hud->property("commandPlacementContract").toString();
      const auto viewport_rect = scene_rect(viewport);
      const auto hud_rect = scene_rect(hud);
      const auto center_delta =
          std::abs(hud_rect.center().x() - viewport_rect.center().x());
      const auto top_delta = std::abs(hud_rect.top() - (viewport_rect.top() + 12.0));
      const bool fits_width =
          hud_rect.left() + 1.0 >= viewport_rect.left() &&
          hud_rect.right() <= viewport_rect.right() + 1.0;
      const bool passed =
          contract == QStringLiteral("canvas-command-placement:v1|slot:top-center|top-margin:12|safe-width:true|consistent:true|image-overlap:minimized") &&
          center_delta <= 2.0 && top_delta <= 2.0 && fits_width;
      if (!passed && detail != nullptr) {
        *detail = QStringLiteral("%1 contract=%2 centerDelta=%3 topDelta=%4 fitsWidth=%5 hud=%6,%7 %8x%9 viewport=%10,%11 %12x%13")
                      .arg(object_name)
                      .arg(contract)
                      .arg(center_delta)
                      .arg(top_delta)
                      .arg(fits_width)
                      .arg(hud_rect.x())
                      .arg(hud_rect.y())
                      .arg(hud_rect.width())
                      .arg(hud_rect.height())
                      .arg(viewport_rect.x())
                      .arg(viewport_rect.y())
                      .arg(viewport_rect.width())
                      .arg(viewport_rect.height());
      }
      return passed;
    };
    QString placement_detail;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "activateEditTool",
            Q_ARG(QVariant, QVariant(QStringLiteral("crop")))) ||
        !wait_for_condition([&]() {
          auto* hud = find_named_item(edit_workspace, QStringLiteral("cropToolHud"));
          return item_visible(hud);
        }, 1500) ||
        !check_hud_placement(QStringLiteral("cropToolHud"), &placement_detail)) {
      std::cerr << "Crop command placement: " << placement_detail.toStdString() << '\n';
      return fail("Crop command HUD should use the shared top-center canvas command slot");
    }
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "activateEditTool",
            Q_ARG(QVariant, QVariant(QStringLiteral("brush")))) ||
        !wait_for_condition([&]() {
          auto* hud = find_named_item(edit_workspace, QStringLiteral("brushToolHud"));
          return item_visible(hud);
        }, 1500) ||
        !check_hud_placement(QStringLiteral("brushToolHud"), &placement_detail)) {
      std::cerr << "Brush command placement: " << placement_detail.toStdString() << '\n';
      return fail("Brush command HUD should use the shared top-center canvas command slot");
    }
    QVariant placement_retouch_mode;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "startRetouchTool",
            Q_RETURN_ARG(QVariant, placement_retouch_mode),
            Q_ARG(QVariant, QStringLiteral("heal")),
            Q_ARG(QVariant, false)) ||
        placement_retouch_mode.toString() != QStringLiteral("heal") ||
        !wait_for_condition([&]() {
          auto* hud = find_named_item(edit_workspace, QStringLiteral("retouchToolHud"));
          return item_visible(hud);
        }, 1500) ||
        !check_hud_placement(QStringLiteral("retouchToolHud"), &placement_detail)) {
      std::cerr << "Retouch command placement: " << placement_detail.toStdString() << '\n';
      return fail("Retouch command HUD should use the shared top-center canvas command slot");
    }
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "createMaskSource",
            Q_ARG(QVariant, QVariant(QStringLiteral("object")))) ||
        !wait_for_condition([&]() {
          auto* hud = find_named_item(edit_workspace, QStringLiteral("activeCanvasToolChip"));
          return item_visible(hud);
        }, 1500) ||
        !check_hud_placement(QStringLiteral("activeCanvasToolChip"), &placement_detail)) {
      std::cerr << "Selection command placement: " << placement_detail.toStdString() << '\n';
      return fail("Selection command HUD should use the shared top-center canvas command slot");
    }
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "startMaskPolygonFromShortcut") ||
        !wait_for_condition([&]() {
          auto* hud = find_named_item(edit_workspace, QStringLiteral("polygonToolHud"));
          return item_visible(hud);
        }, 1500) ||
        !check_hud_placement(QStringLiteral("polygonToolHud"), &placement_detail)) {
      std::cerr << "Polygon command placement: " << placement_detail.toStdString() << '\n';
      return fail("Polygon command HUD should use the shared top-center canvas command slot");
    }
    trace_smoke_progress("focused command bar placement checked");
    return EXIT_SUCCESS;
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_VIEW_TOOL_SEPARATION")) {
    auto* edit_zoom_toolbar = find_named_object(edit_workspace, QStringLiteral("editZoomToolbar"));
    auto* edit_action_toolbar = find_named_object(edit_workspace, QStringLiteral("editActionToolbar"));
    auto* edit_image_viewport = find_named_object(edit_workspace, QStringLiteral("editImageViewport"));
    auto* edit_pan_zoom_input_handler =
        find_named_object(edit_workspace, QStringLiteral("editImageViewportPanZoomInputHandler"));
    if (edit_zoom_toolbar == nullptr || edit_action_toolbar == nullptr ||
        edit_image_viewport == nullptr || edit_pan_zoom_input_handler == nullptr) {
      return fail("view/edit separation proof should find canvas view toolbars");
    }
    const QStringList zoom_toolbar_items{
        QStringLiteral("fitToViewButton"),
        QStringLiteral("fillViewButton"),
        QStringLiteral("zoomStepButton"),
        QStringLiteral("zoomOutButton"),
        QStringLiteral("zoomInButton"),
        QStringLiteral("diagnosticsOverlayButton"),
    };
    const QStringList action_toolbar_items{
        QStringLiteral("referenceImageButton"),
        QStringLiteral("clearReferenceImageButton"),
        QStringLiteral("inspectorPanelToggleButton"),
        QStringLiteral("fullScreenEditButton"),
    };
    const QStringList diagnostics_panel_items{
        QStringLiteral("diagnosticsPanelCloseButton"),
        QStringLiteral("loupeOverlayButton"),
        QStringLiteral("focusOverlayButton"),
        QStringLiteral("peakingOverlayButton"),
        QStringLiteral("clippingOverlayButton"),
        QStringLiteral("falseColorOverlayButton"),
        QStringLiteral("gamutOverlayButton"),
        QStringLiteral("zoneMapOverlayButton"),
    };
    const auto check_stable_canvas_view_chrome = [&](const int width, QString* detail) {
      root->setProperty("width", width);
      root->setProperty("height", 820);
      QMetaObject::invokeMethod(
          edit_workspace,
          "applyResponsivePanelBehavior",
          Q_ARG(QVariant, width));
      QMetaObject::invokeMethod(
          edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
      edit_workspace->setProperty("diagnosticsPanelOpen", false);
      process_layout_events();

      edit_image_viewport = find_named_object(edit_workspace, QStringLiteral("editImageViewport"));
      edit_zoom_toolbar = find_named_object(edit_workspace, QStringLiteral("editZoomToolbar"));
      edit_action_toolbar = find_named_object(edit_workspace, QStringLiteral("editActionToolbar"));
      auto* diagnostics_overlay_button =
          find_named_object(edit_workspace, QStringLiteral("diagnosticsOverlayButton"));
      auto* diagnostics_overlay_panel =
          find_named_object(edit_workspace, QStringLiteral("diagnosticsOverlayPanel"));
      auto* loupe_overlay_button =
          find_named_object(edit_workspace, QStringLiteral("loupeOverlayButton"));
      auto* gamut_overlay_button =
          find_named_object(edit_workspace, QStringLiteral("gamutOverlayButton"));
      auto* viewport_item = qobject_cast<QQuickItem*>(edit_image_viewport);
      auto* zoom_toolbar_item = qobject_cast<QQuickItem*>(edit_zoom_toolbar);
      auto* action_toolbar_item = qobject_cast<QQuickItem*>(edit_action_toolbar);
      if (!item_visible(viewport_item) || !item_visible(zoom_toolbar_item) ||
          !item_visible(action_toolbar_item) || diagnostics_overlay_button == nullptr ||
          diagnostics_overlay_panel == nullptr || loupe_overlay_button == nullptr ||
          gamut_overlay_button == nullptr) {
        if (detail != nullptr)
          *detail = QStringLiteral("width %1 missing visible viewport toolbar chrome").arg(width);
        return false;
      }

      const auto viewport_width = viewport_item->width();
      const auto toolbar_compact_width =
          edit_image_viewport->property("toolbarCompactWidth").toDouble();
      const auto toolbar_compact = viewport_width < toolbar_compact_width;
      const auto zoom_right = edit_zoom_toolbar->property("x").toDouble() +
                              edit_zoom_toolbar->property("width").toDouble();
      const auto action_left = edit_action_toolbar->property("x").toDouble();
      const auto zoom_bottom = edit_zoom_toolbar->property("y").toDouble() +
                               edit_zoom_toolbar->property("height").toDouble();
      const auto action_top = edit_action_toolbar->property("y").toDouble();
      const auto toolbar_stacked = edit_image_viewport->property("toolbarStacked").toBool();
      const auto viewport_rect = scene_rect(viewport_item);
      const auto zoom_toolbar_contract =
          edit_zoom_toolbar->property("toolGroupContract").toString();
      const auto action_toolbar_contract =
          edit_action_toolbar->property("toolGroupContract").toString();
      if (edit_image_viewport->property("toolbarCompact").toBool() != toolbar_compact ||
          !zoom_toolbar_contract.contains(QStringLiteral("canvas-toolbar:v1|group:view-controls")) ||
          !zoom_toolbar_contract.contains(QStringLiteral("stable-height:true")) ||
          !zoom_toolbar_contract.contains(QStringLiteral("zoom-fit-group:true")) ||
          !zoom_toolbar_contract.contains(QStringLiteral("diagnostics-menu:true")) ||
          !zoom_toolbar_contract.contains(QStringLiteral("view-separated:true")) ||
          !zoom_toolbar_contract.contains(QStringLiteral("no-overlap:true")) ||
          !action_toolbar_contract.contains(QStringLiteral("canvas-toolbar:v1|group:canvas-actions")) ||
          !action_toolbar_contract.contains(QStringLiteral("stable-height:true")) ||
          !action_toolbar_contract.contains(QStringLiteral("view-separated:true")) ||
          !action_toolbar_contract.contains(QStringLiteral("no-overlap:true")) ||
          diagnostics_overlay_button->property("labelVisible").toBool() ||
          diagnostics_overlay_panel->property("visible").toBool() ||
          loupe_overlay_button->property("visible").toBool() ||
          gamut_overlay_button->property("visible").toBool() ||
          !contains_with_tolerance(viewport_rect, scene_rect(zoom_toolbar_item)) ||
          !contains_with_tolerance(viewport_rect, scene_rect(action_toolbar_item)) ||
          !visible_named_items_fit_parent(
              edit_workspace,
              QStringLiteral("editZoomToolbar"),
              zoom_toolbar_items) ||
          !visible_named_items_fit_parent(
              edit_workspace,
              QStringLiteral("editActionToolbar"),
              action_toolbar_items) ||
          !visible_named_items_do_not_overlap(edit_workspace, zoom_toolbar_items) ||
          !visible_named_items_do_not_overlap(edit_workspace, action_toolbar_items) ||
          (!toolbar_stacked && zoom_right + 8.0 > action_left) ||
          (toolbar_stacked && zoom_bottom + 6.0 > action_top)) {
        if (detail != nullptr)
          *detail = QStringLiteral("width %1 unstable toolbar geometry or contracts").arg(width);
        return false;
      }

      edit_workspace->setProperty("diagnosticsPanelOpen", true);
      process_layout_events();
      diagnostics_overlay_panel =
          find_named_object(edit_workspace, QStringLiteral("diagnosticsOverlayPanel"));
      auto* diagnostics_overlay_panel_item = qobject_cast<QQuickItem*>(diagnostics_overlay_panel);
      const auto diagnostics_panel_visible = item_visible(diagnostics_overlay_panel_item);
      const auto diagnostics_panel_inside =
          diagnostics_panel_visible &&
          contains_with_tolerance(viewport_rect, scene_rect(diagnostics_overlay_panel_item));
      const auto diagnostics_items_fit = visible_named_items_fit_parent(
          edit_workspace,
          QStringLiteral("diagnosticsOverlayPanel"),
          diagnostics_panel_items);
      const auto diagnostics_items_clear =
          visible_named_items_do_not_overlap(edit_workspace, diagnostics_panel_items);
      if (!diagnostics_panel_visible || !diagnostics_panel_inside ||
          !diagnostics_items_fit || !diagnostics_items_clear) {
        if (detail != nullptr)
          *detail = QStringLiteral(
                        "width %1 unstable diagnostics overlay geometry visible=%2 inside=%3 fit=%4 clear=%5")
                        .arg(width)
                        .arg(diagnostics_panel_visible)
                        .arg(diagnostics_panel_inside)
                        .arg(diagnostics_items_fit)
                        .arg(diagnostics_items_clear);
        if (diagnostics_panel_visible) {
          const auto viewport_debug_rect = scene_rect(viewport_item);
          const auto panel_debug_rect = scene_rect(diagnostics_overlay_panel_item);
          std::cerr << "viewport rect=" << viewport_debug_rect.x() << ","
                    << viewport_debug_rect.y() << " "
                    << viewport_debug_rect.width() << "x"
                    << viewport_debug_rect.height() << " panel rect="
                    << panel_debug_rect.x() << "," << panel_debug_rect.y() << " "
                    << panel_debug_rect.width() << "x" << panel_debug_rect.height()
                    << '\n';
          log_visible_named_item_geometry(edit_workspace, diagnostics_panel_items);
        }
        return false;
      }
      edit_workspace->setProperty("diagnosticsPanelOpen", false);
      return true;
    };
    QString stable_chrome_detail;
    for (const auto width : QList<int>{1024, 1180, 1320, 1600}) {
      if (!check_stable_canvas_view_chrome(width, &stable_chrome_detail)) {
        std::cerr << "Canvas view chrome stability: "
                  << stable_chrome_detail.toStdString() << '\n';
        return fail("zoom, fit, pan, and overlay controls should keep stable positions");
      }
    }
    edit_workspace->setProperty("compareViewMode", QStringLiteral("split"));
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "activateEditTool",
            Q_ARG(QVariant, QVariant(QStringLiteral("compare"))))) {
      return fail("compare view should activate for stable-position proof");
    }
    process_layout_events();
    auto* before_after_split_panel =
        find_named_object(edit_workspace, QStringLiteral("beforeAfterSplitPanel"));
    auto* edit_context_panel = edit_workspace->findChild<QObject*>(QStringLiteral("editContextPanel"));
    if (before_after_split_panel == nullptr || edit_context_panel == nullptr ||
        edit_workspace->property("editContextPanelActive").toBool() ||
        edit_context_panel->property("visible").toBool() ||
        !before_after_split_panel->property("visible").toBool()) {
      return fail("compare view should use the canvas compare surface without shifting context chrome");
    }
    edit_workspace->setProperty("compareViewMode", QStringLiteral("wipe"));
    process_layout_events();
    if (!before_after_split_panel->property("visible").toBool()) {
      return fail("wipe compare mode should keep the stable compare panel slot");
    }
    edit_workspace->setProperty("compareViewMode", QStringLiteral("side-by-side"));
    process_layout_events();
    if (before_after_split_panel->property("visible").toBool()) {
      return fail("side-by-side compare mode should intentionally replace the split/wipe panel slot");
    }
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "activateEditTool",
            Q_ARG(QVariant, QVariant(QStringLiteral("adjust")))) ||
        !wait_for_condition([&]() {
          return edit_zoom_toolbar->property("visible").toBool() &&
                 edit_action_toolbar->property("visible").toBool();
        }, 1500) ||
        edit_workspace->property("activeCanvasToolChromeReduced").toBool()) {
      return fail("Adjust should show canvas view controls outside active command HUD mode");
    }
    auto check_command_tool_separates_view_controls =
        [&](const QString& tool_id, const QString& hud_name, QString* detail) {
          if (!QMetaObject::invokeMethod(
                  edit_workspace,
                  "activateEditTool",
                  Q_ARG(QVariant, QVariant(tool_id)))) {
            if (detail != nullptr)
              *detail = QStringLiteral("%1 activation failed").arg(tool_id);
            return false;
          }
          const bool ready = wait_for_condition([&]() {
            auto* hud = find_named_item(edit_workspace, hud_name);
            return item_visible(hud) &&
                   !edit_zoom_toolbar->property("visible").toBool() &&
                   !edit_action_toolbar->property("visible").toBool() &&
                   edit_workspace->property("activeCanvasToolChromeReduced").toBool();
          }, 1500);
          if (!ready && detail != nullptr) {
            auto* hud = find_named_item(edit_workspace, hud_name);
            *detail = QStringLiteral("%1 hudVisible=%2 zoomVisible=%3 actionVisible=%4 chromeReduced=%5")
                          .arg(tool_id)
                          .arg(item_visible(hud))
                          .arg(edit_zoom_toolbar->property("visible").toBool())
                          .arg(edit_action_toolbar->property("visible").toBool())
                          .arg(edit_workspace->property("activeCanvasToolChromeReduced").toBool());
          }
          return ready;
        };
    QString separation_detail;
    if (!check_command_tool_separates_view_controls(
            QStringLiteral("crop"),
            QStringLiteral("cropToolHud"),
            &separation_detail)) {
      std::cerr << "Crop view/control separation: " << separation_detail.toStdString() << '\n';
      return fail("Crop command mode should hide view toolbars and show its command HUD");
    }
    if (!check_command_tool_separates_view_controls(
            QStringLiteral("brush"),
            QStringLiteral("brushToolHud"),
            &separation_detail)) {
      std::cerr << "Brush view/control separation: " << separation_detail.toStdString() << '\n';
      return fail("Brush command mode should hide view toolbars and show its command HUD");
    }
    QVariant retouch_mode;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "startRetouchTool",
            Q_RETURN_ARG(QVariant, retouch_mode),
            Q_ARG(QVariant, QStringLiteral("heal")),
            Q_ARG(QVariant, false)) ||
        retouch_mode.toString() != QStringLiteral("heal") ||
        !wait_for_condition([&]() {
          auto* hud = find_named_item(edit_workspace, QStringLiteral("retouchToolHud"));
          return item_visible(hud) &&
                 !edit_zoom_toolbar->property("visible").toBool() &&
                 !edit_action_toolbar->property("visible").toBool() &&
                 edit_workspace->property("activeCanvasToolChromeReduced").toBool();
        }, 1500)) {
      std::cerr << "Retouch view/control separation: zoom="
                << edit_zoom_toolbar->property("visible").toBool()
                << " action=" << edit_action_toolbar->property("visible").toBool()
                << " chromeReduced=" << edit_workspace->property("activeCanvasToolChromeReduced").toBool()
                << '\n';
      return fail("Retouch command mode should hide view toolbars and show its command HUD");
    }
    trace_smoke_progress("focused view tool separation and stable canvas chrome checked");
    return EXIT_SUCCESS;
  }
  auto* white_balance_inspector_contract =
      find_named_object(edit_workspace, QStringLiteral("whiteBalanceInspectorGroup"));
  if (white_balance_inspector_contract == nullptr ||
      !white_balance_inspector_contract->property("sectionContract")
          .toString()
          .contains(QStringLiteral("inspector-section:v2")) ||
      !white_balance_inspector_contract->property("sectionContract")
          .toString()
          .contains(QStringLiteral("fixed-actions:true")) ||
      !white_balance_inspector_contract->property("sectionContract")
          .toString()
          .contains(QStringLiteral("icon-first-actions:true")) ||
      !white_balance_inspector_contract->property("sectionContract")
          .toString()
          .contains(QStringLiteral("keyboard:true")) ||
      !white_balance_inspector_contract->property("readableTextContract")
          .toString()
          .contains(QStringLiteral("ellipsis:none")) ||
      !white_balance_inspector_contract->property("sectionGeometryContract")
          .toString()
          .contains(QStringLiteral("header-min:30"))) {
    return fail("Studio adjustment inspector groups should render from the shared InspectorSection contract");
  }
  auto* photo_layer_add_split_contract =
      find_named_object(edit_workspace, QStringLiteral("photoLayerAddSplitButton"));
  if (photo_layer_add_split_contract == nullptr ||
      photo_layer_add_split_contract->property("text").toString() != QStringLiteral("Add") ||
      photo_layer_add_split_contract->property("accessibleName").toString() != QStringLiteral("Add layer") ||
      !photo_layer_add_split_contract->property("splitContract")
          .toString()
          .contains(QStringLiteral("split-button:v1")) ||
      !photo_layer_add_split_contract->property("splitContract")
          .toString()
          .contains(QStringLiteral("keyboard:true")) ||
      !photo_layer_add_split_contract->property("splitContract")
          .toString()
          .contains(QStringLiteral("|danger-last:true|")) ||
      !photo_layer_add_split_contract->property("splitContract")
          .toString()
          .contains(QStringLiteral("|disabled-reasons-complete:true|")) ||
      !photo_layer_add_split_contract->property("splitContract")
          .toString()
          .contains(QStringLiteral("no-elide:true")) ||
      photo_layer_add_split_contract->property("layerAddSplitContract").toString() !=
          QStringLiteral("layer-add-split:v2|primary:adjustment|menu:adjustment,paint,shapes,fill,text,paragraph,source,group|descriptions:8|checked:1|disabled-reasons:0|one-primary:true|permanent-fill-text:false|readable-menu:true|stack-clutter:false|no-elide:true")) {
    std::cerr << "layer Add object=" << (photo_layer_add_split_contract != nullptr);
    if (photo_layer_add_split_contract != nullptr) {
      std::cerr << " text=" << photo_layer_add_split_contract->property("text").toString().toStdString()
                << " split=" << photo_layer_add_split_contract->property("splitContract").toString().toStdString()
                << " layer=" << photo_layer_add_split_contract->property("layerAddSplitContract").toString().toStdString();
    }
    std::cerr << '\n';
    return fail("Studio layer Add should use one shared SplitButton with Adjustment as primary and Paint/Shapes/Fill/Text/Paragraph/Source/Group in the menu");
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_EDIT_RAIL")) {
    return EXIT_SUCCESS;
  }
  trace_smoke_progress("after edit rail checks");
  auto* workspace_tab_edit = find_named_object(root, QStringLiteral("workspaceTabEdit"));
  auto* workspace_tab_edit_label = find_named_object(root, QStringLiteral("workspaceTabLabelEdit"));
  auto* top_workspace_status_label = find_named_object(root, QStringLiteral("topWorkspaceStatusLabel"));
  auto* top_selection_status_label = find_named_object(root, QStringLiteral("topSelectionStatusLabel"));
  if (workspace_tab_edit == nullptr || workspace_tab_edit_label == nullptr ||
      top_workspace_status_label == nullptr || top_selection_status_label == nullptr ||
      workspace_tab_edit->property("text").toString() != QStringLiteral("Edit") ||
      workspace_tab_edit_label->property("text").toString() != QStringLiteral("Edit") ||
      top_workspace_status_label->property("text").toString().startsWith(QStringLiteral("EDIT")) ||
      top_selection_status_label->property("text").toString() == QStringLiteral("NO ACTIVE ASSET")) {
    return fail("global workspace chrome should use readable title-case navigation and status labels");
  }

  const auto viewport_asset_path = runtime_root.filePath(QStringLiteral("viewport-qml-proof.png"));
  if (!write_overlay_visual_fixture(viewport_asset_path, QStringLiteral("base"))) {
    return fail("viewport QML proof should create a readable test image");
  }
  const auto bright_overlay_asset_path =
      runtime_root.filePath(QStringLiteral("overlay-bright-proof.png"));
  const auto dark_overlay_asset_path = runtime_root.filePath(QStringLiteral("overlay-dark-proof.png"));
  const auto saturated_overlay_asset_path =
      runtime_root.filePath(QStringLiteral("overlay-saturated-proof.png"));
  const auto transparent_overlay_asset_path =
      runtime_root.filePath(QStringLiteral("overlay-transparent-proof.png"));
  if (!write_overlay_visual_fixture(bright_overlay_asset_path, QStringLiteral("bright")) ||
      !write_overlay_visual_fixture(dark_overlay_asset_path, QStringLiteral("dark")) ||
      !write_overlay_visual_fixture(saturated_overlay_asset_path, QStringLiteral("saturated")) ||
      !write_overlay_visual_fixture(transparent_overlay_asset_path, QStringLiteral("transparent"))) {
    return fail("overlay visual proof should create bright, dark, saturated, and transparent fixtures");
  }
  catalog_assets.replaceAssetsForTesting({make_asset(
      QStringLiteral("viewport-qml-proof"),
      viewport_asset_path,
      QUrl::fromLocalFile(viewport_asset_path).toString())});
  catalog_assets.selectIndex(0);
  if (!wait_for_condition([&]() {
        return editor_preview.hasAsset() &&
               editor_preview.assetId() == QStringLiteral("viewport-qml-proof");
      })) {
    return fail("viewport QML proof asset should load into the editor");
  }
  trace_smoke_progress("viewport proof asset loaded");
  auto* edit_viewport = find_named_object(edit_workspace, QStringLiteral("editImageViewport"));
  if (edit_viewport == nullptr ||
      !QMetaObject::invokeMethod(
          edit_viewport,
          "setZoom",
          Q_ARG(QVariant, QVariant(2.0)),
          Q_ARG(QVariant, QVariant(160.0)),
          Q_ARG(QVariant, QVariant(120.0)))) {
    return fail("edit ImageViewport should expose a callable zoom path");
  }
  if (!wait_for_condition([&]() {
        return editor_preview.previewViewportActive() &&
               editor_preview.previewViewportRect().isValid();
      })) {
    return fail("StudioWorkspace zoom should publish an active preview viewport to the controller");
  }
  trace_smoke_progress("zoom viewport published");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_BRUSH_CURSOR_OVERLAY")) {
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "setMaskCreationOperation",
            Q_ARG(QVariant, QVariant(QStringLiteral("add")))) ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "startMaskBrushSource",
            Q_ARG(QVariant, QVariant(QStringLiteral("add")))) ||
        !wait_for_condition([&]() {
          auto* hud = find_named_item(edit_workspace, QStringLiteral("brushToolHud"));
          auto* cursor = find_named_item(edit_workspace, QStringLiteral("editImageViewportBrushCursorRing"));
          return item_visible(hud) && item_visible(cursor);
        }, 3000)) {
      return fail("focused brush cursor proof should activate Brush and show its HUD/cursor");
    }
    editor_preview.setActiveMaskBrushSize(0.11);
    editor_preview.setActiveMaskBrushHardness(0.46);
    editor_preview.setActiveMaskBrushFlow(0.78);
    QCoreApplication::processEvents();
    auto* brush_cursor_ring =
        find_named_object(edit_workspace, QStringLiteral("editImageViewportBrushCursorRing"));
    auto* brush_cursor_hardness_ring =
        find_named_object(edit_workspace, QStringLiteral("editImageViewportBrushCursorHardnessRing"));
    auto* brush_cursor_mode_label =
        find_named_object(edit_workspace, QStringLiteral("editImageViewportBrushCursorModeLabel"));
    auto* brush_canvas_overlay =
        find_named_object(edit_workspace, QStringLiteral("editImageViewportMaskCanvasOverlay"));
    auto* brush_status_label =
        find_named_object(edit_workspace, QStringLiteral("brushHudStatusLabel"));
    const auto expected_size_text = QStringLiteral("Size 11%");
    const auto expected_hardness_text = QStringLiteral("Hard 46%");
    if (brush_cursor_ring == nullptr ||
        brush_cursor_hardness_ring == nullptr ||
        brush_cursor_mode_label == nullptr ||
        brush_canvas_overlay == nullptr ||
        brush_status_label == nullptr ||
        brush_cursor_ring->property("brushCursorVisibilityContract").toString() !=
            QStringLiteral("persistent-last-position|dual-contrast-ring|hardness-inner-ring:v1") ||
        brush_cursor_ring->property("brushDiameter").toDouble() <= 0.0 ||
        brush_cursor_ring->property("hardnessDiameter").toDouble() <= 0.0 ||
        brush_cursor_ring->property("hardnessDiameter").toDouble() >
            brush_cursor_ring->property("brushDiameter").toDouble() ||
        !item_visible(qobject_cast<QQuickItem*>(brush_cursor_hardness_ring)) ||
        !brush_cursor_mode_label->property("text").toString().contains(expected_size_text) ||
        !brush_cursor_mode_label->property("text").toString().contains(expected_hardness_text) ||
        !brush_status_label->property("text").toString().contains(expected_size_text) ||
        !brush_status_label->property("text").toString().contains(expected_hardness_text)) {
      std::cerr << "Brush cursor state: cursor="
                << (brush_cursor_ring == nullptr ? "null" : "present")
                << " hardnessRing=" << (brush_cursor_hardness_ring == nullptr ? "null" : "present")
                << " modeLabel="
                << (brush_cursor_mode_label == nullptr
                        ? QString()
                        : brush_cursor_mode_label->property("text").toString())
                       .toStdString()
                << " status="
                << (brush_status_label == nullptr
                        ? QString()
                        : brush_status_label->property("text").toString())
                       .toStdString()
                << '\n';
      return fail("Brush cursor should show live brush size and hardness directly on canvas");
    }
    editor_preview.beginActiveBrushStroke();
    editor_preview.addBrushPointWithTablet(0.42, 0.52, 0.64, 0.35, -0.20);
    QCoreApplication::processEvents();
    if (!editor_preview.brushStrokeActive() ||
        !brush_canvas_overlay->property("activeBrushPainting").toBool() ||
        !brush_canvas_overlay->property("fullMaskMatteVisible").toBool() ||
        !item_visible(qobject_cast<QQuickItem*>(brush_cursor_ring))) {
      std::cerr << "Brush live overlay state: stroke=" << editor_preview.brushStrokeActive()
                << " activeBrushPainting="
                << (brush_canvas_overlay == nullptr
                        ? false
                        : brush_canvas_overlay->property("activeBrushPainting").toBool())
                << " fullMaskMatteVisible="
                << (brush_canvas_overlay == nullptr
                        ? false
                        : brush_canvas_overlay->property("fullMaskMatteVisible").toBool())
                << '\n';
      return fail("Brush overlay should stay visible while painting");
    }
    editor_preview.endActiveBrushStroke();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    trace_smoke_progress("focused brush cursor overlay checked");
    return EXIT_SUCCESS;
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_BRUSH_CANCEL")) {
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "setMaskCreationOperation",
            Q_ARG(QVariant, QVariant(QStringLiteral("add")))) ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "startMaskBrushSource",
            Q_ARG(QVariant, QVariant(QStringLiteral("add")))) ||
        !wait_for_condition([&]() {
          auto* hud = find_named_item(edit_workspace, QStringLiteral("brushToolHud"));
          return item_visible(hud);
        }, 3000)) {
      return fail("focused brush cancel proof should activate Brush and show its HUD");
    }
    auto* brush_tool_hud_for_cancel =
        find_named_object(edit_workspace, QStringLiteral("brushToolHud"));
    auto* cancel_active_tool_shortcut =
        find_named_object(edit_workspace, QStringLiteral("cancelActiveToolShortcut"));
    const auto mask_count_before_cancel = editor_preview.maskCount();
    editor_preview.beginActiveBrushStroke();
    editor_preview.addBrushPointWithTablet(0.48, 0.50, 0.82, 0.0, 0.0);
    QCoreApplication::processEvents();
    QVariant brush_cancelled;
    if (cancel_active_tool_shortcut == nullptr ||
        !cancel_active_tool_shortcut->property("enabled").toBool() ||
        !editor_preview.brushStrokeActive() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "cancelActiveTool",
            Q_RETURN_ARG(QVariant, brush_cancelled)) ||
        !brush_cancelled.toBool() ||
        editor_preview.brushStrokeActive() ||
        edit_workspace->property("editTool").toString() != QStringLiteral("layers") ||
        !editor_preview.hasActiveMask() ||
        editor_preview.activeMaskKind() != QStringLiteral("brush") ||
        editor_preview.maskCount() != mask_count_before_cancel ||
        (brush_tool_hud_for_cancel != nullptr &&
         brush_tool_hud_for_cancel->property("visible").toBool())) {
      std::cerr << "Brush cancel state: shortcut="
                << (cancel_active_tool_shortcut == nullptr ? "null" : "present")
                << " shortcutEnabled="
                << (cancel_active_tool_shortcut != nullptr &&
                    cancel_active_tool_shortcut->property("enabled").toBool())
                << " cancelled=" << brush_cancelled.toBool()
                << " stroke=" << editor_preview.brushStrokeActive()
                << " tool=" << edit_workspace->property("editTool").toString().toStdString()
                << " hasMask=" << editor_preview.hasActiveMask()
                << " kind=" << editor_preview.activeMaskKind().toStdString()
                << " maskCountBefore=" << mask_count_before_cancel
                << " maskCountAfter=" << editor_preview.maskCount() << '\n';
      return fail("Escape should end an active brush stroke and return to Selections without deleting the brush selection");
    }
    trace_smoke_progress("focused brush cancel checked");
    return EXIT_SUCCESS;
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_TOOL_EXIT_PATHS")) {
    auto* cancel_active_tool_shortcut =
        find_named_object(edit_workspace, QStringLiteral("cancelActiveToolShortcut"));
    QVariant cancel_result;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "setMaskCreationOperation",
            Q_ARG(QVariant, QVariant(QStringLiteral("add")))) ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "startMaskBrushSource",
            Q_ARG(QVariant, QVariant(QStringLiteral("add")))) ||
        !wait_for_condition([&]() {
          auto* hud = find_named_item(edit_workspace, QStringLiteral("brushToolHud"));
          return item_visible(hud);
        }, 3000)) {
      return fail("focused tool exit proof should activate Brush and show its HUD");
    }
    const auto mask_count_before_brush_cancel = editor_preview.maskCount();
    editor_preview.beginActiveBrushStroke();
    editor_preview.addBrushPointWithTablet(0.48, 0.50, 0.82, 0.0, 0.0);
    QCoreApplication::processEvents();
    if (cancel_active_tool_shortcut == nullptr ||
        !cancel_active_tool_shortcut->property("enabled").toBool() ||
        !editor_preview.brushStrokeActive() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "cancelActiveTool",
            Q_RETURN_ARG(QVariant, cancel_result)) ||
        !cancel_result.toBool() ||
        editor_preview.brushStrokeActive() ||
        edit_workspace->property("editTool").toString() != QStringLiteral("layers") ||
        !editor_preview.hasActiveMask() ||
        editor_preview.activeMaskKind() != QStringLiteral("brush") ||
        editor_preview.maskCount() != mask_count_before_brush_cancel) {
      return fail("focused tool exit proof should exit Brush through the shared Escape/cancel path");
    }
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "activateEditTool",
            Q_ARG(QVariant, QVariant(QStringLiteral("retouch")))) ||
        !wait_for_condition([&]() {
          return edit_workspace->property("editTool").toString() == QStringLiteral("retouch") &&
                 editor_preview.hasActiveMask() &&
                 editor_preview.activeMaskToolRole() == QStringLiteral("retouch");
        }, 3000) ||
        !cancel_active_tool_shortcut->property("enabled").toBool() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "cancelActiveTool",
            Q_RETURN_ARG(QVariant, cancel_result)) ||
        !cancel_result.toBool() ||
        edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
        editor_preview.hasActiveMask()) {
      return fail("focused tool exit proof should exit Retouch through the shared Escape/cancel path");
    }
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "activateEditTool",
            Q_ARG(QVariant, QVariant(QStringLiteral("crop")))) ||
        !wait_for_condition([&]() {
          return edit_workspace->property("editTool").toString() == QStringLiteral("crop") &&
                 editor_preview.cropEditing();
        }, 3000) ||
        !cancel_active_tool_shortcut->property("enabled").toBool() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "cancelActiveTool",
            Q_RETURN_ARG(QVariant, cancel_result)) ||
        !cancel_result.toBool() ||
        edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
        editor_preview.cropEditing()) {
      return fail("focused tool exit proof should exit Crop through the shared Escape/cancel path");
    }
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "activateEditTool",
            Q_ARG(QVariant, QVariant(QStringLiteral("compare")))) ||
        !wait_for_condition([&]() {
          return edit_workspace->property("editTool").toString() == QStringLiteral("compare") &&
                 editor_preview.compareMode();
        }, 3000) ||
        !cancel_active_tool_shortcut->property("enabled").toBool() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "cancelActiveTool",
            Q_RETURN_ARG(QVariant, cancel_result)) ||
        !cancel_result.toBool() ||
        edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
        editor_preview.compareMode()) {
      return fail("focused tool exit proof should exit Compare through the shared Escape/cancel path");
    }
    edit_workspace->setProperty("diagnosticsPanelOpen", true);
    QCoreApplication::processEvents();
    if (!cancel_active_tool_shortcut->property("enabled").toBool() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "cancelActiveTool",
            Q_RETURN_ARG(QVariant, cancel_result)) ||
        !cancel_result.toBool() ||
        edit_workspace->property("diagnosticsPanelOpen").toBool() ||
        edit_workspace->property("editTool").toString() != QStringLiteral("adjust")) {
      return fail("focused tool exit proof should close transient diagnostics before changing tools");
    }
    trace_smoke_progress("focused tool exit paths checked");
    return EXIT_SUCCESS;
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_BRUSH_WORKFLOW")) {
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "setMaskCreationOperation",
            Q_ARG(QVariant, QVariant(QStringLiteral("add")))) ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "startMaskBrushSource",
            Q_ARG(QVariant, QVariant(QStringLiteral("add")))) ||
        !wait_for_condition([&]() {
          auto* hud = find_named_item(edit_workspace, QStringLiteral("brushToolHud"));
          return item_visible(hud);
        }, 3000)) {
      return fail("Brush workflow proof should enter Brush through the selection workflow");
    }
    auto* brush_tool_hud_for_workflow =
        find_named_object(edit_workspace, QStringLiteral("brushToolHud"));
    auto* brush_status_for_workflow =
        find_named_object(edit_workspace, QStringLiteral("brushHudStatusLabel"));
    const auto initial_workflow_mask_count = editor_preview.maskCount();
    const auto initial_brush_size = editor_preview.activeMaskBrushSize();
    editor_preview.beginActiveBrushStroke();
    editor_preview.addBrushPointWithTablet(0.40, 0.46, 0.70, 0.0, 0.0);
    QCoreApplication::processEvents();
    const auto paint_step_ok = editor_preview.brushStrokeActive();
    editor_preview.endActiveBrushStroke();
    QCoreApplication::processEvents();
    QVariant erase_mode_result;
    QVariant size_tuned;
    QVariant done_result;
    if (brush_tool_hud_for_workflow == nullptr ||
        brush_status_for_workflow == nullptr ||
        !paint_step_ok ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "setBrushPaintMode",
            Q_RETURN_ARG(QVariant, erase_mode_result),
            Q_ARG(QVariant, QVariant(QStringLiteral("subtract")))) ||
        erase_mode_result.toString() != QStringLiteral("subtract") ||
        editor_preview.activeMaskOperation() != QStringLiteral("subtract") ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "adjustActiveBrushSize",
            Q_RETURN_ARG(QVariant, size_tuned),
            Q_ARG(QVariant, QVariant(0.03))) ||
        !size_tuned.toBool() ||
        editor_preview.activeMaskBrushSize() <= initial_brush_size ||
        !brush_status_for_workflow->property("text").toString().contains(QStringLiteral("Size")) ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "finishActiveCanvasTool",
            Q_RETURN_ARG(QVariant, done_result)) ||
        !done_result.toBool() ||
        edit_workspace->property("editTool").toString() != QStringLiteral("layers") ||
        !editor_preview.hasActiveMask() ||
        editor_preview.activeMaskKind() != QStringLiteral("brush") ||
        editor_preview.maskCount() != initial_workflow_mask_count ||
        brush_tool_hud_for_workflow->property("visible").toBool()) {
      std::cerr << "Brush workflow state: paint=" << paint_step_ok
                << " erase=" << erase_mode_result.toString().toStdString()
                << " sizeTuned=" << size_tuned.toBool()
                << " initialSize=" << initial_brush_size
                << " currentSize=" << editor_preview.activeMaskBrushSize()
                << " done=" << done_result.toBool()
                << " tool=" << edit_workspace->property("editTool").toString().toStdString()
                << " maskCountBefore=" << initial_workflow_mask_count
                << " maskCountAfter=" << editor_preview.maskCount() << '\n';
      return fail("Brush workflow should support enter, paint, erase, tune, and Done without losing the selection");
    }
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "startMaskBrushSource",
            Q_ARG(QVariant, QVariant(QStringLiteral("add")))) ||
        !wait_for_condition([&]() {
          auto* hud = find_named_item(edit_workspace, QStringLiteral("brushToolHud"));
          return item_visible(hud);
        }, 3000)) {
      return fail("Brush workflow proof should re-enter Brush before testing cancel");
    }
    const auto mask_count_before_workflow_cancel = editor_preview.maskCount();
    editor_preview.beginActiveBrushStroke();
    editor_preview.addBrushPointWithTablet(0.52, 0.54, 0.66, 0.0, 0.0);
    QCoreApplication::processEvents();
    QVariant cancel_result;
    if (!editor_preview.brushStrokeActive() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "cancelActiveTool",
            Q_RETURN_ARG(QVariant, cancel_result)) ||
        !cancel_result.toBool() ||
        editor_preview.brushStrokeActive() ||
        edit_workspace->property("editTool").toString() != QStringLiteral("layers") ||
        !editor_preview.hasActiveMask() ||
        editor_preview.activeMaskKind() != QStringLiteral("brush") ||
        editor_preview.maskCount() != mask_count_before_workflow_cancel) {
      std::cerr << "Brush workflow cancel state: cancel=" << cancel_result.toBool()
                << " stroke=" << editor_preview.brushStrokeActive()
                << " tool=" << edit_workspace->property("editTool").toString().toStdString()
                << " maskCountBefore=" << mask_count_before_workflow_cancel
                << " maskCountAfter=" << editor_preview.maskCount() << '\n';
      return fail("Brush workflow should support cancel after painting without deleting the selection");
    }
    trace_smoke_progress("focused brush workflow checked");
    return EXIT_SUCCESS;
  }
  const auto run_pixel_zoom_visual_proof =
      qEnvironmentVariableIsSet("PHOTO_EDITOR_RUN_PIXEL_ZOOM_VISUAL_PROOF") ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_PIXEL_ZOOM");
  if (!run_pixel_zoom_visual_proof) {
    editor_preview.setPreviewViewport(0, 0, 0, 0, 0, false);
    catalog_assets.replaceAssetsForTesting(
        {make_asset(QStringLiteral("asset-2"), QStringLiteral("IMG_0002.jpg"))});
    if (!wait_for_condition([&]() {
          return editor_preview.assetId() == QStringLiteral("asset-2") &&
                 !editor_preview.processing();
        }, 10000)) {
      return fail("viewport QML proof should restore the lightweight smoke asset before continuing");
    }
    trace_smoke_progress("heavy pixel zoom visual proof skipped");
  } else {
  const auto zoom_viewport_rect = editor_preview.previewViewportRect();
  if (zoom_viewport_rect.width() <= 0.0 || zoom_viewport_rect.height() <= 0.0 ||
      zoom_viewport_rect.width() > 1.0 || zoom_viewport_rect.height() > 1.0 ||
      (zoom_viewport_rect.width() >= 0.99 && zoom_viewport_rect.height() >= 0.99) ||
      editor_preview.previewViewportPatchMaxEdge() <= 0) {
    return fail("StudioWorkspace zoom should publish a bounded normalized source viewport");
  }
  edit_viewport->setProperty("panX", edit_viewport->property("panX").toDouble() - 96.0);
  edit_viewport->setProperty("panY", edit_viewport->property("panY").toDouble() - 64.0);
  if (!wait_for_condition([&]() {
        return editor_preview.previewViewportActive() &&
               editor_preview.previewViewportRect() != zoom_viewport_rect;
  })) {
    return fail("StudioWorkspace pan should update the published preview viewport");
  }
  const auto pixel_zoom_asset_path =
      runtime_root.filePath(QStringLiteral("pixel-zoom-proof.png"));
  QImage pixel_zoom_fixture(32, 32, QImage::Format_RGB32);
  for (int y = 0; y < pixel_zoom_fixture.height(); ++y) {
    for (int x = 0; x < pixel_zoom_fixture.width(); ++x) {
      const auto checker = ((x / 4) + (y / 4)) % 2 == 0;
      pixel_zoom_fixture.setPixelColor(
          x,
          y,
          checker ? QColor(24, 26, 30) : QColor(225, 229, 235));
    }
  }
  if (!pixel_zoom_fixture.save(pixel_zoom_asset_path)) {
    return fail("pixel zoom proof should create a source-pixel fixture");
  }
  catalog_assets.replaceAssetsForTesting({make_asset(
      QStringLiteral("pixel-zoom-proof"),
      pixel_zoom_asset_path,
      QUrl::fromLocalFile(pixel_zoom_asset_path).toString())});
  catalog_assets.selectIndex(0);
  if (!wait_for_condition([&]() {
        return editor_preview.hasAsset() &&
               editor_preview.assetId() == QStringLiteral("pixel-zoom-proof") &&
               editor_preview.sourceImageWidth() == pixel_zoom_fixture.width() &&
               editor_preview.sourceImageHeight() == pixel_zoom_fixture.height();
      })) {
    return fail("pixel zoom proof asset should publish source image dimensions");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "createMaskSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("rectangle"))))) {
    return fail("pixel zoom visual proof should create an aligned rectangle selection overlay");
  }
  editor_preview.setActiveMaskCenterX(0.5);
  editor_preview.setActiveMaskCenterY(0.5);
  editor_preview.setActiveMaskRadiusX(0.24);
  editor_preview.setActiveMaskRadiusY(0.24);
  editor_preview.setActiveMaskFeather(0.0);
  editor_preview.setMaskPreviewOverlayOpacity(0.88);
  editor_preview.setMaskPreviewOverlayColor(QStringLiteral("#24dcc2"));
  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  process_layout_events(12);
  auto* pixel_grid_overlay =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportPixelGridOverlay"));
  auto* native_preview_image =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportNativePreviewImage"));
  auto* shared_mask_overlay =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportSharedMaskOverlay"));
  auto* shared_mask_outline =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportSharedActiveSelectionOutline"));
  auto* pixel_loupe_view =
      find_named_object(edit_workspace, QStringLiteral("loupeView"));
  auto* pixel_focus_peaking_overlay =
      find_named_object(edit_workspace, QStringLiteral("focusPeakingOverlay"));
  if (pixel_grid_overlay == nullptr || native_preview_image == nullptr ||
      shared_mask_overlay == nullptr || shared_mask_outline == nullptr ||
      pixel_loupe_view == nullptr || pixel_focus_peaking_overlay == nullptr) {
    return fail("pixel zoom visual proof should expose grid, native preview, mask overlay, outline, and high-cost overlay objects");
  }
  const auto capture_pixel_zoom_proof = [&](double zoom_level) {
    if (!QMetaObject::invokeMethod(
            edit_viewport,
            "setZoom",
            Q_ARG(QVariant, QVariant(zoom_level)),
            Q_ARG(QVariant, QVariant(180.0)),
            Q_ARG(QVariant, QVariant(140.0)))) {
      std::cerr << "Unable to set pixel proof zoom " << zoom_level << '\n';
      return false;
    }
    process_layout_events(18);
    if (!wait_for_condition([&]() {
          return std::abs(edit_viewport->property("zoom").toDouble() - zoom_level) < 0.02 &&
                 edit_viewport->property("pixelGridVisible").toBool() &&
                 item_visible(qobject_cast<QQuickItem*>(pixel_grid_overlay)) &&
                 item_visible(qobject_cast<QQuickItem*>(shared_mask_overlay)) &&
                 item_visible(qobject_cast<QQuickItem*>(shared_mask_outline)) &&
                 (native_preview_image->property("crispPixels").toBool() ||
                  edit_viewport->property("providerPreviewVisible").toBool());
        }, 10000)) {
      std::cerr << "pixel visual proof state: zoom=" << edit_viewport->property("zoom").toDouble()
                << " requested=" << zoom_level
                << " pixelGridVisible=" << edit_viewport->property("pixelGridVisible").toBool()
                << " gridVisible=" << item_visible(qobject_cast<QQuickItem*>(pixel_grid_overlay))
                << " maskOverlayVisible=" << item_visible(qobject_cast<QQuickItem*>(shared_mask_overlay))
                << " outlineVisible=" << item_visible(qobject_cast<QQuickItem*>(shared_mask_outline))
                << " crispPixels=" << native_preview_image->property("crispPixels").toBool()
                << " pixelInspectionActive="
                << edit_viewport->property("pixelInspectionActive").toBool()
                << " pixelGridFullResolutionBacked="
                << edit_viewport->property("pixelGridFullResolutionBacked").toBool()
                << " nativePreviewActive="
                << edit_viewport->property("nativePreviewActive").toBool()
                << " nativeAvailable=" << editor_preview.displayPreviewNativeAvailable()
                << " nativeFullResolution=" << editor_preview.displayPreviewNativeFullResolution()
                << " nativeSize="
                << editor_preview.displayPreviewNativeSize().width() << "x"
                << editor_preview.displayPreviewNativeSize().height()
                << " previewViewportActive=" << editor_preview.previewViewportActive()
                << " previewViewportPatchMaxEdge="
                << editor_preview.previewViewportPatchMaxEdge()
                << " sourcePixelScreenWidth="
                << edit_viewport->property("sourcePixelScreenWidth").toDouble()
                << " sourcePixelScreenHeight="
                << edit_viewport->property("sourcePixelScreenHeight").toDouble()
                << '\n';
      return false;
    }
    QJsonObject metadata;
    metadata.insert(QStringLiteral("zoom"), zoom_level);
    metadata.insert(QStringLiteral("sourceImageWidth"), editor_preview.sourceImageWidth());
    metadata.insert(QStringLiteral("sourceImageHeight"), editor_preview.sourceImageHeight());
    metadata.insert(
        QStringLiteral("sourcePixelScreenWidth"),
        edit_viewport->property("sourcePixelScreenWidth").toDouble());
    metadata.insert(
        QStringLiteral("sourcePixelScreenHeight"),
        edit_viewport->property("sourcePixelScreenHeight").toDouble());
    metadata.insert(
        QStringLiteral("pixelGridThreshold"),
        edit_viewport->property("pixelGridThreshold").toDouble());
    return capture_visual_proof(
        root_window,
        root,
        visual_proof_dir,
        QStringLiteral("pixel-zoom-%1-grid-mask").arg(static_cast<int>(zoom_level * 100.0)),
        1600,
        1000,
        {
            QStringLiteral("editImageViewport"),
            QStringLiteral("editImageViewportPixelGridOverlay"),
            QStringLiteral("editImageViewportSharedMaskOverlay"),
            QStringLiteral("editImageViewportSharedActiveSelectionOutline"),
        },
        &visual_proof_manifest,
        metadata);
  };
  for (const auto zoom_level : QVector<double>{1.0, 2.0, 4.0, 8.0}) {
    if (!capture_pixel_zoom_proof(zoom_level)) {
      return fail("desktop visual proof should capture crisp pixel grid and mask overlay alignment at 100, 200, 400, and 800 percent zoom");
    }
  }
  if (!QMetaObject::invokeMethod(
          edit_viewport,
          "setZoom",
          Q_ARG(QVariant, QVariant(8.0)),
          Q_ARG(QVariant, QVariant(180.0)),
          Q_ARG(QVariant, QVariant(140.0)))) {
    return fail("edit ImageViewport should allow direct 800 percent inspection zoom");
  }
  if (!wait_for_condition([&]() {
        return edit_viewport->property("zoom").toDouble() > 7.99 &&
               edit_viewport->property("pixelGridVisible").toBool() &&
               editor_preview.previewViewportActive();
      })) {
    std::cerr << "pixel zoom state: zoom=" << edit_viewport->property("zoom").toDouble()
              << " pixelGridVisible=" << edit_viewport->property("pixelGridVisible").toBool()
              << " previewViewportActive=" << editor_preview.previewViewportActive()
              << " previewContentVisible="
              << edit_viewport->property("previewContentVisible").toBool()
              << " sourcePixelScreenWidth="
              << edit_viewport->property("sourcePixelScreenWidth").toDouble()
              << " sourcePixelScreenHeight="
              << edit_viewport->property("sourcePixelScreenHeight").toDouble()
              << " sourceImageWidth=" << editor_preview.sourceImageWidth()
              << " sourceImageHeight=" << editor_preview.sourceImageHeight()
              << '\n';
    return fail("StudioWorkspace should expose an 800 percent pixel grid inspection state");
  }
  const auto pixel_inspection_contract =
      edit_viewport->property("pixelInspectionContract").toString();
  if (!pixel_inspection_contract.contains(QStringLiteral("zoom-levels:100,200,400,800")) ||
      !pixel_inspection_contract.contains(QStringLiteral("adaptive-source-pixel")) ||
      !pixel_inspection_contract.contains(QStringLiteral("crisp-source-pixels:true")) ||
      !pixel_inspection_contract.contains(QStringLiteral("grid-threshold:8px")) ||
      edit_viewport->property("maxInspectionZoom").toDouble() < 8.0 ||
      edit_viewport->property("pixelGridThreshold").toDouble() != 8.0 ||
      edit_viewport->property("sourcePixelScreenWidth").toDouble() < 8.0 ||
      edit_viewport->property("sourcePixelScreenHeight").toDouble() < 8.0 ||
      pixel_grid_overlay == nullptr ||
      !item_visible(qobject_cast<QQuickItem*>(pixel_grid_overlay)) ||
      native_preview_image == nullptr ||
      (!native_preview_image->property("crispPixels").toBool() &&
       !edit_viewport->property("providerPreviewVisible").toBool())) {
    return fail("StudioWorkspace pixel inspection should expose 100/200/400/800 zoom, crisp pixels, and a threshold-gated pixel grid");
  }
  const auto full_resolution_pixel_zoom_asset_path =
      runtime_root.filePath(QStringLiteral("pixel-zoom-full-resolution-proof.png"));
  QImage full_resolution_pixel_zoom_fixture(2048, 1536, QImage::Format_RGB32);
  for (int y = 0; y < full_resolution_pixel_zoom_fixture.height(); ++y) {
    for (int x = 0; x < full_resolution_pixel_zoom_fixture.width(); ++x) {
      const auto checker = ((x / 2) + (y / 2)) % 2 == 0;
      full_resolution_pixel_zoom_fixture.setPixelColor(
          x,
          y,
          checker ? QColor(18, 20, 24) : QColor(232, 237, 242));
    }
  }
  if (!full_resolution_pixel_zoom_fixture.save(full_resolution_pixel_zoom_asset_path)) {
    return fail("full-resolution pixel zoom proof should create a large source-pixel fixture");
  }
  catalog_assets.replaceAssetsForTesting({make_asset(
      QStringLiteral("pixel-zoom-full-resolution-proof"),
      full_resolution_pixel_zoom_asset_path,
      QUrl::fromLocalFile(full_resolution_pixel_zoom_asset_path).toString())});
  catalog_assets.selectIndex(0);
	  if (!wait_for_condition([&]() {
	        return editor_preview.hasAsset() &&
	               editor_preview.assetId() == QStringLiteral("pixel-zoom-full-resolution-proof") &&
	               editor_preview.sourceImageWidth() == full_resolution_pixel_zoom_fixture.width() &&
	               editor_preview.sourceImageHeight() == full_resolution_pixel_zoom_fixture.height() &&
	               !editor_preview.processing();
	      }, 15000)) {
	    return fail("full-resolution pixel zoom proof asset should publish source image dimensions");
	  }
  process_layout_events(18);
  const auto required_source_pixel_zoom =
      edit_viewport->property("sourcePixelInspectionZoom").toDouble();
  const auto adaptive_max_zoom =
      edit_viewport->property("maxInspectionZoom").toDouble();
  QVariant full_resolution_inspection_started;
  if (required_source_pixel_zoom <= 8.0 ||
      adaptive_max_zoom <= 8.0 ||
      adaptive_max_zoom + 0.0001 < required_source_pixel_zoom ||
      !QMetaObject::invokeMethod(
          edit_viewport,
          "inspectDetailAtOneToOne",
          Q_RETURN_ARG(QVariant, full_resolution_inspection_started)) ||
      !full_resolution_inspection_started.toBool()) {
    std::cerr << "full-resolution pixel zoom setup: required="
              << required_source_pixel_zoom
              << " max=" << adaptive_max_zoom
              << " started=" << full_resolution_inspection_started.toBool()
              << '\n';
    return fail("full-resolution pixel zoom should compute an adaptive max zoom above 800 percent");
  }
  QElapsedTimer immediate_pixel_grid_timer;
  immediate_pixel_grid_timer.start();
		  if (!wait_for_condition([&]() {
		        return edit_viewport->property("zoom").toDouble() > 8.0 &&
		               edit_viewport->property("pixelInspectionActive").toBool() &&
		               edit_viewport->property("pixelGridVisible").toBool() &&
		               item_visible(qobject_cast<QQuickItem*>(pixel_grid_overlay));
			      }, 750)) {
    std::cerr << "immediate full-resolution pixel grid state: zoom="
              << edit_viewport->property("zoom").toDouble()
              << " pixelInspectionActive="
              << edit_viewport->property("pixelInspectionActive").toBool()
              << " pixelGridVisible=" << edit_viewport->property("pixelGridVisible").toBool()
              << " sourceBacked="
              << edit_viewport->property("pixelGridFullResolutionBacked").toBool()
              << " sourcePixelScreenWidth="
              << edit_viewport->property("sourcePixelScreenWidth").toDouble()
              << " sourcePixelScreenHeight="
              << edit_viewport->property("sourcePixelScreenHeight").toDouble()
              << '\n';
	    return fail("full-resolution pixel zoom should show the pixel grid immediately instead of waiting for the source tile render");
	  }
  if (!edit_viewport->property("highCostInspectionOverlaysVisible").toBool() ||
      !item_visible(qobject_cast<QQuickItem*>(pixel_loupe_view))) {
    return fail("full-resolution pixel inspection should keep explicit loupe and analysis overlays available while source pixels are visible");
  }
	  const auto immediate_pixel_grid_elapsed_ms = immediate_pixel_grid_timer.elapsed();
  if (adaptive_max_zoom < 64.0 ||
      adaptive_max_zoom + 0.0001 < required_source_pixel_zoom * 3.5) {
    std::cerr << "deep full-resolution pixel zoom setup: required="
              << required_source_pixel_zoom
              << " max=" << adaptive_max_zoom
              << '\n';
    return fail("full-resolution pixel zoom should allow deep inspection beyond the first readable-pixel threshold");
  }
  if (!QMetaObject::invokeMethod(
          edit_viewport,
          "setZoom",
          Q_ARG(QVariant, QVariant(adaptive_max_zoom)),
          Q_ARG(QVariant, QVariant(220.0)),
          Q_ARG(QVariant, QVariant(180.0)))) {
    return fail("full-resolution pixel zoom should allow jumping to the adaptive deep-zoom cap");
  }
  if (!wait_for_condition([&]() {
        return edit_viewport->property("zoom").toDouble() >= adaptive_max_zoom - 0.02 &&
               edit_viewport->property("pixelInspectionActive").toBool() &&
               edit_viewport->property("pixelGridVisible").toBool() &&
               edit_viewport->property("sourcePixelScreenWidth").toDouble() >=
                   edit_viewport->property("pixelGridThreshold").toDouble() * 3.0 &&
               edit_viewport->property("sourcePixelScreenHeight").toDouble() >=
                   edit_viewport->property("pixelGridThreshold").toDouble() * 3.0;
      }, 1000)) {
    std::cerr << "deep full-resolution pixel zoom state: zoom="
              << edit_viewport->property("zoom").toDouble()
              << " max=" << adaptive_max_zoom
              << " sourcePixelScreenWidth="
              << edit_viewport->property("sourcePixelScreenWidth").toDouble()
              << " sourcePixelScreenHeight="
              << edit_viewport->property("sourcePixelScreenHeight").toDouble()
              << " threshold=" << edit_viewport->property("pixelGridThreshold").toDouble()
              << '\n';
    return fail("full-resolution pixel zoom should make source pixels substantially larger than the grid threshold");
  }
	  if (!wait_for_condition([&]() {
	        return edit_viewport->property("zoom").toDouble() > 8.0 &&
               edit_viewport->property("maxInspectionZoom").toDouble() > 8.0 &&
               edit_viewport->property("sourcePixelScreenWidth").toDouble() >=
                   edit_viewport->property("pixelGridThreshold").toDouble() &&
               edit_viewport->property("sourcePixelScreenHeight").toDouble() >=
                   edit_viewport->property("pixelGridThreshold").toDouble() &&
               edit_viewport->property("pixelInspectionActive").toBool() &&
               edit_viewport->property("pixelGridVisible").toBool() &&
               edit_viewport->property("pixelGridFullResolutionBacked").toBool() &&
               item_visible(qobject_cast<QQuickItem*>(pixel_grid_overlay)) &&
               editor_preview.previewViewportActive();
      }, 15000)) {
    std::cerr << "full-resolution pixel zoom state: zoom="
              << edit_viewport->property("zoom").toDouble()
              << " required=" << required_source_pixel_zoom
              << " max=" << adaptive_max_zoom
              << " sourcePixelScreenWidth="
              << edit_viewport->property("sourcePixelScreenWidth").toDouble()
              << " sourcePixelScreenHeight="
              << edit_viewport->property("sourcePixelScreenHeight").toDouble()
              << " threshold=" << edit_viewport->property("pixelGridThreshold").toDouble()
              << " pixelInspectionActive="
              << edit_viewport->property("pixelInspectionActive").toBool()
              << " pixelGridVisible=" << edit_viewport->property("pixelGridVisible").toBool()
              << " sourceBacked="
              << edit_viewport->property("pixelGridFullResolutionBacked").toBool()
              << " previewViewportActive=" << editor_preview.previewViewportActive()
              << " nativeFullResolution=" << editor_preview.displayPreviewNativeFullResolution()
              << " nativeSize="
              << editor_preview.displayPreviewNativeSize().width() << "x"
              << editor_preview.displayPreviewNativeSize().height()
              << '\n';
    return fail("full-resolution pixel zoom should zoom past 800 percent until individual source pixels are visible");
  }
  const auto final_source_pixel_zoom =
      edit_viewport->property("sourcePixelInspectionZoom").toDouble();
  const auto final_adaptive_max_zoom =
      edit_viewport->property("maxInspectionZoom").toDouble();
  QJsonObject full_resolution_metadata;
  full_resolution_metadata.insert(QStringLiteral("zoom"), edit_viewport->property("zoom").toDouble());
  full_resolution_metadata.insert(QStringLiteral("maxInspectionZoom"), final_adaptive_max_zoom);
  full_resolution_metadata.insert(QStringLiteral("sourcePixelInspectionZoom"), final_source_pixel_zoom);
  full_resolution_metadata.insert(
      QStringLiteral("initialSourcePixelInspectionZoom"),
      required_source_pixel_zoom);
  full_resolution_metadata.insert(QStringLiteral("initialMaxInspectionZoom"), adaptive_max_zoom);
  full_resolution_metadata.insert(
      QStringLiteral("immediatePixelGridElapsedMs"),
      static_cast<int>(immediate_pixel_grid_elapsed_ms));
	  full_resolution_metadata.insert(
	      QStringLiteral("pixelGridFullResolutionBacked"),
	      edit_viewport->property("pixelGridFullResolutionBacked").toBool());
  full_resolution_metadata.insert(
      QStringLiteral("highCostInspectionOverlaysVisible"),
      edit_viewport->property("highCostInspectionOverlaysVisible").toBool());
  full_resolution_metadata.insert(
      QStringLiteral("loupeVisibleDuringPixelInspection"),
      item_visible(qobject_cast<QQuickItem*>(pixel_loupe_view)));
  full_resolution_metadata.insert(
      QStringLiteral("focusPeakingVisibleDuringPixelInspection"),
      item_visible(qobject_cast<QQuickItem*>(pixel_focus_peaking_overlay)));
  full_resolution_metadata.insert(QStringLiteral("sourceImageWidth"), editor_preview.sourceImageWidth());
  full_resolution_metadata.insert(QStringLiteral("sourceImageHeight"), editor_preview.sourceImageHeight());
  full_resolution_metadata.insert(
      QStringLiteral("sourcePixelScreenWidth"),
      edit_viewport->property("sourcePixelScreenWidth").toDouble());
  full_resolution_metadata.insert(
      QStringLiteral("sourcePixelScreenHeight"),
      edit_viewport->property("sourcePixelScreenHeight").toDouble());
  full_resolution_metadata.insert(
      QStringLiteral("pixelGridThreshold"),
      edit_viewport->property("pixelGridThreshold").toDouble());
  if (!capture_visual_proof(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("pixel-zoom-full-resolution-adaptive-grid"),
          1600,
          1000,
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("editImageViewportPixelGridOverlay"),
          },
          &visual_proof_manifest,
          full_resolution_metadata)) {
    return fail("desktop visual proof should capture an adaptive full-resolution pixel grid above 800 percent zoom");
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_PIXEL_ZOOM")) {
    const auto visual_manifest_path = QDir(visual_proof_dir).filePath(QStringLiteral("manifest.json"));
    QFile visual_manifest_file(visual_manifest_path);
    if (!visual_manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        visual_manifest_file.write(QJsonDocument(visual_proof_manifest).toJson(QJsonDocument::Indented)) <=
            0) {
      return fail("focused pixel zoom visual proof should write a manifest");
    }
    visual_manifest_file.close();
    std::cout << "desktop_visual_proof_dir=" << visual_proof_dir.toStdString() << '\n';
    return EXIT_SUCCESS;
  }
  editor_preview.setPreviewViewport(0, 0, 0, 0, 0, false);
  catalog_assets.replaceAssetsForTesting(
      {make_asset(QStringLiteral("asset-2"), QStringLiteral("IMG_0002.jpg"))});
  if (!wait_for_condition([&]() {
        return editor_preview.assetId() == QStringLiteral("asset-2") && !editor_preview.processing();
      }, 10000)) {
    return fail("viewport QML proof should restore the lightweight smoke asset before continuing");
  }
  trace_smoke_progress("lightweight asset restored");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activateBaseImageLayerTarget") ||
      !wait_for_condition([&]() {
        QVariant active_scope;
        if (!QMetaObject::invokeMethod(
                edit_workspace,
                "activeEditTargetScope",
                Q_RETURN_ARG(QVariant, active_scope))) {
          return false;
        }
        return edit_workspace->property("editTool").toString() == QStringLiteral("adjust") &&
               active_scope.toString() == QStringLiteral("base-image") &&
               !edit_workspace->property("activeCanvasToolChromeReduced").toBool() &&
               !editor_preview.hasActiveMask() &&
               !editor_preview.compareMode() &&
               !editor_preview.cropEditing();
      })) {
    return fail("smoke proof should restore neutral Adjust chrome before checking filmstrip and inspector layout");
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYER_HOVER_LIGHTWEIGHT")) {
    edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
    if (!editor_preview.addFillLayer(QStringLiteral("#808080"), QStringLiteral("Hover Proof Fill")) ||
        !editor_preview.addTextLayer(QStringLiteral("Layer"), QStringLiteral("Hover Proof Text"))) {
      return fail("layer hover lightweight proof should create editable layers");
    }
    edit_workspace->setProperty("activePhotoLayerIndex", 0);
    for (auto iteration = 0; iteration < 8; ++iteration) {
      QCoreApplication::processEvents();
    }
    auto* photo_layers_group =
        find_named_object(edit_workspace, QStringLiteral("photoLayersInspectorGroup"));
    auto* photo_layer_inline_actions =
        find_named_object(edit_workspace, QStringLiteral("photoLayerInlineActions"));
    auto* photo_layer_structure_actions =
        find_named_object(edit_workspace, QStringLiteral("photoLayerStructureActions"));
    auto* photo_layer_fixed_state_action_zone =
        find_named_object(edit_workspace, QStringLiteral("photoLayerFixedStateActionZone"));
    const auto sparse_layer_stack_mode =
        photo_layers_group != nullptr &&
        photo_layers_group->property("sparseLayerUiContract")
            .toString()
            .contains(QStringLiteral("layer-sparse-ui:v3"));
    if (!sparse_layer_stack_mode ||
        photo_layer_inline_actions == nullptr ||
        photo_layer_structure_actions == nullptr ||
        photo_layer_fixed_state_action_zone == nullptr ||
        photo_layer_inline_actions->property("active").toBool() ||
        photo_layer_structure_actions->property("active").toBool() ||
        photo_layer_inline_actions->property("actionVisibilityContract").toString() !=
            QStringLiteral("layer-actions:v2|lazy:true|active:false|hover-instantiates:false|precision-controls:properties-panel|keyboard-accessible:properties-panel|disabled-reasons:true") ||
        photo_layer_structure_actions->property("actionVisibilityContract").toString() !=
            QStringLiteral("layer-structure-actions:v2|lazy:true|active:false|hover-instantiates:false|reorder:properties-menu|overflow-menu:true|destructive-separated:true|disabled-reasons:true") ||
        find_named_object(edit_workspace, QStringLiteral("photoLayerSelectionButtonLegacy")) != nullptr ||
        find_named_object(edit_workspace, QStringLiteral("photoLayerOpacitySliderLegacy")) != nullptr ||
        find_named_object(edit_workspace, QStringLiteral("photoLayerMaskOpacitySliderLegacy")) != nullptr ||
        find_named_object(edit_workspace, QStringLiteral("photoLayerMaskDensitySliderLegacy")) != nullptr ||
        find_named_object(edit_workspace, QStringLiteral("photoLayerBlendModeComboBoxLegacy")) != nullptr ||
        find_named_object(edit_workspace, QStringLiteral("photoLayerClippingMaskButtonLegacy")) != nullptr ||
        find_named_object(edit_workspace, QStringLiteral("photoLayerMaskButtonLegacy")) != nullptr ||
        find_named_object(edit_workspace, QStringLiteral("photoLayerTransformButtonLegacy")) != nullptr ||
        find_named_object(edit_workspace, QStringLiteral("photoLayerRowSoloButtonLegacy")) != nullptr ||
        find_named_object(edit_workspace, QStringLiteral("photoLayerLockButtonLegacy")) != nullptr ||
        find_named_object(edit_workspace, QStringLiteral("photoLayerMoveUpButtonLegacy")) != nullptr ||
        find_named_object(edit_workspace, QStringLiteral("photoLayerMoveDownButtonLegacy")) != nullptr ||
        find_named_object(edit_workspace, QStringLiteral("photoLayerDuplicateButtonLegacy")) != nullptr ||
        find_named_object(edit_workspace, QStringLiteral("photoLayerDeleteButtonLegacy")) != nullptr) {
      return fail("photo layer row hover should not instantiate legacy precision or structure controls");
    }
    trace_smoke_progress("layer hover lightweight row checked");
        if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER")) {
          app_shell.completeFirstRunExperience();
          auto* pointer_guide = find_named_object(root, QStringLiteral("firstRunGuideDialog"));
          if (pointer_guide != nullptr && (!QMetaObject::invokeMethod(pointer_guide, "close") ||
              !wait_for_condition([&]() { return !pointer_guide->property("visible").toBool(); })))
            return fail("onboarding must be closed before native mask pointer checks");

          const auto pointer_photo = runtime_root.filePath(QStringLiteral("mask-pointer-photo.png"));
          const auto pointer_fixture_kind =
              qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_REFINE_CANCEL")
                  ? QStringLiteral("saturated")
                  : QStringLiteral("saturated-small");
          edit_workspace->setProperty("agentFocusedMaskId", QStringLiteral("previous-photo-mask"));
          edit_workspace->setProperty("agentSpatialTargetMode", true);
          if (!write_overlay_visual_fixture(pointer_photo, pointer_fixture_kind))
            return fail("mask pointer proof must create a decodable image");
          editor_preview.loadAsset(QStringLiteral("mask-pointer-photo"), pointer_photo,
                                   QUrl::fromLocalFile(pointer_photo).toString(),
                                   QStringLiteral("Pointer fixture"), {}, {}, {});
          if (!edit_workspace->property("agentFocusedMaskId").toString().isEmpty() ||
              edit_workspace->property("agentSpatialTargetMode").toBool())
            return fail("switching photos must clear previous agent mask focus and point targeting");
          if (!wait_for_condition([&]() { return !editor_preview.processing() &&
                  !editor_preview.displayPreviewSource().isEmpty(); }, 10000))
            return fail("mask pointer fixture must finish its initial preview render");

          const auto pointer_kind = qEnvironmentVariable("PHOTO_EDITOR_MASK_POINTER_KIND", QStringLiteral("radial"));
          const auto first_kind = pointer_kind == QStringLiteral("compound") ? QStringLiteral("radial") : pointer_kind;
          if (pointer_kind == QStringLiteral("generated")) {
            qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
            if (!QMetaObject::invokeMethod(edit_workspace, "createGeneratedSelectionSource", Q_ARG(QVariant, QVariant(QStringLiteral("subject")))) ||
                !editor_preview.hasActiveMask() ||
                editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value(QStringLiteral("segmentationData")).toString().isEmpty())
              return fail("generated pointer fixture must contain segmentation alpha data");
            std::cout << "generated-mask-provider=test-stub; segmentation-quality-proof=false\n";
          } else if (!QMetaObject::invokeMethod(edit_workspace, "createMaskSource", Q_ARG(QVariant, QVariant(first_kind))))
            return fail("pointer fixture must create its requested mask");
          if (pointer_kind == QStringLiteral("luminance")) {
            editor_preview.setActiveMaskLuminanceMin(0.2);
            editor_preview.setActiveMaskLuminanceMax(0.65);
            editor_preview.setActiveMaskRangeFeather(0.08);
            if (std::abs(editor_preview.activeMaskLuminanceMin() - 0.2) > 1e-6 ||
                std::abs(editor_preview.activeMaskLuminanceMax() - 0.65) > 1e-6)
              return fail("range refinement fixture must retain nondefault luminance bounds");
          }
          if (pointer_kind == QStringLiteral("compound")) {
            editor_preview.addMask(QStringLiteral("linear"));
            const auto sources = editor_preview.maskStack();
            if (sources.size() != 2 || editor_preview.activeMaskIndex() != 1 ||
                sources.at(0).toMap().value(QStringLiteral("maskSetId")) != sources.at(1).toMap().value(QStringLiteral("maskSetId")))
              return fail("compound pointer fixture must have two sources in one mask set");
          }
          if (pointer_kind == QStringLiteral("brush")) {
            editor_preview.beginActiveBrushStroke();
            editor_preview.addBrushPoint(0.3, 0.4);
            editor_preview.addBrushPoint(0.45, 0.5);
            editor_preview.addBrushPoint(0.6, 0.6);
            editor_preview.endActiveBrushStroke();
            if (!wait_for_condition([&]() { return !editor_preview.processing() &&
                    editor_preview.activeMaskCommitted(); }, 10000))
              return fail("brush pointer fixture must finish committing its painted stroke");
          }
          const auto expected_component_id = editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value(QStringLiteral("id")).toString();
          if (qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_REFINE_CANCEL")) {
            const auto original_masks = editor_preview.maskStack();
            const auto original_history = editor_preview.editHistoryEntries();
            editor_preview.beginCoalescedEdit();
            editor_preview.startSelectionEdgeBrush(false);
            editor_preview.beginActiveBrushStroke();
            // Make the worker long enough for this test to cancel it reliably
            // even while neighboring UI shards are loading the machine.
            for (int sweep = 0; sweep < 16; ++sweep) {
              const auto y = 0.12 + 0.76 * static_cast<double>(sweep) / 15.0;
              for (int step = 0; step <= 32; ++step) {
                const auto progress = static_cast<double>(step) / 32.0;
                const auto x = sweep % 2 == 0
                                   ? 0.1 + 0.8 * progress
                                   : 0.9 - 0.8 * progress;
                editor_preview.addBrushPoint(x, y);
              }
            }
            editor_preview.endActiveBrushStroke();
            QElapsedTimer worker_start_wait;
            worker_start_wait.start();
            bool brush_worker_observed = false;
            while (worker_start_wait.elapsed() < 10000) {
              QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
              if (photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_running(
                      editor_preview)) {
                brush_worker_observed = true;
                break;
              }
              QThread::msleep(1);
            }
            if (!brush_worker_observed) {
              std::cerr << "brush-refine cancellation fixture: kind="
                        << editor_preview.activeMaskKind().toStdString()
                        << " active=" << editor_preview.hasActiveMask()
                        << " stroke=" << editor_preview.brushStrokeActive()
                        << " committed=" << editor_preview.activeMaskCommitted()
                        << " processing=" << editor_preview.processing()
                        << " worker_active="
                        << photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_active(
                               editor_preview)
                        << " worker_running="
                        << photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_running(
                               editor_preview)
                        << " commit_scheduled="
                        << photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_scheduled(
                               editor_preview)
                        << " pending_commits="
                        << photo_editor::desktop::EditorPreviewControllerTestAccess::pending_brush_commit_count(
                               editor_preview)
                        << " status=" << editor_preview.status().toStdString() << '\n';
              return fail("rollback race fixture must observe an active brush raster worker");
            }
            if (!editor_preview.cancelCoalescedEdit()) return fail("refinement transaction must cancel");
            if (!wait_for_condition([&]() {
                  return !photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_running(editor_preview) &&
                         !editor_preview.processing();
                }, 10000)) return fail("canceled brush worker must finish without reviving the edit");
            process_layout_events();
            if (editor_preview.maskStack() != original_masks || editor_preview.editHistoryEntries() != original_history ||
                editor_preview.brushStrokeActive())
              return fail("late brush completion must preserve the canceled refinement's original state");
            std::cout << "mask-refine-cancel-active-worker=passed\n";
          }
          edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
          for (int iteration = 0; iteration < 8; ++iteration) QCoreApplication::processEvents();
          const auto visible_rect = [](QQuickItem* item) {
            if (!item_visible(item) || item->window() == nullptr) return QRectF{};
            auto rect = scene_rect(item).intersected(QRectF(0, 0, item->window()->width(), item->window()->height()));
            for (auto* parent = item->parentItem(); parent != nullptr; parent = parent->parentItem()) {
              if (!parent->isVisible()) return QRectF{};
              if (parent->clip()) rect = rect.intersected(scene_rect(parent));
            }
            return rect;
          };
          qint64 last_dispatch_ns = 0;
          const auto click_item = [&](QQuickItem* item) {
            if (item == nullptr || !item->isVisible() || item->window() == nullptr) return false;
            auto* window = item->window();
            const auto rect = visible_rect(item);
            if (rect.width() < 2 || rect.height() < 2) return false;
            const auto point = rect.center();
            const auto global = QPointF(window->mapToGlobal(point.toPoint()));
            QElapsedTimer dispatch_timer;
            dispatch_timer.start();
            QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &press);
            if (qEnvironmentVariableIsSet("PHOTO_EDITOR_REFINEMENT_RAPID_PAGES") && item->objectName().startsWith(QStringLiteral("maskRefinement"))) {
              auto* grabber = window->mouseGrabberItem();
              std::cerr << "pointer=" << item->objectName().toStdString() << " enabled=" << item->isEnabled()
                        << " pressed=" << item->property("pressed").toBool() << " grabber="
                        << (grabber ? grabber->objectName().toStdString() : "none") << '\n';
            }
            QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &release);
            last_dispatch_ns = dispatch_timer.nsecsElapsed();
            QCoreApplication::processEvents();
            return true;
          };
          const auto find_thumbnail = [&]() -> QQuickItem* {
            for (const auto* name : {"photoLayerMaskSetThumbnailTargetArea", "photoLayerNestedMaskThumbnailTargetArea", "activeLayerMaskChildThumbnailTargetArea"}) {
              for (auto* candidate : find_named_items(edit_workspace, QString::fromLatin1(name))) {
                const auto rect = visible_rect(candidate);
                if (rect.width() >= 2 && rect.height() >= 2) return candidate;
              }
            }
            return nullptr;
          };
          QPointer<QQuickItem> thumbnail = find_thumbnail();
          if (!click_item(thumbnail) || !editor_preview.hasActiveMask())
            return fail("native mask thumbnail must be visible and selectable by pointer");
          const auto selected_id = editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value(QStringLiteral("id")).toString();
          if (selected_id != expected_component_id)
            return fail("first thumbnail click must retain the last edited component, not select the first source");
          const auto hidden_mask_case = qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_POINTER_HIDDEN");
          if (qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_REFINE_UI_CANCEL")) {
            const auto before_refine = editor_preview.maskStack();
            const QImage alpha_source(pointer_photo);
            const auto evaluate_refinement_alpha = [&]() {
              return photo_editor::desktop::evaluate_canonical_composite_mask_alpha(
                  alpha_source, editor_preview.maskStack(), 1.0, {1.0, 1.0, 1.0});
            };
            const auto original_alpha = evaluate_refinement_alpha();
            if (!original_alpha.valid || original_alpha.alpha_mask.isEmpty())
              return fail("refinement proof must evaluate the original matte");
            auto before_history = editor_preview.editHistoryEntries();
            const auto before_refine_history_index = editor_preview.editHistoryCurrentIndex();
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefineViewSettingsButton"))) ||
                edit_workspace->property("maskRefinementPage").toString() != QStringLiteral("view") ||
                editor_preview.maskStack() != before_refine || editor_preview.editHistoryEntries() != before_history)
              return fail("direct View must open without changing the existing mask or history");
            process_layout_events();
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskFirstHiddenButton"))) ||
                editor_preview.maskPreviewDisplayMode() != QStringLiteral("hidden") ||
                !click_item(find_named_item(edit_workspace, QStringLiteral("maskCreationDoneButton"))) ||
                editor_preview.maskStack() != before_refine || editor_preview.editHistoryEntries() != before_history)
              return fail("view-only refinement must finish without a recipe history operation");
            edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
            process_layout_events();
            if (!click_item(find_thumbnail())) return fail("view-only refinement must preserve reselection");
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefineEdgeSettingsButton"))))
              return fail("geometric mask must open Edge before explicit brush addition");
            process_layout_events();
            auto* add_brush_page = find_named_item(edit_workspace, QStringLiteral("maskRefinementPaintPageButton"));
            if (add_brush_page == nullptr || add_brush_page->property("text").toString() != QStringLiteral("Add Brush") ||
                editor_preview.maskStack() != before_refine || !click_item(add_brush_page) ||
                editor_preview.activeMaskKind() != QStringLiteral("brush") || editor_preview.maskStack().size() != before_refine.size() + 1)
              return fail("geometric Paint entry must explicitly add one brush component");
            process_layout_events();
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefinementCancelButton"))) ||
                editor_preview.maskStack() != before_refine || editor_preview.editHistoryEntries() != before_history)
              return fail("Cancel must discard an explicitly added brush component");
            edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
            process_layout_events();
            if (!click_item(find_thumbnail())) return fail("explicit brush cancellation must preserve reselection");
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefineEdgeSettingsButton"))) ||
                !edit_workspace->property("maskRefinementSessionActive").toBool() ||
                edit_workspace->property("maskRefinementPage").toString() != QStringLiteral("edge") ||
                editor_preview.maskStack() != before_refine || editor_preview.editHistoryEntries() != before_history)
              return fail("opening edge settings must retain the existing selection without recipe or history changes");
            editor_preview.setActiveMaskFeather(0.12);
            if (editor_preview.maskStack() == before_refine || editor_preview.maskStack().size() != before_refine.size())
              return fail("direct edge editing must change the existing component without adding another");
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefinementCancelButton"))) ||
                editor_preview.maskStack() != before_refine || editor_preview.editHistoryEntries() != before_history)
              return fail("canceling edited edge refinement must restore the existing selection");
            const auto canceled_edge_alpha = evaluate_refinement_alpha();
            if (!canceled_edge_alpha.valid ||
                canceled_edge_alpha.alpha_mask != original_alpha.alpha_mask)
              return fail("Cancel must restore every evaluated alpha byte after editing the existing edge");
            edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
            process_layout_events();
            if (!click_item(find_thumbnail())) return fail("edge refinement must leave the selection reselectable");
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefineEdgeSettingsButton"))))
              return fail("direct edge editing must reopen for commit");
            editor_preview.setActiveMaskFeather(0.18);
            editor_preview.setActiveMaskSelectionSmooth(23.0);
            const auto completed_edge = editor_preview.maskStack();
            const auto completed_edge_alpha = evaluate_refinement_alpha();
            if (!completed_edge_alpha.valid || completed_edge_alpha.alpha_mask == original_alpha.alpha_mask)
              return fail("edge edits must change evaluated alpha, not just recipe values");
            if (completed_edge == before_refine || completed_edge.size() != before_refine.size() ||
                !click_item(find_named_item(edit_workspace, QStringLiteral("maskCreationDoneButton"))))
              return fail("Done must accept an existing-component edge edit");
            if (!wait_for_condition([&]() { return !photo_editor::desktop::EditorPreviewControllerTestAccess::edit_transaction_active(editor_preview); }, 10000) ||
                editor_preview.editHistoryEntries().size() != before_history.size() + 1)
              return fail("direct edge Done must commit one history operation");
            editor_preview.undo();
            if (editor_preview.maskStack() != before_refine) return fail("direct edge undo must restore the original matte recipe");
            const auto undone_edge_alpha = evaluate_refinement_alpha();
            if (!undone_edge_alpha.valid || undone_edge_alpha.alpha_mask != original_alpha.alpha_mask)
              return fail("direct edge undo must restore every evaluated alpha byte");
            editor_preview.redo();
            if (editor_preview.maskStack() != completed_edge) return fail("direct edge redo must restore the completed matte recipe");
            const auto redone_edge_alpha = evaluate_refinement_alpha();
            if (!redone_edge_alpha.valid || redone_edge_alpha.alpha_mask != completed_edge_alpha.alpha_mask)
              return fail("direct edge redo must restore every completed alpha byte");
            editor_preview.undo();
            edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
            process_layout_events();
            if (!click_item(find_thumbnail()) || editor_preview.maskStack() != before_refine)
              return fail("direct edge undo must preserve reselection");
            before_history = editor_preview.editHistoryEntries();
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefineEdgeAddButton"))) ||
                !edit_workspace->property("maskRefinementSessionActive").toBool() || editor_preview.maskStack().size() != before_refine.size() + 1)
              return fail("pointer Add must start a refinement session");
            process_layout_events();
            if (qEnvironmentVariableIsSet("PHOTO_EDITOR_REFINEMENT_PAGES_NARROW")) {
              qobject_cast<QQuickItem*>(edit_workspace)->window()->resize(1280, 900);
              process_layout_events();
            }
            const auto refinement_masks = editor_preview.maskStack();
            for (const auto& page : {QStringLiteral("Edge"), QStringLiteral("Strength"), QStringLiteral("View"), QStringLiteral("Paint")}) {
              if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefinement") + page + QStringLiteral("PageButton"))))
                return fail("refinement pages must be reachable by pointer");
              process_layout_events();
              const auto selected_page = page.toLower();
              for (const auto& control : {std::pair{QStringLiteral("maskFirstBrushSettingsGroup"), QStringLiteral("paint")},
                                          std::pair{QStringLiteral("maskFirstFeatherSlider"), QStringLiteral("edge")},
                                          std::pair{QStringLiteral("maskFirstDensitySlider"), QStringLiteral("strength")},
                                          std::pair{QStringLiteral("maskFirstPreviewModeRow"), QStringLiteral("view")}}) {
                auto* item = find_named_item(edit_workspace, control.first);
                if (item == nullptr || item->isVisible() != (selected_page == control.second))
                  return fail("refinement page must show only its focused controls");
              }
              for (const auto& name : {QStringLiteral("maskRefinementPages"), QStringLiteral("maskRefinementCancelButton")}) {
                auto* item = find_named_item(edit_workspace, name);
                if (item == nullptr || visible_rect(item).width() < item->width() - 1 ||
                    visible_rect(item).height() < item->height() - 1)
                  return fail("refinement navigation and Cancel must remain fully visible");
              }
              if (selected_page == QStringLiteral("paint") || selected_page == QStringLiteral("edge")) {
                const auto primary_name = selected_page == QStringLiteral("paint") ? QStringLiteral("maskFirstBrushSizeSlider") : QStringLiteral("maskFirstFeatherSlider");
                const auto detail_name = selected_page == QStringLiteral("paint") ? QStringLiteral("maskFirstBrushHardnessSlider") : QStringLiteral("maskFirstRefineSmoothSlider");
                auto* primary = find_named_item(edit_workspace, primary_name);
                auto* detail = find_named_item(edit_workspace, detail_name);
                auto* detail_button = find_named_item(edit_workspace, QStringLiteral("maskRefinementDetailPageButton"));
                if (primary == nullptr || detail == nullptr || !primary->isVisible() || detail->isVisible() || !click_item(detail_button))
                  return fail("refinement must start with focused primary settings and expose its detail page");
                process_layout_events();
                if (primary->isVisible() || !detail->isVisible())
                  return fail("refinement detail must replace primary settings");
                QElapsedTimer detail_presentation_timer;
                detail_presentation_timer.start();
                if (!wait_for_condition([&]() { return detail_presentation_timer.elapsed() >= 200; }, 2000))
                  return fail("detail capture must settle after navigation");
                const auto detail_capture = qEnvironmentVariable("PHOTO_EDITOR_REFINEMENT_CAPTURE_PATH");
                if (!detail_capture.isEmpty() && !detail->window()->grabWindow().save(
                        detail_capture + QStringLiteral(".") + selected_page + QStringLiteral("-detail.png")))
                  return fail("refinement detail page must produce a native capture");
                if (!click_item(detail_button)) return fail("refinement detail must provide Back");
                process_layout_events();
                if (!primary->isVisible() || detail->isVisible())
                  return fail("Back must restore focused primary settings");
                if (!click_item(detail_button)) return fail("detail page must reopen");
                process_layout_events();
                if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefinementViewPageButton"))))
                  return fail("detail navigation must allow leaving for View");
                QElapsedTimer primary_presentation_timer;
                primary_presentation_timer.start();
                if (!wait_for_condition([&]() { return primary_presentation_timer.elapsed() >= (qEnvironmentVariableIsSet("PHOTO_EDITOR_REFINEMENT_RAPID_PAGES") ? 0 : 200) &&
                        edit_workspace->property("maskRefinementPage").toString() == QStringLiteral("view"); }, 2000))
                  return fail("View must be presented before the next page-navigation gesture");
                process_layout_events();
                auto* return_page_button = find_named_item(edit_workspace, QStringLiteral("maskRefinement") + page + QStringLiteral("PageButton"));
                const auto return_rect_before = visible_rect(return_page_button);
                if (!click_item(return_page_button))
                  return fail("detail navigation must allow returning to its primary page");
                const auto return_rect_after = visible_rect(return_page_button);
                process_layout_events();
                if (edit_workspace->property("maskRefinementDetailPage").toBool() || !primary->isVisible() || detail->isVisible()) {
                  std::cerr << "returnBefore=" << return_rect_before.x() << ',' << return_rect_before.y() << ',' << return_rect_before.width() << ',' << return_rect_before.height()
                            << " returnAfter=" << return_rect_after.x() << ',' << return_rect_after.y() << ',' << return_rect_after.width() << ',' << return_rect_after.height() << '\n';
                  std::cerr << "page=" << edit_workspace->property("maskRefinementPage").toString().toStdString()
                            << " expected=" << selected_page.toStdString()
                            << " detail=" << edit_workspace->property("maskRefinementDetailPage").toBool()
                            << " primaryVisible=" << primary->isVisible() << " detailVisible=" << detail->isVisible() << '\n';
                  return fail("switching primary pages must reset deeper navigation");
                }
              }
              QElapsedTimer page_presentation_timer;
              page_presentation_timer.start();
              if (!wait_for_condition([&]() { return page_presentation_timer.elapsed() >= 200; }, 2000))
                return fail("refinement page capture must settle after pointer navigation");
              const auto page_capture = qEnvironmentVariable("PHOTO_EDITOR_REFINEMENT_CAPTURE_PATH");
              if (!page_capture.isEmpty() && !qobject_cast<QQuickItem*>(edit_workspace)->window()->grabWindow().save(
                      page_capture + QStringLiteral(".") + selected_page + QStringLiteral(".png")))
                return fail("each refinement page must produce a native capture");
              if (editor_preview.maskStack() != refinement_masks || editor_preview.editHistoryEntries().size() != before_history.size())
                return fail("changing refinement pages must preserve masks and history");
            }
            const auto refinement_capture = qEnvironmentVariable("PHOTO_EDITOR_REFINEMENT_CAPTURE_PATH");
            if (!refinement_capture.isEmpty()) {
              auto* cancel_button = find_named_item(edit_workspace, QStringLiteral("maskRefinementCancelButton"));
              if (cancel_button == nullptr || visible_rect(cancel_button).isEmpty() ||
                  !cancel_button->window()->grabWindow().save(refinement_capture))
                return fail("active refinement controls must be visible in the native capture");
            }
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefinementCancelButton"))) ||
                edit_workspace->property("maskRefinementSessionActive").toBool() || editor_preview.maskStack() != before_refine ||
                editor_preview.editHistoryEntries() != before_history)
              return fail("pointer Cancel must restore the original masks and history");
            const auto canceled_pointer_alpha = evaluate_refinement_alpha();
            if (!canceled_pointer_alpha.valid ||
                canceled_pointer_alpha.alpha_mask != original_alpha.alpha_mask)
              return fail("pointer Cancel must restore every evaluated alpha byte in the rendered mask");
            edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
            process_layout_events();
            if (!click_item(find_thumbnail())) return fail("canceled mask must remain reselectable");
            std::cout << "mask-refine-pointer-cancel=passed\n";
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefineEdgeAddButton"))))
              return fail("Escape fixture must start refinement by pointer");
            for (int stroke = 0; stroke < 2; ++stroke) {
              const auto previous_stroke = editor_preview.maskStack();
              editor_preview.beginActiveBrushStroke();
              editor_preview.addBrushPoint(0.25 + 0.3 * stroke, 0.3);
              editor_preview.addBrushPoint(0.35 + 0.3 * stroke, 0.5);
              editor_preview.endActiveBrushStroke();
              if (!wait_for_condition([&]() { return editor_preview.activeMaskCommitted() && !editor_preview.processing() &&
                      !photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_running(editor_preview); }, 10000) ||
                  editor_preview.maskStack() == previous_stroke)
                return fail("Escape fixture must rasterize each refinement stroke before canceling");
            }
            auto* escape_workspace = qobject_cast<QQuickItem*>(edit_workspace);
            if (escape_workspace == nullptr) return fail("Escape fixture needs a focusable workspace");
            escape_workspace->forceActiveFocus();
            if (!send_window_key(escape_workspace->window(), Qt::Key_Escape, Qt::NoModifier))
              return fail("refinement must accept the Escape key");
            process_layout_events();
            if (edit_workspace->property("maskRefinementSessionActive").toBool() ||
                editor_preview.maskStack() != before_refine || editor_preview.editHistoryEntries() != before_history)
              return fail("Escape must cancel refinement without committing it");
            const auto canceled_brush_alpha = evaluate_refinement_alpha();
            if (!canceled_brush_alpha.valid || canceled_brush_alpha.alpha_mask != original_alpha.alpha_mask)
              return fail("Escape must restore every original alpha byte after committed brush strokes");
            if (!click_item(find_thumbnail())) return fail("Escape must leave the original mask reselectable");
            std::cout << "mask-refine-escape-cancel=passed\n";
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefineEdgeAddButton"))))
              return fail("pointer Add must restart refinement after Cancel");
            editor_preview.beginActiveBrushStroke();
            editor_preview.addBrushPoint(0.3, 0.4);
            editor_preview.addBrushPoint(0.45, 0.5);
            editor_preview.addBrushPoint(0.6, 0.6);
            editor_preview.endActiveBrushStroke();
            if (!wait_for_condition([&]() { return editor_preview.activeMaskCommitted() && !editor_preview.processing() &&
                    !photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_running(editor_preview); }, 10000))
              return fail("first refinement stroke must commit before the next stroke");
            if (editor_preview.editHistoryEntries().size() != before_history.size())
              return fail("intermediate refinement strokes must not publish separate history entries");
            const auto first_stroke_masks = editor_preview.maskStack();
            editor_preview.beginActiveBrushStroke();
            editor_preview.addBrushPoint(0.7, 0.3);
            editor_preview.addBrushPoint(0.75, 0.45);
            editor_preview.addBrushPoint(0.8, 0.6);
            process_layout_events();
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskCreationDoneButton"))))
              return fail("pointer Done must finish refinement with an active stroke");
            if (!wait_for_condition([&]() { return !editor_preview.processing() &&
                    !photo_editor::desktop::EditorPreviewControllerTestAccess::edit_transaction_active(editor_preview) &&
                    !photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_running(editor_preview); }, 10000))
              return fail("Done must allow the final brush commit to finish");
            if (edit_workspace->property("maskRefinementSessionActive").toBool() ||
                editor_preview.maskStack().size() != before_refine.size() + 1 ||
                editor_preview.editHistoryCurrentIndex() != before_refine_history_index + 1 ||
                editor_preview.editHistoryEntries().size() != before_refine_history_index + 2) {
              std::cerr << "refinement active=" << edit_workspace->property("maskRefinementSessionActive").toBool()
                        << " masks=" << editor_preview.maskStack().size() << " history=" << editor_preview.editHistoryEntries().size()
                        << " before=" << before_history.size() << '\n';
              return fail("Done must retain refinement as exactly one history step");
            }
            const auto finished_masks = editor_preview.maskStack();
            const auto finished_brush_alpha = evaluate_refinement_alpha();
            if (!finished_brush_alpha.valid || finished_brush_alpha.alpha_mask == original_alpha.alpha_mask)
              return fail("committed brush refinement must change evaluated matte alpha");
            if (finished_brush_alpha.size != original_alpha.size ||
                finished_brush_alpha.alpha_mask.size() != original_alpha.alpha_mask.size())
              return fail("brush refinement must preserve matte dimensions");
            // Both stroke paths lie in x=[0.3,0.8], y=[0.3,0.6]. A conservative
            // 0.1 image-relative margin includes the brush footprint and feather.
            QRect outside_change_bounds;
            int outside_change_count = 0;
            for (int y = 0; y < original_alpha.size.height(); ++y) {
              for (int x = 0; x < original_alpha.size.width(); ++x) {
                const auto nx = (static_cast<double>(x) + 0.5) / original_alpha.size.width();
                const auto ny = (static_cast<double>(y) + 0.5) / original_alpha.size.height();
                if (nx >= 0.2 && nx <= 0.9 && ny >= 0.2 && ny <= 0.7) continue;
                const auto offset = y * original_alpha.size.width() + x;
                if (finished_brush_alpha.alpha_mask.at(offset) != original_alpha.alpha_mask.at(offset)) {
                  outside_change_bounds = outside_change_bounds.united(QRect(x, y, 1, 1));
                  ++outside_change_count;
                }
              }
            }
            if (outside_change_count > 0) {
              QVariantList brush_only_masks;
              for (const auto& value : finished_masks) {
                const auto mask = value.toMap();
                std::cerr << "mask kind=" << mask.value(QStringLiteral("kind")).toString().toStdString()
                          << " operation=" << mask.value(QStringLiteral("operation")).toString().toStdString()
                          << " parent=" << mask.value(QStringLiteral("parentId")).toString().toStdString()
                          << " set=" << mask.value(QStringLiteral("maskSetId")).toString().toStdString()
                          << " invert=" << mask.value(QStringLiteral("selectionInvert")).toBool()
                          << " maskInvert=" << mask.value(QStringLiteral("invert")).toBool()
                          << " blend=" << mask.value(QStringLiteral("blendMode")).toString().toStdString()
                          << " opacity=" << mask.value(QStringLiteral("opacity")).toDouble()
                          << " density=" << mask.value(QStringLiteral("density")).toDouble()
                          << " expand=" << mask.value(QStringLiteral("selectionExpand")).toDouble()
                          << " grow=" << mask.value(QStringLiteral("selectionGrow")).toDouble()
                          << " shrink=" << mask.value(QStringLiteral("selectionShrink")).toDouble()
                          << " feather=" << mask.value(QStringLiteral("selectionFeather")).toDouble()
                          << " blur=" << mask.value(QStringLiteral("selectionBlur")).toDouble()
                          << " fill=" << mask.value(QStringLiteral("selectionFillHoles")).toDouble()
                          << " remove=" << mask.value(QStringLiteral("selectionRemoveIslands")).toDouble()
                          << '\n';
                if (mask.value(QStringLiteral("kind")).toString() != QStringLiteral("brush")) continue;
                brush_only_masks.append(mask);
                const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
                const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
                const auto height = mask.value(QStringLiteral("segmentationHeight")).toInt();
                QRect raster_bounds;
                if (width > 0 && height > 0 && data.size() == width * height) {
                  for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                      if (data.at(y * width + x) != 0) raster_bounds = raster_bounds.united(QRect(x, y, 1, 1));
                }
                std::cerr << "brush raster=" << width << 'x' << height << " bytes=" << data.size()
                          << " bounds=" << raster_bounds.x() << ',' << raster_bounds.y() << ',' << raster_bounds.width() << ',' << raster_bounds.height()
                          << " size=" << mask.value(QStringLiteral("brushSize")).toDouble() << '\n';
              }
              const auto original_mask_id = before_refine.isEmpty()
                  ? QString()
                  : before_refine.constFirst().toMap().value(QStringLiteral("id")).toString();
              for (const auto& value : finished_masks) {
                const auto after_mask = value.toMap();
                if (after_mask.value(QStringLiteral("id")).toString() != original_mask_id) continue;
                const auto before_mask = before_refine.constFirst().toMap();
                for (auto iterator = before_mask.cbegin(); iterator != before_mask.cend(); ++iterator) {
                  if (after_mask.value(iterator.key()) != iterator.value()) {
                    std::cerr << "parent field changed=" << iterator.key().toStdString()
                              << " from=" << iterator.value().toString().toStdString()
                              << " to=" << after_mask.value(iterator.key()).toString().toStdString()
                              << '\n';
                  }
                }
                break;
              }
              std::cerr << "outside alpha pixels=" << outside_change_count << " bounds="
                        << outside_change_bounds.x() << ',' << outside_change_bounds.y() << ','
                        << outside_change_bounds.width() << ',' << outside_change_bounds.height() << '\n';
              if (!outside_change_bounds.isNull()) {
                const auto x = outside_change_bounds.x();
                const auto y = outside_change_bounds.y();
                const auto offset = y * original_alpha.size.width() + x;
                const auto brush_only = photo_editor::desktop::evaluate_canonical_composite_mask_alpha(
                    alpha_source, brush_only_masks, 1.0, {1.0, 1.0, 1.0});
                const auto brush_only_offset = y * brush_only.size.width() + x;
                std::cerr << "first outside pixel=" << x << ',' << y
                          << " original=" << static_cast<int>(original_alpha.alpha_mask.at(offset))
                          << " finished=" << static_cast<int>(finished_brush_alpha.alpha_mask.at(offset))
                          << " brush-only="
                          << (brush_only.valid && brush_only_offset < brush_only.alpha_mask.size()
                                  ? static_cast<int>(brush_only.alpha_mask.at(brush_only_offset))
                                  : -1)
                          << '\n';
              }
              return fail("local brush strokes must preserve alpha outside the conservative stroke region");
            }
            if (finished_masks == first_stroke_masks)
              return fail("second refinement stroke must change the committed mask");
            editor_preview.undo();
            if (editor_preview.maskStack() != before_refine)
              return fail("one undo must remove the complete refinement");
            const auto undone_brush_alpha = evaluate_refinement_alpha();
            if (!undone_brush_alpha.valid || undone_brush_alpha.alpha_mask != original_alpha.alpha_mask)
              return fail("brush undo must restore every original alpha byte");
            editor_preview.redo();
            if (editor_preview.maskStack() != finished_masks)
              return fail("one redo must restore both refinement strokes exactly");
            const auto redone_brush_alpha = evaluate_refinement_alpha();
            if (!redone_brush_alpha.valid || redone_brush_alpha.alpha_mask != finished_brush_alpha.alpha_mask)
              return fail("brush redo must restore every completed alpha byte");
            editor_preview.undo();
            if (editor_preview.maskStack() != before_refine)
              return fail("repeated undo must restore the original mask");
            process_layout_events();
            if (!click_item(find_thumbnail())) return fail("undone refinement must restore the original selectable mask");
            std::cout << "mask-refine-pointer-done-one-undo=passed\n";
          }
          const auto locked_mask_case = qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_POINTER_LOCKED");
          const auto renamed_layer_case = qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_POINTER_RENAMED_LAYER");
          if (renamed_layer_case) {
            if (editor_preview.photoLayerCount() != 1 ||
                !editor_preview.renamePhotoLayer(0, QStringLiteral("Renamed mask owner")))
              return fail("rename fixture must rename the mask's owning layer");
            process_layout_events();
          }
          const auto layer_identities = [&]() {
            QVariantList result;
            for (const auto& entry : editor_preview.photoLayers()) {
              const auto layer = entry.toMap();
              result.append(QVariantMap{{QStringLiteral("id"), layer.value(QStringLiteral("id"))},
                                        {QStringLiteral("name"), layer.value(QStringLiteral("name"))}});
            }
            return result;
          };
          const auto layers_before_selection = layer_identities();
          if (locked_mask_case) editor_preview.setActiveMaskLocked(true);
          if (hidden_mask_case) {
            editor_preview.setActiveMaskEnabled(false);
            editor_preview.setMaskPreviewMode(QStringLiteral("none"));
            process_layout_events();
          }
          const auto masks_before_selection = editor_preview.maskStack();
          const auto history_before_selection = editor_preview.editHistoryEntries();
          const auto history_index_before_selection = editor_preview.editHistoryCurrentIndex();
          qint64 maximum_selection_ms = 0;
          qint64 maximum_dispatch_ns = 0;
          const bool layer_click_away = qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_POINTER_LAYER_AWAY");
          const bool collapsed_group_case = qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_POINTER_COLLAPSED_GROUP");
          for (int cycle = 0; cycle < 20; ++cycle) {
            auto* base = find_named_item(edit_workspace, layer_click_away
                ? QStringLiteral("photoLayerNameLabel") : QStringLiteral("baseImageLayerRowTargetArea"));
            if (!click_item(base)) return fail("click-away row must be visible for pointer selection");
            QVariant base_scope;
            if (!QMetaObject::invokeMethod(edit_workspace, "selectedEditTargetScope", Q_RETURN_ARG(QVariant, base_scope)) ||
                (layer_click_away ? base_scope.toString() != QStringLiteral("adjustment-layer")
                                  : base_scope.toString() != QStringLiteral("base-image")))
              return fail("pointer click-away must actually select the requested layer target");
            if (collapsed_group_case) {
              auto* toggle = find_named_item(edit_workspace, QStringLiteral("photoLayerNestedMaskSectionToggle"));
              if (!click_item(toggle)) return fail("linked-mask group must be collapsible by pointer");
              process_layout_events();
              toggle = find_named_item(edit_workspace, QStringLiteral("photoLayerNestedMaskSectionToggle"));
              if (toggle == nullptr || toggle->property("active").toBool())
                return fail("pointer collapse must hide the nested mask group");
              if (!click_item(toggle)) return fail("collapsed linked-mask group must reopen by pointer");
              process_layout_events();
              toggle = find_named_item(edit_workspace, QStringLiteral("photoLayerNestedMaskSectionToggle"));
              if (toggle == nullptr || !toggle->property("active").toBool())
                return fail("pointer expand must restore the nested mask group");
            }
            thumbnail = find_thumbnail();
            QElapsedTimer selection_timer;
            selection_timer.start();
            const auto click_dispatched = click_item(thumbnail);
            const auto selection_restored = click_dispatched && wait_for_condition([&]() {
              return editor_preview.hasActiveMask() &&
                  editor_preview.activeMaskIndex() >= 0 &&
                  editor_preview.activeMaskIndex() < editor_preview.maskStack().size() &&
                  editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap()
                      .value(QStringLiteral("id")).toString() == selected_id;
            }, 1000);
            if (!selection_restored) {
              const auto actual_id = editor_preview.hasActiveMask()
                  ? editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap()
                        .value(QStringLiteral("id")).toString()
                  : QString{};
              std::cerr << "cycle=" << cycle << " thumbnail=" << (thumbnail ? thumbnail->objectName().toStdString() : "destroyed")
                        << " visible=" << (thumbnail && thumbnail->isVisible()) << " active=" << editor_preview.activeMaskIndex()
                        << " click_dispatched=" << click_dispatched
                        << " expected_id=" << selected_id.toStdString() << " actual_id=" << actual_id.toStdString()
                        << " mask_count=" << editor_preview.maskStack().size() << '\n';
              const auto failure_capture = qEnvironmentVariable("PHOTO_EDITOR_MASK_POINTER_CAPTURE_PATH");
              auto* workspace_item = qobject_cast<QQuickItem*>(edit_workspace);
              if (!failure_capture.isEmpty() && workspace_item != nullptr && workspace_item->window() != nullptr)
                workspace_item->window()->grabWindow().save(failure_capture);
              return fail("native pointer reselection changed the selected component identity");
            }
            maximum_selection_ms = std::max(maximum_selection_ms, selection_timer.elapsed());
            maximum_dispatch_ns = std::max(maximum_dispatch_ns, last_dispatch_ns);
          }
          std::cout << "mask pointer cycles=20 component=" << selected_id.toStdString()
                    << " kind=" << pointer_kind.toStdString()
                    << " maximum-controller-selection-ms=" << maximum_selection_ms
                    << " maximum-event-dispatch-ms=" << static_cast<double>(maximum_dispatch_ns) / 1000000.0 << '\n';
          if (editor_preview.maskStack() != masks_before_selection ||
              layer_identities() != layers_before_selection ||
              editor_preview.editHistoryEntries() != history_before_selection ||
              editor_preview.editHistoryCurrentIndex() != history_index_before_selection)
            return fail("pointer selection alone must preserve mask data and edit history");
          if (hidden_mask_case && (editor_preview.activeMaskEnabled() ||
              editor_preview.maskPreviewMode() != QStringLiteral("none")))
            return fail("reselecting a hidden mask must preserve its disabled effect and overlay preference");
          if (!editor_preview.previewFailureStage().isEmpty())
            return fail("mask pointer fixture must render without a preview failure");
          if (qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_LASSO_SWITCH")) {
            auto* viewport = find_named_item(edit_workspace, QStringLiteral("editImageViewport"));
            if (!viewport || !viewport->window()) return fail("selection gestures need the actual Studio viewport");
            const auto send_pointer = [&](QEvent::Type type, double nx, double ny) {
              const QPointF local(viewport->property("paintedContentX").toDouble() + nx * viewport->property("paintedContentWidth").toDouble(),
                  viewport->property("paintedContentY").toDouble() + ny * viewport->property("paintedContentHeight").toDouble());
              const auto scene = viewport->mapToScene(local);
              const auto global = QPointF(viewport->window()->mapToGlobal(scene.toPoint()));
              QMouseEvent event(type, scene, global, type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                  type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
              QCoreApplication::sendEvent(viewport->window(), &event);
              QCoreApplication::processEvents();
            };
            auto lasso_cycles = qEnvironmentVariableIntValue("PHOTO_EDITOR_MASK_LASSO_CYCLES");
            if (lasso_cycles <= 0) lasso_cycles = 8;
            if (lasso_cycles > 100) return fail("selection-switch stress cycles must be between 1 and 100");
            for (int cycle = 0; cycle < lasso_cycles; ++cycle) {
              QElapsedTimer cycle_timer;
              cycle_timer.start();
              if (!QMetaObject::invokeMethod(edit_workspace, "startMaskBrushSource", Q_ARG(QVariant, QVariant("add"))))
                return fail("smart selection must accept brush extension");
              const auto brush_id = editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value("id").toString();
              send_pointer(QEvent::MouseButtonPress, 0.3, 0.35);
              send_pointer(QEvent::MouseMove, 0.45, 0.45);
              send_pointer(QEvent::MouseButtonRelease, 0.5, 0.45);
              if (editor_preview.brushStrokeActive()) return fail("released selection brush must finish");
              if (!wait_for_condition([&]() {
                    return !photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_running(editor_preview) &&
                           !editor_preview.processing();
                  }, 10000))
                return fail("brush refinement must settle before the selection can be reselected");
              edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
              process_layout_events();
              if (!click_item(find_named_item(edit_workspace, QStringLiteral("baseImageLayerRowTargetArea"))))
                return fail("extended selection must survive clicking away to the base image");
              QPointer<QQuickItem> extension_thumbnail;
              const auto extension_thumbnail_ready = wait_for_condition([&]() {
                extension_thumbnail = find_thumbnail();
                return !extension_thumbnail.isNull();
              }, 1000);
              if (!extension_thumbnail_ready)
                return fail("committed brush selection must remain visible in the layer panel");
              const auto extension_click_dispatched = click_item(extension_thumbnail);
              const auto extension_reselected = extension_click_dispatched && wait_for_condition([&]() {
                return editor_preview.hasActiveMask() &&
                    editor_preview.activeMaskIndex() >= 0 &&
                    editor_preview.activeMaskIndex() < editor_preview.maskStack().size() &&
                    editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap()
                        .value(QStringLiteral("id")).toString() == brush_id;
              }, 1000);
              if (!extension_reselected) {
                const auto actual_id = editor_preview.hasActiveMask() &&
                        editor_preview.activeMaskIndex() >= 0 &&
                        editor_preview.activeMaskIndex() < editor_preview.maskStack().size()
                    ? editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap()
                          .value(QStringLiteral("id")).toString()
                    : QString{};
                std::cerr << "lasso-cycle=" << cycle << " extension-thumbnail="
                          << (extension_thumbnail ? extension_thumbnail->objectName().toStdString() : "destroyed")
                          << " click_dispatched=" << extension_click_dispatched
                          << " active=" << editor_preview.activeMaskIndex()
                          << " expected_id=" << selected_id.toStdString()
                          << " actual_id=" << actual_id.toStdString()
                          << " mask_count=" << editor_preview.maskStack().size() << '\n';
                for (const auto* target_name : {"photoLayerMaskSetThumbnailTargetArea",
                                                "photoLayerNestedMaskThumbnailTargetArea",
                                                "activeLayerMaskChildThumbnailTargetArea"}) {
                  const auto targets = find_named_items(edit_workspace, QString::fromLatin1(target_name));
                  std::cerr << "target=" << target_name << " count=" << targets.size();
                  for (auto* target : targets) {
                    const auto rect = visible_rect(target);
                    std::cerr << " [visible=" << target->isVisible() << " rect="
                              << rect.x() << ',' << rect.y() << ',' << rect.width() << ',' << rect.height() << ']';
                  }
                  std::cerr << '\n';
                }
                for (const auto& mask_value : editor_preview.maskStack()) {
                  const auto mask = mask_value.toMap();
                  std::cerr << "mask id=" << mask.value(QStringLiteral("id")).toString().toStdString()
                            << " kind=" << mask.value(QStringLiteral("kind")).toString().toStdString()
                            << " set=" << mask.value(QStringLiteral("maskSetId")).toString().toStdString()
                            << " name=" << mask.value(QStringLiteral("name")).toString().toStdString() << '\n';
                }
                const auto failure_capture = qEnvironmentVariable("PHOTO_EDITOR_MASK_POINTER_CAPTURE_PATH");
                auto* workspace_item = qobject_cast<QQuickItem*>(edit_workspace);
                if (!failure_capture.isEmpty() && workspace_item != nullptr && workspace_item->window() != nullptr)
                  workspace_item->window()->grabWindow().save(failure_capture);
                return fail("extended selection must survive clicking away and reselecting its stable component");
              }
              if (!QMetaObject::invokeMethod(edit_workspace, "startMaskLassoFromShortcut") ||
                  !edit_workspace->property("lassoDrawArmed").toBool()) return fail("lasso must arm after brush reselection");
              process_layout_events();
              send_pointer(QEvent::MouseButtonPress, 0.65, 0.5);
              for (int step = 1; step <= 48; ++step) {
                const auto angle = step * 6.283185307179586 / 48;
                send_pointer(QEvent::MouseMove, 0.5 + 0.15 * std::cos(angle), 0.5 + 0.15 * std::sin(angle));
              }
              send_pointer(QEvent::MouseButtonRelease, 0.65, 0.5);
              if (!editor_preview.hasActiveMask() || editor_preview.activeMaskKind() != "lasso" ||
                  editor_preview.activeMaskPolygonPointCount() < 24 ||
                  !editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value("lassoClosed").toBool() ||
                  edit_workspace->property("lassoDrawArmed").toBool() ||
                  edit_workspace->property("maskShapeEditTransactionActive").toBool())
                return fail("native lasso drag must retain detail, close, and release its undo transaction");
              if (!wait_for_condition([&]() {
                    for (const auto& value : editor_preview.maskStack()) {
                      const auto mask = value.toMap();
                      if (mask.value("id").toString() == brush_id)
                        return !mask.value("brushStrokes").toList().isEmpty();
                    }
                    return false;
                  }, 10000)) return fail("switching to lasso must preserve the released brush extension");
              const auto closed = editor_preview.maskStack();
              send_pointer(QEvent::MouseButtonPress, 0.3, 0.5);
              send_pointer(QEvent::MouseMove, 0.4, 0.6);
              send_pointer(QEvent::MouseButtonRelease, 0.4, 0.6);
              if (editor_preview.maskStack() != closed) return fail("completed lasso must ignore stray drags");
              // Keep stress iterations independent: first prove the accepted
              // brush/lasso edits, then undo both so contour complexity does
              // not grow with the iteration count.
              if (cycle + 1 < lasso_cycles) {
                int undo_count = 0;
                while (editor_preview.editHistoryCurrentIndex() > history_index_before_selection && undo_count < 10) {
                  editor_preview.undo();
                  ++undo_count;
                }
                process_layout_events();
                if (undo_count == 0 || editor_preview.editHistoryCurrentIndex() != history_index_before_selection ||
                    editor_preview.maskStack() != masks_before_selection ||
                    !editor_preview.hasActiveMask() ||
                    editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap()
                        .value(QStringLiteral("id")).toString() != selected_id)
                  return fail("stress iteration undo must restore the original selected mask exactly");
              }
              std::cout << "selection-switch-cycle=" << (cycle + 1)
                        << " elapsed-ms=" << cycle_timer.elapsed() << std::endl;
            }
            const auto retained_lasso_index = editor_preview.activeMaskIndex();
            editor_preview.setActiveMaskIndex(-1);
            if (!QMetaObject::invokeMethod(edit_workspace, "createMaskSource", Q_ARG(QVariant, QVariant("object"))))
              return fail("Smart Select must arm without an existing active mask");
            send_pointer(QEvent::MouseButtonPress, 0.3, 0.4);
            if (!editor_preview.smartSelectStrokeActive())
              return fail("Smart Select must receive its first press without an active mask");
            const auto initial_prompt = editor_preview.smartSelectStrokePromptPayload();
            send_pointer(QEvent::MouseMove, 0.6, 0.6);
            if (editor_preview.smartSelectStrokePromptPayload() == initial_prompt)
              return fail("Smart Select must collect drag input before a mask exists");
            editor_preview.resetSmartSelectStroke();
            viewport->setProperty("maskGestureKind", QString());
            send_pointer(QEvent::MouseButtonRelease, 0.6, 0.6);
            QMetaObject::invokeMethod(edit_workspace, "clearTransientMaskTools");
            editor_preview.setActiveMaskIndex(retained_lasso_index);
            process_layout_events();
            auto* lasso_hud = find_named_item(edit_workspace, QStringLiteral("polygonToolHud"));
            auto* selection_chip = find_named_item(edit_workspace, QStringLiteral("activeCanvasToolChip"));
            if (!lasso_hud || !selection_chip || scene_rect(lasso_hud).intersects(scene_rect(selection_chip)))
              return fail("lasso actions and selection completion controls must not overlap");
            const auto lasso_capture = qEnvironmentVariable("PHOTO_EDITOR_MASK_LASSO_CAPTURE_PATH");
            if (!lasso_capture.isEmpty() && !viewport->window()->grabWindow().save(lasso_capture))
              return fail("selection switch proof must save its visible canvas");
            std::cout << "smart-selection-brush-reselection-lasso-pointer-cycles=" << lasso_cycles
                      << "; lasso-detail=passed\n";
            return 0;
          }
          if (pointer_kind == QStringLiteral("compound")) {
            const auto reselect_at_index = [&](int expected_index) {
              process_layout_events();
              if (editor_preview.maskStack().at(expected_index).toMap().value(QStringLiteral("id")).toString() != selected_id)
                return false;
              if (!click_item(find_named_item(edit_workspace, QStringLiteral("baseImageLayerRowTargetArea"))))
                return false;
              return click_item(find_thumbnail()) && editor_preview.activeMaskIndex() == expected_index &&
                     editor_preview.maskStack().at(expected_index).toMap().value(QStringLiteral("id")).toString() == selected_id;
            };
            editor_preview.moveActiveMask(-1);
            if (!reselect_at_index(0)) return fail("reordered component must reselect by stable ID");
            editor_preview.undo();
            if (!reselect_at_index(1)) return fail("undo must restore component order and stable reselection");
            editor_preview.redo();
            if (!reselect_at_index(0)) return fail("redo must preserve stable component reselection");
            std::cout << "mask-pointer-component-reorder-undo-redo=passed\n";
          }
          if (qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_POINTER_ASSET_RETURN")) {
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("baseImageLayerRowTargetArea"))))
              return fail("asset-return fixture must deselect the mask by pointer");
            QFile saved_recipe(runtime_root.filePath(QStringLiteral("mask-pointer-recipe.json")));
            const auto recipe = photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview).toUtf8();
            if (!saved_recipe.open(QIODevice::WriteOnly) || saved_recipe.write(recipe) != recipe.size())
              return fail("asset-return fixture must save its recipe");
            saved_recipe.close();
            if (!saved_recipe.open(QIODevice::ReadOnly)) return fail("saved mask recipe must reopen");
            const auto restored_recipe = QString::fromUtf8(saved_recipe.readAll());
            saved_recipe.close();
            editor_preview.loadAsset(QStringLiteral("mask-pointer-other-photo"), pointer_photo,
                                     QUrl::fromLocalFile(pointer_photo).toString(), {}, {}, {}, restored_recipe);
            if (!wait_for_condition([&]() { return !editor_preview.processing(); }, 10000) ||
                editor_preview.maskStack().size() != 2)
              return fail("second photo must load the same compound mask IDs");
            editor_preview.setActiveMaskIndex(1);
            const auto other_component_id = editor_preview.maskStack().at(1).toMap().value(QStringLiteral("id")).toString();
            if (other_component_id == selected_id)
              return fail("asset-isolation fixture must choose different components on each photo");
            process_layout_events();
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("baseImageLayerRowTargetArea"))) ||
                !click_item(find_thumbnail()) || editor_preview.activeMaskIndex() != 1)
              return fail("second photo must reselect its own component through its thumbnail");
            editor_preview.loadAsset(QStringLiteral("mask-pointer-photo"), pointer_photo,
                                     QUrl::fromLocalFile(pointer_photo).toString(), {}, {}, {}, restored_recipe);
            if (!wait_for_condition([&]() { return !editor_preview.processing(); }, 10000))
              return fail("returned photo must finish rendering");
            thumbnail = find_thumbnail();
            if (!click_item(thumbnail) || !editor_preview.hasActiveMask() ||
                editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value(QStringLiteral("id")).toString() != selected_id)
              return fail("pointer reselection after asset return must retain the last component");
            std::cout << "mask-pointer-asset-return=passed; recipe-roundtrip=disk; colliding-asset-mask-ids=passed\n";
          }
          if (locked_mask_case) {
            const auto locked_stack = editor_preview.maskStack();
            const auto locked_history = editor_preview.editHistoryEntries();
            editor_preview.setActiveMaskDensity(0.31);
            if (!editor_preview.activeMaskLocked() || editor_preview.activeMaskEditDisabledReason().isEmpty() ||
                editor_preview.maskStack() != locked_stack || editor_preview.editHistoryEntries() != locked_history)
              return fail("locked mask must remain selectable, explain editing unavailability, and reject density edits");
            process_layout_events();
            auto* lock_reason = find_named_item(edit_workspace, QStringLiteral("selectedMaskLockReason"));
            if (!item_visible(lock_reason) || lock_reason->property("text").toString() != editor_preview.activeMaskEditDisabledReason())
              return fail("selected locked mask must visibly explain its edit restriction");
            const auto lock_capture = qEnvironmentVariable("PHOTO_EDITOR_MASK_LOCK_CAPTURE_PATH");
            if (!lock_capture.isEmpty() && !lock_reason->window()->grabWindow().save(lock_capture))
              return fail("locked selection proof must save its screenshot");
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefineViewSettingsButton"))) ||
                edit_workspace->property("maskRefinementPage").toString() != QStringLiteral("view"))
              return fail("locked masks must still allow View inspection");
            process_layout_events();
            for (const auto& name : {QStringLiteral("maskRefinementPaintPageButton"), QStringLiteral("maskRefinementEdgePageButton"),
                                     QStringLiteral("maskRefinementStrengthPageButton"), QStringLiteral("maskFirstVisibilityButton"), QStringLiteral("maskFirstInvertButton")}) {
              auto* control = find_named_item(edit_workspace, name);
              if (control == nullptr || control->isEnabled()) return fail("locked View must keep matte-changing controls disabled");
            }
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskFirstHiddenButton"))) ||
                editor_preview.maskPreviewDisplayMode() != QStringLiteral("hidden") ||
                !click_item(find_named_item(edit_workspace, QStringLiteral("maskCreationDoneButton"))) ||
                !editor_preview.activeMaskLocked() || editor_preview.maskStack() != locked_stack || editor_preview.editHistoryEntries() != locked_history)
              return fail("locked View inspection must preserve the lock, mask and history");
            edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
            process_layout_events();
            if (!click_item(find_thumbnail())) return fail("locked View must preserve mask reselection");
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("selectedMaskUnlockButton"))) || editor_preview.activeMaskLocked())
              return fail("visible Unlock action must unlock the selected mask by pointer");
            std::cout << "locked-mask-reselection-and-write-guard=passed\n";
          }
          editor_preview.setActiveMaskDensity(0.63);
          if (std::abs(editor_preview.activeMaskDensity() - 0.63) > 1e-6 ||
              editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value(QStringLiteral("id")).toString() != selected_id)
            return fail("density edit after pointer selection must reach the selected stable component");
          const auto name = QStringLiteral("Pointer reselected mask");
          editor_preview.setActiveMaskName(name);
          const auto edited = editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap();
          if (edited.value(QStringLiteral("id")).toString() != selected_id || edited.value(QStringLiteral("name")).toString() != name)
            return fail("edit after native pointer reselection must modify the selected stable ID");
          const auto capture_path = qEnvironmentVariable("PHOTO_EDITOR_MASK_POINTER_CAPTURE_PATH");
          if (!wait_for_condition([&]() { return !editor_preview.processing(); }, 10000))
            return fail("mask edit must finish rendering before its proof capture");
          process_layout_events();
          auto* capture_workspace = qobject_cast<QQuickItem*>(edit_workspace);
          if (qEnvironmentVariableIsSet("PHOTO_EDITOR_FONT_ENTRY_PROOF")) {
            if (!editor_preview.addTextLayer(QStringLiteral("Font proof"), QStringLiteral("Font proof")))
              return fail("font proof must create a text layer");
            const auto text_index = 0;
            if (!QMetaObject::invokeMethod(edit_workspace, "setActivePhotoLayerSelection",
                  Q_ARG(QVariant, QVariant(text_index)), Q_ARG(QVariant, QVariant(true))))
              return fail("font proof must select the text layer");
            process_layout_events();
            auto* panel = find_named_object(edit_workspace, QStringLiteral("photoLayerPropertiesPanel"));
            if (panel == nullptr || !panel->setProperty("detailsExpanded", true)) return fail("font details must expand");
            process_layout_events();
            auto* field = find_named_item(edit_workspace, QStringLiteral("photoLayerTextFontField"));
            if (field == nullptr) return fail("editable font picker must exist");
            for (auto* parent = field->parentItem(); parent != nullptr; parent = parent->parentItem()) {
              if (parent->property("contentY").isValid()) {
                parent->setProperty("contentY", std::max(0.0, parent->property("contentY").toDouble() +
                    scene_rect(field).top() - scene_rect(parent).top() - 40.0));
                process_layout_events();
              }
            }
            const auto fonts = editor_preview.availablePhotoTextFonts();
            if (fonts.isEmpty() || !click_item(field)) return fail("font picker must accept a visible pointer click");
            const auto desired_font = fonts.first();
            auto* window = field->window();
            if (!send_window_key(window, Qt::Key_A, QKeySequence(QKeySequence::SelectAll)[0].keyboardModifiers())) return fail("font input must select its text");
            for (const auto character : desired_font) {
              QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(character));
              QCoreApplication::sendEvent(window, &press);
              QKeyEvent release(QEvent::KeyRelease, 0, Qt::NoModifier, QString(character));
              QCoreApplication::sendEvent(window, &release);
            }
            if (!send_window_key(window, Qt::Key_Return, Qt::NoModifier)) return fail("typed font must be accepted");
            process_layout_events();
            if (editor_preview.photoLayers().at(text_index).toMap().value(QStringLiteral("fontFamily")).toString() != desired_font)
              return fail("typed font picker value must reach the text layer");
            std::cout << "editable-font-pointer-and-keyboard=passed\n";
          }
          if (qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_REFINE_NARROW")) {
            if (capture_workspace == nullptr || capture_workspace->window() == nullptr)
              return fail("refinement layout proof needs a native window");
            capture_workspace->window()->resize(1280, 900);
            process_layout_events();
            if (!capture_path.isEmpty() && !capture_workspace->window()->grabWindow().save(capture_path + QStringLiteral(".immediate.png")))
              return fail("immediate resize capture must be saved");
            QElapsedTimer resize_settle;
            resize_settle.start();
            if (!wait_for_condition([&]() { return resize_settle.elapsed() >= 500 && !editor_preview.processing(); }, 10000))
              return fail("resized preview must settle before the layout capture");
            for (const auto* flow_name : {"maskCreationFlow", "maskRefineFlow"}) {
              auto* flow = find_named_item(edit_workspace, QString::fromLatin1(flow_name));
              if (flow == nullptr || !flow->isVisible() || capture_workspace->window()->width() != 1280)
                return fail("refinement layout must be tested at the requested narrow width");
              if (flow->parentItem() == nullptr || flow->y() + flow->height() > flow->parentItem()->height() + 1)
                return fail("mask toolbar must allocate enough height for its wrapped controls");
              QList<QRectF> occupied;
              for (auto* child : flow->childItems()) {
                if (!child->isVisible() || child->width() <= 0 || child->height() <= 0) continue;
                const QRectF bounds(child->x(), child->y(), child->width(), child->height());
                if (bounds.left() < -1 || bounds.right() > flow->width() + 1 ||
                    bounds.bottom() > flow->height() + 1)
                  return fail("mask toolbar controls must remain inside the narrow toolbar");
                for (const auto& prior : occupied)
                  if (prior.intersected(bounds).width() > 1 && prior.intersected(bounds).height() > 1)
                    return fail("mask toolbar controls must not overlap at narrow width");
                occupied.append(bounds);
              }
              const auto minimum_visible_controls =
                  QString::fromLatin1(flow_name) == QStringLiteral("maskRefineFlow") ? 7 : 6;
              if (occupied.size() < minimum_visible_controls) {
                std::cerr << flow_name << " visible controls=" << occupied.size() << " names=";
                for (auto* child : flow->childItems()) {
                  if (child != nullptr && child->isVisible() && child->width() > 0 && child->height() > 0)
                    std::cerr << child->objectName().toStdString() << ",";
                }
                std::cerr << '\n';
                return fail("refinement layout proof must include its visible navigation and action controls");
              }
              std::cout << flow_name << "-layout=passed; window=1280x900; controls=" << occupied.size() << '\n';
            }
          }
          if (!capture_path.isEmpty() && (capture_workspace == nullptr || capture_workspace->window() == nullptr ||
              !capture_workspace->window()->grabWindow().save(capture_path)))
            return fail("native mask pointer proof screenshot must be saved");
        }
    return EXIT_SUCCESS;
  }

  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  QVariant cool_kelvin;
  QVariant neutral_kelvin;
  QVariant warm_kelvin;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "whiteBalanceKelvin",
          Q_RETURN_ARG(QVariant, cool_kelvin),
          Q_ARG(QVariant, QVariant(-100))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "whiteBalanceKelvin",
          Q_RETURN_ARG(QVariant, neutral_kelvin),
          Q_ARG(QVariant, QVariant(0))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "whiteBalanceKelvin",
          Q_RETURN_ARG(QVariant, warm_kelvin),
          Q_ARG(QVariant, QVariant(100))) ||
      !(cool_kelvin.toInt() < neutral_kelvin.toInt() &&
        neutral_kelvin.toInt() < warm_kelvin.toInt())) {
    return fail("temperature slider Kelvin labels should increase in the warm direction");
  }

  const auto expect_slider = [&](const char* object_name,
                                 double expected_from,
                                 double expected_to,
                                 double expected_default,
                                 const char* failure_message) -> int {
    auto* slider = edit_workspace->findChild<QObject*>(QString::fromLatin1(object_name));
    if (slider == nullptr) {
      std::cerr << failure_message << ": slider should exist\n";
      return EXIT_FAILURE;
    }
    const auto actual_from = slider->property("from").toDouble();
    const auto actual_to = slider->property("to").toDouble();
    const auto actual_default = slider->property("defaultValue").toDouble();
    const auto modified = slider->property("modified").toBool();
    if (std::abs(actual_from - expected_from) > 1e-6 || std::abs(actual_to - expected_to) > 1e-6 ||
        std::abs(actual_default - expected_default) > 1e-6 || modified) {
      std::cerr << failure_message << ": expected range/default "
                << expected_from << ".." << expected_to << " default " << expected_default
                << ", got " << actual_from << ".." << actual_to << " default " << actual_default
                << ", modified=" << modified << "\n";
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  };
  if (expect_slider("selectedToneWheelHueSlider", 0.0, 1.0, 0.0, "color grading hue slider") !=
          EXIT_SUCCESS ||
      expect_slider("toneCurveInputBlackSlider", 0.0, 95.0, 0.0, "tone curve input black slider") !=
          EXIT_SUCCESS ||
      expect_slider("toneCurveInputGammaSlider", -100.0, 100.0, 0.0, "tone curve input gamma slider") !=
          EXIT_SUCCESS ||
      expect_slider("toneCurveInputWhiteSlider", 5.0, 100.0, 100.0, "tone curve input white slider") !=
          EXIT_SUCCESS ||
      expect_slider("channelMixerRedFromRedSlider", -200.0, 200.0, 100.0, "channel mixer identity slider") !=
          EXIT_SUCCESS ||
      expect_slider("colorReplacementTargetHueSlider", 0.0, 1.0, 0.33, "color replacement target hue slider") !=
          EXIT_SUCCESS ||
      expect_slider("colorReplacementRangeSlider", 1.0, 100.0, 35.0, "color replacement range slider") !=
          EXIT_SUCCESS ||
      expect_slider("colorReplacementSoftnessSlider", 0.0, 100.0, 100.0, "color replacement softness slider") !=
          EXIT_SUCCESS ||
      expect_slider("colorReplaceSkinHueSlider", 0.0, 1.0, 0.08, "color replacement protected skin hue slider") !=
          EXIT_SUCCESS ||
      expect_slider("colorReplaceSkinRangeSlider", 1.0, 100.0, 30.0, "color replacement protected skin range slider") !=
          EXIT_SUCCESS ||
      expect_slider("colorReplaceSkinProtectionSlider", 0.0, 100.0, 0.0, "color replacement skin protection slider") !=
          EXIT_SUCCESS ||
      expect_slider("skinToneLineHueSlider", 0.0, 1.0, 0.08, "skin tone line hue slider") != EXIT_SUCCESS ||
      expect_slider("cropZoomSlider", 1.0, 3.0, 1.0, "crop tightening slider") != EXIT_SUCCESS ||
      expect_slider("cropRectXSlider", 0.0, 100.0, 0.0, "crop rectangle x slider") !=
          EXIT_SUCCESS ||
      expect_slider("cropRectYSlider", 0.0, 100.0, 0.0, "crop rectangle y slider") !=
          EXIT_SUCCESS ||
      expect_slider("cropRectWidthSlider", 5.0, 100.0, 100.0, "crop rectangle width slider") !=
          EXIT_SUCCESS ||
      expect_slider("cropRectHeightSlider", 5.0, 100.0, 100.0, "crop rectangle height slider") !=
          EXIT_SUCCESS ||
      expect_slider("transformScaleSlider", 0.25, 4.0, 1.0, "transform scale slider") != EXIT_SUCCESS ||
      expect_slider("detailSharpenRadiusSlider", 0.5, 3.0, 1.0, "detail sharpen radius slider") !=
          EXIT_SUCCESS ||
      expect_slider("detailSharpenDetailSlider", 0.0, 100.0, 0.0, "detail sharpen detail slider") !=
          EXIT_SUCCESS ||
      expect_slider("liquifyRadiusSlider", 0.02, 1.0, 0.25, "liquify radius slider") != EXIT_SUCCESS) {
    return EXIT_FAILURE;
  }
  if (edit_workspace->findChild<QObject*>(QStringLiteral("channelMixerIdentityPresetButton")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("channelMixerMonochromePresetButton")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("channelMixerSwapRedBluePresetButton")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("channelMixerResetRedRowButton")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("channelMixerResetGreenRowButton")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("channelMixerResetBlueRowButton")) == nullptr) {
    return fail("channel mixer should expose presets and per-row reset controls");
  }
  if (edit_workspace->findChild<QObject*>(QStringLiteral("colorReplaceProtectSkinCheckBox")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("colorReplacementPreserveLuminanceCheckBox")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("colorReplacementPreserveSaturationCheckBox")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("colorReplaceMaskPreviewButton")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("colorReplaceAutoSkinButton")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("colorToolMaskPreviewOverlay")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("colorReplaceSkinHueSlider")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("colorReplaceSkinRangeSlider")) == nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("colorReplaceSkinProtectionSlider")) == nullptr) {
    return fail("color replace should expose preservation and skin protection controls next to replacement controls");
  }
  auto* white_balance_group = edit_workspace->findChild<QObject*>(QStringLiteral("whiteBalanceInspectorGroup"));
  auto* light_group = edit_workspace->findChild<QObject*>(QStringLiteral("lightInspectorGroup"));
  auto* tone_curve_group = edit_workspace->findChild<QObject*>(QStringLiteral("toneCurveInspectorGroup"));
  auto* color_group = edit_workspace->findChild<QObject*>(QStringLiteral("colorInspectorGroup"));
  auto* presence_group = edit_workspace->findChild<QObject*>(QStringLiteral("presenceInspectorGroup"));
  auto* raw_group = find_named_object(edit_workspace, QStringLiteral("rawInspectorGroup"));
  auto* edit_inspector_panel = edit_workspace->findChild<QObject*>(QStringLiteral("editInspectorPanel"));
  auto* adjust_inline_histogram_scope =
      find_named_object(edit_workspace, QStringLiteral("adjustInlineHistogramScope"));
  auto* adjust_inline_waveform_scope =
      find_named_object(edit_workspace, QStringLiteral("adjustInlineWaveformScope"));
  auto* adjust_inspector_tab = find_named_object(edit_workspace, QStringLiteral("adjustInspectorTabButton"));
  auto* presets_inspector_tab = find_named_object(edit_workspace, QStringLiteral("presetsInspectorTabButton"));
  auto* layers_inspector_tab = find_named_object(edit_workspace, QStringLiteral("layersInspectorTabButton"));
  auto* crop_inspector_tab = find_named_object(edit_workspace, QStringLiteral("cropInspectorTabButton"));
  auto* scopes_inspector_tab = find_named_object(edit_workspace, QStringLiteral("scopesInspectorTabButton"));
  auto* detail_inspector_tab = find_named_object(edit_workspace, QStringLiteral("detailInspectorTabButton"));
  auto* info_inspector_tab = find_named_object(edit_workspace, QStringLiteral("infoInspectorTabButton"));
  auto* photo_layers_group = find_named_object(edit_workspace, QStringLiteral("photoLayersInspectorGroup"));
  auto* adjust_detail_group = find_named_object(edit_workspace, QStringLiteral("detailInspectorGroup"));
  auto* detail_inspect_button = find_named_object(edit_workspace, QStringLiteral("detailInspectOneToOneButton"));
  auto* detail_loupe_button = find_named_object(edit_workspace, QStringLiteral("detailLoupeButton"));
  auto* detail_mask_preview_button =
      find_named_object(edit_workspace, QStringLiteral("detailMaskPreviewButton"));
  auto* detail_before_after_button =
      find_named_object(edit_workspace, QStringLiteral("detailPanelBeforeAfterButton"));
  auto* detail_sharpening_group = find_named_object(edit_workspace, QStringLiteral("detailSharpeningGroup"));
  auto* detail_noise_reduction_group =
      find_named_object(edit_workspace, QStringLiteral("detailNoiseReductionGroup"));
  auto* adjust_lens_group = find_named_object(edit_workspace, QStringLiteral("lensInspectorGroup"));
  auto* light_basic_tone_group = find_named_object(edit_workspace, QStringLiteral("lightBasicToneGroup"));
  auto* light_advanced_tone_group = find_named_object(edit_workspace, QStringLiteral("lightAdvancedToneGroup"));
  auto* levels_inspector_group = find_named_object(edit_workspace, QStringLiteral("levelsInspectorGroup"));
  auto* light_zone_mapping_group = find_named_object(edit_workspace, QStringLiteral("lightZoneMappingGroup"));
  auto* tone_curve_input_levels_group =
      find_named_object(edit_workspace, QStringLiteral("toneCurveInputLevelsGroup"));
  auto* tone_curve_input_levels_strip =
      find_named_object(edit_workspace, QStringLiteral("toneCurveInputLevelsStrip"));
  auto* tone_curve_input_levels_histogram =
      find_named_object(edit_workspace, QStringLiteral("toneCurveInputLevelsHistogram"));
  auto* tone_curve_input_levels_handle =
      find_named_object(edit_workspace, QStringLiteral("toneCurveInputLevelsHandle"));
  auto* light_rolloff_detail_reset_button =
      find_named_object(edit_workspace, QStringLiteral("lightRollOffDetailResetButton"));
  auto* light_zone_mixer_reset_button =
      find_named_object(edit_workspace, QStringLiteral("lightZoneMixerResetButton"));
  auto* tone_curve_input_levels_reset_button =
      find_named_object(edit_workspace, QStringLiteral("toneCurveInputLevelsResetButton"));
  auto* advanced_color_group = find_named_object(edit_workspace, QStringLiteral("advancedColorInspectorGroup"));
  auto* color_grading_group = find_named_object(edit_workspace, QStringLiteral("colorGradingInspectorGroup"));
  auto* color_mix_group = find_named_object(edit_workspace, QStringLiteral("colorMixGroup"));
  auto* color_mix_active_preview =
      find_named_object(edit_workspace, QStringLiteral("colorMixActiveChannelPreview"));
  auto* color_mix_active_swatch =
      find_named_object(edit_workspace, QStringLiteral("colorMixActiveChannelSwatch"));
  auto* color_mix_active_label =
      find_named_object(edit_workspace, QStringLiteral("colorMixActiveChannelLabel"));
  auto* color_mix_active_values =
      find_named_object(edit_workspace, QStringLiteral("colorMixActiveChannelValues"));
  auto* color_mix_reset_channel_button =
      find_named_object(edit_workspace, QStringLiteral("colorMixResetChannelButton"));
  auto* color_mix_affected_range_strip =
      find_named_object(edit_workspace, QStringLiteral("colorMixAffectedRangeStrip"));
  auto* color_mix_affected_range_swatch =
      find_named_object(edit_workspace, QStringLiteral("colorMixAffectedRangeSwatch"));
  auto* channel_mixer_group = find_named_object(edit_workspace, QStringLiteral("channelMixerGroup"));
  auto* channel_mixer_red_sum_label =
      find_named_object(edit_workspace, QStringLiteral("channelMixerRedRowSumLabel"));
  auto* channel_mixer_green_sum_label =
      find_named_object(edit_workspace, QStringLiteral("channelMixerGreenRowSumLabel"));
  auto* channel_mixer_blue_sum_label =
      find_named_object(edit_workspace, QStringLiteral("channelMixerBlueRowSumLabel"));
  auto* black_white_group = find_named_object(edit_workspace, QStringLiteral("blackWhiteGroup"));
  auto* color_replace_group = find_named_object(edit_workspace, QStringLiteral("colorReplaceGroup"));
  auto* color_replace_swatch_preview =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceSwatchPreview"));
  auto* color_replace_source_swatch =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceSourceSwatch"));
  auto* color_replace_target_swatch =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceTargetSwatch"));
  auto* color_replace_summary_label =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceSummaryLabel"));
  auto* color_replace_selection_label =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceSelectionLabel"));
  auto* color_replace_swap_button =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceSwapHueButton"));
  auto* color_replace_mask_preview_button =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceMaskPreviewButton"));
  auto* color_replace_auto_skin_button =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceAutoSkinButton"));
  auto* color_replace_reset_skin_button =
      find_named_object(edit_workspace, QStringLiteral("colorReplaceResetSkinProtectionButton"));
  auto* color_replace_preserve_luminance_check =
      find_named_object(edit_workspace, QStringLiteral("colorReplacementPreserveLuminanceCheckBox"));
  auto* color_replace_preserve_saturation_check =
      find_named_object(edit_workspace, QStringLiteral("colorReplacementPreserveSaturationCheckBox"));
  auto* skin_tone_group = find_named_object(edit_workspace, QStringLiteral("skinToneGroup"));
  auto* harmony_group = find_named_object(edit_workspace, QStringLiteral("harmonyGroup"));
  auto* harmony_palette_preview = find_named_object(edit_workspace, QStringLiteral("harmonyPalettePreview"));
  auto* harmony_anchor_swatch = find_named_object(edit_workspace, QStringLiteral("harmonyAnchorSwatch"));
  auto* harmony_affected_range_strip =
      find_named_object(edit_workspace, QStringLiteral("harmonyAffectedRangeStrip"));
  auto* harmony_affected_range_swatch =
      find_named_object(edit_workspace, QStringLiteral("harmonyAffectedRangeSwatch"));
  auto* harmony_warm_preset_button = find_named_object(edit_workspace, QStringLiteral("harmonyWarmPresetButton"));
  auto* harmony_complement_preset_button =
      find_named_object(edit_workspace, QStringLiteral("harmonyComplementPresetButton"));
  auto* harmony_triad_preset_button = find_named_object(edit_workspace, QStringLiteral("harmonyTriadPresetButton"));
  auto* harmony_base_hue_slider = find_named_object(edit_workspace, QStringLiteral("harmonyBaseHueSlider"));
  auto* harmony_amount_slider = find_named_object(edit_workspace, QStringLiteral("harmonyAmountSlider"));
  auto* split_toning_group = find_named_object(edit_workspace, QStringLiteral("splitToningGroup"));
  auto* hsl_channel_button_row = find_named_object(edit_workspace, QStringLiteral("hslChannelButtonRow"));
  auto* hsl_reds_button = find_named_object(edit_workspace, QStringLiteral("hslChannelRedsButton"));
  auto* hsl_magentas_button = find_named_object(edit_workspace, QStringLiteral("hslChannelMagentasButton"));
  auto* edit_inspector_footer = find_named_object(edit_workspace, QStringLiteral("editInspectorFooter"));
  auto* initial_edit_filmstrip = find_named_item(edit_workspace, QStringLiteral("editFilmstrip"));
  auto* edit_inspector_reset_button = find_named_object(edit_workspace, QStringLiteral("editInspectorResetButton"));
  auto* edit_inspector_save_preset_button =
      find_named_object(edit_workspace, QStringLiteral("editInspectorSavePresetButton"));
  auto* light_panel_before_after_button =
      find_named_object(edit_workspace, QStringLiteral("lightPanelBeforeAfterButton"));
  auto* color_panel_before_after_button =
      find_named_object(edit_workspace, QStringLiteral("colorPanelBeforeAfterButton"));
  auto* presence_panel_before_after_button =
      find_named_object(edit_workspace, QStringLiteral("presencePanelBeforeAfterButton"));
  auto* edit_adjustment_filter_field =
      find_named_object(edit_workspace, QStringLiteral("editAdjustmentFilterField"));
  auto* clear_edit_adjustment_filter_button =
      find_named_object(edit_workspace, QStringLiteral("clearEditAdjustmentFilterButton"));
  auto* edit_adjustment_filter_status_label =
      find_named_object(edit_workspace, QStringLiteral("editAdjustmentFilterStatusLabel"));
  auto* active_edit_target_bar =
      find_named_object(edit_workspace, QStringLiteral("activeEditTargetBar"));
  auto* active_edit_target_mode_label =
      find_named_object(edit_workspace, QStringLiteral("activeEditTargetModeLabel"));
  auto* active_edit_target_breadcrumb_label =
      find_named_object(edit_workspace, QStringLiteral("activeEditTargetBreadcrumbLabel"));
  auto* active_adjustment_layer_target_bar =
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentLayerTargetBar"));
  auto* active_adjustment_layer_target_name_label =
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentLayerTargetNameLabel"));
  auto* active_adjustment_layer_target_status_label =
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentLayerTargetStatusLabel"));
  auto* active_adjustment_layer_open_layers_button =
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentLayerOpenLayersButton"));
  auto* base_adjustment_target_guard =
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentTargetGuard"));
  process_layout_events(6);
  auto* filmstrip_thumbnail_card = find_named_object(edit_workspace, QStringLiteral("editFilmstripThumbnailCard"));
  auto* filmstrip_image_frame = find_named_object(edit_workspace, QStringLiteral("filmstripImageFrame"));
  auto* filmstrip_image = find_named_object(edit_workspace, QStringLiteral("filmstripImage"));
  auto* filmstrip_placeholder = find_named_object(edit_workspace, QStringLiteral("filmstripPlaceholder"));
  auto* filmstrip_selection_indicator =
      find_named_object(edit_workspace, QStringLiteral("filmstripSelectionIndicator"));
  auto* filmstrip_aspect_badge = find_named_object(edit_workspace, QStringLiteral("filmstripAspectBadge"));
  constexpr int kQmlImagePreserveAspectFit = 1;
  if (initial_edit_filmstrip == nullptr || filmstrip_thumbnail_card == nullptr ||
      filmstrip_image_frame == nullptr || filmstrip_image == nullptr || filmstrip_placeholder == nullptr ||
      filmstrip_selection_indicator == nullptr || filmstrip_aspect_badge == nullptr ||
      !item_visible(initial_edit_filmstrip) ||
      !qobject_cast<QQuickItem*>(filmstrip_thumbnail_card)->isVisible() ||
      !filmstrip_thumbnail_card->property("filmstrip").toBool() ||
      filmstrip_thumbnail_card->property("filmstripImageMargin").toInt() != 0 ||
      filmstrip_thumbnail_card->property("filmstripAspectBadgeVisible").toBool() ||
      filmstrip_image->property("fillMode").toInt() != kQmlImagePreserveAspectFit ||
      filmstrip_aspect_badge->property("visible").toBool() ||
      !filmstrip_selection_indicator->property("visible").toBool()) {
    std::cerr << "Filmstrip thumbnail state: filmstrip="
              << (filmstrip_thumbnail_card == nullptr
                      ? false
                      : filmstrip_thumbnail_card->property("filmstrip").toBool())
              << " cardVisible="
              << (filmstrip_thumbnail_card != nullptr &&
                  qobject_cast<QQuickItem*>(filmstrip_thumbnail_card) != nullptr &&
                  qobject_cast<QQuickItem*>(filmstrip_thumbnail_card)->isVisible())
              << " itemVisible="
              << (filmstrip_thumbnail_card != nullptr &&
                  item_visible(qobject_cast<QQuickItem*>(filmstrip_thumbnail_card)))
              << " margin="
              << (filmstrip_thumbnail_card == nullptr
                      ? -1
                      : filmstrip_thumbnail_card->property("filmstripImageMargin").toInt())
              << " aspectBadgeEnabled="
              << (filmstrip_thumbnail_card != nullptr &&
                  filmstrip_thumbnail_card->property("filmstripAspectBadgeVisible").toBool())
              << " imageFillMode="
              << (filmstrip_image == nullptr ? -1 : filmstrip_image->property("fillMode").toInt())
              << " aspectBadgeVisible="
              << (filmstrip_aspect_badge != nullptr &&
                  filmstrip_aspect_badge->property("visible").toBool())
              << " selectionVisible="
              << (filmstrip_selection_indicator != nullptr &&
                  filmstrip_selection_indicator->property("visible").toBool())
              << " selected="
              << (filmstrip_thumbnail_card != nullptr &&
                  filmstrip_thumbnail_card->property("selected").toBool())
              << '\n';
    return fail("edit filmstrip thumbnails should preserve aspect ratio with minimal chrome and no in-image aspect badge");
  }
  if (white_balance_group == nullptr || light_group == nullptr || tone_curve_group == nullptr ||
      color_group == nullptr || color_grading_group == nullptr || presence_group == nullptr ||
      adjust_detail_group == nullptr || adjust_lens_group == nullptr || raw_group == nullptr ||
      edit_inspector_footer == nullptr || edit_inspector_reset_button == nullptr ||
      edit_inspector_save_preset_button == nullptr ||
      !white_balance_group->property("visible").toBool() || !light_group->property("visible").toBool() ||
      !tone_curve_group->property("visible").toBool() || !color_group->property("visible").toBool() ||
      !color_grading_group->property("visible").toBool() ||
      !presence_group->property("visible").toBool() ||
      !adjust_detail_group->property("visible").toBool() ||
      !adjust_lens_group->property("visible").toBool() ||
      !(light_group->property("y").toDouble() < white_balance_group->property("y").toDouble() &&
        white_balance_group->property("y").toDouble() < color_group->property("y").toDouble() &&
        color_group->property("y").toDouble() < color_grading_group->property("y").toDouble() &&
        color_grading_group->property("y").toDouble() < tone_curve_group->property("y").toDouble() &&
        tone_curve_group->property("y").toDouble() < presence_group->property("y").toDouble())) {
    return fail("adjust inspector should order primary controls as white balance, light, color, color grading, tone curve, then presence and keep Detail/Lens in Adjust");
  }
  if (edit_inspector_panel == nullptr || adjust_inspector_tab == nullptr || presets_inspector_tab == nullptr ||
      layers_inspector_tab == nullptr ||
      crop_inspector_tab == nullptr ||
      scopes_inspector_tab == nullptr || detail_inspector_tab == nullptr || info_inspector_tab == nullptr ||
      photo_layers_group == nullptr ||
      edit_inspector_panel->property("title").toString() != QStringLiteral("Adjust") ||
      adjust_inline_histogram_scope != nullptr ||
      adjust_inline_waveform_scope != nullptr ||
      adjust_inspector_tab->property("text").toString() != QStringLiteral("Adjust") ||
      presets_inspector_tab->property("text").toString() != QStringLiteral("Presets") ||
      layers_inspector_tab->property("text").toString() != QStringLiteral("Layers") ||
      crop_inspector_tab->property("text").toString() != QStringLiteral("Crop") ||
      crop_inspector_tab->property("visible").toBool() ||
      scopes_inspector_tab->property("text").toString() != QStringLiteral("Scopes") ||
      detail_inspector_tab->property("text").toString() != QStringLiteral("Detail") ||
      detail_inspector_tab->property("visible").toBool() ||
      info_inspector_tab->property("text").toString() != QStringLiteral("Info") ||
      photo_layers_group->property("visible").toBool() ||
      light_panel_before_after_button == nullptr || color_panel_before_after_button == nullptr ||
      presence_panel_before_after_button == nullptr ||
      edit_adjustment_filter_field == nullptr || clear_edit_adjustment_filter_button == nullptr ||
      edit_adjustment_filter_status_label == nullptr ||
      active_edit_target_bar == nullptr || active_edit_target_mode_label == nullptr ||
      active_edit_target_breadcrumb_label == nullptr ||
      active_adjustment_layer_target_bar == nullptr ||
      active_adjustment_layer_target_name_label == nullptr ||
      active_adjustment_layer_target_status_label == nullptr ||
      active_adjustment_layer_open_layers_button == nullptr ||
      base_adjustment_target_guard != nullptr ||
      !item_visible(qobject_cast<QQuickItem*>(active_edit_target_bar)) ||
      active_edit_target_bar->property("breadcrumbContract").toString() !=
          QStringLiteral("active-target-breadcrumbs:v2|inspector-visible:true|queryable:true|max-visible:1|duplicate-visible:false") ||
      active_edit_target_bar->property("targetScope").toString() != QStringLiteral("base-image") ||
      active_adjustment_layer_target_bar->property("visible").toBool() ||
      edit_inspector_reset_button->property("text").toString() != QStringLiteral("Reset") ||
      edit_inspector_save_preset_button->property("text").toString() != QStringLiteral("Save Preset") ||
      white_balance_group->property("title").toString() != QStringLiteral("White Balance") ||
      tone_curve_group->property("title").toString() != QStringLiteral("Tone Curve")) {
    std::cerr << "Adjust inspector baseline: panel="
              << (edit_inspector_panel == nullptr ? "null" : "present")
              << " title="
              << (edit_inspector_panel == nullptr
                      ? std::string("null")
                      : edit_inspector_panel->property("title").toString().toStdString())
              << " tabs="
              << (adjust_inspector_tab == nullptr ? "null" : adjust_inspector_tab->property("text").toString().toStdString())
              << "/"
              << (layers_inspector_tab == nullptr ? "null" : layers_inspector_tab->property("text").toString().toStdString())
              << " layersVisible="
              << (photo_layers_group != nullptr && photo_layers_group->property("visible").toBool())
              << " activeBar="
              << (active_edit_target_bar == nullptr ? "null" : "present")
              << " itemVisible="
              << (active_edit_target_bar != nullptr && item_visible(qobject_cast<QQuickItem*>(active_edit_target_bar)))
              << " targetScope="
              << (active_edit_target_bar == nullptr
                      ? std::string("null")
                      : active_edit_target_bar->property("targetScope").toString().toStdString())
              << " adjustmentBarVisible="
              << (active_adjustment_layer_target_bar != nullptr &&
                  active_adjustment_layer_target_bar->property("visible").toBool())
              << " baseGuard=" << (base_adjustment_target_guard == nullptr ? "null" : "present")
              << " resetText="
              << (edit_inspector_reset_button == nullptr
                      ? std::string("null")
                      : edit_inspector_reset_button->property("text").toString().toStdString())
              << " saveText="
              << (edit_inspector_save_preset_button == nullptr
                      ? std::string("null")
                      : edit_inspector_save_preset_button->property("text").toString().toStdString())
              << " wbTitle="
              << (white_balance_group == nullptr
                      ? std::string("null")
                      : white_balance_group->property("title").toString().toStdString())
              << " curveTitle="
              << (tone_curve_group == nullptr
                      ? std::string("null")
                      : tone_curve_group->property("title").toString().toStdString())
              << '\n';
    return fail("adjust inspector should use readable labels without showing a Base Image target card");
  }
  trace_smoke_progress("adjust inspector baseline checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_ADJUST_INSPECTOR_BASELINE")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");

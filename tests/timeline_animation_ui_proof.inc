bool popup_content_ready_for_capture(QObject* popup, QQuickWindow* window,
                                    const QString& item_name) {
  if (popup == nullptr || window == nullptr ||
      !popup->property("visible").toBool() || !popup->property("opened").toBool()) {
    return false;
  }
  const auto opacity = popup->property("opacity");
  if (!opacity.isValid() || opacity.toDouble() < 0.99) return false;
  auto* item = find_visible_named_item(window, item_name);
  if (!item_visible(item) || item->window() != window || item->opacity() < 0.99) return false;
  const auto item_bounds = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
  const QRectF window_bounds(0, 0, window->width(), window->height());
  return item_bounds.isValid() && window_bounds.contains(item_bounds);
}

bool docked_timeline_mixer_ready_for_capture(
    QObject* workspace, QQuickWindow* window, QObject** mixer_out = nullptr) {
  if (workspace == nullptr || window == nullptr) return false;
  auto* mixer = find_named_object(workspace, QStringLiteral("timelineStudioAudioMixer"));
  auto* dock = find_named_item(workspace, QStringLiteral("timelineStudioAudioDock"));
  auto* content = mixer != nullptr
      ? qobject_cast<QQuickItem*>(mixer->property("contentItem").value<QObject*>()) : nullptr;
  auto* title = find_visible_named_item(workspace, QStringLiteral("timelineStudioAudioMixerTitle"));
  auto* master = find_visible_named_item(workspace, QStringLiteral("timelineMasterMixerStrip"));
  auto* voiceover = find_named_item(workspace, QStringLiteral("timelineRecordVoiceoverButton"));
  auto* compact_tools = find_named_item(workspace, QStringLiteral("timelineAudioDockToolsMenu"));
  if (mixer == nullptr || dock == nullptr || content == nullptr || title == nullptr || master == nullptr ||
      !mixer->property("docked").toBool() ||
      mixer->property("workspaceHost").value<QObject*>() != dock ||
      !popup_content_ready_for_capture(mixer, window, QStringLiteral("timelineStudioAudioMixerTitle")) ||
      !item_visible(dock) || !item_visible(content) || content->window() != window ||
      !item_visible(title) || title->window() != window || !item_visible(master) ||
      master->window() != window)
    return false;

  const auto dock_bounds = item_scene_rect(dock);
  const auto content_bounds = item_scene_rect(content);
  const auto title_bounds = item_scene_rect(title);
  const auto master_bounds = item_scene_rect(master);
  const QRectF window_bounds(0, 0, window->width(), window->height());
  if (!dock_bounds.isValid() || !window_bounds.intersects(dock_bounds) ||
      !content_bounds.isValid() || !dock_bounds.intersects(content_bounds) ||
      !content_bounds.contains(title_bounds) || !window_bounds.intersects(master_bounds))
    return false;

  const bool direct_voiceover_available = item_visible(voiceover) && voiceover->isEnabled() &&
                                           voiceover->window() == window &&
                                           content_bounds.contains(item_scene_rect(voiceover));
  bool compact_voiceover_available = false;
  if (item_visible(compact_tools) && compact_tools->isEnabled() && compact_tools->window() == window &&
      content_bounds.contains(item_scene_rect(compact_tools))) {
    const auto items = compact_tools->property("menuItems").toList();
    compact_voiceover_available = std::any_of(items.cbegin(), items.cend(), [](const QVariant& value) {
      return value.toMap().value(QStringLiteral("key")).toString() ==
             QStringLiteral("record-voiceover");
    });
  }
  if (!direct_voiceover_available && !compact_voiceover_available) return false;
  if (mixer_out != nullptr) *mixer_out = mixer;
  return true;
}

bool click_docked_timeline_voiceover(
    QObject* workspace, QQuickWindow* window,
    const std::function<bool(QQuickWindow*, const QString&)>& click) {
  if (!docked_timeline_mixer_ready_for_capture(workspace, window)) return false;
  auto* voiceover = find_visible_named_item(workspace, QStringLiteral("timelineRecordVoiceoverButton"));
  if (item_visible(voiceover))
    return voiceover->isEnabled() && click(window, QStringLiteral("timelineRecordVoiceoverButton"));

  auto* tools = find_visible_named_item(workspace, QStringLiteral("timelineAudioDockToolsMenu"));
  if (!item_visible(tools) || !tools->isEnabled() ||
      !click(window, QStringLiteral("timelineAudioDockToolsMenuPrimaryAction")))
    return false;
  auto* menu = find_named_object(workspace, QStringLiteral("timelineAudioDockToolsMenuPopup"));
  const auto row_name = QStringLiteral("timelineAudioDockToolsMenuPopupItem0");
  if (!wait_for_condition([&] {
        auto* row = find_visible_named_item(workspace, row_name);
        return popup_content_ready_for_capture(menu, window, row_name) && row != nullptr &&
               row->property("itemKey").toString() == QStringLiteral("record-voiceover") &&
               row->isEnabled();
      }, 3000))
    return false;
  return click(window, row_name);
}

bool timeline_panel_visible_state(QObject* workspace, const QString& panel_id, bool* visible) {
  if (workspace == nullptr || visible == nullptr) return false;
  auto* dock_state = find_named_object(workspace, QStringLiteral("timelinePanelDockState"));
  QVariant value;
  if (dock_state == nullptr ||
      !QMetaObject::invokeMethod(dock_state, "visible", Q_RETURN_ARG(QVariant, value),
                                 Q_ARG(QVariant, panel_id)))
    return false;
  if (value.metaType() == QMetaType::fromType<QJSValue>())
    value = value.value<QJSValue>().toVariant();
  *visible = value.toBool();
  return true;
}

bool program_text_effect_state_matches(QObject* workspace, const QString& clip_id,
                                       bool shadow_active, bool stroke_active) {
  if (workspace == nullptr || clip_id.isEmpty()) return false;
  auto* shadow_loader = find_named_object(
      workspace, QStringLiteral("timelineProgramTextShadowLoader-%1").arg(clip_id));
  auto* stroke_loader = find_named_object(
      workspace, QStringLiteral("timelineProgramTextStrokeLoader-%1").arg(clip_id));
  if (shadow_loader == nullptr || stroke_loader == nullptr ||
      shadow_loader->property("active").toBool() != shadow_active ||
      stroke_loader->property("active").toBool() != stroke_active)
    return false;
  auto* shadow_item = shadow_loader->property("item").value<QObject*>();
  auto* stroke_item = stroke_loader->property("item").value<QObject*>();
  if ((shadow_item != nullptr) != shadow_active ||
      (stroke_item != nullptr) != stroke_active)
    return false;
  return !stroke_active || stroke_item->property("delegateCount").toInt() == 8;
}

bool program_text_effects_align_to_card(QObject* workspace, const QString& clip_id) {
  if (workspace == nullptr || clip_id.isEmpty()) return false;
  auto* card = qobject_cast<QQuickItem*>(find_named_object(
      workspace, QStringLiteral("timelineProgramTitleCard-%1").arg(clip_id)));
  auto* title_text = qobject_cast<QQuickItem*>(find_named_object(
      workspace, QStringLiteral("timelineProgramText-%1").arg(clip_id)));
  auto* shadow_text = qobject_cast<QQuickItem*>(find_named_object(
      workspace, QStringLiteral("timelineProgramTextShadow-%1").arg(clip_id)));
  if (card == nullptr || title_text == nullptr || shadow_text == nullptr) return false;

  const auto model = card->property("modelData").toMap();
  const auto style_scale = card->property("styleScale").toDouble();
  const auto card_center = QPointF(card->width() / 2, card->height() / 2);
  const auto near = [](const QPointF& left, const QPointF& right) {
    return std::hypot(left.x() - right.x(), left.y() - right.y()) <= 1.0;
  };
  const auto item_center = [card](QQuickItem* item) {
    return item->mapToItem(card, QPointF(item->width() / 2, item->height() / 2));
  };
  const auto shadow_center = card_center + QPointF(
      model.value(QStringLiteral("captionShadowOffsetX"), 2.0).toDouble() * style_scale,
      model.value(QStringLiteral("captionShadowOffsetY"), 3.0).toDouble() * style_scale);
  if (!near(item_center(title_text), card_center) ||
      !near(item_center(shadow_text), shadow_center))
    return false;

  const auto stroke_width = model.value(QStringLiteral("captionStrokeWidth")).toDouble()
                            * style_scale;
  const QList<QPointF> stroke_offsets{
      {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
  for (qsizetype index = 0; index < stroke_offsets.size(); ++index) {
    auto* stroke_text = qobject_cast<QQuickItem*>(find_named_object(
        workspace, QStringLiteral("timelineProgramTextStroke-%1-%2")
                       .arg(clip_id).arg(static_cast<qlonglong>(index))));
    if (stroke_text == nullptr ||
        !near(item_center(stroke_text),
              card_center + stroke_offsets.at(index) * stroke_width))
      return false;
  }
  return true;
}

int run_timeline_source_crop_ui_section(
    QQuickWindow* window, QObject* workspace,
    photo_editor::desktop::TimelineDocumentController* document,
    photo_editor::desktop::TimelinePlaybackController* playback,
    const QString& output_dir, QColor* crop_center_pixel_out,
    bool native_shortcut_proof_passed) {
  ulong timestamp = 50000;
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
  QImage crop_source(128, 96, QImage::Format_RGB32);
  for (int y = 0; y < crop_source.height(); ++y) {
    for (int x = 0; x < crop_source.width(); ++x) {
      crop_source.setPixelColor(
          x, y, x < 42 ? QColor(QStringLiteral("#e52f35"))
                       : x < 85 ? QColor(QStringLiteral("#32c85a"))
                                : QColor(QStringLiteral("#285fe8")));
    }
  }
  const auto crop_source_path = QDir(output_dir).filePath("source-crop-color-bars.png");
  const auto crop_video_path = QDir(output_dir).filePath("source-crop-color-bars.mp4");
  const auto white_foreground_path = QDir(output_dir).filePath("source-crop-white-foreground.png");
  QImage white_foreground(128, 96, QImage::Format_RGB32);
  white_foreground.fill(Qt::white);
  if (!crop_source.save(crop_source_path) ||
      !white_foreground.save(white_foreground_path) ||
      !write_timeline_proof_video_file(crop_source_path, crop_video_path)) {
    return fail("Source-crop UI proof needs its encoded color-bar and overlay fixtures");
  }

  document->createTimelineWithSettings({{"timelineName", "Source crop proof"},
                                        {"width", 640}, {"height", 480},
                                        {"frameRate", 30.0}});
  document->addClipFromGallery("video:source-crop-ui", crop_video_path,
                               "Crop controls", 10.0,
                               QUrl::fromLocalFile(crop_source_path).toString());
  document->selectClip(0);
  document->setSelectedFrameMode("stretch");
  document->resetSelectedSourceCrop();
  if (!QMetaObject::invokeMethod(
          workspace, "openInspectorTab",
          Q_ARG(QVariant, QVariant(QStringLiteral("motion"))))) {
    return fail("The source-crop proof must open the visible Motion Inspector route");
  }
  workspace->setProperty("sourceCropAspectConstraint", "free");
  if (playback == nullptr) return fail("Source-crop UI proof requires the real Timeline playback controller");
  const auto sample_count_before_preview = playback->frameProcessingMetrics()
                                               .value(QStringLiteral("sampleCount"))
                                               .toLongLong();
  document->seekSequenceSeconds(0.0);
  document->playSequence();
  if (!wait_for_condition([&] {
        return playback->frameProcessingMetrics()
                   .value(QStringLiteral("sampleCount"))
                   .toLongLong() > sample_count_before_preview &&
               !playback->processedFrameSource().isEmpty();
      }, 12000)) {
    document->pauseSequence();
    return fail("The source-crop interaction requires a fresh decoded Program frame");
  }
  document->pauseSequence();
  process_layout_events(8);
  if (!wait_for_condition([&] {
        return !workspace->property("timelineInspectorPanelLoading").toBool() &&
               named_item_visible(workspace, "timelineSourceCropControls") &&
               named_item_visible(workspace, "timelineProgramSourceCropOverlay") &&
               named_item_visible(workspace, "timelineProgramSourceCropHandle-left");
      }, 12000)) {
    const auto clip = document->currentClip();
    std::cerr << "Source-crop UI state: page="
              << workspace->property("timelineEditorPageLoaded").toBool()
              << " loading=" << workspace->property("timelineInspectorPanelLoading").toBool()
              << " tab=" << workspace->property("inspectorTab").toString().toStdString()
              << " clips=" << document->clipCount() << " selected="
              << document->currentIndex() << " clipId="
              << clip.value("clipId").toString().toStdString() << " media="
              << clip.value("mediaType").toString().toStdString() << " frameMode="
              << clip.value("frameMode").toString().toStdString() << " videoMonitor="
              << workspace->property("videoMonitor").isValid() << '\n';
    for (const auto* name : {"timelineSourceCropControls",
                             "timelineProgramSourceCropOverlay",
                             "timelineProgramSourceCropHandle-left"}) {
      auto* object = find_named_object(workspace, QString::fromLatin1(name));
      auto* item = qobject_cast<QQuickItem*>(object);
      std::cerr << "  " << name << " exists=" << (object != nullptr)
                << " visible=" << (item != nullptr && item->isVisible())
                << " enabled=" << (item != nullptr && item->isEnabled())
                << " size=" << (item != nullptr ? item->width() : 0)
                << 'x' << (item != nullptr ? item->height() : 0) << '\n';
    }
    window->grabWindow().save(QDir(output_dir).filePath("source-crop-controls-timeout.png"));
    return fail("A selected source must expose the crop inspector and direct Program handles");
  }
  const auto crop_before_drag = document->deterministicDocumentHash();
  auto* crop_left_handle = find_visible_named_item(
      workspace, "timelineProgramSourceCropHandle-left");
  const auto crop_handle_point = crop_left_handle->mapToScene(
      QPointF(crop_left_handle->width() / 2, crop_left_handle->height() / 2));
  pointer(window, QEvent::MouseButtonPress, crop_handle_point, Qt::LeftButton);
  pointer(window, QEvent::MouseMove, crop_handle_point + QPointF(38, 0), Qt::LeftButton);
  pointer(window, QEvent::MouseButtonRelease, crop_handle_point + QPointF(38, 0), Qt::NoButton);
  const auto dragged_left = document->currentClip()
                                .value("sourceCropLeft").toDouble();
  if (dragged_left < 0.04 || dragged_left > 0.3 ||
      !window->grabWindow().save(QDir(output_dir).filePath("source-crop-program-handles.png"))) {
    std::cerr << "Program crop drag left=" << dragged_left << '\n';
    return fail("Dragging a Program crop edge must change the selected source and be capturable");
  }
  document->undo();
  if (document->deterministicDocumentHash() != crop_before_drag ||
      std::abs(document->currentClip().value("sourceCropLeft").toDouble()) > 0.0001) {
    return fail("One Undo must restore the full source after a Program crop gesture");
  }

  auto* crop_left_editor = find_visible_named_item(
      workspace, "timelineSourceCropLeftValueEditor");
  if (!item_visible(crop_left_editor) || !crop_left_editor->isEnabled())
    return fail("The precise source-crop percentage field must be available");
  const auto crop_editor_point = crop_left_editor->mapToScene(
      QPointF(crop_left_editor->width() / 2, crop_left_editor->height() / 2));
  pointer(window, QEvent::MouseButtonPress, crop_editor_point, Qt::LeftButton);
  pointer(window, QEvent::MouseButtonRelease, crop_editor_point, Qt::NoButton);
  crop_left_editor->forceActiveFocus();
  QMetaObject::invokeMethod(crop_left_editor, "selectAll");
  process_layout_events(2);
  if (!crop_left_editor->property("activeFocus").toBool()) {
    std::cerr << "Crop percentage editor failed focus: enabled="
              << crop_left_editor->isEnabled() << " visible="
              << crop_left_editor->isVisible() << " focus="
              << crop_left_editor->property("activeFocus").toBool() << '\n';
    return fail("The crop percentage field must accept focus before typing");
  }
  QTest::keyClick(window, Qt::Key_1);
  QTest::keyClick(window, Qt::Key_2);
  QTest::keyClick(window, Qt::Key_Period);
  QTest::keyClick(window, Qt::Key_0);
  QTest::keyClick(window, Qt::Key_Return);
  process_layout_events(8);
  if (std::abs(document->currentClip().value("sourceCropLeft").toDouble() - 0.12) > 0.001 ||
      !window->grabWindow().save(QDir(output_dir).filePath("source-crop-inspector.png"))) {
    std::cerr << "Inspector crop left="
              << document->currentClip().value("sourceCropLeft").toDouble()
              << " editorText=" << crop_left_editor->property("text").toString().toStdString()
              << " activeFocus=" << crop_left_editor->property("activeFocus").toBool()
              << " editingText=" << crop_left_editor->property("editingText").toBool()
              << " value=" << crop_left_editor->property("value").toInt() << '\n';
    return fail("Typing an exact crop percentage must update the source crop");
  }
  if (!click(window, "timelineSourceCropResetButton") ||
      std::abs(document->currentClip().value("sourceCropLeft").toDouble()) > 0.0001 ||
      std::abs(document->currentClip().value("sourceCropRight").toDouble() - 1.0) > 0.0001) {
    return fail("The crop inspector Reset action must restore all source edges");
  }

  if (playback == nullptr || !document->setSelectedSourceCrop(0.66, 0.0, 1.0, 1.0))
    return fail("Primary crop pixel proof needs a live decoded Program source");
  const auto frame_sample_count_before_crop = playback->frameProcessingMetrics()
                                                  .value(QStringLiteral("sampleCount"))
                                                  .toLongLong();
  document->seekSequenceSeconds(0.0);
  document->playSequence();
  if (!wait_for_condition([&] {
        const auto metrics = playback->frameProcessingMetrics();
        return metrics.value(QStringLiteral("sampleCount")).toLongLong() >
                   frame_sample_count_before_crop &&
               !playback->processedFrameSource().isEmpty();
      }, 12000)) {
    document->pauseSequence();
    return fail("Changing a primary source crop must publish a fresh decoded Program frame");
  }
  document->pauseSequence();
  auto* primary_crop_monitor = find_visible_named_item(workspace, "timelineProgramMonitor");
  auto* primary_crop_image = find_visible_named_item(
      workspace, "timelineProgramProcessedVideoFrame");
  const auto primary_frame_bound_and_ready = wait_for_condition([&] {
    primary_crop_monitor = find_visible_named_item(workspace, "timelineProgramMonitor");
    primary_crop_image = find_visible_named_item(
        workspace, "timelineProgramProcessedVideoFrame");
    if (!item_visible(primary_crop_monitor) || !item_visible(primary_crop_image)) return false;
    const auto image_source = primary_crop_image->property("source").toString();
    const auto processed_source = playback->processedFrameSource();
    const auto source_size = primary_crop_image->property("sourceSize").toSize();
    return !processed_source.isEmpty() && image_source == processed_source &&
           primary_crop_image->property("status").toInt() == 1 &&
           source_size.width() > 0 && source_size.height() > 0;
  }, 5000);
  process_layout_events(4);
  if (!primary_frame_bound_and_ready || !item_visible(primary_crop_monitor) ||
      !item_visible(primary_crop_image) ||
      primary_crop_image->property("source").toString() != playback->processedFrameSource()) {
    QVariant should_show_video_monitor;
    QMetaObject::invokeMethod(workspace, "shouldShowVideoMonitor",
                              Q_RETURN_ARG(QVariant, should_show_video_monitor));
    std::cerr << "Primary crop frame binding: monitorVisible="
              << item_visible(primary_crop_monitor) << " imageVisible="
              << item_visible(primary_crop_image) << " imageSource="
              << (primary_crop_image
                      ? primary_crop_image->property("source").toString().toStdString() : "")
              << " processedSource=" << playback->processedFrameSource().toStdString()
              << " samples=" << playback->frameProcessingMetrics()
                                  .value(QStringLiteral("sampleCount")).toLongLong()
              << " imageStatus=" << (primary_crop_image
                                          ? primary_crop_image->property("status").toInt() : -1)
              << " imageSize=" << (primary_crop_image
                                       ? primary_crop_image->property("sourceSize").toSize()
                                             .width() : 0)
              << 'x' << (primary_crop_image
                             ? primary_crop_image->property("sourceSize").toSize()
                                   .height() : 0)
              << " sequencePlaying=" << workspace->property("sequencePlaying").toBool()
              << " renderCacheActive=" << workspace->property("renderCacheActive").toBool()
              << " shouldShowVideoMonitor=" << should_show_video_monitor.toBool()
              << " currentClip=" << document->currentClip()
                                     .value("clipId").toString().toStdString() << '\n';
    return fail("The primary crop pixel sample must come from the current processed Program frame");
  }
  QTest::qWait(80);
  process_layout_events(2);
  const auto primary_crop_frame = window->grabWindow();
  if (primary_crop_frame.isNull() ||
      !primary_crop_frame.save(QDir(output_dir).filePath("source-crop-primary-fresh-frame.png"))) {
    return fail("The freshly decoded primary crop Program frame must be capturable");
  }
  const auto monitor_pixel_at = [&](qreal x_fraction, qreal y_fraction) {
    const auto point = primary_crop_monitor->mapToScene(
        QPointF(primary_crop_monitor->width() * x_fraction,
                primary_crop_monitor->height() * y_fraction));
    return primary_crop_frame.pixelColor(
        qBound(0, qRound(point.x() * primary_crop_frame.width() / window->width()),
               primary_crop_frame.width() - 1),
        qBound(0, qRound(point.y() * primary_crop_frame.height() / window->height()),
               primary_crop_frame.height() - 1));
  };
  const auto primary_crop_center_pixel = monitor_pixel_at(0.50, 0.50);
  const auto primary_crop_left_pixel = monitor_pixel_at(0.30, 0.50);
  const auto primary_crop_right_pixel = monitor_pixel_at(0.70, 0.50);
  const auto is_blue_source_pixel = [](const QColor& pixel) {
    return pixel.blue() > pixel.red() + 65 && pixel.blue() > pixel.green() + 35;
  };
  if (!is_blue_source_pixel(primary_crop_center_pixel) ||
      !is_blue_source_pixel(primary_crop_left_pixel) ||
      !is_blue_source_pixel(primary_crop_right_pixel)) {
    std::cerr << "Fresh primary crop pixels center="
              << primary_crop_center_pixel.name().toStdString() << " left="
              << primary_crop_left_pixel.name().toStdString() << " right="
              << primary_crop_right_pixel.name().toStdString() << " crop="
              << document->currentClip().value("sourceCropLeft").toDouble() << ','
              << document->currentClip().value("sourceCropRight").toDouble()
              << " frame=" << playback->processedFrameSource().toStdString() << '\n';
    return fail("A fresh primary frame cropped to its rightmost third must show blue source pixels across the Program monitor");
  }
  document->resetSelectedSourceCrop();
  if (std::abs(document->currentClip().value("sourceCropLeft").toDouble()) > 0.0001 ||
      std::abs(document->currentClip().value("sourceCropRight").toDouble() - 1.0) > 0.0001)
    return fail("Primary crop pixel proof must restore the full source before the next setup");

  document->createTimelineWithSettings({{"timelineName", "Secondary source crop proof"},
                                        {"width", 640}, {"height", 480},
                                        {"frameRate", 30.0}});
  document->addVideoTrack();
  QVector<QPair<int, int>> crop_video_tracks;
  const auto crop_tracks = document->sequenceTracks();
  for (int index = 0; index < crop_tracks.size(); ++index) {
    const auto track = crop_tracks.at(index).toMap();
    if (track.value("trackKind").toString() == "video" &&
        track.value("trackRole").toString() != "caption") {
      crop_video_tracks.push_back({track.value("laneIndex").toInt(), index});
    }
  }
  std::sort(crop_video_tracks.begin(), crop_video_tracks.end());
  if (crop_video_tracks.size() < 2)
    return fail("Secondary source-crop proof needs two video lanes");
  const auto lower_video_track = crop_video_tracks.first().second;
  const auto upper_video_track = crop_video_tracks.constLast().second;
  const auto target_video_track = [&](int target_row) {
    auto tracks = document->sequenceTracks();
    for (int index = 0; index < tracks.size(); ++index) {
      const auto track = tracks.at(index).toMap();
      if (track.value("trackKind").toString() == "video" &&
          track.value("trackRole").toString() != "caption" && index != target_row &&
          track.value("targeted").toBool()) {
        document->toggleTrackTargeted(index);
      }
    }
    tracks = document->sequenceTracks();
    if (target_row < 0 || target_row >= tracks.size()) return false;
    const auto target = tracks.at(target_row).toMap();
    if (!target.value("targeted").toBool()) document->toggleTrackTargeted(target_row);
    return document->sequenceTracks().at(target_row).toMap()
        .value("targeted").toBool();
  };
  if (!target_video_track(upper_video_track))
    return fail("Source-crop proof could not target the upper video lane");
  document->seekSequenceSeconds(0.0);
  document->addClipFromGallery("photo:source-crop-secondary-foreground",
                               white_foreground_path, "White foreground", 10.0,
                               QUrl::fromLocalFile(white_foreground_path).toString());
  const auto foreground_clip_id = document->currentClip()
                                      .value("clipId").toString();
  document->setSelectedClipOpacity(0.25);
  if (!target_video_track(lower_video_track))
    return fail("Source-crop proof could not target the lower video lane");
  document->seekSequenceSeconds(0.0);
  document->addClipFromGallery("video:source-crop-secondary", crop_video_path,
                               "Cropped secondary", 10.0,
                               QUrl::fromLocalFile(crop_source_path).toString());
  if (!document->setSelectedSourceCrop(0.5, 0.0, 1.0, 1.0))
    return fail("Source-crop proof could not crop the secondary video source");
  document->setSelectedFrameMode("fit");
  int foreground_clip_index = -1;
  int clip_count = 0;
  for (const auto& track_value : document->sequenceTracks()) {
    clip_count += track_value.toMap().value("clips").toList().size();
  }
  for (int clip_index = 0; clip_index < clip_count; ++clip_index) {
    if (document->timelineClipDetails(clip_index)
            .value("clipId").toString() == foreground_clip_id) {
      foreground_clip_index = clip_index;
      break;
    }
  }
  if (foreground_clip_id.isEmpty() || foreground_clip_index < 0)
    return fail("Source-crop proof must retain its foreground photo sentinel");
  document->selectClip(foreground_clip_index);
  if (document->programClip().value("clipId").toString() != foreground_clip_id)
    return fail("The white foreground photo must remain the active Program clip");
  document->seekSequenceSeconds(0.5);
  workspace->setProperty("inspectorTab", "effects");
  process_layout_events(8);
  document->playSequence();
  if (!wait_for_condition([&] {
        auto* player = find_named_object(workspace, "timelineProgramBackgroundMediaPlayer");
        auto* source = find_visible_named_item(
            workspace, "timelineProgramBackgroundSourceCrop");
        auto* video_output = find_visible_named_item(
            workspace, "timelineProgramBackgroundVideo");
        return player && player->property("hasVideo").toBool() && item_visible(source) &&
               item_visible(video_output) && source->width() > 0 && source->height() > 0;
      }, 12000)) {
    document->pauseSequence();
    auto* player = find_named_object(workspace, "timelineProgramBackgroundMediaPlayer");
    auto* source = find_visible_named_item(
        workspace, "timelineProgramBackgroundSourceCrop");
    auto* video_output = find_visible_named_item(
        workspace, "timelineProgramBackgroundVideo");
    const auto clip = document->currentClip();
    std::cerr << "Secondary crop playback diagnostics: player=" << (player != nullptr)
              << " hasVideo=" << (player ? player->property("hasVideo").toBool() : false)
              << " mediaStatus=" << (player ? player->property("mediaStatus").toInt() : -1)
              << " playbackState=" << (player ? player->property("playbackState").toInt() : -1)
              << " error=" << (player ? player->property("errorString").toString().toStdString() : "")
              << " playerSource=" << (player ? player->property("source").toUrl().toString().toStdString() : "")
              << " wrapper=" << (source != nullptr)
              << " wrapperVisible=" << item_visible(source)
              << " wrapperSize=" << (source ? source->width() : 0.0) << 'x'
              << (source ? source->height() : 0.0)
              << " output=" << (video_output != nullptr)
              << " outputVisible=" << item_visible(video_output)
              << " outputSize=" << (video_output ? video_output->width() : 0.0) << 'x'
              << (video_output ? video_output->height() : 0.0)
              << " sequencePlaying=" << workspace->property("sequencePlaying").toBool()
              << " selectedClip=" << clip.value("clipId").toString().toStdString()
              << " sourceUri=" << clip.value("sourceUri").toString().toStdString()
              << " mediaType=" << clip.value("mediaType").toString().toStdString()
              << " sourceCrop=" << clip.value("sourceCropLeft").toDouble() << ','
              << clip.value("sourceCropRight").toDouble() << '\n';
    return fail("The lower video must play through the live secondary crop wrapper");
  }
  QTest::qWait(350);
  document->pauseSequence();
  process_layout_events(8);
  auto* crop_monitor = find_visible_named_item(workspace, "timelineProgramMonitor");
  auto* background_source_crop = find_visible_named_item(
      workspace, "timelineProgramBackgroundSourceCrop");
  auto* background_display_viewport = find_visible_named_item(
      workspace, "timelineSourceCropDisplayViewport");
  const auto secondary_display_rect = background_source_crop
                                         ? background_source_crop->property("displayRect").toRectF()
                                         : QRectF();
  const auto secondary_display_fraction = background_source_crop &&
                                                   background_source_crop->width() > 0
                                               ? secondary_display_rect.width() /
                                                     background_source_crop->width()
                                               : 0.0;
  if (!item_visible(crop_monitor) || !item_visible(background_source_crop) ||
      !item_visible(background_display_viewport) ||
      background_source_crop->property("cropLeft").toDouble() < 0.49 ||
      background_source_crop->property("cropRight").toDouble() > 1.001 ||
      secondary_display_fraction < 0.40 || secondary_display_fraction > 0.60 ||
      background_display_viewport->x() < background_source_crop->width() * 0.15 ||
      background_display_viewport->width() > background_source_crop->width() * 0.75) {
    std::cerr << "Secondary crop display fraction=" << secondary_display_fraction
              << " viewport=" << (background_display_viewport
                                      ? background_display_viewport->width() : 0.0)
              << " crop=" << (background_source_crop
                                   ? background_source_crop->property("cropLeft").toDouble() : -1.0)
              << '\n';
    return fail("The selected secondary source crop must reach the live Program layer");
  }
  const auto secondary_crop_frame = window->grabWindow();
  if (secondary_crop_frame.isNull() ||
      !secondary_crop_frame.save(QDir(output_dir).filePath("source-crop-secondary-video.png"))) {
    return fail("The secondary source-crop Program frame must be capturable");
  }
  const auto crop_monitor_center = crop_monitor->mapToScene(
      QPointF(crop_monitor->width() / 2, crop_monitor->height() / 2));
  const auto crop_center_pixel = secondary_crop_frame.pixelColor(
      qBound(0, qRound(crop_monitor_center.x() * secondary_crop_frame.width() / window->width()),
             secondary_crop_frame.width() - 1),
      qBound(0, qRound(crop_monitor_center.y() * secondary_crop_frame.height() / window->height()),
             secondary_crop_frame.height() - 1));
  if (crop_center_pixel.blue() < crop_center_pixel.green() + 45) {
    std::cerr << "Secondary crop monitor center=" << crop_center_pixel.name().toStdString()
              << " crop=" << background_source_crop->property("cropLeft").toDouble()
              << ',' << background_source_crop->property("cropRight").toDouble() << '\n';
    return fail("The live secondary layer must display cropped blue source pixels, not the uncropped green center");
  }
  const auto crop_monitor_left = crop_monitor->mapToScene(
      QPointF(crop_monitor->width() * 0.10, crop_monitor->height() * 0.50));
  const auto secondary_fit_letterbox_pixel = secondary_crop_frame.pixelColor(
      qBound(0, qRound(crop_monitor_left.x() * secondary_crop_frame.width() / window->width()),
             secondary_crop_frame.width() - 1),
      qBound(0, qRound(crop_monitor_left.y() * secondary_crop_frame.height() / window->height()),
             secondary_crop_frame.height() - 1));
  const auto letterbox_channel_spread = std::max(
      {secondary_fit_letterbox_pixel.red(), secondary_fit_letterbox_pixel.green(),
       secondary_fit_letterbox_pixel.blue()}) -
      std::min({secondary_fit_letterbox_pixel.red(), secondary_fit_letterbox_pixel.green(),
                secondary_fit_letterbox_pixel.blue()});
  if (letterbox_channel_spread > 28) {
    std::cerr << "Secondary Fit letterbox pixel="
              << secondary_fit_letterbox_pixel.name().toStdString()
              << " viewport=" << background_display_viewport->x() << ','
              << background_display_viewport->width() << " crop="
              << background_source_crop->property("cropLeft").toDouble() << ','
              << background_source_crop->property("cropRight").toDouble() << '\n';
    return fail("Secondary Fit must clip uncropped source pixels out of the letterbox margins");
  }

  document->createTimelineWithSettings({{"timelineName", "Secondary still source crop proof"},
                                        {"width", 640}, {"height", 480},
                                        {"frameRate", 30.0}});
  document->addVideoTrack();
  QVector<QPair<int, int>> still_video_tracks;
  const auto still_tracks = document->sequenceTracks();
  for (int index = 0; index < still_tracks.size(); ++index) {
    const auto track = still_tracks.at(index).toMap();
    if (track.value("trackKind").toString() == "video" &&
        track.value("trackRole").toString() != "caption") {
      still_video_tracks.push_back({track.value("laneIndex").toInt(), index});
    }
  }
  std::sort(still_video_tracks.begin(), still_video_tracks.end());
  if (still_video_tracks.size() < 2)
    return fail("Secondary still source-crop proof needs two video lanes");
  const auto still_lower_video_track = still_video_tracks.first().second;
  const auto still_upper_video_track = still_video_tracks.constLast().second;
  if (!target_video_track(still_upper_video_track))
    return fail("Still source-crop proof could not target the upper video lane");
  document->seekSequenceSeconds(0.0);
  document->addClipFromGallery("photo:source-crop-still-foreground",
                               white_foreground_path, "White foreground", 10.0,
                               QUrl::fromLocalFile(white_foreground_path).toString());
  const auto still_foreground_clip_id = document->currentClip()
                                            .value("clipId").toString();
  document->setSelectedClipOpacity(0.25);
  if (!target_video_track(still_lower_video_track))
    return fail("Still source-crop proof could not target the lower video lane");
  document->seekSequenceSeconds(0.0);
  document->addClipFromGallery("photo:source-crop-overlay", crop_source_path,
                               "Cropped still image", 10.0,
                               QUrl::fromLocalFile(crop_source_path).toString());
  if (!document->setSelectedSourceCrop(0.66, 0.0, 1.0, 1.0))
    return fail("Source-crop proof could not crop the still-image overlay");
  document->setSelectedClipOpacity(1.0);
  document->setSelectedFrameMode("fit");
  int still_foreground_clip_index = -1;
  int still_clip_count = 0;
  for (const auto& track_value : document->sequenceTracks()) {
    still_clip_count += track_value.toMap().value("clips").toList().size();
  }
  for (int clip_index = 0; clip_index < still_clip_count; ++clip_index) {
    if (document->timelineClipDetails(clip_index)
            .value("clipId").toString() == still_foreground_clip_id) {
      still_foreground_clip_index = clip_index;
      break;
    }
  }
  if (still_foreground_clip_id.isEmpty() || still_foreground_clip_index < 0)
    return fail("Still source-crop proof must retain its foreground photo sentinel");
  document->selectClip(still_foreground_clip_index);
  if (document->programClip().value("clipId").toString() != still_foreground_clip_id)
    return fail("The white still-image foreground must remain the active Program clip");
  document->seekSequenceSeconds(0.5);
  if (!wait_for_condition([&] {
        auto* image = find_visible_named_item(workspace, "timelineProgramBackgroundPhotoImage");
        if (!item_visible(image)) return false;
        const auto source_size = image->property("sourceSize").toSize();
        return image->property("status").toInt() == 1 &&
               source_size.width() == crop_source.width() &&
               source_size.height() == crop_source.height();
      }, 12000)) {
    return fail("The uncropped still-image source must load before its crop pixel proof");
  }
  process_layout_events(8);
  auto* still_image = find_visible_named_item(workspace, "timelineProgramBackgroundPhotoImage");
  auto* still_image_crop_clip = still_image && still_image->parentItem() &&
                                       still_image->parentItem()->parentItem() &&
                                       still_image->parentItem()->parentItem()->parentItem()
                                   ? still_image->parentItem()->parentItem()->parentItem()
                                   : nullptr;
  const auto still_display_rect = still_image_crop_clip
                                      ? still_image_crop_clip->property("displayRect").toRectF()
                                      : QRectF();
  const auto still_display_fraction = still_image_crop_clip &&
                                              still_image_crop_clip->width() > 0
                                          ? still_display_rect.width() /
                                                still_image_crop_clip->width()
                                          : 0.0;
  const auto still_image_crop_frame = window->grabWindow();
  if (still_image_crop_frame.isNull() ||
      !still_image_crop_frame.save(
          QDir(output_dir).filePath("source-crop-secondary-still-image.png"))) {
    return fail("The cropped still-image Program frame must be capturable");
  }
  const auto still_image_center_pixel = still_image_crop_frame.pixelColor(
      qBound(0, qRound(crop_monitor_center.x() * still_image_crop_frame.width() / window->width()),
             still_image_crop_frame.width() - 1),
      qBound(0, qRound(crop_monitor_center.y() * still_image_crop_frame.height() / window->height()),
             still_image_crop_frame.height() - 1));
  if (!still_image_crop_clip ||
      still_image_crop_clip->property("cropLeft").toDouble() < 0.65 ||
      still_display_fraction < 0.28 || still_display_fraction > 0.42 ||
      still_image_center_pixel.blue() < still_image_center_pixel.green() + 45) {
    std::cerr << "Still-image crop center="
              << still_image_center_pixel.name().toStdString() << " crop="
              << (still_image_crop_clip
                      ? still_image_crop_clip->property("cropLeft").toDouble() : -1.0)
              << " display_fraction=" << still_display_fraction
              << '\n';
    return fail("The still-image layer must render its cropped blue source pixels into the Program frame");
  }
  if (crop_center_pixel_out != nullptr) *crop_center_pixel_out = crop_center_pixel;
  QJsonArray captures;
  captures.append(QStringLiteral("source-crop-program-handles.png"));
  captures.append(QStringLiteral("source-crop-inspector.png"));
  captures.append(QStringLiteral("source-crop-primary-fresh-frame.png"));
  captures.append(QStringLiteral("source-crop-secondary-video.png"));
  captures.append(QStringLiteral("source-crop-secondary-still-image.png"));
  const QJsonObject receipt{
      {QStringLiteral("proof"), QStringLiteral("timeline-source-crop-ui")},
      {QStringLiteral("status"), QStringLiteral("passed")},
      {QStringLiteral("nativeShortcutProof"),
       native_shortcut_proof_passed ? QStringLiteral("passed_in_full_animation_proof")
                                    : QStringLiteral("unqualified_not_run")},
      {QStringLiteral("directProgramDragLeft"), dragged_left},
      {QStringLiteral("undoRestoredFullSource"), true},
      {QStringLiteral("inspectorLeftPercent"), 12.0},
      {QStringLiteral("resetRestoredFullSource"), true},
      {QStringLiteral("primaryCropLeft"), 0.66},
      {QStringLiteral("primaryFreshFrameSampleCount"),
       playback->frameProcessingMetrics().value(QStringLiteral("sampleCount")).toLongLong()},
      {QStringLiteral("primaryCropCenterPixel"),
       primary_crop_center_pixel.name(QColor::HexArgb)},
      {QStringLiteral("primaryCropLeftPixel"), primary_crop_left_pixel.name(QColor::HexArgb)},
      {QStringLiteral("primaryCropRightPixel"), primary_crop_right_pixel.name(QColor::HexArgb)},
      {QStringLiteral("secondaryCropLeft"), 0.5},
      {QStringLiteral("secondaryCropCenterPixel"), crop_center_pixel.name(QColor::HexArgb)},
      {QStringLiteral("secondaryFitLetterboxPixel"),
       secondary_fit_letterbox_pixel.name(QColor::HexArgb)},
      {QStringLiteral("secondaryFitLetterboxChannelSpread"), letterbox_channel_spread},
      {QStringLiteral("stillImageCropLeft"), 0.66},
      {QStringLiteral("stillImageCropCenterPixel"),
       still_image_center_pixel.name(QColor::HexArgb)},
      {QStringLiteral("captures"), captures}};
  QFile receipt_file(QDir(output_dir).filePath(QStringLiteral("source-crop-ui-proof.json")));
  const auto receipt_bytes = QJsonDocument(receipt).toJson(QJsonDocument::Indented);
  if (!receipt_file.open(QIODevice::WriteOnly) ||
      receipt_file.write(receipt_bytes) != receipt_bytes.size()) {
    return fail("Source-crop UI proof could not write its scoped receipt");
  }
  return EXIT_SUCCESS;
}

int run_timeline_masked_blur_ui_proof_only(
    QQuickWindow* window, QObject* root,
    photo_editor::desktop::TimelineDocumentController* document,
    const QString& output_dir) {
  if (QGuiApplication::platformName() != QStringLiteral("cocoa"))
    return fail("The masked-blur UI proof requires the native Cocoa platform");
  if (!QDir().mkpath(output_dir))
    return fail("Masked-blur proof needs an output directory");

  root->setProperty("width", 1440);
  root->setProperty("height", 900);
  document->createTimelineWithSettings({{"timelineName", "Masked lower-layer Blur proof"},
                                        {"width", 640}, {"height", 480},
                                        {"frameRate", 30.0}});
  document->addVideoTrack();
  QVariant routed;
  QMetaObject::invokeMethod(root, "switchToWorkspaceIndex", Q_RETURN_ARG(QVariant, routed),
                            Q_ARG(QVariant, root->property("timelineWorkspaceIndex")));
  QObject* workspace = nullptr;
  if (!wait_for_condition([&] {
        workspace = active_workspace(root);
        return workspace != nullptr && !root->property("workspaceTransitionActive").toBool() &&
               workspace->property("timelineEditorPageLoaded").toBool();
      }, 30000)) {
    return fail("Masked-blur proof could not load the real Timeline workspace");
  }
  workspace->setProperty("sourcePanelCollapsed", true);
  workspace->setProperty("effectsPanelCollapsed", true);
  if (!wait_for_condition([&] {
        return window->isExposed() && window->rendererInterface() != nullptr &&
               window->rendererInterface()->graphicsApi() == QSGRendererInterface::Metal;
      }, 5000)) {
    return fail("The masked-blur UI pixel proof requires the Metal scene graph");
  }

  QImage lower_image(160, 120, QImage::Format_RGB32);
  for (int y = 0; y < lower_image.height(); ++y) {
    for (int x = 0; x < lower_image.width(); ++x) {
      const auto checker = ((x / 8) + (y / 8)) % 2 == 0;
      const auto color = x < lower_image.width() / 2
                             ? (checker ? QColor(QStringLiteral("#e52f35"))
                                        : QColor(QStringLiteral("#720a12")))
                             : (checker ? QColor(QStringLiteral("#285fe8"))
                                        : QColor(QStringLiteral("#091872")));
      lower_image.setPixelColor(x, y, color);
    }
  }
  QImage foreground_image(lower_image.size(), QImage::Format_RGB32);
  foreground_image.fill(Qt::white);
  const auto lower_path = QDir(output_dir).filePath("masked-blur-lower-red-blue.png");
  const auto foreground_path = QDir(output_dir).filePath("masked-blur-foreground-sentinel.png");
  if (!lower_image.save(lower_path) || !foreground_image.save(foreground_path))
    return fail("Masked-blur proof could not write its lower and foreground image fixtures");

  QVector<QPair<int, int>> video_tracks;
  const auto tracks = document->sequenceTracks();
  for (int index = 0; index < tracks.size(); ++index) {
    const auto track = tracks.at(index).toMap();
    if (track.value("trackKind").toString() == "video" &&
        track.value("trackRole").toString() != "caption") {
      video_tracks.push_back({track.value("laneIndex").toInt(), index});
    }
  }
  std::sort(video_tracks.begin(), video_tracks.end());
  if (video_tracks.size() < 2)
    return fail("Masked lower-layer Blur proof needs two video lanes");
  const auto lower_track = video_tracks.first().second;
  const auto upper_track = video_tracks.constLast().second;
  const auto target_video_track = [&](int target_row) {
    auto current_tracks = document->sequenceTracks();
    for (int index = 0; index < current_tracks.size(); ++index) {
      const auto track = current_tracks.at(index).toMap();
      if (track.value("trackKind").toString() == "video" &&
          track.value("trackRole").toString() != "caption" && index != target_row &&
          track.value("targeted").toBool()) {
        document->toggleTrackTargeted(index);
      }
    }
    current_tracks = document->sequenceTracks();
    if (target_row < 0 || target_row >= current_tracks.size()) return false;
    if (!current_tracks.at(target_row).toMap().value("targeted").toBool())
      document->toggleTrackTargeted(target_row);
    return document->sequenceTracks().at(target_row).toMap().value("targeted").toBool();
  };
  if (!target_video_track(upper_track))
    return fail("Masked Blur proof could not target the upper foreground lane");
  document->seekSequenceSeconds(0.0);
  document->addClipFromGallery("photo:masked-blur-foreground", foreground_path,
                               "White foreground sentinel", 4.0,
                               QUrl::fromLocalFile(foreground_path).toString());
  const auto foreground_id = document->currentClip().value("clipId").toString();
  document->setSelectedClipOpacity(0.25);
  if (!target_video_track(lower_track))
    return fail("Masked Blur proof could not target the lower background lane");
  document->seekSequenceSeconds(0.0);
  document->addClipFromGallery("photo:masked-blur-background", lower_path,
                               "Masked background", 4.0,
                               QUrl::fromLocalFile(lower_path).toString());
  const auto background_id = document->currentClip().value("clipId").toString();
  document->setSelectedFrameMode("stretch");
  document->addSelectedClipEffect(QStringLiteral("Blur"));
  document->setSelectedClipEffectAmount(QStringLiteral("Blur"), 0.0);
  const QVariantMap blur_mask{
      {QStringLiteral("enabled"), true}, {QStringLiteral("shape"), QStringLiteral("rectangle")},
      {QStringLiteral("centerX"), 0.5}, {QStringLiteral("centerY"), 0.5},
      {QStringLiteral("width"), 0.4}, {QStringLiteral("height"), 0.8},
      {QStringLiteral("feather"), 0.0}, {QStringLiteral("inverted"), false}};
  document->setSelectedClipEffectMask(QStringLiteral("Blur"), blur_mask);

  int foreground_index = -1;
  int flattened_count = 0;
  for (const auto& track_value : document->sequenceTracks())
    flattened_count += track_value.toMap().value("clips").toList().size();
  for (int index = 0; index < flattened_count; ++index) {
    if (document->timelineClipDetails(index).value("clipId").toString() == foreground_id) {
      foreground_index = index;
      break;
    }
  }
  if (foreground_id.isEmpty() || background_id.isEmpty() || foreground_index < 0)
    return fail("Masked Blur proof must retain both composited picture layers");
  document->selectClip(foreground_index);
  if (document->programClip().value("clipId").toString() != foreground_id)
    return fail("The foreground sentinel must be the active Program clip above the masked layer");
  document->seekSequenceSeconds(0.5);

  auto* monitor = find_visible_named_item(workspace, "timelineProgramMonitor");
  if (!wait_for_condition([&] {
        monitor = find_visible_named_item(workspace, "timelineProgramMonitor");
        auto* image = find_visible_named_item(workspace, "timelineProgramBackgroundPhotoImage");
        return item_visible(monitor) && item_visible(image) &&
               image->property("status").toInt() == 1 &&
               image->property("sourceSize").toSize() == lower_image.size();
      }, 12000)) {
    return fail("The lower still image must decode in the ordinary Program compositor before Blur sampling");
  }
  auto* image = find_visible_named_item(workspace, "timelineProgramBackgroundPhotoImage");
  if (image->property("source").toUrl().toLocalFile() != lower_path)
    return fail("The visible background photo must be the lower-layer red/blue fixture");

  const auto grab_after_rendered_frames = [window](int frame_count) {
    QSignalSpy frames(window, &QQuickWindow::frameSwapped);
    window->requestUpdate();
    if (!wait_for_condition([&] {
          if (frames.size() >= frame_count) return true;
          // A paused scene may stop rendering after its first update. Request
          // each frame explicitly rather than relying on an unrelated animation.
          window->requestUpdate();
          return false;
        }, 5000))
      return QImage{};
    process_layout_events(4);
    return window->grabWindow();
  };
  const auto sample_monitor = [window, monitor](const QImage& frame, double x, double y) {
    const auto scene = monitor->mapToScene(QPointF(monitor->width() * x, monitor->height() * y));
    const auto px = qBound(0, qRound(scene.x() * frame.width() / window->width()),
                           frame.width() - 1);
    const auto py = qBound(0, qRound(scene.y() * frame.height() / window->height()),
                           frame.height() - 1);
    return frame.pixelColor(px, py);
  };
  const auto channel_distance = [](const QColor& left, const QColor& right) {
    return std::abs(left.red() - right.red()) +
           std::abs(left.green() - right.green()) +
           std::abs(left.blue() - right.blue());
  };
  struct BlurProbe final {
    double x = 0.0;
    double y = 0.0;
    int control_delta = -1;
    QColor baseline;
    QColor unmasked_reference;
  };
  const auto strongest_interior_blur_probe = [sample_monitor, channel_distance](
                                                  const QImage& baseline,
                                                  const QImage& unmasked) {
    BlurProbe best;
    // The lower fixture is a 160x120 image with 8px checker cells. Sample
    // deterministic cell centers strictly inside the rectangular mask so the
    // stimulus is not dominated by monitor resampling at checker boundaries.
    for (int tile_y = 2; tile_y <= 12; ++tile_y) {
      for (int tile_x = 6; tile_x <= 13; ++tile_x) {
        const auto x = (static_cast<double>(tile_x) + 0.5) / 20.0;
        const auto y = (static_cast<double>(tile_y) + 0.5) / 15.0;
        const auto original = sample_monitor(baseline, x, y);
        const auto global_blur = sample_monitor(unmasked, x, y);
        const auto delta = channel_distance(original, global_blur);
        if (delta > best.control_delta) {
          best = {x, y, delta, original, global_blur};
        }
      }
    }
    return best;
  };
  const auto background_model_clip = [workspace](const QString& clip_id) {
    for (const auto& value : workspace->property("programBackgroundLayerModel").toList()) {
      const auto clip = value.toMap();
      if (clip.value("clipId").toString() == clip_id) return clip;
    }
    return QVariantMap{};
  };
  const auto blur_detail = [](const QVariantMap& clip) {
    for (const auto& value : clip.value("effectDetails").toList()) {
      const auto detail = value.toMap();
      if (detail.value("name").toString() == QStringLiteral("Blur")) return detail;
    }
    return QVariantMap{};
  };
  const auto before = grab_after_rendered_frames(2);
  if (before.isNull() ||
      !before.save(QDir(output_dir).filePath("timeline-masked-blur-before.png")))
    return fail("The unblurred secondary-layer Program frame must be rendered and capturable");
  const auto before_left = sample_monitor(before, 0.125, 0.542);
  const auto before_center = sample_monitor(before, 0.525, 0.542);
  const auto before_right = sample_monitor(before, 0.875, 0.542);
  if (before_left.red() < before_left.blue() + 60 ||
      before_right.blue() < before_right.red() + 50)
    return fail("The foreground sentinel must leave both red and blue lower-layer pixels visible");

  int background_index = -1;
  for (int index = 0; index < flattened_count; ++index) {
    if (document->timelineClipDetails(index).value("clipId").toString() == background_id) {
      background_index = index;
      break;
    }
  }
  if (background_index < 0) return fail("Masked Blur proof lost its lower-layer clip");
  auto unmasked_blur_mask = blur_mask;
  unmasked_blur_mask.insert(QStringLiteral("enabled"), false);
  document->selectClip(background_index);
  document->setSelectedClipEffectAmount(QStringLiteral("Blur"), 1.0);
  document->setSelectedClipEffectMask(QStringLiteral("Blur"), unmasked_blur_mask);
  document->selectClip(foreground_index);
  if (document->programClip().value("clipId").toString() != foreground_id)
    return fail("Applying lower-layer Blur must retain the active foreground Program clip");
  auto* global_photo_effect = find_visible_named_item(
      workspace, "timelineProgramBackgroundPhotoGlobalEffect");
  QVariant global_photo_blur_amount;
  const auto read_photo_blur_amount = [&] {
    global_photo_blur_amount.clear();
    QMetaObject::invokeMethod(
        workspace, "clipEffectBlur", Q_RETURN_ARG(QVariant, global_photo_blur_amount),
        Q_ARG(QVariant, QVariant::fromValue(document->timelineClipDetails(background_index))));
  };
  QVariantMap unmasked_photo_model_clip;
  QVariantMap unmasked_photo_model_blur;
  if (!wait_for_condition([&] {
        global_photo_effect = find_visible_named_item(
            workspace, "timelineProgramBackgroundPhotoGlobalEffect");
        read_photo_blur_amount();
        unmasked_photo_model_clip = background_model_clip(background_id);
        unmasked_photo_model_blur = blur_detail(unmasked_photo_model_clip);
        return item_visible(global_photo_effect) &&
               global_photo_effect->property("blurEnabled").toBool() &&
               std::abs(global_photo_effect->property("blur").toDouble() - 1.0) < 0.001 &&
               std::abs(global_photo_blur_amount.toDouble() - 1.0) < 0.001 &&
               unmasked_photo_model_clip.value("clipId").toString() == background_id &&
               std::abs(unmasked_photo_model_blur.value("amount").toDouble() - 1.0) < 0.001 &&
               !unmasked_photo_model_blur.value("mask").toMap().value("enabled").toBool();
      }, 5000)) {
    const auto clip = document->timelineClipDetails(background_index);
    std::cerr << "Unmasked still Blur route diagnostics: clip="
              << QJsonDocument::fromVariant(clip).toJson(QJsonDocument::Compact).toStdString()
              << " globalEffect=" << (global_photo_effect != nullptr)
              << " visible=" << item_visible(global_photo_effect)
              << " blurEnabled=" << (global_photo_effect ? global_photo_effect->property("blurEnabled").toBool() : false)
              << " blur=" << (global_photo_effect ? global_photo_effect->property("blur").toDouble() : -1.0)
              << " computedBlur=" << global_photo_blur_amount.toDouble()
              << " modelClip="
              << QJsonDocument::fromVariant(unmasked_photo_model_clip).toJson(QJsonDocument::Compact).toStdString()
              << " controllerClip="
              << QJsonDocument::fromVariant(clip).toJson(QJsonDocument::Compact).toStdString() << '\n';
    return fail("Disabling the Blur mask must route its amount to the ordinary lower-photo global Blur stage");
  }
  const auto unmasked = grab_after_rendered_frames(3);
  if (unmasked.isNull() ||
      !unmasked.save(QDir(output_dir).filePath("timeline-masked-blur-unmasked-control.png")))
    return fail("The unmasked Blur negative control must be rendered and capturable");
  const auto unmasked_left = sample_monitor(unmasked, 0.125, 0.542);
  const auto unmasked_right = sample_monitor(unmasked, 0.875, 0.542);
  const auto unmasked_outside_left_delta = channel_distance(before_left, unmasked_left);
  const auto unmasked_outside_right_delta = channel_distance(before_right, unmasked_right);
  if (unmasked_outside_left_delta <= 24 && unmasked_outside_right_delta <= 24)
    return fail("The textured lower-layer negative control must detect global Blur outside the mask");
  const auto photo_probe = strongest_interior_blur_probe(before, unmasked);
  if (photo_probe.control_delta < 55) {
    std::cerr << "Still Blur interior stimulus diagnostics: probe=" << photo_probe.x << ','
              << photo_probe.y << " globalBlurDelta=" << photo_probe.control_delta
              << " baseline=" << photo_probe.baseline.name().toStdString()
              << " unmasked=" << photo_probe.unmasked_reference.name().toStdString() << '\n';
    return fail("The still global-Blur reference must change an interior mask sample by at least 55");
  }
  document->selectClip(background_index);
  document->setSelectedClipEffectMask(QStringLiteral("Blur"), blur_mask);
  document->selectClip(foreground_index);
  auto* masked_blur = find_visible_named_item(
      workspace, "timelineProgramBackgroundMaskedBlurEffect");
  if (!wait_for_condition([&] {
        masked_blur = find_visible_named_item(
            workspace, "timelineProgramBackgroundMaskedBlurEffect");
        return item_visible(masked_blur) &&
               masked_blur->property("maskedBlurEnabled").toBool() &&
               std::abs(masked_blur->property("blurAmount").toDouble() - 1.0) < 0.001;
      }, 12000)) {
    const auto layers = workspace->property("programBackgroundLayerModel").toList();
    std::cerr << "Masked Blur compositor diagnostics: effect=" << (masked_blur != nullptr)
              << " visible=" << item_visible(masked_blur)
              << " amount=" << (masked_blur ? masked_blur->property("blurAmount").toDouble() : -1.0)
              << " enabled=" << (masked_blur ? masked_blur->property("maskedBlurEnabled").toBool() : false)
              << " backgroundLayers=" << layers.size()
              << " foreground=" << document->programClip().value("clipId").toString().toStdString()
              << " lower=" << background_id.toStdString() << '\n';
    return fail("The real lower-layer mask compositor must receive the enabled Blur amount");
  }
  const auto after = grab_after_rendered_frames(3);
  if (after.isNull() ||
      !after.save(QDir(output_dir).filePath("timeline-masked-blur-after.png")))
    return fail("The masked lower-layer Program frame must be rendered and capturable");
  const auto after_left = sample_monitor(after, 0.125, 0.542);
  const auto after_center = sample_monitor(after, 0.525, 0.542);
  const auto after_right = sample_monitor(after, 0.875, 0.542);
  const auto outside_left_delta = channel_distance(before_left, after_left);
  const auto outside_right_delta = channel_distance(before_right, after_right);
  const auto photo_probe_after = sample_monitor(after, photo_probe.x, photo_probe.y);
  const auto inside_probe_delta = channel_distance(photo_probe.baseline, photo_probe_after);
  const auto inside_probe_reference_delta =
      channel_distance(photo_probe.unmasked_reference, photo_probe_after);
  if (outside_left_delta > 24 || outside_right_delta > 24 || inside_probe_delta < 55 ||
      inside_probe_reference_delta > 24) {
    std::cerr << "Masked Blur pixels before left/center/right="
              << before_left.name().toStdString() << '/' << before_center.name().toStdString()
              << '/' << before_right.name().toStdString() << " after="
              << after_left.name().toStdString() << '/' << after_center.name().toStdString()
              << '/' << after_right.name().toStdString() << " deltas=" << outside_left_delta
              << '/' << inside_probe_delta << '/' << outside_right_delta
              << " probe=" << photo_probe.x << ',' << photo_probe.y
              << " globalReferenceDelta=" << photo_probe.control_delta
              << " maskedReferenceDelta=" << inside_probe_reference_delta << '\n';
    return fail("Masked Blur must alter pixels inside the mask while keeping both outside samples unchanged");
  }

  const auto lower_video_path =
      QDir(output_dir).filePath(QStringLiteral("masked-blur-lower-video.mp4"));
  if (!write_timeline_proof_video_file(lower_path, lower_video_path))
    return fail("Masked video Blur proof could not encode its textured source frame");
  document->createTimelineWithSettings({{"timelineName", "Masked lower-video Blur proof"},
                                        {"width", 640}, {"height", 480},
                                        {"frameRate", 30.0}});
  document->addVideoTrack();
  QVector<QPair<int, int>> video_blur_tracks;
  const auto video_blur_track_values = document->sequenceTracks();
  for (int index = 0; index < video_blur_track_values.size(); ++index) {
    const auto track = video_blur_track_values.at(index).toMap();
    if (track.value("trackKind").toString() == QStringLiteral("video") &&
        track.value("trackRole").toString() != QStringLiteral("caption")) {
      video_blur_tracks.push_back({track.value("laneIndex").toInt(), index});
    }
  }
  std::sort(video_blur_tracks.begin(), video_blur_tracks.end());
  if (video_blur_tracks.size() < 2)
    return fail("Masked video Blur proof needs separate foreground and background lanes");
  const auto foreground_video_track = video_blur_tracks.constLast().second;
  const auto background_video_track = video_blur_tracks.first().second;
  if (!target_video_track(foreground_video_track))
    return fail("Masked video Blur proof could not target the upper foreground lane");
  document->seekSequenceSeconds(0.0);
  document->addClipFromGallery("photo:masked-video-foreground", foreground_path,
                               "White foreground sentinel", 4.0,
                               QUrl::fromLocalFile(foreground_path).toString());
  const auto foreground_video_id = document->currentClip().value("clipId").toString();
  document->setSelectedClipOpacity(0.25);
  if (!target_video_track(background_video_track))
    return fail("Masked video Blur proof could not target the lower background lane");
  document->seekSequenceSeconds(0.0);
  document->addClipFromGallery("video:masked-video-background", lower_video_path,
                               "Masked background video", 4.0,
                               QUrl::fromLocalFile(lower_path).toString());
  const auto background_video_id = document->currentClip().value("clipId").toString();
  document->setSelectedFrameMode("stretch");
  document->addSelectedClipEffect(QStringLiteral("Blur"));
  document->setSelectedClipEffectAmount(QStringLiteral("Blur"), 0.0);
  document->setSelectedClipEffectMask(QStringLiteral("Blur"), blur_mask);
  int foreground_video_index = -1;
  int background_video_index = -1;
  int video_blur_clip_count = 0;
  for (const auto& track_value : document->sequenceTracks())
    video_blur_clip_count += track_value.toMap().value("clips").toList().size();
  for (int index = 0; index < video_blur_clip_count; ++index) {
    const auto clip_id = document->timelineClipDetails(index).value("clipId").toString();
    if (clip_id == foreground_video_id) foreground_video_index = index;
    if (clip_id == background_video_id) background_video_index = index;
  }
  if (foreground_video_id.isEmpty() || background_video_id.isEmpty() ||
      foreground_video_index < 0 || background_video_index < 0)
    return fail("Masked video Blur proof must retain both composited tracks");
  document->selectClip(foreground_video_index);
  if (document->programClip().value("clipId").toString() != foreground_video_id)
    return fail("The upper still must remain the active Program clip over the lower video");
  document->seekSequenceSeconds(0.5);
  document->playSequence();
  auto* background_player = find_named_object(workspace, "timelineProgramBackgroundMediaPlayer");
  auto* background_output = find_visible_named_item(workspace, "timelineProgramBackgroundVideo");
  auto* background_video_effect = find_visible_named_item(
      workspace, "timelineProgramBackgroundMaskedBlurEffect");
  auto* background_video_effect_source = find_named_item(
      workspace, "timelineProgramBackgroundSourceCrop");
  if (!wait_for_condition([&] {
        background_player = find_named_object(workspace, "timelineProgramBackgroundMediaPlayer");
        background_output = find_visible_named_item(workspace, "timelineProgramBackgroundVideo");
        background_video_effect = find_visible_named_item(
            workspace, "timelineProgramBackgroundMaskedBlurEffect");
        background_video_effect_source = find_named_item(
            workspace, "timelineProgramBackgroundSourceCrop");
        return background_player != nullptr && background_player->property("hasVideo").toBool() &&
               item_visible(background_output) && item_visible(background_video_effect_source) &&
               background_video_effect_source->width() > 0 &&
               background_video_effect_source->height() > 0;
      }, 12000)) {
    document->pauseSequence();
    return fail("The lower video must decode through the ordinary secondary-layer compositor");
  }
  if (!program_text_effect_state_matches(workspace, background_video_id, false, false))
    return fail("A secondary video layer must not instantiate unused title shadow or stroke delegates");
  const QPointer<QObject> stable_background_video_player(background_player);
  QTest::qWait(350);
  document->pauseSequence();
  process_layout_events(4);
  const auto before_video = grab_after_rendered_frames(2);
  if (before_video.isNull() ||
      !before_video.save(QDir(output_dir).filePath("timeline-masked-video-blur-before.png")))
    return fail("The unblurred lower-video Program frame must be rendered and capturable");
  const auto before_video_left = sample_monitor(before_video, 0.125, 0.542);
  const auto before_video_center = sample_monitor(before_video, 0.525, 0.542);
  const auto before_video_right = sample_monitor(before_video, 0.875, 0.542);
  if (before_video_left.red() < before_video_left.blue() + 30 ||
      before_video_right.blue() < before_video_right.red() + 25)
    return fail("The foreground sentinel must preserve both red and blue lower-video pixels");
  document->selectClip(background_video_index);
  document->setSelectedClipEffectAmount(QStringLiteral("Blur"), 1.0);
  document->setSelectedClipEffectMask(QStringLiteral("Blur"), unmasked_blur_mask);
  document->selectClip(foreground_video_index);
  if (document->programClip().value("clipId").toString() != foreground_video_id)
    return fail("Applying lower-video Blur must retain the active foreground Program clip");
  auto* global_video_effect = find_visible_named_item(
      workspace, "timelineProgramBackgroundVideoGlobalEffect");
  QVariant global_video_blur_amount;
  const auto read_video_blur_amount = [&] {
    global_video_blur_amount.clear();
    QMetaObject::invokeMethod(
        workspace, "clipEffectBlur", Q_RETURN_ARG(QVariant, global_video_blur_amount),
        Q_ARG(QVariant, QVariant::fromValue(document->timelineClipDetails(background_video_index))));
  };
  QVariantMap unmasked_video_model_clip;
  QVariantMap unmasked_video_model_blur;
  if (!wait_for_condition([&] {
        background_player = find_named_object(workspace, "timelineProgramBackgroundMediaPlayer");
        global_video_effect = find_visible_named_item(
            workspace, "timelineProgramBackgroundVideoGlobalEffect");
        read_video_blur_amount();
        unmasked_video_model_clip = background_model_clip(background_video_id);
        unmasked_video_model_blur = blur_detail(unmasked_video_model_clip);
        return stable_background_video_player &&
               background_player == stable_background_video_player.data() &&
               stable_background_video_player->property("hasVideo").toBool() &&
               item_visible(global_video_effect) &&
               global_video_effect->property("blurEnabled").toBool() &&
               std::abs(global_video_effect->property("blur").toDouble() - 1.0) < 0.001 &&
               std::abs(global_video_blur_amount.toDouble() - 1.0) < 0.001 &&
               unmasked_video_model_clip.value("clipId").toString() == background_video_id &&
               std::abs(unmasked_video_model_blur.value("amount").toDouble() - 1.0) < 0.001 &&
               !unmasked_video_model_blur.value("mask").toMap().value("enabled").toBool();
      }, 5000)) {
    const auto clip = document->timelineClipDetails(background_video_index);
    std::cerr << "Unmasked video Blur route diagnostics: clip="
              << QJsonDocument::fromVariant(clip).toJson(QJsonDocument::Compact).toStdString()
              << " globalEffect=" << (global_video_effect != nullptr)
              << " visible=" << item_visible(global_video_effect)
              << " blurEnabled=" << (global_video_effect ? global_video_effect->property("blurEnabled").toBool() : false)
              << " blur=" << (global_video_effect ? global_video_effect->property("blur").toDouble() : -1.0)
              << " computedBlur=" << global_video_blur_amount.toDouble()
              << " stablePlayer=" << stable_background_video_player.data()
              << " currentPlayer=" << background_player
              << " playerHasVideo=" << (background_player != nullptr
                                             && background_player->property("hasVideo").toBool())
              << " modelClip="
              << QJsonDocument::fromVariant(unmasked_video_model_clip).toJson(QJsonDocument::Compact).toStdString()
              << " controllerClip="
              << QJsonDocument::fromVariant(clip).toJson(QJsonDocument::Compact).toStdString() << '\n';
    return fail("Disabling the Blur mask must route its amount to the ordinary lower-video global Blur stage");
  }
  const auto unmasked_video = grab_after_rendered_frames(3);
  if (unmasked_video.isNull() ||
      !unmasked_video.save(QDir(output_dir).filePath("timeline-masked-video-blur-unmasked-control.png")))
    return fail("The unmasked lower-video Blur negative control must be rendered and capturable");
  const auto unmasked_video_left = sample_monitor(unmasked_video, 0.125, 0.542);
  const auto unmasked_video_right = sample_monitor(unmasked_video, 0.875, 0.542);
  const auto unmasked_video_outside_left_delta =
      channel_distance(before_video_left, unmasked_video_left);
  const auto unmasked_video_outside_right_delta =
      channel_distance(before_video_right, unmasked_video_right);
  if (unmasked_video_outside_left_delta <= 24 && unmasked_video_outside_right_delta <= 24)
    return fail("The lower-video negative control must detect global Blur outside the mask");
  const auto video_probe = strongest_interior_blur_probe(before_video, unmasked_video);
  if (video_probe.control_delta < 55) {
    std::cerr << "Video Blur interior stimulus diagnostics: probe=" << video_probe.x << ','
              << video_probe.y << " globalBlurDelta=" << video_probe.control_delta
              << " baseline=" << video_probe.baseline.name().toStdString()
              << " unmasked=" << video_probe.unmasked_reference.name().toStdString() << '\n';
    return fail("The video global-Blur reference must change an interior mask sample by at least 55");
  }
  document->selectClip(background_video_index);
  document->setSelectedClipEffectMask(QStringLiteral("Blur"), blur_mask);
  document->selectClip(foreground_video_index);
  if (!wait_for_condition([&] {
        background_player = find_named_object(workspace, "timelineProgramBackgroundMediaPlayer");
        background_video_effect = find_visible_named_item(
            workspace, "timelineProgramBackgroundMaskedBlurEffect");
        return stable_background_video_player &&
               background_player == stable_background_video_player.data() &&
               stable_background_video_player->property("hasVideo").toBool() &&
               item_visible(background_video_effect) &&
               background_video_effect->property("maskedBlurEnabled").toBool() &&
               std::abs(background_video_effect->property("blurAmount").toDouble() - 1.0) < 0.001;
      }, 12000)) {
    std::cerr << "Masked video player lifetime diagnostics: stablePlayer="
              << stable_background_video_player.data() << " currentPlayer="
              << background_player << " hasVideo="
              << (background_player != nullptr
                      && background_player->property("hasVideo").toBool())
              << " maskEffect=" << background_video_effect
              << " visible=" << item_visible(background_video_effect)
              << " amount=" << (background_video_effect != nullptr
                                    ? background_video_effect->property("blurAmount").toDouble()
                                    : -1.0) << '\n';
    return fail("The real lower-video mask compositor must receive its enabled Blur amount");
  }
  const auto after_video = grab_after_rendered_frames(3);
  if (after_video.isNull() ||
      !after_video.save(QDir(output_dir).filePath("timeline-masked-video-blur-after.png")))
    return fail("The masked lower-video Program frame must be rendered and capturable");
  const auto after_video_left = sample_monitor(after_video, 0.125, 0.542);
  const auto after_video_center = sample_monitor(after_video, 0.525, 0.542);
  const auto after_video_right = sample_monitor(after_video, 0.875, 0.542);
  const auto video_outside_left_delta = channel_distance(before_video_left, after_video_left);
  const auto video_outside_right_delta = channel_distance(before_video_right, after_video_right);
  const auto video_probe_after = sample_monitor(after_video, video_probe.x, video_probe.y);
  const auto video_inside_probe_delta = channel_distance(video_probe.baseline, video_probe_after);
  const auto video_inside_probe_reference_delta =
      channel_distance(video_probe.unmasked_reference, video_probe_after);
  if (video_outside_left_delta > 24 || video_outside_right_delta > 24 ||
      video_inside_probe_delta < 55 || video_inside_probe_reference_delta > 24) {
    std::cerr << "Masked video Blur pixels before left/center/right="
              << before_video_left.name().toStdString() << '/' << before_video_center.name().toStdString()
              << '/' << before_video_right.name().toStdString() << " after="
              << after_video_left.name().toStdString() << '/' << after_video_center.name().toStdString()
              << '/' << after_video_right.name().toStdString() << " deltas="
              << video_outside_left_delta << '/' << video_inside_probe_delta << '/'
              << video_outside_right_delta << " probe=" << video_probe.x << ',' << video_probe.y
              << " globalReferenceDelta=" << video_probe.control_delta
              << " maskedReferenceDelta=" << video_inside_probe_reference_delta << '\n';
    return fail("Masked video Blur must alter pixels inside the mask and preserve both outside pixels");
  }

  document->createTimelineWithSettings({{"timelineName", "Zero-valued caption style proof"},
                                        {"width", 640}, {"height", 480},
                                        {"frameRate", 30.0}});
  document->addClipFromGallery("photo:caption-zero-style", foreground_path,
                               "Caption background", 4.0,
                               QUrl::fromLocalFile(foreground_path).toString());
  document->generateCaptionsFromTranscript(QStringLiteral("zero values"));
  int caption_index = -1;
  int caption_clip_count = 0;
  for (const auto& track_value : document->sequenceTracks())
    caption_clip_count += track_value.toMap().value("clips").toList().size();
  for (int index = 0; index < caption_clip_count; ++index) {
    if (document->timelineClipDetails(index).value("mediaType").toString() == "caption") {
      caption_index = index;
      break;
    }
  }
  if (caption_index < 0) return fail("Zero-valued style proof needs a generated, editable caption");
  document->selectClip(caption_index);
  document->updateSelectedTitle({
      {QStringLiteral("text"), QStringLiteral("zero\nvalues")},
      {QStringLiteral("positionX"), 0.0}, {QStringLiteral("positionY"), 0.0},
      {QStringLiteral("safeMarginX"), 0.0}, {QStringLiteral("safeMarginY"), 0.0},
      {QStringLiteral("backgroundPadding"), 0.0}, {QStringLiteral("backgroundRadius"), 0.0},
      {QStringLiteral("strokeWidth"), 0.0},
      {QStringLiteral("shadowEnabled"), true}, {QStringLiteral("shadowOffsetX"), 0.0},
      {QStringLiteral("shadowOffsetY"), 0.0}});
  const auto caption_clip_id = document->currentClip().value("clipId").toString();
  if (caption_clip_id.isEmpty() ||
      !wait_for_condition([&] {
        return program_text_effect_state_matches(workspace, caption_clip_id, true, false);
      }, 5000))
    return fail("A caption with zero stroke width must keep its shadow and omit stroke delegates");
  auto caption_detail = document->timelineClipDetails(caption_index);
  QVariant caption_plain_text;
  if (!QMetaObject::invokeMethod(
          workspace, "programCaptionPlainText", Q_RETURN_ARG(QVariant, caption_plain_text),
          Q_ARG(QVariant, QVariant::fromValue(caption_detail))) ||
      !caption_plain_text.toString().contains(QLatin1Char('\n')))
    return fail("The Program caption plain-text path must retain authored line breaks");
  for (const auto& mode : {QStringLiteral("none"), QStringLiteral("highlight"),
                           QStringLiteral("karaoke"), QStringLiteral("pop")}) {
    caption_detail.insert(QStringLiteral("captionEmphasisMode"), mode);
    QVariant caption_rich_text;
    if (!QMetaObject::invokeMethod(
            workspace, "programCaptionRichText", Q_RETURN_ARG(QVariant, caption_rich_text),
            Q_ARG(QVariant, QVariant::fromValue(caption_detail))) ||
        !caption_rich_text.toString().contains(QStringLiteral("<br/>")))
      return fail("Program caption rich text must keep authored line breaks across emphasis modes");
  }
  caption_detail.insert(QStringLiteral("captionWords"), QVariantList{});
  QVariant title_only_rich_text;
  if (!QMetaObject::invokeMethod(
          workspace, "programCaptionRichText", Q_RETURN_ARG(QVariant, title_only_rich_text),
          Q_ARG(QVariant, QVariant::fromValue(caption_detail))) ||
      !title_only_rich_text.toString().contains(QStringLiteral("<br/>")))
    return fail("Program captions without word timing must render authored line breaks as rich-text breaks");
  QMetaObject::invokeMethod(workspace, "openInspectorTab",
                            Q_ARG(QVariant, QVariant(QStringLiteral("title"))));
  if (!wait_for_condition([&] {
        return named_item_visible(workspace, "timelineCaptionInspector") &&
               find_named_object(workspace, "timelineCaptionBackgroundPaddingSlider") != nullptr &&
               find_named_object(workspace, "timelineCaptionBackgroundRadiusSlider") != nullptr &&
               find_named_object(workspace, "timelineTitlePositionXSlider") != nullptr &&
               find_named_object(workspace, "timelineTitlePositionYSlider") != nullptr;
      }, 12000))
    return fail("Zero-valued caption style controls must load in the actual Title inspector");
  auto* caption_scroll = find_named_object(workspace, "timelineTitleCaptionScrollView");
  if (caption_scroll != nullptr) {
    const auto max_scroll = qMax(0.0, caption_scroll->property("contentHeight").toDouble() -
                                         caption_scroll->property("height").toDouble());
    caption_scroll->setProperty("contentY", max_scroll);
  }
  auto* padding_slider = find_named_object(workspace, "timelineCaptionBackgroundPaddingSlider");
  auto* radius_slider = find_named_object(workspace, "timelineCaptionBackgroundRadiusSlider");
  auto* position_x_slider = find_named_object(workspace, "timelineTitlePositionXSlider");
  auto* position_y_slider = find_named_object(workspace, "timelineTitlePositionYSlider");
  process_layout_events(8);
  if (padding_slider == nullptr || radius_slider == nullptr || position_x_slider == nullptr ||
      position_y_slider == nullptr ||
      std::abs(padding_slider->property("value").toDouble()) > 0.001 ||
      std::abs(radius_slider->property("value").toDouble()) > 0.001 ||
      padding_slider->property("valueText").toString() != "0 px" ||
      radius_slider->property("valueText").toString() != "0 px" ||
      std::abs(position_x_slider->property("value").toDouble()) > 0.001 ||
      std::abs(position_y_slider->property("value").toDouble()) > 0.001)
    return fail("Caption Inspector must display stored zero padding, radius, and position values");

  if (!grab_after_rendered_frames(3).save(
          QDir(output_dir).filePath("timeline-caption-zero-inspector.png")))
    return fail("The zero-valued caption inspector must be rendered before review");

  document->updateSelectedTitle({{QStringLiteral("strokeWidth"), 2.0}});
  if (!wait_for_condition([&] {
        return program_text_effect_state_matches(workspace, caption_clip_id, true, true);
      }, 5000))
    return fail("A caption with a visible stroke must instantiate its eight native stroke delegates");

  QMetaObject::invokeMethod(workspace, "openTranscriptEditor");
  QObject* caption_popup = nullptr;
  if (!wait_for_condition([&] {
        caption_popup = find_named_object(workspace, "timelineTranscriptEditorPopup");
        return caption_popup != nullptr && caption_popup->property("visible").toBool();
      }, 5000))
    return fail("Zero-style preview proof must open the real Caption Styles surface");
  QMetaObject::invokeMethod(workspace, "openCaptionStyles");
  const QVariantMap zero_style{
      {QStringLiteral("positionX"), 0.0}, {QStringLiteral("positionY"), 0.0},
      {QStringLiteral("safeMarginX"), 0.0}, {QStringLiteral("safeMarginY"), 0.0},
      {QStringLiteral("backgroundPadding"), 0.0}, {QStringLiteral("backgroundRadius"), 0.0},
      {QStringLiteral("shadowEnabled"), true}, {QStringLiteral("shadowOffsetX"), 0.0},
      {QStringLiteral("shadowOffsetY"), 0.0}};
  workspace->setProperty("captionStyleTemplatePreviewData", QVariantMap{
      {QStringLiteral("templateId"), QStringLiteral("native-zero-values")},
      {QStringLiteral("description"), QStringLiteral("Zero values should remain visible")},
      {QStringLiteral("summary"), QStringLiteral("Zero-value style preview")},
      {QStringLiteral("builtIn"), true}, {QStringLiteral("found"), true},
      {QStringLiteral("style"), zero_style},
      {QStringLiteral("aspectOverrides"), QVariantMap{}}});
  if (!wait_for_condition([&] {
        return named_item_visible(workspace, "timelineCaptionStylesPanel") &&
               named_item_visible(workspace, "timelineCaptionStylePreviewCard") &&
               named_item_visible(workspace, "timelineCaptionStylePreviewText");
      }, 5000))
    return fail("Caption Styles must instantiate its real preview before checking zero values");
  auto* preview_card = find_named_item(workspace, "timelineCaptionStylePreviewCard");
  auto* preview_text = find_named_item(workspace, "timelineCaptionStylePreviewText");
  auto* shadow_item = find_named_item(workspace, "timelineCaptionStylePreviewShadow");
  const auto zero_property = [](QObject* item, const char* name) {
    return item != nullptr && std::abs(item->property(name).toDouble()) <= 0.001;
  };
  if (!zero_property(preview_card, "safeX") || !zero_property(preview_card, "safeY") ||
      !zero_property(preview_card, "anchorX") || !zero_property(preview_card, "x") ||
      !zero_property(preview_card, "y") || !zero_property(preview_card, "paddingAmount") ||
      !zero_property(preview_card, "radius") ||
      preview_text == nullptr || shadow_item == nullptr ||
      std::abs(shadow_item->mapToItem(preview_card,
                                     QPointF(shadow_item->width() / 2,
                                             shadow_item->height() / 2)).x() -
               preview_card->property("width").toDouble() / 2) > 1.0 ||
      std::abs(shadow_item->mapToItem(preview_card,
                                     QPointF(shadow_item->width() / 2,
                                             shadow_item->height() / 2)).y() -
               preview_card->property("height").toDouble() / 2) > 1.0 ||
      !wait_for_condition([&] { return caption_popup->property("opened").toBool(); }, 5000) ||
      !grab_after_rendered_frames(3).save(QDir(output_dir).filePath("timeline-caption-style-zero-values.png")))
    return fail("Caption style preview must honor zero margins, position, padding, radius, and shadow offsets");

  const QJsonObject receipt{
      {QStringLiteral("proof"), QStringLiteral("timeline-masked-blur-and-zero-caption-style-ui")},
      {QStringLiteral("status"), QStringLiteral("passed")},
      {QStringLiteral("activeProgramClipId"), foreground_id},
      {QStringLiteral("maskedLowerClipId"), background_id},
      {QStringLiteral("mask"), QJsonObject{{QStringLiteral("shape"), QStringLiteral("rectangle")},
                                          {QStringLiteral("centerX"), 0.5},
                                          {QStringLiteral("centerY"), 0.5},
                                          {QStringLiteral("width"), 0.4},
                                          {QStringLiteral("height"), 0.8}}},
      {QStringLiteral("blurAmount"), 1.0},
      {QStringLiteral("beforeLeft"), before_left.name(QColor::HexArgb)},
      {QStringLiteral("beforeCenter"), before_center.name(QColor::HexArgb)},
      {QStringLiteral("beforeRight"), before_right.name(QColor::HexArgb)},
      {QStringLiteral("afterLeft"), after_left.name(QColor::HexArgb)},
      {QStringLiteral("afterCenter"), after_center.name(QColor::HexArgb)},
      {QStringLiteral("afterRight"), after_right.name(QColor::HexArgb)},
      {QStringLiteral("outsideLeftDelta"), outside_left_delta},
      {QStringLiteral("stillProbeX"), photo_probe.x},
      {QStringLiteral("stillProbeY"), photo_probe.y},
      {QStringLiteral("stillProbeBaseline"), photo_probe.baseline.name(QColor::HexArgb)},
      {QStringLiteral("stillProbeUnmaskedReference"), photo_probe.unmasked_reference.name(QColor::HexArgb)},
      {QStringLiteral("stillProbeGlobalBlurDelta"), photo_probe.control_delta},
      {QStringLiteral("stillProbeMaskedDelta"), inside_probe_delta},
      {QStringLiteral("stillProbeMaskedVsUnmaskedDelta"), inside_probe_reference_delta},
      {QStringLiteral("outsideRightDelta"), outside_right_delta},
      {QStringLiteral("unmaskedOutsideLeftDelta"), unmasked_outside_left_delta},
      {QStringLiteral("unmaskedOutsideRightDelta"), unmasked_outside_right_delta},
      {QStringLiteral("maskedLowerVideoClipId"), background_video_id},
      {QStringLiteral("videoBeforeLeft"), before_video_left.name(QColor::HexArgb)},
      {QStringLiteral("videoBeforeCenter"), before_video_center.name(QColor::HexArgb)},
      {QStringLiteral("videoBeforeRight"), before_video_right.name(QColor::HexArgb)},
      {QStringLiteral("videoAfterLeft"), after_video_left.name(QColor::HexArgb)},
      {QStringLiteral("videoAfterCenter"), after_video_center.name(QColor::HexArgb)},
      {QStringLiteral("videoAfterRight"), after_video_right.name(QColor::HexArgb)},
      {QStringLiteral("videoOutsideLeftDelta"), video_outside_left_delta},
      {QStringLiteral("videoProbeX"), video_probe.x},
      {QStringLiteral("videoProbeY"), video_probe.y},
      {QStringLiteral("videoProbeBaseline"), video_probe.baseline.name(QColor::HexArgb)},
      {QStringLiteral("videoProbeUnmaskedReference"), video_probe.unmasked_reference.name(QColor::HexArgb)},
      {QStringLiteral("videoProbeGlobalBlurDelta"), video_probe.control_delta},
      {QStringLiteral("videoProbeMaskedDelta"), video_inside_probe_delta},
      {QStringLiteral("videoProbeMaskedVsUnmaskedDelta"), video_inside_probe_reference_delta},
      {QStringLiteral("videoOutsideRightDelta"), video_outside_right_delta},
      {QStringLiteral("videoUnmaskedOutsideLeftDelta"), unmasked_video_outside_left_delta},
      {QStringLiteral("videoUnmaskedOutsideRightDelta"), unmasked_video_outside_right_delta},
      {QStringLiteral("zeroPadding"), padding_slider->property("value").toDouble()},
      {QStringLiteral("zeroRadius"), radius_slider->property("value").toDouble()},
      {QStringLiteral("zeroPositionX"), position_x_slider->property("value").toDouble()},
      {QStringLiteral("zeroPositionY"), position_y_slider->property("value").toDouble()},
      {QStringLiteral("zeroStylePreviewSafeMarginsPositionPaddingRadiusShadow"), true}};
  QFile receipt_file(QDir(output_dir).filePath(QStringLiteral("timeline-masked-blur-ui-proof.json")));
  const auto receipt_bytes = QJsonDocument(receipt).toJson(QJsonDocument::Indented);
  if (!receipt_file.open(QIODevice::WriteOnly) ||
      receipt_file.write(receipt_bytes) != receipt_bytes.size())
    return fail("Masked Blur proof could not write its scoped pixel receipt");
  return EXIT_SUCCESS;
}

int run_timeline_crop_ui_proof_only(
    QQuickWindow* window, QObject* root,
    photo_editor::desktop::TimelineDocumentController* document,
    photo_editor::desktop::TimelinePlaybackController* playback,
    const QString& output_dir) {
  if (QGuiApplication::platformName() != QStringLiteral("cocoa"))
    return fail("The crop-only UI proof requires the native Cocoa platform");
  if (!QDir().mkpath(output_dir)) return fail("Crop-only proof needs an output directory");
  root->setProperty("width", 1440);
  root->setProperty("height", 900);
  document->createTimelineWithSettings({{"timelineName", "Crop UI proof bootstrap"},
                                        {"width", 640}, {"height", 480},
                                        {"frameRate", 30.0}});
  QVariant routed;
  QMetaObject::invokeMethod(root, "switchToWorkspaceIndex", Q_RETURN_ARG(QVariant, routed),
                            Q_ARG(QVariant, root->property("timelineWorkspaceIndex")));
  QObject* workspace = nullptr;
  if (!wait_for_condition([&] {
        workspace = active_workspace(root);
        return workspace != nullptr && !root->property("workspaceTransitionActive").toBool() &&
               workspace->property("timelineEditorPageLoaded").toBool();
      }, 30000)) {
    return fail("Crop-only proof could not load the real Timeline workspace");
  }
  workspace->setProperty("sourcePanelCollapsed", true);
  workspace->setProperty("effectsPanelCollapsed", true);
  process_layout_events(10);
  if (!wait_for_condition([&] {
        return window->isExposed() && window->rendererInterface() != nullptr &&
               window->rendererInterface()->graphicsApi() == QSGRendererInterface::Metal;
      }, 5000)) {
    return fail("The crop-only UI pixel proof requires the Metal scene graph");
  }
  QColor crop_center_pixel;
  if (run_timeline_source_crop_ui_section(
          window, workspace, document, playback, output_dir, &crop_center_pixel, false) != EXIT_SUCCESS) {
    return EXIT_FAILURE;
  }
  ulong timestamp = 90000;
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
  QMetaObject::invokeMethod(workspace, "handleTimelineHeaderMenuAction",
                            Q_ARG(QVariant, QStringLiteral("collect-project-package")));
  auto* delivery_popup = find_named_object(root, "timelineProjectDeliveryToolsPopup");
  if (!wait_for_condition([&] {
        return popup_content_ready_for_capture(
            delivery_popup, window, "timelineDeliveryToolsCloseButton");
      }, 5000)) {
    return fail("Save must finish opening readable, in-bounds portable-project tools");
  }
  if (!window->grabWindow().save(QDir(output_dir).filePath("project-delivery-tools.png")) ||
      !click(window, "timelineDeliveryToolsCloseButton")) {
    return fail("Project delivery tools must be readable and dismissible");
  }
  process_layout_events(6);
  QMetaObject::invokeMethod(workspace, "handleTimelineHeaderMenuAction",
                            Q_ARG(QVariant, QStringLiteral("timeline-interchange")));
  if (!wait_for_condition([&] {
        return popup_content_ready_for_capture(
            delivery_popup, window, "timelineInterchangeCloseButton");
      }, 5000)) {
    return fail("Save must finish opening readable, in-bounds OTIO interchange");
  }
  if (!window->grabWindow().save(QDir(output_dir).filePath("timeline-interchange.png")) ||
      !click(window, "timelineInterchangeCloseButton")) {
    return fail("OTIO preview and acceptance controls must be readable and dismissible");
  }
  process_layout_events(6);
  QMetaObject::invokeMethod(workspace, "openAudioMixerPopup");
  QObject* mixer = nullptr;
  if (!wait_for_condition([&] {
        return docked_timeline_mixer_ready_for_capture(workspace, window, &mixer);
      }, 5000)) {
    return fail("The retained docked Mixer must render its real Popup content and voiceover route");
  }
  if (!click_docked_timeline_voiceover(workspace, window, click))
    return fail("The visible Mixer Voiceover button or compact Tools menu must open by pointer");
  QObject* voiceover_popup = nullptr;
  if (!wait_for_condition([&] {
        voiceover_popup = find_named_object(root, "timelineVoiceoverPanelPopup");
        return popup_content_ready_for_capture(
            voiceover_popup, window, "timelineVoiceoverInputDevice");
      }, 5000) ||
      !wait_for_condition([&] {
        auto* device = find_visible_named_item(window, "timelineVoiceoverInputDevice");
        return item_visible(device) && device->isEnabled();
      }, 5000)) {
    return fail("Voiceover must open device and pre-roll controls");
  }
  if (!window->grabWindow().save(QDir(output_dir).filePath("voiceover-controls.png"))) {
    return fail("Voiceover controls must be capturable without starting capture");
  }
  QTest::keyClick(window, Qt::Key_Escape);
  if (!wait_for_condition([&] { return !voiceover_popup->property("visible").toBool(); }, 3000) ||
      !QMetaObject::invokeMethod(mixer, "close") ||
      !QMetaObject::invokeMethod(workspace, "closeTimelinePanel",
                                  Q_ARG(QVariant, QVariant(QStringLiteral("mixer")))))
    return fail("Voiceover and the docked Mixer must close through their active surfaces");
  if (!wait_for_condition([&] {
        bool visible = true;
        return timeline_panel_visible_state(workspace, QStringLiteral("mixer"), &visible) &&
               !visible && !mixer->property("visible").toBool();
      }, 3000))
    return fail("The docked Mixer must leave its panel state closed after the voiceover check");
  process_layout_events(6);
  std::cout << "Timeline source-crop UI proof passed: Cocoa/Metal Program drag, Undo, exact inspector percentage/reset, secondary video crop pixels="
            << crop_center_pixel.name(QColor::HexArgb).toStdString()
            << "; native shortcut proof remains unqualified. Receipt: "
            << QDir(output_dir).filePath(QStringLiteral("source-crop-ui-proof.json")).toStdString()
            << '\n';
  return EXIT_SUCCESS;
}

int run_timeline_animation_ui_proof(
    QQuickWindow* window, QObject* root,
    photo_editor::desktop::AppShellController* app_shell,
    photo_editor::desktop::TimelineDocumentController* document,
    photo_editor::desktop::TimelinePlaybackController* playback,
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
  QObject* workspace = nullptr;
  if (!wait_for_condition([&] {
        workspace = active_workspace(root);
        return workspace && !root->property("workspaceTransitionActive").toBool() &&
               workspace->property("timelineEditorPageLoaded").toBool() &&
               named_item_visible(workspace, "timelineProgramAnimationToolbar");
      }, 30000))
    return fail("Selecting a video must load the Timeline workspace and animation controls");
  workspace->setProperty("programTransformControlsVisible", true);
  if (!wait_for_condition([&] {
        return named_item_visible(workspace, "timelineProgramTransformOverlay");
      }, 5000)) {
    const auto clip = document->currentClip();
    std::cerr << "Program transform overlay diagnostics: controls="
              << workspace->property("programTransformControlsVisible").toBool()
              << " playing=" << workspace->property("sequencePlaying").toBool()
              << " clipId=" << clip.value("clipId").toString().toStdString()
              << " mediaType=" << clip.value("mediaType").toString().toStdString()
              << " start=" << clip.value("startSeconds").toDouble()
              << " end=" << clip.value("endSeconds").toDouble() << '\n';
    return fail("Enabling Program transform controls for the selected video must expose its overlay");
  }
  root->setProperty("width", 1440);
  root->setProperty("height", 900);
  workspace->setProperty("sourcePanelCollapsed", true);
  workspace->setProperty("effectsPanelCollapsed", true);
  activate_native_test_application();
  window->requestActivate();
  if (QGuiApplication::platformName() == QStringLiteral("cocoa") &&
      !wait_for_condition([&] { return window->isActive(); }, 5000))
    return fail("Native shortcut proof requires the test application window to be active");
  process_layout_events(10);
  const auto original_shortcuts = app_shell->shortcutOverrides();
  if (!app_shell->setShortcutOverride("timeline-razor", "Q"))
    return fail("Timeline tool remapping must accept an unused key");
  auto* workspace_item = qobject_cast<QQuickItem*>(workspace);
  if (!workspace_item) return fail("Timeline workspace must expose a focusable item");
  workspace_item->forceActiveFocus();
  process_layout_events(4);
  document->setActiveTool("select");
  QTest::keyClick(window, Qt::Key_C);
  process_layout_events(2);
  if (document->activeTool() != "select")
    return fail("The old razor shortcut must stop firing immediately after remapping");
  QTest::keyClick(window, Qt::Key_Q);
  process_layout_events(2);
  if (document->activeTool() != "razor") {
    QVariant configured;
    QVariant enabled;
    QMetaObject::invokeMethod(workspace, "configuredTimelineShortcut", Q_RETURN_ARG(QVariant, configured),
                              Q_ARG(QVariant, QStringLiteral("timeline-razor")), Q_ARG(QVariant, QStringLiteral("C")));
    QMetaObject::invokeMethod(workspace, "shortcutsEnabled", Q_RETURN_ARG(QVariant, enabled));
    std::cerr << "Shortcut proof: active=" << window->isActive()
              << " configured=" << configured.toString().toStdString()
              << " enabled=" << enabled.toBool()
              << " focus=" << (window->activeFocusItem() ? window->activeFocusItem()->objectName().toStdString() : "none")
              << " bindings=" << QJsonDocument::fromVariant(app_shell->shortcutOverrides()).toJson(QJsonDocument::Compact).toStdString() << '\n';
    return fail("The remapped key must activate the real Timeline razor tool");
  }
  document->setActiveTool("select");
  auto* name_field = workspace->property("timelineNameField").value<QObject*>();
  auto* name_item = qobject_cast<QQuickItem*>(name_field);
  if (!name_item) return fail("Timeline name input must be available for text-focus proof");
  name_item->forceActiveFocus();
  QTest::keyClick(window, Qt::Key_Q);
  process_layout_events(2);
  if (document->activeTool() != "select")
    return fail("Typing a remapped key in a text field must not switch Timeline tools");
  if (!app_shell->setShortcutOverrides(original_shortcuts))
    return fail("The original keymap must restore atomically after remapping proof");
  workspace_item->forceActiveFocus();
  QMetaObject::invokeMethod(root, "openKeyboardShortcutsDialog");
  if (!wait_for_condition([&] {
        auto* dialog = find_named_object(root, "keyboardShortcutsDialog");
        return dialog && dialog->property("visible").toBool() &&
               dialog->property("editableShortcutCount").toInt() >= 30;
      }, 5000)) return fail("Shortcut editor must expose the live Timeline mappings");
  auto* shortcuts_dialog = find_named_object(root, "keyboardShortcutsDialog");
  shortcuts_dialog->setProperty("shortcutSearchText", "timeline");
  process_layout_events(8);
  if (!window->grabWindow().save(QDir(output_dir).filePath("keyboard-shortcuts-timeline.png")))
    return fail("Timeline shortcut editor must be capturable");
  if (!QMetaObject::invokeMethod(shortcuts_dialog, "close") ||
      !wait_for_condition([&] {
        return !shortcuts_dialog->property("visible").toBool();
      }, 5000)) {
    std::cerr << "Shortcut dialog did not leave the Program input surface: visible="
              << shortcuts_dialog->property("visible").toBool()
              << " opened=" << shortcuts_dialog->property("opened").toBool()
              << " opacity=" << shortcuts_dialog->property("opacity").toDouble() << '\n';
    return fail("The shortcut dialog must finish closing before Program keyframe input");
  }
  process_layout_events(3);
  workspace_item->forceActiveFocus();
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
  const auto clip_keyframe_count = [&](const QString& parameter) {
    auto count = 0;
    for (const auto& entry : document->currentClip().value("keyframes").toList()) {
      if (entry.toMap().value("parameter").toString() == parameter) ++count;
    }
    return count;
  };
  const auto report_program_keyframe_stage = [&](const QString& parameter,
                                                 QQuickItem* button,
                                                 const char* stage) {
    const auto clip = document->currentClip();
    const auto bounds = button != nullptr
        ? button->mapRectToScene(QRectF(0, 0, button->width(), button->height()))
        : QRectF{};
    std::cerr << "Program keyframe stage=" << stage
              << " parameter=" << parameter.toStdString()
              << " selected=" << workspace->property("selectedAnimatedParameter").toString().toStdString()
              << " curve=" << workspace->property("curveEditorParameter").toString().toStdString()
              << " clip=" << clip.value("clipId").toString().toStdString()
              << " index=" << document->currentIndex()
              << " playhead=" << document->sequencePlayheadSeconds()
              << " visible=" << (button != nullptr && button->isVisible())
              << " enabled=" << (button != nullptr && button->isEnabled())
              << " buttonWindow=" << (button != nullptr && button->window() == window)
              << " buttonBounds=" << bounds.x() << ',' << bounds.y() << ','
              << bounds.width() << 'x' << bounds.height()
              << " parameterKeys=" << clip_keyframe_count(parameter)
              << " allKeys="
              << QJsonDocument::fromVariant(clip.value("keyframes")).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
  };
  for (const auto& parameter_name : {"frameScale", "frameOffsetX", "frameOffsetY", "frameRotationDegrees"}) {
    const QString parameter = QString::fromLatin1(parameter_name);
    workspace->setProperty("curveEditorParameter", parameter);
    process_layout_events(6);
    auto* add_keyframe_button = find_visible_named_item(window, "timelineProgramAddKeyframeButton");
    if (workspace->property("selectedAnimatedParameter").toString() != parameter ||
        workspace->property("curveEditorParameter").toString() != parameter ||
        !item_visible(add_keyframe_button) || !add_keyframe_button->isEnabled() ||
        add_keyframe_button->window() != window) {
      report_program_keyframe_stage(parameter, add_keyframe_button, "ready-failed");
      return fail("Each animation parameter must be selected by the shared Program and curve state");
    }
    if (!click(window, "timelineProgramAddKeyframeButton")) {
      report_program_keyframe_stage(parameter, add_keyframe_button, "pointer-click-failed");
      return fail("Transform parameters must be keyframeable from Program");
    }
    if (!wait_for_condition([&] { return clip_keyframe_count(parameter) == 1; }, 1500)) {
      report_program_keyframe_stage(parameter, add_keyframe_button, "key-not-authored");
      return fail("The real Program Add Keyframe button must create one key for each selected transform parameter");
    }
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
  process_layout_events(4);
  for (const auto& parameter_name : {"frameScale", "frameOffsetX", "frameOffsetY", "frameRotationDegrees"}) {
    const QString parameter = QString::fromLatin1(parameter_name);
    if (clip_keyframe_count(parameter) != 1) {
      report_program_keyframe_stage(parameter, nullptr, "gesture-undo-lost-key");
      return fail("Undoing the Program transform gesture must preserve each Program-authored key");
    }
  }
  if (!document->setSelectedClipTransform(0.25, -0.1, 0.8, 8))
    return fail("A transform edit at the second playhead position must update the animated clip");
  workspace->setProperty("curveEditorParameter", "frameScale");
  if (clip_keyframe_count(QStringLiteral("frameScale")) < 2) {
    report_program_keyframe_stage(QStringLiteral("frameScale"), nullptr,
                                  "transform-edit-did-not-add-second-key");
    return fail("Changing an already animated transform at a later playhead must create its second key");
  }
  // Preserve full coverage of the established floating tool-window path after
  // the default editor moved into the resizable Inspector-side dock.
  workspace->setProperty("curveEditorDocked", false);
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
  if (!curve->property("fitGraphValues").toBool() ||
      qml_value(curve->property("curveKeyframes")).toList().size() < 2)
    return fail("The floating graph must load the selected clip's fitted two-point scale curve");
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
  document->updateSelectedTitle({{"text", "Made to move"},
                                 {"strokeWidth", 2.0},
                                 {"shadowEnabled", true},
                                 {"shadowOffsetX", 0.0},
                                 {"shadowOffsetY", 0.0}});
  const auto title_clip_id = document->currentClip().value("clipId").toString();
  if (title_clip_id.isEmpty() ||
      !wait_for_condition([&] {
        return program_text_effect_state_matches(workspace, title_clip_id, true, true) &&
               program_text_effects_align_to_card(workspace, title_clip_id);
      }, 5000))
    return fail("An editable stroked title must load all delegates centered on the title card");
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

  QColor crop_center_pixel;
  if (run_timeline_source_crop_ui_section(
          window, workspace, document, playback, output_dir, &crop_center_pixel, true) != EXIT_SUCCESS) {
    return EXIT_FAILURE;
  }

  // Exercise the actual tool surfaces without requesting microphone access or
  // opening a platform file picker. Backend file/recording behavior has its own
  // fixture coverage; these checks protect reachability and loaded UI state.
  QMetaObject::invokeMethod(workspace, "handleTimelineHeaderMenuAction",
                            Q_ARG(QVariant, QStringLiteral("collect-project-package")));
  auto* delivery_popup = find_named_object(root, "timelineProjectDeliveryToolsPopup");
  if (!wait_for_condition([&] {
        return popup_content_ready_for_capture(
            delivery_popup, window, "timelineDeliveryToolsCloseButton");
      }, 5000)) return fail("Save must finish opening readable, in-bounds portable-project tools");
  if (!window->grabWindow().save(QDir(output_dir).filePath("project-delivery-tools.png")) ||
      !click(window, "timelineDeliveryToolsCloseButton"))
    return fail("Project delivery tools must be readable and dismissible");
  process_layout_events(6);
  QMetaObject::invokeMethod(workspace, "handleTimelineHeaderMenuAction",
                            Q_ARG(QVariant, QStringLiteral("timeline-interchange")));
  if (!wait_for_condition([&] {
        return popup_content_ready_for_capture(
            delivery_popup, window, "timelineInterchangeCloseButton");
      }, 5000)) return fail("Save must finish opening readable, in-bounds OTIO interchange");
  if (!window->grabWindow().save(QDir(output_dir).filePath("timeline-interchange.png")) ||
      !click(window, "timelineInterchangeCloseButton"))
    return fail("OTIO preview and acceptance controls must be readable and dismissible");
  process_layout_events(6);
  QMetaObject::invokeMethod(workspace, "openAudioMixerPopup");
  QObject* mixer = nullptr;
  if (!wait_for_condition([&] {
        return docked_timeline_mixer_ready_for_capture(workspace, window, &mixer);
      }, 5000)) return fail("The retained docked Mixer must render its real Popup content and voiceover route");
  if (!click_docked_timeline_voiceover(workspace, window, click))
    return fail("The visible Mixer Voiceover button or compact Tools menu must open by pointer");
  QObject* voiceover_popup = nullptr;
  if (!wait_for_condition([&] {
        voiceover_popup = find_named_object(root, "timelineVoiceoverPanelPopup");
        return popup_content_ready_for_capture(
            voiceover_popup, window, "timelineVoiceoverInputDevice");
      }, 5000) ||
      !wait_for_condition([&] {
        auto* device = find_visible_named_item(window, "timelineVoiceoverInputDevice");
        return item_visible(device) && device->isEnabled();
      }, 5000)) return fail("Voiceover must open device and pre-roll controls");
  if (!window->grabWindow().save(QDir(output_dir).filePath("voiceover-controls.png")))
    return fail("Voiceover controls must be capturable without starting capture");
  QTest::keyClick(window, Qt::Key_Escape);
  if (!wait_for_condition([&] { return !voiceover_popup->property("visible").toBool(); }, 3000) ||
      !QMetaObject::invokeMethod(mixer, "close") ||
      !QMetaObject::invokeMethod(workspace, "closeTimelinePanel",
                                  Q_ARG(QVariant, QVariant(QStringLiteral("mixer")))))
    return fail("Voiceover and the docked Mixer must close through their active surfaces");
  if (!wait_for_condition([&] {
        bool visible = true;
        return timeline_panel_visible_state(workspace, QStringLiteral("mixer"), &visible) &&
               !visible && !mixer->property("visible").toBool();
      }, 3000))
    return fail("The docked Mixer must leave its panel state closed after the voiceover check");
  process_layout_events(6);

  if (const auto inspector_proof = run_timeline_inspector_animation_ui_proof(
          window, root, document, image_path, video, output_dir);
      inspector_proof != EXIT_SUCCESS) {
    return inspector_proof;
  }
  std::cout << "Timeline animation UI proof passed: transforms, graph, text, speed, crop, delivery, interchange and voiceover controls; secondary crop pixels="
            << crop_center_pixel.name().toStdString() << '\n';
  return EXIT_SUCCESS;
}

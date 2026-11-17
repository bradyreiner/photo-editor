int run_timeline_channels_trim_ui_proof(
    QQuickWindow* window, QObject* root,
    photo_editor::desktop::TimelineDocumentController* document,
    photo_editor::desktop::TimelinePlaybackController* playback,
    photo_editor::desktop::CatalogAssetModel* catalog,
    const QString& source_path, const QString& output_dir) {
  if (!window || !root || !document || !playback || !catalog || !QDir().mkpath(output_dir))
    return fail("Channels and trim proof needs the native desktop shell");
  root->setProperty("recentUserInput", true);
  const bool native_platform=QGuiApplication::platformName()==QStringLiteral("cocoa");
  if (native_platform) activate_native_test_application();
  window->requestActivate();
  const auto video_path = QDir(output_dir).filePath("motion-study.mp4");
  QProcess generator;
  generator.start(QStandardPaths::findExecutable("ffmpeg"), {"-hide_banner","-loglevel","error","-y",
      "-f","lavfi","-i","testsrc2=size=640x360:rate=30:duration=10",
      "-f","lavfi","-i","anullsrc=channel_layout=stereo:sample_rate=48000",
      "-t","10","-c:v","libx264","-preset","ultrafast","-crf","16","-g","30",
      "-pix_fmt","yuv420p","-c:a","aac","-shortest","-threads","1",video_path});
  if (!generator.waitForFinished(30000) || generator.exitCode()!=0) return fail("A moving, decoded source video is required");
  auto asset = make_asset("channel-trim-video", video_path);
  asset.media_type = "video";
  asset.duration_seconds = 10;
  asset.duration_text = "0:10";
  catalog->replaceAssetsForTesting({asset});
  document->createTimelineWithSettings({{"timelineName", "Channels and precision trim"},
      {"width", 640}, {"height", 360}, {"frameRate", 30.0}});
  document->addClipFromGallery("channel-trim-video", video_path, "Motion study", 10,
      QUrl::fromLocalFile(source_path).toString());
  document->selectClip(0);
  document->seekSequenceSeconds(0);
  document->toggleSelectedClipKeyframe("frameScale");
  document->toggleSelectedClipKeyframe("frameOffsetX");
  document->setSelectedFrameOffsetX(-0.4);
  document->seekSequenceSeconds(4);
  document->setSelectedFrameScale(1.4);
  document->setSelectedFrameOffsetX(0.4);
  document->seekSequenceSeconds(2);
  const auto original_keys = document->currentClip().value("keyframes");
  QVariant result;
  if (!QMetaObject::invokeMethod(root, "switchToWorkspaceIndex", Q_RETURN_ARG(QVariant, result),
      Q_ARG(QVariant, root->property("timelineWorkspaceIndex"))) || !result.toBool() ||
      !wait_for_condition([&] {
        auto* view = active_workspace(root);
        return view && view->property("timelineEditorPageLoaded").toBool();
      }, 30000)) return fail("The production Timeline must open");
  QPointer<QObject> workspace(active_workspace(root));
  const auto invoke = [&](const char* method, const QVariant& argument) {
    return QMetaObject::invokeMethod(workspace.data(), method, Q_ARG(QVariant, argument));
  };
  QMetaObject::invokeMethod(workspace.data(), "resetCurrentTimelineWorkspaceLayout");
  window->resize(1366, 900);
  window->requestActivate();
  if (!wait_for_condition([&] { return window->isExposed() && (!native_platform || window->isActive()); }, 5000))
    return fail("Channels and trim proof requires an active exposed native window");
  QJsonArray captures;
  const auto capture = [&](const char* name) {
    const auto count = captures.size();
    save_capture_if_requested(window, output_dir, QString::fromUtf8(name), &captures);
    return captures.size() == count + 1;
  };
  const auto click = [&](const QString& name) {
    QSignalSpy frames(window, &QQuickWindow::frameSwapped);
    window->update();
    if (frames.isEmpty() && !frames.wait(1000)) return false;
    auto* item = find_visible_named_item(workspace, name);
    if (!item || !item->isEnabled()) return false;
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
    process_layout_events(6);
    return true;
  };
  if (!invoke("openCurveEditor", "frameScale") ||
      !wait_for_condition([&] { return named_item_visible(workspace, "timelineKeyframeChannelsButton"); }, 5000)) return fail("Channels must be discoverable beside Value and Speed");
  auto* dock = find_named_object(workspace, "timelinePanelDockState");
  QVariant moved;
  if (!dock || !QMetaObject::invokeMethod(dock, "movePanel", Q_RETURN_ARG(QVariant, moved),
      Q_ARG(QVariant, "keyframes"), Q_ARG(QVariant, "right"), Q_ARG(QVariant, 999)) || !moved.toBool())
    return fail("The channels view must occupy the ordinary curve dock");
  process_layout_events(8);
  if (!click("timelineKeyframeChannelsButton")) return fail("The docked Channels button must accept native pointer input");
  if (!wait_for_condition([&] {
        auto* channels = find_visible_named_item(workspace, "timelineAnimationChannels");
        return channels && channels->property("channelCount").toInt() == 5 &&
            named_item_visible(workspace, "timelineAnimationLanePoint-frameScale-0") &&
            named_item_visible(workspace, "timelineAnimationLanePoint-frameOffsetX-0");
      }, 5000)) {
    auto* view=find_named_object(workspace,"timelineAnimationChannels");
    auto* state=find_named_object(workspace,"timelineKeyframeCurveWindow");
    std::cerr<<"Channel projection mode="<<(state?state->property("graphMode").toString().toStdString():"missing")
             <<" count="<<(view?view->property("channelCount").toInt():-1)
             <<" available="<<document->currentClip().value("availableKeyframeParameters").toStringList().join(',').toStdString()
             <<" active="<<window->isActive()<<" exposed="<<window->isExposed()<<'\n';
    capture("channels-projection-failure");
    return fail("Motion and opacity channels must be projected into separate real lanes");
  }
  process_layout_events(8);
  auto* channels = find_visible_named_item(workspace, "timelineAnimationChannels");
  auto* curve = find_named_object(workspace, "timelineKeyframeCurveWindow");
  const auto point_center = [&](const QString& name) {
    auto* point = find_visible_named_item(workspace, name);
    return point ? point->mapToScene(QPointF(point->width()/2, point->height()/2)) : QPointF(-1000,-1000);
  };
  for (int index = 0; index < 2; ++index) {
    const auto scale = point_center(QStringLiteral("timelineAnimationLanePoint-frameScale-%1").arg(index));
    const auto position = point_center(QStringLiteral("timelineAnimationLanePoint-frameOffsetX-%1").arg(index));
    if (scale.x() < 0 || std::abs(scale.x() - position.x()) > 1 || std::abs(scale.y() - position.y()) < 20)
      return fail("Keys at the same time must align horizontally across independently scaled channel lanes");
  }
  if (!capture("channels-aligned-curves")) return fail("The native channel layout must produce a real frame capture");
  QPointer<QQuickItem> pointer(find_visible_named_item(workspace, "timelineAnimationLanePointer-frameScale"));
  const auto start = point_center("timelineAnimationLanePoint-frameScale-0").toPoint();
  const QPoint delta(30, -5);
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
  for (int step = 1; step <= 6; ++step) QTest::mouseMove(window, start + delta*step/6, 15);
  const bool live = wait_for_condition([&] {
    return pointer && pointer->property("pressed").toBool() && document->currentClip().value("keyframes") != original_keys;
  }, 1000);
  if (!live) return fail("A channel point must publish while held without losing its lane mouse grab");
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start + delta);
  process_layout_events(6);
  const auto channel_keys = [](const QVariant& source, const QString& parameter) {
    QVariantList values;
    for (const auto& value : source.toList()) if (value.toMap().value("parameter").toString() == parameter) values.push_back(value);
    return values;
  };
  if (channel_keys(document->currentClip().value("keyframes"), "frameOffsetX") != channel_keys(original_keys, "frameOffsetX"))
    return fail("Editing Scale must preserve the neighboring position channel");
  if (curve->property("curveGestureActive").toBool()) return fail("The channel drag must close its continuous edit scope");
  if (!capture("channels-live-edit")) return fail("The edited channels must remain visibly rendered");
  document->undo();
  if (document->currentClip().value("keyframes") != original_keys) return fail("One Undo must restore the complete lane point gesture");
  QTest::keyClick(window, Qt::Key_A, Qt::MetaModifier);
  QTest::keyClick(window, Qt::Key_Right);
  process_layout_events(5);
  const auto nudged = channel_keys(document->currentClip().value("keyframes"), "frameScale");
  const auto baseline = channel_keys(original_keys, "frameScale");
  if (nudged.size() != 2 || channel_keys(document->currentClip().value("keyframes"), "frameOffsetX") != channel_keys(original_keys,"frameOffsetX"))
    return fail("Channel keyboard selection must affect only the active channel");
  for (int index = 0; index < 2; ++index)
    if (std::abs(nudged[index].toMap().value("seconds").toDouble() - baseline[index].toMap().value("seconds").toDouble() - 1.0/30) > 0.0001)
      return fail("The shared channel keyboard path must preserve authored-frame nudging");
  document->undo();
  if (document->currentClip().value("keyframes") != original_keys) return fail("A channel selection nudge must undo as one edit");
  const auto context_open = [&] {
    auto* menu=find_named_object(workspace,"timelineCurveContextMenu");
    return menu && menu->property("opened").toBool();
  };
  const auto context_closed = [&] {
    auto* menu=find_named_object(workspace,"timelineCurveContextMenu");
    return menu && !menu->property("visible").toBool();
  };
  const auto context_in_window = [&] {
    auto* menu=find_named_object(workspace,"timelineCurveContextMenu");
    auto* content=menu ? menu->property("contentItem").value<QQuickItem*>() : nullptr;
    return content && QRectF(0,0,window->width(),window->height()).adjusted(-1,-1,1,1)
        .contains(content->mapRectToScene(content->boundingRect()));
  };
  const auto right_click_point = [&](const QString& name) {
    QSignalSpy frames(window,&QQuickWindow::frameSwapped);
    window->update();
    if (frames.isEmpty() && !frames.wait(1000)) return false;
    const auto position=point_center(name);
    if (position.x()<0) return false;
    QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,position.toPoint());
    return wait_for_condition(context_open,3000);
  };
  QTest::keyClick(window,Qt::Key_A,Qt::MetaModifier);
  if (!right_click_point("timelineAnimationLanePoint-frameScale-0") ||
      curve->property("selectedSeconds").toList().size()!=2 ||
      document->currentClip().value("keyframes")!=original_keys || !context_in_window())
    return fail("Right-clicking a selected channel key must preserve the multi-key selection and open a bounded non-mutating menu");
  if (!capture("channels-context-menu") || !click("timelineCurveContextEaseOut") ||
      !wait_for_condition(context_closed,2000)) return fail("The channel easing action must be reachable through its real popup");
  if (!wait_for_condition([&] {
        auto* focus=window->activeFocusItem();
        return focus && (focus==channels || channels->isAncestorOf(focus));
      },1000)) return fail("A context action must return keyboard ownership to the invoking channel graph");
  for (const auto& key:channel_keys(document->currentClip().value("keyframes"),"frameScale"))
    if (key.toMap().value("interpolation").toString()!="ease-out") return fail("Context easing must reach every selected key in the active channel");
  if (channel_keys(document->currentClip().value("keyframes"),"frameOffsetX")!=channel_keys(original_keys,"frameOffsetX"))
    return fail("Context easing must preserve the other channel");
  document->undo();
  if (document->currentClip().value("keyframes")!=original_keys) return fail("One Undo must restore multi-key context easing");
  QTest::keyClick(window,Qt::Key_F10,Qt::ShiftModifier);
  const bool keyboard_menu_open=wait_for_condition(context_open,3000);
  const bool keyboard_copy=keyboard_menu_open && click("timelineCurveContextCopy");
  if (!keyboard_menu_open || !keyboard_copy || !wait_for_condition(context_closed,2000) || document->keyframeClipboardCount()!=2) {
    std::cerr << "Channel keyboard context open=" << keyboard_menu_open << " copy=" << keyboard_copy
              << " clipboard=" << document->keyframeClipboardCount() << " selected=" << curve->property("selectedSeconds").toList().size()
              << " active=" << window->isActive() << " focus="
              << (window->activeFocusItem() ? window->activeFocusItem()->objectName().toStdString() : "none") << '\n';
    capture("channels-keyboard-context-failure");
    return fail("Shift-F10 must open the channel menu and copy only its selected keys");
  }
  document->seekSequenceSeconds(2);
  QTest::keyClick(window,Qt::Key_F10,Qt::ShiftModifier);
  if (!wait_for_condition(context_open,3000) || !click("timelineCurveContextPaste") ||
      !wait_for_condition(context_closed,2000) || channel_keys(document->currentClip().value("keyframes"),"frameScale").size()!=4)
    return fail("The channel context menu must paste the selected curve keys at the playhead");
  document->undo();
  if (document->currentClip().value("keyframes")!=original_keys) return fail("Context paste must undo as one edit");
  auto* scale_lane=find_visible_named_item(workspace,"timelineAnimationLane-frameScale");
  auto* lane_pointer=find_visible_named_item(workspace,"timelineAnimationLanePointer-frameScale");
  const auto lane_bounds=scale_lane ? scale_lane->property("bounds").toMap() : QVariantMap{};
  if (!lane_pointer || lane_bounds.isEmpty()) return fail("The active channel must retain its real pointer surface");
  document->seekSequenceSeconds(3);
  process_layout_events(4);
  // Empty curve space at local time 2, deliberately different from the playhead.
  const double empty_x=channels->property("plotLeft").toDouble()+0.2*(lane_pointer->width()-channels->property("plotLeft").toDouble()-16);
  const auto empty_point=lane_pointer->mapToScene(QPointF(empty_x,2)).toPoint();
  QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,empty_point);
  if (!wait_for_condition(context_open,3000) || !curve->property("selectedSeconds").toList().isEmpty() ||
      !click("timelineCurveContextAdd") || !wait_for_condition(context_closed,2000))
    return fail("Empty channel space must clear stale key selection and offer Add at its clicked time");
  const auto added=channel_keys(document->currentClip().value("keyframes"),"frameScale");
  if (added.size()!=3 || std::abs(added[1].toMap().value("seconds").toDouble()-2)>0.0001)
    return fail("Context Add must use the clicked authored frame, rather than the playhead");
  document->undo();
  if (document->currentClip().value("keyframes")!=original_keys) return fail("Context Add must preserve the original curve after Undo");
  if (!right_click_point("timelineAnimationLanePoint-frameOffsetX-0") ||
      workspace->property("curveEditorParameter").toString()!="frameOffsetX" ||
      !click("timelineCurveContextHold") || !wait_for_condition(context_closed,2000))
    return fail("Right-clicking an inactive channel must target that channel's actual key");
  const auto held_position=channel_keys(document->currentClip().value("keyframes"),"frameOffsetX");
  if (held_position.first().toMap().value("interpolation").toString()!="hold" ||
      channel_keys(document->currentClip().value("keyframes"),"frameScale")!=channel_keys(original_keys,"frameScale"))
    return fail("Context easing on a neighboring lane must leave the former active channel unchanged");
  document->undo();
  if (document->currentClip().value("keyframes")!=original_keys || !right_click_point("timelineAnimationLanePoint-frameScale-0"))
    return fail("Switching channel context and Undo must preserve both original curves");
  QTest::keyClick(window,Qt::Key_Escape);
  if (!wait_for_condition(context_closed,2000)) return fail("The channel context must dismiss before changing its chooser");
  if (!click("timelineAnimationChannelChooserPrimaryAction") || !wait_for_condition([&] {
        auto* popup = find_named_object(workspace, "timelineAnimationChannelChooserPopup");
        return popup && popup->property("opened").toBool();
      }, 3000)) return fail("The channel comparison chooser must open");
  const auto opacity_index = document->currentClip().value("availableKeyframeParameters").toStringList().indexOf("opacity") + 1;
  if (opacity_index < 1 || !click(QStringLiteral("timelineAnimationChannelChooserPopupItem%1").arg(opacity_index)) ||
      !wait_for_condition([&] { return channels->property("channelCount").toInt() == 4; }, 2000))
    return fail("Channel checkboxes must change the compared lanes without changing keys");
  if (!wait_for_condition([&] {
        auto* popup = find_named_object(workspace, "timelineAnimationChannelChooserPopup");
        return popup && !popup->property("visible").toBool();
      }, 2000) || !click("timelineAnimationChannelChooserPrimaryAction") ||
      !wait_for_condition([&] { auto* popup = find_named_object(workspace,"timelineAnimationChannelChooserPopup"); return popup && popup->property("opened").toBool(); }, 2000) ||
      !click("timelineAnimationChannelChooserPopupItem0") ||
      !wait_for_condition([&] { return channels->property("channelCount").toInt() == 5; }, 2000))
    return fail("The comparison chooser must restore Motion and animated channels");
  if (document->currentClip().value("keyframes") != original_keys) return fail("Comparing channels must leave the document unchanged");
  if (!document->saveTimeline() || !wait_for_condition([&] { return !document->timelineSaveInProgress() && !document->dirty(); }, 10000))
    return fail("Multi-channel animation must finish saving before close/reopen");
  const auto saved_path = document->savedDocumentPath();
  document->closeTimeline();
  if (!document->loadTimeline(saved_path)) return fail("The multi-channel project must reopen");
  document->selectClip(0);
  QMetaObject::invokeMethod(root,"switchToWorkspaceIndex",Q_RETURN_ARG(QVariant,result),
      Q_ARG(QVariant,root->property("timelineWorkspaceIndex")));
  if (!wait_for_condition([&] {
        auto* view=active_workspace(root);
        if (!view || view->objectName()!="timelineWorkspace" || !view->property("timelineEditorPageLoaded").toBool()) return false;
        workspace=view;
        return true;
      },10000)) return fail("Reopening a project must reacquire its live Timeline workspace");
  if (document->currentClip().value("keyframes") != original_keys) return fail("All channel values and timings must survive reopening");
  invoke("closeTimelinePanel", "keyframes");
  invoke("applyTimelineLayoutPreset", "edit");
  document->clearSelectedClipKeyframes();
  document->trimSelectedClipStartToSequenceSeconds(2, false);
  document->trimSelectedClipEndToSequenceSeconds(8, false);
  document->setSelectedClipPlaybackRate(2);
  document->setActiveTool(QStringLiteral("trim"));
  workspace->setProperty("timelineZoom", 72.0);
  workspace->setProperty("snappingEnabled", false);
  workspace->setProperty("rippleModeEnabled", false);
  document->seekSequenceSeconds(2.0);
  process_layout_events(12);
  const auto trim_before = document->currentClip();
  if (std::abs(trim_before.value("startSeconds").toDouble() - 2) > 0.0001 ||
      std::abs(trim_before.value("trimmedDurationSeconds").toDouble() - 3) > 0.0001)
    return fail("The precision preview fixture must have source 2–8 at 2× rate and a three-second sequence duration");
  auto* block = find_visible_named_item(workspace, "timelineClip-Motion study");
  auto* right = block ? find_visible_named_item(block, "timelineClipRightTrimHandle") : nullptr;
  if (!right) return fail("The real selected clip must expose its out handle");
  const auto trim_start = right->mapToScene(QPointF(right->width()/2, right->height()/2)).toPoint();
  const QPoint trim_delta(qRound(workspace->property("timelineZoom").toDouble() * 0.5), 0);
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, trim_start);
  if (!wait_for_condition([&] {
        auto* before=find_visible_named_item(workspace,"timelineTrimOutgoingFrame");
        auto* after=find_visible_named_item(workspace,"timelineTrimIncomingFrame");
        return before && after && before->property("frameReady").toBool() && after->property("frameReady").toBool() &&
            before->property("decodedFrameSource")==after->property("decodedFrameSource");
      },8000)) return fail("Both trim consumers must receive the same complete source frame before the edge moves");
  for (int step = 1; step <= 8; ++step) QTest::mouseMove(window, trim_start + trim_delta*step/8, 12);
  if (!wait_for_condition([&] {
        const auto state = workspace->property("trimFeedbackState").toMap();
        const auto metrics = state.value("metrics").toMap();
        return state.value("visible").toBool() && metrics.value("deltaFrames").toInt() == 15 &&
            metrics.value("durationBeforeFrames").toInt() == 90 && metrics.value("durationAfterFrames").toInt() == 105 &&
            metrics.value("headHandleFrames").toInt() == 30 && metrics.value("tailHandleFrames").toInt() == 15;
      }, 3000)) return fail("Live trim must report exact frame delta, before/after duration, and source handle room");
  const auto state = workspace->property("trimFeedbackState").toMap();
  const double expected_after = 9.0 - 2.0/30.0;
  const double expected_before = 8.0 - 2.0/30.0;
  if (std::abs(state.value("before").toMap().value("sourceSeconds").toDouble() - expected_before) > 0.00001 ||
      std::abs(state.value("after").toMap().value("sourceSeconds").toDouble() - expected_after) > 0.00001 ||
      document->currentClip().value("trimmedDurationSeconds") != trim_before.value("trimmedDurationSeconds"))
    return fail("A 2× trim must map both preview samples through source time while keeping the document unchanged during drag");
  auto* after_view = find_visible_named_item(workspace, "timelineTrimIncomingFrame");
  if (!after_view || !wait_for_condition([&] {return after_view->property("frameReady").toBool();}, 8000))
    return fail("The trim after view must deliver a real decoded source frame");
  const auto decoded_path=QUrl(after_view->property("decodedFrameSource").toString()).toLocalFile();
  const auto reference_path=QDir(output_dir).filePath("trim-source-reference.png");
  QProcess decoder;
  decoder.start(QStandardPaths::findExecutable("ffmpeg"), {"-hide_banner","-loglevel","error","-y",
      "-i",video_path,"-ss",QString::number(expected_after,'f',6),"-frames:v","1",reference_path});
  if (!decoder.waitForFinished(15000) || decoder.exitCode()!=0) return fail("The trim source reference must decode");
  const QImage reference(reference_path);
  const auto frame_error=[&] {
    const auto frame=QImage(decoded_path).convertToFormat(QImage::Format_RGB32);
    if (frame.isNull() || reference.isNull() || frame.size()!=reference.size()) return 1000.0;
    double sum=0;
    for (int y=0;y<frame.height();++y) for(int x=0;x<frame.width();++x) {
      const auto a=frame.pixelColor(x,y), b=reference.pixelColor(x,y);
      sum+=std::abs(a.red()-b.red())+std::abs(a.green()-b.green())+std::abs(a.blue()-b.blue());
    }
    return sum/(frame.width()*frame.height()*3);
  };
  if (!wait_for_condition([&] {return frame_error()<=8.0;},5000)) {
    QImage(decoded_path).save(QDir(output_dir).filePath("trim-source-decoded-failure.png"));
    std::cerr<<"Trim decoded-frame RGB error="<<frame_error()<<'\n';
    return fail("The trim after view must match the actual mapped source frame, including when native frame timestamps are unavailable");
  }
  const auto decoded_error=frame_error();
  QImage(decoded_path).save(QDir(output_dir).filePath("trim-source-decoded.png"));
  if (!capture("trim-before-after-rate-aware")) return fail("The live two-up trim must be visibly rendered");
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, trim_start + trim_delta);
  if (!wait_for_condition([&] { return std::abs(document->currentClip().value("trimmedDurationSeconds").toDouble() - 3.5) < 0.0001; }, 3000))
    return fail("The committed trim duration must match the preview duration");
  document->undo();
  if (std::abs(document->currentClip().value("trimmedDurationSeconds").toDouble() - 3) > 0.0001)
    return fail("One Undo must restore the complete trim gesture");
  process_layout_events(8);
  block = find_visible_named_item(workspace, "timelineClip-Motion study");
  right = block ? find_visible_named_item(block, "timelineClipRightTrimHandle") : nullptr;
  if (!right) return fail("Undo must restore an editable source edge");
  const auto limit_start = right->mapToScene(QPointF(right->width()/2,right->height()/2)).toPoint();
  const QPoint overrun(qRound(workspace->property("timelineZoom").toDouble()*2),0);
  QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,limit_start);
  QTest::mouseMove(window,limit_start+overrun,30);
  if (!wait_for_condition([&] {
        const auto feedback=workspace->property("trimFeedbackState").toMap();
        return feedback.value("limitReached").toBool() && feedback.value("metrics").toMap().value("tailHandleFrames").toInt()==0 &&
            std::abs(feedback.value("boundarySeconds").toDouble()-6)<0.0001 && named_item_visible(workspace,"timelineTrimLimitReadout");
      },3000)) return fail("Dragging past the available source must stop at its exact handle and explain the limit");
  if (!capture("trim-source-limit")) return fail("Source limit feedback must remain inside the real Program viewer");
  auto* grabber=window->mouseGrabberItem();
  if (!grabber) return fail("A live trim must retain its pointer owner until release or cancellation");
  grabber->ungrabMouse();
  QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,limit_start+overrun);
  process_layout_events(8);
  if (std::abs(document->currentClip().value("trimmedDurationSeconds").toDouble()-3)>0.0001 ||
      workspace->property("trimFeedbackState").toMap().value("visible").toBool())
    return fail("Canceling a handle-limited trim must restore Program without changing the document");
  document->seekSequenceSeconds(2);
  document->toggleSelectedClipKeyframe("frameScale");
  document->toggleSelectedClipKeyframe("frameOffsetX");
  document->seekSequenceSeconds(4);
  document->setSelectedFrameScale(1.4);
  document->setSelectedFrameOffsetX(0.4);
  window->resize(1100,760);
  invoke("openCurveEditor","frameScale");
  process_layout_events(10);
  auto* graph=find_named_object(workspace,"timelineKeyframeCurveWindow");
  if (graph) graph->setProperty("graphMode","channels");
  if (!wait_for_condition([&] { return named_item_visible(workspace,"timelineAnimationChannels"); },3000))
    return fail("The channel comparison must remain available after trimming and resizing");
  auto* compact_scale=find_visible_named_item(workspace,"timelineAnimationLanePoint-frameScale-0");
  auto* compact_position=find_visible_named_item(workspace,"timelineAnimationLanePoint-frameOffsetX-0");
  if (!compact_scale || !compact_position ||
      std::abs(compact_scale->mapToScene(QPointF(0,0)).x()-compact_position->mapToScene(QPointF(0,0)).x())>1)
    return fail("The compact view must show two aligned animated channel curves");
  auto* title=find_visible_named_item(workspace,"timelineTitleButton");
  auto* title_label=title ? find_visible_named_item(title,"sharedCommandLabel") : nullptr;
  if (!title_label || title_label->property("contentWidth").toDouble()>title_label->width()+1 ||
      !title->property("accessibleName").toString().contains(document->timelineName()))
    return fail("A long project title must fit the compact header and retain its full accessible name");
  if (!capture("channels-compact-workspace")) return fail("The compact channel layout must produce a native capture");
  if (!right_click_point("timelineAnimationLanePoint-frameScale-0") || !context_in_window() ||
      !capture("channels-compact-context-menu")) return fail("The channel menu must stay within the compact native window");
  QTest::keyClick(window,Qt::Key_Escape);
  if (!wait_for_condition(context_closed,2000)) return fail("Escape must dismiss the channel context menu");
  const auto unlocked_keys=document->currentClip().value("keyframes");
  document->toggleTrackLocked(0);
  process_layout_events(6);
  if (!right_click_point("timelineAnimationLanePoint-frameScale-0")) return fail("Locked channels must still expose read-only context actions");
  for (const auto& name:{"timelineCurveContextAdd","timelineCurveContextEaseOut","timelineCurveContextPaste","timelineCurveContextDelete"}) {
    auto* action=find_named_object(workspace,name);
    if (!action || action->property("enabled").toBool()) return fail("Locked-track mutation actions must be disabled in the channel menu");
  }
  auto* copy_action=find_named_object(workspace,"timelineCurveContextCopy");
  if (!copy_action || !copy_action->property("enabled").toBool() || document->currentClip().value("keyframes")!=unlocked_keys)
    return fail("A locked channel must permit copying while preserving its keyframes");
  if (!click("timelineCurveContextCopy") || !wait_for_condition(context_closed,2000) || document->keyframeClipboardCount()!=1 ||
      document->currentClip().value("keyframes")!=unlocked_keys)
    return fail("Copy on a locked selected channel must actually replace the clipboard without editing the document");
  if (!right_click_point("timelineAnimationLanePoint-frameScale-0")) return fail("The read-only context menu must reopen");
  workspace->setProperty("selectedAnimatedParameter","frameOffsetX");
  if (!wait_for_condition(context_closed,2000)) return fail("Changing the context target must dismiss its stale menu");
  document->toggleTrackLocked(0);
  if (!right_click_point("timelineAnimationLanePoint-frameScale-0") || !click("timelineCurveContextDelete") ||
      !wait_for_condition(context_closed,2000) || channel_keys(document->currentClip().value("keyframes"),"frameScale").size()!=1)
    return fail("Context Delete must remove only the clicked channel key after unlocking");
  document->undo();
  if (document->currentClip().value("keyframes")!=unlocked_keys)
    return fail("One Undo must restore a context-deleted key without touching the other channel");
  const QJsonObject receipt{{"schema","timeline-channels-trim-ui-proof:v1"},{"passed",native_platform && window->isActive()},
      {"functionalAssertionsPassed",true},{"platform",QGuiApplication::platformName()},{"nativeWindowActive",native_platform && window->isActive()},
      {"captures",captures},{"alignedChannelTimes",true},{"liveLanePointerCapture",true},{"isolatedChannelEdit",true},
      {"channelGestureUndo",true},{"channelFrameNudge",true},{"channelChooser",true},{"channelContextActions",true},{"channelContextKeyboard",true},{"channelActionFocusRestore",true},{"channelContextBounds",true},{"channelLockedContext",true},{"channelContextTargetDismissal",true},{"channelSaveReopen",true},
      {"trimFrameMetrics",true},{"trimRateAwareSourceMapping",true},{"trimDecodedFrameReady",true},{"trimSharedFrameConsumers",true},
      {"trimExpectedSourceSeconds",expected_after},{"trimDecodedMeanAbsoluteRgbError",decoded_error},{"trimDecodedErrorLimit",8},{"trimDurationCommitUndo",true},{"trimSourceLimit",true},{"trimCancel",true}};
  QFile file(QDir(output_dir).filePath("timeline-channels-trim-proof.json"));
  if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(receipt).toJson())<=0)
    return fail("The channel and trim proof must retain its evidence receipt");
  std::cout << QJsonDocument(receipt).toJson().constData();
  if (native_platform && !window->isActive())
    return fail("Native qualification requires the test window to remain active through the final capture");
  return EXIT_SUCCESS;
}

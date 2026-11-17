        QVariant added_board;
        if (!QMetaObject::invokeMethod(visual_root, "addEditBoard", Q_RETURN_ARG(QVariant, added_board), Q_ARG(QVariant, false)) || !added_board.toBool() ||
            !wait_for_condition([&] { return carousel_document.currentIndex() == 1 && editor_preview.photoLayerCount() == 0 && !editor_preview.processing(); }, 20000))
          return fail("Create an independent blank 4K board beside the existing board");
        const bool controls_proof = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_BOARD_VIDEO_CONTROLS");
        const double fixture_duration = controls_proof ? 1.2 : .4;
        const auto first_movie = runtime_root.filePath("first-wide-video.mp4"), second_movie = runtime_root.filePath("second-tall-video.mp4");
        for (const auto& fixture : {qMakePair(first_movie, QStringLiteral("color=c=red:s=640x360:r=30:d=%1").arg(fixture_duration)),
                                  qMakePair(second_movie, QStringLiteral("color=c=blue:s=360x640:r=30:d=%1").arg(fixture_duration))}) {
          QProcess process; process.start(QStandardPaths::findExecutable("ffmpeg"), {"-v", "error", "-y", "-f", "lavfi", "-i", fixture.second,
              "-c:v", "libx264", "-pix_fmt", "yuv420p", fixture.first});
          if (!process.waitForFinished(15000) || process.exitCode() != 0) return fail("Generate two differently sized source movies");
        }
        catalog_assets.clearFilters();
        auto first_asset = make_asset("two-wide", first_movie), second_asset = make_asset("two-tall", second_movie);
        for (auto* asset : {&first_asset, &second_asset}) {
          asset->media_type = "video"; asset->duration_seconds = fixture_duration; asset->project_names = {"Canvas project"};
        }
        catalog_assets.replaceAssetsForTesting({first_asset, second_asset});
        catalog_assets.addAssetToProject("two-wide", "Canvas project");
        catalog_assets.addAssetToProject("two-tall", "Canvas project");
        if (!QMetaObject::invokeMethod(edit, "openBoardImagePicker") || !wait_for_condition([&] {
              return visual_root->property("currentWorkspace") == visual_root->property("libraryWorkspaceIndex") && !visual_root->property("workspaceTransitionActive").toBool(); }, 15000))
          return fail("Choose both videos through the project's Library");
        catalog_assets.setMediaTypeFilter("video");
        if (!wait_for_condition([&] { return catalog_assets.assetCount() == 2; }, 5000)) return fail("Both project videos are visible after Library filters settle");
        catalog_assets.selectIndexWithModifiers(0, 0, false);
        catalog_assets.selectIndexWithModifiers(1, Qt::ControlModifier, false);
        if (catalog_assets.selectedAssetCount() != 2) {
          std::cerr << "Videos visible=" << catalog_assets.assetCount() << " selected=" << catalog_assets.selectedAssetIds().join(',').toStdString() << '\n';
          return fail("Library retains both selected videos");
        }
        if (!capture("two-videos-selected-in-library", {"libraryAddPhotosToCanvasButton"})) return fail("Capture the real two-video selection");
        auto* add = find_named_object(visual_root, "libraryAddPhotosToCanvasButton");
        if (!add || !QMetaObject::invokeMethod(add, "clicked") || !wait_for_condition([&] {
              return !timeline_document.carouselBoardSession().isEmpty() && !timeline_document.mediaAnalysisBusy() &&
                  visual_root->property("currentWorkspace") == visual_root->property("timelineWorkspaceIndex") && !visual_root->property("workspaceTransitionActive").toBool(); }, 30000))
          return fail("Import both videos into the owning board sequence in one operation");
        int movies = 0;
        QJsonArray video_frames;
        for (const auto& value : timeline_document.clips()) {
          const auto clip = value.toMap(); if (clip.value("mediaType") != "video") continue;
          ++movies;
          const auto crop_width = clip.value("sourceCropRight").toDouble() - clip.value("sourceCropLeft").toDouble();
          const auto crop_height = clip.value("sourceCropBottom").toDouble() - clip.value("sourceCropTop").toDouble();
          video_frames.push_back(QJsonObject{{"movie", QFileInfo(clip.value("filePath").toString()).fileName()},
              {"width", clip.value("frameWidth").toDouble()}, {"height", clip.value("frameHeight").toDouble()},
              {"x", clip.value("frameOffsetX").toDouble()}, {"y", clip.value("frameOffsetY").toDouble()},
              {"cropWidth", crop_width}, {"cropHeight", crop_height}});
          if (qAbs(clip.value("frameWidth").toDouble() * creation_width - (creation_width - 8)) > .01 ||
              qAbs(clip.value("frameHeight").toDouble() * creation_height - (creation_height - 12) / 2.0) > .01 ||
              qAbs(clip.value("frameOffsetY").toDouble()) < .9 || clip.value("startSeconds").toDouble() != 0) {
            std::cerr << QJsonDocument(video_frames).toJson().toStdString();
            return fail("A two-video Library import must apply separate equal layout halves before opening Timeline");
          }
        }
        if (movies != 2 || timeline_document.sequenceWidth() != creation_width || timeline_document.sequenceHeight() != creation_height)
          return fail("Both source videos retain independent frames inside the board's resolution");
        if (qAbs(timeline_document.totalDurationSecondsValue() - fixture_duration) > .05) return fail("Canvas stills follow the video duration instead of adding an artificial tail");
        if (!capture("two-video-layout-in-timeline", {"carouselBoardSequenceBar", "carouselSequenceApplyButton"})) return fail("Capture both laid-out clips in Timeline");
        auto* apply = find_named_object(visual_root, "carouselSequenceApplyButton");
        if (!apply || !QMetaObject::invokeMethod(apply, "clicked") || !wait_for_condition([&] {
              return timeline_document.carouselBoardSession().isEmpty() && !carousel_document.boardSequence(1).value("videoPath").toString().isEmpty() &&
                  visual_root->property("currentWorkspace") == visual_root->property("editWorkspaceIndex") && !visual_root->property("workspaceTransitionActive").toBool() &&
                  !editor_preview.processing(); }, 60000)) return fail("Apply both videos and return to the editable board layout");
        auto* video_board_layout = find_named_object(edit, QStringLiteral("boardLayoutInspectorGroup"));
        if (!video_board_layout || !video_board_layout->setProperty("expanded", true) ||
            !wait_for_condition([&] {
              return item_visible(find_named_item(edit, QStringLiteral("boardPhotoControls"))) &&
                     item_visible(find_named_item(edit, QStringLiteral("photoLayersInspectorGroup")));
            }, 5000) ||
            !capture("two-video-layout-on-board", {"boardApplyPhotoLayoutButton", "editCanvasAddBoardButton"}))
          return fail("Video boards expose layout controls in the stacked inspector above Layers");
        const auto sequence = carousel_document.boardSequence(1);
        const QImage poster(sequence.value("posterPath").toString());
        const auto red = poster.pixelColor(creation_width / 2, creation_height / 4), blue = poster.pixelColor(creation_width / 2, creation_height * 3 / 4);
        if (poster.size() != QSize(creation_width, creation_height) ||
            !((red.red() > red.blue() * 2 && blue.blue() > blue.red() * 2) || (red.blue() > red.red() * 2 && blue.red() > blue.blue() * 2)))
          return fail("The exported board shows both videos concurrently in their layout halves");
        for (const auto& point : {QPoint(0, creation_height / 4), QPoint(creation_width / 2, 0), QPoint(creation_width / 2, creation_height / 2)}) {
          const auto color = poster.pixelColor(point);
          if (color.red() < 235 || color.green() < 235 || color.blue() < 235) return fail("The video layout retains white edges and one shared center border");
        }
        poster.save(QDir(proof_dir).filePath("two-video-import-render.png"));
        if (carousel_document.boardSequence(1).value("needsUpdate").toBool()) return fail("The applied batch video layout is ready for playback and export");
        for (const int board : {0, 1}) {
          QVariant activated;
          if (!QMetaObject::invokeMethod(visual_root, "activateEditBoard", Q_RETURN_ARG(QVariant, activated), Q_ARG(QVariant, board)) || !activated.toBool() ||
              !wait_for_condition([&] { return carousel_document.currentIndex() == board && !editor_preview.processing(); }, 20000) ||
              !video_board_layout->setProperty("expanded", true) ||
              !wait_for_condition([&] { return item_visible(find_named_item(edit, QStringLiteral("boardPhotoControls"))); }, 5000))
            return fail("Switch boards and reopen their Board settings");
        }
        if (edit->property("boardSequencePreviewActive").toBool() || !capture("two-video-board-reopened-for-layout", {"boardApplyPhotoLayoutButton"}))
          return fail("Returning to a video board keeps both layout frames editable");
        edit->setProperty("editSequenceBackgroundMode", false);
        if (!wait_for_condition([&] { return edit->property("boardSequencePreviewActive").toBool(); }, 5000)) return fail("Explicitly show the current rendered video sequence");
        const auto before_move = editor_preview.currentAssetBoundAdjustmentPatchJson();
        if (!editor_preview.movePhotoLayersByPixels({0}, 1, 0) || !wait_for_condition([&] {
              return carousel_document.boardSequence(1).value("needsUpdate").toBool() && !edit->property("boardSequencePreviewActive").toBool(); }, 10000) ||
            !capture("stale-video-preview-keeps-layout-visible", {"boardApplyPhotoLayoutButton"})) return fail("A stale movie never covers the current editable layout");
        editor_preview.undo();
        if (!wait_for_condition([&] { return editor_preview.currentAssetBoundAdjustmentPatchJson() == before_move && !carousel_document.boardSequence(1).value("needsUpdate").toBool(); }, 10000))
          return fail("Undo restores the playable batch layout");
        QQuickItem* open_sequence = nullptr;
        for (auto* candidate : find_named_items(visual_root, "boardEditVideoSequenceButton")) if (item_visible(candidate) && candidate->isEnabled()) { open_sequence = candidate; break; }
        if (!open_sequence) return fail("A saved video board exposes its sequence action");
        const auto sequence_action = scene_rect(open_sequence).center().toPoint();
        QTest::mouseClick(visual_window, Qt::LeftButton, Qt::NoModifier, sequence_action);
        if (!wait_for_condition([&] { return !timeline_document.carouselBoardSession().isEmpty() &&
                visual_root->property("currentWorkspace") == visual_root->property("timelineWorkspaceIndex") && !visual_root->property("workspaceTransitionActive").toBool(); }, 30000)) {
          std::cerr << "Sequence button: status=" << timeline_document.status().toStdString() << " board=" << carousel_document.status().toStdString()
                    << " feedback=" << edit->property("editActionFeedback").toString().toStdString() << " session=" << QJsonDocument::fromVariant(timeline_document.carouselBoardSession()).toJson().toStdString() << '\n';
          return fail("The actual sequence button reopens an existing board sequence");
        }
        if (timeline_document.currentClip().value("mediaType") != "video") return fail("Reopening selects an editable video rather than the locked canvas background");
        if (!timeline_document.cancelCarouselBoardSequence() || !wait_for_condition([&] { return timeline_document.carouselBoardSession().isEmpty() &&
              visual_root->property("currentWorkspace") == visual_root->property("editWorkspaceIndex") && !visual_root->property("workspaceTransitionActive").toBool(); }, 10000)) return fail("Return from reopened sequence without changing the board");
        if (controls_proof) {
          const auto visible_control = [&](const QString& name) -> QQuickItem* {
            for (auto* candidate : find_named_items(visual_root, name)) if (item_visible(candidate) && candidate->isEnabled()) return candidate;
            return nullptr;
          };
          const auto click_control = [&](const QString& name) {
            auto* control = visible_control(name); if (!control) return false;
            QTest::mouseClick(visual_window, Qt::LeftButton, Qt::NoModifier, scene_rect(control).center().toPoint()); return true;
          };
          auto* player = find_named_object(find_named_object(edit, "editImageViewport"), "boardSequencePlayer");
          if (!player || !click_control("boardVideoTransportPlayButton") || !wait_for_condition([&] {
                return player->property("position").toLongLong() > 50 && edit->property("boardSequencePlaying").toBool(); }, 15000)) {
            std::cerr << "Board player: found=" << !!player << " playing=" << edit->property("boardSequencePlaying").toBool()
                      << " preview=" << edit->property("boardSequencePreviewActive").toBool() << " active=" << edit->property("workspaceActive").toBool();
            if (player) for (const auto* key : {"source", "position", "duration", "mediaStatus", "playbackState", "errorString"}) std::cerr << ' ' << key << '=' << player->property(key).toString().toStdString();
            std::cerr << '\n';
            return fail("The persistent board Play control starts actual media playback");
          }
          if (!click_control("boardVideoTransportPlayButton") || !wait_for_condition([&] {
                return !edit->property("boardSequencePlaying").toBool() && player->property("playbackState").toInt() == QMediaPlayer::PausedState; }, 5000)) return fail("Pause holds the decoded video frame");
          auto* output = qobject_cast<QQuickItem*>(player->property("videoOutput").value<QObject*>());
          if (!output || !output->isVisible()) return fail("Pausing keeps the decoded VideoOutput visible rather than replacing it with the poster");
          auto* seek = visible_control("boardSequenceSeekSlider");
          if (!seek) return fail("The board exposes a usable video scrubber");
          const auto seek_rect = scene_rect(seek);
          QTest::mouseClick(visual_window, Qt::LeftButton, Qt::NoModifier, QPointF(seek_rect.x() + seek_rect.width() * .75, seek_rect.center().y()).toPoint());
          if (!wait_for_condition([&] { return player->property("position").toLongLong() > 600; }, 5000) ||
              !capture("board-video-paused-and-scrubbed", {"boardVideoTransportBar", "boardSequenceSeekSlider"})) return fail("Seeking updates the paused board preview");
          if (!click_control("boardSequenceRestartButton") || !wait_for_condition([&] { return player->property("position").toLongLong() == 0; }, 5000)) return fail("Restart returns the board preview to its first frame");
          if (!editor_preview.movePhotoLayersByPixels({0}, 1, 0) || !wait_for_condition([&] { return carousel_document.boardSequence(1).value("needsUpdate").toBool(); }, 5000)) return fail("Prepare a changed video layout");
          if (!click_control("boardVideoTransportPlayButton") || !wait_for_condition([&] {
                return !timeline_document.carouselBoardSession().isEmpty() && !visual_root->property("workspaceTransitionActive").toBool() &&
                    timeline_document.sequencePlayheadSeconds() > .05; }, 15000)) {
            std::cerr << "Live layout preview: pending=" << visual_root->property("pendingBoardSequencePlayback").toBool()
                      << " workspaceActive=" << timeline_playback.workspaceActive() << " playing=" << timeline_document.sequencePlaying()
                      << " error=" << timeline_playback.errorString().toStdString() << " status=" << timeline_document.status().toStdString()
                      << " readiness=" << QJsonDocument::fromVariant(photo_editor::desktop::TimelinePlaybackFrameTestAccess::readiness(timeline_playback)).toJson().toStdString() << '\n';
            return fail("Preview layout opens and plays current clips without first rendering an export");
          }
          if (timeline_document.currentClip().value("mediaType") != "video" ||
              !click_control("timelinePlayPauseButton") || !wait_for_condition([&] { return !timeline_document.sequencePlaying() && !timeline_playback.playbackStartPending(); }, 5000) ||
              !capture("changed-board-layout-live-timeline-preview", {"carouselBoardSequenceBar", "timelinePlayPauseButton"})) return fail("The live layout preview has selected video controls and usable transport");
          if (!timeline_document.cancelCarouselBoardSequence() || !wait_for_condition([&] { return timeline_document.carouselBoardSession().isEmpty() &&
                visual_root->property("currentWorkspace") == visual_root->property("editWorkspaceIndex") && !visual_root->property("workspaceTransitionActive").toBool(); }, 10000)) return fail("Return from live preview preserves editable board media");
          editor_preview.undo();
          if (!wait_for_condition([&] { return !carousel_document.boardSequence(1).value("needsUpdate").toBool(); }, 10000)) return fail("Undo restores the saved playable layout after live preview");
          std::cout << "Native board controls: saved sequence reopen selects video; Play, Pause, Seek, Restart and current-layout preview passed\n";
        }
        std::cout << "Native two-video Library import: separate 4K layout frames, mixed source ratios, concurrent export, editable return and existing board preservation passed\n";
        return 0;

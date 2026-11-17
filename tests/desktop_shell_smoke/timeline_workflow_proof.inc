      if (!show_studio_workspace("timelineWorkspaceIndex", QStringLiteral("timelineWorkspace"))) {
        return fail("Timeline workflow should reach its editor");
      }
      auto* workflow_timeline = find_named_object(studio_root, QStringLiteral("timelineWorkspace"));
      if (!workflow_timeline || !QMetaObject::invokeMethod(workflow_timeline,
          "applyTimelineLayoutPreset", Q_ARG(QVariant, QVariant(QStringLiteral("edit"))))) {
        return fail("Timeline workflow should open its editing layout");
      }
      if (auto* recovery = find_named_object(workflow_timeline, QStringLiteral("timelineRecoveryPopup"));
          recovery && recovery->property("visible").toBool()) {
        QMetaObject::invokeMethod(recovery, "close");
      }
      studio_window->resize(1440, 900);
      workflow_timeline->setProperty("timelineZoom", 140.0);
      workflow_timeline->setProperty("snappingEnabled", false);
      workflow_timeline->setProperty("timelineAdvancedControlsVisible", false);
      process_layout_events(8);
      const auto capture_workflow = [&](const QString& name, const QStringList& required) {
        return capture_visual_proof(studio_window, studio_root, proof_dir, name,
                                    studio_window->width(), studio_window->height(), required, &studio_manifest);
      };
      ulong workflow_pointer_timestamp = 1000;
      const auto send_pointer = [&](QEvent::Type type, const QPointF& point) {
        if (type == QEvent::MouseButtonPress &&
            qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER") &&
            (!studio_window->isActive() || !studio_window->isExposed())) {
          activate_native_test_window(studio_window);
          if (!wait_for_condition([&] {
                return studio_window->isActive() && studio_window->isExposed();
              }, 5000)) {
            std::cerr << "Native workflow input requires an active, exposed window\n";
            return;
          }
        }
        const auto global = QPointF(studio_window->mapToGlobal(point.toPoint()));
        QMouseEvent event(type, point, global,
                          type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                          Qt::NoModifier);
        event.setTimestamp(workflow_pointer_timestamp);
        workflow_pointer_timestamp += 16;
        QCoreApplication::sendEvent(studio_window, &event);
        process_layout_events(2);
      };
      const auto click_item = [&](QQuickItem* item) {
        if (!item_visible(item) || item->window() != studio_window || !item->property("enabled").toBool()) return false;
        // These are intentional single clicks. A zero timestamp makes separate
        // popup visits look like one double tap to GalleryAssetCard's TapHandler.
        workflow_pointer_timestamp += static_cast<ulong>(QApplication::doubleClickInterval() + 1);
        const auto point = scene_rect(item).center();
        send_pointer(QEvent::MouseButtonPress, point);
        send_pointer(QEvent::MouseButtonRelease, point);
        return true;
      };
      const auto live_item = [&](const QString& name) -> QQuickItem* {
        for (auto* item : find_named_items(studio_root, name)) {
          if (item_visible(item) && item->window() == studio_window) return item;
        }
        return nullptr;
      };
      const auto click_named = [&](const QString& name) {
        return click_item(live_item(name));
      };
      const auto click_item_without_layout_gap = [&](QQuickItem* item) {
        if (!item_visible(item) || item->window() != studio_window ||
            !item->property("enabled").toBool())
          return false;
        workflow_pointer_timestamp += static_cast<ulong>(QApplication::doubleClickInterval() + 1);
        const auto point = scene_rect(item).center();
        const auto send_immediate_pointer = [&](QEvent::Type type) {
          const auto global = QPointF(studio_window->mapToGlobal(point.toPoint()));
          QMouseEvent event(type, point, global, Qt::LeftButton,
                            type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                            Qt::NoModifier);
          event.setTimestamp(workflow_pointer_timestamp++);
          QCoreApplication::sendEvent(studio_window, &event);
        };
        send_immediate_pointer(QEvent::MouseButtonPress);
        send_immediate_pointer(QEvent::MouseButtonRelease);
        process_layout_events(2);
        return true;
      };
      const auto drag_item = [&](QQuickItem* item, double delta_x) {
        if (!item_visible(item)) return false;
        const auto start = scene_rect(item).center();
        send_pointer(QEvent::MouseButtonPress, start);
        for (int step = 1; step <= 6; ++step) {
          send_pointer(QEvent::MouseMove, start + QPointF(delta_x * step / 6.0, 0));
        }
        send_pointer(QEvent::MouseButtonRelease, start + QPointF(delta_x, 0));
        return true;
      };
      const auto clip_by_id = [&](const QString& id) {
        for (const auto& row : timeline_document.timelineTracks()) {
          for (const auto& clip : row.toMap().value(QStringLiteral("clips")).toList()) {
            if (clip.toMap().value(QStringLiteral("clipId")).toString() == id) return clip.toMap();
          }
        }
        return QVariantMap{};
      };
      QJsonArray seek_events;
      auto* workflow_player = photo_editor::desktop::TimelinePlaybackFrameTestAccess::player(timeline_playback);
      const auto position_trace = QObject::connect(workflow_player, &QMediaPlayer::positionChanged, workflow_player,
          [&](qint64 value) { seek_events.append(QJsonObject{{QStringLiteral("position"), value}}); });
      const auto status_trace = QObject::connect(workflow_player, &QMediaPlayer::mediaStatusChanged, workflow_player,
          [&](QMediaPlayer::MediaStatus value) {
            seek_events.append(QJsonObject{{QStringLiteral("status"), static_cast<int>(value)},
                                           {QStringLiteral("position"), workflow_player->position()}});
          });
      const auto disconnect_trace = qScopeGuard([&] { QObject::disconnect(position_trace); QObject::disconnect(status_trace); });
      const auto report_playback = [&] {
        std::cerr << "Playback: ready=" << timeline_playback.mediaReady()
                  << " frame=" << timeline_playback.processedFrameSource().toStdString()
                  << " error=" << timeline_playback.errorString().toStdString() << '\n';
        std::cerr << QJsonDocument::fromVariant(timeline_playback.frameProcessingMetrics())
                         .toJson(QJsonDocument::Compact).toStdString() << '\n';
        std::cerr << QJsonDocument::fromVariant(
                         photo_editor::desktop::TimelinePlaybackFrameTestAccess::readiness(timeline_playback))
                         .toJson(QJsonDocument::Compact).toStdString() << '\n';
        std::cerr << QJsonDocument(seek_events).toJson(QJsonDocument::Compact).toStdString() << '\n';
      };
      if (!wait_for_condition([&] {
            const auto state = photo_editor::desktop::TimelinePlaybackFrameTestAccess::readiness(timeline_playback);
            return timeline_playback.mediaReady() && !state.value(QStringLiteral("waitingForPosition")).toBool();
          }, 10000)) {
        report_playback();
        return fail("Loading at position zero should not leave playback waiting for a nonexistent positionChanged signal");
      }
      std::unique_ptr<QQuickImageProvider> program_provider;
      program_provider.reset(timeline_playback.createImageProvider());

      bool scene_camera_proof = false;
      bool scene_camera_timing_estimated = false;
      double scene_point_selection_source_seconds = -1.0;
      double roto_correction_source_seconds = -1.0;
      int roto_correction_frame_index = -1;
      bool surface_retrack_proof = false;
      bool surface_correction_proof = false;
      bool surface_attachment_occlusion_proof = false;
      double surface_playback_sample_seconds = -1;
      const auto scene_only_mode = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_SCENE_ONLY");
      const auto surface_only_mode = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_SURFACE_ONLY");
      const auto roto_only_mode = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_ROTO_ONLY");
      const auto surface_occlusion_mode = qEnvironmentVariableIntValue("PHOTO_EDITOR_SMOKE_OCCLUSION") == 1;
      const auto tracking_only_mode = scene_only_mode || surface_only_mode || roto_only_mode;
      const auto run_surface_preview_checks = [&](const QString& source_id, const QString& target_clip_id,
                                                  const QString& source_path, int source_clip_index) -> int {
        const auto target_clip = clip_by_id(target_clip_id);
        const auto target_end = target_clip.value(QStringLiteral("startSeconds")).toDouble() +
                                target_clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble();
        const auto last_title_time = std::max(0.0, std::min(target_end - 1.0 / 24.0, 1.875));
        if (std::abs(last_title_time - 1.875) > 0.0001)
          return fail("Surface fixture should keep the title visible through source time 1.875 seconds");
        const auto first_picture = program_provider->requestImage(QString(), nullptr, QSize());
        const auto reference_picture = [&](double seconds) {
          QProcess decoder;
          decoder.start(QStringLiteral("ffmpeg"), {
              QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
              QStringLiteral("-ss"), QString::number(seconds, 'f', 6), QStringLiteral("-i"), source_path,
              QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-vf"),
              QStringLiteral("scale=128:96:flags=bilinear"), QStringLiteral("-f"), QStringLiteral("image2pipe"),
              QStringLiteral("-vcodec"), QStringLiteral("png"), QStringLiteral("pipe:1")});
          if (!decoder.waitForFinished(10000) || decoder.exitCode() != 0) return QImage{};
          return QImage::fromData(decoder.readAllStandardOutput(), "PNG");
        };
        const auto first_reference = reference_picture(0.0);
        const auto middle_reference = reference_picture(1.0);
        const auto last_reference = reference_picture(last_title_time);
        if (first_reference.isNull() || middle_reference.isNull() || last_reference.isNull())
          return fail("Surface source pixel references should decode independently of the native player");
        const auto picture_distance = [](const QImage& picture, const QImage& reference) {
          if (picture.isNull()) return 1.0e9;
          const auto scaled = picture.scaled(reference.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
          double sum = 0.0;
          for (int y = 0; y < reference.height(); ++y) for (int x = 0; x < reference.width(); ++x) {
            const auto actual = scaled.pixelColor(x, y), expected = reference.pixelColor(x, y);
            sum += qAbs(actual.red() - expected.red()) + qAbs(actual.green() - expected.green()) +
                   qAbs(actual.blue() - expected.blue());
          }
          return sum / (static_cast<double>(reference.width()) * reference.height() * 3.0);
        };
        const auto matches_source_frame = [&](const QImage& picture, qint64 timestamp_us) {
          if (timestamp_us < 0) return false;
          const auto seconds = static_cast<double>(timestamp_us) / 1'000'000.0;
          const auto expected = reference_picture(seconds);
          if (expected.isNull()) return false;
          const auto expected_distance = picture_distance(picture, expected);
          if (expected_distance > 18.0) return false;
          constexpr double source_frame_step = 0.125;
          const auto adjacent_seconds = seconds >= source_frame_step ? seconds - source_frame_step : seconds + source_frame_step;
          if (adjacent_seconds <= 1.875) {
            const auto adjacent = reference_picture(adjacent_seconds);
            if (!adjacent.isNull() && picture_distance(expected, adjacent) > 2.0 &&
                expected_distance >= picture_distance(picture, adjacent) * 0.8) return false;
          }
          return true;
        };
        timeline_playback.seekSequenceSeconds(last_title_time);
        const auto seek_ready = wait_for_condition([&] {
          const auto picture = program_provider->requestImage(QString(), nullptr, QSize());
          const auto cached = timeline_document.scenePreviewFrame();
          const auto cached_seconds = cached.value(QStringLiteral("sourceSeconds"), -1.0).toDouble();
          const auto published = timeline_playback.processedFrameTimestampUs();
          return std::abs(timeline_document.sequencePlayheadSeconds() - last_title_time) < 0.08 &&
              cached.value(QStringLiteral("active")).toBool() &&
              QFileInfo::exists(cached.value(QStringLiteral("path")).toString()) &&
              cached.value(QStringLiteral("sourceClipId")).toString() == source_id &&
              std::abs(cached_seconds - last_title_time) < 0.0001 &&
              qAbs(published - qRound64(cached_seconds * 1'000'000)) <= 1 && picture != first_picture &&
              picture_distance(picture, last_reference) < picture_distance(picture, first_reference) * 0.8 &&
              picture_distance(picture, last_reference) < picture_distance(picture, middle_reference) * 0.8;
        }, 10000);
        if (!seek_ready || !capture_workflow(QStringLiteral("surface-2d-exact-seek"),
                                              {QStringLiteral("timelineProgramMonitor")}))
          return fail("Paused surface preview seek should publish exact cached source pixels at 1.875 seconds");
        const auto paused_cache = timeline_document.scenePreviewFrame();
        const auto paused_published = timeline_playback.processedFrameTimestampUs();
        const auto paused_picture = program_provider->requestImage(QString(), nullptr, QSize());
        if (!QFileInfo::exists(paused_cache.value(QStringLiteral("path")).toString()) ||
            qAbs(paused_published - qRound64(paused_cache.value(QStringLiteral("sourceSeconds")).toDouble() * 1'000'000)) > 1 ||
            !matches_source_frame(paused_picture, paused_published))
          return fail("Paused surface preview pixels should match an independent FFmpeg decode at the cached timestamp");

        timeline_playback.seekSequenceSeconds(0.0);
        if (!wait_for_condition([&] {
              return timeline_playback.processedFrameTimestampUs() == 0 &&
                  !timeline_playback.processedFrameTimestampEstimated() &&
                  std::abs(timeline_document.sequencePlayheadSeconds()) < 0.0001;
            }, 10000)) return fail("Surface playback should begin from its published first source frame");
        timeline_playback.play();
        const auto playback_ready = wait_for_condition([&] {
          const auto cached = timeline_document.scenePreviewFrame();
          const auto cached_seconds = cached.value(QStringLiteral("sourceSeconds"), -1.0).toDouble();
          const auto published = timeline_playback.processedFrameTimestampUs();
          return timeline_document.sequencePlaying() && workflow_player->playbackState() == QMediaPlayer::PlayingState &&
              cached.value(QStringLiteral("active")).toBool() && QFileInfo::exists(cached.value(QStringLiteral("path")).toString()) &&
              cached.value(QStringLiteral("sourceClipId")).toString() == source_id && published >= 1'000'000 &&
              qAbs(published - qRound64(cached_seconds * 1'000'000)) <= 1;
        }, 10000);
        const auto playback_published = timeline_playback.processedFrameTimestampUs();
        const auto playback_cache = timeline_document.scenePreviewFrame();
        const auto playback_picture = program_provider->requestImage(QString(), nullptr, QSize());
        surface_playback_sample_seconds = static_cast<double>(playback_published) / 1'000'000;
        timeline_playback.pause();
        if (!playback_ready || !QFileInfo::exists(playback_cache.value(QStringLiteral("path")).toString()) ||
            qAbs(playback_published - qRound64(playback_cache.value(QStringLiteral("sourceSeconds")).toDouble() * 1'000'000)) > 1 ||
            !matches_source_frame(playback_picture, playback_published))
          return fail(qPrintable(QStringLiteral("Surface playback frame mismatch: ready=%1, published=%2, selectedCache=%3").arg(playback_ready).arg(playback_published).arg(playback_cache.value(QStringLiteral("sourceSeconds"), -1).toDouble())));

        timeline_document.selectClip(source_clip_index);
        const auto source_before = clip_by_id(source_id);
        const auto trim_before = source_before.value(QStringLiteral("trimStartSeconds")).toDouble();
        const auto trim_after = trim_before + 0.25;
        timeline_document.setSelectedTrimStartSeconds(trim_after);
        timeline_playback.seekSequenceSeconds(source_before.value(QStringLiteral("startSeconds")).toDouble());
        const auto trim_ready = wait_for_condition([&] {
          const auto cached = timeline_document.scenePreviewFrame();
          const auto seconds = cached.value(QStringLiteral("sourceSeconds"), -1.0).toDouble();
          const auto published = timeline_playback.processedFrameTimestampUs();
          return cached.value(QStringLiteral("active")).toBool() && QFileInfo::exists(cached.value(QStringLiteral("path")).toString()) &&
              cached.value(QStringLiteral("sourceClipId")).toString() == source_id && std::abs(seconds - trim_after) < 0.0001 &&
              qAbs(published - qRound64(seconds * 1'000'000)) <= 1;
        }, 10000);
        const auto trim_cache = timeline_document.scenePreviewFrame();
        const auto trim_published = timeline_playback.processedFrameTimestampUs();
        const auto trim_picture = program_provider->requestImage(QString(), nullptr, QSize());
        timeline_document.setSelectedTrimStartSeconds(trim_before);
        if (!trim_ready || !QFileInfo::exists(trim_cache.value(QStringLiteral("path")).toString()) ||
            qAbs(trim_published - qRound64(trim_cache.value(QStringLiteral("sourceSeconds")).toDouble() * 1'000'000)) > 1 ||
            !matches_source_frame(trim_picture, trim_published))
          return fail("Trimmed surface source should publish pixels from its updated source-time origin");
        scene_camera_proof = true;
        scene_camera_timing_estimated = timeline_playback.processedFrameTimestampEstimated();
        return EXIT_SUCCESS;
      };
      const auto surface_workflow_proof = [&]() -> int {
        const auto fixture_path = qEnvironmentVariable("PHOTO_EDITOR_SURFACE_TIMELINE_FIXTURE");
        if (fixture_path.isEmpty())
          return fail("PHOTO_EDITOR_SMOKE_SURFACE_ONLY requires PHOTO_EDITOR_SURFACE_TIMELINE_FIXTURE");
        const auto fixture_file = QFileInfo(fixture_path).absoluteFilePath();
        if (!QFileInfo::exists(fixture_file))
          return fail("PHOTO_EDITOR_SURFACE_TIMELINE_FIXTURE should name a saved editable surface timeline");
        const auto active_fixture = QFileInfo(timeline_document.savedDocumentPath()).absoluteFilePath();
        if (active_fixture != fixture_file &&
            (!timeline_document.loadTimeline(fixture_file) ||
             !wait_for_condition([&] { return timeline_document.hasActiveTimeline(); }, 10000)))
          return fail("Surface proof should load its editable timeline");
        studio_root->setProperty("currentWorkspace", studio_root->property("timelineWorkspaceIndex"));
        if (!wait_for_condition([&] {
              auto* loader = find_named_object(studio_root, QStringLiteral("timelineWorkspaceLoader"));
              auto* workspace = loader ? loader->property("item").value<QObject*>() : nullptr;
              if (!loader || loader->property("status").toInt() != 1 || !workspace ||
                  !workspace->property("workspacePanelSettingsInitialized").toBool()) return false;
              workflow_timeline = workspace;
              auto* editor_loader = find_named_object(workspace, QStringLiteral("timelineEditorSurfaceLoader"));
              return editor_loader && editor_loader->property("status").toInt() == 1 &&
                  item_visible(find_named_item(workspace, QStringLiteral("timelineViewport")));
          }, 25000))
          return fail("Surface proof should show its editable timeline in the native shell");
        if (!wait_for_condition([&] {
              const auto state = photo_editor::desktop::TimelinePlaybackFrameTestAccess::readiness(timeline_playback);
              return timeline_playback.mediaReady() && !state.value(QStringLiteral("waitingForPosition")).toBool();
            }, 10000)) {
          report_playback();
          return fail("Loaded surface fixture should reach native media readiness");
        }
        timeline_playback.pause();
        workflow_timeline->setProperty("timelineZoom", 140.0);
        process_layout_events(10);

        QVariantMap source;
        for (const auto& candidate : timeline_document.surfaceTrackSources()) {
          const auto row = candidate.toMap();
          if (!row.value(QStringLiteral("clipId")).toString().isEmpty() &&
              row.value(QStringLiteral("error")).toString().isEmpty()) {
            source = row;
            break;
          }
        }
        if (source.isEmpty()) return fail("Surface fixture should contain an analyzed tracked video source");
        const auto source_id = source.value(QStringLiteral("clipId")).toString();
        int source_clip_index = -1;
        int target_clip_index = -1;
        QString target_clip_id;
        int flattened_index = 0;
        for (const auto& row_value : timeline_document.timelineTracks()) {
          for (const auto& clip_value : row_value.toMap().value(QStringLiteral("clips")).toList()) {
            const auto clip = clip_value.toMap();
            if (clip.value(QStringLiteral("clipId")).toString() == source_id) source_clip_index = flattened_index;
            const auto media_type = clip.value(QStringLiteral("mediaType")).toString();
            if (target_clip_index < 0 && media_type == QStringLiteral("title") &&
                !clip.value(QStringLiteral("clipId")).toString().isEmpty()) {
              target_clip_index = flattened_index;
              target_clip_id = clip.value(QStringLiteral("clipId")).toString();
            }
            ++flattened_index;
          }
        }
        if (source_clip_index < 0 || target_clip_index < 0)
          return fail("Surface fixture should include its tracked video and editable title target");
        if (!capture_workflow(QStringLiteral("surface-2d-before-attachment"),
                              {QStringLiteral("timelineProgramMonitor")}))
          return fail("Surface fixture should render the native Timeline preview");

        const auto click_clip_by_id = [&](const QString& id) {
          for (auto* item : find_named_items(studio_root, QStringLiteral("timelineClip-"))) {
            const auto model = item->property("modelData").toMap();
            if (model.value(QStringLiteral("clipId")).toString() == id) return click_item(item);
          }
          for (const auto& row : timeline_document.timelineTracks()) {
            for (const auto& value : row.toMap().value(QStringLiteral("clips")).toList()) {
              const auto clip = value.toMap();
              if (clip.value(QStringLiteral("clipId")).toString() != id) continue;
              for (auto* item : find_named_items(studio_root,
                       QStringLiteral("timelineClip-") + clip.value(QStringLiteral("displayLabel")).toString()))
                if (item_visible(item) && item->property("modelData").toMap().value(QStringLiteral("clipId")).toString() == id)
                  return click_item(item);
            }
          }
          return false;
        };
        const auto open_tracking = [&] {
          QMetaObject::invokeMethod(workflow_timeline, "openAutoReframe", Q_ARG(QVariant, QVariant{}));
          return wait_for_condition([&] { return live_item(QStringLiteral("timelineOpenTrackingToolsButton")); }, 5000) &&
              click_named(QStringLiteral("timelineOpenTrackingToolsButton"));
        };
        if (!click_clip_by_id(source_id) ||
            !wait_for_condition([&] { return timeline_document.currentClip().value(QStringLiteral("clipId")).toString() == source_id; }, 3000) ||
            !open_tracking() ||
            !wait_for_condition([&] {
              auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup"));
              return popup && popup->property("visible").toBool() &&
                  live_item(QStringLiteral("timelineSurfaceTrackSourceCombo")) &&
                  live_item(QStringLiteral("timelineSurfaceTrackSourceCombo"))->property("count").toInt() > 0;
            }, 5000))
          return fail("Surface proof should pointer-select its source and open the native tracking panel");
        auto* source_combo = live_item(QStringLiteral("timelineSurfaceTrackSourceCombo"));
        const auto source_rows = timeline_document.surfaceTrackSources();
        int source_row_index = -1;
        for (int index = 0; index < source_rows.size(); ++index)
          if (source_rows[index].toMap().value(QStringLiteral("clipId")).toString() == source_id) source_row_index = index;
        if (!source_combo || source_row_index < 0)
          return fail("Surface panel should offer the analyzed source for attachment");
        const auto source_option_name = QStringLiteral("timelineSurfaceTrackSourceComboOption") + QString::number(source_row_index);
        if (!click_item(source_combo) ||
            !wait_for_condition([&] { return live_item(source_option_name); }, 3000) ||
            !click_named(source_option_name) || source_combo->property("currentIndex").toInt() != source_row_index ||
            source_combo->property("currentText").toString().isEmpty())
          return fail("Surface proof should choose the existing tracked source in the native panel");
        if (!click_named(QStringLiteral("timelineAddSurfaceExclusionButton")) ||
            !wait_for_condition([&] {
              auto* image = live_item(QStringLiteral("timelineMotionSeedSourceFrame"));
              return workflow_timeline->property("motionSeedSelectionHudVisible").toBool() &&
                  !workflow_timeline->property("motionSeedAwaitingFirstFrame").toBool() && image &&
                  image->property("status").toInt() == 1;
            }, 10000))
          return fail("Surface repair should open a native exclusion region on the tracked source frame");
        auto* seed_layer = live_item(QStringLiteral("timelineMotionSeedSelectionLayer"));
        if (!seed_layer) return fail("Surface exclusion selection should expose its source-frame bounds");
        const auto seed_bounds = seed_layer->property("sourceBounds").toRectF();
        const auto exclusion_start = seed_layer->mapToScene(
            QPointF(seed_bounds.x() + seed_bounds.width() * 0.08, seed_bounds.y() + seed_bounds.height() * 0.08));
        const auto exclusion_end = seed_layer->mapToScene(
            QPointF(seed_bounds.x() + seed_bounds.width() * 0.105, seed_bounds.y() + seed_bounds.height() * 0.105));
        workflow_pointer_timestamp += static_cast<ulong>(QApplication::doubleClickInterval() + 1);
        send_pointer(QEvent::MouseButtonPress, exclusion_start);
        for (int step = 1; step <= 4; ++step)
          send_pointer(QEvent::MouseMove, exclusion_start + (exclusion_end - exclusion_start) * (step / 4.0));
        send_pointer(QEvent::MouseButtonRelease, exclusion_end);
        if (!wait_for_condition([&] {
              const auto rect = workflow_timeline->property("motionSeedRectNormalized").toMap();
              return rect.value(QStringLiteral("width")).toDouble() >= 0.015 &&
                  rect.value(QStringLiteral("height")).toDouble() >= 0.015;
            }, 2000) || !click_named(QStringLiteral("timelineMotionSeedHudDoneButton")) ||
            !wait_for_condition([&] {
              auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup"));
              auto* count = live_item(QStringLiteral("timelineSurfaceExclusionCount"));
              return popup && popup->property("visible").toBool() && count && count->property("text").toString().contains("1");
            }, 5000) ||
            !capture_workflow(QStringLiteral("surface-2d-retrack-exclusion"),
                              {QStringLiteral("timelineSurfaceTrackingSection"),
                               QStringLiteral("timelineRetrackSurfaceButton")}) ||
            !click_named(QStringLiteral("timelineRetrackSurfaceButton")))
          return fail("Surface repair should save an exclusion and start Retrack from the native panel");
        bool saw_retrack_busy = false;
        const auto retrack_ready = wait_for_condition([&] {
          saw_retrack_busy = saw_retrack_busy || timeline_document.trackingBusy();
          const auto rows = timeline_document.surfaceTrackSources();
          const auto source_clip = clip_by_id(source_id);
          const auto track = source_clip.value(QStringLiteral("motionTracking")).toMap().value(QStringLiteral("surfaceTrack")).toMap();
          const auto seed = track.value(QStringLiteral("seed")).toMap();
          return saw_retrack_busy && !timeline_document.trackingBusy() &&
              source_clip.value(QStringLiteral("surfaceTrackReady")).toBool() &&
              seed.value(QStringLiteral("exclusions")).toList().size() == 1 &&
              std::any_of(rows.begin(), rows.end(), [&](const QVariant& value) {
                return value.toMap().value(QStringLiteral("clipId")).toString() == source_id;
              });
        }, 30000);
        if (!retrack_ready)
          return fail(qPrintable(QStringLiteral("Surface Retrack should finish with its exclusion saved and source track available: %1").arg(timeline_document.trackingStatus())));
        surface_retrack_proof = true;

        timeline_playback.pause();
        timeline_playback.seekSequenceSeconds(0.0);
        if (!wait_for_condition([&] {
              return timeline_playback.processedFrameTimestampUs() == 0 &&
                  !timeline_playback.processedFrameTimestampEstimated() &&
                  std::abs(timeline_document.currentClipPlayheadSeconds()) < 0.0001 &&
                  workflow_timeline->property("surfaceCorrectionFrameReady").toBool();
            }, 10000))
          return fail("Surface correction should be enabled on the exact cached first source frame");
        if (!click_named(QStringLiteral("timelineCorrectSurfaceAtPlayheadButton")) ||
            !wait_for_condition([&] {
              return item_visible(live_item(QStringLiteral("timelineSurfaceCorrectionOverlay"))) &&
                  live_item(QStringLiteral("timelineSurfaceCorrectionHandle0")) &&
                  live_item(QStringLiteral("timelineSurfaceCorrectionApplyButton"));
            }, 5000) ||
            !capture_workflow(QStringLiteral("surface-2d-corner-correction"),
                              {QStringLiteral("timelineSurfaceCorrectionOverlay"),
                               QStringLiteral("timelineSurfaceCorrectionApplyButton")}))
          return fail("Surface correction should open its native four-corner overlay");
        auto* top_left_handle = live_item(QStringLiteral("timelineSurfaceCorrectionHandle0"));
        const auto handle_center = scene_rect(top_left_handle).center();
        workflow_pointer_timestamp += static_cast<ulong>(QApplication::doubleClickInterval() + 1);
        send_pointer(QEvent::MouseButtonPress, handle_center);
        send_pointer(QEvent::MouseMove, handle_center + QPointF(2.0, 2.0));
        send_pointer(QEvent::MouseButtonRelease, handle_center + QPointF(2.0, 2.0));
        const auto previous_surface_solve = clip_by_id(source_id).value(QStringLiteral("motionTracking")).toMap()
            .value(QStringLiteral("surfaceTrack")).toMap().value(QStringLiteral("solveId")).toString();
        if (!click_named(QStringLiteral("timelineSurfaceCorrectionApplyButton")))
          return fail("Surface correction should accept the adjusted corner and retrack it");
        bool saw_correction_busy = false;
        const auto correction_ready = wait_for_condition([&] {
          saw_correction_busy = saw_correction_busy || timeline_document.trackingBusy();
          const auto source_clip = clip_by_id(source_id);
          const auto track = source_clip.value(QStringLiteral("motionTracking")).toMap().value(QStringLiteral("surfaceTrack")).toMap();
          const auto seed = track.value(QStringLiteral("seed")).toMap();
          bool has_first_frame_correction = false;
          for (const auto& correction : seed.value(QStringLiteral("correctionFrames")).toList())
            has_first_frame_correction = has_first_frame_correction || correction.toMap().value(QStringLiteral("frameIndex")).toInt() == 0;
          return saw_correction_busy && !timeline_document.trackingBusy() &&
              source_clip.value(QStringLiteral("surfaceTrackReady")).toBool() && has_first_frame_correction &&
              !track.value(QStringLiteral("solveId")).toString().isEmpty() &&
              track.value(QStringLiteral("solveId")).toString() != previous_surface_solve;
        }, 30000);
        if (!correction_ready)
          return fail(qPrintable(QStringLiteral("Correct and retrack should persist the frame-zero corner correction and rebuild the surface track: %1 (busy observed %2)").arg(timeline_document.trackingStatus()).arg(saw_correction_busy)));
        surface_correction_proof = true;
        if (auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup")))
          QMetaObject::invokeMethod(popup, "close");
        if (!click_clip_by_id(target_clip_id) ||
            !wait_for_condition([&] { return timeline_document.currentClip().value(QStringLiteral("clipId")).toString() == target_clip_id; }, 3000) ||
            !open_tracking() ||
            !wait_for_condition([&] {
              auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup"));
              auto* attach = live_item(QStringLiteral("timelineAttachSurfaceTrackButton"));
              return popup && popup->property("visible").toBool() && attach && attach->property("enabled").toBool();
            }, 5000) || !click_named(QStringLiteral("timelineAttachSurfaceTrackButton")) ||
            !wait_for_condition([&] {
              const auto clip = clip_by_id(target_clip_id);
              const auto attachment = clip.value(QStringLiteral("sceneAttachment")).toMap();
              return clip.value(QStringLiteral("sceneAttachmentEnabled")).toBool() &&
                  clip.value(QStringLiteral("sceneAttachmentReady")).toBool() &&
                  attachment.value(QStringLiteral("mode")).toString() == QStringLiteral("surface2d") &&
                  attachment.value(QStringLiteral("sourceClipId")).toString() == source_id;
            }, 10000))
          return fail("Native Attach surface should attach the title to the chosen analyzed source");
        if (surface_occlusion_mode) {
          const auto source_cutout_enabled_before =
              clip_by_id(source_id).value(QStringLiteral("foregroundCutoutEnabled")).toBool();
          if (!clip_by_id(source_id).value(QStringLiteral("foregroundCutoutReady")).toBool())
            return fail("PHOTO_EDITOR_SMOKE_OCCLUSION requires a ready source foreground mask in the surface fixture");
          if (source_cutout_enabled_before)
            return fail("Occlusion proof fixture should have a ready foreground mask with source cutout disabled");
          auto* occlusion_checkbox = live_item(QStringLiteral("timelineSurfaceAttachmentOcclusionCheckBox"));
          if (!occlusion_checkbox || !occlusion_checkbox->property("visible").toBool() ||
              !occlusion_checkbox->property("enabled").toBool() ||
              !click_named(QStringLiteral("timelineSurfaceAttachmentOcclusionCheckBox")) ||
              !wait_for_condition([&] {
                const auto attachment = clip_by_id(target_clip_id).value(QStringLiteral("sceneAttachment")).toMap();
                return attachment.value(QStringLiteral("occlusionEnabled")).toBool() &&
                    clip_by_id(source_id).value(QStringLiteral("foregroundCutoutEnabled")).toBool() == source_cutout_enabled_before;
              }, 5000) ||
              !capture_workflow(QStringLiteral("surface-2d-occlusion-enabled"),
                                {QStringLiteral("timelineProgramMonitor")}))
            return fail("Surface attachment occlusion should enable from the native checkbox without changing source cutout state");
          if (!click_named(QStringLiteral("timelineSurfaceAttachmentOcclusionCheckBox")) ||
              !wait_for_condition([&] {
                const auto attachment = clip_by_id(target_clip_id).value(QStringLiteral("sceneAttachment")).toMap();
                return !attachment.value(QStringLiteral("occlusionEnabled")).toBool() &&
                    clip_by_id(source_id).value(QStringLiteral("foregroundCutoutEnabled")).toBool() == source_cutout_enabled_before;
              }, 5000) ||
              !capture_workflow(QStringLiteral("surface-2d-occlusion-disabled"),
                                {QStringLiteral("timelineProgramMonitor")}))
            return fail("Surface attachment occlusion should disable independently from the source foreground cutout");
          surface_attachment_occlusion_proof = true;
        }
        if (auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup")))
          QMetaObject::invokeMethod(popup, "close");
        timeline_playback.seekSequenceSeconds(0.0);
        if (!wait_for_condition([&] {
              return clip_by_id(target_clip_id).value(QStringLiteral("sceneAttachmentQuad")).toList().size() == 4 &&
                  timeline_playback.processedFrameTimestampUs() >= 0 && timeline_playback.processedFrameTimestampUs() <= 60'000;
            }, 10000) ||
            !capture_workflow(QStringLiteral("surface-2d-attached-first-frame"),
                              {QStringLiteral("timelineProgramMonitor")}))
          return fail("Attached surface title should render at its first frame");
        return run_surface_preview_checks(source_id, target_clip_id, source.value(QStringLiteral("filePath")).toString(), source_clip_index);
      };
      const auto run_scene_camera_workflow = [&]() -> int {
        const auto scene_fixture_path = qEnvironmentVariable("PHOTO_EDITOR_SCENE_TIMELINE_FIXTURE");
        if (scene_fixture_path.isEmpty())
          return scene_only_mode ? fail("PHOTO_EDITOR_SMOKE_SCENE_ONLY requires PHOTO_EDITOR_SCENE_TIMELINE_FIXTURE")
                            : EXIT_SUCCESS;
        if (!wait_for_condition([&] {
              const auto state = photo_editor::desktop::TimelinePlaybackFrameTestAccess::readiness(timeline_playback);
              return timeline_playback.mediaReady() && !state.value(QStringLiteral("waitingForPosition")).toBool();
            }, 10000)) {
          report_playback();
          return fail("Scene fixture should be media-ready before pointer and cached-frame proof");
        }
        const auto fixture_file = QFileInfo(scene_fixture_path).absoluteFilePath();
        if (!QFileInfo::exists(fixture_file))
          return fail("PHOTO_EDITOR_SCENE_TIMELINE_FIXTURE should name an editable solved scene timeline");
        const auto active_fixture = QFileInfo(timeline_document.savedDocumentPath()).absoluteFilePath();
        if (active_fixture != fixture_file &&
            (!timeline_document.loadTimeline(fixture_file) ||
             !wait_for_condition([&] { return timeline_document.hasActiveTimeline(); }, 10000))) {
          std::cerr << "Scene fixture load status: " << timeline_document.status().toStdString() << '\n';
          return fail("Scene camera proof should load its editable timeline");
        }
        // The setup page has the same objectName as the editor. Select the
        // visible, initialized editor instead of a retained hidden setup page.
        studio_root->setProperty("currentWorkspace", studio_root->property("timelineWorkspaceIndex"));
        if (!wait_for_condition([&] {
              auto* loader = find_named_object(studio_root, QStringLiteral("timelineWorkspaceLoader"));
              auto* workspace = loader ? loader->property("item").value<QObject*>() : nullptr;
              if (!loader || loader->property("status").toInt() != 1 || !workspace ||
                  !workspace->property("workspacePanelSettingsInitialized").toBool()) return false;
              workflow_timeline = workspace;
              auto* editor_loader = find_named_object(workspace, QStringLiteral("timelineEditorSurfaceLoader"));
              return editor_loader && editor_loader->property("status").toInt() == 1 &&
                  item_visible(find_named_item(workspace, QStringLiteral("timelineViewport")));
            }, 25000)) {
          std::cerr << "Scene fixture workspace: hasActive=" << timeline_document.hasActiveTimeline()
                    << " status=" << timeline_document.status().toStdString() << '\n';
          for (const auto& name : {QStringLiteral("timelineEditorLoadingSurface"), QStringLiteral("timelineWorkspaceLoadingSurface"), QStringLiteral("timelineViewport")}) {
            auto* item = find_named_item(studio_root, name);
            std::cerr << name.toStdString() << " exists=" << (item != nullptr) << " visible=" << (item && item_visible(item)) << '\n';
          }
          return fail("Scene camera proof should show its editable timeline in the native shell");
        }
        if (!workflow_timeline) return fail("Scene proof should use the current live Timeline workspace");
        if (!wait_for_condition([&] {
              const auto state = photo_editor::desktop::TimelinePlaybackFrameTestAccess::readiness(timeline_playback);
              return timeline_playback.mediaReady() && !state.value(QStringLiteral("waitingForPosition")).toBool();
            }, 10000)) {
          report_playback();
          return fail("Loaded scene fixture should reach native media readiness after its editor route opens");
        }
        timeline_playback.pause();
        workflow_timeline->setProperty("timelineZoom", 140.0);
        process_layout_events(10);
        const auto open_scene_tracking = [&] {
          QMetaObject::invokeMethod(workflow_timeline, "openAutoReframe", Q_ARG(QVariant, QVariant{}));
          return wait_for_condition([&] {
            return live_item(QStringLiteral("timelineOpenTrackingToolsButton"));
          }, 5000) && click_named(QStringLiteral("timelineOpenTrackingToolsButton"));
        };

        const auto scene_sources = timeline_document.property("sceneTrackSources").toList();
        QVariantMap scene_source;
        for (const auto& candidate : scene_sources) {
          const auto row = candidate.toMap();
          if (row.value(QStringLiteral("points")).toList().size() >= 3 &&
              row.value(QStringLiteral("pointCount")).toInt() >= 3 &&
              row.value(QStringLiteral("error")).toString().isEmpty()) {
            scene_source = row;
            break;
          }
        }
        if (scene_source.isEmpty())
          return fail("Scene camera fixture should contain at least three reconstructed source points");
        const auto source_id = scene_source.value(QStringLiteral("clipId")).toString();
        int source_clip_index = -1;
        int target_clip_index = -1;
        QString target_clip_id;
        int flattened_index = 0;
        for (const auto& row_value : timeline_document.timelineTracks()) {
          for (const auto& clip_value : row_value.toMap().value(QStringLiteral("clips")).toList()) {
            const auto clip = clip_value.toMap();
            if (clip.value(QStringLiteral("clipId")).toString() == source_id)
              source_clip_index = flattened_index;
            const auto media_type = clip.value(QStringLiteral("mediaType")).toString();
            if (target_clip_index < 0 &&
                (media_type == QStringLiteral("title") || media_type == QStringLiteral("caption")) &&
                !clip.value(QStringLiteral("clipId")).toString().isEmpty()) {
              target_clip_index = flattened_index;
              target_clip_id = clip.value(QStringLiteral("clipId")).toString();
            }
            ++flattened_index;
          }
        }
        if (source_clip_index < 0 || target_clip_index < 0)
          return fail("Scene camera fixture should include its source video and editable title target");
        if (!capture_workflow(QStringLiteral("12a-scene-camera-attached-first-frame"),
                              {QStringLiteral("timelineProgramMonitor")}))
          return fail("Scene camera fixture should render the native Timeline preview");

        timeline_document.selectClip(target_clip_index);
        auto* scene_panel = find_named_object(workflow_timeline, QStringLiteral("timelineTrackingPanel"));
        if (scene_panel) scene_panel->setProperty("sceneCameraDetailsExpanded", true);
        if (!open_scene_tracking() ||
            !wait_for_condition([&] {
              auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup"));
              return popup && popup->property("visible").toBool() &&
                     live_item(QStringLiteral("timelineDetachSceneTrackButton"));
            }, 3000) || !click_named(QStringLiteral("timelineDetachSceneTrackButton")) ||
            !wait_for_condition([&] {
              const auto clip = clip_by_id(target_clip_id);
              return clip.value(QStringLiteral("sceneAttachmentEnabled")).toBool() == false;
            }, 5000))
          return fail("Scene proof should detach the fixture title before exercising point selection and attach");
        if (auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup")))
          QMetaObject::invokeMethod(popup, "close");

        timeline_document.selectClip(source_clip_index);
        const auto source_clip_for_points = clip_by_id(source_id);
        constexpr double point_selection_source_seconds = 0.5;
        const auto point_selection_sequence_seconds =
            source_clip_for_points.value(QStringLiteral("startSeconds")).toDouble() +
            point_selection_source_seconds -
            source_clip_for_points.value(QStringLiteral("trimStartSeconds")).toDouble();
        timeline_playback.pause();
        timeline_playback.seekSequenceSeconds(point_selection_sequence_seconds);
        if (!wait_for_condition([&] {
              const auto cached = timeline_document.scenePreviewFrame();
              const auto seconds = cached.value(QStringLiteral("sourceSeconds"), -1.0).toDouble();
              return cached.value(QStringLiteral("active")).toBool() &&
                  cached.value(QStringLiteral("sourceClipId")).toString() == source_id &&
                  QFileInfo::exists(cached.value(QStringLiteral("path")).toString()) &&
                  std::abs(seconds - point_selection_source_seconds) < 0.0001 &&
                  qAbs(timeline_playback.processedFrameTimestampUs() -
                       qRound64(seconds * 1'000'000)) <= 1;
            }, 10000))
          return fail("Scene point selection should seek to the exact cached source frame at 0.5 seconds");
        const auto exact_point_frame = timeline_document.sceneTrackPointFrame(
            source_id, point_selection_sequence_seconds);
        if (!exact_point_frame.value(QStringLiteral("success")).toBool() ||
            std::abs(exact_point_frame.value(QStringLiteral("sourceSeconds")).toDouble() -
                     point_selection_source_seconds) >= 0.0001 ||
            !QFileInfo::exists(QUrl(exact_point_frame.value(QStringLiteral("path")).toString()).toLocalFile()))
          return fail("Scene point frame API should return its cached image and exact 0.5-second source timestamp");
        const auto first_frame_points = scene_source.value(QStringLiteral("points")).toList();
        const auto middle_frame_points = exact_point_frame.value(QStringLiteral("points")).toList();
        bool saw_moved_point = false;
        for (const auto& middle_value : middle_frame_points) {
          const auto middle = middle_value.toMap();
          for (const auto& first_value : first_frame_points) {
            const auto first = first_value.toMap();
            if (middle.value(QStringLiteral("id")) == first.value(QStringLiteral("id")) &&
                (qAbs(middle.value(QStringLiteral("x")).toDouble() -
                      first.value(QStringLiteral("x")).toDouble()) > 0.005 ||
                 qAbs(middle.value(QStringLiteral("y")).toDouble() -
                      first.value(QStringLiteral("y")).toDouble()) > 0.005)) {
              saw_moved_point = true;
              break;
            }
          }
          if (saw_moved_point) break;
        }
        if (!saw_moved_point)
          return fail("Scene point selection fixture should show feature coordinates changed from first frame to source time 0.5 seconds");

        if (!open_scene_tracking() ||
            !wait_for_condition([&] {
              auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup"));
              return popup && popup->property("visible").toBool() &&
                     live_item(QStringLiteral("timelineSelectScenePlanePointsButton")) &&
                     live_item(QStringLiteral("timelineSelectScenePlanePointsButton"))->property("enabled").toBool();
            }, 5000) || !capture_workflow(QStringLiteral("12b-scene-camera-point-tools"),
                                          {QStringLiteral("timelineTrackingPanel"),
                                           QStringLiteral("timelineSelectScenePlanePointsButton")}) ||
            !click_named(QStringLiteral("timelineSelectScenePlanePointsButton")) ||
            !wait_for_condition([&] {
              auto* image = live_item(QStringLiteral("timelineScenePointSourceFrame"));
              return workflow_timeline->property("scenePointSelectionHudVisible").toBool() &&
                     !workflow_timeline->property("scenePointFrameAwaiting").toBool() && image &&
                     image->property("status").toInt() == 1;
            }, 10000) || !capture_workflow(QStringLiteral("12c-scene-camera-source-points-0.5s"),
                                           {QStringLiteral("timelineScenePointSelectionHud"),
                                            QStringLiteral("timelineScenePointOverlay")}))
          return fail("Scene proof should show the cached 0.5-second source-frame point-selection overlay in the native monitor");

        const auto points = workflow_timeline->property("scenePointFramePoints").toList();
        if (points != middle_frame_points ||
            std::abs(workflow_timeline->property("scenePointFrameSourceSeconds").toDouble() -
                     point_selection_source_seconds) >= 0.0001 ||
            workflow_timeline->property("scenePointFrameSource").toString() !=
                exact_point_frame.value(QStringLiteral("path")).toString())
          return fail("Scene point overlay should use the controller's exact cached 0.5-second frame and points");
        scene_point_selection_source_seconds =
            workflow_timeline->property("scenePointFrameSourceSeconds").toDouble();
        const auto nearest_point = [&](double x, double y, const QStringList& exclude) {
          QVariantMap nearest;
          double best_distance = 1e9;
          for (const auto& point_value : points) {
            const auto point = point_value.toMap();
            if (exclude.contains(point.value(QStringLiteral("id")).toString()))
              continue;
            const auto dx = point.value(QStringLiteral("x")).toDouble() - x;
            const auto dy = point.value(QStringLiteral("y")).toDouble() - y;
            const auto distance = dx * dx + dy * dy;
            if (distance < best_distance) {
              best_distance = distance;
              nearest = point;
            }
          }
          return nearest;
        };
        QStringList chosen_point_ids;
        for (const auto& target : {QPointF(0.43, 0.48), QPointF(0.62, 0.48), QPointF(0.43, 0.66)}) {
          const auto point = nearest_point(target.x(), target.y(), chosen_point_ids);
          const auto id = point.value(QStringLiteral("id")).toString();
          if (id.isEmpty()) return fail("Scene proof should resolve three stable reconstructed feature IDs");
          chosen_point_ids.append(id);
          auto* overlay = live_item(QStringLiteral("timelineScenePointOverlay"));
          auto* selection_layer = live_item(QStringLiteral("timelineScenePointSelectionLayer"));
          if (!overlay || !selection_layer)
            return fail("Scene point overlay should stay visible while selecting source features");
          const auto bounds = selection_layer->property("sourceBounds").toRectF();
          const auto scene_point = selection_layer->mapToScene(
              QPointF(bounds.x() + point.value(QStringLiteral("x")).toDouble() * bounds.width(),
                      bounds.y() + point.value(QStringLiteral("y")).toDouble() * bounds.height()));
          workflow_pointer_timestamp += static_cast<ulong>(QApplication::doubleClickInterval() + 1);
          send_pointer(QEvent::MouseButtonPress, scene_point);
          send_pointer(QEvent::MouseButtonRelease, scene_point);
          if (!wait_for_condition([&] {
                return workflow_timeline->property("scenePointSelectionDraftIds").toStringList().size() ==
                       chosen_point_ids.size();
              }, 2000))
            return fail("Native point clicks should select each stable feature ID in order");
        }
        if (!workflow_timeline->property("scenePointSelectionDraftIds").toStringList().contains(chosen_point_ids.at(0)) ||
            !click_named(QStringLiteral("timelineScenePointHudDoneButton")) ||
            !wait_for_condition([&] {
              return !workflow_timeline->property("scenePointSelectionHudVisible").toBool() &&
                     workflow_timeline->property("scenePointSelectionIds").toStringList() == chosen_point_ids;
            }, 3000))
          return fail("Scene proof should preserve the ordered three-point plane selection after Done");
        if (auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup")))
          QMetaObject::invokeMethod(popup, "close");

        timeline_document.selectClip(target_clip_index);
        if (!open_scene_tracking() ||
            !wait_for_condition([&] {
              auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup"));
              auto* attach = live_item(QStringLiteral("timelineAttachSceneTrackButton"));
              return popup && popup->property("visible").toBool() && attach && attach->property("enabled").toBool();
            }, 5000) || !click_named(QStringLiteral("timelineAttachSceneTrackButton")) ||
            !wait_for_condition([&] {
              const auto clip = clip_by_id(target_clip_id);
              return clip.value(QStringLiteral("sceneAttachmentEnabled")).toBool() &&
                     clip.value(QStringLiteral("sceneAttachmentReady")).toBool();
            }, 10000))
          return fail("Scene proof should attach the selected title to the chosen source plane");
        if (auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup")))
          QMetaObject::invokeMethod(popup, "close");
        timeline_playback.seekSequenceSeconds(0.0);
        if (!wait_for_condition([&] {
              const auto clip = clip_by_id(target_clip_id);
              return clip.value(QStringLiteral("sceneAttachmentQuad")).toList().size() == 4 &&
                  timeline_playback.processedFrameTimestampUs() >= 0 &&
                  timeline_playback.processedFrameTimestampUs() <= 60'000;
            }, 10000) ||
            !capture_workflow(QStringLiteral("12d-scene-camera-attached-first-frame"),
                              {QStringLiteral("timelineProgramMonitor")})) {
          std::cerr << "First scene frame: source=" << workflow_player->source().toString().toStdString()
                    << " position=" << workflow_player->position()
                    << " displayedTimestampUs=" << timeline_playback.processedFrameTimestampUs()
                    << " available=" << !timeline_playback.processedFrameSource().isEmpty()
                    << " mediaReady=" << timeline_playback.mediaReady()
                    << " workspaceActive=" << timeline_playback.workspaceActive() << '\n';
          report_playback();
          return fail("Attached scene title should render as a projective quad at its first frame");
        }
        const auto target_clip = clip_by_id(target_clip_id);
        const auto target_end = target_clip.value(QStringLiteral("startSeconds")).toDouble() +
                                target_clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble();
        const auto last_title_time = std::max(0.0, std::min(target_end - 1.0 / 24.0, 1.875));
        if (std::abs(last_title_time - 1.875) > 0.0001)
          return fail("Scene camera fixture should keep the title visible through source time 1.875 seconds");
        const auto first_scene_picture = program_provider->requestImage(QString(), nullptr, QSize());
        const auto reference_picture = [&](const QString& path, double seconds) {
          QProcess decoder;
          decoder.start(QStringLiteral("ffmpeg"), {
              QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
              QStringLiteral("-ss"), QString::number(seconds, 'f', 6),
              QStringLiteral("-i"), path,
              QStringLiteral("-frames:v"), QStringLiteral("1"),
              QStringLiteral("-vf"), QStringLiteral("scale=128:96:flags=bilinear"),
              QStringLiteral("-f"), QStringLiteral("image2pipe"),
              QStringLiteral("-vcodec"), QStringLiteral("png"), QStringLiteral("pipe:1")});
          if (!decoder.waitForFinished(10000) || decoder.exitCode() != 0) return QImage{};
          return QImage::fromData(decoder.readAllStandardOutput(), "PNG");
        };
        const auto source_path = scene_source.value(QStringLiteral("filePath")).toString();
        const auto reference_first = reference_picture(source_path, 0.0);
        const auto reference_middle = reference_picture(source_path, 1.0);
        const auto reference_last = reference_picture(source_path, last_title_time);
        if (reference_first.isNull() || reference_middle.isNull() || reference_last.isNull())
          return fail("Scene source pixel references should decode independently of the native player");
        const auto picture_distance = [](const QImage& picture, const QImage& reference) {
          if (picture.isNull()) return 1.0e9;
          const auto scaled = picture.scaled(reference.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
          double sum = 0.0;
          for (int y = 0; y < reference.height(); ++y) {
            for (int x = 0; x < reference.width(); ++x) {
              const auto actual = scaled.pixelColor(x, y);
              const auto expected = reference.pixelColor(x, y);
              sum += qAbs(actual.red() - expected.red()) + qAbs(actual.green() - expected.green()) +
                     qAbs(actual.blue() - expected.blue());
            }
          }
          return sum / (static_cast<double>(reference.width()) * reference.height() * 3.0);
        };
        const auto matches_published_source_frame = [&](const QImage& picture, qint64 timestamp_us) {
          if (timestamp_us < 0) return false;
          const auto seconds = static_cast<double>(timestamp_us) / 1'000'000.0;
          const auto expected = reference_picture(source_path, seconds);
          if (expected.isNull()) return false;
          const auto expected_distance = picture_distance(picture, expected);
          if (expected_distance > 18.0) return false;
          // The fixture is 8 fps. Compare the published frame with its nearest
          // adjacent source frame as well, when that frame is distinct enough
          // to make an exact-frame assertion meaningful.
          constexpr double source_frame_step = 0.125;
          const auto adjacent_seconds = seconds >= source_frame_step
              ? seconds - source_frame_step
              : seconds + source_frame_step;
          if (adjacent_seconds <= 1.875) {
            const auto adjacent = reference_picture(source_path, adjacent_seconds);
            if (!adjacent.isNull()) {
              const auto adjacent_distance = picture_distance(picture, adjacent);
              if (picture_distance(expected, adjacent) > 2.0 &&
                  expected_distance >= adjacent_distance * 0.8) return false;
            }
          }
          return true;
        };
        timeline_playback.seekSequenceSeconds(last_title_time);
        const auto later_scene_picture_ready = wait_for_condition([&] {
              const auto picture = program_provider->requestImage(QString(), nullptr, QSize());
              const auto cached = timeline_document.scenePreviewFrame();
              const auto cached_path = cached.value(QStringLiteral("path")).toString();
              const auto cached_seconds = cached.value(QStringLiteral("sourceSeconds"), -1.0).toDouble();
              const auto published_timestamp_us = timeline_playback.processedFrameTimestampUs();
              const auto last_distance = picture_distance(picture, reference_last);
              return std::abs(timeline_document.sequencePlayheadSeconds() - last_title_time) < 0.08 &&
                  cached.value(QStringLiteral("active")).toBool() &&
                  !cached_path.isEmpty() && QFileInfo::exists(cached_path) &&
                  cached.value(QStringLiteral("sourceClipId")).toString() == source_id &&
                  std::abs(cached_seconds - last_title_time) < 0.0001 &&
                  qAbs(published_timestamp_us - qRound64(cached_seconds * 1'000'000)) <= 1 &&
                  picture != first_scene_picture &&
                  last_distance < picture_distance(picture, reference_first) * 0.8 &&
                  last_distance < picture_distance(picture, reference_middle) * 0.8;
            }, 10000);
        if (!later_scene_picture_ready) {
          const auto picture = program_provider->requestImage(QString(), nullptr, QSize());
          studio_window->grabWindow().save(QStringLiteral("/private/tmp/looksy-scene-later-frame-debug.png"));
          std::cerr << "Scene seek: source=" << workflow_player->source().toString().toStdString()
                    << " position=" << workflow_player->position()
                    << " displayedTimestampUs=" << timeline_playback.processedFrameTimestampUs()
                    << " pixelDistanceFirst=" << picture_distance(picture, reference_first)
                    << " pixelDistanceMiddle=" << picture_distance(picture, reference_middle)
                    << " pixelDistanceLast=" << picture_distance(picture, reference_last)
                    << " mediaReady=" << timeline_playback.mediaReady() << '\n';
          report_playback();
        }
        if (!later_scene_picture_ready ||
            !capture_workflow(QStringLiteral("12e-scene-camera-attached-last-frame"),
                              {QStringLiteral("timelineProgramMonitor")}))
          return fail("Precise paused seek should publish the cached source frame at 1.875 seconds and keep the attached title visible");

        const auto paused_cached_frame = timeline_document.scenePreviewFrame();
        const auto paused_published_timestamp_us = timeline_playback.processedFrameTimestampUs();
        const auto paused_cached_image = program_provider->requestImage(QString(), nullptr, QSize());
        const auto paused_cached_seconds = paused_cached_frame.value(QStringLiteral("sourceSeconds")).toDouble();
        if (paused_cached_frame.value(QStringLiteral("path")).toString().isEmpty() ||
            !QFileInfo::exists(paused_cached_frame.value(QStringLiteral("path")).toString()) ||
            qAbs(paused_published_timestamp_us - qRound64(paused_cached_seconds * 1'000'000)) > 1 ||
            !matches_published_source_frame(paused_cached_image, paused_published_timestamp_us))
          return fail("Paused scene preview pixels should match an independent FFmpeg decode at the cached source timestamp");

        timeline_playback.seekSequenceSeconds(0.0);
        if (!wait_for_condition([&] {
              const auto cached = timeline_document.scenePreviewFrame();
              const auto cached_seconds = cached.value(QStringLiteral("sourceSeconds"), -1.0).toDouble();
              const auto published_timestamp_us = timeline_playback.processedFrameTimestampUs();
              return cached.value(QStringLiteral("active")).toBool() &&
                  cached.value(QStringLiteral("sourceClipId")).toString() == source_id &&
                  QFileInfo::exists(cached.value(QStringLiteral("path")).toString()) &&
                  std::abs(cached_seconds) < 0.0001 && published_timestamp_us >= 0 &&
                  published_timestamp_us <= 60'000 &&
                  qAbs(published_timestamp_us - qRound64(cached_seconds * 1'000'000)) <= 1;
            }, 10000))
          return fail("Forward playback should start from the exact first cached source frame");
        const auto playback_start_timestamp_us = timeline_playback.processedFrameTimestampUs();
        timeline_playback.play();
        const auto playback_scene_frame_ready = wait_for_condition([&] {
              const auto cached = timeline_document.scenePreviewFrame();
              const auto cached_path = cached.value(QStringLiteral("path")).toString();
              const auto cached_seconds = cached.value(QStringLiteral("sourceSeconds"), -1.0).toDouble();
              const auto published_timestamp_us = timeline_playback.processedFrameTimestampUs();
              return timeline_document.sequencePlaying() &&
                  workflow_player->playbackState() == QMediaPlayer::PlayingState &&
                  cached.value(QStringLiteral("active")).toBool() &&
                  !cached_path.isEmpty() && QFileInfo::exists(cached_path) &&
                  cached.value(QStringLiteral("sourceClipId")).toString() == source_id &&
                  published_timestamp_us >= 125'000 &&
                  published_timestamp_us > playback_start_timestamp_us &&
                  qAbs(published_timestamp_us - qRound64(cached_seconds * 1'000'000)) <= 1;
            }, 10000);
        const auto playback_published_timestamp_us = timeline_playback.processedFrameTimestampUs();
        const auto playback_cached_frame = timeline_document.scenePreviewFrame();
        const auto playback_cached_image = program_provider->requestImage(QString(), nullptr, QSize());
        const auto playback_snapshot_was_playing = timeline_document.sequencePlaying() &&
            workflow_player->playbackState() == QMediaPlayer::PlayingState;
        timeline_playback.pause();
        const auto playback_picture_matches =
            matches_published_source_frame(playback_cached_image, playback_published_timestamp_us);
        if (!playback_scene_frame_ready ||
            !playback_snapshot_was_playing ||
            playback_published_timestamp_us < 125'000 ||
            playback_published_timestamp_us <= playback_start_timestamp_us ||
            playback_cached_frame.value(QStringLiteral("path")).toString().isEmpty() ||
            !QFileInfo::exists(playback_cached_frame.value(QStringLiteral("path")).toString()) ||
            qAbs(playback_published_timestamp_us - qRound64(
                playback_cached_frame.value(QStringLiteral("sourceSeconds")).toDouble() * 1'000'000)) > 1 ||
            !playback_picture_matches) {
          const auto published_seconds = static_cast<double>(playback_published_timestamp_us) / 1'000'000.0;
          const auto expected_playback_picture = reference_picture(source_path, published_seconds);
          const auto playback_pixel_distance = expected_playback_picture.isNull()
              ? -1.0 : picture_distance(playback_cached_image, expected_playback_picture);
          const auto playback_adjacent_picture = published_seconds >= 0.125
              ? reference_picture(source_path, published_seconds - 0.125)
              : reference_picture(source_path, published_seconds + 0.125);
          const auto adjacent_pixel_distance = playback_adjacent_picture.isNull()
              ? -1.0 : picture_distance(playback_cached_image, playback_adjacent_picture);
          const auto selected_cache_path = playback_cached_frame.value(QStringLiteral("path")).toString();
          const auto playback_image_path = QStringLiteral("/private/tmp/looksy-tracking-next-native-scene-playback-failure.png");
          playback_cached_image.save(playback_image_path);
          std::cerr << "Scene playback frame mismatch: ready=" << playback_scene_frame_ready
                    << " snapshotWasPlaying=" << playback_snapshot_was_playing
                    << " publishedTimestampUs=" << playback_published_timestamp_us
                    << " publishedSourceSeconds=" << published_seconds
                    << " cachedSourceSeconds=" << playback_cached_frame.value(QStringLiteral("sourceSeconds"), -1.0).toDouble()
                    << " cachedClip=" << playback_cached_frame.value(QStringLiteral("sourceClipId")).toString().toStdString()
                    << " cachedPath=" << selected_cache_path.toStdString()
                    << " cacheExists=" << QFileInfo::exists(selected_cache_path)
                    << " cacheTimestampDeltaUs=" << playback_published_timestamp_us -
                        qRound64(playback_cached_frame.value(QStringLiteral("sourceSeconds")).toDouble() * 1'000'000)
                    << " pictureMatchesSource=" << playback_picture_matches
                    << " sourcePixelDistance=" << playback_pixel_distance
                    << " adjacentPixelDistance=" << adjacent_pixel_distance
                    << " pictureSize=" << playback_cached_image.width() << 'x' << playback_cached_image.height()
                    << " sourceImage=" << playback_image_path.toStdString() << '\n';
          report_playback();
          return fail("Forward playback scene frames should match independent FFmpeg decodes at their published source timestamps");
        }

        // Trim the video source by one quarter second, then seek to its sequence
        // start. The cached preview must use the source origin after the trim.
        timeline_document.selectClip(source_clip_index);
        const auto source_clip_before_trim = clip_by_id(source_id);
        const auto source_trim_before = source_clip_before_trim.value(QStringLiteral("trimStartSeconds")).toDouble();
        const auto source_trim_after = source_trim_before + 0.25;
        timeline_document.setSelectedTrimStartSeconds(source_trim_after);
        const auto source_sequence_start = source_clip_before_trim.value(QStringLiteral("startSeconds")).toDouble();
        timeline_playback.seekSequenceSeconds(source_sequence_start);
        const auto trimmed_origin_ready = wait_for_condition([&] {
              const auto cached = timeline_document.scenePreviewFrame();
              const auto cached_path = cached.value(QStringLiteral("path")).toString();
              const auto cached_seconds = cached.value(QStringLiteral("sourceSeconds"), -1.0).toDouble();
              const auto published_timestamp_us = timeline_playback.processedFrameTimestampUs();
              return cached.value(QStringLiteral("active")).toBool() &&
                  !cached_path.isEmpty() && QFileInfo::exists(cached_path) &&
                  cached.value(QStringLiteral("sourceClipId")).toString() == source_id &&
                  std::abs(cached_seconds - source_trim_after) < 0.0001 &&
                  qAbs(published_timestamp_us - qRound64(cached_seconds * 1'000'000)) <= 1;
            }, 10000);
        const auto trimmed_cached_frame = timeline_document.scenePreviewFrame();
        const auto trimmed_published_timestamp_us = timeline_playback.processedFrameTimestampUs();
        const auto trimmed_cached_image = program_provider->requestImage(QString(), nullptr, QSize());
        timeline_document.setSelectedTrimStartSeconds(source_trim_before);
        if (!trimmed_origin_ready ||
            trimmed_cached_frame.value(QStringLiteral("path")).toString().isEmpty() ||
            !QFileInfo::exists(trimmed_cached_frame.value(QStringLiteral("path")).toString()) ||
            qAbs(trimmed_published_timestamp_us - qRound64(
                trimmed_cached_frame.value(QStringLiteral("sourceSeconds")).toDouble() * 1'000'000)) > 1 ||
            !matches_published_source_frame(trimmed_cached_image, trimmed_published_timestamp_us))
          return fail("A trimmed video source should publish pixels from its updated source-time origin");
        scene_camera_proof = true;
        scene_camera_timing_estimated = timeline_playback.processedFrameTimestampEstimated();
        return EXIT_SUCCESS;
      };
      const auto run_roto_correction_workflow = [&]() -> int {
        const auto fixture_path = qEnvironmentVariable("PHOTO_EDITOR_ROTO_TIMELINE_FIXTURE");
        if (fixture_path.isEmpty())
          return fail("PHOTO_EDITOR_SMOKE_ROTO_ONLY requires PHOTO_EDITOR_ROTO_TIMELINE_FIXTURE");
        const auto fixture_file = QFileInfo(fixture_path).absoluteFilePath();
        if (!QFileInfo::exists(fixture_file))
          return fail("PHOTO_EDITOR_ROTO_TIMELINE_FIXTURE should name a saved SAM2 matte timeline");
        const auto active_fixture = QFileInfo(timeline_document.savedDocumentPath()).absoluteFilePath();
        if (active_fixture != fixture_file &&
            (!timeline_document.loadTimeline(fixture_file) ||
             !wait_for_condition([&] { return timeline_document.hasActiveTimeline(); }, 10000)))
          return fail("Rotoscope correction proof should load its saved editable timeline");

        studio_root->setProperty("currentWorkspace", studio_root->property("timelineWorkspaceIndex"));
        if (!wait_for_condition([&] {
              auto* loader = find_named_object(studio_root, QStringLiteral("timelineWorkspaceLoader"));
              auto* workspace = loader ? loader->property("item").value<QObject*>() : nullptr;
              if (!loader || loader->property("status").toInt() != 1 || !workspace ||
                  !workspace->property("workspacePanelSettingsInitialized").toBool()) return false;
              workflow_timeline = workspace;
              auto* editor_loader = find_named_object(workspace, QStringLiteral("timelineEditorSurfaceLoader"));
              return editor_loader && editor_loader->property("status").toInt() == 1 &&
                  item_visible(find_named_item(workspace, QStringLiteral("timelineViewport")));
            }, 25000))
          return fail("Rotoscope correction proof should show its Timeline editor in the native shell");
        if (!wait_for_condition([&] {
              const auto state = photo_editor::desktop::TimelinePlaybackFrameTestAccess::readiness(timeline_playback);
              return timeline_playback.mediaReady() && !state.value(QStringLiteral("waitingForPosition")).toBool();
            }, 10000))
          return fail("Rotoscope fixture should reach native media readiness");

        QVariantMap source_clip;
        int source_clip_index = -1;
        int flattened_index = 0;
        for (const auto& track_value : timeline_document.timelineTracks()) {
          for (const auto& clip_value : track_value.toMap().value(QStringLiteral("clips")).toList()) {
            const auto clip = clip_value.toMap();
            if (clip.value(QStringLiteral("rotoCorrectionReady")).toBool() &&
                clip.value(QStringLiteral("mediaType")).toString() == QStringLiteral("video")) {
              source_clip = clip;
              source_clip_index = flattened_index;
              break;
            }
            ++flattened_index;
          }
          if (!source_clip.isEmpty()) break;
        }
        if (source_clip.isEmpty() || source_clip_index < 0)
          return fail("Rotoscope fixture should include a SAM2-ready source clip with cached correction frames");
        const auto source_id = source_clip.value(QStringLiteral("clipId")).toString();
        if (source_id.isEmpty() || source_clip.value(QStringLiteral("foregroundCutoutEnabled")).toBool())
          return fail("Rotoscope correction fixture should keep the source cutout disabled");
        timeline_document.selectClip(source_clip_index);
        timeline_playback.pause();
        const auto foreground = source_clip.value(QStringLiteral("foregroundCutout")).toMap();
        const auto seed = foreground.value(QStringLiteral("seed")).toMap();
        const auto trim_start = source_clip.value(QStringLiteral("trimStartSeconds")).toDouble();
        const auto duration = source_clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble();
        const auto fps = foreground.value(QStringLiteral("fps")).toDouble();
        const auto frame_count = foreground.value(QStringLiteral("frameCount")).toInt();
        if (duration <= 0.1 || fps <= 0.0 || frame_count < 1 || seed.value(QStringLiteral("width")).toDouble() <= 0.0 ||
            seed.value(QStringLiteral("height")).toDouble() <= 0.0)
          return fail("Rotoscope fixture should expose its valid seed region and nontrivial source duration");
        const auto original_correction_prompts = seed.value(QStringLiteral("correctionPrompts")).toList();
        const auto preferred_frame_index = qMin(frame_count - 1,
            qMax(0, qRound(qMin(0.5, duration * 0.5) * fps)));
        auto frame_index = -1;
        auto best_frame_distance = frame_count + 1;
        for (int index = 0; index < frame_count; ++index) {
          bool already_prompted = false;
          for (const auto& prompt_value : original_correction_prompts) {
            if (prompt_value.toMap().value(QStringLiteral("frameIndex"), -1).toInt() == index) {
              already_prompted = true;
              break;
            }
          }
          if (already_prompted || (frame_count > 2 && (index == 0 || index == frame_count - 1))) continue;
          const auto distance = qAbs(index - preferred_frame_index);
          if (distance < best_frame_distance) {
            frame_index = index;
            best_frame_distance = distance;
          }
        }
        if (frame_index < 0)
          return fail("Rotoscope fixture should have an uncorrected interior frame for native refinement");
        const auto local_time = trim_start + static_cast<double>(frame_index) / fps;
        const auto sequence_time = source_clip.value(QStringLiteral("startSeconds")).toDouble() + local_time - trim_start;
        timeline_playback.seekSequenceSeconds(sequence_time);
        QVariantMap correction_frame;
        if (!wait_for_condition([&] {
              correction_frame = timeline_document.rotoscopeCorrectionFrame();
              const auto frame_path = QUrl(correction_frame.value(QStringLiteral("path")).toString()).toLocalFile();
              return correction_frame.value(QStringLiteral("success")).toBool() &&
                  correction_frame.value(QStringLiteral("frameIndex")).toInt() >= 0 &&
                  std::abs(correction_frame.value(QStringLiteral("sourceSeconds")).toDouble() - local_time) < 0.0001 &&
                  !frame_path.isEmpty() && QFileInfo(frame_path).isReadable() &&
                  !timeline_document.sequencePlaying() &&
                  qAbs(timeline_playback.processedFrameTimestampUs() -
                       qRound64(local_time * 1'000'000)) <= 1 &&
                  std::abs(timeline_document.currentClipPlayheadSeconds() - local_time) < 0.0001;
            }, 10000))
          return fail(qPrintable(QStringLiteral("Rotoscope proof should expose an exact cached paused source frame: %1")
                                   .arg(correction_frame.value(QStringLiteral("message")).toString())));
        const auto frame_path = QUrl(correction_frame.value(QStringLiteral("path")).toString()).toLocalFile();
        const QImage correction_image(frame_path);
        if (correction_image.isNull())
          return fail("Rotoscope correction frame should decode from its cached source path");
        const auto correction_frame_index = correction_frame.value(QStringLiteral("frameIndex")).toInt();
        if (correction_frame_index != frame_index)
          return fail("Rotoscope paused source playhead should map to the selected SAM2 frame index");
        roto_correction_source_seconds = correction_frame.value(QStringLiteral("sourceSeconds")).toDouble();
        roto_correction_frame_index = correction_frame_index;
        const auto original_frame_points = correction_frame.value(QStringLiteral("points")).toList();
        if (!original_frame_points.isEmpty())
          return fail("Rotoscope native correction should choose an uncorrected frame without discarding existing prompts");
        const auto open_roto_tracking = [&] {
          QMetaObject::invokeMethod(workflow_timeline, "openAutoReframe", Q_ARG(QVariant, QVariant{}));
          return wait_for_condition([&] { return live_item(QStringLiteral("timelineOpenTrackingToolsButton")); }, 5000) &&
              click_named(QStringLiteral("timelineOpenTrackingToolsButton"));
        };
        if (!open_roto_tracking() ||
            !wait_for_condition([&] {
              auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup"));
              auto* refine = live_item(QStringLiteral("timelineCorrectRotoscopeButton"));
              return popup && popup->property("visible").toBool() && refine &&
                  refine->property("visible").toBool() && refine->property("enabled").toBool();
            }, 5000) ||
            !capture_workflow(QStringLiteral("roto-correction-panel"),
                              {QStringLiteral("timelineCorrectRotoscopeButton")}) ||
            !click_named(QStringLiteral("timelineCorrectRotoscopeButton")) ||
            !wait_for_condition([&] {
              auto* layer = live_item(QStringLiteral("timelineRotoscopeCorrectionLayer"));
              auto* image = live_item(QStringLiteral("timelineRotoscopeCorrectionSourceFrame"));
              return layer && layer->property("visible").toBool() && image &&
                  image->property("status").toInt() == 1 &&
                  workflow_timeline->property("rotoCorrectionOverlayVisible").toBool();
            }, 10000))
          return fail("Refine Matte should open the actual cached paused source frame and point HUD");
        auto* correction_layer = live_item(QStringLiteral("timelineRotoscopeCorrectionLayer"));
        auto* correction_points = live_item(QStringLiteral("timelineRotoscopeCorrectionPoints"));
        auto* correction_image_item = live_item(QStringLiteral("timelineRotoscopeCorrectionSourceFrame"));
        const auto scene_point_for_source = [&](double x, double y) {
          const auto bounds = correction_layer->property("sourceBounds").toRectF();
          return correction_layer->mapToScene(QPointF(bounds.x() + x * bounds.width(),
                                                       bounds.y() + y * bounds.height()));
        };
        const auto click_source_point = [&](double x, double y, Qt::KeyboardModifiers modifiers) {
          const auto point = scene_point_for_source(x, y);
          if (modifiers == Qt::NoModifier) {
            workflow_pointer_timestamp += static_cast<ulong>(QApplication::doubleClickInterval() + 1);
            send_pointer(QEvent::MouseButtonPress, point);
            send_pointer(QEvent::MouseButtonRelease, point);
            return;
          }
          const auto global = QPointF(studio_window->mapToGlobal(point.toPoint()));
          workflow_pointer_timestamp += static_cast<ulong>(QApplication::doubleClickInterval() + 1);
          QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, modifiers);
          press.setTimestamp(workflow_pointer_timestamp);
          QCoreApplication::sendEvent(studio_window, &press);
          workflow_pointer_timestamp += 16;
          QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, modifiers);
          release.setTimestamp(workflow_pointer_timestamp);
          QCoreApplication::sendEvent(studio_window, &release);
          process_layout_events(2);
        };
        const auto positive_x = seed.value(QStringLiteral("x")).toDouble() + seed.value(QStringLiteral("width")).toDouble() * 0.5;
        const auto positive_y = seed.value(QStringLiteral("y")).toDouble() + seed.value(QStringLiteral("height")).toDouble() * 0.5;
        const auto negative_x = seed.value(QStringLiteral("x")).toDouble() >= 0.04
            ? qMax(0.01, seed.value(QStringLiteral("x")).toDouble() * 0.5)
            : qMin(0.99, seed.value(QStringLiteral("x")).toDouble() + seed.value(QStringLiteral("width")).toDouble() + 0.04);
        const auto negative_y = positive_y;
        if (!correction_layer || !correction_points || !correction_image_item || correction_image.isNull())
          return fail("Rotoscope source frame should remain visible under its editable point markers");
        if (correction_image_item->property("source").toUrl().toString() !=
                correction_frame.value(QStringLiteral("path")).toString() ||
            std::abs(workflow_timeline->property("rotoCorrectionSourceSeconds").toDouble() - local_time) >= 0.0001 ||
            workflow_timeline->property("rotoCorrectionFrameIndex").toInt() != frame_index)
          return fail("Rotoscope overlay should use the exact cached paused frame returned by the controller");

        auto* correction_hud = live_item(QStringLiteral("timelineRotoscopeCorrectionHud"));
        auto* hud_drag_area = live_item(QStringLiteral("timelineRotoscopeCorrectionHudDragArea"));
        if (!correction_hud || !hud_drag_area)
          return fail("Rotoscope controls should expose a drag handle so every source pixel remains editable");
        const auto original_hud_y = correction_hud->y();
        const auto drag_start = scene_rect(hud_drag_area).center();
        const auto drag_delta = qMax(0.0, correction_layer->height() - correction_hud->height() - 8.0 - original_hud_y);
        workflow_pointer_timestamp += static_cast<ulong>(QApplication::doubleClickInterval() + 1);
        send_pointer(QEvent::MouseButtonPress, drag_start);
        for (int step = 1; step <= 6; ++step)
          send_pointer(QEvent::MouseMove, drag_start + QPointF(0, drag_delta * step / 6.0));
        send_pointer(QEvent::MouseButtonRelease, drag_start + QPointF(0, drag_delta));
        if (!wait_for_condition([&] { return correction_hud->y() > original_hud_y + 10.0 &&
              workflow_timeline->property("rotoCorrectionDraftPoints").toList().isEmpty(); }, 2000))
          return fail("Dragging the correction toolbar must move controls without adding matte points");

        click_source_point(positive_x, positive_y, Qt::NoModifier);
        if (!wait_for_condition([&] {
              const auto points = workflow_timeline->property("rotoCorrectionDraftPoints").toList();
              return points.size() == 1 && points.first().toMap().value(QStringLiteral("label")).toInt() == 1;
            }, 2000))
          return fail("Default pointer click should add a positive subject point");
        click_source_point(negative_x, negative_y, Qt::AltModifier);
        if (!wait_for_condition([&] {
              const auto points = workflow_timeline->property("rotoCorrectionDraftPoints").toList();
              return points.size() == 2 && points.at(0).toMap().value(QStringLiteral("label")).toInt() == 1 &&
                  points.at(1).toMap().value(QStringLiteral("label")).toInt() == 0;
            }, 2000) ||
            !capture_workflow(QStringLiteral("roto-correction-positive-negative-points"),
                              {QStringLiteral("timelineRotoscopeCorrectionHud"),
                               QStringLiteral("timelineRotoscopeCorrectionPoints")}))
          return fail("Alt-click should add a negative background point alongside the positive subject point");
        if (!click_named(QStringLiteral("timelineRotoscopeCorrectionUndoButton")) ||
            !wait_for_condition([&] {
              const auto points = workflow_timeline->property("rotoCorrectionDraftPoints").toList();
              return points.size() == 1 && points.first().toMap().value(QStringLiteral("label")).toInt() == 1;
            }, 2000))
          return fail("Undo point should remove only the last negative correction prompt");
        click_source_point(negative_x, negative_y, Qt::AltModifier);
        if (!wait_for_condition([&] {
              return workflow_timeline->property("rotoCorrectionDraftPoints").toList().size() == 2;
            }, 2000) || !click_named(QStringLiteral("timelineRotoscopeCorrectionCancelButton")) ||
            !wait_for_condition([&] {
              return !workflow_timeline->property("rotoCorrectionOverlayVisible").toBool() &&
                  !timeline_document.trackingBusy();
            }, 3000))
          return fail("Cancel should close the correction overlay without starting SAM2 analysis");
        auto cancelled_clip = clip_by_id(source_id);
        auto cancelled_seed = cancelled_clip.value(QStringLiteral("foregroundCutout")).toMap()
            .value(QStringLiteral("seed")).toMap();
        if (cancelled_seed.value(QStringLiteral("correctionPrompts")).toList() != original_correction_prompts)
          return fail("Cancel should discard draft points without changing persisted SAM2 correction prompts");

        if (!wait_for_condition([&] {
              auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup"));
              auto* refine = live_item(QStringLiteral("timelineCorrectRotoscopeButton"));
              return popup && popup->property("visible").toBool() && refine && refine->property("enabled").toBool();
            }, 5000) || !click_named(QStringLiteral("timelineCorrectRotoscopeButton")) ||
            !wait_for_condition([&] {
              auto* layer = live_item(QStringLiteral("timelineRotoscopeCorrectionLayer"));
              auto* image = live_item(QStringLiteral("timelineRotoscopeCorrectionSourceFrame"));
              return layer && layer->property("visible").toBool() && image && image->property("status").toInt() == 1 &&
                  workflow_timeline->property("rotoCorrectionDraftPoints").toList() == original_frame_points;
            }, 10000))
          return fail("Reopening correction should use the cached source frame and discard cancelled prompt drafts");
        click_source_point(positive_x, positive_y, Qt::NoModifier);
        click_source_point(negative_x, negative_y, Qt::AltModifier);
        if (!wait_for_condition([&] {
              const auto points = workflow_timeline->property("rotoCorrectionDraftPoints").toList();
              return points.size() == 2 && points.at(0).toMap().value(QStringLiteral("label")).toInt() == 1 &&
                  points.at(1).toMap().value(QStringLiteral("label")).toInt() == 0;
            }, 2000))
          return fail("Done should submit the positive and negative prompts from the reopened correction overlay");
        const auto submitted_points = workflow_timeline->property("rotoCorrectionDraftPoints").toList();
        if (qAbs(submitted_points.at(0).toMap().value(QStringLiteral("x")).toDouble() - positive_x) > 0.01 ||
            qAbs(submitted_points.at(0).toMap().value(QStringLiteral("y")).toDouble() - positive_y) > 0.01 ||
            qAbs(submitted_points.at(1).toMap().value(QStringLiteral("x")).toDouble() - negative_x) > 0.01 ||
            qAbs(submitted_points.at(1).toMap().value(QStringLiteral("y")).toDouble() - negative_y) > 0.01 ||
            !click_named(QStringLiteral("timelineRotoscopeCorrectionDoneButton")))
          return fail("Done should submit the positive and negative prompts from the reopened correction overlay");
        bool saw_analysis_busy = false;
        const auto correction_complete = wait_for_condition([&] {
          saw_analysis_busy = saw_analysis_busy || timeline_document.trackingBusy();
          const auto clip = clip_by_id(source_id);
          const auto matte = clip.value(QStringLiteral("foregroundCutout")).toMap();
          const auto updated_seed = matte.value(QStringLiteral("seed")).toMap();
          bool saved_frame_prompt = false;
          const auto updated_prompts = updated_seed.value(QStringLiteral("correctionPrompts")).toList();
          bool original_prompts_preserved = true;
          for (const auto& original_prompt_value : original_correction_prompts) {
            const auto original_prompt = original_prompt_value.toMap();
            bool found_original = false;
            for (const auto& prompt_value : updated_prompts) {
              if (prompt_value.toMap() == original_prompt) {
                found_original = true;
                break;
              }
            }
            original_prompts_preserved = original_prompts_preserved && found_original;
          }
          for (const auto& prompt_value : updated_prompts) {
            const auto prompt = prompt_value.toMap();
            if (prompt.value(QStringLiteral("frameIndex")).toInt() != frame_index) continue;
            const auto points = prompt.value(QStringLiteral("points")).toList();
            saved_frame_prompt = points.size() == 2 &&
                points.at(0).toMap().value(QStringLiteral("label")).toInt() == 1 &&
                points.at(1).toMap().value(QStringLiteral("label")).toInt() == 0 &&
                qAbs(points.at(0).toMap().value(QStringLiteral("x")).toDouble() - positive_x) < 0.01 &&
                qAbs(points.at(0).toMap().value(QStringLiteral("y")).toDouble() - positive_y) < 0.01 &&
                qAbs(points.at(1).toMap().value(QStringLiteral("x")).toDouble() - negative_x) < 0.01 &&
                qAbs(points.at(1).toMap().value(QStringLiteral("y")).toDouble() - negative_y) < 0.01;
          }
          return saw_analysis_busy && !timeline_document.trackingBusy() &&
              clip.value(QStringLiteral("foregroundCutoutReady")).toBool() &&
              clip.value(QStringLiteral("rotoCorrectionReady")).toBool() && saved_frame_prompt && original_prompts_preserved &&
              !clip.value(QStringLiteral("foregroundCutoutEnabled")).toBool();
        }, 60000);
        if (!correction_complete)
          return fail(qPrintable(QStringLiteral("SAM2 refinement should re-run and publish a ready matte with the saved frame prompts: %1 (busy observed %2)")
                                   .arg(timeline_document.trackingStatus()).arg(saw_analysis_busy)));
        if (!capture_workflow(QStringLiteral("roto-correction-rerun-ready"),
                              {QStringLiteral("timelineProgramMonitor")}))
          return fail("Completed SAM2 correction should capture its ready source clip in the native monitor");
        if (auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup")))
          QMetaObject::invokeMethod(popup, "close");
        return EXIT_SUCCESS;
      };

      if (tracking_only_mode) {
        const auto scene_status = roto_only_mode ? run_roto_correction_workflow()
            : surface_only_mode ? surface_workflow_proof() : run_scene_camera_workflow();
        if (scene_status != EXIT_SUCCESS) return scene_status;
        QFile scene_manifest_file(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
        if (!scene_manifest_file.open(QIODevice::WriteOnly)) return fail("Tracking-only proof should write its captures");
        scene_manifest_file.write(QJsonDocument(studio_manifest).toJson(QJsonDocument::Indented));
        const auto receipt_name = roto_only_mode ? QStringLiteral("roto-workflow.json")
            : surface_only_mode ? QStringLiteral("surface-workflow.json") : QStringLiteral("scene-workflow.json");
        QFile scene_receipt_file(QDir(proof_dir).filePath(receipt_name));
        const QJsonObject receipt = roto_only_mode
            ? QJsonObject{{QStringLiteral("mode"), QStringLiteral("sam2-roto-refinement")},
                {QStringLiteral("rotoCorrectionNativeWorkflow"), true},
                {QStringLiteral("rotoPositiveNegativePoints"), true},
                {QStringLiteral("rotoUndoLastAndCancel"), true},
                {QStringLiteral("rotoRerunAndReadyMatte"), true},
                {QStringLiteral("rotoCorrectionSourceSeconds"), roto_correction_source_seconds},
                {QStringLiteral("rotoCorrectionFrameIndex"), roto_correction_frame_index},
                {QStringLiteral("sourceCutoutEnabled"), false},
                {QStringLiteral("baselineSkipped"), true}, {QStringLiteral("overallStatus"), QStringLiteral("passed")}}
            : surface_only_mode
            ? QJsonObject{{QStringLiteral("mode"), QStringLiteral("surface2d")},
                {QStringLiteral("surfaceNativeWorkflow"), scene_camera_proof},
                {QStringLiteral("surfaceOcclusionRequested"), surface_occlusion_mode},
                {QStringLiteral("surfaceAttachmentOcclusion"), surface_attachment_occlusion_proof},
                {QStringLiteral("surfacePreviewExactSeekPixels"), scene_camera_proof},
                {QStringLiteral("surfacePreviewPlaybackPixels"), scene_camera_proof},
                {QStringLiteral("surfaceForwardPlaybackSampleSeconds"), surface_playback_sample_seconds},
                {QStringLiteral("surfacePreviewTrimmedSourceOrigin"), scene_camera_proof},
                {QStringLiteral("surfaceRetrackWithExclusion"), surface_retrack_proof},
                {QStringLiteral("surfaceCornerCorrectionRetracked"), surface_correction_proof},
                {QStringLiteral("surfaceTimingEstimated"), scene_camera_timing_estimated},
                {QStringLiteral("baselineSkipped"), true}, {QStringLiteral("overallStatus"), QStringLiteral("passed")}}
            : QJsonObject{{QStringLiteral("sceneCameraNativeWorkflow"), scene_camera_proof},
                {QStringLiteral("scenePointSelectionSourceSeconds"), scene_point_selection_source_seconds},
                {QStringLiteral("scenePreviewExactSeekPixels"), scene_camera_proof},
                {QStringLiteral("scenePreviewPlaybackPixels"), scene_camera_proof},
                {QStringLiteral("sceneAudioTransportPlaying"), scene_camera_proof},
                {QStringLiteral("scenePreviewTrimmedSourceOrigin"), scene_camera_proof},
                {QStringLiteral("sceneCameraTimingEstimated"), scene_camera_timing_estimated},
                {QStringLiteral("baselineSkipped"), true}, {QStringLiteral("overallStatus"), QStringLiteral("passed")}};
        if (!scene_receipt_file.open(QIODevice::WriteOnly) ||
            scene_receipt_file.write(QJsonDocument(receipt).toJson(QJsonDocument::Indented)) <= 0)
          return fail("Tracking-only proof should write its workflow receipt");
        return EXIT_SUCCESS;
      }

      const auto choose_edit_mode = [&](const QString& mode, const QString& capture_name) {
        // Native popup transactions need a genuinely active, exposed owner.
        // Another application may have taken focus between workflow stages.
        if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER")) {
          activate_native_test_window(studio_window);
          if (!wait_for_condition([&] {
                return studio_window->isActive() && studio_window->isExposed();
              }, 5000))
            return false;
        }
        if (!click_named(QStringLiteral("timelineActionsMenuButton"))) return false;
        auto* popup = find_named_object(studio_root, QStringLiteral("timelineActionsMenuButtonPopup"));
        if (!popup || !wait_for_condition([&] { return popup->property("opened").toBool(); }, 3000))
          return false;
        auto menu_value = popup->property("menuItems");
        if (menu_value.canConvert<QJSValue>()) menu_value = menu_value.value<QJSValue>().toVariant();
        const auto items = menu_value.toList();
        auto item_index = -1;
        for (int index = 0; index < items.size(); ++index) {
          if (items.at(index).toMap().value("key").toString() == "mode:mode-" + mode) {
            item_index = index;
            break;
          }
        }
        if (item_index < 0 || !QMetaObject::invokeMethod(popup, "focusMenuItem", Q_ARG(QVariant, item_index)))
          return false;
        const auto item_name = QStringLiteral("timelineActionsMenuButtonPopupItem%1").arg(item_index);
        if (!wait_for_condition([&] { return live_item(item_name); }, 3000) ||
            !capture_workflow(capture_name, {item_name}) || !click_named(item_name))
          return false;
        return wait_for_condition([&] {
          auto* control = live_item(QStringLiteral("timelineActionsMenuButton"));
          return timeline_document.editMode() == mode && control &&
              control->property("text").toString().contains(mode, Qt::CaseInsensitive) &&
              !popup->property("visible").toBool();
        }, 3000);
      };
      if (workflow_timeline->property("timelineAdvancedControlsVisible").toBool() ||
          !choose_edit_mode(QStringLiteral("overwrite"), QStringLiteral("00a-edit-mode-options")) ||
          !capture_workflow(QStringLiteral("00-visible-edit-mode"), {QStringLiteral("timelineActionsMenuButton")}))
        return fail("Normal Timeline Actions must select Overwrite and keep that mode visibly labeled");
      if (!choose_edit_mode(QStringLiteral("insert"), QStringLiteral("00b-return-to-insert")))
        return fail("Visible edit mode should return to Insert before moving clips");

      if (timeline_playback.processedFrameSource().isEmpty()) {
        QVariant poster_visible;
        QMetaObject::invokeMethod(workflow_timeline, "shouldShowTimelinePoster",
                                  Q_RETURN_ARG(QVariant, poster_visible));
        if (!poster_visible.toBool()) return fail("Program should retain its poster until a decoded frame is published");
      }
      if (!capture_workflow(QStringLiteral("01-ready-poster"),
            {QStringLiteral("timelineProgramMonitor"), QStringLiteral("timelinePlayPauseButton")})) {
        return fail("Program opening state should be visible");
      }
      if (!click_named(QStringLiteral("timelinePlayPauseButton")) ||
          !wait_for_condition([&] {
            return timeline_document.sequencePlaying() &&
                   timeline_playback.frameProcessingMetrics().value(QStringLiteral("sampleCount")).toInt() >= 1;
          }, 10000)) {
        report_playback();
        return fail("Visible Play should advance the timeline and publish decoded video frames");
      }
      const auto first_playing_picture = program_provider->requestImage(QString(), nullptr, QSize());
      if (!wait_for_condition([&] {
            return timeline_document.sequencePlaying() &&
                   timeline_document.sequencePlayheadSeconds() > 0.15 &&
                   timeline_playback.frameProcessingMetrics().value(QStringLiteral("sampleCount")).toInt() >= 3 &&
                   program_provider->requestImage(QString(), nullptr, QSize()) != first_playing_picture;
          }, 5000)) {
        report_playback();
        return fail("Forward playback must advance the decoded picture, not repeatedly publish the first frame");
      }
      if (!click_named(QStringLiteral("timelinePlayPauseButton")) || timeline_document.sequencePlaying()) {
        return fail("Visible Pause should stop sequence playback");
      }
      const auto decoded = program_provider->requestImage(QString(), nullptr, QSize());
      if (decoded.width() < 32 || decoded.height() < 32 ||
          decoded.pixelColor(decoded.width() / 2, decoded.height() / 2).alpha() < 200) {
        report_playback();
        std::cerr << "Paused Program resources: "
                  << QJsonDocument::fromVariant(timeline_playback.playbackResourceDiagnostics())
                         .toJson(QJsonDocument::Compact).toStdString() << '\n';
        return fail("Program proof requires real decoded pixels rather than an empty image-provider URL");
      }
      if (!capture_workflow(QStringLiteral("02-decoded-program"), {QStringLiteral("timelineProgramMonitor")})) {
        return fail("Decoded Program frame should render after Play and Pause");
      }
      const auto decoded_frame_metrics = timeline_playback.frameProcessingMetrics();
      const auto frame_before_seek = timeline_playback.processedFrameSource();
      const auto image_before_seek = program_provider->requestImage(QString(), nullptr, QSize());
      timeline_playback.seekSequenceSeconds(0.6);
      auto paused_seek_verified = wait_for_condition([&] {
            return !timeline_playback.processedFrameSource().isEmpty() &&
                   timeline_playback.processedFrameSource() != frame_before_seek &&
                   timeline_playback.frameProcessingMetrics().value(QStringLiteral("sampleCount")).toInt() > 0 &&
                   program_provider->requestImage(QString(), nullptr, QSize()) != image_before_seek &&
                   qAbs(workflow_player->position() - 600) <= 40;
          }, 5000);
      QJsonArray paused_seek_targets{0.6};
      if (paused_seek_verified) {
        for (const auto target : {1.2, 0.3}) {
          const auto previous_frame = timeline_playback.processedFrameSource();
          const auto previous_picture = program_provider->requestImage(QString(), nullptr, QSize());
          timeline_playback.seekSequenceSeconds(target);
          if (!wait_for_condition([&] {
                return !timeline_document.sequencePlaying() &&
                       !timeline_playback.processedFrameSource().isEmpty() &&
                       timeline_playback.processedFrameSource() != previous_frame &&
                       program_provider->requestImage(QString(), nullptr, QSize()) != previous_picture &&
                       qAbs(workflow_player->position() - qRound64(target * 1000)) <= 40;
              }, 5000)) {
            paused_seek_verified = false;
            break;
          }
          paused_seek_targets.append(target);
        }
      }
      if (!paused_seek_verified) {
        report_playback();
      }
      if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_TIMELINE_CACHE_PROBE_ONLY")) {
        if (!paused_seek_verified) return fail("Cache comparison requires real decoded paused seeks");
        QJsonArray cache_samples;
        QMap<double, QByteArray> hashes_by_target;
        int cached_frames = 0;
        for (const auto target : {0.6, 0.9, 0.6, 0.9, 0.6, 0.9}) {
          const auto previous_frame = timeline_playback.processedFrameSource();
          QElapsedTimer seek_timer;
          seek_timer.start();
          timeline_playback.seekSequenceSeconds(target);
          if (!wait_for_condition([&] {
                return timeline_playback.mediaReady() &&
                       timeline_playback.processedFrameSource() != previous_frame &&
                       qAbs(workflow_player->position() - qRound64(target * 1000)) <= 40;
              }, 5000)) return fail("Repeated cache probe seeks must publish real decoded frames");
          const auto image = program_provider->requestImage(QString(), nullptr, QSize());
          if (image.width() < 32 || image.height() < 32) return fail("Cache probe requires decoded pixels");
          const auto hash = QCryptographicHash::hash(
              QByteArray(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes()), QCryptographicHash::Sha256);
          if (hashes_by_target.contains(target) && hashes_by_target.value(target) != hash) return fail("Cache reuse must preserve the exact picture at a repeated seek");
          hashes_by_target.insert(target, hash);
          const auto metrics = timeline_playback.frameProcessingMetrics();
          if (metrics.value(QStringLiteral("frameCacheBytes")).toULongLong() > 128ULL * 1024ULL * 1024ULL) return fail("Native cache must remain inside its memory budget");
          if (metrics.value(QStringLiteral("frameCacheHit")).toBool()) ++cached_frames;
          cache_samples.append(QJsonObject{
              {QStringLiteral("targetSeconds"), target}, {QStringLiteral("seekToPublishMs"), seek_timer.elapsed()},
              {QStringLiteral("pixelSha256"), QString::fromLatin1(hash.toHex())},
              {QStringLiteral("width"), image.width()}, {QStringLiteral("height"), image.height()},
              {QStringLiteral("metrics"), QJsonObject::fromVariantMap(metrics)}});
        }
        const auto cache_enabled = !qEnvironmentVariableIsSet("PHOTO_EDITOR_TIMELINE_FRAME_CACHE_DISABLE");
        QFile cache_receipt(QDir(proof_dir).filePath(QStringLiteral("frame-cache.json")));
        if (!cache_receipt.open(QIODevice::WriteOnly) ||
            cache_receipt.write(QJsonDocument(QJsonObject{{QStringLiteral("cacheEnabled"), cache_enabled},
                {QStringLiteral("mediaReady"), timeline_playback.mediaReady()},
                {QStringLiteral("cachedFrames"), cached_frames}, {QStringLiteral("samples"), cache_samples}}).toJson(QJsonDocument::Indented)) <= 0) return fail("Native cache probe must preserve its evidence");
        if (cache_enabled && cached_frames < 2) return fail("Native replay must actually exercise cached decoded frames");
        std::cout << "Native repeated-seek cache probe: enabled=" << cache_enabled << " hits=" << cached_frames << " frames=6\n";
        return EXIT_SUCCESS;
      }
      timeline_document.selectClip(0);
      const auto opening_id = timeline_document.currentClip().value(QStringLiteral("clipId")).toString();
      const auto original_duration = timeline_document.currentClip()
                                         .value(QStringLiteral("trimmedDurationSeconds")).toDouble();
      const auto original_ripple_mode = workflow_timeline->property("rippleModeEnabled").toBool();
      workflow_timeline->setProperty("rippleModeEnabled", false);
      const auto precision_original = clip_by_id(opening_id);
      const auto original_start = precision_original.value(QStringLiteral("startSeconds")).toDouble();
      const auto frame_seconds = 1.0 / timeline_document.framesPerSecond();
      const auto choose_trim_action = [&](int index, const QString& capture_name) {
        const auto row_name = QStringLiteral("timelineTrimToolButtonPopupItem%1").arg(index);
        return click_named(QStringLiteral("timelineTrimToolButton")) &&
               wait_for_condition([&] {
                 auto* popup = find_named_object(studio_root, QStringLiteral("timelineTrimToolButtonPopup"));
                 return popup && popup->property("opened").toBool() && live_item(row_name);
               }, 3000) && capture_workflow(capture_name, {row_name}) && click_named(row_name) &&
               wait_for_condition([&] {
                 auto* popup = find_named_object(studio_root, QStringLiteral("timelineTrimToolButtonPopup"));
                 return popup && !popup->property("visible").toBool();
               }, 3000);
      };
      process_layout_events(8);
      if (!choose_trim_action(0, QStringLiteral("02a-choose-in-edge")) ||
          !choose_trim_action(3, QStringLiteral("02b-one-frame-trim")) ||
          std::abs(clip_by_id(opening_id).value(QStringLiteral("startSeconds")).toDouble() -
                   original_start - frame_seconds) > 0.0001 ||
          std::abs(clip_by_id(opening_id).value(QStringLiteral("trimmedDurationSeconds")).toDouble() -
                   original_duration + frame_seconds) > 0.0001) {
        return fail("Visible precision Trim should move the In edge later by exactly one sequence frame");
      }
      timeline_document.undo();
      process_layout_events(8);
      if (!choose_trim_action(1, QStringLiteral("02c-choose-out-edge")) ||
          !choose_trim_action(4, QStringLiteral("02d-five-frame-trim")) ||
          std::abs(clip_by_id(opening_id).value(QStringLiteral("trimmedDurationSeconds")).toDouble() -
                   original_duration + 5.0 * frame_seconds) > 0.0001) {
        return fail("Visible precision Trim should move the Out edge earlier by five sequence frames");
      }
      timeline_document.undo();
      timeline_playback.seekSequenceSeconds(1.2);
      process_layout_events(8);
      if (!choose_trim_action(7, QStringLiteral("02e-trim-out-to-playhead")) ||
          std::abs(clip_by_id(opening_id).value(QStringLiteral("startSeconds")).toDouble() +
                   clip_by_id(opening_id).value(QStringLiteral("trimmedDurationSeconds")).toDouble() - 1.2) > 0.0001) {
        return fail("Visible Trim Out to playhead should remove content after the playhead");
      }
      timeline_document.undo();
      process_layout_events(8);
      if (!choose_trim_action(9, QStringLiteral("02f-return-to-select")) ||
          timeline_document.activeTool() != QStringLiteral("select") ||
          !workflow_timeline->property("keyboardTrimEdge").toString().isEmpty() ||
          clip_by_id(opening_id) != precision_original) {
        return fail("Returning to Select should disarm precision trimming and Undo should restore the clip");
      }
      workflow_timeline->setProperty("rippleModeEnabled", original_ripple_mode);
      timeline_playback.seekSequenceSeconds(0.3);
      process_layout_events(8);
      auto* opening_item = find_named_item(workflow_timeline, QStringLiteral("timelineClip-Opening shot"));
      auto* end_handle = find_named_item(opening_item, QStringLiteral("timelineClipRightTrimHandle"));
      if (!drag_item(end_handle, -70.0) ||
          !wait_for_condition([&] {
            return clip_by_id(opening_id).value(QStringLiteral("trimmedDurationSeconds")).toDouble() < original_duration - 0.2;
          }, 3000)) {
        return fail("Dragging the visible right edge should shorten the selected clip");
      }
      const auto trimmed_duration = clip_by_id(opening_id).value(QStringLiteral("trimmedDurationSeconds")).toDouble();
      if (!capture_workflow(QStringLiteral("03-edge-trim"), {QStringLiteral("timelineClip-Opening shot")})) {
        return fail("Trimmed clip should remain visible");
      }
      timeline_document.undo();
      if (std::abs(clip_by_id(opening_id).value(QStringLiteral("trimmedDurationSeconds")).toDouble() - original_duration) > 0.04) {
        return fail("One Undo should restore the whole trim gesture");
      }
      timeline_document.redo();
      process_layout_events(8);
      opening_item = live_item(QStringLiteral("timelineClip-Opening shot"));
      end_handle = find_named_item(opening_item, QStringLiteral("timelineClipRightTrimHandle"));
      const auto cancel_trim_point = scene_rect(end_handle).center();
      send_pointer(QEvent::MouseButtonPress, cancel_trim_point);
      send_pointer(QEvent::MouseMove, cancel_trim_point - QPointF(30, 0));
      auto* trim_grabber = studio_window->mouseGrabberItem();
      if (!trim_grabber) return fail("A pointer trim should hold its input grab");
      trim_grabber->ungrabMouse();
      send_pointer(QEvent::MouseButtonRelease, cancel_trim_point - QPointF(30, 0));
      process_layout_events(8);
      QThread::msleep(30);
      process_layout_events(8);
      if (workflow_timeline->property("trimFeedbackState").toMap().value(QStringLiteral("visible")).toBool() ||
          workflow_timeline->property("pendingTrimEndSequenceSeconds").toDouble() >= 0 ||
          std::abs(clip_by_id(opening_id).value(QStringLiteral("trimmedDurationSeconds")).toDouble() - trimmed_duration) > 0.04) {
        return fail("Canceled trimming should restore the edit and discard pending timer work");
      }
      const auto start_before_move = clip_by_id(opening_id).value(QStringLiteral("startSeconds")).toDouble();
      process_layout_events(8);
      opening_item = find_named_item(workflow_timeline, QStringLiteral("timelineClip-Opening shot"));
      if (!drag_item(opening_item, 110.0) ||
          !wait_for_condition([&] {
            return clip_by_id(opening_id).value(QStringLiteral("startSeconds")).toDouble() > start_before_move + 0.4;
          }, 3000)) {
        return fail("Dragging the clip body should move it along the timeline");
      }
      const auto moved_start = clip_by_id(opening_id).value(QStringLiteral("startSeconds")).toDouble();
      bool linked_audio_verified = false;
      int linked_audio_matches = 0;
      for (const auto& row : timeline_document.timelineTracks()) {
        for (const auto& clip : row.toMap().value(QStringLiteral("clips")).toList()) {
          const auto data = clip.toMap();
          // Paired audio mirrors the visual clip ID. Its display label changes
          // from Silent to Audio when asynchronous media analysis completes.
          if (data.value(QStringLiteral("clipId")).toString() == opening_id &&
              data.value(QStringLiteral("trackKind")).toString() == QStringLiteral("audio")) {
            ++linked_audio_matches;
            linked_audio_verified = std::abs(data.value(QStringLiteral("startSeconds")).toDouble() - moved_start) < 0.04 &&
                std::abs(data.value(QStringLiteral("trimmedDurationSeconds")).toDouble() - trimmed_duration) < 0.04;
          }
        }
      }
      if (!linked_audio_verified || linked_audio_matches != 1) {
        std::cerr << "Linked audio timing: expected start=" << moved_start
                  << " duration=" << trimmed_duration << '\n'
                  << QJsonDocument::fromVariant(timeline_document.timelineTracks())
                         .toJson(QJsonDocument::Compact).toStdString() << '\n';
        return fail("Trim and move should preserve linked audio timing");
      }
      timeline_document.undo();
      if (std::abs(clip_by_id(opening_id).value(QStringLiteral("startSeconds")).toDouble() - start_before_move) > 0.04) {
        return fail("One Undo should restore the whole move gesture");
      }
      timeline_document.redo();
      if (!capture_workflow(QStringLiteral("04-move-linked-clips"), {QStringLiteral("timelineClip-Opening shot")})) {
        return fail("Moved linked clips should remain visible");
      }
      opening_item = live_item(QStringLiteral("timelineClip-Opening shot"));
      const auto cancel_move_point = scene_rect(opening_item).center();
      send_pointer(QEvent::MouseButtonPress, cancel_move_point);
      send_pointer(QEvent::MouseMove, cancel_move_point + QPointF(30, 0));
      auto* move_grabber = studio_window->mouseGrabberItem();
      if (!move_grabber) return fail("A pointer move should hold its input grab");
      move_grabber->ungrabMouse();
      send_pointer(QEvent::MouseButtonRelease, cancel_move_point + QPointF(30, 0));
      process_layout_events(8);
      if (workflow_timeline->property("dragPreviewState").toMap().value(QStringLiteral("visible")).toBool() ||
          std::abs(clip_by_id(opening_id).value(QStringLiteral("startSeconds")).toDouble() - moved_start) > 0.04) {
        return fail("Canceled clip movement should clear the preview and leave the committed position intact");
      }
      auto photo_asset = make_asset(QStringLiteral("studio-photo"), photo_path);
      auto added_asset = make_asset(QStringLiteral("workflow-video"), video_path,
                                   QUrl::fromLocalFile(photo_path).toString());
      added_asset.camera_label = QStringLiteral("Added shot");
      added_asset.media_type = QStringLiteral("video");
      added_asset.duration_seconds = 2.0;
      added_asset.duration_text = QStringLiteral("0:02");
      catalog_assets.replaceAssetsForTesting({photo_asset, added_asset});
#include "timeline_media_pool_proof.inc"
      timeline_playback.seekSequenceSeconds(0.0);
      if (!click_named(QStringLiteral("timelinePrimaryAddMediaButton")) ||
          !wait_for_condition([&] {
            auto* picker = find_named_object(workflow_timeline, QStringLiteral("galleryPickerDialog"));
            return picker && picker->property("opened").toBool() &&
                   live_item(QStringLiteral("galleryPickerAsset-workflow-video"));
          }, 5000) || !click_named(QStringLiteral("galleryPickerAsset-workflow-video")) ||
          !wait_for_condition([&] {
            auto* picker = find_named_object(workflow_timeline, QStringLiteral("galleryPickerDialog"));
            return picker && picker->property("selectedAssetId").toString() == QStringLiteral("workflow-video");
          }, 5000)) {
        return fail("Add Media should open a usable picker and allow pointer selection");
      }
      const auto count_before_add = timeline_document.clipCount();
      const auto requested_add_position = timeline_document.sequencePlayheadSeconds();
      if (!capture_workflow(QStringLiteral("05-picker-add-action"), {QStringLiteral("timelinePickerAddToTimelineButton")}) ||
          !click_named(QStringLiteral("timelinePickerAddToTimelineButton")) ||
          !wait_for_condition([&] {
            auto* picker = find_named_object(workflow_timeline, QStringLiteral("galleryPickerDialog"));
            return timeline_document.clipCount() > count_before_add && picker && !picker->property("visible").toBool();
          }, 5000)) {
        return fail("Picker primary Add to Timeline should add media, rather than only loading Source");
      }
      const auto added_id = timeline_document.currentClip().value(QStringLiteral("clipId")).toString();
      if (timeline_document.currentClip().value(QStringLiteral("assetId")).toString() != QStringLiteral("workflow-video") ||
          std::abs(timeline_document.currentClip().value(QStringLiteral("startSeconds")).toDouble() - requested_add_position) > 0.04) {
        std::cerr << "Add: requested=" << requested_add_position
                  << " actual=" << QJsonDocument::fromVariant(timeline_document.currentClip()).toJson(QJsonDocument::Compact).toStdString() << '\n';
        return fail("Newly added media should be selected at the requested playhead");
      }
      QVariant seed_source_trackable;
      if (!QMetaObject::invokeMethod(workflow_timeline, "motionSeedSourceIsTrackable",
                                     Q_RETURN_ARG(QVariant, seed_source_trackable)) ||
          !seed_source_trackable.toBool())
        return fail("The isolated video fixture should be eligible for source-normalized seed dragging");
      auto* auto_reframe_popup = find_named_object(workflow_timeline, QStringLiteral("timelineAutoReframePopup"));
      QMetaObject::invokeMethod(workflow_timeline, "openAutoReframe", Q_ARG(QVariant, QVariant{}));
      if (!wait_for_condition([&] {
            auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineAutoReframePopup"));
            return popup && popup->property("visible").toBool();
          }, 3000) || !click_named(QStringLiteral("timelineOpenTrackingToolsButton")) ||
          !wait_for_condition([&] {
            auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup"));
            return popup && popup->property("visible").toBool() && auto_reframe_popup &&
                   !auto_reframe_popup->property("visible").toBool();
          }, 3000) || !capture_workflow(QStringLiteral("06a-motion-tracking-panel"),
                                        {QStringLiteral("timelineTrackingPanel"),
                                         QStringLiteral("timelineMotionSeedButton")}) ||
          !click_named(QStringLiteral("timelineMotionSeedButton")) ||
         !wait_for_condition([&] {
            auto* hud = find_named_object(workflow_timeline, QStringLiteral("timelineMotionSeedHud"));
            return hud && hud->property("visible").toBool() &&
                   !workflow_timeline->property("motionSeedAwaitingFirstFrame").toBool();
          }, 5000) || !capture_workflow(QStringLiteral("06b-motion-seed-hud"),
                                        {QStringLiteral("timelineMotionSeedHud"),
                                         QStringLiteral("timelineProgramMonitor")})) {
        auto* seed_button = find_named_object(workflow_timeline, QStringLiteral("timelineMotionSeedButton"));
        auto* hud = find_named_object(workflow_timeline, QStringLiteral("timelineMotionSeedHud"));
        auto* tracking_popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup"));
        std::cerr << "Motion seed setup state: button_exists=" << (seed_button != nullptr)
                  << " button_visible=" << (seed_button && item_visible(qobject_cast<QQuickItem*>(seed_button)))
                  << " button_enabled=" << (seed_button && seed_button->property("enabled").toBool())
                  << " hud_visible=" << (hud && hud->property("visible").toBool())
                  << " hud_visible_flag=" << workflow_timeline->property("motionSeedSelectionHudVisible").toBool()
                  << " awaiting_first_frame=" << workflow_timeline->property("motionSeedAwaitingFirstFrame").toBool()
                  << " tracking_popup_visible=" << (tracking_popup && tracking_popup->property("visible").toBool())
                  << " media_ready=" << timeline_playback.mediaReady()
                  << " playhead=" << timeline_document.sequencePlayheadSeconds()
                  << " expected=" << workflow_timeline->property("motionSeedExpectedSequenceSeconds").toDouble()
                  << " processed_frame=" << timeline_playback.processedFrameSource().toStdString()
                  << " before_frame=" << workflow_timeline->property("motionSeedFrameBeforeSeek").toString().toStdString()
                  << '\n';
        report_playback();
        return fail("Tracking setup should close the modal inspector and seek to the first frame before region dragging");
      }
      auto* seed_layer = live_item(QStringLiteral("timelineMotionSeedSelectionLayer"));
      if (!seed_layer) return fail("First-frame seed selection should expose a live monitor overlay");
      const auto seed_bounds = scene_rect(seed_layer);
      const auto seed_start = seed_bounds.topLeft() + QPointF(seed_bounds.width() * 0.30,
                                                              seed_bounds.height() * 0.35);
      const auto seed_end = seed_start + QPointF(seed_bounds.width() * 0.14,
                                                 seed_bounds.height() * 0.12);
      workflow_pointer_timestamp += static_cast<ulong>(QApplication::doubleClickInterval() + 1);
      send_pointer(QEvent::MouseButtonPress, seed_start);
      for (int step = 1; step <= 6; ++step)
        send_pointer(QEvent::MouseMove, seed_start + (seed_end - seed_start) * step / 6.0);
      send_pointer(QEvent::MouseButtonRelease, seed_end);
      const auto seeded_region = workflow_timeline->property("motionSeedRectNormalized").toMap();
      if (seeded_region.value(QStringLiteral("width")).toDouble() < 0.05 ||
          seeded_region.value(QStringLiteral("height")).toDouble() < 0.05 ||
          !click_named(QStringLiteral("timelineMotionSeedHudDoneButton")) ||
          !wait_for_condition([&] {
            auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup"));
            return popup && popup->property("visible").toBool();
          }, 3000)) {
        return fail("Monitor dragging should set a normalized first-frame seed and reopen tracking controls");
      }
      if (auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup")))
        QMetaObject::invokeMethod(popup, "close");
      if (!wait_for_condition([&] {
            auto* popup = find_named_object(workflow_timeline, QStringLiteral("timelineMotionTrackingPopup"));
            return popup && !popup->property("visible").toBool() &&
                   !popup->property("opened").toBool();
          }, 3000))
        return fail("Tracking controls must finish closing before the ordinary edit workspace is captured");
      if (!capture_workflow(QStringLiteral("06-media-added-at-playhead"), {QStringLiteral("timelineClip-Added shot")})) {
        return fail("Added media should appear in the sequence");
      }
      const auto count_before_preview = timeline_document.clipCount();
      if (!click_named(QStringLiteral("timelinePrimaryAddMediaButton")) ||
          !wait_for_condition([&] {
            auto* picker = find_named_object(workflow_timeline, QStringLiteral("galleryPickerDialog"));
            return picker && picker->property("opened").toBool() &&
                   live_item(QStringLiteral("galleryPickerAsset-workflow-video"));
          }, 5000) ||
          !click_named(QStringLiteral("galleryPickerAsset-workflow-video")) ||
          !wait_for_condition([&] {
            auto* picker = find_named_object(workflow_timeline, QStringLiteral("galleryPickerDialog"));
            return picker && picker->property("selectedAssetId").toString() == QStringLiteral("workflow-video");
          }, 5000)) {
        return fail("Picker should reopen for preview-only media inspection");
      }
      if (!capture_workflow(QStringLiteral("07-picker-preview-action"), {QStringLiteral("timelinePickerPreviewInSourceButton")}) ||
          !click_named(QStringLiteral("timelinePickerPreviewInSourceButton")) ||
          !wait_for_condition([&] {
            auto* picker = find_named_object(workflow_timeline, QStringLiteral("galleryPickerDialog"));
            return picker && !picker->property("visible").toBool();
          }, 5000) || timeline_document.clipCount() != count_before_preview ||
          timeline_document.sourceClip().value(QStringLiteral("assetId")).toString() != QStringLiteral("workflow-video")) {
        capture_workflow(QStringLiteral("preview-failure"), {});
        std::cerr << "Preview: clips=" << timeline_document.clipCount() << " expected=" << count_before_preview
                  << " source=" << QJsonDocument::fromVariant(timeline_document.sourceClip()).toJson(QJsonDocument::Compact).toStdString()
                  << " status=" << timeline_document.status().toStdString() << '\n';
        return fail("Preview in Source should inspect media without adding or replacing sequence clips");
      }
      if (!wait_for_condition([&] {
            auto* source_video = live_item(QStringLiteral("timelineSourceVideoOutput"));
            auto* source_poster = live_item(QStringLiteral("timelineSourcePosterImage"));
            return item_visible(source_video) ||
                   (item_visible(source_poster) && source_poster->property("status").toInt() == 1);
          }, 5000)) {
        for (auto* poster : find_named_items(studio_root, QStringLiteral("timelineSourcePosterImage"))) {
          std::cerr << "Source poster: status=" << poster->property("status").toInt()
                    << " source=" << poster->property("source").toUrl().toString().toStdString() << '\n';
          for (auto* ancestor = poster; ancestor; ancestor = ancestor->parentItem()) {
            std::cerr << ancestor->metaObject()->className() << ' ' << ancestor->objectName().toStdString()
                      << " visible=" << ancestor->isVisible() << " size=" << ancestor->width() << 'x' << ancestor->height()
                      << " implicit=" << ancestor->implicitWidth() << 'x' << ancestor->implicitHeight() << '\n';
          }
        }
        return fail("Source preview should show its poster or a decoded frame after loading media");
      }
      if (!capture_workflow(QStringLiteral("08-source-preview"),
            {QStringLiteral("timelineProgramMonitor"), QStringLiteral("timelineSourceEditorScroll")})) {
        return fail("Source preview layout should remain usable");
      }
      const auto source_resume_fixture_path = QDir(proof_dir).filePath(
          QStringLiteral("source-resume-red-blue.mp4"));
      QProcess source_resume_fixture;
      source_resume_fixture.setProcessChannelMode(QProcess::MergedChannels);
      source_resume_fixture.start(QStringLiteral("ffmpeg"), {
          QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
          QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
          QStringLiteral("color=c=red:s=320x180:r=30:d=0.5"),
          QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
          QStringLiteral("color=c=blue:s=320x180:r=30:d=1.5"),
          QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
          QStringLiteral("sine=frequency=440:sample_rate=48000:duration=2"),
          QStringLiteral("-filter_complex"),
          QStringLiteral("[0:v][1:v]concat=n=2:v=1:a=0,format=yuv420p[v]"),
          QStringLiteral("-map"), QStringLiteral("[v]"),
          QStringLiteral("-map"), QStringLiteral("2:a:0"),
          QStringLiteral("-t"), QStringLiteral("2"), QStringLiteral("-r"), QStringLiteral("30"),
          QStringLiteral("-af"), QStringLiteral("volume=0.02"),
          QStringLiteral("-c:v"), QStringLiteral("libx264"),
          QStringLiteral("-preset"), QStringLiteral("ultrafast"),
          QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
          QStringLiteral("-c:a"), QStringLiteral("aac"),
          QStringLiteral("-b:a"), QStringLiteral("96k"),
          QStringLiteral("-movflags"), QStringLiteral("+faststart"),
          QStringLiteral("-y"), source_resume_fixture_path,
      });
      if (!source_resume_fixture.waitForStarted(3000) ||
          !source_resume_fixture.waitForFinished(15000) ||
          source_resume_fixture.exitStatus() != QProcess::NormalExit ||
          source_resume_fixture.exitCode() != 0 ||
          QFileInfo(source_resume_fixture_path).size() <= 1024) {
        std::cerr << "Source fixture generation: "
                  << source_resume_fixture.readAllStandardOutput().toStdString() << '\n';
        return fail("The Source resume proof needs its two-color decoded-video fixture");
      }
      timeline_document.loadSourceClipFromGallery(
          QStringLiteral("source-resume-red-blue"), source_resume_fixture_path,
          QStringLiteral("Source resume proof"), 2.0);
      if (timeline_document.sourceClip().value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("source-resume-red-blue"))
        return fail("The Source resume fixture should load without changing the sequence");
      auto* source_panel = find_named_object(
          studio_root, QStringLiteral("timelineSourcePreviewRail"));
      auto* source_video_output = find_named_object(
          source_panel, QStringLiteral("timelineSourceVideoOutput"));
      auto* source_preview_player = find_named_object(
          source_panel, QStringLiteral("timelineSourcePreviewPlayer"));
      auto* source_sink = source_video_output
          ? source_video_output->property("videoSink").value<QVideoSink*>()
          : nullptr;
      constexpr qint64 kSourceResumeProofTargetUs = 750'000;
      qint64 source_initial_frame_pts_us = -1;
      QColor source_initial_frame_pixel;
      int source_playback_start_frame_generation = -1;
      int source_first_user_play_frame_generation = -1;
      double source_playback_start_seconds = -1.0;
      qint64 source_first_user_play_frame_position_us = -1;
      qint64 source_first_user_play_frame_pts_us = -1;
      QColor source_first_user_play_frame_pixel;
      bool source_preroll_audio_detached = false;
      bool source_first_user_play_frame_audio_restored = false;
      QMetaObject::Connection source_first_user_play_frame_connection;
      if (!source_panel || !source_preview_player || !source_sink ||
          !wait_for_condition([&] {
            const auto frame = source_sink->videoFrame();
            source_preroll_audio_detached =
                source_preview_player->property("audioOutput").isNull();
            return source_preview_player->property("source").toUrl().toLocalFile() ==
                       source_resume_fixture_path &&
                   source_panel->property("sourcePrerollActive").toBool() &&
                   source_panel->property("sourcePrerollFirstFrameSeen").toBool() &&
                   source_preview_player->property("playbackState").toInt() == 1 &&
                   source_preroll_audio_detached &&
                   frame.isValid();
          }, 5000)) {
        return fail("Source should enter silent preroll with its first decoded frame before Play");
      }
      auto* source_play_button = live_item(QStringLiteral("timelineSourcePlayButton"));
      if (!source_play_button ||
          source_play_button->property("iconName").toString() != QStringLiteral("play") ||
          !QMetaObject::invokeMethod(
              source_panel, "seekSource", Q_ARG(QVariant, QVariant(0.75)),
              Q_ARG(QVariant, QVariant(false)))) {
        return fail("Source should accept the requested target before explicit Play");
      }
      source_first_user_play_frame_connection = QObject::connect(
          source_sink, &QVideoSink::videoFrameChanged, source_panel,
          [&](const QVideoFrame& frame) {
            if (source_first_user_play_frame_generation >= 0 ||
                source_preview_player->property("playbackState").toInt() != 1)
              return;
            const auto start_generation =
                source_panel->property("sourcePrerollPlaybackStartFrameGeneration").toInt();
            const auto current_frame_generation =
                source_panel->property("sourceFrameGeneration").toInt();
            if (start_generation < 0 || current_frame_generation <= start_generation ||
                !frame.isValid())
              return;
            const auto image = frame.toImage();
            if (image.isNull())
              return;
            source_playback_start_frame_generation = start_generation;
            source_playback_start_seconds =
                source_panel->property("sourcePrerollPlaybackStartSeconds").toDouble();
            source_first_user_play_frame_generation =
                source_panel->property("sourceFrameGeneration").toInt();
            source_first_user_play_frame_position_us =
                source_preview_player->property("position").toLongLong() * 1000;
            source_first_user_play_frame_pts_us = frame.startTime();
            source_first_user_play_frame_pixel =
                image.pixelColor(image.width() / 2, image.height() / 2);
            source_first_user_play_frame_audio_restored =
                !source_preview_player->property("audioOutput").isNull();
          });
      if (!click_named(QStringLiteral("timelineSourcePlayButton")) ||
          !(source_panel->property("sourcePrerollPlayAfterSeek").toBool() ||
            source_panel->property("sourcePrerollPlaybackHandoffPending").toBool() ||
            (!source_panel->property("sourcePrerollActive").toBool() &&
             source_panel->property("sourcePrerollCompletedTargetSeconds").toDouble() >= 0.65 &&
             source_preview_player->property("playbackState").toInt() == 1)) ||
          !source_panel->property("sourceUserPlaybackRequested").toBool()) {
        QObject::disconnect(source_first_user_play_frame_connection);
        return fail("Play during silent preroll should retain the requested Source target");
      }
      const auto source_play_handoff_ready = wait_for_condition([&] {
            const auto frame = source_sink->videoFrame();
            if (!frame.isValid() || source_panel->property("sourcePrerollActive").toBool() ||
                source_panel->property("sourcePrerollPlaybackHandoffPending").toBool() ||
                !source_panel->property("sourceDecoderPrimed").toBool() ||
                !source_panel->property("sourcePrerollCompletedFrameValid").toBool() ||
                source_preview_player->property("playbackState").toInt() != 1 ||
                !source_panel->property("sourceUserPlaybackRequested").toBool())
              return false;
            const auto image = frame.toImage();
            if (image.isNull())
              return false;
            const auto pixel = image.pixelColor(image.width() / 2, image.height() / 2);
            const auto player_position_us =
                source_preview_player->property("position").toLongLong() * 1000;
            const auto frame_pts_match = frame.startTime() < 0 ||
                qAbs(frame.startTime() - kSourceResumeProofTargetUs) <= 100'000;
            const auto completed_generation =
                source_panel->property("sourcePrerollCompletedFrameGeneration").toInt();
            const auto seek_generation =
                source_panel->property("sourcePrerollCompletedSeekFrameGeneration").toInt();
            const auto acknowledged_generation =
                source_panel->property("sourcePrerollCompletedAcknowledgedFrameGeneration").toInt();
            const auto acknowledged_position_ms =
                source_panel->property("sourcePrerollCompletedAcknowledgedPositionMs").toDouble();
            const auto start_frame_generation =
                source_panel->property("sourcePrerollPlaybackStartFrameGeneration").toInt();
            const auto start_seconds =
                source_panel->property("sourcePrerollPlaybackStartSeconds").toDouble();
            const auto requested_frame_match =
                qAbs(player_position_us - kSourceResumeProofTargetUs) <= 100'000 &&
                frame_pts_match && source_preview_player->property("playbackState").toInt() == 1 &&
                source_panel->property("sourceDecoderPrimed").toBool() &&
                completed_generation > seek_generation &&
                acknowledged_generation > seek_generation &&
                completed_generation > acknowledged_generation &&
                acknowledged_position_ms > 750.0 &&
                acknowledged_position_ms - 750.0 <= 100.0 &&
                source_panel->property("sourceFrameGeneration").toInt() > start_frame_generation &&
                start_frame_generation >= completed_generation &&
                qAbs(start_seconds - 0.75) <= 0.1 &&
                source_first_user_play_frame_generation > source_playback_start_frame_generation &&
                qAbs(source_first_user_play_frame_position_us - kSourceResumeProofTargetUs) <= 100'000 &&
                (source_first_user_play_frame_pts_us < 0 ||
                 qAbs(source_first_user_play_frame_pts_us - kSourceResumeProofTargetUs) <= 100'000) &&
                source_first_user_play_frame_pixel.blue() >
                    source_first_user_play_frame_pixel.red() + 65 &&
                source_first_user_play_frame_pixel.blue() >
                    source_first_user_play_frame_pixel.green() + 35 &&
                source_first_user_play_frame_audio_restored &&
                pixel.blue() > pixel.red() + 65 &&
                pixel.blue() > pixel.green() + 35;
            if (!requested_frame_match ||
                std::abs(source_panel->property("previewSeconds").toDouble() - 0.75) >= 0.1)
              return false;
            source_initial_frame_pts_us = frame.startTime();
            source_initial_frame_pixel = pixel;
            return true;
          }, 5000);
      QObject::disconnect(source_first_user_play_frame_connection);
      if (!source_play_handoff_ready) {
        const auto failed_frame = source_sink->videoFrame();
        const auto failed_image = failed_frame.isValid() ? failed_frame.toImage() : QImage{};
        const auto failed_pixel = failed_image.isNull()
            ? QColor{} : failed_image.pixelColor(failed_image.width() / 2,
                                                  failed_image.height() / 2);
        std::cerr << "Source Play handoff: state="
                  << source_preview_player->property("playbackState").toInt()
                  << " positionMs="
                  << source_preview_player->property("position").toLongLong()
                  << " audioDetached="
                  << source_preview_player->property("audioOutput").isNull()
                  << " preroll=" << source_panel->property("sourcePrerollActive").toBool()
                  << " firstFrame="
                  << source_panel->property("sourcePrerollFirstFrameSeen").toBool()
                  << " seekIssued="
                  << source_panel->property("sourcePrerollSeekIssued").toBool()
                  << " seekAck="
                  << source_panel->property("sourcePrerollSeekAcknowledged").toBool()
                  << " requestedMs="
                  << source_panel->property("sourcePrerollSeekRequestedPositionMs").toDouble()
                  << " ackMs="
                  << source_panel->property("sourcePrerollSeekAcknowledgedPositionMs").toDouble()
                  << " ackFrameGen="
                  << source_panel->property("sourcePrerollSeekAcknowledgedFrameGeneration").toInt()
                  << " completedTarget="
                  << source_panel->property("sourcePrerollCompletedTargetSeconds").toDouble()
                  << " completedPosition="
                  << source_panel->property("sourcePrerollCompletedPositionSeconds").toDouble()
                  << " completedSeekGen="
                  << source_panel->property("sourcePrerollCompletedSeekFrameGeneration").toInt()
                  << " completedAckGen="
                  << source_panel->property("sourcePrerollCompletedAcknowledgedFrameGeneration").toInt()
                  << " completedAckMs="
                  << source_panel->property("sourcePrerollCompletedAcknowledgedPositionMs").toDouble()
                  << " completedFrameGen="
                  << source_panel->property("sourcePrerollCompletedFrameGeneration").toInt()
                  << " decoderPrimed="
                  << source_panel->property("sourceDecoderPrimed").toBool()
                  << " handoffPending="
                  << source_panel->property("sourcePrerollPlaybackHandoffPending").toBool()
                  << " userRequested="
                  << source_panel->property("sourceUserPlaybackRequested").toBool()
                  << " sourceFrameGen="
                  << source_panel->property("sourceFrameGeneration").toInt()
                  << " startFrameGen="
                  << source_panel->property("sourcePrerollPlaybackStartFrameGeneration").toInt()
                  << " startSeconds="
                  << source_panel->property("sourcePrerollPlaybackStartSeconds").toDouble()
                  << " failedFirstGen=" << source_first_user_play_frame_generation
                  << " failedFirstPositionUs=" << source_first_user_play_frame_position_us
                  << " failedFirstPtsUs=" << source_first_user_play_frame_pts_us
                  << " failedFirstAudioRestored="
                  << source_first_user_play_frame_audio_restored
                  << " failedFirstPixel=" << source_first_user_play_frame_pixel.red() << ','
                  << source_first_user_play_frame_pixel.green() << ','
                  << source_first_user_play_frame_pixel.blue()
                  << " sinkValid=" << failed_frame.isValid()
                  << " sinkPtsUs=" << failed_frame.startTime()
                  << " sinkPixel=" << failed_pixel.red() << ',' << failed_pixel.green() << ','
                  << failed_pixel.blue() << '\n';
        return fail("Play during Source preroll should start from the requested decoded blue frame");
      }
      auto* source_pause_button = live_item(QStringLiteral("timelineSourcePlayButton"));
      // Explicit Play advances the clock. Pause must retain the position at the
      // user's stop request, rather than the earlier seek target.
      const auto source_pause_request_position_us =
          source_preview_player->property("position").toLongLong() * 1000;
      if (!click_item_without_layout_gap(source_pause_button) ||
          !wait_for_condition([&] {
            const auto frame = source_sink->videoFrame();
            if (!frame.isValid() || source_preview_player->property("playbackState").toInt() == 1 ||
                qAbs(source_preview_player->property("position").toLongLong() * 1000 -
                     source_pause_request_position_us) > 100'000 ||
                std::abs(source_panel->property("previewSeconds").toDouble() -
                         source_preview_player->property("position").toLongLong() / 1000.0) >= 0.1 ||
                (frame.startTime() >= 0 &&
                 qAbs(frame.startTime() - source_pause_request_position_us) > 100'000))
              return false;
            const auto image = frame.toImage();
            if (image.isNull())
              return false;
            const auto pixel = image.pixelColor(image.width() / 2, image.height() / 2);
            return pixel.blue() > pixel.red() + 65 && pixel.blue() > pixel.green() + 35 &&
                   qAbs(pixel.red() - source_initial_frame_pixel.red()) <= 24 &&
                   qAbs(pixel.green() - source_initial_frame_pixel.green()) <= 24 &&
                   qAbs(pixel.blue() - source_initial_frame_pixel.blue()) <= 24;
          }, 2000) || timeline_document.clipCount() != count_before_preview ||
          timeline_document.sequencePlaying()) {
        const auto paused_source_frame = source_sink->videoFrame();
        const auto paused_source_image = paused_source_frame.isValid()
            ? paused_source_frame.toImage() : QImage{};
        const auto paused_source_pixel = paused_source_image.isNull()
            ? QColor{} : paused_source_image.pixelColor(paused_source_image.width() / 2,
                                                         paused_source_image.height() / 2);
        std::cerr << "Source Pause frame: state="
                  << source_preview_player->property("playbackState").toInt()
                  << " positionMs="
                  << source_preview_player->property("position").toLongLong()
                  << " previewSeconds=" << source_panel->property("previewSeconds").toDouble()
                  << " ptsUs=" << paused_source_frame.startTime()
                  << " pixel=" << paused_source_pixel.red() << ',' << paused_source_pixel.green()
                  << ',' << paused_source_pixel.blue()
                  << " initialPixel=" << source_initial_frame_pixel.red() << ','
                  << source_initial_frame_pixel.green() << ',' << source_initial_frame_pixel.blue()
                  << " firstPlayPositionUs=" << source_first_user_play_frame_position_us
                  << " firstPlayPtsUs=" << source_first_user_play_frame_pts_us
                  << " firstPlayPixel=" << source_first_user_play_frame_pixel.red() << ','
                  << source_first_user_play_frame_pixel.green() << ','
                  << source_first_user_play_frame_pixel.blue()
                  << " firstPlayGen=" << source_first_user_play_frame_generation
                  << " playbackStartSec=" << source_playback_start_seconds
                  << " prerollTarget="
                  << source_panel->property("sourcePrerollCompletedTargetSeconds").toDouble()
                  << " sequencePlaying=" << timeline_document.sequencePlaying()
                  << " clipCount=" << timeline_document.clipCount()
                  << " expectedClipCount=" << count_before_preview << '\n';
        return fail("Source Play/Pause should show the sought blue frame without editing or playing Program");
      }
      const auto source_paused_position_us =
          source_preview_player->property("position").toLongLong() * 1000;
      if (!capture_workflow(QStringLiteral("09-source-decoded"), {QStringLiteral("timelineSourceVideoOutput")})) {
        return fail("Decoded Source preview should remain visible after Pause");
      }
      if (!capture_workflow(QStringLiteral("09a-save-and-render-actions"),
                            {QStringLiteral("timelineSaveStateChip"), QStringLiteral("timelineSaveButton")}))
        return fail("Save and Render should be separate visible actions");
      if (!click_named(QStringLiteral("timelineSaveStateChip")) ||
          !wait_for_condition([&] { return !timeline_document.timelineSaveInProgress() &&
                                          !timeline_document.savedDocumentPath().isEmpty() && !timeline_document.dirty(); }, 10000))
        return fail("The visible Save action should save edits without publishing");
      const auto source_session_path = timeline_document.savedDocumentPath();
      const auto saved_opening_start = clip_by_id(opening_id).value(QStringLiteral("startSeconds")).toDouble();
      // Exercise the real shell with loaded media, rather than an empty route.
      timeline_playback.seekSequenceSeconds(0.3);
      timeline_playback.play();
      if (!click_named(QStringLiteral("timelineSourcePlayButton")))
        return fail("Source should start before testing workspace suspension");
      process_layout_events(4);
      QElapsedTimer edit_switch_timer;
      edit_switch_timer.start();
      if (!show_studio_workspace("editWorkspaceIndex", QStringLiteral("editStudioWorkspace")))
        return fail("A loaded Timeline should return to Edit");
      const auto edit_switch_ms = edit_switch_timer.elapsed();
      const auto hidden_document_hash = timeline_document.deterministicDocumentHash();
      const auto hidden_selected_clip_ids = timeline_document.selectedClipIds();
      const auto hidden_can_undo = timeline_document.canUndo();
      QElapsedTimer source_hidden_total_timer;
      source_hidden_total_timer.start();
      QPointer<QObject> source_panel_lifetime(source_panel);
      QPointer<QObject> source_player_lifetime(source_preview_player);
      const auto source_resume_target_seconds = source_panel->property("previewSeconds").toDouble();
      const auto hidden_playhead = timeline_document.sequencePlayheadSeconds();
      const auto hidden_frames = timeline_playback.frameProcessingMetrics().value(QStringLiteral("sampleCount")).toInt();
      QEventLoop hidden_timeline_loop;
      QTimer::singleShot(250, &hidden_timeline_loop, &QEventLoop::quit);
      hidden_timeline_loop.exec();
      const auto hidden_playhead_delta = timeline_document.sequencePlayheadSeconds() - hidden_playhead;
      const auto hidden_frame_delta = timeline_playback.frameProcessingMetrics().value(QStringLiteral("sampleCount")).toInt() - hidden_frames;
      const auto hidden_timeline_playing = timeline_document.sequencePlaying();
      auto* hidden_source_player = find_named_object(source_panel, QStringLiteral("timelineSourcePreviewPlayer"));
      if (hidden_timeline_playing || std::abs(hidden_playhead_delta) > 0.0001 || hidden_frame_delta != 0 ||
          !hidden_source_player || hidden_source_player->property("playbackState").toInt() == 1)
        return fail("Hidden Timeline must pause Program and Source and stop frame processing");
      QEventLoop source_release_loop;
      QTimer::singleShot(2250, &source_release_loop, &QEventLoop::quit);
      source_release_loop.exec();
      if (source_panel_lifetime.isNull() || source_player_lifetime.isNull() ||
          !source_preview_player->property("source").toUrl().isEmpty() ||
          source_preview_player->property("playbackState").toInt() == 1 ||
          source_sink->videoFrame().isValid() ||
          source_panel->property("suspendedSourceSeconds").toDouble() < 0.65 ||
          std::abs(source_panel->property("suspendedSourceSeconds").toDouble() -
                   source_resume_target_seconds) > 0.1) {
        return fail("Source should release its decoder after hibernation while retaining the paused source position");
      }
      const auto source_eviction_wait_ms = static_cast<int>(
          std::max<qint64>(0, 10'250 - source_hidden_total_timer.elapsed()));
      QEventLoop source_eviction_loop;
      QTimer::singleShot(source_eviction_wait_ms, &source_eviction_loop, &QEventLoop::quit);
      source_eviction_loop.exec();
      if (!wait_for_condition([&] {
            return source_panel_lifetime.isNull() && source_player_lifetime.isNull();
          }, 1500))
        return fail("After ten hidden seconds, Timeline must evict the Source panel and its decoder object");
      const auto source_eviction_elapsed_ms = source_hidden_total_timer.elapsed();
      timeline_playback.pause();
      if (!show_studio_workspace("timelineWorkspaceIndex", QStringLiteral("timelineWorkspace")))
        return fail("A suspended Timeline should reopen");
      if (!capture_workflow(QStringLiteral("10-return-from-edit"), {QStringLiteral("timelineProgramMonitor")}))
        return fail("Returning from Edit should restore the Timeline monitor");
      QObject* resumed_source_panel = nullptr;
      QObject* resumed_source_player = nullptr;
      QObject* resumed_source_output = nullptr;
      QVideoSink* resumed_source_sink = nullptr;
      if (!wait_for_condition([&] {
            resumed_source_panel = find_named_object(
                studio_root, QStringLiteral("timelineSourcePreviewRail"));
            resumed_source_player = find_named_object(
                resumed_source_panel, QStringLiteral("timelineSourcePreviewPlayer"));
            resumed_source_output = find_named_object(
                resumed_source_panel, QStringLiteral("timelineSourceVideoOutput"));
            resumed_source_sink = resumed_source_output
                ? resumed_source_output->property("videoSink").value<QVideoSink*>() : nullptr;
            return resumed_source_panel && resumed_source_player && resumed_source_sink;
          }, 5000))
        return fail("Timeline should recreate its evicted Source panel when the session resumes");
      if (!timeline_document.hasActiveTimeline() ||
          timeline_document.savedDocumentPath() != source_session_path ||
          timeline_document.sourceClip().value(QStringLiteral("assetId")).toString() !=
              QStringLiteral("source-resume-red-blue") ||
          std::abs(timeline_document.sequencePlayheadSeconds() - hidden_playhead) > 0.0001 ||
          timeline_document.deterministicDocumentHash() != hidden_document_hash ||
          timeline_document.selectedClipIds() != hidden_selected_clip_ids ||
          timeline_document.canUndo() != hidden_can_undo)
        return fail("Evicting the Source panel should preserve the active document, source clip, and Program playhead");
      const auto source_resume_target_us =
          qRound64(source_resume_target_seconds * 1'000'000.0);
      qint64 source_resume_frame_pts_us = -1;
      QColor source_resume_frame_pixel;
      int source_resume_frame_generation = -1;
      int source_resume_seek_frame_generation = -1;
      int source_resume_acknowledged_frame_generation = -1;
      double source_resume_acknowledged_position_ms = -1.0;
      double source_resume_completed_position_seconds = -1.0;
      const auto source_resume_ready = wait_for_condition([&] {
        if (!resumed_source_panel || !resumed_source_player || !resumed_source_sink)
          return false;
        const auto frame = resumed_source_sink->videoFrame();
        const auto image = frame.isValid() ? frame.toImage() : QImage{};
        if (!frame.isValid() || image.isNull() ||
            !resumed_source_panel->property("sourceViewerActive").toBool() ||
            !resumed_source_panel->property("sourceDecoderPrimed").toBool() ||
            resumed_source_panel->property("sourcePrerollActive").toBool() ||
            resumed_source_panel->property("sourcePrerollFailed").toBool() ||
            !resumed_source_panel->property("sourcePrerollCompletedFrameValid").toBool() ||
            !resumed_source_panel->property("sourceFrameAvailable").toBool() ||
            resumed_source_player->property("playbackState").toInt() == 1 ||
            qAbs(resumed_source_player->property("position").toLongLong() * 1000 -
                 source_resume_target_us) > 100'000 ||
            std::abs(resumed_source_panel->property("previewSeconds").toDouble() -
                     source_resume_target_seconds) > 0.1 ||
            std::abs(resumed_source_panel->property("sourcePrerollCompletedTargetSeconds").toDouble() -
                     source_resume_target_seconds) > 0.1 ||
            std::abs(resumed_source_panel->property("sourcePrerollCompletedPositionSeconds").toDouble() -
                     source_resume_target_seconds) > 0.1 ||
            timeline_document.sequencePlaying())
          return false;

        const auto completed_frame_generation =
            resumed_source_panel->property("sourcePrerollCompletedFrameGeneration").toInt();
        const auto completed_seek_frame_generation =
            resumed_source_panel->property("sourcePrerollCompletedSeekFrameGeneration").toInt();
        const auto completed_acknowledged_frame_generation = resumed_source_panel
            ->property("sourcePrerollCompletedAcknowledgedFrameGeneration").toInt();
        const auto completed_acknowledged_position_ms = resumed_source_panel
            ->property("sourcePrerollCompletedAcknowledgedPositionMs").toDouble();
        if (completed_seek_frame_generation < 0 ||
            completed_acknowledged_frame_generation <= completed_seek_frame_generation ||
            completed_frame_generation <= completed_acknowledged_frame_generation ||
            completed_acknowledged_position_ms <= source_resume_target_us / 1000.0 ||
            completed_acknowledged_position_ms - source_resume_target_us / 1000.0 > 100.0 ||
            completed_frame_generation <= completed_seek_frame_generation ||
            resumed_source_panel->property("sourceFrameGeneration").toInt() < completed_frame_generation)
          return false;

        const auto pixel = image.pixelColor(image.width() / 2, image.height() / 2);
        const auto blue_reference = pixel.blue() > pixel.red() + 65 &&
            pixel.blue() > pixel.green() + 35;
        const auto matches_initial_reference =
            qAbs(pixel.red() - source_initial_frame_pixel.red()) <= 24 &&
            qAbs(pixel.green() - source_initial_frame_pixel.green()) <= 24 &&
            qAbs(pixel.blue() - source_initial_frame_pixel.blue()) <= 24;
        if (!blue_reference || !matches_initial_reference)
          return false;

        if (frame.startTime() >= 0 &&
            qAbs(frame.startTime() - source_resume_target_us) > 100'000)
          return false;

        source_resume_frame_pts_us = frame.startTime();
        source_resume_frame_pixel = pixel;
        source_resume_frame_generation = completed_frame_generation;
        source_resume_seek_frame_generation = completed_seek_frame_generation;
        source_resume_acknowledged_frame_generation = completed_acknowledged_frame_generation;
        source_resume_acknowledged_position_ms = completed_acknowledged_position_ms;
        source_resume_completed_position_seconds = resumed_source_panel
            ->property("sourcePrerollCompletedPositionSeconds").toDouble();
        return true;
      }, 5000);
      if (!source_resume_ready) {
        const auto frame = resumed_source_sink ? resumed_source_sink->videoFrame() : QVideoFrame{};
        std::cerr << "Source resume: target=" << source_resume_target_seconds
                  << " panel=" << (resumed_source_panel
                        ? resumed_source_panel->property("previewSeconds").toDouble() : -1.0)
                  << " player=" << (resumed_source_player
                        ? resumed_source_player->property("position").toLongLong() : -1)
                  << " pts=" << frame.startTime()
                  << " primed=" << (resumed_source_panel &&
                        resumed_source_panel->property("sourceDecoderPrimed").toBool())
                  << " preroll=" << (resumed_source_panel &&
                        resumed_source_panel->property("sourcePrerollActive").toBool())
                  << " failed=" << (resumed_source_panel &&
                        resumed_source_panel->property("sourcePrerollFailed").toBool()) << '\n';
        return fail("Returning from Edit should restore a paused Source decoded frame at its saved position after decoder release");
      }
      if (!wait_for_condition([&] { return timeline_playback.mediaReady() &&
              !timeline_playback.processedFrameSource().isEmpty(); }, 5000) || timeline_document.sequencePlaying())
        return fail("Returning to Timeline should show a decoded picture and remain paused");
      if (!capture_workflow(QStringLiteral("10a-restored-source"),
                            {QStringLiteral("timelineSourceVideoOutput"), QStringLiteral("timelineProgramMonitor")}))
        return fail("The verified restored Source and Program frames should be visible");
      // Snapshot restoration evidence before save/reopen and the Scene proof
      // intentionally replace the document and can evict these QML objects.
      const auto restored_source_player_position_ms =
          resumed_source_player->property("position").toLongLong();
      const auto restored_source_preview_seconds =
          resumed_source_panel->property("previewSeconds").toDouble();
      const QJsonObject source_eviction_state_snapshot{
          {QStringLiteral("hiddenElapsedMs"), source_eviction_elapsed_ms},
          {QStringLiteral("panelDestroyed"), source_panel_lifetime.isNull()},
          {QStringLiteral("decoderDestroyed"), source_player_lifetime.isNull()},
          {QStringLiteral("sessionPathPreserved"), timeline_document.savedDocumentPath() == source_session_path},
          {QStringLiteral("playheadPreserved"), std::abs(timeline_document.sequencePlayheadSeconds() - hidden_playhead) <= 0.0001},
          {QStringLiteral("sourceClipPreserved"), timeline_document.sourceClip().value(QStringLiteral("assetId")).toString() == QStringLiteral("source-resume-red-blue")},
          {QStringLiteral("documentHashPreserved"), timeline_document.deterministicDocumentHash() == hidden_document_hash},
          {QStringLiteral("selectedClipIdsPreserved"), timeline_document.selectedClipIds() == hidden_selected_clip_ids},
          {QStringLiteral("undoStatePreserved"), timeline_document.canUndo() == hidden_can_undo},
          {QStringLiteral("blueFrameRestored"), source_resume_ready},
      };
      if (!timeline_document.saveTimeline() || !wait_for_condition([&] { return !timeline_document.timelineSaveInProgress(); }, 10000)) {
        return fail("Edited sequence should save");
      }
      const auto edited_document_path = timeline_document.savedDocumentPath();
      timeline_document.closeTimeline();
      if (!timeline_document.loadTimeline(edited_document_path) || clip_by_id(opening_id).isEmpty() || clip_by_id(added_id).isEmpty() ||
          std::abs(clip_by_id(opening_id).value(QStringLiteral("trimmedDurationSeconds")).toDouble() - trimmed_duration) > 0.04 ||
          std::abs(clip_by_id(opening_id).value(QStringLiteral("startSeconds")).toDouble() - saved_opening_start) > 0.04) {
        return fail("Adding, trimming, and moving should survive save and reopen");
      }
      timeline_document.addSequenceMarker(QStringLiteral("Recent recovery proof"));
      // Close immediately, before the debounce timer has checkpointed this edit.
      timeline_document.closeTimeline();
      if (!wait_for_condition([&] {
            return live_item(QStringLiteral("timelineSetupSurface")) &&
                   !timeline_document.recentTimelinesLoading() &&
                   live_item(QStringLiteral("timelineRecentDocumentRow0"));
          }, 10000))
        return fail("Timeline setup should list recoverable work in Recents");
      if (!capture_workflow(QStringLiteral("11-recovery-in-recents"),
                           {QStringLiteral("timelineSetupSurface"), QStringLiteral("timelineRecentDocumentRow0")}))
        return fail("Recovery rows should be visible in Recent Timelines");
      if (!click_named(QStringLiteral("timelineRecentDocumentRow0")) || !wait_for_condition([&] {
            return timeline_document.hasActiveTimeline() && timeline_document.dirty() &&
                   timeline_document.sequenceMarkers().size() > 0;
          }, 10000))
        return fail("Activating a Recent recovery should reopen unsaved work");
      const auto scene_status = run_scene_camera_workflow();
      if (scene_status != EXIT_SUCCESS) return scene_status;
      QFile manifest_file(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
      if (!manifest_file.open(QIODevice::WriteOnly)) return fail("Workflow proof should write its captures");
      manifest_file.write(QJsonDocument(studio_manifest).toJson(QJsonDocument::Indented));
      QFile receipt_file(QDir(proof_dir).filePath(QStringLiteral("workflow.json")));
      if (!receipt_file.open(QIODevice::WriteOnly)) return fail("Workflow proof should write its receipt");
      receipt_file.write(QJsonDocument(QJsonObject{
          {QStringLiteral("pointerAddToTimeline"), true}, {QStringLiteral("previewDoesNotEditSequence"), true},
          {QStringLiteral("pointerEdgeTrim"), true}, {QStringLiteral("pointerClipMove"), true},
          {QStringLiteral("linkedAudioPreserved"), linked_audio_verified}, {QStringLiteral("gestureUndoRedo"), true},
          {QStringLiteral("gestureCancellation"), true},
          {QStringLiteral("visibleEditModeMenu"), true},
          {QStringLiteral("precisionTrimMenu"), true}, {QStringLiteral("precisionTrimOneAndFiveFrames"), true},
          {QStringLiteral("precisionTrimToPlayhead"), true}, {QStringLiteral("precisionTrimDisarmedOnSelect"), true},
          {QStringLiteral("silentAudioFixture"), true},
          {QStringLiteral("forwardPlaybackChangesPicture"), true},
          {QStringLiteral("saveRenderSeparated"), true}, {QStringLiteral("saveReopen"), true}, {QStringLiteral("decodedProgramFrames"), true},
          {QStringLiteral("sourcePosterAndDecodedPlayback"), true},
          {QStringLiteral("editSwitchMs"), edit_switch_ms},
          {QStringLiteral("hiddenTimelinePlaying"), hidden_timeline_playing},
          {QStringLiteral("hiddenPlayheadDelta"), hidden_playhead_delta},
          {QStringLiteral("hiddenFrameDelta"), hidden_frame_delta},
          {QStringLiteral("sourcePausedOnLeave"), true},
          {QStringLiteral("sourcePlayDuringSilentPreroll"), QJsonObject{
              {QStringLiteral("passed"), true},
              {QStringLiteral("requestedSeconds"), 0.75},
              {QStringLiteral("playbackStartSeconds"), source_playback_start_seconds},
              {QStringLiteral("playbackStartFrameGeneration"), source_playback_start_frame_generation},
              {QStringLiteral("firstPlayingFrameGeneration"), source_first_user_play_frame_generation},
              {QStringLiteral("firstPlayingFramePositionUs"), source_first_user_play_frame_position_us},
              {QStringLiteral("pauseRequestPositionUs"), source_pause_request_position_us},
              {QStringLiteral("pausedPositionUs"), source_paused_position_us},
              {QStringLiteral("firstPlayingFramePtsUs"), source_first_user_play_frame_pts_us},
              {QStringLiteral("audioDetachedDuringPreroll"), source_preroll_audio_detached},
              {QStringLiteral("audioRestoredAtFirstPlayingFrame"),
               source_first_user_play_frame_audio_restored},
              {QStringLiteral("firstPlayingFramePositionEstimated"),
               source_first_user_play_frame_pts_us < 0},
              {QStringLiteral("firstPlayingFramePixelRgb"), QJsonObject{
                  {QStringLiteral("red"), source_first_user_play_frame_pixel.red()},
                  {QStringLiteral("green"), source_first_user_play_frame_pixel.green()},
                  {QStringLiteral("blue"), source_first_user_play_frame_pixel.blue()}}},
              {QStringLiteral("bluePixelReferenceMatched"), true}}},
          {QStringLiteral("sourceDockEviction"), source_eviction_state_snapshot},
          {QStringLiteral("sourcePausedPositionRestore"), QJsonObject{
              {QStringLiteral("passed"), true},
              {QStringLiteral("requestedSeconds"), source_resume_target_seconds},
              {QStringLiteral("playerPositionMs"), restored_source_player_position_ms},
              {QStringLiteral("panelPreviewSeconds"), restored_source_preview_seconds},
              {QStringLiteral("completedPositionSeconds"), source_resume_completed_position_seconds},
              {QStringLiteral("sinkFrameValid"), true},
              {QStringLiteral("postSeekFrameGeneration"), source_resume_frame_generation},
              {QStringLiteral("seekBaseFrameGeneration"), source_resume_seek_frame_generation},
              {QStringLiteral("framePtsUs"), source_resume_frame_pts_us},
              {QStringLiteral("positionMatchEstimated"), source_resume_frame_pts_us < 0},
              {QStringLiteral("bluePixelReferenceMatched"), true},
              {QStringLiteral("initialFramePtsUs"), source_initial_frame_pts_us},
              {QStringLiteral("initialFramePositionEstimated"), source_initial_frame_pts_us < 0},
              {QStringLiteral("pixelRgb"), QJsonObject{
                  {QStringLiteral("red"), source_resume_frame_pixel.red()},
                  {QStringLiteral("green"), source_resume_frame_pixel.green()},
                  {QStringLiteral("blue"), source_resume_frame_pixel.blue()}}}}},
          {QStringLiteral("recoveryInRecents"), true},
          {QStringLiteral("recoveryOpenedFromRecents"), true},
          {QStringLiteral("sceneCameraNativeWorkflow"), scene_camera_proof},
          {QStringLiteral("scenePointSelectionSourceSeconds"), scene_point_selection_source_seconds},
          {QStringLiteral("sceneCameraTimingEstimated"), scene_camera_timing_estimated},
          {QStringLiteral("scenePreviewExactSeekPixels"), scene_camera_proof},
          {QStringLiteral("scenePreviewPlaybackPixels"), scene_camera_proof},
          {QStringLiteral("sceneAudioTransportPlaying"), scene_camera_proof},
          {QStringLiteral("scenePreviewTrimmedSourceOrigin"), scene_camera_proof},
          {QStringLiteral("pausedSeek"), paused_seek_verified},
          {QStringLiteral("pausedSeekTargets"), paused_seek_targets},
          {QStringLiteral("overallStatus"), paused_seek_verified ? QStringLiteral("passed") : QStringLiteral("partial")},
          {QStringLiteral("frameSize"), QStringLiteral("%1x%2").arg(decoded.width()).arg(decoded.height())},
          {QStringLiteral("frameMetrics"), QJsonObject::fromVariantMap(decoded_frame_metrics)}
      }).toJson(QJsonDocument::Indented));
      if (!paused_seek_verified) return fail("Paused seeking should publish a fresh decoded frame with a changed picture");
      std::cout << "Pointer Add, preview-only Source, edge trim, linked move, gesture Undo/Redo, decoded Play/Pause/seek, and save/reopen passed.\n";
      return EXIT_SUCCESS;

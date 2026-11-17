  {
    app_shell.completeFirstRunExperience();

    const auto source_path = runtime_root.filePath(QStringLiteral("main-color-range-controls-source.png"));
    QImage source(480, 320, QImage::Format_RGB32);
    source.fill(QColor(QStringLiteral("#20242b")));
    for (int y = 48; y < 272; ++y) {
      auto* row = reinterpret_cast<QRgb*>(source.scanLine(y));
      for (int x = 24; x < 214; ++x) row[x] = qRgb(240, 32, 40);
      for (int x = 266; x < 456; ++x) row[x] = qRgb(36, 72, 240);
    }
    if (!source.save(source_path, "PNG"))
      return fail("The Main Color Range journey must create a real two-hue source image");

    const auto source_asset_id = QStringLiteral("main-color-range-controls-asset");
    catalog_assets.replaceAssetsForTesting({make_asset(
        source_asset_id, source_path, QUrl::fromLocalFile(source_path).toString())});
    catalog_assets.selectIndex(0);

    QStringList relevant_qml_warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine,
        [&relevant_qml_warnings](const QList<QQmlError>& warnings) {
          for (const auto& warning : warnings) {
            const auto description = warning.toString();
            if (description.contains(QStringLiteral("ReferenceError"), Qt::CaseInsensitive) ||
                description.contains(QStringLiteral("TypeError"), Qt::CaseInsensitive) ||
                description.contains(QStringLiteral("Cannot assign"), Qt::CaseInsensitive))
              relevant_qml_warnings.append(description);
          }
        });
    engine.load(QUrl::fromLocalFile(main_qml_path));
    QCoreApplication::processEvents();
    if (engine.rootObjects().isEmpty())
      return fail("The Main Color Range journey must load the production Main.qml root");
    QObject* root = engine.rootObjects().constFirst();
    auto* root_window = qobject_cast<QQuickWindow*>(root);
    if (root == nullptr || root_window == nullptr)
      return fail("The Main Color Range journey needs the actual exposed application window");
    root_window->resize(1440, 1100);
    root_window->show();
    const auto edit_workspace_index = root->property("editWorkspaceIndex").toInt();
    if (edit_workspace_index < 0 ||
        !root->setProperty("currentWorkspace", edit_workspace_index) ||
        !QMetaObject::invokeMethod(root, "loadSelectedAssetIntoEditor"))
      return fail("The Main Color Range journey must route the selected catalog photo into Edit");

    QObject* edit_workspace_host = nullptr;
    QObject* edit_workspace = nullptr;
    if (!wait_for_condition([&] {
          edit_workspace_host = find_named_object(root, QStringLiteral("editStudioWorkspace"));
          if (edit_workspace_host != nullptr) {
            edit_workspace = find_named_object(
                edit_workspace_host, QStringLiteral("editStudioWorkspaceContent"));
            if (edit_workspace == nullptr) edit_workspace = edit_workspace_host;
          }
          return edit_workspace_host != nullptr && edit_workspace != nullptr &&
              edit_workspace_host->property("pageContentReady").toBool() &&
              root_window->isExposed() && !root->property("workspaceTransitionActive").toBool() &&
              catalog_assets.selectedAssetId() == source_asset_id &&
              editor_preview.assetId() == source_asset_id && !editor_preview.processing() &&
              !editor_preview.displayPreviewSource().isEmpty();
        }, 15000))
      return fail("The Main Color Range journey must wait for the real selected Edit photo and viewport");

    int actual_wheel_scroll_events = 0;
    const auto wheel_scroll_item_into_view = [&](QQuickItem* target) {
      if (!item_visible(target) || target->window() != root_window) return false;
      const auto scroll_with_wheel = [&](QQuickItem* viewport_item, QQuickItem* flickable_item) {
        if (!item_visible(viewport_item) || flickable_item == nullptr ||
            flickable_item->metaObject()->indexOfProperty("contentY") < 0) return false;
        const auto viewport = scene_rect(viewport_item);
        if (viewport.isEmpty()) return false;
        const auto wait_until_scroll_settles = [&] {
          auto previous_y = flickable_item->property("contentY").toDouble();
          int stable_samples = 0;
          return wait_for_condition([&] {
            process_layout_events(1);
            const auto current_y = flickable_item->property("contentY").toDouble();
            stable_samples = std::abs(current_y - previous_y) <= 0.1
                ? stable_samples + 1 : 0;
            previous_y = current_y;
            return stable_samples >= 3 && !flickable_item->property("moving").toBool() &&
                !flickable_item->property("flicking").toBool();
          }, 3000);
        };
        const auto wheel_position = viewport.center();
        const auto wheel_global = QPointF(root_window->mapToGlobal(wheel_position.toPoint()));
        for (int step = 0; step < 32; ++step) {
          const auto target_bounds = scene_rect(target);
          if (viewport.adjusted(0.0, 2.0, 0.0, -2.0).contains(target_bounds))
            return wait_until_scroll_settles() &&
                viewport.adjusted(0.0, 2.0, 0.0, -2.0).contains(scene_rect(target));
          const bool scroll_down = target_bounds.bottom() > viewport.bottom();
          const QPoint pixel_delta(0, scroll_down ? -180 : 180);
          const QPoint angle_delta(0, scroll_down ? -120 : 120);
          const auto content_y_before = flickable_item->property("contentY").toDouble();
          QTest::mouseMove(root_window, wheel_position.toPoint());
          process_layout_events(2);
          QWheelEvent wheel(wheel_position, wheel_global, pixel_delta, angle_delta,
                            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
          QCoreApplication::sendEvent(root_window, &wheel);
          auto did_scroll = wait_for_condition([&] {
            process_layout_events(1);
            return std::abs(flickable_item->property("contentY").toDouble() - content_y_before) > 0.5;
          }, 750);
          if (!did_scroll) {
            const auto local_position = flickable_item->mapFromScene(wheel_position);
            QWheelEvent targeted_wheel(local_position, wheel_global, pixel_delta, angle_delta,
                                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(flickable_item, &targeted_wheel);
            did_scroll = wait_for_condition([&] {
              process_layout_events(1);
              return std::abs(flickable_item->property("contentY").toDouble() - content_y_before) > 0.5;
            }, 750);
          }
          if (!did_scroll) break;
          ++actual_wheel_scroll_events;
        }
        return wait_until_scroll_settles() &&
            viewport.adjusted(0.0, 2.0, 0.0, -2.0).contains(scene_rect(target));
      };
      for (auto* scroll = target->parentItem(); scroll != nullptr; scroll = scroll->parentItem()) {
        if (scroll->metaObject()->indexOfProperty("contentY") < 0 ||
            scroll->metaObject()->indexOfProperty("contentHeight") < 0) continue;
        if (scroll_with_wheel(scroll, scroll)) return true;
      }
      auto* tool_scroll = find_named_item(edit_workspace, QStringLiteral("creativeToolsScroll"));
      if (tool_scroll != nullptr) {
        auto* content_item = qobject_cast<QQuickItem*>(
            tool_scroll->property("contentItem").value<QObject*>());
        if (scroll_with_wheel(tool_scroll, content_item)) return true;
      }
      const auto target_bounds = scene_rect(target);
      const QRectF window_bounds(0.0, 0.0, root_window->width(), root_window->height());
      if (!window_bounds.contains(target_bounds)) return false;
      for (auto* parent = target->parentItem(); parent != nullptr; parent = parent->parentItem()) {
        if (parent->clip() && !scene_rect(parent).contains(target_bounds)) return false;
      }
      return true;
    };
    const auto click_item = [&](QQuickItem* item) {
      if (!item_visible(item) || !item->isEnabled() || item->window() != root_window ||
          !wheel_scroll_item_into_view(item))
        return false;
      const auto point = scene_rect(item).center().toPoint();
      if (!QRect(0, 0, root_window->width(), root_window->height()).contains(point))
        return false;
      QTest::mouseClick(root_window, Qt::LeftButton, Qt::NoModifier, point);
      process_layout_events(4);
      return true;
    };
    const auto click_canvas = [&](const double normalized_x, const double normalized_y) {
      auto* canvas = find_named_item(edit_workspace, QStringLiteral("editImageViewport"));
      if (!item_visible(canvas) || canvas->window() != root_window)
        return false;
      const auto content_x = canvas->property("paintedContentX").toDouble();
      const auto content_y = canvas->property("paintedContentY").toDouble();
      const auto content_width = canvas->property("paintedContentWidth").toDouble();
      const auto content_height = canvas->property("paintedContentHeight").toDouble();
      if (content_width <= 1.0 || content_height <= 1.0)
        return false;
      const QPointF local(content_x + content_width * normalized_x,
                          content_y + content_height * normalized_y);
      const auto point = canvas->mapToScene(local).toPoint();
      if (!QRect(0, 0, root_window->width(), root_window->height()).contains(point))
        return false;
      QTest::mouseClick(root_window, Qt::LeftButton, Qt::NoModifier, point);
      process_layout_events(4);
      return true;
    };
    const auto displayed_artwork_pixel = [&]() {
      auto* canvas = find_named_item(edit_workspace, QStringLiteral("editImageViewport"));
      const auto frame = root_window->grabWindow();
      if (!item_visible(canvas) || frame.isNull()) return QColor{};
      const auto content_x = canvas->property("paintedContentX").toDouble();
      const auto content_y = canvas->property("paintedContentY").toDouble();
      const auto content_width = canvas->property("paintedContentWidth").toDouble();
      const auto content_height = canvas->property("paintedContentHeight").toDouble();
      const auto logical_point = canvas->mapToScene(QPointF(
          content_x + content_width * 0.24, content_y + content_height * 0.5));
      const auto ratio = frame.devicePixelRatio();
      return frame.pixelColor(qBound(0, qRound(logical_point.x() * ratio), frame.width() - 1),
                              qBound(0, qRound(logical_point.y() * ratio), frame.height() - 1));
    };
    const auto settle_preview = [&] {
      return wait_for_condition([&] {
        return !editor_preview.processing() && !editor_preview.fullRenderRunning() &&
            !editor_preview.selectionWorkBusy() &&
            !editor_preview.displayPreviewSource().isEmpty();
      }, 15000);
    };

    const QString original_recipe = editor_preview.currentAssetBoundAdjustmentPatchJson();
    const auto original_mask_count = editor_preview.maskCount();
    const auto original_layer_count = editor_preview.photoLayerCount();
    const auto original_mask_snapshot = editor_preview.maskSetSnapshot();
    const auto original_mask_stack_snapshot = editor_preview.maskStack();
    const auto original_layer_snapshot = editor_preview.photoLayerSnapshot();
    const auto original_history_index = editor_preview.editHistoryCurrentIndex();
    const auto original_history_entries = editor_preview.editHistoryEntries().size();
    const auto original_can_undo = editor_preview.canUndo();
    const auto original_can_redo = editor_preview.canRedo();
    if (original_recipe.isEmpty())
      return fail("The Main Color Range journey must capture its original recipe");

    auto* selection_rail = find_named_item(edit_workspace, QStringLiteral("editRailSelectionButton"));
    if (!click_item(selection_rail) || !wait_for_condition([&] {
          return item_visible(find_named_item(edit_workspace, QStringLiteral("selectionToolbox")));
        }, 5000))
      return fail("The Main Color Range journey must enter the production Selection tool rail");
    QColor original_displayed_pixel;
    const bool original_frame_presented = wait_for_condition([&] {
      process_layout_events(2);
      original_displayed_pixel = displayed_artwork_pixel();
      return original_displayed_pixel.isValid() &&
          original_displayed_pixel.red() > original_displayed_pixel.green() + 50 &&
          original_displayed_pixel.red() > original_displayed_pixel.blue() + 50;
    }, 5000);
    if (!original_frame_presented) {
      std::cerr << "main-color-range-baseline-frame-diagnostic"
                << " pixelValid=" << original_displayed_pixel.isValid()
                << " pixel=" << original_displayed_pixel.red() << ','
                << original_displayed_pixel.green() << ','
                << original_displayed_pixel.blue() << ',' << original_displayed_pixel.alpha()
                << " previewSource=" << editor_preview.displayPreviewSource().toStdString()
                << " previewSourceAvailable=" << !editor_preview.displayPreviewSource().isEmpty()
                << " processing=" << editor_preview.processing()
                << " fullRenderRunning=" << editor_preview.fullRenderRunning() << '\n';
      return fail("The Main Color Range journey must wait for the known red artwork pixel to be presented before authoring");
    }
    auto* more_tools = find_named_item(edit_workspace, QStringLiteral("selectionAllTools"));
    auto* more_tools_header = find_named_item(edit_workspace, QStringLiteral("selectionAllToolsHeader"));
    if (more_tools == nullptr || more_tools_header == nullptr)
      return fail("Selection More tools must be present in the mounted Main workspace");
    if (!more_tools->property("expanded").toBool() && !click_item(more_tools_header))
      return fail("The production More tools header must open to reveal Color Range");
    if (!wait_for_condition([&] {
          return more_tools->property("expanded").toBool() &&
              item_visible(find_named_item(edit_workspace, QStringLiteral("selectionSource_color")));
        }, 5000))
      return fail("The Color Range source must be reachable from Selection More tools");

    auto* color_source_tile = find_named_item(edit_workspace, QStringLiteral("selectionSource_color"));
    auto* selection_toolbox = find_named_item(edit_workspace, QStringLiteral("selectionToolbox"));
    QSignalSpy color_tile_clicks(color_source_tile, SIGNAL(clicked()));
    QSignalSpy color_tool_requests(selection_toolbox, SIGNAL(toolRequested(QString)));
    const auto color_tile_clicked = click_item(color_source_tile);
    const auto color_source_ready = color_tile_clicked && wait_for_condition([&] {
      auto* range_controls = find_named_item(edit_workspace, QStringLiteral("selectionRangeControls"));
      auto* sample_button = find_named_item(edit_workspace, QStringLiteral("selectionRangeSampleButton"));
      auto* tools_scroll = find_named_item(edit_workspace, QStringLiteral("creativeToolsScroll"));
      auto* tools_flickable = tools_scroll ? qobject_cast<QQuickItem*>(
          tools_scroll->property("contentItem").value<QObject*>()) : nullptr;
      const auto scroll_viewport = tools_scroll ? scene_rect(tools_scroll) : QRectF{};
      const auto sample_bounds = sample_button ? scene_rect(sample_button) : QRectF{};
      return editor_preview.selectionEditSessionActive() &&
          editor_preview.activeMaskKind() == QStringLiteral("color") &&
          edit_workspace->property("maskColorRangeSampling").toBool() &&
          item_visible(range_controls) && item_visible(sample_button) && tools_flickable != nullptr &&
          std::abs(tools_flickable->property("contentY").toDouble()) <= 0.5 &&
          scroll_viewport.adjusted(-1.0, -1.0, 1.0, 1.0).contains(sample_bounds);
    }, 10000);
    if (!color_source_ready) {
      const auto bounds = color_source_tile ? scene_rect(color_source_tile) : QRectF{};
      auto* hit = root_window->contentItem();
      for (int depth = 0; hit != nullptr && depth < 32; ++depth) {
        auto* child = hit->childAt(hit->mapFromScene(bounds.center()).x(),
                                   hit->mapFromScene(bounds.center()).y());
        if (child == nullptr || child == hit) break;
        hit = child;
      }
      const auto proof_dir = qEnvironmentVariableIsSet("PHOTO_EDITOR_VISUAL_PROOF_DIR")
          ? QString::fromUtf8(qgetenv("PHOTO_EDITOR_VISUAL_PROOF_DIR"))
          : runtime_root.path();
      QDir().mkpath(proof_dir);
      const auto screenshot_path = QDir(proof_dir).filePath(
          QStringLiteral("main-color-range-source-create-failure.png"));
      const auto screenshot_saved = root_window->grabWindow().save(screenshot_path, "PNG");
      auto* range_controls = find_named_item(edit_workspace, QStringLiteral("selectionRangeControls"));
      QStringList parent_chain;
      for (auto* ancestor = color_source_tile ? color_source_tile->parentItem() : nullptr;
           ancestor != nullptr; ancestor = ancestor->parentItem()) {
        QString line = QStringLiteral("%1:%2 clip=%3 size=%4x%5")
            .arg(ancestor->objectName().isEmpty() ? QStringLiteral("<unnamed>") : ancestor->objectName())
            .arg(QString::fromLatin1(ancestor->metaObject()->className()))
            .arg(ancestor->clip())
            .arg(ancestor->width()).arg(ancestor->height());
        if (ancestor->metaObject()->indexOfProperty("contentY") >= 0)
          line += QStringLiteral(" contentY=%1 contentHeight=%2")
              .arg(ancestor->property("contentY").toDouble())
              .arg(ancestor->property("contentHeight").toDouble());
        parent_chain.append(line);
      }
      auto* tools_scroll = find_named_item(edit_workspace, QStringLiteral("creativeToolsScroll"));
      auto* tools_content = tools_scroll ? qobject_cast<QQuickItem*>(
          tools_scroll->property("contentItem").value<QObject*>()) : nullptr;
      std::cerr << "main-color-range-create-diagnostic"
                << " click=" << color_tile_clicked
                << " clickedSpyValid=" << color_tile_clicks.isValid()
                << " clickedCount=" << color_tile_clicks.count()
                << " requestSpyValid=" << color_tool_requests.isValid()
                << " requestCount=" << color_tool_requests.count()
                << " requestedKey=" << (color_tool_requests.count() > 0
                    ? color_tool_requests.constFirst().constFirst().toString().toStdString() : "none")
                << " item=" << (color_source_tile ? color_source_tile->objectName().toStdString() : "missing")
                << " tileVisible=" << (color_source_tile && color_source_tile->isVisible())
                << " tileEnabled=" << (color_source_tile && color_source_tile->isEnabled())
                << " tileSize=" << (color_source_tile ? color_source_tile->width() : 0) << 'x'
                << (color_source_tile ? color_source_tile->height() : 0)
                << " tileBounds=" << bounds.x() << ',' << bounds.y() << ',' << bounds.width() << ',' << bounds.height()
                << " hit=" << (hit ? hit->objectName().toStdString() : "none")
                << " hitClass=" << (hit ? hit->metaObject()->className() : "none")
                << " expanded=" << more_tools->property("expanded").toBool()
                << " toolboxVisible=" << item_visible(selection_toolbox)
                << " scrollView=" << (tools_scroll
                    ? QStringLiteral("%1,%2 %3x%4").arg(scene_rect(tools_scroll).x())
                          .arg(scene_rect(tools_scroll).y()).arg(tools_scroll->width())
                          .arg(tools_scroll->height()).toStdString()
                    : "missing")
                << " scrollContent=" << (tools_content ? QString::fromLatin1(tools_content->metaObject()->className()).toStdString() : "missing")
                << " contentY=" << (tools_content ? tools_content->property("contentY").toDouble() : -1.0)
                << " parentChain=" << parent_chain.join(QStringLiteral(" -> ")).toStdString()
                << " editTool=" << edit_workspace->property("editTool").toString().toStdString()
                << " session=" << editor_preview.selectionEditSessionActive()
                << " activeMask=" << editor_preview.activeMaskKind().toStdString()
                << " maskCount=" << editor_preview.maskCount()
                << " sampleArmed=" << edit_workspace->property("maskColorRangeSampling").toBool()
                << " controlsVisible=" << item_visible(range_controls)
                << " sampleButtonBounds=" << scene_rect(find_named_item(
                    edit_workspace, QStringLiteral("selectionRangeSampleButton"))).x() << ','
                << scene_rect(find_named_item(
                    edit_workspace, QStringLiteral("selectionRangeSampleButton"))).y()
                << " inScrollViewport=" << (tools_scroll && scene_rect(tools_scroll).contains(
                    scene_rect(find_named_item(edit_workspace, QStringLiteral("selectionRangeSampleButton")))))
                << " scrollContentY=" << (tools_content ? tools_content->property("contentY").toDouble() : -1.0)
                << " status=" << editor_preview.status().toStdString()
                << " screenshot=" << screenshot_path.toStdString()
                << " saved=" << screenshot_saved << '\n';
      return fail("Creating Color Range in Main must create its source and automatically arm canvas sampling");
    }
    const auto authored_mask_count = editor_preview.maskCount();
    const auto authored_layer_count = editor_preview.photoLayerCount();
    const auto authored_history_index = editor_preview.editHistoryCurrentIndex();
    const auto authored_samples = editor_preview.activeMaskColorSamples();
    if (authored_mask_count != original_mask_count + 1 || !authored_samples.isEmpty())
      return fail("Color Range creation must add one editable source before any sampled colors");

    if (!click_canvas(0.24, 0.5) || !wait_for_condition([&] {
          return !edit_workspace->property("maskColorRangeSampling").toBool() &&
              editor_preview.activeMaskColorSamples().size() == 1;
        }, 10000))
      return fail("The actual Main canvas must sample the first color and leave sampling mode");
    const auto first_sample = editor_preview.activeMaskColorSamples().constFirst().toMap();
    if (first_sample.value(QStringLiteral("red")).toInt() < 200 ||
        first_sample.value(QStringLiteral("green")).toInt() > 90 ||
        first_sample.value(QStringLiteral("blue")).toInt() > 100)
      return fail("The first Main canvas pick must retain the red artwork pixel in its sampled color");
    auto* range_sample_button = find_named_item(edit_workspace, QStringLiteral("selectionRangeSampleButton"));
    if (!click_item(range_sample_button) || !wait_for_condition([&] {
          return edit_workspace->property("maskColorRangeSampling").toBool();
        }, 3000) || !click_canvas(0.76, 0.5) || !wait_for_condition([&] {
          return !edit_workspace->property("maskColorRangeSampling").toBool() &&
              editor_preview.activeMaskColorSamples().size() == 2;
        }, 10000))
      return fail("The visible Add color control and a second Main canvas pick must add a distinct sample");
    const auto two_samples = editor_preview.activeMaskColorSamples();
    const auto second_sample = two_samples.at(1).toMap();
    if (second_sample.value(QStringLiteral("blue")).toInt() < 200 ||
        second_sample.value(QStringLiteral("red")).toInt() > 100 ||
        editor_preview.maskCount() != authored_mask_count ||
        editor_preview.photoLayerCount() != authored_layer_count ||
        editor_preview.activeMaskKind() != QStringLiteral("color"))
      return fail("Adding a sampled hue must preserve the same Color Range source without adding masks or layers");
    const auto retained_proof_dir = qEnvironmentVariableIsSet("PHOTO_EDITOR_VISUAL_PROOF_DIR")
        ? QString::fromUtf8(qgetenv("PHOTO_EDITOR_VISUAL_PROOF_DIR")) : QString();
    QString after_samples_capture_path;
    if (!retained_proof_dir.isEmpty()) {
      if (!QDir().mkpath(retained_proof_dir))
        return fail("The requested Color Range proof directory must be available");
      after_samples_capture_path = QDir(retained_proof_dir).filePath(
          QStringLiteral("main-color-range-journey-after-two-samples.png"));
      const auto after_samples_capture = root_window->grabWindow();
      if (after_samples_capture.isNull() || !after_samples_capture.save(after_samples_capture_path, "PNG"))
        return fail("The actual Main window with both Color Range swatches must be captured");
    }

    auto* remove_first_sample = find_named_item(edit_workspace, QStringLiteral("selectionRangeRemoveSample0"));
    if (!click_item(remove_first_sample) || !wait_for_condition([&] {
          const auto remaining = editor_preview.activeMaskColorSamples();
          return remaining.size() == 1 && remaining.constFirst().toMap().value(QStringLiteral("blue")).toInt() > 200;
        }, 10000))
      return fail("Removing the first visible sample must leave the other hue in the same Color Range source");
    const auto remaining_sample_count = editor_preview.activeMaskColorSamples().size();
    if (!click_item(range_sample_button) || !wait_for_condition([&] {
          return edit_workspace->property("maskColorRangeSampling").toBool();
        }, 3000) || !click_item(range_sample_button) || !wait_for_condition([&] {
          return !edit_workspace->property("maskColorRangeSampling").toBool();
        }, 3000) || editor_preview.activeMaskColorSamples().size() != remaining_sample_count ||
        editor_preview.maskCount() != authored_mask_count ||
        editor_preview.photoLayerCount() != authored_layer_count)
      return fail("Cancel sampling must leave the authored sample and source, mask count, and layer stack unchanged");

    auto* cancel_workflow = find_named_item(edit_workspace, QStringLiteral("maskRefinementCancelButton"));
    if (!click_item(cancel_workflow) || !wait_for_condition([&] {
          return !editor_preview.selectionEditSessionActive() && !editor_preview.processing() &&
              editor_preview.maskCount() == original_mask_count &&
              editor_preview.photoLayerCount() == original_layer_count;
        }, 15000) || !settle_preview())
      return fail("Canceling the Main selection workflow must roll back its temporary Color Range source");
    QColor final_displayed_pixel;
    const bool cancel_frame_presented = wait_for_condition([&] {
      process_layout_events(2);
      final_displayed_pixel = displayed_artwork_pixel();
      return final_displayed_pixel.isValid() && final_displayed_pixel == original_displayed_pixel;
    }, 5000);
    const bool recipe_restored = editor_preview.currentAssetBoundAdjustmentPatchJson() == original_recipe;
    const bool history_restored = editor_preview.editHistoryCurrentIndex() == original_history_index &&
        editor_preview.editHistoryEntries().size() == original_history_entries &&
        editor_preview.canUndo() == original_can_undo && editor_preview.canRedo() == original_can_redo;
    const bool preview_pixel_restored = final_displayed_pixel.isValid() &&
        final_displayed_pixel == original_displayed_pixel;
    const bool asset_restored = editor_preview.assetId() == source_asset_id;
    const bool history_order_valid = authored_history_index >= original_history_index;
    if (!relevant_qml_warnings.isEmpty()) {
      for (const auto& warning : relevant_qml_warnings)
        std::cerr << "Main Color Range runtime QML error: " << warning.toStdString() << '\n';
      return fail("The Main Color Range workflow must complete without QML ReferenceError, TypeError, or assignment warnings");
    }
    if (!recipe_restored || !history_restored || !preview_pixel_restored ||
        !asset_restored || !history_order_valid) {
      const auto describe_pixel = [](const QColor& color) {
        return QStringLiteral("%1,%2,%3,%4")
            .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
      };
      const auto current_recipe = editor_preview.currentAssetBoundAdjustmentPatchJson();
      std::cerr << "main-color-range-cancel-diagnostic"
                << " recipeRestored=" << recipe_restored
                << " originalRecipeBytes=" << original_recipe.size()
                << " currentRecipeBytes=" << current_recipe.size()
                << " originalRecipeSha256=" << QCryptographicHash::hash(
                    original_recipe.toUtf8(), QCryptographicHash::Sha256).toHex().toStdString()
                << " currentRecipeSha256=" << QCryptographicHash::hash(
                    current_recipe.toUtf8(), QCryptographicHash::Sha256).toHex().toStdString()
                << " historyRestored=" << history_restored
                << " originalHistoryIndex=" << original_history_index
                << " currentHistoryIndex=" << editor_preview.editHistoryCurrentIndex()
                << " originalHistoryEntries=" << original_history_entries
                << " currentHistoryEntries=" << editor_preview.editHistoryEntries().size()
                << " originalCanUndo=" << original_can_undo
                << " currentCanUndo=" << editor_preview.canUndo()
                << " originalCanRedo=" << original_can_redo
                << " currentCanRedo=" << editor_preview.canRedo()
                << " originalPixelValid=" << original_displayed_pixel.isValid()
                << " originalPixel=" << describe_pixel(original_displayed_pixel).toStdString()
                << " finalPixelValid=" << final_displayed_pixel.isValid()
                << " finalPixel=" << describe_pixel(final_displayed_pixel).toStdString()
                << " baselineFramePresented=" << original_frame_presented
                << " cancelFrameMatched=" << cancel_frame_presented
                << " previewPixelRestored=" << preview_pixel_restored
                << " sourceAssetId=" << source_asset_id.toStdString()
                << " currentAssetId=" << editor_preview.assetId().toStdString()
                << " assetRestored=" << asset_restored
                << " authoredHistoryIndex=" << authored_history_index
                << " historyOrderValid=" << history_order_valid
                << " masks=" << editor_preview.maskCount()
                << " layers=" << editor_preview.photoLayerCount()
                << " originalMaskSets=" << QJsonDocument::fromVariant(
                    QVariant(original_mask_snapshot)).toJson(QJsonDocument::Compact).toStdString()
                << " originalMaskStack=" << QJsonDocument::fromVariant(
                    QVariant(original_mask_stack_snapshot)).toJson(QJsonDocument::Compact).toStdString()
                << " currentMaskSets=" << QJsonDocument::fromVariant(
                    QVariant(editor_preview.maskSetSnapshot())).toJson(QJsonDocument::Compact).toStdString()
                << " currentMaskStack=" << QJsonDocument::fromVariant(
                    QVariant(editor_preview.maskStack())).toJson(QJsonDocument::Compact).toStdString()
                << " originalLayers=" << QJsonDocument::fromVariant(
                    QVariant(original_layer_snapshot)).toJson(QJsonDocument::Compact).toStdString()
                << " currentLayers=" << QJsonDocument::fromVariant(
                    QVariant(editor_preview.photoLayerSnapshot())).toJson(QJsonDocument::Compact).toStdString()
                << " activeMask=" << QJsonDocument::fromVariant(
                    QVariant(editor_preview.maskStack().value(editor_preview.activeMaskIndex()).toMap())).toJson(QJsonDocument::Compact).toStdString()
                << " activeMaskIndex=" << editor_preview.activeMaskIndex()
                << " activeMaskKind=" << editor_preview.activeMaskKind().toStdString()
                << " maskEditLayerId=" << editor_preview.maskEditLayerId().toStdString()
                << " previewSource=" << editor_preview.displayPreviewSource().toStdString() << '\n';
      return fail("Canceled Color Range authoring must restore the original recipe, rendered canvas pixels, and history position");
    }

    QString after_cancel_capture_path;
    if (!retained_proof_dir.isEmpty()) {
      after_cancel_capture_path = QDir(retained_proof_dir).filePath(
          QStringLiteral("main-color-range-journey-after-cancel.png"));
      const auto after_cancel_capture = root_window->grabWindow();
      if (after_cancel_capture.isNull() || !after_cancel_capture.save(after_cancel_capture_path, "PNG"))
        return fail("The actual Main window after selection cancellation must be captured");
      QJsonObject receipt{
          {QStringLiteral("schema"), QStringLiteral("looksy.main-color-range-journey.v1")},
          {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
          {QStringLiteral("qtQuickBackend"), qEnvironmentVariable("QT_QUICK_BACKEND", QStringLiteral("platform-default"))},
          {QStringLiteral("pointerInput"), QStringLiteral("QTest clicks and QWheelEvent on the actual QQuickWindow")},
          {QStringLiteral("runtimeQmlErrors"), relevant_qml_warnings.size()},
          {QStringLiteral("sourceAssetId"), source_asset_id},
          {QStringLiteral("autoScrolledColorToolIntoView"), actual_wheel_scroll_events > 0},
          {QStringLiteral("wheelScrollEvents"), actual_wheel_scroll_events},
          {QStringLiteral("colorSourceAutoArmedSampling"), true},
          {QStringLiteral("sampleHexes"), QJsonArray{
              first_sample.value(QStringLiteral("hex")).toString(),
              second_sample.value(QStringLiteral("hex")).toString()}},
          {QStringLiteral("removedSamples"), 1},
          {QStringLiteral("samplingCancelPreservedRemainingSample"), true},
          {QStringLiteral("workflowCancelRestoredRecipe"), recipe_restored},
          {QStringLiteral("historyPositionRestored"), history_restored},
          {QStringLiteral("renderedCanvasPixelRestored"), preview_pixel_restored},
          {QStringLiteral("afterTwoSamplesCapture"), after_samples_capture_path},
          {QStringLiteral("afterCancelCapture"), after_cancel_capture_path}};
      QSaveFile receipt_file(QDir(retained_proof_dir).filePath(
          QStringLiteral("main-color-range-journey-receipt.json")));
      if (!receipt_file.open(QIODevice::WriteOnly) ||
          receipt_file.write(QJsonDocument(receipt).toJson(QJsonDocument::Indented)) < 0 ||
          !receipt_file.commit())
        return fail("The successful Main Color Range journey must publish its compact JSON receipt");
    }

    std::cout << "main-color-range-controls: canvas-picks=2 remove=1 sampling-cancel=true "
                 "workflow-cancel=restored preview=equal\n";
    return EXIT_SUCCESS;
  }

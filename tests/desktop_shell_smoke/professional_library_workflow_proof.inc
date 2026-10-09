  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_PROFESSIONAL_LIBRARY")) {
    using photo_editor::desktop::CatalogAssetItem;
    using photo_editor::desktop::LibraryWorkflowController;

    QVector<CatalogAssetItem> library_fixtures;
    const QStringList fixture_names{
        QStringLiteral("alpha-photo.png"),
        QStringLiteral("bravo-photo.png"),
        QStringLiteral("charlie-photo.png")};
    const QStringList fixture_ids{
        QStringLiteral("professional-library-alpha"),
        QStringLiteral("professional-library-bravo"),
        QStringLiteral("professional-library-charlie")};
    for (int index = 0; index < fixture_names.size(); ++index) {
      const auto path = runtime_root.filePath(fixture_names.at(index));
      QImage image(960, 640, QImage::Format_RGB32);
      image.fill(index == 0 ? QColor(QStringLiteral("#7892a8"))
                            : index == 1 ? QColor(QStringLiteral("#c78a5e"))
                                         : QColor(QStringLiteral("#718c6b")));
      if (!image.save(path)) return fail("Professional Library proof should create three photo fixtures");
      library_fixtures.push_back(make_asset(
          fixture_ids.at(index), path, QUrl::fromLocalFile(path).toString()));
    }

    catalog_assets.setMediaCollectionFilter(QStringLiteral("images"));
    catalog_assets.setMediaTypeFilter(QStringLiteral("all"));
    catalog_assets.setReviewFilter(QStringLiteral("all"));
    catalog_assets.setMinimumRatingFilter(0);
    catalog_assets.replaceAssetsForTesting(library_fixtures);
    if (catalog_assets.assetCount() != 3) return fail("Professional Library fixture should contain exactly three photos");
    const auto selected_asset_id = fixture_ids.at(1);
    catalog_assets.selectIndexWithModifiers(1, 0, false);
    if (catalog_assets.selectedAssetId() != selected_asset_id)
      return fail("Professional Library fixture should select the intended metadata-edit photo");

    // The real project command writes its recoverable local membership outbox.
    // This proof deliberately does not claim that the disposable database sync succeeded.
    catalog_assets.createProjectWithSelectedAssets(QStringLiteral("Nepal Trip"));
    QFile project_outbox(runtime_root.filePath(QStringLiteral("project-membership-outbox.json")));
    if (!project_outbox.open(QIODevice::ReadOnly))
      return fail("Creating Nepal Trip should write the isolated fixture membership outbox");
    QJsonParseError project_parse_error{};
    const auto project_document = QJsonDocument::fromJson(project_outbox.readAll(), &project_parse_error);
    const auto project_members = project_document.object()
                                     .value(QStringLiteral("projects")).toObject()
                                     .value(QStringLiteral("Nepal Trip")).toObject();
    if (!project_document.isObject() || project_parse_error.error != QJsonParseError::NoError ||
        project_members.value(selected_asset_id).toBool() != true ||
        catalog_assets.projectNames().contains(QStringLiteral("Nepal Trip")) == false ||
        catalog_assets.assetCountForProject(QStringLiteral("Nepal Trip")) != 1) {
      return fail("Nepal Trip should contain the selected photo and retain its local membership intent");
    }

    // Start in All Images so the initial native capture shows the whole library.
    catalog_assets.clearFilters();
    catalog_assets.setMediaCollectionFilter(QStringLiteral("images"));
    catalog_assets.setSortOrder(QStringLiteral("imported-desc"));
    catalog_assets.selectIndex(1);

    // This UI fixture uses the deliberately nonexistent database configured
    // by the smoke runner. Suppress only that synthetic startup error in this
    // scoped fixture so it cannot cover the Library controls during capture.
    bool clearing_fixture_catalog_error = false;
    const auto clear_fixture_catalog_error = [&]() {
      if (clearing_fixture_catalog_error ||
          catalog_assets.status() != QStringLiteral("Catalog unavailable") ||
          catalog_assets.errorMessage().isEmpty()) {
        return;
      }
      clearing_fixture_catalog_error = true;
      catalog_assets.reportCatalogStartupFailure(QString{});
      clearing_fixture_catalog_error = false;
    };
    QObject fixture_catalog_error_scope;
    QObject::connect(
        &catalog_assets, &photo_editor::desktop::CatalogAssetModel::statusChanged,
        &fixture_catalog_error_scope, clear_fixture_catalog_error);
    QObject::connect(
        &catalog_assets, &photo_editor::desktop::CatalogAssetModel::errorMessageChanged,
        &fixture_catalog_error_scope, clear_fixture_catalog_error);

    engine.load(QUrl::fromLocalFile(main_qml_path));
    if (engine.rootObjects().isEmpty()) return fail("Professional Library proof should load Main.qml");
    auto* library_root = engine.rootObjects().constFirst();
    auto* library_window = qobject_cast<QQuickWindow*>(library_root);
    if (!library_window) return fail("Professional Library proof requires a native quick window");
    const auto catalog_recovery_hidden_for_fixture = [&]() {
      clear_fixture_catalog_error();
      process_layout_events(2);
      return !library_root->property("catalogRecoveryNeeded").toBool() &&
          !item_visible(find_named_item(library_root, QStringLiteral("recoverySummarySurface")));
    };
    const auto native_pointer_proof = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER");
    library_root->setProperty("currentWorkspace", library_root->property("libraryWorkspaceIndex"));
    if (!wait_for_condition([&]() {
          return item_visible(find_named_item(library_root, QStringLiteral("libraryImagesCollectionButton"))) &&
                 !library_root->property("workspaceTransitionActive").toBool();
        }, 15000)) {
      return fail("Professional Library proof should route to the Images library");
    }
    if (native_pointer_proof) {
      activate_native_test_application();
      library_window->raise();
      library_window->requestActivate();
      if (!wait_for_condition([&]() {
            if (!library_window->isActive()) {
              library_window->raise();
              library_window->requestActivate();
            }
            return library_window->isActive() && QGuiApplication::focusWindow() == library_window &&
                   QGuiApplication::applicationState() == Qt::ApplicationActive;
          }, 5000)) {
        return fail("Professional Library native window should be active before pointer interactions");
      }
    }

    const auto proof_dir = qEnvironmentVariable(
        "PHOTO_EDITOR_VISUAL_PROOF_DIR", runtime_root.filePath(QStringLiteral("professional-library-proof")));
    if (!QDir().mkpath(proof_dir)) return fail("Professional Library proof directory should be writable");
    if (!catalog_recovery_hidden_for_fixture()) {
      const auto title = find_named_object(library_root, QStringLiteral("recoverySummaryTitle"));
      std::cerr << "Professional Library recovery diagnostic: status="
                << catalog_assets.status().toStdString()
                << " error=" << catalog_assets.errorMessage().toStdString()
                << " catalogRecovery=" << library_root->property("catalogRecoveryNeeded").toBool()
                << " jobRecovery=" << library_root->property("jobRecoveryNeeded").toBool()
                << " surfaceVisible=" << library_root->property("recoverySurfaceVisible").toBool()
                << " summarySurfaceVisible="
                << item_visible(find_named_item(library_root, QStringLiteral("recoverySummarySurface")))
                << " sessionRecovery=" << app_shell.sessionRecoveryAvailable()
                << " jobReport=" << job_queue.hasRecoveryReport()
                << " repairPending=" << app_shell.catalogRepairConfirmationPending()
                << " title=" << (title ? title->property("text").toString().toStdString() : "") << '\n';
      return fail("Synthetic catalog recovery should not cover the isolated Library fixture");
    }
    QJsonArray library_manifest;
    const auto capture_library = [&](const QString& state, int width, int height,
                                     const QStringList& required) {
      if (!catalog_recovery_hidden_for_fixture() ||
          !capture_visual_proof(library_window, library_root, proof_dir, state,
                                width, height, required, &library_manifest)) {
        return false;
      }
      return catalog_recovery_hidden_for_fixture();
    };
    const auto find_visible_library_item = [&](const QString& object_name) -> QQuickItem* {
      const auto items = find_named_items(library_root, object_name);
      for (auto* item : items) {
        if (item_visible(item) && item->window()) return item;
      }
      return nullptr;
    };
    const auto log_library_controls = [&](const QString& object_name) {
      const auto items = find_named_items(library_root, object_name);
      std::cerr << "Library control " << object_name.toStdString() << " instances=" << items.size() << '\n';
      for (auto* item : items) {
        const auto rect = scene_rect(item);
        std::cerr << "  visible=" << item_visible(item)
                  << " enabled=" << item->property("enabled").toBool()
                  << " active=" << item->property("active").toBool()
                  << " rect=" << rect.x() << ',' << rect.y() << ' ' << rect.width() << 'x' << rect.height()
                  << " window=" << (item->window() ? item->window()->title().toStdString() : "none") << '\n';
      }
    };
    const auto library_drawer_is_fully_closed = [&]() {
      auto* drawer = find_named_object(library_root, QStringLiteral("libraryNavigationDrawer"));
      if (!drawer || drawer->property("opened").toBool() || drawer->property("visible").toBool() ||
          drawer->property("position").toDouble() > 0.001) return false;
      for (auto* child : drawer->findChildren<QObject*>()) {
        if (QByteArray(child->metaObject()->className()).contains("Loader") &&
            child->property("active").toBool()) return false;
      }
      return true;
    };
    const auto log_library_overlay_state = [&]() {
      auto* drawer = find_named_object(library_root, QStringLiteral("libraryNavigationDrawer"));
      std::cerr << "Library overlay state: drawerFound=" << (drawer != nullptr)
                << " opened=" << (drawer && drawer->property("opened").toBool())
                << " visible=" << (drawer && drawer->property("visible").toBool())
                << " position=" << (drawer ? drawer->property("position").toDouble() : -1.0) << '\n';
      if (drawer) {
        for (auto* child : drawer->findChildren<QObject*>()) {
          if (QByteArray(child->metaObject()->className()).contains("Loader")) {
            std::cerr << "  drawer loader class=" << child->metaObject()->className()
                      << " active=" << child->property("active").toBool()
                      << " visible=" << child->property("visible").toBool() << '\n';
          }
        }
      }
      for (auto* object : library_root->findChildren<QObject*>()) {
        const QByteArray class_name(object->metaObject()->className());
        if (class_name.contains("Popup") && object->property("visible").toBool()) {
          std::cerr << "  visible popup class=" << class_name.constData()
                    << " name=" << object->objectName().toStdString()
                    << " opened=" << object->property("opened").toBool() << '\n';
        }
      }
    };
    quint64 library_pointer_timestamp = 1000;
    const auto click_library_item = [&](QQuickItem* item) {
      auto* target_window = item ? item->window() : nullptr;
      if (!item_visible(item) || !target_window || !item->property("enabled").toBool()) return false;
      // Keep separate popup interactions from being interpreted as double clicks.
      target_window->grabWindow();
      const QPointF item_center(item->width() / 2.0, item->height() / 2.0);
      const auto point = item->mapToScene(item_center);
      auto* content_item = target_window->contentItem();
      if (!item->contains(item_center) || !content_item ||
          !content_item->contains(content_item->mapFromScene(point))) return false;
      if (native_pointer_proof) {
        click_native_test_window(target_window, point.toPoint());
        process_layout_events(3);
        return true;
      }
      library_pointer_timestamp += static_cast<quint64>(QApplication::doubleClickInterval() + 1);
      const auto send_pointer = [&](QEvent::Type type, const QPointF& local_point) {
        const auto global = QPointF(target_window->mapToGlobal(local_point.toPoint()));
        QMouseEvent event(type, local_point, global,
                          Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                          Qt::NoModifier);
        event.setTimestamp(library_pointer_timestamp);
        library_pointer_timestamp += 16;
        QCoreApplication::sendEvent(target_window, &event);
        process_layout_events(2);
      };
      send_pointer(QEvent::MouseButtonPress, point);
      send_pointer(QEvent::MouseButtonRelease, point);
      return true;
    };
    const auto activate_library_control = [&](const QString& object_name) {
      auto* item = find_visible_library_item(object_name);
      return native_pointer_proof ? click_library_item(item) : send_activation_key(item);
    };
    const auto send_library_key = [&](int key) {
      if (native_pointer_proof)
        key_native_test_window(library_window, key, Qt::NoModifier);
      else if (!send_window_key(library_window, key))
        return false;
      process_layout_events(3);
      return true;
    };
    const auto visual_child_at_diagnostic = [&](QQuickItem* item) {
      auto* target_window = item ? item->window() : nullptr;
      if (!item || !target_window) return QStringLiteral("target/window missing");
      const auto point = scene_rect(item).center();
      auto* hit_item = target_window->contentItem();
      QStringList path;
      for (int depth = 0; hit_item && depth < 32; ++depth) {
        const auto name = hit_item->objectName().isEmpty()
                              ? QString::fromLatin1(hit_item->metaObject()->className())
                              : hit_item->objectName();
        path.push_back(name);
        const auto local = hit_item->mapFromScene(point);
        auto* child = hit_item->childAt(local.x(), local.y());
        if (!child || child == hit_item) break;
        hit_item = child;
      }
      bool target_in_path = false;
      for (auto* ancestor = hit_item; ancestor; ancestor = ancestor->parentItem()) {
        if (ancestor == item) target_in_path = true;
      }
      return QStringLiteral("target=%1 enabled=%2 rect=%3,%4 %5x%6 hit=%7 path=%8")
          .arg(item->objectName())
          .arg(item->property("enabled").toBool())
          .arg(point.x()).arg(point.y())
          .arg(scene_rect(item).width()).arg(scene_rect(item).height())
          .arg(target_in_path)
          .arg(path.join(QStringLiteral(" -> ")));
    };
    const auto type_library_text = [&](const QString& object_name, const QString& text) {
      auto* field = find_named_item(library_root, object_name);
      if (!item_visible(field)) return false;
      const bool custom_focus_method = field->metaObject()->indexOfMethod("forceInputFocus()") >= 0 &&
                                       QMetaObject::invokeMethod(field, "forceInputFocus");
      if (!custom_focus_method) field->forceActiveFocus(Qt::OtherFocusReason);
      QCoreApplication::processEvents();
      auto* input_target = library_window->activeFocusItem();
      bool focus_matches = input_target == field;
      for (auto* ancestor = input_target ? input_target->parentItem() : nullptr;
           !focus_matches && ancestor; ancestor = ancestor->parentItem()) {
        focus_matches = ancestor == field;
      }
      if (!focus_matches) {
        std::cerr << "Library text focus mismatch: control=" << object_name.toStdString()
                  << " targetFocus=" << (input_target ? input_target->objectName().toStdString() : "none")
                  << " customFocusMethod=" << custom_focus_method << '\n';
        return false;
      }
      const auto select_all = QKeySequence(QKeySequence::SelectAll)[0];
      QKeyEvent select_press(QEvent::KeyPress, select_all.key(), select_all.keyboardModifiers());
      QKeyEvent select_release(QEvent::KeyRelease, select_all.key(), select_all.keyboardModifiers());
      QCoreApplication::sendEvent(input_target, &select_press);
      QCoreApplication::sendEvent(input_target, &select_release);
      QInputMethodEvent input;
      input.setCommitString(text);
      QCoreApplication::sendEvent(input_target, &input);
      process_layout_events(3);
      const auto actual_text = field->property("text").toString();
      if (actual_text != text) {
        std::cerr << "Library text input mismatch: control=" << object_name.toStdString()
                  << " expected=" << text.toStdString() << " actual=" << actual_text.toStdString() << '\n';
        return false;
      }
      return true;
    };
    const auto settle_library_scroll_layout = [&](QQuickItem* viewport) {
      if (!viewport) return false;
      library_window->grabWindow();
      auto previous_height = viewport->property("contentHeight").toDouble();
      int stable_samples = 0;
      const bool stable = wait_for_condition([&]() {
        const auto current_height = viewport->property("contentHeight").toDouble();
        if (qAbs(current_height - previous_height) <= 0.5)
          ++stable_samples;
        else
          stable_samples = 0;
        previous_height = current_height;
        return stable_samples >= 3;
      }, 2000);
      library_window->grabWindow();
      process_layout_events(3);
      return stable;
    };
    const auto scroll_library_item_into_view = [&](QQuickItem* item) {
      if (!item) return false;
      auto* ancestor = item->parentItem();
      while (ancestor) {
        if (ancestor->metaObject()->indexOfProperty("contentY") >= 0) {
          const bool trace_delivery_scroll =
              item->objectName() == QStringLiteral("libraryDeliveryExportSelectedButton");
          auto* content_item = ancestor->property("contentItem").value<QQuickItem*>();
          if (content_item) {
            const auto content_rect = item->mapRectToItem(
                content_item, QRectF(0.0, 0.0, item->width(), item->height()));
            const auto item_top = content_rect.top();
            const auto item_bottom = content_rect.bottom();
            const auto current_scroll = ancestor->property("contentY").toDouble();
            const auto viewport_height = ancestor->height();
            auto required_scroll = current_scroll;
            if (item_top < current_scroll)
              required_scroll = item_top;
            else if (item_bottom > current_scroll + viewport_height)
              required_scroll = item_bottom - viewport_height;
            required_scroll = qMax(0.0, required_scroll);
            ancestor->setProperty("contentY", required_scroll);
          } else {
            const auto item_position = item->mapToItem(ancestor, QPointF(0.0, 0.0));
            const auto current_scroll = ancestor->property("contentY").toDouble();
            const auto required_scroll = qMax(
                0.0, current_scroll + item_position.y() + item->height() - ancestor->height());
            ancestor->setProperty("contentY", required_scroll);
          }
          process_layout_events(6);
          if (!settle_library_scroll_layout(ancestor)) return false;
          if (trace_delivery_scroll) {
            const auto viewport_rect = scene_rect(ancestor);
            const auto item_rect = scene_rect(item);
            const auto current_scroll = ancestor->property("contentY").toDouble();
            const auto content_height = ancestor->property("contentHeight").toDouble();
            std::cerr << "Delivery scroll base: contentHeight=" << content_height
                      << " viewportHeight=" << ancestor->height()
                      << " currentY=" << current_scroll
                      << " maxY=" << qMax(0.0, content_height - ancestor->height())
                      << " itemY=" << item_rect.y() << " viewportY=" << viewport_rect.y()
                      << " rectFit=" << contains_with_tolerance(viewport_rect, item_rect, 0.5) << '\n';
          }
          // Layout changes and an earlier scroll can leave a requested contentY
          // short of the target's actual clipped viewport. Correct from the
          // rendered scene geometry, bounded to avoid chasing a capped Flickable.
          for (int correction = 0; correction < 4; ++correction) {
            const auto viewport_rect = scene_rect(ancestor);
            const auto item_rect = scene_rect(item);
            if (contains_with_tolerance(viewport_rect, item_rect, 0.5)) break;
            double vertical_overflow = 0.0;
            if (item_rect.top() < viewport_rect.top())
              vertical_overflow = item_rect.top() - viewport_rect.top();
            else if (item_rect.bottom() > viewport_rect.bottom())
              vertical_overflow = item_rect.bottom() - viewport_rect.bottom();
            if (qAbs(vertical_overflow) < 0.5) break;
            const QPointF scene_start(item_rect.center().x(), item_rect.bottom());
            const auto local_start = ancestor->mapFromScene(scene_start);
            const auto local_end = ancestor->mapFromScene(
                QPointF(scene_start.x(), scene_start.y() + vertical_overflow));
            const auto local_overflow = local_end.y() - local_start.y();
            const auto previous_item_y = item_rect.y();
            const auto previous_scroll = ancestor->property("contentY").toDouble();
            const auto corrected_scroll = qMax(
                0.0, previous_scroll + local_overflow);
            ancestor->setProperty("contentY", corrected_scroll);
            process_layout_events(4);
            if (!settle_library_scroll_layout(ancestor)) return false;
            if (trace_delivery_scroll) {
              const auto corrected_item_rect = scene_rect(item);
              const auto corrected_viewport_rect = scene_rect(ancestor);
              const auto content_height = ancestor->property("contentHeight").toDouble();
              std::cerr << "Delivery scroll correction=" << correction
                        << " sceneOverflow=" << vertical_overflow
                        << " localOverflow=" << local_overflow
                        << " priorY=" << previous_scroll
                        << " requestedY=" << corrected_scroll
                        << " currentY=" << ancestor->property("contentY").toDouble()
                        << " maxY=" << qMax(0.0, content_height - ancestor->height())
                        << " itemY=" << previous_item_y << "->" << corrected_item_rect.y()
                        << " fit=" << contains_with_tolerance(
                               corrected_viewport_rect, corrected_item_rect, 0.5) << '\n';
            }
          }
          return true;
        }
        ancestor = ancestor->parentItem();
      }
      return item_visible(item);
    };
    const auto library_scroll_viewport_for = [&](QQuickItem* item) -> QQuickItem* {
      for (auto* ancestor = item ? item->parentItem() : nullptr; ancestor;
           ancestor = ancestor->parentItem()) {
        if (ancestor->metaObject()->indexOfProperty("contentY") >= 0 &&
            ancestor->property("clip").toBool()) return ancestor;
      }
      return nullptr;
    };
    const auto select_layout_menu_item = [&](const QString& key) {
      auto* menu_button = find_named_item(library_root, QStringLiteral("libraryWorkspaceLayoutButton"));
      auto* menu_trigger = find_visible_library_item(QStringLiteral("libraryWorkspaceLayoutButtonPrimaryAction"));
      if (!menu_button || !item_visible(menu_trigger) || !menu_trigger->property("enabled").toBool()) {
        std::cerr << "Layout menu trigger is missing, hidden, or disabled: "
                  << visual_child_at_diagnostic(menu_trigger).toStdString() << '\n';
        return false;
      }
      if (!click_library_item(menu_trigger)) {
        std::cerr << "Layout trigger could not receive the native pointer click: "
                  << visual_child_at_diagnostic(menu_trigger).toStdString() << '\n';
        return false;
      }
      const auto popup_name = QStringLiteral("libraryWorkspaceLayoutButtonPopup");
      auto* popup = find_named_object(library_root, popup_name);
      if (!wait_for_condition([&]() { return popup && popup->property("visible").toBool(); }, 3000)) {
        std::cerr << "Layout popup did not open after native click: "
                  << visual_child_at_diagnostic(menu_trigger).toStdString() << '\n';
        return false;
      }
      const auto variant_list = [](const QVariant& value) {
        return value.canConvert<QJSValue>() ? value.value<QJSValue>().toVariant().toList() : value.toList();
      };
      auto menu_items = variant_list(menu_button->property("menuItems"));
      if (menu_items.isEmpty()) {
        auto* workspace = find_named_item(library_root, QStringLiteral("libraryStudioWorkspace"));
        QVariant menu_items_value;
        if (workspace && QMetaObject::invokeMethod(
                             workspace, "workspaceLayoutMenuItems", Q_RETURN_ARG(QVariant, menu_items_value))) {
          menu_items = variant_list(menu_items_value);
        }
      }
      int item_index = -1;
      for (int index = 0; index < menu_items.size(); ++index) {
        if (menu_items.at(index).toMap().value(QStringLiteral("key")).toString() == key) {
          item_index = index;
          break;
        }
      }
      if (item_index < 0) return false;
      const auto item_name = popup_name + QStringLiteral("Item%1").arg(item_index);
      const auto scroll_name = menu_button->objectName() + QStringLiteral("Scroll");
      QQuickItem* menu_item = nullptr;
      auto* menu_scroll = find_named_item(library_root, scroll_name);
      if (!wait_for_condition([&]() {
            menu_item = find_named_item(library_root, item_name);
            return item_visible(menu_item) && item_visible(menu_scroll);
          }, 3000))
        return false;
      // Long menus can have delegates whose visible flag is true while their
      // bounds remain clipped by the Flickable viewport. Navigate the actual
      // menu focus to the target so MenuPopover scrolls it into view.
      if (!wait_for_condition([&]() { return popup->property("currentFocusIndex").toInt() >= 0; }, 3000))
        return false;
      for (int attempt = 0; attempt < menu_items.size(); ++attempt) {
        const auto focused_index = popup->property("currentFocusIndex").toInt();
        if (focused_index == item_index) break;
        if (!send_library_key(item_index > focused_index ? Qt::Key_Down : Qt::Key_Up))
          return false;
        process_layout_events(3);
      }
      if (!wait_for_condition([&]() {
            return popup->property("currentFocusIndex").toInt() == item_index &&
                   contains_with_tolerance(scene_rect(menu_scroll), scene_rect(menu_item), 0.5);
          }, 3000)) {
        std::cerr << "Layout option did not scroll into the popup viewport: index=" << item_index
                  << " focus=" << popup->property("currentFocusIndex").toInt()
                  << " row=" << scene_rect(menu_item).x() << ',' << scene_rect(menu_item).y() << ' '
                  << scene_rect(menu_item).width() << 'x' << scene_rect(menu_item).height()
                  << " scroll=" << scene_rect(menu_scroll).x() << ',' << scene_rect(menu_scroll).y() << ' '
                  << scene_rect(menu_scroll).width() << 'x' << scene_rect(menu_scroll).height() << '\n';
        return false;
      }
      if (!click_library_item(menu_item)) {
        std::cerr << "Layout option could not receive the native pointer click: "
                  << item_name.toStdString() << " "
                  << visual_child_at_diagnostic(menu_item).toStdString() << '\n';
        return false;
      }
      if (!wait_for_condition([&]() {
            return !popup->property("visible").toBool() &&
                   catalog_assets.sortOrder() == QStringLiteral("filename-asc");
          }, 3000)) {
        std::cerr << "Layout option click did not apply filename sort: "
                  << item_name.toStdString() << " popup=" << popup->property("visible").toBool()
                  << " sort=" << catalog_assets.sortOrder().toStdString() << " "
                  << visual_child_at_diagnostic(menu_item).toStdString() << '\n';
        return false;
      }
      return true;
    };
    const auto select_project_stage = [&](int index) {
      const auto combo_name = QStringLiteral("libraryProjectStage_Nepal Trip");
      auto* combo = find_visible_library_item(combo_name);
      if (!click_library_item(combo)) return false;
      const auto option_name = combo_name + QStringLiteral("Option%1").arg(index);
      auto* option = find_visible_library_item(option_name);
      if (!wait_for_condition([&]() {
            option = find_visible_library_item(option_name);
            return option != nullptr;
          }, 3000) || !click_library_item(option)) return false;
      auto* popup = find_named_object(library_root, combo_name + QStringLiteral("Popup"));
      return wait_for_condition([&]() {
        return combo->property("currentIndex").toInt() == index &&
               (!popup || !popup->property("visible").toBool()) &&
               library_workflow.stageForProject(QStringLiteral("Nepal Trip")) == QStringLiteral("edit");
      }, 3000);
    };

    if (catalog_assets.assetCount() != 3 ||
        !capture_library(QStringLiteral("professional-library-all-images-1440"), 1440, 900,
                         {QStringLiteral("libraryImagesCollectionButton"),
                          QStringLiteral("libraryNavigationPanel"),
                          QStringLiteral("libraryNavigationAllImagesButton"),
                          QStringLiteral("studioLibrarySearchInput"),
                          QStringLiteral("studioLibraryGalleryGrid"),
                          QStringLiteral("libraryInspectorPanel")}) ||
        !visible_named_items_do_not_overlap(
            library_root,
            {QStringLiteral("libraryNavigationPanel"),
             QStringLiteral("studioLibraryGalleryGrid"),
             QStringLiteral("libraryInspectorPanel")})) {
      return fail("All Images should show the complete responsive Library layout at 1440 by 900");
    }

    // Use the actual compact navigation button and project row at the narrow size.
    library_root->setProperty("width", 900);
    library_root->setProperty("height", 700);
    if (!wait_for_condition([&]() {
          return item_visible(find_named_item(library_root, QStringLiteral("libraryCompactNavigationButton")));
        }, 5000) ||
        !activate_library_control(QStringLiteral("libraryCompactNavigationButton")) ||
        !wait_for_condition([&]() {
          auto* drawer = find_named_object(library_root, QStringLiteral("libraryNavigationDrawer"));
          return drawer && drawer->property("opened").toBool() &&
                 find_visible_library_item(QStringLiteral("libraryNavigationProject_Nepal Trip")) != nullptr;
        }, 5000) ||
        !capture_library(QStringLiteral("professional-library-compact-navigation-900"), 900, 700,
                         {QStringLiteral("libraryCompactNavigationButton"),
                          QStringLiteral("libraryImagesCollectionButton"),
                          QStringLiteral("libraryNavigationScroll"),
                          QStringLiteral("libraryNavigationAllImagesButton"),
                          QStringLiteral("libraryNavigationProject_Nepal Trip")}) ||
        !visible_named_items_do_not_overlap(
            library_root,
            {QStringLiteral("libraryCompactNavigationButton"),
             QStringLiteral("libraryImagesCollectionButton"),
             QStringLiteral("libraryCarouselsCollectionButton"),
             QStringLiteral("libraryTimelinesCollectionButton")})) {
      return fail("The 900 by 700 Library should expose its compact drawer without overlapping navigation controls");
    }

    if (!select_project_stage(1)) {
      std::cerr << "Nepal Trip stage after selector: "
                << library_workflow.stageForProject(QStringLiteral("Nepal Trip")).toStdString() << '\n';
      return fail("The project workflow stage selector should persist Edit");
    }
    QJsonObject stage_interaction;
    stage_interaction.insert(QStringLiteral("event"), QStringLiteral("project-stage-selection"));
    stage_interaction.insert(QStringLiteral("method"), QStringLiteral("native-pointer-click"));
    stage_interaction.insert(QStringLiteral("stage"), QStringLiteral("edit"));
    library_manifest.push_back(stage_interaction);
    if (!activate_library_control(QStringLiteral("libraryNavigationProject_Nepal Trip")) ||
        !wait_for_condition([&]() {
          return library_workflow.activeDestination() == QStringLiteral("project:Nepal Trip") &&
                 catalog_assets.projectFilter() == QStringLiteral("Nepal Trip") &&
                 catalog_assets.assetCount() == 1 && catalog_assets.selectedAssetId() == selected_asset_id;
        }, 5000)) {
      std::cerr << "Nepal Trip project state: destination="
                << library_workflow.activeDestination().toStdString()
                << " filter=" << catalog_assets.projectFilter().toStdString()
                << " count=" << catalog_assets.assetCount()
                << " selected=" << catalog_assets.selectedAssetId().toStdString() << '\n';
      return fail("The compact drawer should open the exact Nepal Trip project and save its workflow stage");
    }
    if (!wait_for_condition(library_drawer_is_fully_closed, 5000)) {
      log_library_overlay_state();
      return fail("The compact navigation drawer and its Loader should finish closing after project selection");
    }

    LibraryWorkflowController reopened_workflow(
        runtime_root.filePath(QStringLiteral("library-workflow.json")), QStringLiteral("isolated-test-catalog"));
    if (reopened_workflow.stageForProject(QStringLiteral("Nepal Trip")) != QStringLiteral("edit"))
      return fail("Project stage should persist when the Library workflow controller is reopened");

    library_root->setProperty("width", 1440);
    library_root->setProperty("height", 900);
    if (!wait_for_condition([&]() {
          return item_visible(find_named_item(library_root, QStringLiteral("libraryNavigationProject_Nepal Trip"))) &&
                 !library_root->property("workspaceTransitionActive").toBool();
        }, 5000)) {
      return fail("The full-width Library navigation should restore after the compact drawer proof");
    }

    auto* metadata_panel = find_named_item(library_root, QStringLiteral("libraryMetadataPanel"));
    if (!item_visible(metadata_panel) || !metadata_panel->setProperty("expanded", true))
      return fail("The selected photo metadata panel should expand for keyboard editing");
    if (!scroll_library_item_into_view(metadata_panel))
      return fail("Expanded metadata should remain visible in the Inspector");
    if (catalog_assets.selectedAssetId() != selected_asset_id || catalog_assets.assetCount() != 1 ||
        !type_library_text(QStringLiteral("libraryMetadatatitleField"), QStringLiteral("Namche Bazaar morning")) ||
        !type_library_text(QStringLiteral("libraryMetadatakeywordsField"), QStringLiteral("Nepal, hiking")) ||
        !type_library_text(QStringLiteral("libraryMetadatacaptionField"), QStringLiteral("Three day approach")) ||
        !type_library_text(QStringLiteral("libraryMetadatacreatorField"), QStringLiteral("Looksy proof")) ||
        !type_library_text(QStringLiteral("libraryMetadatacopyrightField"), QStringLiteral("Looksy proof")) ||
        !type_library_text(QStringLiteral("libraryMetadatalocationField"), QStringLiteral("Khumbu, Nepal")) ||
        !type_library_text(QStringLiteral("libraryMetadatacaptureTimeField"), QStringLiteral("2026-10-07T09:15:00-06:00"))) {
      return fail("Expanded metadata fields should accept actual keyboard text without changing the selected photo");
    }
    auto* metadata_apply_button = find_named_item(library_root, QStringLiteral("libraryMetadataApplyButton"));
    auto* metadata_scroll_viewport = library_scroll_viewport_for(metadata_apply_button);
    const auto save_rect_before_scroll = metadata_apply_button ? scene_rect(metadata_apply_button) : QRectF{};
    const auto viewport_rect_before_scroll = metadata_scroll_viewport ? scene_rect(metadata_scroll_viewport) : QRectF{};
    const bool save_inside_viewport_before_scroll = metadata_scroll_viewport && metadata_apply_button &&
        contains_with_tolerance(viewport_rect_before_scroll, save_rect_before_scroll, 0.5);
    auto* metadata_stage_popup = find_named_object(
        library_root, QStringLiteral("libraryProjectStage_Nepal TripPopup"));
    auto* library_focus_item = library_window->activeFocusItem();
    std::cerr << "Library metadata pre-save: selected=" << catalog_assets.selectedAssetId().toStdString()
              << " draftAsset=" << (metadata_panel ? metadata_panel->property("draftAssetId").toString().toStdString() : "")
              << " dirty=" << (metadata_panel && metadata_panel->property("dirty").toBool())
              << " saveEnabled=" << (metadata_apply_button && metadata_apply_button->property("enabled").toBool())
              << " saveInsideViewport=" << save_inside_viewport_before_scroll
              << " saveRect=" << QStringLiteral("%1,%2 %3x%4")
                     .arg(save_rect_before_scroll.x()).arg(save_rect_before_scroll.y())
                     .arg(save_rect_before_scroll.width()).arg(save_rect_before_scroll.height()).toStdString()
              << " viewportRect=" << QStringLiteral("%1,%2 %3x%4")
                     .arg(viewport_rect_before_scroll.x()).arg(viewport_rect_before_scroll.y())
                     .arg(viewport_rect_before_scroll.width()).arg(viewport_rect_before_scroll.height()).toStdString()
              << " stagePopupOpen=" << (metadata_stage_popup && metadata_stage_popup->property("visible").toBool())
              << " focus=" << (library_focus_item ? library_focus_item->objectName().toStdString() : "none") << '\n';
    if (!metadata_panel || metadata_panel->property("draftAssetId").toString() != selected_asset_id ||
        !metadata_panel->property("dirty").toBool()) {
      return fail("Typed metadata should dirty the draft for the selected photo before saving");
    }
    if (!metadata_apply_button || !metadata_scroll_viewport ||
        !scroll_library_item_into_view(metadata_apply_button) ||
        !wait_for_condition([&]() {
          return item_visible(metadata_apply_button) && metadata_apply_button->property("enabled").toBool() &&
                 contains_with_tolerance(scene_rect(metadata_scroll_viewport),
                                         scene_rect(metadata_apply_button), 0.5);
        }, 3000)) {
      std::cerr << "Library metadata before save: selected=" << catalog_assets.selectedAssetId().toStdString()
                << " draftAsset=" << (metadata_panel ? metadata_panel->property("draftAssetId").toString().toStdString() : "")
                << " dirty=" << (metadata_panel && metadata_panel->property("dirty").toBool())
                << " saveEnabled=" << (metadata_apply_button && metadata_apply_button->property("enabled").toBool())
                << " saveRect=" << (metadata_apply_button ? QStringLiteral("%1,%2 %3x%4")
                       .arg(scene_rect(metadata_apply_button).x()).arg(scene_rect(metadata_apply_button).y())
                       .arg(scene_rect(metadata_apply_button).width()).arg(scene_rect(metadata_apply_button).height()).toStdString() : "missing")
                << " viewportRect=" << (metadata_scroll_viewport ? QStringLiteral("%1,%2 %3x%4")
                       .arg(scene_rect(metadata_scroll_viewport).x()).arg(scene_rect(metadata_scroll_viewport).y())
                       .arg(scene_rect(metadata_scroll_viewport).width()).arg(scene_rect(metadata_scroll_viewport).height()).toStdString() : "missing")
                << '\n';
      return fail("Metadata edits should enable a visible Save metadata button inside the Inspector viewport");
    }
    if (!activate_library_control(QStringLiteral("libraryMetadataApplyButton")) ||
        !wait_for_condition([&]() {
          const auto metadata = catalog_assets.selectedMetadata();
          const auto keywords = metadata.value(QStringLiteral("keywords")).toStringList();
          return metadata.value(QStringLiteral("title")).toString() == QStringLiteral("Namche Bazaar morning") &&
                 keywords.contains(QStringLiteral("Nepal")) && keywords.contains(QStringLiteral("hiking"));
        }, 5000)) {
      const auto metadata = catalog_assets.selectedMetadata();
      std::cerr << "Library metadata save result: selected=" << catalog_assets.selectedAssetId().toStdString()
                << " count=" << catalog_assets.assetCount()
                << " dirty=" << (metadata_panel && metadata_panel->property("dirty").toBool())
                << " saveEnabled=" << metadata_apply_button->property("enabled").toBool()
                << " title=" << metadata.value(QStringLiteral("title")).toString().toStdString()
                << " keywords=" << metadata.value(QStringLiteral("keywords")).toStringList().join(',').toStdString()
                << " saveRect=" << QStringLiteral("%1,%2 %3x%4")
                       .arg(scene_rect(metadata_apply_button).x()).arg(scene_rect(metadata_apply_button).y())
                       .arg(scene_rect(metadata_apply_button).width()).arg(scene_rect(metadata_apply_button).height()).toStdString()
                << " viewportRect=" << QStringLiteral("%1,%2 %3x%4")
                       .arg(scene_rect(metadata_scroll_viewport).x()).arg(scene_rect(metadata_scroll_viewport).y())
                       .arg(scene_rect(metadata_scroll_viewport).width()).arg(scene_rect(metadata_scroll_viewport).height()).toStdString()
                << " localMessage=" << (metadata_panel ? metadata_panel->property("localMessage").toString().toStdString() : "")
                << " modelStatus=" << catalog_assets.status().toStdString()
                << " error=" << catalog_assets.errorMessage().toStdString() << '\n';
      return fail("Saving metadata should update the selected photo before catalog sync completes");
    }
    QFile metadata_outbox(runtime_root.filePath(QStringLiteral("metadata-outbox.json")));
    if (!metadata_outbox.open(QIODevice::ReadOnly))
      return fail("Saving photo metadata should preserve the local recovery outbox");
    QJsonParseError metadata_parse_error{};
    const auto metadata_document = QJsonDocument::fromJson(metadata_outbox.readAll(), &metadata_parse_error);
    const auto metadata_record = metadata_document.object().value(QStringLiteral("records")).toObject()
                                     .value(selected_asset_id).toObject();
    if (!metadata_document.isObject() || metadata_parse_error.error != QJsonParseError::NoError ||
        metadata_record.value(QStringLiteral("title")).toString() != QStringLiteral("Namche Bazaar morning") ||
        metadata_record.value(QStringLiteral("location")).toString() != QStringLiteral("Khumbu, Nepal")) {
      return fail("The metadata recovery outbox should contain the edited photo fields");
    }

    // The Library sort is selected through its menu and the saved search is created in navigation.
    const auto all_images_button_name = QStringLiteral("libraryNavigationAllImagesButton");
    if (!activate_library_control(all_images_button_name)) {
      log_library_controls(all_images_button_name);
      return fail("The All Images navigation button should receive a native pointer click");
    }
    if (!wait_for_condition([&]() {
          return library_workflow.activeDestination() == QStringLiteral("all-images") &&
                 catalog_assets.assetCount() == 3 && catalog_assets.projectFilter() == QStringLiteral("all");
        }, 5000)) {
      log_library_controls(all_images_button_name);
      log_library_overlay_state();
      std::cerr << "All Images route result: destination=" << library_workflow.activeDestination().toStdString()
                << " filter=" << catalog_assets.projectFilter().toStdString()
                << " count=" << catalog_assets.assetCount() << '\n';
      return fail("Clicking All Images should clear the project filter and restore the full collection");
    }
    if (!select_layout_menu_item(QStringLiteral("sort:filename-asc")) ||
        catalog_assets.sortOrder() != QStringLiteral("filename-asc") || catalog_assets.assetCount() != 3 ||
        catalog_assets.assetAt(0).value(QStringLiteral("assetId")).toString() != fixture_ids.at(0) ||
        catalog_assets.assetAt(1).value(QStringLiteral("assetId")).toString() != fixture_ids.at(1) ||
        catalog_assets.assetAt(2).value(QStringLiteral("assetId")).toString() != fixture_ids.at(2)) {
      return fail("The Library Layout menu should sort all photos by filename");
    }
    if (!type_library_text(QStringLiteral("studioLibrarySearchInput"), QStringLiteral("Nepal")) ||
        !wait_for_condition([&]() {
          return catalog_assets.searchFilter() == QStringLiteral("Nepal") && catalog_assets.assetCount() == 1 &&
                 catalog_assets.selectedAssetId() == selected_asset_id;
        }, 5000)) {
      std::cerr << "Library sort/search state: destination="
                << library_workflow.activeDestination().toStdString()
                << " sort=" << catalog_assets.sortOrder().toStdString()
                << " count=" << catalog_assets.assetCount()
                << " search=" << catalog_assets.searchFilter().toStdString()
                << " selected=" << catalog_assets.selectedAssetId().toStdString() << " ids=";
      for (int index = 0; index < catalog_assets.assetCount(); ++index) {
        std::cerr << catalog_assets.assetAt(index).value(QStringLiteral("assetId")).toString().toStdString() << ',';
      }
      std::cerr << "\n";
      return fail("Library should sort all photos by filename before metadata search narrows to the edited photo");
    }
    if (!activate_library_control(QStringLiteral("libraryNavigationSaveSearchButton")) ||
        !type_library_text(QStringLiteral("libraryNavigationSearchNameField"), QStringLiteral("Nepal hiking")) ||
        !activate_library_control(QStringLiteral("libraryNavigationSaveSearchConfirm")) ||
        !wait_for_condition([&]() { return library_workflow.savedSearches().size() == 1; }, 5000)) {
      return fail("The Library should save a named metadata search from the current filters");
    }
    const auto saved_search = library_workflow.savedSearches().constFirst().toMap();
    const auto saved_search_id = saved_search.value(QStringLiteral("id")).toString();
    auto* saved_search_button = find_named_item(
        library_root, QStringLiteral("libraryNavigationSavedSearch_") + saved_search_id);
    if (saved_search_id.isEmpty() || !item_visible(saved_search_button) ||
        saved_search.value(QStringLiteral("filters")).toMap()
                .value(QStringLiteral("searchFilter")).toString() != QStringLiteral("Nepal")) {
      return fail("The named saved search should preserve the metadata search term");
    }

    catalog_assets.setSearchFilter(QString());
    if (!activate_library_control(QStringLiteral("libraryNavigationSavedSearch_") + saved_search_id) ||
        !wait_for_condition([&]() {
          return library_workflow.activeDestination() == QStringLiteral("saved-search:") + saved_search_id &&
                 catalog_assets.searchFilter() == QStringLiteral("Nepal") && catalog_assets.assetCount() == 1;
        }, 5000)) {
      return fail("Selecting the saved search should restore its metadata query and results");
    }
    LibraryWorkflowController reopened_workflow_with_search(
        runtime_root.filePath(QStringLiteral("library-workflow.json")), QStringLiteral("isolated-test-catalog"));
    const QVariantMap reopened_search = reopened_workflow_with_search.savedSearches().isEmpty()
                                            ? QVariantMap{}
                                            : reopened_workflow_with_search.savedSearches().constFirst().toMap();
    if (reopened_search.value(QStringLiteral("id")).toString() != saved_search_id ||
        reopened_workflow_with_search.stageForProject(QStringLiteral("Nepal Trip")) != QStringLiteral("edit")) {
      return fail("Saved metadata searches and project stages should survive reopening workflow state");
    }

    if (!activate_library_control(QStringLiteral("libraryNavigationAllImagesButton")) ||
        !wait_for_condition([&]() {
          return catalog_assets.assetCount() == 3 && catalog_assets.searchFilter().isEmpty() &&
                 catalog_assets.projectFilter() == QStringLiteral("all");
        }, 5000)) {
      return fail("Returning to All Images should preserve every fixture photo");
    }
    QStringList visible_fixture_ids;
    for (int index = 0; index < catalog_assets.assetCount(); ++index)
      visible_fixture_ids.push_back(catalog_assets.assetAt(index).value(QStringLiteral("assetId")).toString());
    for (const auto& fixture_id : fixture_ids) {
      if (!visible_fixture_ids.contains(fixture_id))
        return fail("Metadata organization should leave all original Library photos available");
    }

    // Confirm export navigation does not silently change the manual workflow stage.
    if (!activate_library_control(QStringLiteral("libraryNavigationProject_Nepal Trip")) ||
        !wait_for_condition([&]() {
          return catalog_assets.projectFilter() == QStringLiteral("Nepal Trip") && catalog_assets.assetCount() == 1;
        }, 5000)) {
      return fail("The project view should be available before checking export-stage behavior");
    }
    auto* delivery_panel = find_named_item(library_root, QStringLiteral("libraryDeliveryPanel"));
    if (!item_visible(delivery_panel) || !delivery_panel->setProperty("expanded", true) ||
        !scroll_library_item_into_view(delivery_panel)) {
      return fail("The selected project Delivery panel should expand in the Inspector");
    }
    process_layout_events(4);
    auto* delivery_export_button = find_named_item(
        library_root, QStringLiteral("libraryDeliveryExportSelectedButton"));
    auto* delivery_viewport = library_scroll_viewport_for(delivery_export_button);
    if (!delivery_export_button || !delivery_viewport ||
        !scroll_library_item_into_view(delivery_export_button) ||
        !wait_for_condition([&]() {
          return item_visible(delivery_export_button) && delivery_export_button->property("enabled").toBool() &&
                 contains_with_tolerance(scene_rect(delivery_viewport), scene_rect(delivery_export_button), 0.5);
        }, 3000)) {
      std::cerr << "Library delivery before export: selected=" << catalog_assets.selectedAssetId().toStdString()
                << " project=" << catalog_assets.projectFilter().toStdString()
                << " count=" << catalog_assets.assetCount()
                << " stage=" << library_workflow.stageForProject(QStringLiteral("Nepal Trip")).toStdString()
                << " enabled=" << (delivery_export_button && delivery_export_button->property("enabled").toBool())
                << " buttonRect=" << (delivery_export_button ? QStringLiteral("%1,%2 %3x%4")
                       .arg(scene_rect(delivery_export_button).x()).arg(scene_rect(delivery_export_button).y())
                       .arg(scene_rect(delivery_export_button).width()).arg(scene_rect(delivery_export_button).height()).toStdString() : "missing")
                << " viewportRect=" << (delivery_viewport ? QStringLiteral("%1,%2 %3x%4")
                       .arg(scene_rect(delivery_viewport).x()).arg(scene_rect(delivery_viewport).y())
                       .arg(scene_rect(delivery_viewport).width()).arg(scene_rect(delivery_viewport).height()).toStdString() : "missing") << '\n';
      return fail("The project export action should be visible, enabled, and inside the Inspector viewport");
    }
    if (!activate_library_control(QStringLiteral("libraryDeliveryExportSelectedButton")) ||
        !wait_for_condition([&]() {
          return library_root->property("currentWorkspace").toInt() ==
                 library_root->property("exportWorkspaceIndex").toInt();
        }, 10000)) {
      std::cerr << "Library export result: currentWorkspace="
                << library_root->property("currentWorkspace").toInt()
                << " exportWorkspace=" << library_root->property("exportWorkspaceIndex").toInt()
                << " project=" << catalog_assets.projectFilter().toStdString()
                << " buttonEnabled=" << delivery_export_button->property("enabled").toBool()
                << " stage=" << library_workflow.stageForProject(QStringLiteral("Nepal Trip")).toStdString()
                << '\n';
      return fail("The project Delivery action should open Export after a native pointer click");
    }
    if (library_workflow.stageForProject(QStringLiteral("Nepal Trip")) != QStringLiteral("edit")) {
      return fail("Exporting a project should open Export while leaving its manual stage unchanged");
    }

    QFile manifest_file(QDir(proof_dir).filePath(QStringLiteral("professional-library-manifest.json")));
    if (!manifest_file.open(QIODevice::WriteOnly) ||
        manifest_file.write(QJsonDocument(library_manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("Professional Library proof should write its capture manifest");
    }
    return 0;
  }

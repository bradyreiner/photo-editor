// Real order/structure controls, isolated documents, and rendered scale checks.
      const auto order_structure_dir = qEnvironmentVariable("PHOTO_EDITOR_CAROUSEL_ORDER_STRUCTURE_PROOF_DIR");
      if (!order_structure_dir.isEmpty()) {
        auto* window = qobject_cast<QQuickWindow*>(responsive_root);
        if (!window || !QDir().mkpath(order_structure_dir)) return fail("Order/structure proof needs an output directory");
        app_shell.completeFirstRunExperience();
        if (auto* guide = find_named_object(responsive_root, QStringLiteral("firstRunGuideDialog"))) QMetaObject::invokeMethod(guide, "close");
        window->show();
        QJsonArray captures;
        const auto named = [&](const QString& name) { return find_named_object(responsive_root, name); };
        auto* order = named(QStringLiteral("carouselReadingOrderDialog"));
        auto* structure = named(QStringLiteral("carouselDocumentStructureDialog"));
        auto* order_content = order->property("contentItem").value<QObject*>()->property("item").value<QObject*>();
        auto* structure_content = structure->property("contentItem").value<QObject*>()->property("item").value<QObject*>();
        auto* order_list = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselReadingOrderList")));
        auto* structure_list = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselDocumentStructureList")));
        const auto entries = [&] { return carousel_document.readingOrderState().value(QStringLiteral("entries")).toList(); };
        const auto capture = [&](QObject* dialog, const QString& state, double scale, bool empty = false) {
          const auto prefix = dialog == order ? QStringLiteral("carouselReadingOrder") : QStringLiteral("carouselDocumentStructure");
          QStringList required{prefix + "Header", prefix + "Context", prefix + "List", prefix + "CloseButton"};
          if (dialog == structure) required.append(prefix + "Navigation");
          if (dialog == structure || !empty) required.append(prefix + "Footer");
          if (dialog == structure) required.append(QStringList{prefix + "RefreshButton", prefix + "JumpButton"});
          else if (!empty) {
            required.append(QStringList{prefix + "MoveEarlierButton", prefix + "MoveLaterButton", prefix + "FocusButton"});
            if (named(prefix + "MeaningfulToggle")->property("visible").toBool()) required.append(prefix + "MeaningfulToggle");
          }
          (void)window->grabWindow();
          process_layout_events(10);
          if (!wait_for_condition([&] {
                auto* background = dialog->property("background").value<QQuickItem*>();
                if (!background) return false;
                const auto bounds = scene_rect(background).adjusted(-1, -1, 1, 1);
                for (const auto& name : required) {
                  auto* item = qobject_cast<QQuickItem*>(named(name));
                  if (!item_visible(item) || !bounds.contains(scene_rect(item))) return false;
                }
                auto* header = qobject_cast<QQuickItem*>(named(prefix + "Header"));
                auto* close = qobject_cast<QQuickItem*>(named(prefix + "CloseButton"));
                if (std::abs(scene_rect(header).right() - scene_rect(close).right()) > 1.0) return false;
                auto* list = dialog == order ? order_list : structure_list;
                const auto scroll_needed = list->property("count").toInt() > 0 && list->property("contentHeight").toDouble() > list->height();
                if (named(prefix + "ScrollBar")->property("visible").toBool() != scroll_needed) return false;
                if (!empty) {
                  auto* current = list->property("currentItem").value<QQuickItem*>();
                  auto* label = qobject_cast<QQuickItem*>(named(prefix + (dialog == order ? "Label_" : "Message_") +
                      QString::number(list->property("currentIndex").toInt())));
                  if (!current || !label || !scene_rect(list).adjusted(-1, -1, 1, 1).contains(scene_rect(current)) ||
                      !scene_rect(current).adjusted(-1, -1, 1, 1).contains(scene_rect(label)) ||
                      label->height() + 1 < label->property("implicitHeight").toDouble()) return false;
                }
                return true;
              })) {
            window->grabWindow().save(QDir(order_structure_dir).filePath("failed-bounds.png"));
            std::cerr << "Order/structure bounds: " << state.toStdString() << " scale=" << scale << '\n';
            return false;
          }
          return capture_visual_proof(window, responsive_root, order_structure_dir, state + QStringLiteral("-scale-%1").arg(scale),
              scale == 1.0 ? 1280 : 1024, scale == 1.0 ? 800 : 760, required, &captures);
        };
        const auto reveal_inspector = [&] {
          carousel_document.clearSelection();
          carousel_workspace->setProperty("carouselInspectorVisible", true);
          carousel_workspace->setProperty("inspectorPrimarySurface", QStringLiteral("board"));
          process_layout_events(8);
          if (carousel_workspace->property("compactWorkspace").toBool()) {
            auto* drawer = named(QStringLiteral("carouselCompactInspectorDrawer"));
            if (!drawer->property("opened").toBool() && !send_activation_key(named(QStringLiteral("carouselCompactInspectorButton")))) return false;
            if (!wait_for_condition([&] { return drawer->property("opened").toBool(); })) return false;
          }
          return true;
        };
        const auto reveal = [&](QQuickItem* item) {
          auto* scroll = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselInspectorScroll")));
          auto* flickable = scroll ? scroll->property("contentItem").value<QQuickItem*>() : nullptr;
          if (!item || !flickable || !item_visible(item)) return false;
          flickable->setProperty("contentY", std::max(0.0, flickable->property("contentY").toDouble() +
              scene_rect(item).top() - scene_rect(scroll).top() - 12.0));
          process_layout_events(8);
          if (item->objectName() == QStringLiteral("carouselBoardNotesSection")) return true;
          (void)window->grabWindow();
          process_layout_events(10);
          flickable->setProperty("contentY", std::max(0.0, flickable->property("contentY").toDouble() +
              scene_rect(item).top() - scene_rect(scroll).top() - 12.0));
          if (wait_for_condition([&] { return scene_rect(scroll).adjusted(-1, -1, 1, 1).contains(scene_rect(item)); })) return true;
          const auto bounds = scene_rect(item);
          const auto viewport = scene_rect(scroll);
          std::cerr << "Inspector entry " << item->objectName().toStdString() << " bounds=" << bounds.x() << ',' << bounds.y() << ',' << bounds.width() << ',' << bounds.height()
                    << " viewport=" << viewport.x() << ',' << viewport.y() << ',' << viewport.width() << ',' << viewport.height() << '\n';
          window->grabWindow().save(QDir(order_structure_dir).filePath("failed-inspector-entry.png"));
          return false;
        };
        QQuickItem* order_opener = nullptr;
        const auto open_order = [&] {
          if (!reveal_inspector()) return false;
          auto* notes = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselBoardNotesSection")));
          if (!reveal(notes)) return false;
          if (!notes->property("expanded").toBool() && !send_activation_key(notes)) return false;
          process_layout_events(8);
          order_opener = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselReadingOrderButton")));
          if (!reveal(order_opener) || !send_activation_key(order_opener)) return false;
          return wait_for_condition([&] { return order->property("opened").toBool() && order_list->hasActiveFocus(); });
        };
        QQuickItem* structure_opener = nullptr;
        const auto open_structure = [&] {
          if (!reveal_inspector()) return false;
          structure_opener = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselBoardActionsMenuPrimaryAction")));
          if (!reveal(structure_opener) || !send_activation_key(structure_opener)) return false;
          auto* menu = named(QStringLiteral("carouselBoardActionsMenuPopup"));
          if (!wait_for_condition([&] { return menu && menu->property("opened").toBool(); }) ||
              !send_window_key(window, Qt::Key_Down) || !send_window_key(window, Qt::Key_Return)) return false;
          return wait_for_condition([&] { return structure->property("opened").toBool() && structure_list->hasActiveFocus(); });
        };
        const auto select_order = [&](int index) {
          order_list->forceActiveFocus(Qt::TabFocusReason);
          if (!send_window_key(window, Qt::Key_Home)) return false;
          for (int row = 0; row < index; ++row) if (!send_window_key(window, Qt::Key_Down)) return false;
          process_layout_events(8);
          return order_list->property("currentIndex").toInt() == index;
        };
        QImage photo(64, 80, QImage::Format_RGB32);
        photo.fill(QColor("#486b83"));
        const auto photo_path = QDir(order_structure_dir).filePath("order-fixture.png");
        if (!photo.save(photo_path)) return fail("Reading order fixture should save");
        for (const auto scale : {1.0, 2.0}) {
          app_shell.setInterfaceScale(scale);
          responsive_root->setProperty("width", scale == 1.0 ? 1280 : 1024);
          responsive_root->setProperty("height", scale == 1.0 ? 800 : 760);
          carousel_document.createCarouselWithSettings(QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Reading order review")}, {QStringLiteral("slideCount"), 1}});
          for (int row = 0; row < 6; ++row) {
            carousel_document.addTextItem(QStringLiteral("Authored layer %1: useful technical information remains readable when this entry wraps across multiple lines in a compact inspector.").arg(row + 1));
            carousel_document.setSelectedTextFontFamily(QStringLiteral("system"));
          }
          carousel_document.addPhotoItem(photo_path, QStringLiteral("Supporting diagram for the authored technical layout"));
          const auto media_id = carousel_document.selectedItemId();
          if (!open_order() || entries().size() != 7 || !capture(order, "reading-order-populated", scale))
            return fail("Reading order should open from Board Notes with populated entries and fully visible actions at both scales");
          const auto before_order = carousel_document.documentContentHash();
          const auto header_position = scene_rect(qobject_cast<QQuickItem*>(named(QStringLiteral("carouselReadingOrderHeader")))).topLeft();
          if (!send_window_key(window, Qt::Key_End) || !capture(order, "reading-order-last", scale) ||
              scene_rect(qobject_cast<QQuickItem*>(named(QStringLiteral("carouselReadingOrderHeader")))).topLeft() != header_position)
            return fail("End should expose the final entry without moving the dialog header when media controls appear");
          const auto key = order_content->property("selectedKey").toString();
          const auto last_index = entries().size() - 1;
          if (!send_activation_key(named(QStringLiteral("carouselReadingOrderMoveEarlierButton")))) return fail("Move earlier should activate");
          if (!wait_for_condition([&] { return order_content->property("selectedKey").toString() == key &&
                order_list->property("currentIndex").toInt() == last_index - 1 && entries().at(last_index - 1).toMap().value("key").toString() == key; }) ||
              carousel_document.documentContentHash() == before_order || !capture(order, "reading-order-moved", scale)) return fail("Reordering should retain the same selected element across model replacement");
          const auto before_later = carousel_document.documentContentHash();
          if (!send_activation_key(named(QStringLiteral("carouselReadingOrderMoveLaterButton"))) || !wait_for_condition([&] {
                return order_content->property("selectedKey").toString() == key && order_list->property("currentIndex").toInt() == last_index;
              }) || entries().last().toMap().value("key").toString() != key) return fail("Move later should return the same element to the final position");
          carousel_document.undo();
          process_layout_events(8);
          if (carousel_document.documentContentHash() != before_later) return fail("One Undo should restore the earlier position exactly");
          carousel_document.undo();
          process_layout_events(8);
          if (carousel_document.documentContentHash() != before_order || order_content->property("selectedKey").toString() != key) return fail("One Undo should restore the original reading order exactly");
          int media_index = -1;
          for (int row = 0; row < entries().size(); ++row) if (entries().at(row).toMap().value("itemId").toString() == media_id) media_index = row;
          if (media_index < 0 || !select_order(media_index)) return fail("Photo entry should be selectable through list keys");
          const auto included = carousel_document.readingOrderState().value("meaningfulEntryCount").toInt();
          if (!send_activation_key(named(QStringLiteral("carouselReadingOrderMeaningfulToggle"))) || !wait_for_condition([&] {
                return carousel_document.readingOrderState().value("meaningfulEntryCount").toInt() == included - 1;
              }) || entries().size() != 7 || carousel_document.canvasItems().size() != 7 || !QFileInfo::exists(photo_path) ||
              !capture(order, "reading-order-decorative", scale)) return fail("Decorative media should remain in the document while being excluded from delivery order");
          carousel_document.undo();
          process_layout_events(8);
          if (carousel_document.documentContentHash() != before_order) return fail("One Undo should restore media inclusion exactly");
          if (!send_activation_key(named(QStringLiteral("carouselReadingOrderFocusButton"))) || !wait_for_condition([&] {
                return !order->property("visible").toBool() && carousel_document.selectedItemId() == media_id &&
                    window->activeFocusItem() == qobject_cast<QQuickItem*>(carousel_workspace);
              }) || carousel_document.documentContentHash() != before_order) return fail("Show on canvas should select the exact layer and focus the workspace without editing");
          if (!open_order() || !send_window_key(window, Qt::Key_Escape) || !wait_for_condition([&] { return window->activeFocusItem() == order_opener; })) return fail("Reading order Escape should return focus to its real invoker");
          if (!open_order()) return fail("Reading order should reopen");
          carousel_document.createCarouselWithSettings(QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Empty reading order")}, {QStringLiteral("slideCount"), 1}});
          process_layout_events(8);
          if (order_list->property("count").toInt() != 0 || !order_content->property("selectedKey").toString().isEmpty() ||
              named(QStringLiteral("carouselReadingOrderFooter"))->property("visible").toBool() || !capture(order, "reading-order-empty-live", scale, true) ||
              !send_activation_key(named(QStringLiteral("carouselReadingOrderCloseButton")))) return fail("An empty live model should clear stale targets and retain its usable Close control");
          carousel_document.createCarouselWithSettings(QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Structure review")}, {QStringLiteral("slideCount"), 3}});
          for (int board = 0; board < 3; ++board) {
            carousel_document.selectSlide(board);
            carousel_document.applyTemplateToCurrent(QStringLiteral("split-pair"));
            carousel_document.addTextItem(QStringLiteral("Repeated authored heading for a technical sequence"));
            carousel_document.setSelectedTextFontFamily(QStringLiteral("system"));
          }
          carousel_document.selectSlide(0);
          const auto before_structure = carousel_document.documentContentHash();
          if (!open_structure() || structure_list->property("count").toInt() < 7 || !capture(structure, "structure-all", scale)) return fail("Structure should open from its actual board menu and contain populated findings at both scales");
          const auto sorted = structure->property("filteredIssues").toList();
          bool advisory_seen = false;
          for (const auto& issue : sorted) {
            if (issue.toMap().value("severity").toString() != "error") advisory_seen = true;
            else if (advisory_seen) return fail("Blocking structural findings should precede advisory findings");
          }
          if (!send_window_key(window, Qt::Key_End) || !capture(structure, "structure-last", scale)) return fail("End should reveal the full final finding");
          const auto selected_issue = structure_content->property("selectedIssueId").toString();
          if (!send_activation_key(named(QStringLiteral("carouselDocumentStructureRefreshButton"))) || !wait_for_condition([&] {
                return structure_content->property("selectedIssueId").toString() == selected_issue;
              })) return fail("Refresh should preserve the selected stable finding");
          for (const auto& category : {"empty-slots", "duplicate-copy", "unmatched-components", "broken-links", "style-outliers", "all"}) {
            if (!send_activation_key(named(QStringLiteral("carouselDocumentStructureCategory_") + category))) return fail("Every category should be keyboard reachable");
            process_layout_events(8);
            int expected = 0;
            for (const auto& issue : carousel_document.documentStructureIssues()) if (QString(category) == "all" || issue.toMap().value("category").toString() == category) ++expected;
            if (structure_list->property("count").toInt() != expected || !capture(structure, QStringLiteral("structure-") + category, scale, expected == 0)) return fail("Category filters should show exact controller counts and usable empty states");
            if (!expected && named(QStringLiteral("carouselDocumentStructureJumpButton"))->property("enabled").toBool()) return fail("An empty category should disable its target action");
          }
          if (!send_window_key(window, Qt::Key_Escape) || !wait_for_condition([&] { return window->activeFocusItem() == structure_opener; })) return fail("Structure Escape should return focus to its actual board menu invoker");
          if (!open_structure() || !send_activation_key(named(QStringLiteral("carouselDocumentStructureCategory_duplicate-copy")))) return fail("Duplicate copy should reopen through the category control");
          process_layout_events(8);
          const auto target = structure_content->property("selectedIssue").toMap();
          if (target.value("itemId").toString().isEmpty() || !send_activation_key(named(QStringLiteral("carouselDocumentStructureJumpButton"))) ||
              !wait_for_condition([&] { return !structure->property("visible").toBool() && carousel_document.selectedItemId() == target.value("itemId").toString() &&
                    window->activeFocusItem() == qobject_cast<QQuickItem*>(carousel_workspace); }) || carousel_document.documentContentHash() != before_structure) return fail("Show layer should navigate to the precise duplicate without modifying the document");
          if (!open_structure() || !send_activation_key(named(QStringLiteral("carouselDocumentStructureCategory_empty-slots")))) return fail("Empty slot category should reopen");
          process_layout_events(8);
          const auto board_target = structure_content->property("selectedIssue").toMap().value("boardIndex").toInt();
          if (!send_activation_key(named(QStringLiteral("carouselDocumentStructureJumpButton"))) || !wait_for_condition([&] { return !structure->property("visible").toBool() && carousel_document.currentIndex() == board_target; }) ||
              carousel_document.documentContentHash() != before_structure) return fail("Show board should navigate to the exact board without editing");
        }
        QFile receipt(QDir(order_structure_dir).filePath("manifest.json"));
        if (!receipt.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return fail("Order/structure manifest should save once");
        receipt.write(QJsonDocument(QJsonObject{{"captures", captures}, {"nativeEligible", false},
            {"scope", "Software Qt reading order and document structure workflows at 100 and 200 percent; real isolated controller records, keyboard entry and selection, model refresh, exact Undo, inclusion, filtering and focus handoff"}}).toJson(QJsonDocument::Indented));
        if (!dialog_warnings.isEmpty()) return fail(dialog_warnings.join('\n').toUtf8().constData());
        app_shell.setInterfaceScale(prior_scale);
        responsive_root->setProperty("width", prior_width);
        responsive_root->setProperty("height", prior_height);
        carousel_document.createCarouselWithSettings(QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Responsive proof")}, {QStringLiteral("slideCount"), 1}});
        process_layout_events(8);
      }

      // Exercise the media pool through visible menu rows against the same
      // generated assets used by the Add Media picker proof below.
      const auto workflow_bin_id = timeline_document.createMediaBin(
          QStringLiteral("Workflow Bin"));
      const auto workflow_collection_id = timeline_document.createSmartCollection(
          QStringLiteral("Workflow Videos"),
          QVariantMap{{QStringLiteral("mediaType"), QStringLiteral("video")}});
      if (workflow_bin_id.isEmpty() || workflow_collection_id.isEmpty() ||
          !timeline_document.assignMediaAssetToBin(QStringLiteral("workflow-video"), workflow_bin_id)) {
        return fail("Media pool proof should create a bin, collection, and fixture assignment");
      }
      timeline_document.setActiveMediaBinId(QString());
      timeline_document.setActiveSmartCollectionId(QString());
      process_layout_events(6);

      const auto media_row_visible = [&](const QString& asset_id) {
        for (auto* row : find_named_items(studio_root, QStringLiteral("timelineMediaAssetRow"))) {
          if (row->property("assetId").toString() == asset_id && item_visible(row))
            return true;
        }
        return false;
      };
      const auto menu_row_index = [&](const QString& button_name, const QString& key) {
        auto* button = live_item(button_name);
        if (!button)
          return -1;
        const auto items = button->property("menuItems").toList();
        for (qsizetype index = 0; index < items.size(); ++index) {
          if (items.at(index).toMap().value(QStringLiteral("key")).toString() == key)
            return static_cast<int>(index);
        }
        return -1;
      };
      const auto choose_media_menu_item = [&](const QString& button_name,
                                               const QString& key,
                                               const QString& capture_name = QString()) {
        const auto index = menu_row_index(button_name, key);
        if (index < 0 || !click_named(button_name))
          return false;
        const auto popup_name = button_name + QStringLiteral("Popup");
        const auto row_name = popup_name + QStringLiteral("Item%1").arg(index);
        if (!wait_for_condition([&] {
              auto* popup = find_named_object(studio_root, popup_name);
              return popup && popup->property("opened").toBool() && live_item(row_name);
            }, 3000)) {
          return false;
        }
        if (!capture_name.isEmpty() && !capture_workflow(capture_name, {row_name}))
          return false;
        return click_named(row_name);
      };

      const auto bin_filter_key = QStringLiteral("scope:bin:") + workflow_bin_id;
      const auto collection_filter_key = QStringLiteral("scope:collection:") + workflow_collection_id;
      if (!choose_media_menu_item(QStringLiteral("timelineActiveMediaBinFilter"),
                                  collection_filter_key) ||
          !wait_for_condition([&] {
            return timeline_document.activeSmartCollectionId() == workflow_collection_id &&
                   timeline_document.activeMediaBinId().isEmpty() &&
                   media_row_visible(QStringLiteral("workflow-video")) &&
                   !media_row_visible(QStringLiteral("studio-photo"));
          }, 3000) ||
          !choose_media_menu_item(QStringLiteral("timelineActiveMediaBinFilter"),
                                  bin_filter_key, QStringLiteral("04c-media-bin-menu")) ||
          !wait_for_condition([&] {
            return timeline_document.activeMediaBinId() == workflow_bin_id &&
                   timeline_document.activeSmartCollectionId() == workflow_collection_id &&
                   media_row_visible(QStringLiteral("workflow-video")) &&
                   !media_row_visible(QStringLiteral("studio-photo"));
          }, 3000) ||
          !choose_media_menu_item(QStringLiteral("timelineActiveMediaBinFilter"),
                                  collection_filter_key) ||
          timeline_document.activeMediaBinId() != workflow_bin_id ||
          !timeline_document.activeSmartCollectionId().isEmpty() ||
          !media_row_visible(QStringLiteral("workflow-video"))) {
        return fail("Pointer-selected bins and smart collections should filter and compose in the media pool");
      }
      if (!choose_media_menu_item(QStringLiteral("timelineActiveMediaBinFilter"),
                                  bin_filter_key) ||
          !wait_for_condition([&] {
            return timeline_document.activeMediaBinId().isEmpty() &&
                   media_row_visible(QStringLiteral("workflow-video")) &&
                   media_row_visible(QStringLiteral("studio-photo"));
          }, 3000)) {
        return fail("Clearing the media bin should restore the complete pool");
      }

      auto* media_search_field = live_item(QStringLiteral("timelineUnifiedMediaSearchField"));
      if (!media_search_field || !click_item(media_search_field))
        return fail("Media pool proof should focus its visible search field");
      for (const auto character : QStringLiteral("0:02")) {
        const auto key = character.isDigit() ? static_cast<int>(character.unicode()) : 0;
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, QString(character));
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, QString(character));
        QCoreApplication::sendEvent(studio_window, &press);
        QCoreApplication::sendEvent(studio_window, &release);
        process_layout_events(2);
      }
      const auto choose_search_scope = [&](const QString& scope,
                                            const QString& capture_name = QString()) {
        const auto key = scope;
        const auto index = menu_row_index(QStringLiteral("timelineMediaSearchScopeMenuButton"), key);
        if (index < 0 || !click_named(QStringLiteral("timelineMediaSearchScopeMenuButton")))
          return false;
        const auto row_name = QStringLiteral("timelineMediaSearchScopeMenuButtonPopupItem%1")
                                  .arg(index);
        if (!wait_for_condition([&] {
              auto* popup = find_named_object(
                  studio_root, QStringLiteral("timelineMediaSearchScopeMenuButtonPopup"));
              return popup && popup->property("opened").toBool() && live_item(row_name);
            }, 3000)) {
          return false;
        }
        if (!capture_name.isEmpty() && !capture_workflow(capture_name, {row_name}))
          return false;
        return click_named(row_name);
      };
      if (media_search_field->property("text").toString() != QStringLiteral("0:02") ||
          !wait_for_condition([&] { return media_row_visible(QStringLiteral("workflow-video")); }, 3000) ||
          !choose_search_scope(QStringLiteral("filename"), QStringLiteral("04d-media-search-fields")) ||
          !wait_for_condition([&] { return !media_row_visible(QStringLiteral("workflow-video")); }, 3000) ||
          !choose_search_scope(QStringLiteral("filename")) ||
          !choose_search_scope(QStringLiteral("metadata")) ||
          !wait_for_condition([&] { return media_row_visible(QStringLiteral("workflow-video")); }, 3000)) {
        return fail("Typed media search should respect the pointer-selected Filename and Metadata scopes");
      }

      media_search_field = live_item(QStringLiteral("timelineUnifiedMediaSearchField"));
      if (!media_search_field || !click_item(media_search_field) ||
          !send_window_key(studio_window, Qt::Key_A,
                           QKeySequence(QKeySequence::SelectAll)[0].keyboardModifiers()) ||
          !send_window_key(studio_window, Qt::Key_Backspace)) {
        return fail("Media pool proof should clear its typed query before continuing");
      }
      timeline_document.setActiveMediaBinId(QString());
      timeline_document.setActiveSmartCollectionId(QString());
      if (!timeline_document.deleteSmartCollection(workflow_collection_id) ||
          !timeline_document.assignMediaAssetToBin(QStringLiteral("workflow-video"), QString()) ||
          !timeline_document.deleteMediaBin(workflow_bin_id) ||
          !wait_for_condition([&] {
            return media_search_field->property("text").toString().isEmpty() &&
                   media_row_visible(QStringLiteral("workflow-video")) &&
                   media_row_visible(QStringLiteral("studio-photo"));
          }, 3000)) {
        return fail("Media pool proof should clean up its filters and fixture bins");
      }

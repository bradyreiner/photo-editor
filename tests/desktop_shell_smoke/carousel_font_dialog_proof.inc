// Populated font workflows in the responsive fixture's isolated controllers.
      const auto font_proof_dir = qEnvironmentVariable("PHOTO_EDITOR_FONT_DIALOG_PROOF_DIR");
      if (!font_proof_dir.isEmpty()) {
        auto* window = qobject_cast<QQuickWindow*>(responsive_root);
        if (!window || !QDir().mkpath(font_proof_dir)) return fail("Font dialog proof needs a window and output directory");
        app_shell.completeFirstRunExperience();
        if (auto* guide = find_named_object(responsive_root, QStringLiteral("firstRunGuideDialog")))
          QMetaObject::invokeMethod(guide, "close");
        window->show();
        QJsonArray captures;
        const auto named = [&](const QString& name) { return find_named_object(responsive_root, name); };
        auto* browser = named(QStringLiteral("carouselFontBrowserDialog"));
        auto* recovery = named(QStringLiteral("carouselFontRecoveryDialog"));
        auto* browser_content = browser->property("contentItem").value<QObject*>()->property("item").value<QObject*>();
        auto* apply_font = named(QStringLiteral("carouselFontBrowserApplyButton"));
        auto* apply_replacement = named(QStringLiteral("carouselApplyFontReplacementButton"));
        auto* search = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselFontSearchField")));
        auto* list = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselFontFamilyList")));
        const QStringList browser_required{QStringLiteral("carouselFontBrowserHeader"), QStringLiteral("carouselFontBrowserContext"),
            QStringLiteral("carouselFontSearchField"), QStringLiteral("carouselFontBrowserNavigation"),
            QStringLiteral("carouselFontFamilyList"), QStringLiteral("carouselFontBrowserFooter"),
            QStringLiteral("carouselFontBrowserCloseButton"), QStringLiteral("carouselFontBrowserApplyButton")};
        const QStringList recovery_required{QStringLiteral("carouselFontRecoveryHeader"), QStringLiteral("carouselFontRecoveryChoices"),
            QStringLiteral("carouselFontRecoverySummary"), QStringLiteral("carouselFontRecoveryList"),
            QStringLiteral("carouselFontRecoveryFooter"), QStringLiteral("carouselFontRecoveryCloseButton"),
            QStringLiteral("carouselApplyFontReplacementButton")};
        const auto capture = [&](QObject* dialog, const QString& state, double scale) {
          const auto& required = dialog == browser ? browser_required : recovery_required;
          // A readback completes font-dependent delegate layout. Check the
          // resulting row as well as the dialog's static controls.
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
                if (dialog == browser && state == QStringLiteral("installed")) {
                  auto* current = list->property("currentItem").value<QQuickItem*>();
                  auto* sample = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselFontSample_%1").arg(list->property("currentIndex").toInt())));
                  const auto viewport = scene_rect(list).adjusted(-1, -1, 1, 1);
                  if (!item_visible(current) || !item_visible(sample) || !viewport.contains(scene_rect(current)) ||
                      !viewport.contains(scene_rect(sample))) return false;
                }
                return true;
              })) {
            window->grabWindow().save(QDir(font_proof_dir).filePath(QStringLiteral("failed-bounds.png")));
            std::cerr << "Font dialog bounds failed: " << state.toStdString() << " scale=" << scale << '\n';
            return false;
          }
          return capture_visual_proof(window, responsive_root, font_proof_dir,
              QStringLiteral("fonts-%1-scale-%2").arg(state).arg(scale), scale == 1.0 ? 1280 : 1024,
              scale == 1.0 ? 800 : 760, required, &captures);
        };
        const auto type_search = [&](const QString& value) {
          search->forceActiveFocus(Qt::TabFocusReason);
          if (!send_window_key(window, Qt::Key_A, QKeySequence(QKeySequence::SelectAll)[0].keyboardModifiers())) return false;
          if (value.isEmpty()) {
            if (!send_window_key(window, Qt::Key_Backspace)) return false;
          }
          for (const auto character : value) {
            QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(character));
            QCoreApplication::sendEvent(window, &press);
            QKeyEvent release(QEvent::KeyRelease, 0, Qt::NoModifier, QString(character));
            QCoreApplication::sendEvent(window, &release);
            process_layout_events(2);
            if (window->activeFocusItem() != search) return false;
          }
          process_layout_events(8);
          return carousel_workspace->property("fontBrowserSearchText").toString() == value;
        };
        const auto activate_section = [&](const QString& section) {
          if (!send_activation_key(named(QStringLiteral("carouselFontBrowserSection_") + section))) return false;
          process_layout_events(8);
          return carousel_workspace->property("fontBrowserSection").toString() == section;
        };
        QQuickItem* opener = nullptr;
        bool capture_inspector_entry = false;
        const auto open_browser = [&] {
          carousel_workspace->setProperty("carouselInspectorVisible", true);
          carousel_workspace->setProperty("inspectorPrimarySurface", QStringLiteral("board"));
          if (carousel_workspace->property("compactWorkspace").toBool()) {
            auto* drawer = named(QStringLiteral("carouselCompactInspectorDrawer"));
            if (!drawer->property("opened").toBool() &&
                !send_activation_key(named(QStringLiteral("carouselCompactInspectorButton")))) return false;
            if (!wait_for_condition([&] { return drawer->property("opened").toBool(); })) return false;
          }
          opener = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselOpenFontBrowserButton")));
          auto* current_font = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselCurrentFontLabel")));
          auto* scroll = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselInspectorScroll")));
          auto* flickable = scroll->property("contentItem").value<QQuickItem*>();
          if (!opener || !current_font || !flickable || !wait_for_condition([&] { return item_visible(opener); })) return false;
          flickable->setProperty("contentY", std::max(0.0, flickable->property("contentY").toDouble() +
              std::min(scene_rect(current_font).top(), scene_rect(opener).top()) - scene_rect(scroll).top() - 20.0));
          process_layout_events(8);
          if (!wait_for_condition([&] {
                const auto viewport = scene_rect(scroll).adjusted(-1, -1, 1, 1);
                return viewport.contains(scene_rect(current_font)) && viewport.contains(scene_rect(opener));
              })) return false;
          if (capture_inspector_entry) {
            const auto scale = app_shell.interfaceScale();
            if (!capture_visual_proof(window, responsive_root, font_proof_dir,
                QStringLiteral("fonts-missing-inspector-scale-%1").arg(scale), scale == 1.0 ? 1280 : 1024,
                scale == 1.0 ? 800 : 760,
                QStringList{QStringLiteral("carouselCurrentFontLabel"), QStringLiteral("carouselOpenFontBrowserButton"),
                    QStringLiteral("carouselInspectorScroll")}, &captures)) return false;
            capture_inspector_entry = false;
          }
          if (!send_activation_key(opener) || !wait_for_condition([&] {
                return browser->property("visible").toBool() && window->activeFocusItem() == search;
              })) return false;
          return true;
        };
        const auto select_first_match = [&](const QString& query) {
          if (!type_search(query) || list->property("count").toInt() <= 0 || !send_window_key(window, Qt::Key_Down)) return false;
          process_layout_events(8);
          const auto selected = browser_content->property("selectedFamily").toString();
          if (!list->hasActiveFocus() || selected.isEmpty()) {
            std::cerr << "Font selection query=" << query.toStdString() << " count=" << list->property("count").toInt()
                      << " selected=" << selected.toStdString() << " focus="
                      << (window->activeFocusItem() ? window->activeFocusItem()->objectName().toStdString() : "none") << '\n';
            window->grabWindow().save(QDir(font_proof_dir).filePath(QStringLiteral("failed-selection.png")));
            return false;
          }
          return true;
        };
        const auto installed = carousel_document.installedFontFamilies();
        if (installed.size() < 2) return fail("Font dialog proof needs multiple installed families");
        const auto preferred = installed.contains(QStringLiteral("Avenir Next")) ? QStringLiteral("Avenir Next") : installed.first();
        const auto fallback = installed.contains(QStringLiteral("Helvetica Neue")) ? QStringLiteral("Helvetica Neue") : installed.last();
        for (const auto scale : {1.0, 2.0}) {
          app_shell.setInterfaceScale(scale);
          responsive_root->setProperty("width", scale == 1.0 ? 1280 : 1024);
          responsive_root->setProperty("height", scale == 1.0 ? 800 : 760);
          carousel_document.createCarouselWithSettings(QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Font review")}, {QStringLiteral("slideCount"), 1}});
          carousel_document.addTextItem(QStringLiteral("A technical editor should keep this authored preview readable, with useful controls and enough space for a second line of text."));
          carousel_document.setSelectedTextFontFamily(QStringLiteral("system"));
          process_layout_events(8);
          for (const auto& family : carousel_document.favoriteFontFamilies()) carousel_document.toggleFavoriteFontFamily(family);
          const auto before_font = carousel_document.documentContentHash();
          if (!open_browser() || !wait_for_condition([&] {
                auto* current = list->property("currentItem").value<QQuickItem*>();
                return item_visible(current) && scene_rect(list).adjusted(-1, -1, 1, 1).contains(scene_rect(current));
              }) || !capture(browser, QStringLiteral("installed"), scale))
            return fail("Font browser should open through its text inspector with its complete current-font row and controls visible at both scales");
          if (!activate_section(QStringLiteral("favorites")) || list->property("count").toInt() != 0 ||
              apply_font->property("enabled").toBool() || !capture(browser, QStringLiteral("favorites-empty"), scale))
            return fail("Empty favorites should explain their state and prevent applying an absent family");
          if (!activate_section(QStringLiteral("recent")) || !capture(browser, scale == 1.0 ? QStringLiteral("recent-empty") : QStringLiteral("recent-restored"), scale))
            return fail("Recent fonts should remain accessible at both scales");
          if (!activate_section(QStringLiteral("all")) || !type_search(QStringLiteral("NoSuchFont-Q4X9")) ||
              list->property("count").toInt() != 0 || apply_font->property("enabled").toBool() ||
              !capture(browser, QStringLiteral("search-empty"), scale)) return fail("Unmatched search should explain its empty result and disable Apply");
          if (!select_first_match(preferred)) return fail("Typed font search and Down should focus and select a matching family");
          const auto chosen = browser_content->property("selectedFamily").toString();
          if (carousel_document.documentContentHash() != before_font || !capture(browser, QStringLiteral("search-selected"), scale))
            return fail("Selecting a browser row should preview without changing authored font state");
          const auto selected_index = list->property("currentIndex").toInt();
          auto* row = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselFontFamilyRow_%1").arg(selected_index)));
          auto* sample = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselFontSample_%1").arg(selected_index)));
          auto* favorite = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselFavoriteFont_%1").arg(selected_index)));
          if (!row || !sample || !favorite || !scene_rect(row).adjusted(-1, -1, 1, 1).contains(scene_rect(sample)) ||
              !scene_rect(row).adjusted(-1, -1, 1, 1).contains(scene_rect(favorite)) || !send_activation_key(favorite))
            return fail("Font samples and favorite controls should fit their adaptive row at both scales");
          process_layout_events(8);
          if (!carousel_document.favoriteFontFamilies().contains(chosen) || carousel_document.documentContentHash() != before_font ||
              !type_search(QString{}) || !activate_section(QStringLiteral("favorites")) || list->property("count").toInt() != 1 ||
              !capture(browser, QStringLiteral("favorites"), scale)) return fail("Starring a family should populate Favorites without altering the document");
          if (!send_activation_key(named(QStringLiteral("carouselFavoriteFont_0"))) ||
              !wait_for_condition([&] { return list->property("count").toInt() == 0 && list->hasActiveFocus(); }) ||
              apply_font->property("enabled").toBool() || !capture(browser, QStringLiteral("favorites-removed"), scale))
            return fail("Removing the last favorite should retain list focus, explain the empty state, and disable Apply");
          if (!activate_section(QStringLiteral("all")) || !select_first_match(chosen) ||
              !send_activation_key(named(QStringLiteral("carouselFavoriteFont_%1").arg(list->property("currentIndex").toInt()))) ||
              !type_search(QString{}) || !activate_section(QStringLiteral("favorites")))
            return fail("Favorites should remain editable after removing their last row");
          if (!send_activation_key(apply_font)) return fail("Font should apply through the single footer action");
          process_layout_events(8);
          if (browser->property("visible").toBool() || window->activeFocusItem() != opener ||
              carousel_document.selectionDetails().value(QStringLiteral("textFontFamily")).toString() != chosen ||
              !carousel_document.recentFontFamilies().contains(chosen)) return fail("Apply should change the chosen font, record it in Recent, dismiss, and restore inspector focus");
          const auto after_font = carousel_document.documentContentHash();
          {
            photo_editor::desktop::CarouselDocumentController restored_fonts;
            if (!restored_fonts.favoriteFontFamilies().contains(chosen) || !restored_fonts.recentFontFamilies().contains(chosen))
              return fail("Font favorites and recents should persist in the isolated design library");
          }
          carousel_document.undo();
          if (carousel_document.documentContentHash() != before_font || after_font == before_font)
            return fail("One Undo should restore the original browser font change exactly");
          if (!open_browser() || !activate_section(QStringLiteral("recent")) || list->property("count").toInt() == 0 ||
              !capture(browser, QStringLiteral("recent"), scale) || !send_window_key(window, Qt::Key_Escape))
            return fail("Recent fonts should retain chosen families after document Undo and dismiss with Escape");
          process_layout_events(8);
          if (window->activeFocusItem() != opener || carousel_document.documentContentHash() != before_font)
            return fail("Cancelling the browser should return focus without editing the document");

          const auto missing = QStringLiteral("Looksy Missing Face Q4X9");
          const auto other_missing = QStringLiteral("Looksy Other Missing Face Q4X9");
          carousel_document.createCarouselWithSettings(QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Font recovery review")}, {QStringLiteral("slideCount"), 1}});
          for (int index = 0; index < 6; ++index) {
            carousel_document.addTextItem(QStringLiteral("Affected text layer %1 with authored content").arg(index + 1));
            carousel_document.setSelectedTextFontFamily(index == 5 ? other_missing : missing);
            carousel_document.setSelectedTextPointSize(index == 0 ? 64.0 : 24.0);
            carousel_document.setSelectedTextFrameSize(index == 0 ? 160.0 : 480.0, index == 0 ? 24.0 : 180.0);
          }
          carousel_document.selectItem(carousel_document.canvasItems().first().toMap().value(QStringLiteral("itemId")).toString());
          process_layout_events(8);
          const auto before_missing = carousel_document.documentContentHash();
          if (carousel_document.missingFontEntries().size() != 6) {
            std::cerr << "Missing font fixture has " << carousel_document.missingFontEntries().size() << " missing entries and "
                      << carousel_document.canvasItems().size() << " canvas items\n";
            return fail("Missing font fixture should have six unavailable authored faces");
          }
          capture_inspector_entry = true;
          if (!open_browser()) {
            std::cerr << "Missing browser entry focus=" << (window->activeFocusItem() ? window->activeFocusItem()->objectName().toStdString() : "none")
                      << " selected=" << carousel_document.selectedItemId().toStdString() << '\n';
            window->grabWindow().save(QDir(font_proof_dir).filePath(QStringLiteral("failed-entry.png")));
            return fail("Missing-face browser should open through the text inspector");
          }
          if (!capture(browser, QStringLiteral("missing-face"), scale) || !select_first_match(fallback) ||
              !send_activation_key(apply_font)) return fail("Missing-face browser should open an explicit replacement review");
          process_layout_events(8);
          const auto preview = carousel_document.fontSubstitutionPreview();
          auto* source_combo = named(QStringLiteral("carouselMissingFontCombo"));
          auto* replacement_combo = named(QStringLiteral("carouselReplacementFontCombo"));
          if (!recovery->property("visible").toBool() || browser->property("visible").toBool() ||
              window->activeFocusItem() != qobject_cast<QQuickItem*>(apply_replacement) || source_combo->property("count").toInt() != 2 ||
              preview.size() != 5 || !apply_replacement->property("enabled").toBool() ||
              replacement_combo->property("currentText").toString() != preview.first().toMap().value(QStringLiteral("replacementFontFamily")).toString() ||
              carousel_document.documentContentHash() != before_missing || !capture(recovery, QStringLiteral("recovery-preview"), scale))
            return fail("Browser handoff should keep recovery focus and show the exact unique missing face, chosen replacement, affected layers, and unapplied document");
          if (!preview.first().toMap().value(QStringLiteral("overflowAfter")).toBool()) return fail("Recovery fixture should expose a real measured overflow");
          auto* recovery_list = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselFontRecoveryList")));
          if (!send_activation_key(recovery_list, Qt::Key_End)) return fail("Recovery list should accept keyboard traversal");
          if (!wait_for_condition([&] {
                auto* last = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselFontRecoveryRow_4")));
                return recovery_list->property("currentIndex").toInt() == 4 && item_visible(last)
                    && scene_rect(recovery_list).adjusted(-1, -1, 1, 1).contains(scene_rect(last));
              }) ||
              !capture(recovery, QStringLiteral("recovery-lower"), scale)) return fail("Later affected layers should remain keyboard reachable");
          const auto previous_replacement = replacement_combo->property("currentIndex").toInt();
          if (!send_activation_key(replacement_combo, previous_replacement + 1 < installed.size() ? Qt::Key_Down : Qt::Key_Up))
            return fail("Replacement selector should accept a different installed family");
          process_layout_events(8);
          if (replacement_combo->property("currentIndex").toInt() == previous_replacement ||
              !carousel_document.fontSubstitutionPreview().isEmpty() || apply_replacement->property("enabled").toBool() ||
              !capture(recovery, QStringLiteral("recovery-unpreviewed"), scale)) return fail("Changing the displayed font pair should invalidate the old preview and disable Apply");
          if (!send_activation_key(named(QStringLiteral("carouselPreviewFontReplacementButton")))) return fail("Replacement should re-preview through its real control");
          process_layout_events(8);
          const auto new_replacement = replacement_combo->property("currentText").toString();
          if (carousel_document.fontSubstitutionPreview().size() != 5 || !apply_replacement->property("enabled").toBool() ||
              carousel_document.documentContentHash() != before_missing || !send_window_key(window, Qt::Key_Escape))
            return fail("Refreshing review should remain non-mutating until Apply");
          process_layout_events(8);
          if (!carousel_document.fontSubstitutionPreview().isEmpty() || carousel_document.documentContentHash() != before_missing || window->activeFocusItem() != opener)
            return fail("Cancelling recovery should clear its preview, preserve authored fonts, and restore the original inspector control");
          if (!open_browser() || !select_first_match(new_replacement) || !send_activation_key(apply_font)) return fail("Recovery should reopen through the browser");
          process_layout_events(8);
          if (!send_activation_key(apply_replacement)) return fail("Reviewed replacement should apply by keyboard");
          process_layout_events(8);
          if (recovery->property("visible").toBool() || window->activeFocusItem() != opener || carousel_document.missingFontEntries().size() != 1)
            return fail("Applying one missing-face replacement should leave the unrelated missing face unresolved and return focus");
          for (int index = 0; index < carousel_document.canvasItems().size(); ++index) {
            const auto item = carousel_document.canvasItems().at(index).toMap();
            if (item.value(QStringLiteral("textFontFamily")).toString() != (index == 5 ? other_missing : new_replacement) ||
                (index != 5 && item.value(QStringLiteral("originalFontFamily")).toString() != missing))
              return fail("Recovery should replace only the reviewed face and retain its original identity");
          }
          carousel_document.undo();
          if (carousel_document.documentContentHash() != before_missing || carousel_document.missingFontEntries().size() != 6)
            return fail("One Undo should restore the complete missing-font document exactly");
          if (auto* drawer = named(QStringLiteral("carouselCompactInspectorDrawer"))) QMetaObject::invokeMethod(drawer, "close");
          process_layout_events(8);
        }
        QFile receipt(QDir(font_proof_dir).filePath(QStringLiteral("manifest.json")));
        if (!receipt.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return fail("Font proof manifest should save once");
        receipt.write(QJsonDocument(QJsonObject{{"captures", captures}, {"nativeEligible", false},
            {"scope", "Software Qt populated Font Browser and font recovery at 100 and 200 percent; real installed fonts, keyboard search/selection/favorites, persistence, apply/Undo, explicit missing-font review, stale-preview invalidation, cancellation and focus return; isolated controllers"}}).toJson());
        app_shell.setInterfaceScale(prior_scale);
        responsive_root->setProperty("width", prior_width);
        responsive_root->setProperty("height", prior_height);
        carousel_document.createCarouselWithSettings(QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Responsive proof")}, {QStringLiteral("slideCount"), 1}});
        process_layout_events(8);
        if (!dialog_warnings.isEmpty()) return fail(dialog_warnings.join('\n').toUtf8().constData());
        std::cout << "Populated font browser and recovery workflows passed at 100 and 200 percent.\n";
      }

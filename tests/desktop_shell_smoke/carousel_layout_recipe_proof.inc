// Populated recipe library and comparison with the real isolated controller.
      const auto recipe_proof_dir = qEnvironmentVariable("PHOTO_EDITOR_CAROUSEL_LAYOUT_RECIPE_PROOF_DIR");
      if (!recipe_proof_dir.isEmpty()) {
        auto* window = qobject_cast<QQuickWindow*>(responsive_root);
        if (!window || !QDir().mkpath(recipe_proof_dir)) return fail("Recipe proof needs a window and output directory");
        app_shell.completeFirstRunExperience();
        if (auto* guide = find_named_object(responsive_root, QStringLiteral("firstRunGuideDialog"))) QMetaObject::invokeMethod(guide, "close");
        window->show();
        QJsonArray captures;
        QJsonArray exports;
        const auto named = [&](const QString& name) { return find_named_object(responsive_root, name); };
        auto* dialog = named(QStringLiteral("carouselLayoutRecipeDialog"));
        auto* content = dialog->property("contentItem").value<QObject*>()->property("item").value<QObject*>();
        auto* list = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselLayoutRecipeComparison")));
        auto* apply = named(QStringLiteral("carouselApplyLayoutRecipeButton"));
        auto* show_all = named(QStringLiteral("carouselLayoutRecipeShowUnchangedToggle"));
        const auto reveal = [&](QQuickItem* item) {
          auto* scroll = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselInspectorScroll")));
          auto* flickable = scroll->property("contentItem").value<QQuickItem*>();
          if (!item || !flickable || !item_visible(item)) return false;
          (void)window->grabWindow();
          process_layout_events(10);
          flickable->setProperty("contentY", std::max(0.0, flickable->property("contentY").toDouble() + scene_rect(item).top() - scene_rect(scroll).top() - 12.0));
          if (item->objectName() == "carouselLayoutGuidesSection") return true;
          return wait_for_condition([&] { return scene_rect(scroll).adjusted(-1, -1, 1, 1).contains(scene_rect(item)); });
        };
        const auto inspector = [&] {
          carousel_document.clearSelection();
          carousel_workspace->setProperty("carouselInspectorVisible", true);
          carousel_workspace->setProperty("inspectorPrimarySurface", QStringLiteral("board"));
          process_layout_events(8);
          if (carousel_workspace->property("compactWorkspace").toBool()) {
            auto* drawer = named(QStringLiteral("carouselCompactInspectorDrawer"));
            if (!drawer->property("opened").toBool() && !send_activation_key(named(QStringLiteral("carouselCompactInspectorButton")))) return false;
            if (!wait_for_condition([&] { return drawer->property("opened").toBool(); })) return false;
          }
          auto* section = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselLayoutGuidesSection")));
          if (!reveal(section)) return false;
          if (!section->property("expanded").toBool() && !send_activation_key(section)) return false;
          process_layout_events(8);
          return true;
        };
        const auto type_name = [&](const QString& value) {
          auto* field = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselLayoutRecipeNameField")));
          if (!reveal(field)) return false;
          field->forceActiveFocus(Qt::TabFocusReason);
          if (!send_window_key(window, Qt::Key_A, QKeySequence(QKeySequence::SelectAll)[0].keyboardModifiers()) || !send_window_key(window, Qt::Key_Backspace)) return false;
          for (const auto character : value) {
            const auto key = character == QChar(' ') ? Qt::Key_Space : 0;
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, QString(character));
            QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, QString(character));
            QCoreApplication::sendEvent(window, &press);
            QCoreApplication::sendEvent(window, &release);
            process_layout_events(2);
            if (!field->hasActiveFocus()) return false;
          }
          return carousel_workspace->property("layoutRecipeName").toString() == value;
        };
        QQuickItem* opener = nullptr;
        QString recipe_id;
        const auto open_recipe = [&] {
          if (!inspector()) return false;
          opener = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselPreviewLayoutRecipeButton_") + recipe_id));
          if (!reveal(opener) || !send_activation_key(opener)) return false;
          return wait_for_condition([&] { return dialog->property("opened").toBool() &&
              (list->property("count").toInt() > 0 ? list->hasActiveFocus() : window->activeFocusItem() == qobject_cast<QQuickItem*>(named(QStringLiteral("carouselLayoutRecipeCloseButton")))); });
        };
        const auto capture = [&](const QString& state, double scale) {
          QStringList required{QStringLiteral("carouselLayoutRecipeHeader"), QStringLiteral("carouselLayoutRecipeCloseButton"),
              QStringLiteral("carouselLayoutRecipeContext"), QStringLiteral("carouselLayoutRecipeSummary"),
              QStringLiteral("carouselLayoutRecipeComparison"), QStringLiteral("carouselLayoutRecipeFooter"),
              QStringLiteral("carouselApplyLayoutRecipeButton"), QStringLiteral("carouselLayoutRecipeGuidance")};
          if (named(QStringLiteral("carouselLayoutRecipeRefreshButton"))->property("visible").toBool()) required.append(QStringLiteral("carouselLayoutRecipeRefreshButton"));
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
                if (list->property("count").toInt() > 0) {
                  auto* row = list->property("currentItem").value<QQuickItem*>();
                  if (!row || !scene_rect(list).adjusted(-1, -1, 1, 1).contains(scene_rect(row))) return false;
                  for (const auto& prefix : {QStringLiteral("carouselLayoutRecipeBefore_"), QStringLiteral("carouselLayoutRecipeAfter_")}) {
                    auto* value = qobject_cast<QQuickItem*>(named(prefix + content->property("selectedKey").toString()));
                    if (!value || !scene_rect(row).adjusted(-1, -1, 1, 1).contains(scene_rect(value)) || value->height() + 1 < value->property("implicitHeight").toDouble()) return false;
                  }
                }
                const auto overflow = list->property("count").toInt() > 0 && list->property("contentHeight").toDouble() > list->height();
                return named(QStringLiteral("carouselLayoutRecipeScrollBar"))->property("visible").toBool() == overflow;
              })) {
            window->grabWindow().save(QDir(recipe_proof_dir).filePath("failed-bounds.png"));
            std::cerr << "Recipe bounds failed: " << state.toStdString() << " scale=" << scale << '\n';
            return false;
          }
          return capture_visual_proof(window, responsive_root, recipe_proof_dir, state + QStringLiteral("-scale-%1").arg(scale),
              scale == 1.0 ? 1280 : 1024, scale == 1.0 ? 800 : 760, required, &captures);
        };
        for (const auto scale : {1.0, 2.0}) {
          app_shell.setInterfaceScale(scale);
          responsive_root->setProperty("width", scale == 1.0 ? 1280 : 1024);
          responsive_root->setProperty("height", scale == 1.0 ? 800 : 760);
          const auto original_recipe_count = carousel_document.layoutRecipes().size();
          carousel_document.createCarouselWithSettings(QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Layout recipe review")}, {QStringLiteral("documentFormat"), QStringLiteral("4x5")}, {QStringLiteral("slideCount"), 2}});
          carousel_document.setCurrentSlideLayoutTokens(7, 9, 84.0, 96.0, 22.0, 18.0);
          for (const auto& key : {"gridEnabled", "guidesEnabled", "rulersEnabled", "thirdsEnabled", "bleedEnabled"}) carousel_document.setLayoutSetting(key, true);
          for (const auto& key : {"snappingEnabled", "snapToGrid", "aspectLocked"}) carousel_document.setLayoutSetting(key, false);
          const QVariantMap numeric_settings{{"bleed", 32.0}, {"snapThreshold", 14.0}, {"nudgeStep", 2.5}, {"largeNudgeStep", 25.0}, {"guideOpacity", 0.65}, {"guideColor", "#027591"}};
          for (auto setting = numeric_settings.cbegin(); setting != numeric_settings.cend(); ++setting) carousel_document.setLayoutSetting(setting.key(), setting.value());
          carousel_document.applySafeAreaPreset(QStringLiteral("tiktok"));
          carousel_document.addLayoutGuide(QStringLiteral("vertical"), 180.0);
          carousel_document.addLayoutGuide(QStringLiteral("horizontal"), 320.0);
          const auto saved_settings = carousel_document.layoutSettings();
          const auto recipe_name = QStringLiteral("Production layout %1: consistent board geometry, guides and document precision settings for the complete technical sequence. Saved geometry includes margins, gutters, custom guide positions, grid display, safe areas, bleed, snapping distance and nudge settings. Artwork and styling remain independent.").arg(scale);
          const auto before_save = carousel_document.documentContentHash();
          if (!inspector() || !type_name(recipe_name) || !send_window_key(window, Qt::Key_Return) || !wait_for_condition([&] { return carousel_document.layoutRecipes().size() == original_recipe_count + 1; })) return fail("Recipe should save from the real inspector name field");
          if (!named(QStringLiteral("carouselLayoutGuidesSection"))->property("expanded").toBool()) return fail("Return inside a name field should save without collapsing its containing inspector section");
          for (const auto& value : carousel_document.layoutRecipes()) if (value.toMap().value("name").toString() == recipe_name) recipe_id = value.toMap().value("recipeId").toString();
          if (recipe_id.isEmpty() || carousel_document.documentContentHash() != before_save) return fail("Saving a recipe should persist the library without editing the document");
          {
            photo_editor::desktop::CarouselDocumentController persisted;
            bool found = false;
            for (const auto& value : persisted.layoutRecipes()) if (value.toMap().value("recipeId").toString() == recipe_id) found = true;
            if (!found) return fail("The saved recipe should reopen in the isolated library");
          }
          const auto duplicate_typed = type_name(recipe_name);
          const auto duplicate_submitted = duplicate_typed && send_window_key(window, Qt::Key_Enter);
          if (!duplicate_submitted || !wait_for_condition([&] { return !carousel_workspace->property("layoutRecipeSaveError").toString().isEmpty(); }) || carousel_document.layoutRecipes().size() != original_recipe_count + 1) {
            std::cerr << "Duplicate typed=" << duplicate_typed << " submitted=" << duplicate_submitted << " name=" << carousel_workspace->property("layoutRecipeName").toString().toStdString()
                      << " error=" << carousel_workspace->property("layoutRecipeSaveError").toString().toStdString() << " status=" << carousel_document.status().toStdString() << '\n';
            window->grabWindow().save(QDir(recipe_proof_dir).filePath("failed-duplicate.png"));
            return fail("Duplicate recipe names should report an inline error without duplicating library state");
          }
          auto* error = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselLayoutRecipeSaveError")));
          if (!reveal(qobject_cast<QQuickItem*>(named(QStringLiteral("carouselLayoutRecipeNameField")))) || !error || error->height() + 1 < error->property("implicitHeight").toDouble() || !capture_visual_proof(window, responsive_root, recipe_proof_dir, QStringLiteral("recipe-library-duplicate-scale-%1").arg(scale), scale == 1.0 ? 1280 : 1024, scale == 1.0 ? 800 : 760,
              {QStringLiteral("carouselLayoutRecipeNameField"), QStringLiteral("carouselLayoutRecipeSaveError"), QStringLiteral("carouselSaveLayoutRecipeButton")}, &captures)) return fail("Inline save feedback should remain readable at both scales");
          if (!type_name(QString{})) return fail("Clearing the recipe name should clear its save error");
          auto* library_row = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselLayoutRecipeLibraryRow_") + recipe_id));
          auto* library_name = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselLayoutRecipeLibraryName_") + recipe_id));
          if (!reveal(library_row) || !library_name || library_name->height() + 1 < library_name->property("implicitHeight").toDouble() ||
              !capture_visual_proof(window, responsive_root, recipe_proof_dir, QStringLiteral("recipe-library-saved-scale-%1").arg(scale), scale == 1.0 ? 1280 : 1024, scale == 1.0 ? 800 : 760,
                  {library_row->objectName(), library_name->objectName()}, &captures)) return fail("Saved recipe names and their actions should fit the inspector without truncation");
          carousel_document.selectSlide(1);
          carousel_document.resetLayoutSettings();
          carousel_document.setCurrentSlideBackgroundColor(QStringLiteral("#23435b"));
          carousel_document.addShapeItem(QStringLiteral("rectangle"));
          carousel_document.setSelectedShapeFillColor(QStringLiteral("#d7ac56"));
          carousel_document.addTextItem(QStringLiteral("Artwork stays unchanged"));
          carousel_document.setSelectedTextFontFamily(QStringLiteral("system"));
          carousel_document.setExportManifestEnabled(false);
          carousel_document.setExportBoardScope(QStringLiteral("current"));
          const auto export_before = QDir(recipe_proof_dir).filePath(QStringLiteral("pixels-before-%1").arg(scale));
          const auto export_after = QDir(recipe_proof_dir).filePath(QStringLiteral("pixels-after-%1").arg(scale));
          QDir().mkpath(export_before); QDir().mkpath(export_after);
          if (!carousel_document.exportCarouselToDirectory(export_before)) return fail("Recipe artwork should export before preview");
          const auto before_apply = carousel_document.documentContentHash();
          const auto before_items = carousel_document.canvasItems();
          if (!open_recipe() || content->property("allRows").toList().size() != 25 || list->property("count").toInt() < 12 || !apply->property("enabled").toBool() || !capture("recipe-differences", scale)) return fail("Recipe should preview all affected board and document settings through the real library entry");
          if (scale == 2.0) {
            auto* name_scroll = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselLayoutRecipeNameScroll")));
            auto* name_label = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselLayoutRecipeName")));
            auto* flickable = name_scroll->property("contentItem").value<QQuickItem*>();
            if (!flickable || name_scroll->property("contentHeight").toDouble() <= name_scroll->property("availableHeight").toDouble()) return fail("The long recipe fixture must exercise a bounded, scrollable name");
            name_scroll->forceActiveFocus(Qt::TabFocusReason);
            if (!send_window_key(window, Qt::Key_End) || !wait_for_condition([&] { return scene_rect(name_label).bottom() <= scene_rect(name_scroll).bottom() + 1 && flickable->property("contentY").toDouble() > 0; }) ||
                !capture("recipe-name-end", scale) || !send_window_key(window, Qt::Key_Home) || !send_window_key(window, Qt::Key_Down) || !send_window_key(window, Qt::Key_Up)) return fail("A long recipe name should remain keyboard readable without pushing settings or Apply out of the dialog");
          }
          list->forceActiveFocus(Qt::TabFocusReason);
          if (!send_window_key(window, Qt::Key_End) || !capture("recipe-last-guide", scale) || !send_window_key(window, Qt::Key_Up) || !send_window_key(window, Qt::Key_Down)) return fail("Keyboard navigation should expose complete guide values");
          const auto key = content->property("selectedKey").toString();
          if (!send_activation_key(show_all) || !wait_for_condition([&] { return list->property("count").toInt() == 25 && content->property("selectedKey").toString() == key; }) || !capture("recipe-all-settings", scale) || !send_activation_key(show_all) || !wait_for_condition([&] { return content->property("selectedKey").toString() == key; })) return fail("Showing unchanged settings should preserve the same selected setting");
          if (!send_window_key(window, Qt::Key_Escape) || !wait_for_condition([&] { return !dialog->property("visible").toBool() && window->activeFocusItem() == opener; }) || carousel_document.documentContentHash() != before_apply || !carousel_document.layoutRecipePreview().isEmpty()) return fail("Escape should discard only the preview and restore its actual opener");
          if (!open_recipe()) return fail("Recipe preview should reopen");
          carousel_document.setCurrentSlideBackgroundColor(QStringLiteral("#385269"));
          const auto stale_hash = carousel_document.documentContentHash();
          if (!wait_for_condition([&] { return !apply->property("enabled").toBool(); }) || !capture("recipe-stale-document", scale) || !send_activation_key(named(QStringLiteral("carouselLayoutRecipeRefreshButton"))) || !wait_for_condition([&] { return apply->property("enabled").toBool(); }) || carousel_document.documentContentHash() != stale_hash || !capture("recipe-refreshed", scale)) return fail("A stale preview should disable Apply and refresh against the actual document without editing it");
          if (!send_window_key(window, Qt::Key_Escape)) return fail("Refreshed preview should dismiss");
          carousel_document.undo(); process_layout_events(8);
          if (carousel_document.documentContentHash() != before_apply || !open_recipe()) return fail("Cancelling and Undoing the external edit should restore the original document");
          carousel_document.selectSlide(0);
          if (!wait_for_condition([&] { return !apply->property("enabled").toBool(); }) || !send_activation_key(named(QStringLiteral("carouselLayoutRecipeRefreshButton"))) || !wait_for_condition([&] { return carousel_document.layoutRecipePreview().value("boardIndex").toInt() == 0; }) || !capture("recipe-refreshed-board", scale) || !send_window_key(window, Qt::Key_Escape)) return fail("Changing boards should require a refreshed preview with the correct board context");
          carousel_document.selectSlide(1);
          if (!open_recipe() || !send_activation_key(apply) || !wait_for_condition([&] { return !dialog->property("visible").toBool() && window->activeFocusItem() == opener; })) return fail("Apply should dismiss and restore the library entry");
          const auto after_apply = carousel_document.documentContentHash();
          if (after_apply == before_apply || carousel_document.layoutSettings() != saved_settings || carousel_document.selectionDetails().value("layoutColumns").toInt() != 7 || carousel_document.selectionDetails().value("layoutRows").toInt() != 9 || carousel_document.canvasItems().size() != before_items.size()) return fail("The actual Apply action should apply exact recipe settings without removing artwork");
          carousel_document.undo(); process_layout_events(8);
          if (carousel_document.documentContentHash() != before_apply) return fail("One Undo should restore the entire pre-recipe document exactly");
          carousel_document.redo(); process_layout_events(8);
          if (carousel_document.documentContentHash() != after_apply || !carousel_document.exportCarouselToDirectory(export_after)) return fail("Redo should restore the exact recipe change and export it");
          const auto before_files = QDir(export_before).entryList({"*.png"}, QDir::Files);
          const auto after_files = QDir(export_after).entryList({"*.png"}, QDir::Files);
          if (before_files.size() != 1 || before_files != after_files) return fail("Current-board comparison should export one matching file");
          QImage before_pixels(QDir(export_before).filePath(before_files.first()));
          QImage after_pixels(QDir(export_after).filePath(after_files.first()));
          if (before_pixels.isNull() || after_pixels.isNull() || before_pixels != after_pixels) return fail("Applying only a layout recipe should preserve every exported artwork pixel");
          bool artwork_visible = false;
          for (int y = 0; y < before_pixels.height(); y += std::max(1, before_pixels.height() / 40))
            for (int x = 0; x < before_pixels.width(); x += std::max(1, before_pixels.width() / 40))
              if (before_pixels.pixel(x, y) != before_pixels.pixel(0, 0)) artwork_visible = true;
          if (!artwork_visible) return fail("Pixel preservation proof must include rendered artwork beyond its background");
          exports.append(QJsonObject{{"scale", scale}, {"before", QDir(export_before).filePath(before_files.first())}, {"after", QDir(export_after).filePath(after_files.first())}, {"width", before_pixels.width()}, {"height", before_pixels.height()}, {"pixelsEqual", true}});
          if (!open_recipe() || apply->property("enabled").toBool() || list->property("count").toInt() != 0 || !capture("recipe-matching", scale) || !send_activation_key(show_all) || !wait_for_condition([&] { return list->property("count").toInt() == 25; })) return fail("An already matching recipe should show no differences and disable Apply while allowing full review");
          list->forceActiveFocus(Qt::TabFocusReason);
          if (!send_window_key(window, Qt::Key_Home) || !capture("recipe-matching-all", scale) || !send_activation_key(named(QStringLiteral("carouselLayoutRecipeCloseButton")))) return fail("Matching settings should remain reviewable and dismissible");
          if (!inspector()) return fail("Recipe library should remain accessible after Apply");
          auto* more = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselLayoutRecipeMenu_") + recipe_id + "PrimaryAction"));
          if (!reveal(more) || !send_activation_key(more) || !wait_for_condition([&] { auto* menu = named(QStringLiteral("carouselLayoutRecipeMenu_") + recipe_id + "Popup"); return menu && menu->property("opened").toBool(); }) || !send_window_key(window, Qt::Key_Return) || !wait_for_condition([&] { return carousel_document.layoutRecipes().size() == original_recipe_count; })) return fail("The occasional Delete action should operate through its actual recipe menu");
        }
        if (!dialog_warnings.isEmpty()) return fail(dialog_warnings.join('\n').toUtf8().constData());
        QFile receipt(QDir(recipe_proof_dir).filePath("manifest.json"));
        if (!receipt.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return fail("Recipe manifest should save once");
        receipt.write(QJsonDocument(QJsonObject{{"captures", captures}, {"exports", exports}, {"nativeEligible", false}, {"scope", "Qt software recipe library, all affected settings, stale refresh, exact Apply/Undo/Redo, identical exported artwork and keyboard/focus behavior at 100 and 200 percent"}}).toJson(QJsonDocument::Indented));
        app_shell.setInterfaceScale(prior_scale);
        responsive_root->setProperty("width", prior_width);
        responsive_root->setProperty("height", prior_height);
        carousel_document.createCarouselWithSettings(QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Responsive proof")}, {QStringLiteral("slideCount"), 1}});
        process_layout_events(8);
      }

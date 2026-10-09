// Populated design variants through production controls and isolated documents.
      const auto variant_proof_dir = qEnvironmentVariable("PHOTO_EDITOR_CAROUSEL_VARIANT_PROOF_DIR");
      if (!variant_proof_dir.isEmpty()) {
        auto* window = qobject_cast<QQuickWindow*>(responsive_root);
        if (!window || !QDir().mkpath(variant_proof_dir)) return fail("Variant proof needs an output directory");
        app_shell.completeFirstRunExperience();
        if (auto* guide = find_named_object(responsive_root, QStringLiteral("firstRunGuideDialog"))) QMetaObject::invokeMethod(guide, "close");
        window->show();
        QJsonArray captures;
        const auto named = [&](const QString& name) { return find_named_object(responsive_root, name); };
        auto* dialog = named(QStringLiteral("carouselVariantComparisonDialog"));
        auto* content = dialog->property("contentItem").value<QObject*>()->property("item").value<QObject*>();
        auto* preview = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselVariantComparisonPreviews")));
        auto* apply = named(QStringLiteral("carouselVariantComparisonApplyButton"));
        auto* decision = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselVariantComparisonDecision")));
        auto* section = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselDesignVariantsSection")));
        const auto reveal = [&](QQuickItem* item) {
          auto* scroll = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselInspectorScroll")));
          auto* flickable = scroll->property("contentItem").value<QQuickItem*>();
          if (!item || !flickable || !item_visible(item)) return false;
          (void)window->grabWindow();
          process_layout_events(8);
          flickable->setProperty("contentY", std::max(0.0, flickable->property("contentY").toDouble() + scene_rect(item).top() - scene_rect(scroll).top() - 12.0));
          return item == section || wait_for_condition([&] { return scene_rect(scroll).adjusted(-1, -1, 1, 1).contains(scene_rect(item)); });
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
          if (!reveal(section) || (!section->property("expanded").toBool() && !send_activation_key(section))) return false;
          return true;
        };
        const auto capture_baseline = [&](const QString& name) {
          if (!inspector()) return false;
          auto* field = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselVariantComparisonNameField")));
          if (!reveal(field)) return false;
          field->forceActiveFocus(Qt::TabFocusReason);
          if (!send_window_key(window, Qt::Key_A, QKeySequence(QKeySequence::SelectAll)[0].keyboardModifiers()) || !send_window_key(window, Qt::Key_Backspace)) return false;
          for (const auto character : name) {
            const auto key = character == QChar(' ') ? Qt::Key_Space : 0;
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, QString(character));
            QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, QString(character));
            QCoreApplication::sendEvent(window, &press);
            QCoreApplication::sendEvent(window, &release);
            process_layout_events(2);
          }
          return send_window_key(window, Qt::Key_Return) && wait_for_condition([&] { return carousel_document.variantComparisonState().value("active").toBool(); }) && section->property("expanded").toBool();
        };
        QQuickItem* opener = nullptr;
        const auto open_comparison = [&] {
          if (!inspector()) return false;
          opener = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselOpenVariantComparisonButton")));
          return reveal(opener) && send_activation_key(opener) && wait_for_condition([&] {
            return dialog->property("opened").toBool() && preview->hasActiveFocus() && content->property("previewsReady").toBool();
          });
        };
        const auto choose = [&](int index) {
          decision->forceActiveFocus(Qt::TabFocusReason);
          for (int current = decision->property("currentIndex").toInt(); current < index; ++current)
            if (!send_window_key(window, Qt::Key_Down)) return false;
          for (int current = decision->property("currentIndex").toInt(); current > index; --current)
            if (!send_window_key(window, Qt::Key_Up)) return false;
          return decision->property("currentIndex").toInt() == index;
        };
        const auto capture = [&](const QString& state, double scale) {
          QStringList required{"carouselVariantComparisonHeader", "carouselVariantComparisonCloseButton", "carouselVariantComparisonNameScroll",
              "carouselVariantComparisonNavigation", "carouselVariantComparisonPreviews", "carouselVariantComparisonGuidance", "carouselVariantComparisonFooter",
              "carouselVariantComparisonDecision", "carouselVariantComparisonApplyButton", "carouselVariantComparisonZoomMenu"};
          if (named(QStringLiteral("carouselVariantComparisonRefreshButton"))->property("visible").toBool()) required.append("carouselVariantComparisonRefreshButton");
          (void)window->grabWindow(); process_layout_events(8);
          if (!wait_for_condition([&] {
            auto* background = dialog->property("background").value<QQuickItem*>();
            if (!background) return false;
            const auto bounds = scene_rect(background).adjusted(-1, -1, 1, 1);
            for (const auto& name : required) {
              auto* item = qobject_cast<QQuickItem*>(named(name));
              if (!item_visible(item) || !bounds.contains(scene_rect(item))) return false;
            }
            auto* guidance = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselVariantComparisonGuidance")));
            return guidance->height() + 1 >= guidance->property("implicitHeight").toDouble();
          })) { window->grabWindow().save(QDir(variant_proof_dir).filePath("failed-bounds.png")); return false; }
          return capture_visual_proof(window, responsive_root, variant_proof_dir, state + QStringLiteral("-scale-%1").arg(scale), scale == 1.0 ? 1280 : 1024, scale == 1.0 ? 800 : 760, required, &captures);
        };
        const auto rendered_background = [&](const QString& image_name, const QColor& expected) {
          auto* image = qobject_cast<QQuickItem*>(named(image_name));
          if (!image || image->property("status").toInt() != 1) return false;
          const auto rect = scene_rect(image);
          const auto painted_height = image->property("paintedHeight").toDouble();
          const auto point = QPointF(rect.center().x(), rect.top() + (rect.height() - painted_height) / 2.0 + painted_height * 0.12);
          const auto pixels = window->grabWindow();
          const auto ratio = pixels.devicePixelRatio();
          const auto actual = pixels.pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio));
          return std::abs(actual.red() - expected.red()) < 3 && std::abs(actual.green() - expected.green()) < 3 && std::abs(actual.blue() - expected.blue()) < 3;
        };
        const auto apply_choice = [&] {
          return send_activation_key(apply) && wait_for_condition([&] { return !dialog->property("visible").toBool() && !carousel_document.variantComparisonState().value("active").toBool(); });
        };
        for (const auto scale : {1.0, 2.0}) {
          app_shell.setInterfaceScale(scale);
          responsive_root->setProperty("width", scale == 1.0 ? 1280 : 1024);
          responsive_root->setProperty("height", scale == 1.0 ? 800 : 760);
          carousel_document.createCarouselWithSettings(QVariantMap{{"carouselName", "Design variants review"}, {"slideCount", 2}, {"templateKey", "single"}});
          if (!carousel_document.savedDesignVersions().isEmpty() || carousel_document.variantComparisonState().value("active").toBool()) return fail("New carousel should reset both saved versions and active capture from the previous document");
          for (int board = 0; board < 2; ++board) {
            carousel_document.selectSlide(board);
            carousel_document.setCurrentSlideBackgroundColor(board == 0 ? "#234158" : "#027591");
            carousel_document.addTextItem(QStringLiteral("Captured board %1").arg(board + 1));
            carousel_document.setSelectedTextFontFamily(QStringLiteral("system"));
          }
          carousel_document.selectSlide(0);
          const auto baseline_hash = carousel_document.documentContentHash();
          carousel_workspace->setProperty("variantComparisonName", QStringLiteral("Variant A"));
          if (!inspector() || !reveal(section) || !capture_visual_proof(window, responsive_root, variant_proof_dir, QStringLiteral("variant-capture-controls-scale-%1").arg(scale), scale == 1.0 ? 1280 : 1024, scale == 1.0 ? 800 : 760, {"carouselVariantComparisonNameField", "carouselCaptureVariantButton"}, &captures)) return fail("The independent Design variants section should expose the initial capture field and action");
          const auto name = QStringLiteral("Original direction %1: full document with authored typography, board geometry and all artwork retained for a complete design comparison.").arg(scale);
          if (!capture_baseline(name) || carousel_document.documentContentHash() != baseline_hash) return fail("Real name-field capture should preserve authored state and inspector expansion");
          auto* name_label = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselCapturedVariantName")));
          if (!reveal(name_label) || name_label->height() + 1 < name_label->property("implicitHeight").toDouble() || !capture_visual_proof(window, responsive_root, variant_proof_dir, QStringLiteral("variant-captured-library-scale-%1").arg(scale), scale == 1.0 ? 1280 : 1024, scale == 1.0 ? 800 : 760, {"carouselCapturedVariantName"}, &captures)) return fail("Captured name should remain fully readable in its own inspector section");
          if (!open_comparison() || content->property("comparison").toMap().value("different").toBool() || !capture("variant-matching", scale)) return fail("Matching variants should show actual ready board previews");
          const auto return_target = dialog->property("focusReturnItem").value<QQuickItem*>();
          const auto escape_sent = send_window_key(window, Qt::Key_Escape);
          const auto focus_restored = wait_for_condition([&] { return !dialog->property("visible").toBool() && window->activeFocusItem() == opener; });
          if (!escape_sent || !focus_restored || carousel_document.documentContentHash() != baseline_hash || !carousel_document.variantComparisonState().value("active").toBool()) {
            std::cerr << "Variant dismissal sent=" << escape_sent << " restored=" << focus_restored << " visible=" << dialog->property("visible").toBool()
                      << " returnTarget=" << (return_target ? return_target->objectName().toStdString() : "null")
                      << " actualFocus=" << (window->activeFocusItem() ? window->activeFocusItem()->objectName().toStdString() : "null")
                      << " openerVisible=" << opener->isVisible() << " openerEnabled=" << opener->isEnabled()
                      << " hashMatches=" << (carousel_document.documentContentHash() == baseline_hash)
                      << " captureActive=" << carousel_document.variantComparisonState().value("active").toBool() << '\n';
            window->grabWindow().save(QDir(variant_proof_dir).filePath("failed-escape.png"));
            return fail("Escape should preserve document and capture and restore comparison opener focus");
          }
          carousel_document.selectSlide(1);
          carousel_document.setCurrentSlideBackgroundColor("#b4234d");
          const auto current_hash = carousel_document.documentContentHash();
          if (!open_comparison() || !send_window_key(window, Qt::Key_Right) || !wait_for_condition([&] { return dialog->property("boardIndex").toInt() == 1 && content->property("previewsReady").toBool(); }) || !capture("variant-different-board", scale)) return fail("Right key should navigate synchronized actual different board previews");
          auto* zoom = named(QStringLiteral("carouselVariantComparisonZoomMenuPrimaryAction"));
          if (!send_activation_key(zoom) || !wait_for_condition([&] { return named(QStringLiteral("carouselVariantComparisonZoomMenuPopup"))->property("opened").toBool(); }) || !send_window_key(window, Qt::Key_Down) || !send_window_key(window, Qt::Key_Return) || !wait_for_condition([&] { return content->property("previewZoom").toDouble() == 1.0; })) return fail("Comparison zoom should select actual 100 percent source pixels through its menu");
          preview->forceActiveFocus(Qt::TabFocusReason);
          if (!send_window_key(window, Qt::Key_PageDown) || !send_window_key(window, Qt::Key_PageDown) || !wait_for_condition([&] { return named(QStringLiteral("carouselVariantComparisonLeftViewport"))->property("contentY").toDouble() > 0 && std::abs(named(QStringLiteral("carouselVariantComparisonLeftViewport"))->property("contentY").toDouble() - named(QStringLiteral("carouselVariantComparisonRightViewport"))->property("contentY").toDouble()) < 1.0; }) || !send_window_key(window, Qt::Key_PageUp) || !send_window_key(window, Qt::Key_Down) || !send_window_key(window, Qt::Key_Up) || !send_window_key(window, Qt::Key_Down) || !capture("variant-zoomed-details", scale)) return fail("Zoomed previews should pan together with actual Page Down and Page Up keys");
          if (!send_activation_key(zoom) || !wait_for_condition([&] { return named(QStringLiteral("carouselVariantComparisonZoomMenuPopup"))->property("opened").toBool(); }) || !send_window_key(window, Qt::Key_Down) || !send_window_key(window, Qt::Key_Down) || !send_window_key(window, Qt::Key_Return) || !wait_for_condition([&] { return content->property("previewZoom").toDouble() == 2.0; })) return fail("The larger preview zoom should remain selectable through its actual menu");
          preview->forceActiveFocus(Qt::TabFocusReason);
          if (!send_window_key(window, Qt::Key_PageDown) || !send_window_key(window, Qt::Key_PageUp) || carousel_document.documentContentHash() != current_hash) return fail("Zoom and panning should preserve authored document content");
          if (!send_activation_key(zoom) || !wait_for_condition([&] { return named(QStringLiteral("carouselVariantComparisonZoomMenuPopup"))->property("opened").toBool(); }) || !send_window_key(window, Qt::Key_Home) || !send_window_key(window, Qt::Key_Return) || !wait_for_condition([&] { return content->property("previewZoom").toDouble() == 0.0; })) return fail("Fit should restore complete-board review through the zoom menu");
          preview->forceActiveFocus(Qt::TabFocusReason);
          if (!rendered_background("carouselVariantComparisonLeftImage", QColor("#027591")) || !rendered_background("carouselVariantComparisonRightImage", QColor("#b4234d"))) return fail("Rendered comparison should display the actual captured and changed board colors");
          carousel_document.setCurrentSlideBackgroundColor("#6f3cc3");
          const auto stale_hash = carousel_document.documentContentHash();
          if (!wait_for_condition([&] { return !apply->property("enabled").toBool(); }) || !capture("variant-stale", scale) || !send_activation_key(named(QStringLiteral("carouselVariantComparisonRefreshButton"))) || !wait_for_condition([&] { return apply->property("enabled").toBool(); }) || carousel_document.documentContentHash() != stale_hash || !capture("variant-refreshed", scale) || !rendered_background("carouselVariantComparisonRightImage", QColor("#6f3cc3"))) return fail("Refresh should update the actual displayed pixels, guard stale decisions, and preserve authored state");
          if (!choose(0) || !apply_choice() || carousel_document.documentContentHash() != stale_hash || !carousel_document.savedDesignVersions().isEmpty()) return fail("Keep current should preserve the current document without creating versions");
          if (!wait_for_condition([&] { return window->activeFocusItem() == section; })) return fail("Resolving should return focus to Design variants when the comparison entry disappears");
          carousel_document.undo();
          if (carousel_document.documentContentHash() != current_hash) return fail("Keep current should add no hidden Undo action");
          carousel_document.undo();
          if (carousel_document.documentContentHash() != baseline_hash) return fail("Changed backgrounds should each retain exact Undo history");
          if (!capture_baseline(name)) return fail("A new captured comparison should be available");
          carousel_document.selectSlide(1); carousel_document.setCurrentSlideBackgroundColor("#b4234d");
          carousel_document.addSlide(); carousel_document.setCurrentSlideBackgroundColor("#d3a847");
          const auto added_hash = carousel_document.documentContentHash();
          if (!open_comparison() || !send_window_key(window, Qt::Key_End) || !wait_for_condition([&] { return dialog->property("boardIndex").toInt() == 2 && content->property("previewsReady").toBool(); }) || content->property("leftPresent").toBool() || !capture("variant-added-board", scale) || !rendered_background("carouselVariantComparisonRightImage", QColor("#d3a847"))) return fail("End should expose added boards with an explicit absent captured board");
          if (!send_window_key(window, Qt::Key_Home) || !wait_for_condition([&] { return dialog->property("boardIndex").toInt() == 0 && content->property("previewsReady").toBool(); }) || !choose(1) || !capture("variant-restore-decision", scale) || !apply_choice() || carousel_document.documentContentHash() != baseline_hash) return fail("Restore captured should apply the complete reviewed left document");
          carousel_document.undo(); if (carousel_document.documentContentHash() != added_hash) return fail("Restore captured should Undo in one exact step");
          carousel_document.redo(); if (carousel_document.documentContentHash() != baseline_hash) return fail("Restore captured should Redo in one exact step");
          if (!capture_baseline(name)) return fail("Save both fixture should capture the baseline");
          carousel_document.selectSlide(1); carousel_document.setCurrentSlideBackgroundColor("#6f3cc3");
          const auto before_both = carousel_document.documentContentHash();
          if (!open_comparison() || !choose(2) || !capture("variant-save-both-decision", scale) || !apply_choice() || carousel_document.savedDesignVersions().size() != 2) return fail("Save both should use the explicit selected outcome and create two full versions");
          const auto after_both = carousel_document.documentContentHash();
          carousel_document.undo(); if (carousel_document.documentContentHash() != before_both || !carousel_document.savedDesignVersions().isEmpty()) return fail("Save both should Undo the pair in one step");
          carousel_document.redo(); if (carousel_document.documentContentHash() != after_both || carousel_document.savedDesignVersions().size() != 2) return fail("Save both should Redo the exact pair in one step");
          if (!carousel_document.saveDocument()) return fail("Version pair should save");
          {
            photo_editor::desktop::CarouselDocumentController reopened;
            if (!reopened.loadDocument(carousel_document.savedDocumentId()) || reopened.savedDesignVersions().size() != 2 || reopened.documentContentHash() != after_both) return fail("Saved versions and current artwork should survive actual document reload");
          }
          if (!inspector()) return fail("Saved version library should be reachable");
          const auto versions = carousel_document.savedDesignVersions();
          const auto left_id = versions.first().toMap().value("versionId").toString();
          const auto right_id = versions.last().toMap().value("versionId").toString();
          for (const auto& id : {left_id, right_id}) {
            auto* row = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselDesignVersionRow_") + id));
            if (!reveal(row) || !capture_visual_proof(window, responsive_root, variant_proof_dir, QStringLiteral("variant-saved-%1-scale-%2").arg(id == left_id ? "captured" : "current").arg(scale), scale == 1.0 ? 1280 : 1024, scale == 1.0 ? 800 : 760, {row->objectName()}, &captures)) return fail("Full saved version names, metadata, Restore and More should remain reachable");
          }
          auto* restore = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselRestoreDesignVersion_") + left_id));
          if (!reveal(restore) || !send_activation_key(restore) || carousel_document.slides().at(1).toMap().value("backgroundColor").toString() != "#027591" || carousel_document.savedDesignVersions().size() != 2) return fail("Actual Restore should restore captured artwork and preserve the version library");
          carousel_document.undo(); if (carousel_document.documentContentHash() != after_both) return fail("Restore saved version should Undo exactly once");
          auto* more = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselDesignVersionMenu_") + left_id + "PrimaryAction"));
          if (!reveal(more) || !send_activation_key(more) || !wait_for_condition([&] { auto* menu = named(QStringLiteral("carouselDesignVersionMenu_") + left_id + "Popup"); return menu && menu->property("opened").toBool(); }) || !send_window_key(window, Qt::Key_Return) || !wait_for_condition([&] { return carousel_document.savedDesignVersions().size() == 1; }) || carousel_document.savedDesignVersions().first().toMap().value("versionId").toString() != right_id) return fail("Actual More menu should delete only its selected version");
          carousel_document.undo(); if (carousel_document.documentContentHash() != after_both || carousel_document.savedDesignVersions().size() != 2) return fail("Deleting a version should restore the exact library in one Undo");
          if (!capture_baseline("Complete two-board capture")) return fail("Removed board fixture should capture both boards");
          carousel_document.selectSlide(1); carousel_document.removeCurrentSlide();
          const auto removed_hash = carousel_document.documentContentHash();
          if (!open_comparison() || !send_window_key(window, Qt::Key_End) || !wait_for_condition([&] { return dialog->property("boardIndex").toInt() == 1 && content->property("previewsReady").toBool(); }) || content->property("rightPresent").toBool() || !capture("variant-removed-board", scale) || !send_window_key(window, Qt::Key_Escape) || carousel_document.documentContentHash() != removed_hash) return fail("Captured-only boards should remain reviewable without restoring them on Escape");
          if (auto* drawer = named(QStringLiteral("carouselCompactInspectorDrawer"))) QMetaObject::invokeMethod(drawer, "close");
          process_layout_events(8);
        }
        const auto saved_id = carousel_document.savedDocumentId();
        carousel_document.closeCarousel();
        if (carousel_document.hasActiveCarousel() || !carousel_document.savedDesignVersions().isEmpty() || carousel_document.variantComparisonState().value("active").toBool()) return fail("Closing a carousel should clear its visible version library and transient capture");
        {
          photo_editor::desktop::CarouselDocumentController reopened;
          if (!reopened.loadDocument(saved_id) || reopened.savedDesignVersions().size() != 2) return fail("Closing the document should preserve both versions in its saved file");
        }
        if (!dialog_warnings.isEmpty()) return fail(dialog_warnings.join('\n').toUtf8().constData());
        QFile receipt(QDir(variant_proof_dir).filePath("manifest.json"));
        if (!receipt.open(QIODevice::WriteOnly)) return fail("Variant receipt should save");
        receipt.write(QJsonDocument(QJsonObject{{"captures", captures}, {"nativeEligible", false}, {"scope", "Qt software actual design capture, every compared board, displayed preview refresh, decisions, exact Undo/Redo, saved-version persistence/restore/delete and focus at 100 and 200 percent"}}).toJson(QJsonDocument::Indented));
        app_shell.setInterfaceScale(prior_scale);
        responsive_root->setProperty("width", prior_width);
        responsive_root->setProperty("height", prior_height);
        carousel_document.createCarouselWithSettings(QVariantMap{{"carouselName", "Responsive proof"}, {"slideCount", 1}});
        process_layout_events(8);
      }

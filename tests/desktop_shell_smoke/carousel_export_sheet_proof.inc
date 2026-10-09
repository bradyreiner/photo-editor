// Optional populated export review using isolated documents and actual jobs.
      const auto export_proof_dir = qEnvironmentVariable("PHOTO_EDITOR_CAROUSEL_EXPORT_PROOF_DIR");
      if (!export_proof_dir.isEmpty()) {
        auto* window = qobject_cast<QQuickWindow*>(responsive_root);
        if (!window || !QDir().mkpath(export_proof_dir)) return fail("Carousel export proof needs a window and output directory");
        app_shell.completeFirstRunExperience();
        if (auto* guide = find_named_object(responsive_root, QStringLiteral("firstRunGuideDialog"))) QMetaObject::invokeMethod(guide, "close");
        window->show();
        const auto capture_failure = qScopeGuard([&] {
          if (!QFileInfo::exists(QDir(export_proof_dir).filePath(QStringLiteral("manifest.json"))))
            window->grabWindow().save(QDir(export_proof_dir).filePath(QStringLiteral("failed-route.png")));
        });
        QJsonArray captures;
        QJsonArray outputs;
        const auto named = [&](const QString& name) { return find_named_object(responsive_root, name); };
        auto* sheet = named(QStringLiteral("carouselExportSheet"));
        auto* opener = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselOpenExportButtonPrimaryAction")));
        auto* start = named(QStringLiteral("carouselStartExportButton"));
        auto* scroll = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselExportScroll")));
        auto* flickable = scroll->property("contentItem").value<QQuickItem*>();
        const QStringList required{QStringLiteral("carouselExportHeader"), QStringLiteral("carouselExportSummary"),
            QStringLiteral("carouselExportNavigation"), QStringLiteral("carouselExportScroll"),
            QStringLiteral("carouselExportDestination"), QStringLiteral("carouselExportFooter"),
            QStringLiteral("carouselExportCloseButton"), QStringLiteral("carouselChooseExportDirectoryButton")};
        const auto capture = [&](const QString& state, double scale) {
          (void)window->grabWindow();
          process_layout_events(8);
          if (!carousel_document.exportBusy()) {
            QElapsedTimer settle;
            settle.start();
            if (!wait_for_condition([&] { return settle.elapsed() >= 200; })) return false;
          }
          if (state == QStringLiteral("board-order")) {
            auto* list = named(QStringLiteral("carouselExportOrderedBoardReview"));
            if (!wait_for_condition([&] {
                  auto* current = list->property("currentItem").value<QQuickItem*>();
                  auto* highlight = list->property("highlightItem").value<QQuickItem*>();
                  return list->property("currentIndex").toInt() == 2 && current && highlight &&
                      std::abs(scene_rect(current).top() - scene_rect(highlight).top()) < 1.0;
                })) return false;
          }
          if (!wait_for_condition([&] {
                auto* background = sheet->property("background").value<QQuickItem*>();
                if (!background) return false;
                const auto bounds = scene_rect(background).adjusted(-1, -1, 1, 1);
                for (const auto& name : required) {
                  auto* item = qobject_cast<QQuickItem*>(named(name));
                  if (!item_visible(item) || !bounds.contains(scene_rect(item))) return false;
                }
                return scroll->height() >= 60.0 &&
                    std::abs(scene_rect(qobject_cast<QQuickItem*>(named(QStringLiteral("carouselExportHeader")))).right() -
                             scene_rect(qobject_cast<QQuickItem*>(named(QStringLiteral("carouselExportCloseButton")))).right()) < 2.0;
              })) {
            window->grabWindow().save(QDir(export_proof_dir).filePath(QStringLiteral("failed-bounds.png")));
            std::cerr << "Export review bounds failed: " << state.toStdString() << " scale=" << scale << '\n';
            return false;
          }
          return capture_visual_proof(window, responsive_root, export_proof_dir,
              QStringLiteral("export-%1-scale-%2").arg(state).arg(scale), scale == 1.0 ? 1280 : 1024,
              scale == 1.0 ? 800 : 760, required, &captures);
        };
        const auto activate = [&](const QString& name, int key = Qt::Key_Space) { return send_activation_key(named(name), key); };
        const auto displayed_plan_matches = [&] {
          const auto plan = carousel_document.exportOutputPreview();
          for (int index = 0; index < plan.size(); ++index) {
            auto* label = named(QStringLiteral("carouselExportResolvedFilename_%1").arg(index));
            if (!label || label->property("text").toString() != plan.at(index).toMap().value(QStringLiteral("resolvedName")).toString()) return false;
          }
          return true;
        };
        const auto open_sheet = [&] {
          return opener && item_visible(opener) && opener->isEnabled() && send_activation_key(opener) &&
              wait_for_condition([&] { return sheet->property("visible").toBool() &&
                  window->activeFocusItem() == qobject_cast<QQuickItem*>(named(QStringLiteral("carouselExportSettingsTab"))); });
        };
        const auto page = [&](const QString& name, int index) {
          const auto prior_page = sheet->property("activePage").toInt();
          return activate(name) && wait_for_condition([&] { return sheet->property("activePage").toInt() == index && (prior_page == index || flickable->property("contentY").toDouble() < 1.0); });
        };
        const auto focus_in_scroll = [&](const QString& name) {
          auto* item = qobject_cast<QQuickItem*>(named(name));
          if (!wait_for_condition([&] { return item_visible(item); })) {
            std::cerr << "Export control not visible: " << name.toStdString() << '\n';
            return false;
          }
          item->forceActiveFocus(Qt::TabFocusReason);
          if (!wait_for_condition([&] { return scene_rect(scroll).adjusted(-1, -1, 1, 1).contains(scene_rect(item)); })) {
            std::cerr << "Export focus viewport failed: " << name.toStdString() << " y=" << scene_rect(item).top()
                      << " height=" << item->height() << " viewportY=" << scene_rect(scroll).top()
                      << " viewportHeight=" << scroll->height() << " contentY=" << flickable->property("contentY").toDouble() << '\n';
            return false;
          }
          return true;
        };
        const auto type_field = [&](const QString& name, const QString& value) {
          if (!focus_in_scroll(name) || !send_window_key(window, Qt::Key_A, QKeySequence(QKeySequence::SelectAll)[0].keyboardModifiers())) return false;
          for (const auto character : value) {
            QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(character));
            QCoreApplication::sendEvent(window, &press);
            QKeyEvent release(QEvent::KeyRelease, 0, Qt::NoModifier, QString(character));
            QCoreApplication::sendEvent(window, &release);
            process_layout_events(2);
            if (window->activeFocusItem() != qobject_cast<QQuickItem*>(named(name))) return false;
          }
          return send_window_key(window, Qt::Key_Return);
        };
        const auto reveal_text = [&](const QString& name) {
          auto* label = qobject_cast<QQuickItem*>(named(name));
          if (!label || !item_visible(label) || label->property("text").toString().isEmpty() ||
              label->height() + 1.0 < label->property("contentHeight").toDouble()) return false;
          scroll->forceActiveFocus(Qt::TabFocusReason);
          for (int step = 0; step < 120; ++step) {
            const auto bounds = scene_rect(scroll).adjusted(-1, -1, 1, 1);
            if (bounds.contains(scene_rect(label))) return true;
            if (!send_window_key(window, scene_rect(label).top() < bounds.top() ? Qt::Key_Up : Qt::Key_Down)) return false;
            process_layout_events(2);
          }
          return false;
        };
        const auto media_path = QDir(export_proof_dir).filePath(QStringLiteral("generated-export-media.png"));
        QImage media(1024, 1280, QImage::Format_ARGB32);
        quint32 media_noise = 1729U;
        for (int y = 0; y < media.height(); ++y) {
          auto* row = reinterpret_cast<QRgb*>(media.scanLine(y));
          for (int x = 0; x < media.width(); ++x) {
            media_noise = media_noise * 1664525U + 1013904223U;
            row[x] = qRgb(static_cast<int>((media_noise >> 16U) & 255U),
                          static_cast<int>((media_noise >> 8U) & 255U), static_cast<int>(media_noise & 255U));
          }
        }
        if (!media.save(media_path)) return fail("Export lifecycle fixture should save its generated media");
        for (const auto scale : {1.0, 2.0}) {
          app_shell.setInterfaceScale(scale);
          responsive_root->setProperty("width", scale == 1.0 ? 1280 : 1024);
          responsive_root->setProperty("height", scale == 1.0 ? 800 : 760);
          carousel_document.createCarouselWithSettings(QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Delivery review")},
              {QStringLiteral("documentFormat"), QStringLiteral("4x5")}, {QStringLiteral("slideCount"), 3}});
          for (int index = 0; index < 3; ++index) {
            carousel_document.selectSlide(index);
            carousel_document.setCurrentSlideName(QStringLiteral("Delivery board %1").arg(index + 1));
            carousel_document.addTextItem(QStringLiteral("Delivery review %1").arg(index + 1));
            carousel_document.setSelectedTextFontFamily(QStringLiteral("system"));
          }
          carousel_document.setExportDirectory(QString{});
          if (!open_sheet() || start->property("enabled").toBool() || !capture(QStringLiteral("no-destination"), scale))
            return fail("Export should open through its actual workspace control, keep its destination visible, and block an absent destination");
          const auto destination = QDir(export_proof_dir).filePath(QStringLiteral("review-output-%1").arg(scale));
          QDir().mkpath(destination);
          carousel_document.setExportDirectory(destination);
          const auto before_scope = carousel_document.documentContentHash();
          if (!activate(QStringLiteral("carouselExportScope_current")) || carousel_document.exportSummary().value(QStringLiteral("boardCount")).toInt() != 1)
            return fail("Current-board scope should update the exact export plan");
          carousel_document.undo();
          if (carousel_document.documentContentHash() != before_scope) return fail("One Undo should exactly restore the export scope change");
          if (!activate(QStringLiteral("carouselExportScope_selected")) || carousel_document.exportSummary().value(QStringLiteral("boardCount")).toInt() != carousel_document.selectedSlideCount())
            return fail("Selected-board scope should use the actual selected board count");
          carousel_document.undo();
          if (carousel_document.documentContentHash() != before_scope) return fail("Selected-board scope should be exactly reversible with one Undo");
          if (!activate(QStringLiteral("carouselExportReviewBoardsButton"))) return fail("Ordered board review should open through its visible control");
          auto* review = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselExportOrderedBoardReview")));
          if (!focus_in_scroll(QStringLiteral("carouselExportOrderedBoardReview")) || !send_activation_key(review, Qt::Key_End) ||
              !wait_for_condition([&] {
                auto* row = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselExportBoardReviewRow_2")));
                return review->property("currentIndex").toInt() == 2 && item_visible(row) &&
                    scene_rect(review).adjusted(-1, -1, 1, 1).contains(scene_rect(row)) &&
                    scene_rect(scroll).adjusted(-1, -1, 1, 1).contains(scene_rect(row));
              }) || !capture(QStringLiteral("board-order"), scale)) return fail("Exact ordered board filenames should remain keyboard reachable in the viewport at both scales");
          if (!page(QStringLiteral("carouselExportSettingsTab"), 0) || !focus_in_scroll(QStringLiteral("carouselExportReviewBoardsButton")) ||
              !activate(QStringLiteral("carouselExportReviewBoardsButton")) || !focus_in_scroll(QStringLiteral("carouselExportSize_custom")) ||
              !activate(QStringLiteral("carouselExportSize_custom")) || !type_field(QStringLiteral("carouselExportCustomWidthField"), QStringLiteral("320")) ||
              !capture(QStringLiteral("custom-resolution"), scale)) return fail("Custom export resolution should accept actual keyboard input and keep its related controls usable");
          if (carousel_document.exportSizeSettings().value(QStringLiteral("customWidth")).toInt() != 320 ||
              carousel_document.exportSizeSettings().value(QStringLiteral("customHeight")).toInt() != 400)
            return fail("Typed custom resolution should retain the document aspect ratio");
          if (!focus_in_scroll(QStringLiteral("carouselExportFormat_jpeg")) || !activate(QStringLiteral("carouselExportFormat_jpeg")) ||
              !type_field(QStringLiteral("carouselExportQualitySliderValueField"), QStringLiteral("71")) || carousel_document.exportImageQuality() != 71 ||
              named(QStringLiteral("carouselExportTransparencyToggle"))->property("enabled").toBool() || !capture(QStringLiteral("jpeg-quality"), scale))
            return fail("JPEG should expose editable quality and explain its unavailable transparency");
          if (!page(QStringLiteral("carouselExportFilesTab"), 2) || !type_field(QStringLiteral("carouselExportFilenameField"), QStringLiteral("review_{board_number}_v{version}")) ||
              carousel_document.exportFilenamePattern() != QStringLiteral("review_{board_number}_v{version}") ||
              !focus_in_scroll(QStringLiteral("carouselIncreaseExportVersionButton")) || !activate(QStringLiteral("carouselIncreaseExportVersionButton")) ||
              carousel_document.exportVersion() != 2 || !capture(QStringLiteral("files"), scale)) return fail("Filename pattern and version should use actual keyboard controls and update resolved names");
          if (!focus_in_scroll(QStringLiteral("carouselPdfProofToggle")) || !activate(QStringLiteral("carouselPdfProofToggle")) ||
              !carousel_document.exportProofSheetEnabled() || !focus_in_scroll(QStringLiteral("carouselPdfProofNotesToggle")) ||
              !capture(QStringLiteral("review-files"), scale)) return fail("Review PDF and conditional note settings should remain reachable at both scales");
          carousel_document.setExportProofSheetEnabled(false);
          carousel_document.setSelectedTextFontFamily(QStringLiteral("Looksy Missing Delivery Face Q4X9"));
          const auto missing_hash = carousel_document.documentContentHash();
          if (!page(QStringLiteral("carouselExportChecksTab"), 3) || start->property("enabled").toBool() ||
              carousel_document.exportSummary().value(QStringLiteral("blockingIssueCount")).toInt() < 1 || !capture(QStringLiteral("blocked-checks"), scale))
            return fail("A real missing font should block Start and expose scoped checks on their dedicated page");
          auto* first_issue = named(QStringLiteral("carouselExportIssue_0"));
          if (!first_issue || first_issue->property("severity").toString() != QStringLiteral("error") ||
              !first_issue->property("message").toString().contains(QStringLiteral("Looksy Missing Delivery Face Q4X9")) ||
              !reveal_text(QStringLiteral("carouselExportIssue_0Message")) || !capture(QStringLiteral("blocking-message"), scale))
            return fail("Blocking issues should appear before advisory warnings and expose their full message through keyboard scrolling");
          int missing_issue_index = -1;
          const auto issues = carousel_document.exportPreflightIssues();
          for (int index = 0; index < issues.size(); ++index)
            if (issues.at(index).toMap().value(QStringLiteral("code")).toString() == QStringLiteral("missing-font")) missing_issue_index = index;
          if (missing_issue_index < 0) return fail("Missing font should produce a scoped blocking issue");
          const auto expected_item = issues.at(missing_issue_index).toMap().value(QStringLiteral("itemId")).toString();
          if ( !focus_in_scroll(QStringLiteral("carouselExportIssue_0Inspect")) ||
              !activate(QStringLiteral("carouselExportIssue_0Inspect")) ||
              sheet->property("visible").toBool() || carousel_document.selectedItemId() != expected_item ||
              carousel_document.documentContentHash() != missing_hash) return fail("Every scoped issue should offer a keyboard Inspect action that reveals its affected board without editing it");
          carousel_document.undo();
          if (!open_sheet() || !page(QStringLiteral("carouselExportPresetsTab"), 1)) return fail("Delivery presets should remain reachable after issue inspection");
          auto* picker = named(QStringLiteral("carouselExportPresetBrowser"));
          const auto presets = carousel_document.exportPresets();
          while (picker->property("currentIndex").toInt() > 0) if (!send_activation_key(picker, Qt::Key_Up)) return fail("Preset selection should accept keyboard navigation");
          if (named(QStringLiteral("carouselApplyExportPresetButton"))->property("enabled").toBool() || !capture(QStringLiteral("preset-incompatible"), scale))
            return fail("An incompatible ratio preset should explain why Apply is disabled");
          if (!focus_in_scroll(QStringLiteral("carouselExportGuidanceDetailsButton")) ||
              (named(QStringLiteral("carouselExportGuidanceDetailsButton"))->property("text").toString() != QStringLiteral("Hide preset guidance") &&
               !activate(QStringLiteral("carouselExportGuidanceDetailsButton"))))
            return fail("Platform guidance should remain accessible for an incompatible preset");
          for (const auto& part : {QStringLiteral("Heading"), QStringLiteral("Summary"), QStringLiteral("Defaults"), QStringLiteral("SourceDate")}) {
            if (!reveal_text(QStringLiteral("carouselExportGuidance") + part) ||
                !capture(QStringLiteral("platform-guidance-") + part.toLower(), scale))
              return fail("Dated platform guidance should keep every text block readable through keyboard scrolling");
          }
          if (!focus_in_scroll(QStringLiteral("carouselExportGuidanceSourceButton")) ||
              !capture(QStringLiteral("platform-guidance-source-action"), scale))
            return fail("The guidance source action should remain keyboard reachable without opening an external page");
          int review_index = -1;
          for (int index = 0; index < presets.size(); ++index)
            if (presets.at(index).toMap().value(QStringLiteral("presetId")).toString() == QStringLiteral("review-package")) review_index = index;
          if (review_index < 0) return fail("Review package should be an available delivery preset");
          while (picker->property("currentIndex").toInt() < review_index) if (!send_activation_key(picker, Qt::Key_Down)) return fail("Preset selection should accept the review package");
          if (!named(QStringLiteral("carouselApplyExportPresetButton"))->property("enabled").toBool() || !capture(QStringLiteral("preset-review"), scale))
            return fail("The displayed compatible preset should permit an explicit Apply");
          if (!focus_in_scroll(QStringLiteral("carouselExportGuidanceDetailsButton")) ||
              (named(QStringLiteral("carouselExportGuidanceDetailsButton"))->property("text").toString() != QStringLiteral("Hide preset guidance") &&
               !activate(QStringLiteral("carouselExportGuidanceDetailsButton"))) ||
              named(QStringLiteral("carouselExportGuidanceDetailsButton"))->property("text").toString() != QStringLiteral("Hide preset guidance") ||
              !capture(QStringLiteral("preset-guidance"), scale)) return fail("Dated preset guidance should remain available through its details control");
          for (const auto& part : {QStringLiteral("Heading"), QStringLiteral("Summary"), QStringLiteral("Defaults")}) {
            if (!reveal_text(QStringLiteral("carouselExportGuidance") + part) ||
                !capture(QStringLiteral("guidance-") + part.toLower(), scale))
              return fail("Expanded preset guidance should keep every full text block readable through actual keyboard scrolling");
          }
          scroll->forceActiveFocus(Qt::TabFocusReason);
          for (int step = 0; step < 120; ++step) {
            if (!send_window_key(window, Qt::Key_Down)) return fail("Current export settings should support keyboard scrolling");
            process_layout_events(2);
            if (flickable->property("contentY").toDouble() >= flickable->property("contentHeight").toDouble() - flickable->height() - 1.0) break;
          }
          const auto resolved_count = carousel_document.exportPresetState().value(QStringLiteral("resolvedRows")).toList().size();
          auto* last_setting = qobject_cast<QQuickItem*>(named(QStringLiteral("carouselExportResolvedSetting_%1").arg(resolved_count - 1)));
          if (!last_setting || !scene_rect(scroll).adjusted(-1, -1, 1, 1).contains(scene_rect(last_setting)) ||
              !capture(QStringLiteral("preset-settings-lower"), scale)) return fail("The complete resolved export settings should remain readable at both scales");
          const auto before_preset = carousel_document.documentContentHash();
          if (!focus_in_scroll(QStringLiteral("carouselApplyExportPresetButton")) || !activate(QStringLiteral("carouselApplyExportPresetButton")) ||
              sheet->property("activePage").toInt() != 0 || carousel_document.exportPresetState().value(QStringLiteral("activePresetId")).toString() != QStringLiteral("review-package"))
            return fail("Explicit preset Apply should update the real settings and return to Output");
          carousel_document.undo();
          if (carousel_document.documentContentHash() != before_preset || !send_window_key(window, Qt::Key_Escape))
            return fail("One Undo should restore the complete preset change, and Escape should dismiss review");
          process_layout_events(8);
          if (window->activeFocusItem() != opener) return fail("Export dismissal should return to its original invoking workspace action");

          // Actual worker lifecycle, including a closed/reopened busy sheet.
          const auto job_directory = QDir(export_proof_dir).filePath(QStringLiteral("job-output-%1").arg(scale));
          QDir().mkpath(job_directory);
          carousel_document.createCarouselWithSettings(QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Worker delivery")},
              {QStringLiteral("documentFormat"), QStringLiteral("4x5")}, {QStringLiteral("slideCount"), 1}});
          if (!carousel_document.dropMediaOnTarget(media_path, QStringLiteral("photo"), 0, QStringLiteral("layer-stack"),
              QStringLiteral("primary"), 0.5, 0.5, QStringLiteral("Generated export media")))
            return fail("Worker lifecycle should use actual generated image content");
          for (int index = 0; index < 12; ++index) {
            if (index > 0) carousel_document.duplicateCurrentSlide();
            carousel_document.selectSlide(index);
            carousel_document.setCurrentSlideName(QStringLiteral("Worker board %1").arg(index + 1));
            carousel_document.setCurrentSlideBackgroundColor(index % 2 == 0 ? QStringLiteral("#233348") : QStringLiteral("#f4eee2"));
            if (index == 0) {
              carousel_document.addTextItem(QStringLiteral("Worker delivery"));
              carousel_document.setSelectedTextFontFamily(QStringLiteral("system"));
            }
          }
          carousel_document.setExportDirectory(job_directory);
          carousel_document.setExportFilenamePattern(QStringLiteral("delivery_{board_number}"));
          carousel_document.setExportManifestEnabled(false);
          carousel_document.setExportProofSheetEnabled(false);
          carousel_document.setExportPartialCleanupPolicy(QStringLiteral("keep"));
          carousel_document.setExportSizeMode(QStringLiteral("custom"));
          carousel_document.setExportCustomPixelSize(3072, 3840);
          carousel_document.setExportImageFormat(QStringLiteral("png"));
          carousel_document.setExportImageQuality(71);
          carousel_document.setExportColorSpace(QStringLiteral("sRGB"));
          if (!open_sheet() || !activate(QStringLiteral("carouselStartExportButton")) || !carousel_document.exportBusy())
            return fail("Export should start through its actual footer and real asynchronous worker");
          if (!activate(QStringLiteral("carouselExportCloseButton")) || !open_sheet() || !carousel_document.exportBusy() ||
              !capture(QStringLiteral("running-reopened"), scale) || !carousel_document.exportBusy()) return fail("Closing export review must leave a usable workspace entry for reopening a running job");
          if (!wait_for_condition([&] { return carousel_document.exportProgress().value(QStringLiteral("completedCount")).toInt() >= 1; }, 20000) ||
              !activate(QStringLiteral("carouselCancelExportButton")) ||
              !wait_for_condition([&] { return !carousel_document.exportBusy(); }, 20000) ||
              carousel_document.exportProgress().value(QStringLiteral("state")).toString() != QStringLiteral("cancelled") ||
              carousel_document.exportProgress().value(QStringLiteral("partialFileCount")).toInt() <= 0 ||
              item_visible(qobject_cast<QQuickItem*>(named(QStringLiteral("carouselExportReadyStatus")))) ||
              !capture(QStringLiteral("cancelled"), scale)) return fail("Stopping a real job should expose truthful cancellation and retained partial files");
          if (!page(QStringLiteral("carouselExportFilesTab"), 2) || !focus_in_scroll(QStringLiteral("carouselCleanPartialExportButton")) ||
              !capture(QStringLiteral("partial-files"), scale) || !activate(QStringLiteral("carouselCleanPartialExportButton")) ||
              carousel_document.exportProgress().value(QStringLiteral("partialFileCount")).toInt() != 0 ||
              !wait_for_condition(displayed_plan_matches) ||
              !QDir(job_directory).entryList({QStringLiteral("*.png"), QStringLiteral("*.jpg")}, QDir::Files).isEmpty())
            return fail("The explicit cleanup action should remove only this isolated job's retained partial outputs");
          if (!activate(QStringLiteral("carouselRetryExportButton")) || !wait_for_condition([&] { return !carousel_document.exportBusy(); }, 30000) ||
              carousel_document.exportProgress().value(QStringLiteral("state")).toString() != QStringLiteral("completed") ||
              carousel_document.exportProgress().value(QStringLiteral("completedCount")).toInt() != 12 ||
              item_visible(qobject_cast<QQuickItem*>(named(QStringLiteral("carouselExportReadyStatus")))) ||
              !wait_for_condition(displayed_plan_matches) ||
              !capture(QStringLiteral("completed"), scale)) return fail("Retry should deliver the complete actual job and show the completed result");
          const auto files = QDir(job_directory).entryList({QStringLiteral("*.png"), QStringLiteral("*.jpg")}, QDir::Files, QDir::Name);
          if (files.size() != 12) return fail("Completed export should deliver exactly one encoded image per reviewed board");
          for (const auto& name : files) {
            const auto path = QDir(job_directory).filePath(name);
            const QImage encoded(path);
            if (encoded.isNull() || encoded.size() != QSize(3072, 3840)) return fail("Delivered images should decode at their exact configured resolution");
            outputs.append(QJsonObject{{"path", path}, {"width", encoded.width()}, {"height", encoded.height()}, {"scale", scale}});
          }
          const auto unavailable_directory = QDir(export_proof_dir).filePath(QStringLiteral("unavailable-destination-%1").arg(scale));
          carousel_document.setExportDirectory(unavailable_directory);
          if (!activate(QStringLiteral("carouselStartExportButton")) || carousel_document.exportBusy() ||
              carousel_document.exportProgress().value(QStringLiteral("state")).toString() != QStringLiteral("failed") ||
              item_visible(qobject_cast<QQuickItem*>(named(QStringLiteral("carouselExportReadyStatus")))) ||
              !capture(QStringLiteral("failed-destination"), scale)) return fail("An unavailable destination should show the actual failure and recovery actions");
          // Restore a valid destination with a small scope to exercise recovery
          // without re-encoding the complete delivered workload.
          carousel_document.setExportDirectory(job_directory);
          carousel_document.setExportBoardScope(QStringLiteral("current"));
          carousel_document.setExportImageFormat(QStringLiteral("jpeg"));
          carousel_document.setExportFilenamePattern(QStringLiteral("recovered_{board_number}"));
          if (!activate(QStringLiteral("carouselRetryExportButton")) || !wait_for_condition([&] { return !carousel_document.exportBusy(); }, 30000) ||
              carousel_document.exportProgress().value(QStringLiteral("state")).toString() != QStringLiteral("completed"))
            return fail("Retry should recover an actual destination failure using the corrected settings");
          const auto recovered = QDir(job_directory).entryList({QStringLiteral("recovered_*.jpg")}, QDir::Files);
          if (recovered.size() != 1) return fail("Corrected retry should deliver one actual JPEG");
          const auto recovered_path = QDir(job_directory).filePath(recovered.front());
          const QImage recovered_image(recovered_path);
          if (recovered_image.isNull() || recovered_image.size() != QSize(3072, 3840)) return fail("Recovered JPEG should decode at its configured resolution");
          outputs.append(QJsonObject{{"path", recovered_path}, {"width", recovered_image.width()}, {"height", recovered_image.height()}, {"scale", scale}});
          if (!send_window_key(window, Qt::Key_Escape)) return fail("Completed export review should dismiss by keyboard");
          process_layout_events(8);
          if (window->activeFocusItem() != opener) return fail("Completed export dismissal should return invoking focus");
        }
        QFile manifest(QDir(export_proof_dir).filePath(QStringLiteral("manifest.json")));
        if (!manifest.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return fail("Carousel export proof manifest should save once");
        manifest.write(QJsonDocument(QJsonObject{{"captures", captures}, {"outputs", outputs}, {"generatedMedia", media_path}, {"nativeEligible", false},
            {"scope", "Software Qt export review at 100 and 200 percent; real keyboard settings, scoped checks, compatible/incompatible presets and Undo; actual PNG background export, busy reopening, stop/partial cleanup/retry; actual destination failure and corrected JPEG retry; decoded delivered PNG/JPEG images; isolated controllers and documents"}}).toJson());
        app_shell.setInterfaceScale(prior_scale);
        responsive_root->setProperty("width", prior_width);
        responsive_root->setProperty("height", prior_height);
        carousel_document.createCarouselWithSettings(QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Responsive proof")}, {QStringLiteral("slideCount"), 1}});
        process_layout_events(8);
        if (!dialog_warnings.isEmpty()) return fail(dialog_warnings.join('\n').toUtf8().constData());
        std::cout << "Populated Carousel export settings and worker lifecycle passed at 100 and 200 percent.\n";
      }

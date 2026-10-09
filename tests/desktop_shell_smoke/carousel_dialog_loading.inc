// Exercise deferred contents through the real workspace with isolated controllers.
    {
      const auto dialog_proof_dir = qEnvironmentVariable("PHOTO_EDITOR_CAROUSEL_DIALOG_PROOF_DIR");
      QJsonArray dialog_proof_manifest;
      auto* dialog_proof_window = qobject_cast<QQuickWindow*>(responsive_root);
      const auto prior_scale = app_shell.interfaceScale();
      const auto prior_width = responsive_root->property("width");
      const auto prior_height = responsive_root->property("height");
      if (!dialog_proof_dir.isEmpty()) {
        if (!dialog_proof_window || !QDir().mkpath(dialog_proof_dir))
          return fail("Carousel dialog review should have a window and output directory");
        app_shell.completeFirstRunExperience();
        if (auto* guide = find_named_object(responsive_root, QStringLiteral("firstRunGuideDialog")))
          QMetaObject::invokeMethod(guide, "close");
        app_shell.setInterfaceScale(1.0);
        responsive_root->setProperty("width", 1280);
        responsive_root->setProperty("height", 800);
        dialog_proof_window->show();
        process_layout_events(8);
      }
      QStringList dialog_warnings;
      const auto warning_connection = QObject::connect(
          &engine, &QQmlEngine::warnings, &engine,
          [&](const QList<QQmlError>& warnings) {
            for (const auto& warning : warnings) dialog_warnings.append(warning.toString());
          });
      const auto disconnect_warnings = qScopeGuard([&] { QObject::disconnect(warning_connection); });
      const QStringList dialog_names{
          QStringLiteral("carouselVariantComparisonDialog"),
          QStringLiteral("carouselLayoutRecipeDialog"),
          QStringLiteral("carouselClipboardPasteDialog"),
          QStringLiteral("carouselRepeatedElementLinkDialog"),
          QStringLiteral("carouselCommandPalette"),
          QStringLiteral("carouselPreflightIgnoreDialog"),
          QStringLiteral("carouselBrandApplicationDialog"),
          QStringLiteral("carouselBrandLintDialog"),
          QStringLiteral("carouselBrandKitEditorDialog"),
          QStringLiteral("carouselReadingOrderDialog"),
          QStringLiteral("carouselDocumentStructureDialog"),
          QStringLiteral("carouselBoardRangeCommandDialog"),
          QStringLiteral("carouselNeighborComparisonDialog"),
          QStringLiteral("carouselContinuityPreviewDialog"),
          QStringLiteral("carouselStatusNoticesDialog"),
          QStringLiteral("carouselRecoveryDialog"),
          QStringLiteral("carouselRelinkDialog"),
          QStringLiteral("carouselCopyProofDialog"),
          QStringLiteral("carouselFontBrowserDialog"),
          QStringLiteral("carouselFontRecoveryDialog"),
          QStringLiteral("carouselExportSheet"),
          QStringLiteral("carouselCutoutRefineDialog"),
          QStringLiteral("carouselTemplatePackUpdateDialog"),
          QStringLiteral("carouselSequenceTemplateDialog"),
          QStringLiteral("carouselStoryboardDialog"),
          QStringLiteral("carouselRepeatGridDialog"),
          QStringLiteral("carouselSelectionBatchDialog"),
          QStringLiteral("carouselMediaUsageDialog"),
          QStringLiteral("carouselVideoPreviewDialog"),
          QStringLiteral("carouselMediaAutofillDialog"),
          QStringLiteral("carouselDesignPackageImportDialog")};
      for (const auto& name : dialog_names) {
        if (!dialog_proof_dir.isEmpty() && name == QStringLiteral("carouselClipboardPasteDialog")) {
          carousel_document.addTextItem(QStringLiteral("Carousel dialog review"));
          if (!carousel_document.copySelectedBoards() || !carousel_document.previewClipboardPaste())
            return fail("Carousel paste review should use a real clipboard preview");
        }
        if (!dialog_proof_dir.isEmpty() && name == QStringLiteral("carouselBoardRangeCommandDialog"))
          QMetaObject::invokeMethod(carousel_workspace, "setBoardRangeOperation", Q_ARG(QVariant, QVariant("background")));
        auto* dialog = find_named_object(responsive_root, name);
        auto* loader = dialog == nullptr ? nullptr : dialog->property("contentItem").value<QObject*>();
        if (loader == nullptr || dialog->property("contentCreated").toBool() ||
            loader->property("item").value<QObject*>() != nullptr)
          return fail(("Carousel dialog constructed content before opening: " + name).toUtf8().constData());
        if (!QMetaObject::invokeMethod(dialog, "open"))
          return fail(("Cannot open Carousel dialog: " + name).toUtf8().constData());
        process_layout_events(4);
        QPointer<QObject> content = loader->property("item").value<QObject*>();
        if (!content || !dialog->property("contentCreated").toBool() ||
            !dialog->property("visible").toBool())
          return fail(("Carousel dialog failed to load on demand: " + name).toUtf8().constData());
        if (!dialog_proof_dir.isEmpty() && !capture_visual_proof(
              dialog_proof_window, responsive_root, dialog_proof_dir, name,
              1280, 800, {name}, &dialog_proof_manifest))
          return fail("Carousel dialog review capture should save");
        if (!QMetaObject::invokeMethod(dialog, "close"))
          return fail(("Cannot close Carousel dialog: " + name).toUtf8().constData());
        process_layout_events(4);
        if (!content || loader->property("item").value<QObject*>() != content.data())
          return fail(("Carousel dialog lost its retained controls: " + name).toUtf8().constData());
        QMetaObject::invokeMethod(dialog, "open");
        process_layout_events(4);
        if (loader->property("item").value<QObject*>() != content.data())
          return fail(("Carousel dialog recreated controls on reopen: " + name).toUtf8().constData());
        QMetaObject::invokeMethod(dialog, "close");
        process_layout_events(4);
      }
      if (!dialog_warnings.isEmpty())
        return fail(dialog_warnings.join('\n').toUtf8().constData());
#include "carousel_brand_editor_proof.inc"
#include "carousel_font_dialog_proof.inc"
#include "carousel_export_sheet_proof.inc"
#include "carousel_order_structure_proof.inc"
#include "carousel_layout_recipe_proof.inc"
#include "carousel_variant_comparison_proof.inc"
      if (!dialog_proof_dir.isEmpty()) {
        const auto check_dialog_bounds = [&](QObject* dialog, const QStringList& names) {
          auto* background = dialog->property("background").value<QQuickItem*>();
          if (!background) return false;
          const auto bounds = background->mapRectToScene(background->boundingRect()).adjusted(-1, -1, 1, 1);
          for (const auto& name : names) {
            auto* item = qobject_cast<QQuickItem*>(find_named_object(responsive_root, name));
            if (!item || !bounds.contains(item->mapRectToScene(item->boundingRect()))) {
              std::cerr << "Carousel dialog overflow: " << name.toStdString() << '\n';
              return false;
            }
          }
          return true;
        };
        auto* opener = qobject_cast<QQuickItem*>(find_named_object(responsive_root, QStringLiteral("carouselToolRailSelectButton")));
        if (!opener) return fail("Carousel dialog proof should have a real invoking workspace control");
        for (const auto scale : {1.0, 2.0}) {
          app_shell.setInterfaceScale(scale);
          responsive_root->setProperty("width", scale == 1.0 ? 1280 : 1024);
          responsive_root->setProperty("height", scale == 1.0 ? 800 : 760);
          carousel_document.createCarouselWithSettings(
              QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Range review")},
                          {QStringLiteral("slideCount"), 3}});
          const auto before_range = carousel_document.documentContentHash();
          carousel_workspace->setProperty("boardRangeScope", QStringLiteral("current"));
          opener->forceActiveFocus(Qt::TabFocusReason);
          QMetaObject::invokeMethod(carousel_workspace, "openBoardRangeCommand");
          process_layout_events(8);
          auto* range_dialog = find_named_object(responsive_root, QStringLiteral("carouselBoardRangeCommandDialog"));
          auto* operation = find_named_object(responsive_root, QStringLiteral("carouselBoardRangeOperationCombo"));
          auto* value = find_named_object(responsive_root, QStringLiteral("carouselBoardRangeValueCombo"));
          auto* all = find_named_object(responsive_root, QStringLiteral("carouselBoardRangeScope_all"));
          if (!range_dialog || !operation || !value || dialog_proof_window->activeFocusItem() != qobject_cast<QQuickItem*>(operation))
            return fail("Board range should initially focus its named operation selector");
          const QStringList operations{QStringLiteral("background"), QStringLiteral("template"), QStringLiteral("brand"),
              QStringLiteral("spacing"), QStringLiteral("text-style")};
          for (int index = 0; index < operations.size(); ++index) {
            if (!operation->setProperty("currentIndex", index) ||
                !QMetaObject::invokeMethod(operation, "activated", Q_ARG(int, index)))
              return fail("Every board range operation should activate through the selector");
            process_layout_events(4);
            QVariant options;
            if (carousel_workspace->property("boardRangeOperation").toString() != operations.at(index) ||
                !QMetaObject::invokeMethod(carousel_workspace, "boardRangeValueOptions", Q_RETURN_ARG(QVariant, options),
                    Q_ARG(QVariant, QVariant(operations.at(index)))) || value->property("count").toInt() != options.toList().size())
              return fail("Changing the operation should expose exactly that operation's available values");
            if ((value->property("count").toInt() > 0 && value->property("currentValue").toString() !=
                    carousel_workspace->property("boardRangeValue").toString()) ||
                (value->property("count").toInt() == 0 && value->property("currentIndex").toInt() != -1))
              return fail("The value selector should show the actual preview value, including after its choices change");
          }
          operation->setProperty("currentIndex", 0);
          QMetaObject::invokeMethod(operation, "activated", Q_ARG(int, 0));
          process_layout_events(4);
          if (!send_activation_key(all) || !value->setProperty("currentIndex", 1) ||
              !QMetaObject::invokeMethod(value, "activated", Q_ARG(int, 1)))
            return fail("Board range operation, range, and value should remain operable through their actual controls");
          process_layout_events(8);
          auto* apply_range = find_named_object(responsive_root, QStringLiteral("carouselApplyBoardRangeCommandButton"));
          const auto preview = carousel_workspace->property("boardRangePreview").toMap();
          const QStringList range_required{QStringLiteral("carouselBoardRangeHeader"), QStringLiteral("carouselBoardRangeSettings"),
              QStringLiteral("carouselBoardRangePreview"), QStringLiteral("carouselBoardRangeFooter"), QStringLiteral("carouselBoardRangeCloseButton"),
              QStringLiteral("carouselApplyBoardRangeCommandButton")};
          if (!apply_range || !apply_range->property("enabled").toBool() ||
              preview.value(QStringLiteral("changedBoardCount")).toInt() != 3 ||
              !check_dialog_bounds(range_dialog, range_required) ||
              !capture_visual_proof(dialog_proof_window, responsive_root, dialog_proof_dir,
                  QStringLiteral("board-range-ready-scale-%1").arg(scale), scale == 1.0 ? 1280 : 1024,
                  scale == 1.0 ? 800 : 760, range_required, &dialog_proof_manifest))
            return fail("Board range preview should show the exact changed boards and contain its controls at both scales");
          auto* settings_viewport = qobject_cast<QQuickItem*>(find_named_object(responsive_root, QStringLiteral("carouselBoardRangeSettings")));
          const auto viewport_bounds = settings_viewport->mapRectToScene(settings_viewport->boundingRect()).adjusted(-1, -1, 1, 1);
          for (auto* field : {operation, value, all}) {
            auto* item = qobject_cast<QQuickItem*>(field);
            if (!item || !viewport_bounds.contains(item->mapRectToScene(item->boundingRect()))) {
              if (item) {
                const auto rect = item->mapRectToScene(item->boundingRect());
                auto* content = range_dialog->property("contentItem").value<QObject*>()->property("item").value<QQuickItem*>();
                std::cerr << "Range field " << item->objectName().toStdString() << " bounds=" << rect.x() << ',' << rect.y() << ',' << rect.width() << ',' << rect.height()
                          << " viewport=" << viewport_bounds.x() << ',' << viewport_bounds.y() << ',' << viewport_bounds.width() << ',' << viewport_bounds.height()
                          << " dialogHeight=" << range_dialog->property("height").toDouble() << " contentImplicit=" << (content ? content->implicitHeight() : -1) << '\n';
              }
              return fail("Ready board range choices should be visible together at both UI scales");
            }
          }
          const auto chosen_background = carousel_workspace->property("boardRangeValue").toString();
          if (!send_activation_key(apply_range)) return fail("Board range should apply by keyboard");
          process_layout_events(8);
          for (const auto& board : carousel_document.slides())
            if (board.toMap().value(QStringLiteral("backgroundColor")).toString() != chosen_background)
              return fail("Board range action should change every reviewed board");
          if (range_dialog->property("visible").toBool() || dialog_proof_window->activeFocusItem() != opener)
            return fail("Board range apply should dismiss and restore visible workspace focus");
          carousel_document.undo();
          if (carousel_document.documentContentHash() != before_range)
            return fail("One Undo should restore the board range document exactly");

          carousel_document.createCarouselWithSettings(
              QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Paste review")},
                          {QStringLiteral("slideCount"), 1}});
          carousel_document.addTextItem(QStringLiteral("Authored clipboard text"));
          const auto before_paste = carousel_document.documentContentHash();
          if (!carousel_document.copySelectedBoards()) return fail("Paste proof should copy a real board");
          opener->forceActiveFocus(Qt::TabFocusReason);
          QMetaObject::invokeMethod(carousel_workspace, "openClipboardPasteReview");
          process_layout_events(8);
          auto* paste_dialog = find_named_object(responsive_root, QStringLiteral("carouselClipboardPasteDialog"));
          auto* preserve = qobject_cast<QQuickItem*>(find_named_object(responsive_root, QStringLiteral("carouselClipboardPreserveSourceButton")));
          auto* apply_paste = find_named_object(responsive_root, QStringLiteral("carouselApplyClipboardPasteButton"));
          const QStringList paste_required{QStringLiteral("carouselClipboardPasteHeader"), QStringLiteral("carouselClipboardPasteSummary"),
              QStringLiteral("carouselClipboardPasteSettings"), QStringLiteral("carouselClipboardPasteFooter"),
              QStringLiteral("carouselClipboardPasteCloseButton"), QStringLiteral("carouselApplyClipboardPasteButton")};
          if (!paste_dialog || !preserve || dialog_proof_window->activeFocusItem() != preserve ||
              !apply_paste || !apply_paste->property("enabled").toBool() ||
              !check_dialog_bounds(paste_dialog, paste_required) ||
              !capture_visual_proof(dialog_proof_window, responsive_root, dialog_proof_dir,
                  QStringLiteral("clipboard-ready-scale-%1").arg(scale), scale == 1.0 ? 1280 : 1024,
                  scale == 1.0 ? 800 : 760, paste_required, &dialog_proof_manifest) || !send_activation_key(apply_paste))
            return fail("Paste review should contain its actual content, styling, and apply controls at both scales");
          process_layout_events(8);
          if (carousel_document.slideCount() != 2 || paste_dialog->property("visible").toBool() ||
              dialog_proof_window->activeFocusItem() != opener)
            return fail("Paste should add the reviewed board and restore workspace focus");
          carousel_document.undo();
          if (carousel_document.documentContentHash() != before_paste)
            return fail("One Undo should restore the paste document exactly");

          const auto missing_media_path = runtime_root.filePath(QStringLiteral("clipboard-media-%1.png").arg(scale));
          QImage clipboard_media(64, 64, QImage::Format_RGB32);
          clipboard_media.fill(Qt::blue);
          if (!clipboard_media.save(missing_media_path)) return fail("Paste resource proof should create its own media");
          carousel_document.assignPrimaryImage(missing_media_path, QStringLiteral("Clipboard resource"));
          if (!carousel_document.copySelectedBoards() || !QFile::remove(missing_media_path))
            return fail("Paste resource proof should retain a clipboard reference to its unavailable media");
          const auto before_missing_review = carousel_document.documentContentHash();
          opener->forceActiveFocus(Qt::TabFocusReason);
          QMetaObject::invokeMethod(carousel_workspace, "openClipboardPasteReview");
          process_layout_events(8);
          if (carousel_document.clipboardPastePreview().value(QStringLiteral("missingAssetCount")).toInt() != 1 ||
              !check_dialog_bounds(paste_dialog, paste_required) ||
              !capture_visual_proof(dialog_proof_window, responsive_root, dialog_proof_dir,
                  QStringLiteral("clipboard-missing-media-scale-%1").arg(scale), scale == 1.0 ? 1280 : 1024,
                  scale == 1.0 ? 800 : 760, paste_required, &dialog_proof_manifest))
            return fail("Paste review should expose unavailable media while preserving contained actions at both scales");
          if (!send_window_key(dialog_proof_window, Qt::Key_Escape))
            return fail("Missing-resource paste review should dismiss by Escape");
          process_layout_events(8);
          if (paste_dialog->property("visible").toBool() || dialog_proof_window->activeFocusItem() != opener ||
              carousel_document.documentContentHash() != before_missing_review)
            return fail("Cancelling resource review should preserve the document and restore workspace focus");
        }
        QFile receipt(QDir(dialog_proof_dir).filePath(QStringLiteral("manifest.json")));
        if (!receipt.open(QIODevice::WriteOnly | QIODevice::NewOnly))
          return fail("Carousel dialog review manifest should save once");
        receipt.write(QJsonDocument(QJsonObject{{"captures", dialog_proof_manifest},
            {"scope", "Qt Carousel dialog loading and retained controls; generic fixture states except real board clipboard preview"},
            {"nativeEligible", false}}).toJson());
        app_shell.setInterfaceScale(prior_scale);
        responsive_root->setProperty("width", prior_width);
        responsive_root->setProperty("height", prior_height);
        carousel_document.createCarouselWithSettings(
            QVariantMap{{QStringLiteral("carouselName"), QStringLiteral("Responsive proof")},
                        {QStringLiteral("slideCount"), 1}});
        process_layout_events(8);
      }
      std::cout << "All 31 Carousel dialog contents load on demand and retain their instances on reopen.\n";
    }

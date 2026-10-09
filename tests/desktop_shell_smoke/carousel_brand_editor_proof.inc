// Optional populated Brand Kit proof in the isolated responsive fixture.
      const auto brand_proof_dir = qEnvironmentVariable("PHOTO_EDITOR_BRAND_EDITOR_PROOF_DIR");
      if (!brand_proof_dir.isEmpty()) {
        auto* window = qobject_cast<QQuickWindow*>(responsive_root);
        if (!window || !QDir().mkpath(brand_proof_dir))
          return fail("Brand Kit review requires a window and output directory");
        app_shell.completeFirstRunExperience();
        if (auto* guide = find_named_object(responsive_root, QStringLiteral("firstRunGuideDialog")))
          QMetaObject::invokeMethod(guide, "close");
        window->show();
        auto* dialog = find_named_object(responsive_root, QStringLiteral("carouselBrandKitEditorDialog"));
        auto* loader = dialog->property("contentItem").value<QObject*>();
        auto* content = loader->property("item").value<QObject*>();
        auto* opener = qobject_cast<QQuickItem*>(find_named_object(responsive_root, QStringLiteral("carouselToolRailSelectButton")));
        QJsonArray captures;
        const auto field = [&](const QString& name) { return find_named_object(responsive_root, name); };
        const auto select_page = [&](int index) {
          if (!send_activation_key(field(QStringLiteral("carouselBrandKitPage_%1").arg(index)))) return false;
          process_layout_events(8);
          return content->property("currentSection").toInt() == index;
        };
        const auto type_text = [&](const QString& name, const QString& value) {
          QPointer<QQuickItem> item = qobject_cast<QQuickItem*>(field(name));
          if (!item || !item_visible(item)) return false;
          item->forceActiveFocus(Qt::TabFocusReason);
          if (!send_window_key(window, Qt::Key_A, QKeySequence(QKeySequence::SelectAll)[0].keyboardModifiers())) return false;
          for (const auto character : value) {
            QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(character));
            QCoreApplication::sendEvent(window, &press);
            QKeyEvent release(QEvent::KeyRelease, 0, Qt::NoModifier, QString(character));
            QCoreApplication::sendEvent(window, &release);
            process_layout_events(2);
            if (!item || field(name) != item.data() || window->activeFocusItem() != item.data()) return false;
          }
          return item->property("text").toString() == value;
        };
        const auto commit_text = [&](const QString& name, const QString& value) {
          auto* item = field(name);
          return item && item->setProperty("text", value) && QMetaObject::invokeMethod(item, "editingFinished");
        };
        const QStringList required{QStringLiteral("carouselBrandKitHeader"), QStringLiteral("carouselBrandKitNavigation"),
            QStringLiteral("carouselBrandKitViewport"), QStringLiteral("carouselBrandKitFooter"),
            QStringLiteral("carouselBrandKitCloseButton"), QStringLiteral("carouselBrandKitSaveButton")};
        const auto capture = [&](const QString& state, double scale) {
          if (!wait_for_condition([&] {
                auto* background = dialog->property("background").value<QQuickItem*>();
                if (!background) return false;
                const auto bounds = background->mapRectToScene(background->boundingRect()).adjusted(-1, -1, 1, 1);
                for (const auto& name : required) {
                  auto* item = qobject_cast<QQuickItem*>(field(name));
                  if (!item || !item_visible(item) || !bounds.contains(item->mapRectToScene(item->boundingRect()))) return false;
                }
                return true;
              })) {
            std::cerr << "Unsettled Brand Kit bounds: " << state.toStdString() << " scale=" << scale << '\n';
            window->grabWindow().save(QDir(brand_proof_dir).filePath(QStringLiteral("failed-bounds.png")));
            return false;
          }
          auto* header = qobject_cast<QQuickItem*>(field(QStringLiteral("carouselBrandKitHeader")));
          auto* close = qobject_cast<QQuickItem*>(field(QStringLiteral("carouselBrandKitCloseButton")));
          if (std::abs(header->mapRectToScene(header->boundingRect()).right() -
                       close->mapRectToScene(close->boundingRect()).right()) > 2.0) return false;
          return capture_visual_proof(window, responsive_root, brand_proof_dir,
              QStringLiteral("brand-%1-scale-%2").arg(state).arg(scale), scale == 1.0 ? 1280 : 1024,
              scale == 1.0 ? 800 : 760, required, &captures);
        };
        const auto capture_tail = [&](const QString& state, const QString& last_control, double scale) {
          auto* viewport = qobject_cast<QQuickItem*>(field(QStringLiteral("carouselBrandKitViewport")));
          auto* flickable = viewport->property("contentItem").value<QQuickItem*>();
          if (!flickable || !wait_for_condition([&] { return flickable->property("contentHeight").toDouble() > flickable->height(); }))
            return false;
          viewport->forceActiveFocus(Qt::TabFocusReason);
          for (int step = 0; step < 120; ++step) {
            if (!send_window_key(window, Qt::Key_Down)) return false;
            process_layout_events(2);
            if (flickable->property("contentY").toDouble() >= flickable->property("contentHeight").toDouble() - flickable->height() - 1.0)
              break;
          }
          auto* last = qobject_cast<QQuickItem*>(field(last_control));
          const auto bounds = viewport->mapRectToScene(viewport->boundingRect()).adjusted(-1, -1, 1, 1);
          if (flickable->property("contentY").toDouble() <= 0.0 || !last ||
              !bounds.contains(last->mapRectToScene(last->boundingRect()))) return false;
          return capture(state, scale);
        };
        for (const auto scale : {1.0, 2.0}) {
          app_shell.setInterfaceScale(scale);
          responsive_root->setProperty("width", scale == 1.0 ? 1280 : 1024);
          responsive_root->setProperty("height", scale == 1.0 ? 800 : 760);
          process_layout_events(8);
          carousel_document.beginBrandKitEditor();
          carousel_document.setBrandKitDraftField(QStringLiteral("name"), QString{});
          opener->forceActiveFocus(Qt::TabFocusReason);
          QMetaObject::invokeMethod(dialog, "open");
          process_layout_events(8);
          auto* save = field(QStringLiteral("carouselBrandKitSaveButton"));
          if (!wait_for_condition([&] { return window->activeFocusItem() ==
                  qobject_cast<QQuickItem*>(field(QStringLiteral("carouselBrandKitNameField"))) &&
                  !save->property("enabled").toBool(); }))
            return fail("Brand Kit should focus its name and disable saving while it is empty at both scales");
          if (!capture(QStringLiteral("name-required"), scale))
            return fail("Brand Kit should contain its entry controls and blocked save at both scales");
          if (!type_text(QStringLiteral("carouselBrandKitNameField"), QStringLiteral("Northline Studio")) ||
              !type_text(QStringLiteral("carouselBrandField_primaryName"), QStringLiteral("Paper")) ||
              carousel_document.brandKitDraft().value(QStringLiteral("primaryName")).toString() != QStringLiteral("Paper"))
            return fail("Brand Kit typing should retain its field and focus while updating the draft");
          if (!type_text(QStringLiteral("carouselBrandField_accentColor"), QStringLiteral("#24b6b0")) ||
              !send_window_key(window, Qt::Key_Return))
            return fail("Brand Kit palette colors should commit through their fields");
          if (!send_activation_key(field(QStringLiteral("carouselBrandKitPage_0")))) return fail("Palette should select");
          process_layout_events(8);
          if (!capture(QStringLiteral("palette"), scale)) return fail("Populated Brand Kit palette should fit");
          if (!capture_tail(QStringLiteral("palette-lower"), QStringLiteral("carouselBrandField_campaignAccentColor"), scale) || !select_page(0))
            return fail("Lower palette variants should be reachable by keyboard at both scales");
          if (scale == 1.0) {
            auto* settings = field(QStringLiteral("carouselBrandKitSettings"));
            const auto split_width = settings->property("width").toDouble();
            if (!send_activation_key(field(QStringLiteral("carouselBrandKitPreviewToggle")))) return fail("Preview should toggle");
            if (!wait_for_condition([&] { return settings->property("width").toDouble() > split_width; }))
              return fail("Hiding the preview should let the settings layout settle at its expanded width");
            if (field(QStringLiteral("carouselBrandKitPreviewPanel"))->property("visible").toBool() ||
                settings->property("width").toDouble() <= split_width || !capture(QStringLiteral("palette-preview-hidden"), scale))
              return fail("Hiding the side preview should return space to the editor");
            send_activation_key(field(QStringLiteral("carouselBrandKitPreviewToggle")));
          }
          if (!select_page(1) || !type_text(QStringLiteral("carouselBrandField_titleWeight"), QStringLiteral("semibold")) ||
              !capture(QStringLiteral("typography"), scale)) return fail("Typography should retain typed role values and fit");
          if (!select_page(2) || !send_activation_key(field(QStringLiteral("carouselBrandKitLogoRequired"))))
            return fail("Required logo should toggle through its keyboard control");
          process_layout_events(8);
          if (save->property("enabled").toBool() || !capture(QStringLiteral("logo-required"), scale))
            return fail("Missing required logo should keep its save explanation visible");
          const auto logo_path = runtime_root.filePath(QStringLiteral("brand-review-logo-%1.png").arg(scale));
          QImage logo(64, 64, QImage::Format_RGB32);
          logo.fill(QColor(QStringLiteral("#24b6b0")));
          if (!logo.save(logo_path) || !type_text(QStringLiteral("carouselBrandField_logoPath"), logo_path))
            return fail("Logo path typing should retain the field while validation updates");
          process_layout_events(8);
          if (!save->property("enabled").toBool() || !capture(QStringLiteral("logo-ready"), scale))
            return fail("Existing required logo should enable saving and retain a contained editing layout");
          if (scale == 2.0 && !capture_tail(QStringLiteral("logos-lower"), QStringLiteral("carouselBrandField_logoMinimumContrast"), scale))
            return fail("Logo usage rules should remain reachable at enlarged scale");
          if (!select_page(3) || !commit_text(QStringLiteral("carouselBrandField_cornerMedium"), QStringLiteral("0")) ||
              !commit_text(QStringLiteral("carouselBrandField_strokeHairline"), QStringLiteral("0")))
            return fail("Geometry should accept zero radii and stroke widths");
          process_layout_events(8);
          if (field(QStringLiteral("carouselBrandLivePreview"))->property("radius").toDouble() != 0.0 ||
              !capture(QStringLiteral("geometry"), scale)) return fail("Geometry preview should preserve zero values");
          if (scale == 2.0 && !capture_tail(QStringLiteral("geometry-lower"), QStringLiteral("carouselBrandField_spacingXl"), scale))
            return fail("Lower geometry values should remain reachable at enlarged scale");
          if (scale == 2.0 && !select_page(4)) return fail("Compact editor should expose Preview and checks");
          if (!field(QStringLiteral("carouselBrandKitReminders"))->property("expanded").toBool() &&
              !send_activation_key(field(QStringLiteral("carouselBrandKitReminders")))) return fail("Reminders should expand by keyboard");
          process_layout_events(8);
          if (!field(QStringLiteral("carouselBrandKitReminders"))->property("expanded").toBool() ||
              !capture(QStringLiteral("preview-checks"), scale)) return fail("Preview should retain readable optional reminders");
          if (scale == 2.0 && !capture_tail(QStringLiteral("preview-checks-lower"), QStringLiteral("carouselBrandKitReminders"), scale))
            return fail("Expanded reminders should remain reachable at enlarged scale");
          if (!send_activation_key(save)) return fail("Brand Kit should save through its real action");
          process_layout_events(8);
          if (dialog->property("visible").toBool() || carousel_document.brandKitEditorActive() || window->activeFocusItem() != opener)
            return fail("Brand Kit save should dismiss, finish the draft, and restore workspace focus");
          QString saved_id;
          QVariantMap saved_kit;
          for (const auto& entry : carousel_document.brandKits()) {
            const auto kit = entry.toMap();
            if (kit.value(QStringLiteral("name")).toString() == QStringLiteral("Northline Studio")) {
              saved_id = kit.value(QStringLiteral("brandId")).toString();
              saved_kit = kit;
            }
          }
          if (saved_id.isEmpty()) return fail("Saved Brand Kit should appear in the library");
          carousel_document.beginBrandKitEditor(saved_id);
          if (carousel_document.brandKitDraft().value(QStringLiteral("primaryName")).toString() != QStringLiteral("Paper") ||
              carousel_document.brandKitDraft().value(QStringLiteral("accentColor")).toString() != QStringLiteral("#24b6b0") ||
              carousel_document.brandKitDraft().value(QStringLiteral("cornerMedium")).toDouble() != 0.0 ||
              carousel_document.brandKitDraft().value(QStringLiteral("logoPath")).toString() != logo_path)
            return fail("Brand Kit palette, logo, and zero geometry should survive save and reopen");
          opener->forceActiveFocus(Qt::TabFocusReason);
          QMetaObject::invokeMethod(dialog, "open");
          process_layout_events(8);
          if (!type_text(QStringLiteral("carouselBrandKitNameField"), QStringLiteral("Discard this draft")) ||
              !send_window_key(window, Qt::Key_Escape)) return fail("Edited Brand Kit should cancel with Escape");
          process_layout_events(8);
          bool unchanged = false;
          for (const auto& entry : carousel_document.brandKits())
            if (entry.toMap().value(QStringLiteral("brandId")).toString() == saved_id) unchanged = entry.toMap() == saved_kit;
          if (!unchanged || carousel_document.brandKitEditorActive() || window->activeFocusItem() != opener)
            return fail("Cancelled Brand Kit draft should leave the saved kit unchanged and return focus");
          carousel_document.deleteBrandKit(saved_id);
        }
        QFile receipt(QDir(brand_proof_dir).filePath(QStringLiteral("manifest.json")));
        if (!receipt.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return fail("Brand Kit manifest should save once");
        receipt.write(QJsonDocument(QJsonObject{{"captures", captures}, {"nativeEligible", false},
            {"scope", "Populated software Qt Brand Kit editor at 1x and 2x; keyboard typing, preview, validation, save/reopen and cancel; isolated controllers"}}).toJson());
        app_shell.setInterfaceScale(prior_scale);
        responsive_root->setProperty("width", prior_width);
        responsive_root->setProperty("height", prior_height);
        process_layout_events(8);
        if (!dialog_warnings.isEmpty()) return fail(dialog_warnings.join('\n').toUtf8().constData());
        std::cout << "Populated Brand Kit editing, keyboard focus, validation, save/reopen and cancel passed at 1x and 2x.\n";
      }

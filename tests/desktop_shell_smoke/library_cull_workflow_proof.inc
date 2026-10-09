  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_CULL_WORKFLOW")) {
    QVector<photo_editor::desktop::CatalogAssetItem> cull_fixtures;
    QStringList fixture_paths;
    for (int index = 0; index < 3; ++index) {
      const auto path = runtime_root.filePath(QStringLiteral("cull-photo-%1.png").arg(index + 1));
      QImage image(1200, 800, QImage::Format_RGB32);
      image.fill(index == 0 ? QColor(QStringLiteral("#d48457"))
                            : index == 1 ? QColor(QStringLiteral("#557d9e"))
                                         : QColor(QStringLiteral("#738e64")));
      if (!image.save(path)) return fail("Cull proof should create full-resolution photo fixtures");
      fixture_paths.push_back(path);
      auto asset = make_asset(QStringLiteral("cull-photo-%1").arg(index + 1), path,
                              QUrl::fromLocalFile(path).toString());
      if (index < 2) {
        asset.burst_key = QStringLiteral("cull-proof-burst");
        asset.burst_size = 2;
        asset.burst_position = index + 1;
      }
      cull_fixtures.push_back(asset);
    }
    catalog_assets.setMediaCollectionFilter(QStringLiteral("images"));
    catalog_assets.setMediaTypeFilter(QStringLiteral("all"));
    catalog_assets.setReviewFilter(QStringLiteral("all"));
    catalog_assets.setMinimumRatingFilter(0);
    catalog_assets.setSortOrder(QStringLiteral("imported-desc"));
    catalog_assets.replaceAssetsForTesting(cull_fixtures);
    catalog_assets.selectIndex(0);
    app_shell.setCullingAutoAdvance(true);

    engine.load(QUrl::fromLocalFile(main_qml_path));
    if (engine.rootObjects().isEmpty()) return fail("Cull proof should load Main.qml");
    auto* cull_root = engine.rootObjects().constFirst();
    auto* cull_window = qobject_cast<QQuickWindow*>(cull_root);
    if (!cull_window) return fail("Cull proof requires a native quick window");
    cull_root->setProperty("currentWorkspace", cull_root->property("libraryWorkspaceIndex"));
    if (!wait_for_condition([&]() {
          auto* images_tab = find_named_item(cull_root, QStringLiteral("libraryImagesCollectionButton"));
          return item_visible(images_tab) && !cull_root->property("workspaceTransitionActive").toBool();
        }, 15000)) return fail("Cull proof must open the image Library route");

    const auto proof_dir = qEnvironmentVariable("PHOTO_EDITOR_VISUAL_PROOF_DIR",
                                               runtime_root.filePath(QStringLiteral("cull-proof")));
    if (!QDir().mkpath(proof_dir)) return fail("Cull proof output directory should be writable");
    QJsonArray cull_manifest;
    const auto capture_cull = [&](const QString& state, const QStringList& required,
                                  int width = 1280, int height = 800) {
      return capture_visual_proof(cull_window, cull_root, proof_dir, state,
                                  width, height, required, &cull_manifest);
    };
    const auto source_uses_path = [](const QUrl& source, const QString& file_path) {
      const auto encoded_path = QString::fromLatin1(QUrl::toPercentEncoding(file_path));
      return source.query(QUrl::FullyEncoded).contains(
          QStringLiteral("path=") + encoded_path);
    };
    const auto activate_cull_control = [&](const QString& object_name) {
      return send_activation_key(find_named_item(cull_root, object_name));
    };
    const auto cull_geometry_is_usable = [&]() {
      auto* cull_view = find_named_item(cull_root, QStringLiteral("libraryCullView"));
      auto* cull_viewer = find_named_item(cull_root, QStringLiteral("libraryCullViewer"));
      auto* action_panel = find_named_item(cull_root, QStringLiteral("libraryCullActionPanel"));
      if (!item_visible(cull_view) || !item_visible(cull_viewer) || !item_visible(action_panel)) return false;
      const auto page_bounds = scene_rect(cull_view);
      const auto viewer_bounds = scene_rect(cull_viewer);
      const auto action_bounds = scene_rect(action_panel);
      if (!contains_with_tolerance(page_bounds, viewer_bounds) ||
          !contains_with_tolerance(page_bounds, action_bounds) ||
          overlaps_with_tolerance(viewer_bounds, action_bounds)) return false;
      const QStringList control_names{
          QStringLiteral("libraryCullHeader"), QStringLiteral("libraryCullProgress"),
          QStringLiteral("libraryCullUnmarkedButton"), QStringLiteral("libraryCullResumeButton"),
          QStringLiteral("libraryCullCompareButton"), QStringLiteral("libraryCullZoomControls"),
          QStringLiteral("libraryCullZoomOutButton"), QStringLiteral("libraryCullZoomFitButton"),
          QStringLiteral("libraryCullZoom100Button"), QStringLiteral("libraryCullZoomInButton"),
          QStringLiteral("libraryCullPickButton"), QStringLiteral("libraryCullRejectButton"),
          QStringLiteral("libraryCullRating0Button"), QStringLiteral("libraryCullRating1Button"),
          QStringLiteral("libraryCullRating2Button"), QStringLiteral("libraryCullRating3Button"),
          QStringLiteral("libraryCullRating4Button"), QStringLiteral("libraryCullRating5Button"),
          QStringLiteral("libraryCullClearReviewButton"), QStringLiteral("libraryCullAutoAdvanceButton"),
          QStringLiteral("libraryCullPreviousButton"), QStringLiteral("libraryCullNextButton"),
          QStringLiteral("libraryCullFilmstrip"), QStringLiteral("libraryCullFilmstripNavigation")};
      for (const auto& name : control_names) {
        auto* control = find_named_item(cull_root, name);
        if (item_visible(control) && !contains_with_tolerance(page_bounds, scene_rect(control))) return false;
      }
      return true;
    };
    const auto compare_geometry_is_usable = [&]() {
      if (!cull_geometry_is_usable()) return false;
      auto* cull_viewer = find_named_item(cull_root, QStringLiteral("libraryCullViewer"));
      auto* left = find_named_item(cull_root, QStringLiteral("libraryCullCompareImageLeft"));
      auto* right = find_named_item(cull_root, QStringLiteral("libraryCullCompareImageRight"));
      if (!item_visible(left) || !item_visible(right)) return false;
      const auto viewer_bounds = scene_rect(cull_viewer);
      return viewer_bounds.contains(scene_rect(left).center()) &&
             viewer_bounds.contains(scene_rect(right).center());
    };
    auto* library = find_named_item(cull_root, QStringLiteral("libraryStudioWorkspace"));
    if (!library || !activate_cull_control(QStringLiteral("studioLibraryCullButton")) ||
        !wait_for_condition([&]() {
          auto* viewer = find_named_item(cull_root, QStringLiteral("libraryCullViewer"));
          auto* original = find_named_item(cull_root, QStringLiteral("libraryCullOriginalImage"));
          return item_visible(viewer) && item_visible(original) &&
                 original->property("paintedWidth").toDouble() > 0.0 &&
                 find_named_item(cull_root, QStringLiteral("libraryCullFilmstrip")) != nullptr;
        }, 10000)) {
      return fail("Cull mode should open its original-photo viewer and filmstrip");
    }
    const auto original_source = find_named_item(cull_root, QStringLiteral("libraryCullOriginalImage"));
    const auto source_url = original_source->property("source").toUrl();
    if (source_url.scheme() != QStringLiteral("image") ||
        source_url.host() != QStringLiteral("library-original") ||
        !source_uses_path(source_url, fixture_paths.at(0)) ||
        std::abs(original_source->property("implicitWidth").toDouble() - 1200.0) > 1.0 ||
        std::abs(original_source->property("implicitHeight").toDouble() - 800.0) > 1.0 ||
        catalog_assets.mediaTypeFilter() != QStringLiteral("photos") ||
        catalog_assets.assetCount() != 3 ||
        !capture_cull(QStringLiteral("cull-fit-original"),
                      {QStringLiteral("libraryCullViewer"), QStringLiteral("libraryCullOriginalImage"),
                       QStringLiteral("libraryCullFilmstrip"), QStringLiteral("libraryCullPickButton"),
                       QStringLiteral("libraryCullRejectButton")})) {
      return fail("Cull mode should decode the original source at full dimensions and show review controls");
    }
    if (!cull_geometry_is_usable())
      return fail("Cull review controls should fit beside the original viewer at 1280 by 800");
    if (!capture_cull(QStringLiteral("cull-compact-layout"),
                      {QStringLiteral("libraryCullView"), QStringLiteral("libraryCullHeader"),
                       QStringLiteral("libraryCullActionPanel"), QStringLiteral("libraryCullViewer"),
                       QStringLiteral("libraryCullFilmstrip"), QStringLiteral("libraryCullRating5Button")},
                      900, 700) || !cull_geometry_is_usable()) {
      return fail("Cull review controls should remain inside the review page at 900 by 700");
    }

    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER"))
      activate_native_test_application();
    cull_window->raise();
    cull_window->requestActivate();
    if (!wait_for_condition([&]() {
          if (!cull_window->isActive()) {
            cull_window->raise();
            cull_window->requestActivate();
          }
          return cull_window->isActive() && QGuiApplication::focusWindow() == cull_window &&
                 QGuiApplication::applicationState() == Qt::ApplicationActive;
        }, 5000)) {
      const auto* focus_window = QGuiApplication::focusWindow();
      std::cerr << "Cull activation diagnostic: windowActive=" << cull_window->isActive()
                << " focusWindowIsCull=" << (focus_window == cull_window)
                << " applicationState=" << static_cast<int>(QGuiApplication::applicationState()) << '\n';
      return fail("Cull native window should be active before keyboard shortcut qualification");
    }
    process_layout_events(6);
    library->forceActiveFocus(Qt::OtherFocusReason);
    if (!wait_for_condition([&]() {
          return library->hasActiveFocus() && cull_window->activeFocusItem() == library;
        }, 2000)) return fail("Cull workspace should own keyboard focus before the native shortcut proof");
    auto* library_pick_shortcut = find_named_object(cull_root, QStringLiteral("libraryPickShortcut"));
    const auto send_cull_key = [&](int key) {
      key_native_test_window(cull_window, key, Qt::NoModifier);
      QCoreApplication::processEvents();
      return true;
    };
    if (!send_cull_key(Qt::Key_P) ||
        !wait_for_condition([&]() {
          return catalog_assets.selectedAssetId() == QStringLiteral("cull-photo-2") &&
                 catalog_assets.assetAt(0).value(QStringLiteral("picked")).toBool();
        }, 3000)) {
      auto* active_focus_item = cull_window->activeFocusItem();
      const auto first_asset = catalog_assets.assetAt(0);
      const auto* focus_window = QGuiApplication::focusWindow();
      std::cerr << "Cull P diagnostic: selectedId=" << catalog_assets.selectedAssetId().toStdString()
                << " selectedPicked=" << catalog_assets.selectedPicked()
                << " firstId=" << first_asset.value(QStringLiteral("assetId")).toString().toStdString()
                << " firstPicked=" << first_asset.value(QStringLiteral("picked")).toBool()
                << " autoAdvance=" << app_shell.cullingAutoAdvance()
                << " windowActive=" << cull_window->isActive()
                << " focusWindowIsCull=" << (focus_window == cull_window)
                << " applicationState=" << static_cast<int>(QGuiApplication::applicationState())
                << " workspaceActive=" << library->property("workspaceActive").toBool()
                << " workspaceFocus=" << library->hasActiveFocus()
                << " focusItem=" << (active_focus_item ? active_focus_item->objectName().toStdString() : "<none>")
                << " pickShortcutExists=" << (library_pick_shortcut != nullptr)
                << " pickShortcutEnabled=" << (library_pick_shortcut && library_pick_shortcut->property("enabled").toBool())
                << " pickShortcutSequence=" << (library_pick_shortcut ? library_pick_shortcut->property("sequence").toString().toStdString() : "<missing>")
                << " sortOrder=" << catalog_assets.sortOrder().toStdString()
                << " projectFilter=" << catalog_assets.projectFilter().toStdString()
                << " reviewFilter=" << catalog_assets.reviewFilter().toStdString() << '\n';
      return fail("P should pick the current photo and advance exactly one visible asset");
    }
    if (!send_cull_key(Qt::Key_X) ||
        !wait_for_condition([&]() {
          return catalog_assets.selectedAssetId() == QStringLiteral("cull-photo-3") &&
                 catalog_assets.assetAt(1).value(QStringLiteral("rejected")).toBool();
        }, 3000)) return fail("X should reject the current photo and advance exactly one visible asset");
    if (!send_cull_key(Qt::Key_5) ||
        catalog_assets.selectedRating() != 5 ||
        catalog_assets.selectedAssetId() != QStringLiteral("cull-photo-3"))
      return fail("The 5 key should set the selected photo rating without skipping the last item");
    if (!send_cull_key(Qt::Key_0) || catalog_assets.selectedRating() != 0 ||
        catalog_assets.selectedAssetId() != QStringLiteral("cull-photo-3"))
      return fail("The 0 key should clear the rating while respecting the end of the review queue");
    if (!activate_cull_control(QStringLiteral("libraryCullRating5Button")) ||
        catalog_assets.selectedRating() != 5 ||
        catalog_assets.selectedAssetId() != QStringLiteral("cull-photo-3"))
      return fail("The pointer 5-star control should match the keyboard rating range");
    if (!activate_cull_control(QStringLiteral("libraryCullRating3Button")) ||
        catalog_assets.selectedRating() != 3 ||
        catalog_assets.selectedAssetId() != QStringLiteral("cull-photo-3"))
      return fail("The pointer rating control should use the same exact rating and advancement rules");
    if (!activate_cull_control(QStringLiteral("libraryCullRating0Button")) ||
        catalog_assets.selectedRating() != 0 ||
        catalog_assets.selectedAssetId() != QStringLiteral("cull-photo-3"))
      return fail("The pointer 0 rating control should clear stars and match keyboard behavior");
    if (!send_cull_key(Qt::Key_U) || catalog_assets.selectedCullStatus() != QStringLiteral("unmarked"))
      return fail("U should clear the current photo review state");
    if (!activate_cull_control(QStringLiteral("libraryCullRating2Button")) ||
        catalog_assets.selectedRating() != 2 ||
        !activate_cull_control(QStringLiteral("libraryCullClearReviewButton")) ||
        catalog_assets.selectedCullStatus() != QStringLiteral("unmarked"))
      return fail("The pointer clear-review action should clear an active rating");

    app_shell.setCullingAutoAdvance(false);
    catalog_assets.selectIndex(0);
    catalog_assets.clearSelectedCullState();
    if (!activate_cull_control(QStringLiteral("libraryCullPickButton")) ||
        catalog_assets.selectedAssetId() != QStringLiteral("cull-photo-1") ||
        !catalog_assets.selectedPicked())
      return fail("Pointer Pick should mark the current photo without advancing when Auto Advance is off");
    if (!activate_cull_control(QStringLiteral("libraryCullUnmarkedButton")) ||
        catalog_assets.reviewFilter() != QStringLiteral("unmarked") || catalog_assets.assetCount() != 1)
      return fail("The unmarked queue should filter the current shoot without hiding its remaining photo");
    if (!activate_cull_control(QStringLiteral("libraryCullUnmarkedButton")) ||
        catalog_assets.reviewFilter() != QStringLiteral("all") || catalog_assets.assetCount() != 3)
      return fail("The unmarked queue should restore all review states");

    for (int index = 0; index < catalog_assets.assetCount(); ++index) {
      catalog_assets.selectIndex(index);
      catalog_assets.clearSelectedCullState();
    }
    catalog_assets.selectIndex(0);
    catalog_assets.setReviewFilter(QStringLiteral("unmarked"));
    app_shell.setCullingAutoAdvance(true);
    if (!send_cull_key(Qt::Key_P) ||
        !wait_for_condition([&]() {
          return catalog_assets.reviewFilter() == QStringLiteral("unmarked") &&
                 catalog_assets.assetCount() == 2 &&
                 catalog_assets.selectedAssetId() == QStringLiteral("cull-photo-2");
        }, 3000)) {
      return fail("Auto advance should select the next precomputed photo when review filters remove the current one");
    }
    catalog_assets.setReviewFilter(QStringLiteral("all"));
    app_shell.setCullingAutoAdvance(false);
    catalog_assets.selectIndex(0);

    if (!activate_cull_control(QStringLiteral("libraryCullCompareButton")) ||
        !wait_for_condition([&]() {
          auto* left = find_named_item(cull_root, QStringLiteral("libraryCullCompareImageLeft"));
          auto* right = find_named_item(cull_root, QStringLiteral("libraryCullCompareImageRight"));
          return item_visible(left) && item_visible(right) &&
                 left->property("paintedWidth").toDouble() > 0.0 &&
                 right->property("paintedWidth").toDouble() > 0.0;
        }, 3000)) return fail("Compare should show two real candidates from the current burst");
    auto* left_candidate = find_named_item(cull_root, QStringLiteral("libraryCullCompareImageLeft"));
    auto* right_candidate = find_named_item(cull_root, QStringLiteral("libraryCullCompareImageRight"));
    const auto left_url = left_candidate->property("source").toUrl();
    const auto right_url = right_candidate->property("source").toUrl();
    if (left_url.scheme() != QStringLiteral("image") || right_url.scheme() != QStringLiteral("image") ||
        !source_uses_path(left_url, fixture_paths.at(0)) ||
        !source_uses_path(right_url, fixture_paths.at(1)) ||
        std::abs(left_candidate->property("implicitWidth").toDouble() - 1200.0) > 1.0 ||
        std::abs(right_candidate->property("implicitWidth").toDouble() - 1200.0) > 1.0 ||
        !activate_cull_control(QStringLiteral("libraryCullZoom100Button")))
      return fail("Compare should decode original candidates rather than catalog thumbnails");
    auto* cull_view = find_named_item(cull_root, QStringLiteral("libraryCullView"));
    if (!cull_view || cull_view->property("fitMode").toBool() ||
        std::abs(cull_view->property("zoomPercent").toDouble() - 100.0) > 0.01 ||
        !cull_view->setProperty("panX", 24.0)) {
      return fail("Compare should expose a true 100 percent source view and shared pan state");
    }
    QCoreApplication::processEvents();
    if (!compare_geometry_is_usable() ||
        std::abs(left_candidate->property("displayScale").toDouble() -
                 right_candidate->property("displayScale").toDouble()) > 0.001 ||
        std::abs(left_candidate->x() - right_candidate->x()) > 1.0 ||
        !capture_cull(QStringLiteral("cull-compact-compare-100-percent"),
                      {QStringLiteral("libraryCullViewer"), QStringLiteral("libraryCullCompareImageLeft"),
                       QStringLiteral("libraryCullCompareImageRight"), QStringLiteral("libraryCullZoom100Button")},
                      900, 700) || !compare_geometry_is_usable() ||
        !capture_cull(QStringLiteral("cull-synchronized-100-percent-compare"),
                      {QStringLiteral("libraryCullCompareImageLeft"),
                       QStringLiteral("libraryCullCompareImageRight"),
                       QStringLiteral("libraryCullZoom100Button")})) {
      return fail("Compare should keep candidate pixel scale and panning synchronized");
    }

    catalog_assets.selectIndex(1);
    if (!wait_for_condition([&]() {
          return catalog_assets.selectedAssetId() == QStringLiteral("cull-photo-2") &&
                 library->property("libraryCullResumeAssetId").toString() == QStringLiteral("cull-photo-2");
        }, 3000)) return fail("Cull should remember the current photo in its shoot scope");
    QThread::msleep(600);
    QCoreApplication::processEvents();
    if (!activate_cull_control(QStringLiteral("studioLibraryGridButton")))
      return fail("Cull toggle should leave the dedicated review view");
    catalog_assets.selectIndex(0);
    if (!activate_cull_control(QStringLiteral("studioLibraryCullButton")) ||
        !wait_for_condition([&]() {
          return item_visible(find_named_item(cull_root, QStringLiteral("libraryCullResumeButton")));
        }, 3000) ||
        !activate_cull_control(QStringLiteral("libraryCullResumeButton")) ||
        catalog_assets.selectedAssetId() != QStringLiteral("cull-photo-2")) {
      return fail("Cull should offer and restore the last photo reviewed within the same shoot");
    }

    if (!capture_cull(QStringLiteral("cull-resumed-shoot-position"),
                      {QStringLiteral("libraryCullViewer"), QStringLiteral("libraryCullFilmstrip"),
                       QStringLiteral("libraryCullProgressLabel")}))
      return fail("Cull resume state should render as a usable review page");

    if (photo_editor::desktop::raw_camera_linear_decoder_capability().available) {
      const auto dng_path = runtime_root.filePath(QStringLiteral("synthetic-cull-source.dng"));
      const auto dng_payload = photo_editor::test_fixtures::synthetic_dng();
      QFile dng_file(dng_path);
      if (!dng_file.open(QIODevice::WriteOnly) ||
          dng_file.write(reinterpret_cast<const char*>(dng_payload.data()),
                         static_cast<qint64>(dng_payload.size())) !=
              static_cast<qint64>(dng_payload.size())) {
        return fail("Cull RAW proof should write the generated Bayer DNG fixture");
      }
      dng_file.close();
      auto raw_asset = make_asset(QStringLiteral("synthetic-cull-source"), dng_path,
                                  QUrl::fromLocalFile(fixture_paths.at(1)).toString());
      catalog_assets.replaceAssetsForTesting({raw_asset});
      catalog_assets.selectIndex(0);
      cull_view->setProperty("compareMode", false);
      cull_view->setProperty("fitMode", true);
      auto* raw_image = find_named_item(cull_root, QStringLiteral("libraryCullOriginalImage"));
      auto* raw_view = find_named_item(cull_root, QStringLiteral("libraryCullView"));
      if (!wait_for_condition([&]() {
            return raw_image != nullptr && raw_view != nullptr &&
                   std::abs(raw_image->property("implicitWidth").toDouble() - 128.0) < 1.0 &&
                   std::abs(raw_image->property("implicitHeight").toDouble() - 128.0) < 1.0 &&
                   !raw_view->property("originalSourceUnavailable").toBool();
          }, 15000) ||
          !capture_cull(QStringLiteral("cull-full-resolution-raw-source"),
                        {QStringLiteral("libraryCullViewer"), QStringLiteral("libraryCullOriginalImage"),
                         QStringLiteral("libraryCullImageQualityLabel")})) {
        return fail("Cull should decode a generated Bayer RAW through the photographic display pipeline");
      }
    } else {
      std::cout << "Cull RAW source proof skipped: LibRaw camera-linear decoder is unavailable\n";
    }

    const auto unavailable_raw_path = runtime_root.filePath(QStringLiteral("unavailable-cull-source.dng"));
    QFile unavailable_raw(unavailable_raw_path);
    if (!unavailable_raw.open(QIODevice::WriteOnly) ||
        unavailable_raw.write("not a RAW image") <= 0) {
      return fail("Cull fallback proof should create an unavailable RAW source");
    }
    unavailable_raw.close();
    auto fallback_asset = make_asset(QStringLiteral("unavailable-cull-source"), unavailable_raw_path,
                                     QUrl::fromLocalFile(fixture_paths.at(1)).toString());
    catalog_assets.replaceAssetsForTesting({fallback_asset});
    catalog_assets.selectIndex(0);
    const auto cached_fixture_source = QUrl::fromLocalFile(fixture_paths.at(1)).toString();
    if (!catalog_assets.applyThumbnailResultForTesting(
            fallback_asset.asset_id, fallback_asset.file_path,
            fallback_asset.adjustment_patch_json, cached_fixture_source)) {
      return fail("Cull fallback proof should seed its cached preview through the model thumbnail-result path");
    }
    cull_view->setProperty("compareMode", false);
    cull_view->setProperty("fitMode", true);
    const auto fallback_visible = wait_for_condition([&]() {
          auto* original = find_named_item(cull_root, QStringLiteral("libraryCullOriginalImage"));
          auto* cached = find_named_item(cull_root, QStringLiteral("libraryCullCachedPreviewImage"));
          auto* quality = find_named_item(cull_root, QStringLiteral("libraryCullImageQualityLabel"));
          auto* cull_view = find_named_item(cull_root, QStringLiteral("libraryCullView"));
          return original != nullptr && cull_view != nullptr &&
                 cull_view->property("originalSourceUnavailable").toBool() &&
                 item_visible(cached) && cached->property("status").toInt() == 1 &&
                 cached->property("paintedWidth").toDouble() > 0.0 && quality != nullptr &&
                 quality->property("text").toString().contains(QStringLiteral("original unavailable"));
        }, 10000);
    if (!fallback_visible) {
      auto* original = find_named_item(cull_root, QStringLiteral("libraryCullOriginalImage"));
      auto* cached = find_named_item(cull_root, QStringLiteral("libraryCullCachedPreviewImage"));
      auto* quality = find_named_item(cull_root, QStringLiteral("libraryCullImageQualityLabel"));
      auto* fallback_view = find_named_item(cull_root, QStringLiteral("libraryCullView"));
      std::cerr << "Cull fallback diagnostic: selected=" << catalog_assets.selectedAssetId().toStdString()
                << " file=" << catalog_assets.selectedFilePath().toStdString()
                << " thumbnail=" << catalog_assets.selectedThumbnailSource().toStdString()
                << " originalSource=" << (original ? original->property("source").toUrl().toString().toStdString() : "<missing>")
                << " originalStatus=" << (original ? original->property("status").toInt() : -1)
                << " unavailable=" << (fallback_view ? fallback_view->property("originalSourceUnavailable").toBool() : false)
                << " cachedVisible=" << (cached ? cached->property("visible").toBool() : false)
                << " cachedIsVisible=" << (cached ? cached->isVisible() : false)
                << " cachedSource=" << (cached ? cached->property("source").toUrl().toString().toStdString() : "<missing>")
                << " quality=" << (quality ? quality->property("text").toString().toStdString() : "<missing>") << '\n';
      return fail("When full-source decoding fails, Cull should clearly show its cached preview fallback");
    }
    if (!capture_cull(QStringLiteral("cull-unavailable-original-cached-preview"),
                      {QStringLiteral("libraryCullViewer"),
                       QStringLiteral("libraryCullCachedPreviewImage"), QStringLiteral("libraryCullImageQualityLabel")})) {
      return fail("When full-source decoding fails, Cull should clearly show its cached preview fallback");
    }
    cull_manifest.append(QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("fixture-note")},
        {QStringLiteral("state"), QStringLiteral("cull-unavailable-original-cached-preview")},
        {QStringLiteral("thumbnailSource"), QStringLiteral("generated-fixture-file")},
        {QStringLiteral("thumbnailSeededThrough"), QStringLiteral("applyThumbnailResultForTesting")},
        {QStringLiteral("backendThumbnailGenerationVerified"), false}});

    QFile manifest_file(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
    if (!manifest_file.open(QIODevice::WriteOnly)) return fail("Cull proof manifest should be writable");
    manifest_file.write(QJsonDocument(cull_manifest).toJson(QJsonDocument::Indented));
    editor_preview.setImageProvider(nullptr);
    std::cout << "Library Cull full-source review, keyboard and pointer marks, auto advance, comparison and per-shoot resume passed\n";
    return EXIT_SUCCESS;
  }

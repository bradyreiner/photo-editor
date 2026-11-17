    const auto photo_path = runtime_root.filePath(QStringLiteral("collection-photo.png"));
    if (!write_overlay_visual_fixture(photo_path, QStringLiteral("saturated")))
      return fail("Collection proof must create a photo fixture");
    QImage review_photo(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/fixtures/creative-tools/landscape-mountain-source.jpg"));
    if (review_photo.isNull() || !review_photo.save(photo_path)) return fail("Library proof must decode a real photo");
    auto photo = make_asset(QStringLiteral("collection-photo"), photo_path);
    photo.rating = 5;
    auto video = make_asset(QStringLiteral("collection-video"), photo_path);
    video.media_type = QStringLiteral("video");
    auto board = make_asset(QStringLiteral("collection-board"), QString());
    board.media_type = QStringLiteral("carousel");
    auto timeline = make_asset(QStringLiteral("timeline:collection"), QString());
    timeline.media_type = QStringLiteral("timeline");
    const auto canvas_path = runtime_root.filePath(QStringLiteral("canvas-documents/sample-board/source.png"));
    if (!QDir().mkpath(QFileInfo(canvas_path).absolutePath()) || !QFile::copy(photo_path, canvas_path))
      return fail("Collection proof must create a legacy board backing source");
    auto board_source = make_asset(QStringLiteral("collection-board-source"), canvas_path);
    const auto blank_path = runtime_root.filePath(QStringLiteral("canvas-documents/blank/source.png"));
    QDir().mkpath(QFileInfo(blank_path).absolutePath());
    QImage blank(640, 480, QImage::Format_ARGB32_Premultiplied); blank.fill(Qt::transparent);
    if (!blank.save(blank_path)) return fail("Blank Canvas fixture must be saved");
    auto canvas = make_asset(QStringLiteral("collection-blank-canvas"), blank_path);
    catalog_assets.replaceAssetsForTesting({photo, video, board, timeline, board_source, canvas});
    for (const auto& asset : {photo, video, canvas}) {
      catalog_assets.applyThumbnailResultForTesting(asset.asset_id, asset.file_path,
          catalog_assets.assetById(asset.asset_id).value("adjustmentPatchJson").toString(), QUrl::fromLocalFile(asset.file_path).toString());
    }
    const auto board_patch = photo_editor::desktop::serialize_adjustment_patch_json(
        photo_editor::desktop::EditRecipeState{}, photo_editor::desktop::all_adjustment_groups(),
        photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    if (!carousel_document.createBoardsFromEdit(canvas_path, QStringLiteral("Sample Boards"), board_patch, 640, 480, 2) ||
        !carousel_document.saveDocument(QStringLiteral("Sample Boards")))
      return fail("Collection proof must save its sample board document");
    timeline_document.createTimeline(QStringLiteral("Sample Timeline"));
    timeline_document.addClipFromGallery(photo.asset_id, photo_path, QStringLiteral("Sample photo"), 8.0,
                                         QUrl::fromLocalFile(photo_path).toString());
    if (!timeline_document.saveTimeline())
      return fail("Collection proof must save its sample timeline");
    const auto saved_path = timeline_document.savedDocumentPath();
    timeline_document.closeTimeline();
    engine.load(QUrl::fromLocalFile(main_qml_path));
    if (engine.rootObjects().isEmpty()) return fail("Collection proof must load Main.qml");
    auto* collection_root = engine.rootObjects().constFirst();
    auto* collection_window = qobject_cast<QQuickWindow*>(collection_root);
    if (!collection_window) return fail("Collection proof requires a window");
    collection_root->setProperty("currentWorkspace", collection_root->property("libraryWorkspaceIndex"));
    if (!wait_for_condition([&]() {
          auto* tab = find_named_item(collection_root, QStringLiteral("libraryImagesCollectionButton"));
          return item_visible(tab) && !collection_root->property("workspaceTransitionActive").toBool();
        }, 15000)) return fail("Collection proof must open the actual Library route");
    auto* library = find_named_object(collection_root, QStringLiteral("libraryStudioWorkspace"));
    const auto proof_dir = qEnvironmentVariable("PHOTO_EDITOR_VISUAL_PROOF_DIR",
                                               runtime_root.filePath(QStringLiteral("collection-proof")));
    if (!QDir().mkpath(proof_dir)) return fail("Collection proof output must be writable");
    QJsonArray manifest;
    const auto capture = [&](const QString& state, const QStringList& required) {
      QElapsedTimer animation_settle;
      animation_settle.start();
      while (animation_settle.elapsed() < 240) {
        process_layout_events(1);
        QThread::msleep(2);
      }
      return capture_visual_proof(collection_window, collection_root, proof_dir, state,
                                  1280, 800, required, &manifest);
    };
    const auto activate = [&](const QString& name) {
      auto* item = find_named_item(collection_root, name);
      if (!item_visible(item) || !item->isEnabled()) return false;
      item->forceActiveFocus(Qt::TabFocusReason);
      QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
      QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
      QCoreApplication::sendEvent(collection_window, &press);
      QCoreApplication::sendEvent(collection_window, &release);
      process_layout_events(8);
      return true;
    };
    if (!wait_for_condition([&]() { return catalog_assets.rowCount() == 2; }, 3000) ||
        catalog_assets.mediaCollectionFilter() != QStringLiteral("images") ||
        !capture(QStringLiteral("images-media-only"), {QStringLiteral("studioLibraryGalleryGrid")}))
      return fail("Images must render only photos and videos");
    catalog_assets.selectAssetById(photo.asset_id);
    catalog_assets.setSelectedRating(5);
    process_layout_events(8);
    catalog_assets.setMediaTypeFilter(QStringLiteral("videos"));
    catalog_assets.clearFilters();
    if (catalog_assets.rowCount() != 2) return fail("Clearing filters must preserve Images scope");
    catalog_assets.selectAssetById(photo.asset_id);
    process_layout_events(8);
    if (!capture(QStringLiteral("gold-stars-and-selection"), {QStringLiteral("studioLibraryGalleryGrid")})) return fail("Rated photo capture failed");
    auto* rating_star = find_named_item(collection_root, QStringLiteral("libraryRatingStar"));
    if (!item_visible(rating_star)) return fail("Rated photos must display filled gold stars");
    if (!activate(QStringLiteral("libraryCanvasesCollectionButton")) ||
        !wait_for_condition([&] { return catalog_assets.rowCount() == 1; }, 3000) ||
        catalog_assets.galleryAssetAt(0).value("assetId").toString() != canvas.asset_id ||
        catalog_assets.galleryAssetAt(0).value("fileType").toString() != QStringLiteral("CANVAS") ||
        !capture(QStringLiteral("blank-canvas-separated"), {QStringLiteral("studioLibraryGalleryGrid")}))
      return fail("Blank canvases must render in their own collection with a subtle type label");
    if (!activate(QStringLiteral("libraryImagesCollectionButton")) || catalog_assets.rowCount() != 2)
      return fail("Photos must stay separate after visiting Canvases");
    if (!activate(QStringLiteral("libraryCarouselsCollectionButton")) ||
        library->property("libraryCollection").toString() != QStringLiteral("boards") ||
        find_named_item(collection_root, QStringLiteral("libraryImagesCollectionButton"))->property("active").toBool() ||
        !find_named_item(collection_root, QStringLiteral("libraryCarouselsCollectionButton"))->property("active").toBool() ||
        !capture(QStringLiteral("boards-documents"), {QStringLiteral("savedBoardsGrid")}))
      return fail("Boards tab must open its document page");
    if (!wait_for_condition([&] {
          auto* cover = find_named_item(collection_root, QStringLiteral("savedBoardThumbnail"));
          return cover && cover->property("status").toInt() == 1;
        }, 15000) || !capture(QStringLiteral("boards-rendered-cover"), {QStringLiteral("savedBoardThumbnail")}))
      return fail("Saved Boards must render a real cover without changing the open editor");
    if (!activate(QStringLiteral("libraryTimelinesCollectionButton")) ||
        !wait_for_condition([&]() {
          auto* list = find_named_object(collection_root, QStringLiteral("savedTimelinesList"));
          return list && list->property("count").toInt() == 1 && !timeline_document.recentTimelinesLoading();
        }, 10000) ||
        !find_named_item(collection_root, QStringLiteral("libraryTimelinesCollectionButton"))->property("active").toBool() ||
        find_named_item(collection_root, QStringLiteral("libraryCarouselsCollectionButton"))->property("active").toBool() ||
        !library->property("libraryDocumentsOpen").toBool() ||
        item_visible(find_named_item(collection_root, QStringLiteral("studioLibrarySearchInput"))) ||
        collection_root->property("photoRecipeWorkspace").toBool() ||
        !capture(QStringLiteral("timelines-documents"), {QStringLiteral("savedTimelinesList"), QStringLiteral("libraryOpenTimelineRow0")}))
      return fail("Timelines tab must show saved documents and hide image controls");
    if (!capture_visual_proof(collection_window, collection_root, proof_dir, QStringLiteral("timelines-compact"),
                              900, 700, {QStringLiteral("libraryOpenTimelineRow0"), QStringLiteral("libraryNewTimelineButton")}, &manifest))
      return fail("Timelines must remain usable at compact size");
    if (!activate(QStringLiteral("libraryImagesCollectionButton")) || catalog_assets.rowCount() != 2 ||
        library->property("libraryCollection").toString() != QStringLiteral("images"))
      return fail("Returning to Images must preserve media and filters");
    if (!activate(QStringLiteral("libraryTimelinesCollectionButton")) ||
        !wait_for_condition([&]() { return item_visible(find_named_item(collection_root, QStringLiteral("libraryOpenTimelineRow0"))); }, 5000) ||
        !activate(QStringLiteral("libraryOpenTimelineRow0")) ||
        !wait_for_condition([&]() {
          return timeline_document.hasActiveTimeline() && timeline_document.savedDocumentPath() == saved_path &&
                 collection_root->property("currentWorkspace") == collection_root->property("timelineWorkspaceIndex") &&
                 !collection_root->property("workspaceTransitionActive").toBool() &&
                 item_visible(find_named_item(collection_root, QStringLiteral("timelineWorkspace")));
        }, 45000)) {
      auto* timeline_page = find_named_item(collection_root, QStringLiteral("timelineWorkspace"));
      std::cerr << "Timeline open: active=" << timeline_document.hasActiveTimeline()
                << " path=" << timeline_document.savedDocumentPath().toStdString()
                << " expected=" << saved_path.toStdString()
                << " workspace=" << collection_root->property("currentWorkspace").toInt()
                << " transition=" << collection_root->property("workspaceTransitionActive").toBool()
                << " page=" << (timeline_page != nullptr)
                << " visible=" << item_visible(timeline_page)
                << " status=" << timeline_document.status().toStdString() << '\n';
      return fail("Opening a saved timeline must load that document and route to Timeline");
    }
    collection_root->setProperty("currentWorkspace", collection_root->property("libraryWorkspaceIndex"));
    if (!wait_for_condition([&]() { return item_visible(find_named_item(collection_root, QStringLiteral("libraryNewTimelineButton"))); }, 10000) ||
        !activate(QStringLiteral("libraryNewTimelineButton")) ||
        !wait_for_condition([&]() {
          return timeline_document.hasActiveTimeline() && timeline_document.savedDocumentPath().isEmpty() &&
                 collection_root->property("currentWorkspace") == collection_root->property("timelineWorkspaceIndex") &&
                 !collection_root->property("workspaceTransitionActive").toBool() &&
                 item_visible(find_named_item(collection_root, QStringLiteral("timelineWorkspace")));
        }, 20000)) return fail("New Timeline must create an editable timeline and open its workspace");
    if (!wait_for_condition([&]() {
          auto* incubation = engine.incubationController();
          if (incubation) incubation->incubateFor(5);
          return !incubation || incubation->incubatingObjectCount() == 0;
        }, 5000)) return fail("Timeline UI must finish asynchronous creation before fixture teardown");
    QFile manifest_file(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
    if (!manifest_file.open(QIODevice::WriteOnly)) return fail("Collection manifest must save");
    manifest_file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    editor_preview.setImageProvider(nullptr);
    std::cout << "Library Images/Boards/Timelines switching, scope, open, and create passed\n";
    return EXIT_SUCCESS;

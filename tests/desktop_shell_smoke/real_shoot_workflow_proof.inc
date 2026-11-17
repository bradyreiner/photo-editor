if (qEnvironmentVariableIsSet("PHOTO_EDITOR_REAL_SHOOT_REOPEN_CHILD")) {
  using photo_editor::desktop::CatalogAssetModel;

  const auto child_receipt_path = qEnvironmentVariable(
      "PHOTO_EDITOR_REAL_SHOOT_CHILD_RECEIPT").trimmed();
  const auto parent_receipt_path = qEnvironmentVariable(
      "PHOTO_EDITOR_REAL_SHOOT_REOPEN_RECEIPT").trimmed();
  QJsonObject child_receipt{
      {QStringLiteral("schema"), QStringLiteral("looksy-real-shoot-process-reopen:v1")},
      {QStringLiteral("status"), QStringLiteral("running")},
      {QStringLiteral("processId"), QCoreApplication::applicationPid()},
      {QStringLiteral("parentProcessId"), qEnvironmentVariable(
          "PHOTO_EDITOR_REAL_SHOOT_PARENT_PID").toLongLong()},
      {QStringLiteral("appDataRoot"), qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_DIR")},
      {QStringLiteral("cacheRoot"), qEnvironmentVariable("PHOTO_EDITOR_CACHE_DIR")},
      {QStringLiteral("databaseUrlSha256"), QString::fromLatin1(QCryptographicHash::hash(
          qEnvironmentVariable("PHOTO_EDITOR_DATABASE_URL").toUtf8(),
          QCryptographicHash::Sha256).toHex())},
  };
  const auto persist_child_receipt = [&]() {
    if (child_receipt_path.isEmpty() ||
        !QDir().mkpath(QFileInfo(child_receipt_path).absolutePath())) {
      return false;
    }
    QFile file(child_receipt_path);
    const auto bytes = QJsonDocument(child_receipt).toJson(QJsonDocument::Indented);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(bytes) == bytes.size();
  };
  const auto child_fail = [&](const QString& stage, const QString& message) {
    child_receipt.insert(QStringLiteral("status"), QStringLiteral("failed"));
    child_receipt.insert(QStringLiteral("failureStage"), stage);
    (void)persist_child_receipt();
    const auto utf8_message = message.toUtf8();
    return fail(utf8_message.constData());
  };
  const auto sha256_file = [](const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString{};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
      const auto chunk = file.read(1024 * 1024);
      if (chunk.isEmpty() && file.error() != QFileDevice::NoError) return QString{};
      hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
  };

  QFile parent_receipt_file(parent_receipt_path);
  if (parent_receipt_path.isEmpty() || !parent_receipt_file.open(QIODevice::ReadOnly)) {
    return child_fail(QStringLiteral("parent-receipt"),
        QStringLiteral("the process-reopen child should read the parent workflow receipt"));
  }
  const auto parent_report = QJsonDocument::fromJson(parent_receipt_file.readAll()).object();
  if (parent_report.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("looksy-real-shoot-workflow:v1") ||
      parent_report.value(QStringLiteral("runId")).toString().isEmpty()) {
    return child_fail(QStringLiteral("parent-receipt"),
        QStringLiteral("the process-reopen child should receive the matching workflow manifest"));
  }
  const auto expected_parent_pid = qEnvironmentVariable(
      "PHOTO_EDITOR_REAL_SHOOT_PARENT_PID").toLongLong();
  if (expected_parent_pid <= 1 || expected_parent_pid == QCoreApplication::applicationPid() ||
      qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_DIR").isEmpty() ||
      qEnvironmentVariable("PHOTO_EDITOR_CACHE_DIR").isEmpty() ||
      !qEnvironmentVariable("PHOTO_EDITOR_DATABASE_URL").startsWith(QStringLiteral("postgres"))) {
    return child_fail(QStringLiteral("inherited-runtime"),
        QStringLiteral("the child should use the parent's isolated app data, cache, database, and distinct process"));
  }

  const auto culling = parent_report.value(QStringLiteral("culling")).toObject();
  const auto refine = parent_report.value(QStringLiteral("refineAndBatch")).toObject();
  const auto batch_targets = refine.value(QStringLiteral("batchTargetIds")).toArray();
  const auto picked_asset_id = culling.value(QStringLiteral("pickedAssetId")).toString();
  const auto rejected_asset_id = culling.value(QStringLiteral("rejectedAssetId")).toString();
  QString second_asset_id;
  for (const auto& value : batch_targets) {
    const auto id = value.toString();
    if (!id.isEmpty() && id != picked_asset_id) second_asset_id = id;
  }
  const auto moved_source_root = parent_report.value(QStringLiteral("moveAndRelink"))
                                    .toObject().value(QStringLiteral("movedFolder")).toString();
  const auto expected_export = parent_report.value(QStringLiteral("exportRetry")).toObject();
  const auto export_path = expected_export.value(QStringLiteral("outputPath")).toString();
  const auto export_hash = expected_export.value(QStringLiteral("outputSha256")).toString();
  if (picked_asset_id.isEmpty() || second_asset_id.isEmpty() || rejected_asset_id.isEmpty() ||
      moved_source_root.isEmpty() || export_path.isEmpty() || export_hash.isEmpty()) {
    return child_fail(QStringLiteral("parent-manifest-fields"),
        QStringLiteral("the parent receipt should identify the edited photos, relinked folder, and exported bytes"));
  }

  CatalogAssetModel reopened_catalog;
  reopened_catalog.refresh();
  if (!wait_for_condition([&]() {
        return !reopened_catalog.loading() && reopened_catalog.assetCount() == 3;
      }, 60000)) {
    return child_fail(QStringLiteral("fresh-catalog-load"),
        QStringLiteral("a separate process should reopen all three photos from the existing isolated catalog"));
  }
  auto paths_and_culling_persisted = true;
  for (const auto& id : {picked_asset_id, second_asset_id, rejected_asset_id}) {
    const auto asset = reopened_catalog.assetById(id);
    const auto path = asset.value(QStringLiteral("filePath")).toString();
    paths_and_culling_persisted &= !asset.isEmpty() &&
        asset.value(QStringLiteral("online")).toBool() && path.startsWith(moved_source_root);
  }
  const auto picked = reopened_catalog.assetById(picked_asset_id);
  const auto second = reopened_catalog.assetById(second_asset_id);
  const auto rejected = reopened_catalog.assetById(rejected_asset_id);
  const auto picked_patch = picked.value(QStringLiteral("adjustmentPatchJson")).toString();
  const auto second_patch = second.value(QStringLiteral("adjustmentPatchJson")).toString();
  const auto rejected_patch = rejected.value(QStringLiteral("adjustmentPatchJson")).toString();
  const auto picked_state = photo_editor::desktop::parse_adjustment_patch_json(picked_patch);
  const auto second_state = photo_editor::desktop::parse_adjustment_patch_json(second_patch);
  const auto refined_mask_is_persisted = [](const auto& parsed) {
    return parsed.ok() && !parsed.state->mask_stack.isEmpty() &&
        qAbs(parsed.state->mask_stack.constFirst().toMap()
                 .value(QStringLiteral("exposure")).toDouble() - 0.12) <= 0.001;
  };
  const auto rejected_patch_hash = QString::fromLatin1(
      QCryptographicHash::hash(rejected_patch.toUtf8(), QCryptographicHash::Sha256).toHex());
  const auto recipes_persisted = picked_state.ok() && second_state.ok() &&
      qAbs(picked_state.state->exposure - 0.35) <= 0.001 &&
      qAbs(second_state.state->exposure - 0.35) <= 0.001 &&
      refined_mask_is_persisted(picked_state) && refined_mask_is_persisted(second_state) &&
      rejected_patch_hash == refine.value(QStringLiteral("rejectedPatchSha256")).toString();
  const auto culling_persisted = picked.value(QStringLiteral("picked")).toBool() &&
      picked.value(QStringLiteral("rating")).toInt() == 5 &&
      rejected.value(QStringLiteral("rejected")).toBool();

  QImage exported_image;
  const auto export_bytes_persisted = QFileInfo(export_path).size() > 1024 &&
      sha256_file(export_path) == export_hash && exported_image.load(export_path) &&
      exported_image.width() == expected_export.value(QStringLiteral("decodedWidth")).toInt() &&
      exported_image.height() == expected_export.value(QStringLiteral("decodedHeight")).toInt();
  if (!paths_and_culling_persisted || !culling_persisted || !recipes_persisted ||
      !export_bytes_persisted) {
    child_receipt.insert(QStringLiteral("assetPathsAndAvailabilityPersisted"), paths_and_culling_persisted);
    child_receipt.insert(QStringLiteral("cullingPersisted"), culling_persisted);
    child_receipt.insert(QStringLiteral("refinedRecipesPersisted"), recipes_persisted);
    child_receipt.insert(QStringLiteral("refinedMaskExposure"), 0.12);
    child_receipt.insert(QStringLiteral("exportBytesPersisted"), export_bytes_persisted);
    return child_fail(QStringLiteral("durable-state-check"),
        QStringLiteral("the new process should retain culling, edits including mask exposure, relinks, and exported bytes"));
  }

  auto picked_asset_index = -1;
  for (auto index = 0; index < reopened_catalog.assetCount(); ++index) {
    if (reopened_catalog.assetAt(index).value(QStringLiteral("assetId")).toString() ==
        picked_asset_id) {
      picked_asset_index = index;
      break;
    }
  }
  if (picked_asset_index < 0) {
    return child_fail(QStringLiteral("picked-photo-selection"),
        QStringLiteral("the separate-process Library should locate the recovered picked photo by stable ID"));
  }
  reopened_catalog.selectIndex(picked_asset_index);

  app_shell.completeFirstRunExperience();
  engine.rootContext()->setContextProperty("catalogAssets", &reopened_catalog);
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER"))
    activate_native_test_application();
  engine.load(QUrl::fromLocalFile(main_qml_path));
  if (engine.rootObjects().isEmpty()) {
    return child_fail(QStringLiteral("main-load"),
        QStringLiteral("Main.qml should load against the catalog reopened in a separate process"));
  }
  auto* reopened_root = engine.rootObjects().constFirst();
  auto* reopened_window = qobject_cast<QQuickWindow*>(reopened_root);
  const auto main_visible = wait_for_condition([&]() {
        if (reopened_window != nullptr && !reopened_window->isExposed() &&
            qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER")) {
          activate_native_test_application();
          reopened_window->show();
          reopened_window->raise();
          reopened_window->requestActivate();
        }
        return !reopened_catalog.loading() && reopened_catalog.assetCount() == 3 &&
            item_visible(find_named_item(reopened_root, QStringLiteral("libraryImagesCollectionButton"))) &&
            reopened_window != nullptr && reopened_window->isVisible() && reopened_window->isExposed();
      }, 30000);
  if (!main_visible) {
    return child_fail(QStringLiteral("visible-main-load"),
        QStringLiteral("the separate process should display Main.qml Library with its recovered catalog"));
  }
  QString picked_thumbnail_source;
  QQuickItem* picked_thumbnail_image = nullptr;
  QJsonArray last_thumbnail_candidates;
  const auto picked_thumbnail_ready = wait_for_condition([&]() {
        if (!reopened_window->isExposed() &&
            qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER")) {
          activate_native_test_application();
          reopened_window->show();
          reopened_window->raise();
          reopened_window->requestActivate();
        }
        picked_thumbnail_source = reopened_catalog.assetById(picked_asset_id)
                                      .value(QStringLiteral("thumbnailSource")).toString();
        const QUrl expected_source_url(picked_thumbnail_source);
        last_thumbnail_candidates = {};
        for (auto* candidate : reopened_root->findChildren<QObject*>()) {
          auto* image_item = qobject_cast<QQuickItem*>(candidate);
          if (image_item == nullptr) continue;
          const auto source_value = image_item->property("source");
          const auto status_value = image_item->property("status");
          if ((source_value.isValid() || status_value.isValid()) &&
              last_thumbnail_candidates.size() < 32) {
            const auto scene_bounds = image_item->mapRectToScene(
                QRectF(0.0, 0.0, image_item->width(), image_item->height()));
            last_thumbnail_candidates.append(QJsonObject{
                {QStringLiteral("source"), source_value.toString()},
                {QStringLiteral("status"), status_value.toInt()},
                {QStringLiteral("visible"), image_item->isVisible()},
                {QStringLiteral("width"), image_item->width()},
                {QStringLiteral("height"), image_item->height()},
                {QStringLiteral("opacity"), image_item->opacity()},
                {QStringLiteral("sceneX"), scene_bounds.x()},
                {QStringLiteral("sceneY"), scene_bounds.y()},
                {QStringLiteral("windowMatch"), image_item->window() == reopened_window},
            });
          }
          if (!image_item->isVisible() || image_item->window() != reopened_window ||
              image_item->width() <= 1.0 || image_item->height() <= 1.0 || image_item->opacity() <= 0.05) continue;
          if (picked_thumbnail_source.isEmpty()) continue;
          const auto candidate_url = source_value.toUrl();
          if (source_value.toString() != picked_thumbnail_source &&
              (!candidate_url.isValid() || candidate_url != expected_source_url)) {
            continue;
          }
          bool status_is_valid = false;
          const auto image_status = image_item->property("status").toInt(&status_is_valid);
          // QtQuick Image.Ready is enum value 1.  Require the real gallery Image
          // object for this source, rather than a non-empty model URL alone.
          if (!status_is_valid || image_status != 1) continue;
          picked_thumbnail_image = image_item;
          return true;
        }
        return false;
      }, 90000);
  child_receipt.insert(QStringLiteral("pickedThumbnailSource"), picked_thumbnail_source);
  child_receipt.insert(QStringLiteral("pickedThumbnailImageReady"), picked_thumbnail_ready);
  const auto picked_file_path = reopened_catalog.assetById(picked_asset_id)
                                    .value(QStringLiteral("filePath")).toString();
  child_receipt.insert(QStringLiteral("pickedSourceFilePath"), picked_file_path);
  child_receipt.insert(QStringLiteral("pickedSourceFileExists"), QFileInfo(picked_file_path).isFile());
  child_receipt.insert(QStringLiteral("pickedSourceFileReadable"), QFileInfo(picked_file_path).isReadable());
  child_receipt.insert(QStringLiteral("modelInteractiveRenderingActive"), reopened_catalog.interactiveRenderingActive());
  child_receipt.insert(QStringLiteral("activeThumbnailWorkerCount"), reopened_catalog.activeThumbnailWorkerCount());
  child_receipt.insert(QStringLiteral("pendingThumbnailWorkerCount"), reopened_catalog.pendingThumbnailWorkerCount());
  child_receipt.insert(QStringLiteral("thumbnailCandidates"), last_thumbnail_candidates);
  child_receipt.insert(QStringLiteral("cacheRoot"), qEnvironmentVariable("PHOTO_EDITOR_CACHE_DIR"));
  child_receipt.insert(QStringLiteral("cacheRootExists"), QDir(qEnvironmentVariable("PHOTO_EDITOR_CACHE_DIR")).exists());
  if (!picked_thumbnail_ready || picked_thumbnail_image == nullptr) {
    return child_fail(QStringLiteral("reopened-thumbnail-not-ready"),
        QStringLiteral("the picked CR2 thumbnail should decode into a ready visible gallery Image after process reopen"));
  }

  int ready_gallery_thumbnails = 0;
  const auto all_gallery_thumbnails_ready = wait_for_condition([&]() {
    QSet<QString> ready_sources;
    std::function<void(QQuickItem*)> visit = [&](QQuickItem* item) {
      if (item->objectName() == QStringLiteral("libraryGalleryImage") &&
          item->isVisible() && item->opacity() > 0.05 &&
          item->width() > 1 && item->height() > 1 &&
          item->property("status").toInt() == 1)
        ready_sources.insert(item->property("source").toString());
      for (auto* child : item->childItems()) visit(child);
    };
    visit(reopened_window->contentItem());
    ready_gallery_thumbnails = 0;
    for (const auto& id : {picked_asset_id, second_asset_id, rejected_asset_id}) {
      const auto source = reopened_catalog.assetById(id).value("thumbnailSource").toString();
      if (!source.isEmpty() && ready_sources.contains(source)) ++ready_gallery_thumbnails;
    }
    return ready_gallery_thumbnails == 3;
  }, 90000);
  child_receipt.insert(QStringLiteral("allGalleryThumbnailsReady"), all_gallery_thumbnails_ready);
  child_receipt.insert(QStringLiteral("readyGalleryThumbnailCount"), ready_gallery_thumbnails);
  child_receipt.insert(QStringLiteral("allRecipesReadyAtCapture"), reopened_catalog.allAssetRecipesReady());
  child_receipt.insert(QStringLiteral("activeThumbnailWorkerCount"), reopened_catalog.activeThumbnailWorkerCount());
  child_receipt.insert(QStringLiteral("pendingThumbnailWorkerCount"), reopened_catalog.pendingThumbnailWorkerCount());
  if (!all_gallery_thumbnails_ready)
    return child_fail(QStringLiteral("reopened-gallery-not-ready"),
        QStringLiteral("all three recovered RAW photos should render in the visible Library before the proof completes"));

  const auto reopen_screenshot_path = QDir(QFileInfo(parent_receipt_path).absolutePath())
                                         .filePath(QStringLiteral("process-reopen-window.png"));
  reopened_window->update();
  process_layout_events(12);
  const auto reopen_screenshot = reopened_window->grabWindow();
  const auto screenshot_saved = !reopen_screenshot.isNull() &&
      reopen_screenshot.save(reopen_screenshot_path);
  if (!screenshot_saved) {
    return child_fail(QStringLiteral("visible-main-capture"),
        QStringLiteral("the child Main.qml Library window should produce a native relaunch capture"));
  }
  const auto thumbnail_rect_scene = picked_thumbnail_image->mapRectToScene(
      QRectF(0.0, 0.0, picked_thumbnail_image->width(), picked_thumbnail_image->height()));
  const auto screenshot_scale = reopen_screenshot.devicePixelRatio();
  const QRect thumbnail_rect_pixels(
      qFloor(thumbnail_rect_scene.left() * screenshot_scale),
      qFloor(thumbnail_rect_scene.top() * screenshot_scale),
      qCeil(thumbnail_rect_scene.width() * screenshot_scale),
      qCeil(thumbnail_rect_scene.height() * screenshot_scale));
  const auto thumbnail_capture = reopen_screenshot.copy(
      thumbnail_rect_pixels.intersected(reopen_screenshot.rect()));
  QSet<quint32> thumbnail_color_bins;
  auto minimum_thumbnail_luminance = 255;
  auto maximum_thumbnail_luminance = 0;
  constexpr auto thumbnail_sample_grid = 8;
  if (!thumbnail_capture.isNull()) {
    for (auto y = 0; y < thumbnail_sample_grid; ++y) {
      for (auto x = 0; x < thumbnail_sample_grid; ++x) {
        const auto sample_x = qMin(
            thumbnail_capture.width() - 1,
            static_cast<int>((static_cast<qint64>(x) * 2 + 1) * thumbnail_capture.width() /
                             (thumbnail_sample_grid * 2)));
        const auto sample_y = qMin(
            thumbnail_capture.height() - 1,
            static_cast<int>((static_cast<qint64>(y) * 2 + 1) * thumbnail_capture.height() /
                             (thumbnail_sample_grid * 2)));
        const auto color = thumbnail_capture.pixelColor(sample_x, sample_y);
        const auto luma = (299 * color.red() + 587 * color.green() + 114 * color.blue()) / 1000;
        minimum_thumbnail_luminance = qMin(minimum_thumbnail_luminance, luma);
        maximum_thumbnail_luminance = qMax(maximum_thumbnail_luminance, luma);
        thumbnail_color_bins.insert(
            (static_cast<quint32>(color.red() >> 4) << 8) |
            (static_cast<quint32>(color.green() >> 4) << 4) |
            static_cast<quint32>(color.blue() >> 4));
      }
    }
  }
  const auto thumbnail_luminance_range = maximum_thumbnail_luminance - minimum_thumbnail_luminance;
  const auto thumbnail_capture_path = QDir(QFileInfo(parent_receipt_path).absolutePath())
                                          .filePath(QStringLiteral("process-reopen-picked-thumbnail.png"));
  if (thumbnail_capture.isNull() || thumbnail_color_bins.size() < 8 ||
      thumbnail_luminance_range < 10 || !thumbnail_capture.save(thumbnail_capture_path)) {
    child_receipt.insert(QStringLiteral("pickedThumbnailPixelBins"), thumbnail_color_bins.size());
    child_receipt.insert(QStringLiteral("pickedThumbnailLuminanceRange"), thumbnail_luminance_range);
    child_receipt.insert(QStringLiteral("pickedThumbnailCapturePath"), thumbnail_capture_path);
    return child_fail(QStringLiteral("reopened-thumbnail-pixels"),
        QStringLiteral("the reopened picked-photo gallery thumbnail should contain visible photo pixels"));
  }

  child_receipt.insert(QStringLiteral("status"), QStringLiteral("passed"));
  child_receipt.insert(QStringLiteral("workflowRunId"), parent_report.value(QStringLiteral("runId")));
  child_receipt.insert(QStringLiteral("freshCatalogModel"), true);
  child_receipt.insert(QStringLiteral("mainQmlReloaded"), true);
  child_receipt.insert(QStringLiteral("mainWindowVisible"), reopened_window->isVisible());
  child_receipt.insert(QStringLiteral("mainWindowExposed"), reopened_window->isExposed());
  child_receipt.insert(QStringLiteral("mainWindowScreenshot"), reopen_screenshot_path);
  child_receipt.insert(QStringLiteral("pickedThumbnailImageReady"), true);
  child_receipt.insert(QStringLiteral("pickedThumbnailCapturePath"), thumbnail_capture_path);
  child_receipt.insert(QStringLiteral("pickedThumbnailPixelBins"), thumbnail_color_bins.size());
  child_receipt.insert(QStringLiteral("pickedThumbnailLuminanceRange"), thumbnail_luminance_range);
  child_receipt.insert(QStringLiteral("catalogRows"), reopened_catalog.assetCount());
  child_receipt.insert(QStringLiteral("assetPathsAndAvailabilityPersisted"), paths_and_culling_persisted);
  child_receipt.insert(QStringLiteral("cullingPersisted"), culling_persisted);
  child_receipt.insert(QStringLiteral("refinedRecipesPersisted"), recipes_persisted);
  child_receipt.insert(QStringLiteral("refinedMaskExposure"), 0.12);
  child_receipt.insert(QStringLiteral("exportBytesPersisted"), export_bytes_persisted);
  child_receipt.insert(QStringLiteral("exportSha256"), export_hash);
  child_receipt.insert(QStringLiteral("processSeparationVerified"), true);
  if (!persist_child_receipt()) {
    return fail("the child-process reopen receipt should be writable");
  }
  return EXIT_SUCCESS;
}

if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_REAL_SHOOT_WORKFLOW")) {
  using photo_editor::desktop::CatalogAssetModel;
  using photo_editor::desktop::AppBootstrap;

  const auto default_proof_root = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath(
      QStringLiteral("build/app-audit-fixes-2026-10-25/real-shoot"));
  const auto proof_root = qEnvironmentVariable(
      "PHOTO_EDITOR_REAL_SHOOT_PROOF_DIR", default_proof_root).trimmed();
  const auto staged_source_root = qEnvironmentVariable(
      "PHOTO_EDITOR_REAL_SHOOT_SOURCE_DIR",
      QDir(proof_root).filePath(QStringLiteral("source"))).trimmed();
  const auto media_names = QDir(staged_source_root).entryList(
      {QStringLiteral("*.CR2"), QStringLiteral("*.cr2")}, QDir::Files, QDir::Name);
  if (media_names.size() != 3) {
    std::cerr << "Real-shoot optional fixture requires three copied CR2 files; found "
              << media_names.size() << " at "
              << staged_source_root.toStdString() << '\n';
    return 77;
  }
  const auto run_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto run_root = QDir(proof_root).filePath(QStringLiteral("runs/%1").arg(run_id));
  const auto source_root = QDir(runtime_root.path()).filePath(QStringLiteral("real-shoot/source"));
  const auto output_root = QDir(run_root).filePath(QStringLiteral("delivery"));
  if (!QDir().mkpath(source_root) || !QDir().mkpath(output_root)) {
    return fail("the isolated real-shoot run folders should be writable");
  }

  QJsonObject report{
      {QStringLiteral("schema"), QStringLiteral("looksy-real-shoot-workflow:v1")},
      {QStringLiteral("runId"), run_id},
      {QStringLiteral("status"), QStringLiteral("running")},
      {QStringLiteral("scope"), QStringLiteral(
          "copied Canon CR2 media, isolated managed catalog, production GUI import job, cull, selective recipe batch, mask refinement, export failure and retry, move/relink, and fresh-process Main.qml reopen")},
      {QStringLiteral("humanArtisticApproval"), QStringLiteral("not assessed")},
      {QStringLiteral("cleanMachineQualification"), QStringLiteral("not assessed")},
  };
  const auto persist_report = [&]() {
    if (!QDir().mkpath(run_root)) return false;
    QFile file(QDir(run_root).filePath(QStringLiteral("workflow.json")));
    const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(bytes) == bytes.size();
  };
  const auto sha256_file = [](const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString{};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
      const auto chunk = file.read(1024 * 1024);
      if (chunk.isEmpty() && file.error() != QFileDevice::NoError) return QString{};
      hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
  };

  QJsonArray media_receipt;
  QStringList runtime_media_paths;
  for (const auto& name : media_names) {
    const auto staged_path = QDir(staged_source_root).filePath(name);
    const auto runtime_path = QDir(source_root).filePath(name);
    const auto staged_hash = sha256_file(staged_path);
    if (staged_hash.isEmpty() || !QFile::copy(staged_path, runtime_path) ||
        sha256_file(runtime_path) != staged_hash) {
      report.insert(QStringLiteral("status"), QStringLiteral("failed"));
      report.insert(QStringLiteral("failureStage"), QStringLiteral("copy-media"));
      (void)persist_report();
      return fail("the real-shoot input should be copied into the isolated runtime with identical SHA-256 bytes");
    }
    const QFileInfo info(runtime_path);
    media_receipt.push_back(QJsonObject{
        {QStringLiteral("fileName"), name},
        {QStringLiteral("bytes"), info.size()},
        {QStringLiteral("sha256"), staged_hash},
    });
    runtime_media_paths.push_back(runtime_path);
  }
  report.insert(QStringLiteral("mediaCopies"), media_receipt);
  report.insert(QStringLiteral("rawDecodeExercised"), false);
  report.insert(QStringLiteral("isolatedCatalog"), QJsonObject{
      {QStringLiteral("appDataRoot"), runtime_root.path()},
      {QStringLiteral("proofRunRoot"), run_root},
      {QStringLiteral("databaseBackend"), QStringLiteral("managed local PostgreSQL under the temporary app-data root")},
  });
  if (!persist_report()) return fail("the real-shoot report should be writable before workflow execution");

  auto catalog_environment = QProcessEnvironment::systemEnvironment();
  catalog_environment.remove(QStringLiteral("PHOTO_EDITOR_DATABASE_URL"));
  catalog_environment.insert(QStringLiteral("PHOTO_EDITOR_APP_DATA_DIR"), runtime_root.path());
  catalog_environment.insert(QStringLiteral("PHOTO_EDITOR_CACHE_DIR"), cache_root.path());
  QProcess catalog_start;
  catalog_start.setProcessEnvironment(catalog_environment);
  catalog_start.setWorkingDirectory(QDir::currentPath());
  catalog_start.start(
      AppBootstrap::helperProgram(QStringLiteral("catalog_local_db")),
      AppBootstrap::helperArguments(QStringLiteral("catalog_local_db"),
                                    {QStringLiteral("connection-url"), QStringLiteral("--ensure-running")}));
  if (!catalog_start.waitForStarted(5000) || !catalog_start.waitForFinished(60000) ||
      catalog_start.exitStatus() != QProcess::NormalExit || catalog_start.exitCode() != 0) {
    std::cerr << "Isolated catalog startup failed: "
              << catalog_start.readAllStandardError().toStdString() << '\n';
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("isolated-catalog-start"));
    (void)persist_report();
    return fail("the dedicated real-shoot proof should start its own managed local catalog");
  }
  const auto database_url = QString::fromUtf8(catalog_start.readAllStandardOutput()).trimmed();
  if (!database_url.startsWith(QStringLiteral("postgres"))) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("isolated-catalog-url"));
    report.insert(QStringLiteral("catalogStartOutput"), database_url);
    (void)persist_report();
    return fail("the isolated managed catalog should return a PostgreSQL connection URL");
  }
  qputenv("PHOTO_EDITOR_DATABASE_URL", database_url.toUtf8());

  bool job_runner_recovered = false;
  QString job_runner_recovery_error;
  QObject::connect(
      &job_runner, &photo_editor::desktop::JobRunner::startupRecoveryFinished,
      &job_runner, [&](const QString& error) {
        job_runner_recovery_error = error;
        job_runner_recovered = true;
      });
  job_runner.initializeFromStore();
  job_queue.initializeFromStore();
  export_jobs.initializeFromStore();
  if (!wait_for_condition([&]() {
        return job_runner_recovered && !job_queue.loading() && !export_jobs.loading();
      }, 30000) || !job_runner_recovery_error.isEmpty()) {
    std::cerr << "Real-shoot startup recovery failed: "
              << job_runner_recovery_error.toStdString() << '\n';
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("job-runner-startup"));
    report.insert(QStringLiteral("startupRecoveryError"), job_runner_recovery_error);
    (void)persist_report();
    return fail("the isolated JobRunner should finish startup recovery before import");
  }

  QImage raw_decode_sample;
  const auto raw_decode_succeeded = photo_editor::desktop::decode_raw_image(
      runtime_media_paths.first(), &raw_decode_sample, 1024);
  if (!raw_decode_succeeded || raw_decode_sample.isNull() ||
      raw_decode_sample.width() <= 0 || raw_decode_sample.height() <= 0) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("real-raw-decode"));
    (void)persist_report();
    return fail("a copied Canon CR2 should decode through the production RAW image decoder");
  }
  report.insert(QStringLiteral("rawDecodeExercised"), true);
  report.insert(QStringLiteral("rawDecode"), QJsonObject{
      {QStringLiteral("sourceFile"), QFileInfo(runtime_media_paths.first()).fileName()},
      {QStringLiteral("decodedWidth"), raw_decode_sample.width()},
      {QStringLiteral("decodedHeight"), raw_decode_sample.height()},
      {QStringLiteral("decoder"), QStringLiteral("decode_raw_image on copied camera CR2")},
  });
  if (!persist_report()) return fail("the real RAW decode receipt should be writable");

  app_shell.completeFirstRunExperience();
  catalog_assets.setInteractiveRenderingActive(true);
  engine.load(QUrl::fromLocalFile(main_qml_path));
  if (engine.rootObjects().isEmpty()) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("main-load-before-import"));
    (void)persist_report();
    return fail("the real-shoot acceptance should load the production Main.qml shell");
  }
  auto* main_root = engine.rootObjects().constFirst();
  if (!QMetaObject::invokeMethod(main_root, "beginImportWorkspace")) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("import-route"));
    (void)persist_report();
    return fail("the production Main.qml import route should open");
  }
  auto* import_workspace = find_named_object(main_root, QStringLiteral("importWorkspace"));
  if (!import_workspace) return fail("the production ImportView should be loaded before staging real media");
  QVariantList staged_paths;
  for (const auto& path : runtime_media_paths) staged_paths.push_back(path);
  QVariant staged_result;
  if (!QMetaObject::invokeMethod(
          import_workspace, "stagePickedFiles", Q_RETURN_ARG(QVariant, staged_result),
          Q_ARG(QVariant, QVariant(staged_paths))) || !staged_result.toBool()) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("import-stage-selection"));
    (void)persist_report();
    return fail("ImportView should stage all three copied CR2s through its selected-file path");
  }
  import_workspace->setProperty("workflowProfile", QStringLiteral("studio"));
  import_workspace->setProperty("importMode", QStringLiteral("referenced"));
  import_workspace->setProperty("pendingImportRootPath", source_root);
  import_workspace->setProperty("pendingImportPaths", QVariant::fromValue(runtime_media_paths));
  import_workspace->setProperty("importProjectChooserOpen", true);
  if (!QMetaObject::invokeMethod(import_workspace, "refreshImportPreflight") ||
      !wait_for_condition([&]() { return !catalog_assets.importPreflightBusy(); }, 30000)) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("import-preflight"));
    (void)persist_report();
    return fail("production ImportView preflight should finish for the copied RAW selection");
  }
  const auto import_preflight = import_workspace->property("importPreflight").toMap();
  if (!import_preflight.value(QStringLiteral("ok")).toBool() ||
      import_preflight.value(QStringLiteral("importableCount")).toInt() != runtime_media_paths.size() ||
      !import_workspace->property("canConfirmImport").toBool()) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("import-preflight-review"));
    report.insert(QStringLiteral("preflight"), QJsonObject::fromVariantMap(import_preflight));
    (void)persist_report();
    return fail("reviewed ImportView preflight should admit all three copied CR2 files");
  }
  if (!QMetaObject::invokeMethod(import_workspace, "confirmSelectedImport")) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("import-confirm"));
    (void)persist_report();
    return fail("the real-shoot import should enter the production confirmation handler");
  }
  const auto import_finished = wait_for_condition([&]() {
    const auto latest = job_queue.latestJobForKind(QStringLiteral("catalog_import"));
    return !latest.isEmpty() && latest.value(QStringLiteral("status")).toString() == QStringLiteral("Completed") &&
           !catalog_assets.importing() && !catalog_assets.loading() &&
           catalog_assets.assetCount() == runtime_media_paths.size();
  }, 120000);
  const auto import_job = job_queue.latestJobForKind(QStringLiteral("catalog_import"));
  if (!import_finished) {
    std::cerr << "Real-shoot import diagnostic: job="
              << import_job.value(QStringLiteral("status")).toString().toStdString()
              << " model=" << catalog_assets.status().toStdString()
              << " assets=" << catalog_assets.assetCount()
              << " error=" << catalog_assets.errorMessage().toStdString() << '\n';
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("production-import-job"));
    report.insert(QStringLiteral("importJob"), QJsonObject::fromVariantMap(import_job));
    (void)persist_report();
    return fail("the production JobRunner import should complete and return all RAWs to the catalog");
  }
  const auto import_metadata = import_job.value(QStringLiteral("metadata")).toMap();
  report.insert(QStringLiteral("import"), QJsonObject{
      {QStringLiteral("jobId"), import_job.value(QStringLiteral("jobId")).toString()},
      {QStringLiteral("status"), import_job.value(QStringLiteral("status")).toString()},
      {QStringLiteral("mode"), QStringLiteral("referenced")},
      {QStringLiteral("importedCount"), import_metadata.value(QStringLiteral("importResult")).toMap().value(QStringLiteral("importedCount")).toInt()},
      {QStringLiteral("catalogRows"), catalog_assets.assetCount()},
      {QStringLiteral("preflight"), QJsonObject::fromVariantMap(import_preflight)},
  });
  if (!persist_report()) return fail("the completed production import receipt should be writable");

  QStringList asset_ids;
  QStringList imported_paths;
  for (auto index = 0; index < catalog_assets.assetCount(); ++index) {
    const auto asset = catalog_assets.assetAt(index);
    const auto path = asset.value(QStringLiteral("filePath")).toString();
    const auto id = asset.value(QStringLiteral("assetId")).toString();
    if (id.isEmpty() || !runtime_media_paths.contains(path) ||
        asset.value(QStringLiteral("mediaType")).toString() != QStringLiteral("photo") ||
        !asset.value(QStringLiteral("online")).toBool()) {
      report.insert(QStringLiteral("status"), QStringLiteral("failed"));
      report.insert(QStringLiteral("failureStage"), QStringLiteral("imported-catalog-row"));
      (void)persist_report();
      return fail("the real import should yield online photo rows linked to the copied source files");
    }
    asset_ids.push_back(id);
    imported_paths.push_back(path);
  }
  std::sort(asset_ids.begin(), asset_ids.end());
  std::sort(imported_paths.begin(), imported_paths.end());

  const auto find_asset_index = [&](const QString& file_path) {
    for (auto index = 0; index < catalog_assets.assetCount(); ++index) {
      if (catalog_assets.assetAt(index).value(QStringLiteral("filePath")).toString() == file_path) return index;
    }
    return -1;
  };
  const auto first_index = find_asset_index(runtime_media_paths.at(0));
  const auto second_index = find_asset_index(runtime_media_paths.at(1));
  const auto third_index = find_asset_index(runtime_media_paths.at(2));
  if (first_index < 0 || second_index < 0 || third_index < 0) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("imported-path-map"));
    (void)persist_report();
    return fail("all imported CR2 paths should map to distinct catalog assets");
  }
  const auto first_asset_id = catalog_assets.assetAt(first_index)
                                  .value(QStringLiteral("assetId")).toString();
  const auto second_asset_id = catalog_assets.assetAt(second_index)
                                   .value(QStringLiteral("assetId")).toString();
  const auto third_asset_id = catalog_assets.assetAt(third_index)
                                  .value(QStringLiteral("assetId")).toString();
  const auto find_visible_asset_index = [&](const QString& asset_id) {
    for (auto index = 0; index < catalog_assets.assetCount(); ++index) {
      if (catalog_assets.assetAt(index).value(QStringLiteral("assetId")).toString() == asset_id) {
        return index;
      }
    }
    return -1;
  };
  const auto focus_asset_by_id = [&](const QString& asset_id) {
    const auto index = find_visible_asset_index(asset_id);
    if (index < 0) return false;
    catalog_assets.clearSelection();
    catalog_assets.selectIndex(index);
    return catalog_assets.selectedAssetId() == asset_id;
  };
  const auto select_asset_by_id = [&](const QString& asset_id) {
    if (!focus_asset_by_id(asset_id)) return false;
    const auto index = find_visible_asset_index(asset_id);
    catalog_assets.selectIndexWithModifiers(
        index, static_cast<int>(Qt::NoModifier), true);
    return catalog_assets.selectedAssetIds() == QStringList{asset_id};
  };

  main_root->setProperty("libraryWorkspaceMode", QStringLiteral("catalog"));
  main_root->setProperty("currentWorkspace", main_root->property("libraryWorkspaceIndex"));
  if (!wait_for_condition([&]() {
        return item_visible(find_named_item(main_root, QStringLiteral("libraryImagesCollectionButton"))) &&
               !main_root->property("workspaceTransitionActive").toBool();
      }, 20000)) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("library-route"));
    (void)persist_report();
    return fail("the production Library image collection should show the imported real photos");
  }
  if (!focus_asset_by_id(first_asset_id)) {
    return fail("the first imported photo should remain addressable by its stable catalog identity");
  }
  catalog_assets.setSelectedRating(5);
  catalog_assets.toggleSelectedPick();
  if (!focus_asset_by_id(second_asset_id)) {
    return fail("the second imported photo should remain addressable after culling resorts the catalog");
  }
  catalog_assets.setSelectedRating(4);
  if (!focus_asset_by_id(third_asset_id)) {
    return fail("the third imported photo should remain addressable after culling resorts the catalog");
  }
  catalog_assets.toggleSelectedReject();
  const auto picked_asset_id = first_asset_id;
  const auto rated_asset = catalog_assets.assetById(second_asset_id);
  const auto rejected_asset = catalog_assets.assetById(third_asset_id);
  if (!catalog_assets.assetById(picked_asset_id).value(QStringLiteral("picked")).toBool() ||
      catalog_assets.assetById(picked_asset_id).value(QStringLiteral("rating")).toInt() != 5 ||
      rated_asset.value(QStringLiteral("rating")).toInt() != 4 ||
      !rejected_asset.value(QStringLiteral("rejected")).toBool()) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("culling"));
    (void)persist_report();
    return fail("the imported shoot should support distinct production rating, pick, and reject decisions");
  }
  report.insert(QStringLiteral("culling"), QJsonObject{
      {QStringLiteral("pickedAssetId"), picked_asset_id},
      {QStringLiteral("pickedRating"), 5},
      {QStringLiteral("secondAssetRating"), 4},
      {QStringLiteral("rejectedAssetId"), rejected_asset.value(QStringLiteral("assetId")).toString()},
  });

  if (!select_asset_by_id(picked_asset_id)) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("edit-selection"));
    (void)persist_report();
    return fail("opening Edit should start from the picked asset's explicit, identity-stable selection");
  }
  QQuickItem* edit_viewport = nullptr;
  auto* edit_window = qobject_cast<QQuickWindow*>(main_root);
  const auto edit_open_invoked =
      QMetaObject::invokeMethod(main_root, "openSelectedPhotoInEdit");
  if (edit_window != nullptr &&
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER")) {
    activate_native_test_application();
    edit_window->show();
    edit_window->raise();
    edit_window->requestActivate();
  }
  const auto edit_preview_ready = edit_open_invoked && wait_for_condition([&]() {
        edit_viewport = qobject_cast<QQuickItem*>(
            find_named_object(main_root, QStringLiteral("editImageViewport")));
        if (edit_window != nullptr && !edit_window->isExposed() &&
            qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER")) {
          activate_native_test_application();
          edit_window->show();
          edit_window->raise();
          edit_window->requestActivate();
        }
        const auto display_source = editor_preview.displayPreviewSource();
        const auto native_size = editor_preview.displayPreviewNativeSize();
        const auto native_surface_ready = editor_preview.displayPreviewNativeAvailable() &&
            native_size.width() > 0 && native_size.height() > 0;
        const auto provider_surface_ready = display_source.startsWith(
                QStringLiteral("image://editor-preview/")) && edit_viewport != nullptr &&
            edit_viewport->property("source").toString() == display_source &&
            edit_viewport->property("providerPreviewVisible").toBool();
        return main_root->property("currentWorkspace").toInt() == main_root->property("editWorkspaceIndex").toInt() &&
               !main_root->property("workspaceTransitionActive").toBool() &&
               editor_preview.assetId() == picked_asset_id &&
               editor_preview.sourceImageWidth() > 0 && editor_preview.sourceImageHeight() > 0 &&
               (provider_surface_ready || native_surface_ready) &&
               edit_viewport != nullptr && item_visible(edit_viewport) &&
               edit_viewport->property("paintedContentWidth").toDouble() > 0.0 &&
               edit_viewport->property("paintedContentHeight").toDouble() > 0.0 &&
               edit_window != nullptr && edit_window->isExposed() && edit_window->isVisible() &&
               edit_window->width() > 0 && edit_window->height() > 0;
      }, 90000);
  if (!edit_preview_ready) {
    const auto native_size = editor_preview.displayPreviewNativeSize();
    const QJsonObject preview_diagnostic{
        {QStringLiteral("openSelectedPhotoInvoked"), edit_open_invoked},
        {QStringLiteral("workspace"), main_root->property("currentWorkspace").toInt()},
        {QStringLiteral("editWorkspace"), main_root->property("editWorkspaceIndex").toInt()},
        {QStringLiteral("workspaceTransitionActive"), main_root->property("workspaceTransitionActive").toBool()},
        {QStringLiteral("selectedAssetId"), catalog_assets.selectedAssetId()},
        {QStringLiteral("selectedAssetCount"), catalog_assets.selectedAssetCount()},
        {QStringLiteral("selectedMediaType"), catalog_assets.selectedMediaType()},
        {QStringLiteral("selectedFilePath"), catalog_assets.selectedFilePath()},
        {QStringLiteral("selectedFileReadable"), QFileInfo(catalog_assets.selectedFilePath()).isReadable()},
        {QStringLiteral("editorAssetId"), editor_preview.assetId()},
        {QStringLiteral("previewSource"), editor_preview.previewSource()},
        {QStringLiteral("displayPreviewSource"), editor_preview.displayPreviewSource()},
        {QStringLiteral("displayPreviewNativeAvailable"), editor_preview.displayPreviewNativeAvailable()},
        {QStringLiteral("displayPreviewNativeWidth"), native_size.width()},
        {QStringLiteral("displayPreviewNativeHeight"), native_size.height()},
        {QStringLiteral("viewportFound"), edit_viewport != nullptr},
        {QStringLiteral("viewportVisible"), edit_viewport != nullptr && item_visible(edit_viewport)},
        {QStringLiteral("viewportSource"), edit_viewport != nullptr
             ? edit_viewport->property("source").toString() : QString{}},
        {QStringLiteral("viewportProviderVisible"), edit_viewport != nullptr &&
             edit_viewport->property("providerPreviewVisible").toBool()},
        {QStringLiteral("viewportPaintedWidth"), edit_viewport != nullptr
             ? edit_viewport->property("paintedContentWidth").toDouble() : 0.0},
        {QStringLiteral("viewportPaintedHeight"), edit_viewport != nullptr
             ? edit_viewport->property("paintedContentHeight").toDouble() : 0.0},
        {QStringLiteral("editWindowFound"), edit_window != nullptr},
        {QStringLiteral("editWindowExposed"), edit_window != nullptr && edit_window->isExposed()},
        {QStringLiteral("editWindowVisible"), edit_window != nullptr && edit_window->isVisible()},
        {QStringLiteral("editWindowWidth"), edit_window != nullptr ? edit_window->width() : 0},
        {QStringLiteral("editWindowHeight"), edit_window != nullptr ? edit_window->height() : 0},
        {QStringLiteral("sourceWidth"), editor_preview.sourceImageWidth()},
        {QStringLiteral("sourceHeight"), editor_preview.sourceImageHeight()},
        {QStringLiteral("status"), editor_preview.status()},
        {QStringLiteral("selectionFailureReason"), editor_preview.selectionFailureReason()},
        {QStringLiteral("previewFailureStage"), editor_preview.previewFailureStage()},
        {QStringLiteral("previewFailureDetail"), editor_preview.previewFailureDetail()},
        {QStringLiteral("processing"), editor_preview.processing()},
        {QStringLiteral("fullRenderRunning"), editor_preview.fullRenderRunning()},
    };
    report.insert(QStringLiteral("editPreviewDiagnostic"), preview_diagnostic);
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("real-raw-edit-preview"));
    (void)persist_report();
    std::cerr << "Real-shoot RAW edit preview diagnostic: "
              << QJsonDocument(preview_diagnostic).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
    return fail("Main.qml should open the imported CR2 in Edit and render its decoded photo preview");
  }
  edit_viewport = qobject_cast<QQuickItem*>(
      find_named_object(main_root, QStringLiteral("editImageViewport")));
  edit_window = qobject_cast<QQuickWindow*>(main_root);
  if (edit_viewport == nullptr || edit_window == nullptr) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("edit-preview-capture"));
    (void)persist_report();
    return fail("the visible Edit viewport should remain available for a native screenshot");
  }
  for (auto frame = 0; frame < 8; ++frame) {
    process_layout_events(8);
  }
  edit_window->update();
  process_layout_events(16);
  const auto screenshot = edit_window->grabWindow();
  const auto screenshot_path = QDir(run_root).filePath(QStringLiteral("edit-preview-window.png"));
  const auto photo_rect_scene = edit_viewport->mapRectToScene(QRectF(
      edit_viewport->property("paintedContentX").toDouble(),
      edit_viewport->property("paintedContentY").toDouble(),
      edit_viewport->property("paintedContentWidth").toDouble(),
      edit_viewport->property("paintedContentHeight").toDouble()));
  const auto screenshot_scale = screenshot.devicePixelRatio();
  const QRect photo_rect_pixels(
      qFloor(photo_rect_scene.left() * screenshot_scale),
      qFloor(photo_rect_scene.top() * screenshot_scale),
      qCeil(photo_rect_scene.width() * screenshot_scale),
      qCeil(photo_rect_scene.height() * screenshot_scale));
  const auto bounded_photo_rect = photo_rect_pixels.intersected(screenshot.rect());
  const auto photo_capture = screenshot.copy(bounded_photo_rect);
  QSet<quint32> sampled_color_bins;
  auto minimum_luminance = 255;
  auto maximum_luminance = 0;
  constexpr auto sample_grid = 12;
  if (!photo_capture.isNull()) {
    for (auto y = 0; y < sample_grid; ++y) {
      for (auto x = 0; x < sample_grid; ++x) {
        const auto sample_x = qMin(
            photo_capture.width() - 1,
            static_cast<int>((static_cast<qint64>(x) * 2 + 1) * photo_capture.width() /
                             (sample_grid * 2)));
        const auto sample_y = qMin(
            photo_capture.height() - 1,
            static_cast<int>((static_cast<qint64>(y) * 2 + 1) * photo_capture.height() /
                             (sample_grid * 2)));
        const auto color = photo_capture.pixelColor(sample_x, sample_y);
        const auto luma = (299 * color.red() + 587 * color.green() + 114 * color.blue()) / 1000;
        minimum_luminance = qMin(minimum_luminance, luma);
        maximum_luminance = qMax(maximum_luminance, luma);
        sampled_color_bins.insert(
            (static_cast<quint32>(color.red() >> 4) << 8) |
            (static_cast<quint32>(color.green() >> 4) << 4) |
            static_cast<quint32>(color.blue() >> 4));
      }
    }
  }
  const auto screenshot_saved = !screenshot.isNull() && screenshot.save(screenshot_path) &&
      !photo_capture.isNull() && photo_capture.save(QDir(run_root).filePath(
          QStringLiteral("edit-preview-photo.png")));
  const auto luminance_range = maximum_luminance - minimum_luminance;
  const QJsonObject preview_capture_receipt{
      {QStringLiteral("windowScreenshotPath"), screenshot_path},
      {QStringLiteral("photoCropPath"), QDir(run_root).filePath(QStringLiteral("edit-preview-photo.png"))},
      {QStringLiteral("screenshotWidth"), screenshot.width()},
      {QStringLiteral("screenshotHeight"), screenshot.height()},
      {QStringLiteral("devicePixelRatio"), screenshot_scale},
      {QStringLiteral("windowExposed"), edit_window->isExposed()},
      {QStringLiteral("windowVisible"), edit_window->isVisible()},
      {QStringLiteral("photoCropWidth"), photo_capture.width()},
      {QStringLiteral("photoCropHeight"), photo_capture.height()},
      {QStringLiteral("sampleGrid"), sample_grid * sample_grid},
      {QStringLiteral("distinctColorBins"), sampled_color_bins.size()},
      {QStringLiteral("sampledLuminanceRange"), luminance_range},
      {QStringLiteral("nativeSurfaceAvailable"), editor_preview.displayPreviewNativeAvailable()},
      {QStringLiteral("nativeSurfaceWidth"), editor_preview.displayPreviewNativeSize().width()},
      {QStringLiteral("nativeSurfaceHeight"), editor_preview.displayPreviewNativeSize().height()},
  };
  if (!screenshot_saved || sampled_color_bins.size() < 8 || luminance_range < 10) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("edit-preview-capture"));
    report.insert(QStringLiteral("editPreviewCapture"), preview_capture_receipt);
    (void)persist_report();
    return fail("the real RAW photo should be visibly painted with measurable image pixel variation");
  }
  report.insert(QStringLiteral("editPreviewCapture"), preview_capture_receipt);
  const auto initial_mask_count = editor_preview.maskCount();
  editor_preview.setExposure(0.35);
  editor_preview.addMask(QStringLiteral("linear"));
  editor_preview.setActiveMaskExposure(0.12);
  editor_preview.setActiveMaskFeather(0.04);
  const auto recipe_patch = editor_preview.currentAssetBoundAdjustmentPatchJson();
  const auto recipe_patch_state = photo_editor::desktop::parse_adjustment_patch_json(recipe_patch);
  const auto serialized_mask_exposure = recipe_patch_state.ok() &&
          !recipe_patch_state.state->mask_stack.isEmpty()
      ? recipe_patch_state.state->mask_stack.constFirst().toMap()
            .value(QStringLiteral("exposure"), qQNaN()).toDouble()
      : qQNaN();
  if (editor_preview.maskCount() != initial_mask_count + 1 ||
      qAbs(editor_preview.exposure() - 0.35) > 0.001 ||
      qAbs(editor_preview.activeMaskExposure() - 0.12) > 0.001 || recipe_patch.isEmpty() ||
      !(qAbs(serialized_mask_exposure - 0.12) <= 0.001)) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("mask-refine"));
    (void)persist_report();
    return fail("the real CR2 edit should include a global exposure and refined local mask adjustment");
  }
  (void)catalog_assets.applyAdjustmentPatchJsonToAssetIds({picked_asset_id}, recipe_patch);
  const auto first_patch_before_batch = catalog_assets.assetById(picked_asset_id)
                                            .value(QStringLiteral("adjustmentPatchJson")).toString();
  const auto second_patch_before_batch = catalog_assets.assetById(second_asset_id)
                                             .value(QStringLiteral("adjustmentPatchJson")).toString();
  const auto third_patch_before_batch = catalog_assets.assetById(
      third_asset_id)
                                             .value(QStringLiteral("adjustmentPatchJson")).toString();
  auto* edit_workspace = find_named_object(main_root, QStringLiteral("editStudioWorkspace"));
  const QVariantList batch_sync_groups{
      QStringLiteral("tone"), QStringLiteral("masks")};
  if (edit_workspace == nullptr ||
      !QMetaObject::invokeMethod(edit_workspace, "activateBaseImageLayerTarget") ||
      !QMetaObject::invokeMethod(
          edit_workspace, "activateEditInspectorTab",
          Q_ARG(QVariant, QVariant(QStringLiteral("adjust"))),
          Q_ARG(QVariant, QVariant())) ||
      !edit_workspace->setProperty("batchSyncGroups", batch_sync_groups)) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("batch-ui-setup"));
    (void)persist_report();
    return fail("the real Edit inspector should expose its selected-photo synchronization controls");
  }
  catalog_assets.clearSelection();
  const auto batch_first_index = find_visible_asset_index(picked_asset_id);
  const auto batch_second_index = find_visible_asset_index(second_asset_id);
  if (batch_first_index < 0 || batch_second_index < 0) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("batch-selection"));
    (void)persist_report();
    return fail("the two batch targets should remain visible by stable catalog identity");
  }
  catalog_assets.selectIndex(batch_first_index);
  catalog_assets.selectIndexWithModifiers(
      batch_second_index, static_cast<int>(Qt::ControlModifier), true);
  const auto batch_targets = catalog_assets.selectedAssetIds();
  if (batch_targets.size() != 2 || !batch_targets.contains(picked_asset_id) ||
      !batch_targets.contains(rated_asset.value(QStringLiteral("assetId")).toString())) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("batch-selection"));
    (void)persist_report();
    return fail("selective batch editing should retain exactly the two chosen catalog targets");
  }
  const auto batch_history_entries_before = catalog_assets.batchRecipeHistoryEntries().size();
  const auto batch_history_cursor_before = catalog_assets.batchRecipeHistoryCurrentIndex();
  auto* batch_sync_button = qobject_cast<QQuickItem*>(
      find_named_object(edit_workspace, QStringLiteral("selectiveBatchSyncApplyButton")));
  if (batch_sync_button == nullptr || !item_visible(batch_sync_button) ||
      !batch_sync_button->property("enabled").toBool() ||
      !send_activation_key(batch_sync_button)) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("batch-ui-apply"));
    (void)persist_report();
    return fail("Selective Sync should be visible and apply from its real Edit button");
  }
  const auto first_patch = catalog_assets.assetById(picked_asset_id)
                               .value(QStringLiteral("adjustmentPatchJson")).toString();
  const auto second_patch = catalog_assets.assetById(second_asset_id)
                                .value(QStringLiteral("adjustmentPatchJson")).toString();
  const auto third_patch_after_batch = catalog_assets.assetById(third_asset_id)
                                           .value(QStringLiteral("adjustmentPatchJson")).toString();
  const auto second_patch_state = photo_editor::desktop::parse_adjustment_patch_json(second_patch);
  const auto first_patch_state = photo_editor::desktop::parse_adjustment_patch_json(first_patch);
  const auto second_tone_matches = recipe_patch_state.ok() && second_patch_state.ok() &&
      qAbs(recipe_patch_state.state->exposure - second_patch_state.state->exposure) < 0.001;
  const auto second_mask_stack_matches = recipe_patch_state.ok() && second_patch_state.ok() &&
      recipe_patch_state.state->mask_stack == second_patch_state.state->mask_stack;
  const auto first_serialized_mask_exposure = first_patch_state.ok() &&
          !first_patch_state.state->mask_stack.isEmpty()
      ? first_patch_state.state->mask_stack.constFirst().toMap()
            .value(QStringLiteral("exposure"), qQNaN()).toDouble()
      : qQNaN();
  const auto second_serialized_mask_exposure = second_patch_state.ok() &&
          !second_patch_state.state->mask_stack.isEmpty()
      ? second_patch_state.state->mask_stack.constFirst().toMap()
            .value(QStringLiteral("exposure"), qQNaN()).toDouble()
      : qQNaN();
  const auto refined_mask_exposure_persisted =
      qAbs(first_serialized_mask_exposure - 0.12) <= 0.001 &&
      qAbs(second_serialized_mask_exposure - 0.12) <= 0.001;
  const auto picked_patch_unchanged = first_patch == first_patch_before_batch;
  const auto second_patch_changed = second_patch != second_patch_before_batch;
  const auto batch_changed = static_cast<int>(picked_patch_unchanged ? 0 : 1) +
      static_cast<int>(second_patch_changed ? 1 : 0);
  const auto batch_history_entries = catalog_assets.batchRecipeHistoryEntries();
  const auto expected_batch_history_size = batch_history_entries_before + 1;
  const auto batch_undo_label = catalog_assets.batchRecipeUndoLabel();
  const auto batch_history_entry = batch_history_entries.isEmpty()
      ? QVariantMap{} : batch_history_entries.constLast().toMap();
  const QJsonObject batch_diagnostic{
      {QStringLiteral("batchTargets"), QJsonArray::fromStringList(batch_targets)},
      {QStringLiteral("batchChanged"), batch_changed},
      {QStringLiteral("pickedAssetId"), picked_asset_id},
      {QStringLiteral("secondAssetId"), second_asset_id},
      {QStringLiteral("thirdAssetId"), third_asset_id},
      {QStringLiteral("firstPatchPresent"), !first_patch.isEmpty()},
      {QStringLiteral("secondPatchPresent"), !second_patch.isEmpty()},
      {QStringLiteral("firstPatchEqualsRequested"), first_patch == recipe_patch},
      {QStringLiteral("secondPatchEqualsRequested"), second_patch == recipe_patch},
      {QStringLiteral("secondToneMatchesRequested"), second_tone_matches},
      {QStringLiteral("secondMaskStackMatchesRequested"), second_mask_stack_matches},
      {QStringLiteral("requestedSerializedMaskExposure"), serialized_mask_exposure},
      {QStringLiteral("pickedSerializedMaskExposure"), first_serialized_mask_exposure},
      {QStringLiteral("secondSerializedMaskExposure"), second_serialized_mask_exposure},
      {QStringLiteral("refinedMaskExposurePersisted"), refined_mask_exposure_persisted},
      {QStringLiteral("pickedPatchUnchanged"), picked_patch_unchanged},
      {QStringLiteral("secondPatchChanged"), second_patch_changed},
      {QStringLiteral("thirdPatchUnchanged"), third_patch_after_batch == third_patch_before_batch},
      {QStringLiteral("batchUndoAvailable"), catalog_assets.canUndoBatchRecipe()},
      {QStringLiteral("batchUndoLabel"), batch_undo_label},
      {QStringLiteral("historyEntriesBefore"), batch_history_entries_before},
      {QStringLiteral("historyEntriesAfter"), batch_history_entries.size()},
      {QStringLiteral("historyCursorAfter"), catalog_assets.batchRecipeHistoryCurrentIndex()},
      {QStringLiteral("historyLabel"), batch_history_entry.value(QStringLiteral("label")).toString()},
      {QStringLiteral("historyDetail"), batch_history_entry.value(QStringLiteral("detail")).toString()},
  };
  if (batch_changed != 1 || first_patch.isEmpty() || second_patch.isEmpty() ||
      !picked_patch_unchanged || !second_patch_changed || !second_tone_matches ||
      !second_mask_stack_matches || !refined_mask_exposure_persisted ||
      third_patch_after_batch != third_patch_before_batch ||
      !catalog_assets.canUndoBatchRecipe() || batch_undo_label !=
          QStringLiteral("Synced adjustments across 1 photo") ||
      batch_history_entries.size() != expected_batch_history_size ||
      catalog_assets.batchRecipeHistoryCurrentIndex() != batch_history_cursor_before + 1 ||
      batch_history_entry.value(QStringLiteral("detail")).toString() !=
          QStringLiteral("1 photo changed together")) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("selective-batch-edit"));
    report.insert(QStringLiteral("selectiveBatchDiagnostic"), batch_diagnostic);
    (void)persist_report();
    std::cerr << "Real-shoot selective batch diagnostic: "
              << QJsonDocument(batch_diagnostic).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
    return fail("visible Selective Sync should update only its changed target, preserve the selected source and rejected third asset, and create one undo step");
  }
  catalog_assets.undoBatchRecipe();
  const auto batch_undo_verified = catalog_assets.canRedoBatchRecipe() &&
      catalog_assets.assetById(second_asset_id)
          .value(QStringLiteral("adjustmentPatchJson")).toString() == second_patch_before_batch &&
      catalog_assets.assetById(picked_asset_id)
          .value(QStringLiteral("adjustmentPatchJson")).toString() == first_patch_before_batch &&
      catalog_assets.assetById(third_asset_id)
          .value(QStringLiteral("adjustmentPatchJson")).toString() == third_patch_before_batch;
  catalog_assets.redoBatchRecipe();
  const auto batch_redo_verified = !catalog_assets.canRedoBatchRecipe() &&
      catalog_assets.assetById(second_asset_id)
          .value(QStringLiteral("adjustmentPatchJson")).toString() == second_patch &&
      catalog_assets.assetById(picked_asset_id)
          .value(QStringLiteral("adjustmentPatchJson")).toString() == first_patch &&
      catalog_assets.assetById(third_asset_id)
          .value(QStringLiteral("adjustmentPatchJson")).toString() == third_patch_before_batch;
  const auto history_count_after_redo = catalog_assets.batchRecipeHistoryEntries().size();
  const auto history_cursor_after_redo = catalog_assets.batchRecipeHistoryCurrentIndex();
  if (!batch_undo_verified || !batch_redo_verified ||
      !send_activation_key(batch_sync_button) ||
      catalog_assets.batchRecipeHistoryEntries().size() != history_count_after_redo ||
      catalog_assets.batchRecipeHistoryCurrentIndex() != history_cursor_after_redo ||
      catalog_assets.canRedoBatchRecipe()) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("batch-undo-redo"));
    report.insert(QStringLiteral("batchUndoVerified"), batch_undo_verified);
    report.insert(QStringLiteral("batchRedoVerified"), batch_redo_verified);
    report.insert(QStringLiteral("noOpSyncDidNotAddHistory"),
        catalog_assets.batchRecipeHistoryEntries().size() == history_count_after_redo &&
        catalog_assets.batchRecipeHistoryCurrentIndex() == history_cursor_after_redo &&
        !catalog_assets.canRedoBatchRecipe());
    (void)persist_report();
    return fail("the selected-photo synchronization should undo and redo as one entry and a no-op repeat should add no history");
  }
  report.insert(QStringLiteral("refineAndBatch"), QJsonObject{
      {QStringLiteral("rawAssetId"), picked_asset_id},
      {QStringLiteral("maskCount"), editor_preview.maskCount()},
      {QStringLiteral("serializedMaskExposure"), serialized_mask_exposure},
      {QStringLiteral("activeMaskExposureAfterTargetSwitch"), editor_preview.activeMaskExposure()},
      {QStringLiteral("batchSyncGroups"), QJsonArray::fromStringList({QStringLiteral("tone"), QStringLiteral("masks")})},
      {QStringLiteral("batchTargetIds"), QJsonArray::fromStringList(batch_targets)},
      {QStringLiteral("batchUpdatedAssets"), batch_changed},
      {QStringLiteral("rejectedPatchSha256"), QString::fromLatin1(
          QCryptographicHash::hash(third_patch_before_batch.toUtf8(), QCryptographicHash::Sha256).toHex())},
      {QStringLiteral("unselectedThirdAssetUnchanged"), true},
      {QStringLiteral("batchUndoVerified"), batch_undo_verified},
      {QStringLiteral("batchRedoVerified"), batch_redo_verified},
      {QStringLiteral("noOpSyncDidNotAddHistory"), true},
      {QStringLiteral("editPreviewAssetId"), editor_preview.assetId()},
      {QStringLiteral("editPreviewCatalogFilePath"), catalog_assets.assetById(picked_asset_id)
           .value(QStringLiteral("filePath")).toString()},
  });
  if (!persist_report()) return fail("the batch and mask refinement receipt should be writable");

  const auto destination_root = QDir(output_root).filePath(QStringLiteral("exports"));
  if (!QDir().mkpath(destination_root)) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("export-failure-setup"));
    (void)persist_report();
    return fail("the isolated export destination should exist before the controlled permission failure");
  }
  const auto original_destination_permissions = QFileInfo(destination_root).permissions();
  const auto blocked_destination_permissions = original_destination_permissions &
      ~(QFileDevice::WriteOwner | QFileDevice::WriteUser | QFileDevice::WriteGroup |
        QFileDevice::WriteOther);
  if (!QFile::setPermissions(destination_root, blocked_destination_permissions)) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("export-failure-setup"));
    (void)persist_report();
    return fail("the isolated export destination should be read-only before production export admission");
  }
  QSet<QString> prior_export_ids;
  for (const auto& value : export_jobs.jobs()) {
    prior_export_ids.insert(value.toMap().value(QStringLiteral("jobId")).toString());
  }
  if (!export_jobs.queueExport(
          picked_asset_id, QStringLiteral("photo"),
          QFileInfo(runtime_media_paths.at(0)).completeBaseName(),
          runtime_media_paths.at(0), editor_preview.previewSource(), recipe_patch,
          QStringLiteral("JPEG"), destination_root)) {
    (void)QFile::setPermissions(destination_root, original_destination_permissions);
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("export-admission"));
    (void)persist_report();
    return fail("the real imported photo should enter the production JPEG export controller");
  }
  QString export_job_id;
  const auto export_failed = wait_for_condition([&]() {
    for (const auto& value : export_jobs.jobs()) {
      const auto job = value.toMap();
      const auto id = job.value(QStringLiteral("jobId")).toString();
      if (!prior_export_ids.contains(id) &&
          job.value(QStringLiteral("assetId")).toString() == picked_asset_id) {
        export_job_id = id;
        return job.value(QStringLiteral("status")).toString() == QStringLiteral("Failed");
      }
    }
    return false;
  }, 60000);
  const auto destination_permissions_restored =
      QFile::setPermissions(destination_root, original_destination_permissions);
  QVariantMap failed_export_job;
  for (const auto& value : export_jobs.jobs()) {
    const auto job = value.toMap();
    if (job.value(QStringLiteral("jobId")).toString() == export_job_id) failed_export_job = job;
  }
  if (!destination_permissions_restored || !export_failed || export_job_id.isEmpty() ||
      failed_export_job.value(QStringLiteral("errorMessage")).toString().isEmpty()) {
    std::cerr << "Real-shoot export failure diagnostic: id=" << export_job_id.toStdString()
              << " status=" << failed_export_job.value(QStringLiteral("status")).toString().toStdString()
              << " error=" << failed_export_job.value(QStringLiteral("errorMessage")).toString().toStdString() << '\n';
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("export-failure-observation"));
    report.insert(QStringLiteral("destinationPermissionsRestored"), destination_permissions_restored);
    (void)persist_report();
    return fail("the queued production export should record a real destination failure before retry");
  }
  if (!QFileInfo(destination_root).isDir() ||
      !QFileInfo(destination_root).isWritable()) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("export-retry-destination"));
    (void)persist_report();
    return fail("the read-only export destination should be restored and writable before retry");
  }
  export_jobs.retryJob(export_job_id);
  const auto export_completed = wait_for_condition([&]() {
    for (const auto& value : export_jobs.jobs()) {
      const auto job = value.toMap();
      if (job.value(QStringLiteral("jobId")).toString() == export_job_id)
        return job.value(QStringLiteral("status")).toString() == QStringLiteral("Completed");
    }
    return false;
  }, 180000);
  QVariantMap completed_export_job;
  for (const auto& value : export_jobs.jobs()) {
    const auto job = value.toMap();
    if (job.value(QStringLiteral("jobId")).toString() == export_job_id) completed_export_job = job;
  }
  const auto export_path = completed_export_job.value(QStringLiteral("outputPath")).toString();
  QImage exported_image;
  if (!export_completed || !exported_image.load(export_path) ||
      QFileInfo(export_path).size() <= 1024 || sha256_file(export_path).isEmpty()) {
    std::cerr << "Real-shoot export retry diagnostic: id=" << export_job_id.toStdString()
              << " status=" << completed_export_job.value(QStringLiteral("status")).toString().toStdString()
              << " path=" << export_path.toStdString()
              << " error=" << completed_export_job.value(QStringLiteral("errorMessage")).toString().toStdString() << '\n';
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("export-retry-bytes"));
    (void)persist_report();
    return fail("the failed production export should retry successfully and produce decodable image bytes");
  }
  report.insert(QStringLiteral("exportRetry"), QJsonObject{
      {QStringLiteral("jobId"), export_job_id},
      {QStringLiteral("failureStatus"), failed_export_job.value(QStringLiteral("status")).toString()},
      {QStringLiteral("failureMessage"), failed_export_job.value(QStringLiteral("errorMessage")).toString()},
      {QStringLiteral("retryStatus"), completed_export_job.value(QStringLiteral("status")).toString()},
      {QStringLiteral("outputPath"), export_path},
      {QStringLiteral("outputBytes"), QFileInfo(export_path).size()},
      {QStringLiteral("outputSha256"), sha256_file(export_path)},
      {QStringLiteral("decodedWidth"), exported_image.width()},
      {QStringLiteral("decodedHeight"), exported_image.height()},
  });
  if (!persist_report()) return fail("the completed export retry receipt should be writable");

  const auto moved_source_root = QDir(runtime_root.path()).filePath(QStringLiteral("real-shoot/moved/source"));
  if (!QDir().mkpath(QFileInfo(moved_source_root).absolutePath()) ||
      !QDir().rename(source_root, moved_source_root)) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("move-source-copy"));
    (void)persist_report();
    return fail("the isolated source-copy folder should move to simulate offline originals");
  }
  catalog_assets.setAvailabilityFilter(QStringLiteral("all"));
  catalog_assets.refreshOnlineState();
  if (!wait_for_condition([&]() {
        return !catalog_assets.loading() && catalog_assets.assetCount() == runtime_media_paths.size() &&
               catalog_assets.visibleOfflineCount() == runtime_media_paths.size();
      }, 60000)) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("offline-detection-after-move"));
    (void)persist_report();
    return fail("the catalog should report all moved referenced originals offline before relink");
  }
  catalog_assets.setAvailabilityFilter(QStringLiteral("offline"));
  const auto relinked_count = catalog_assets.relinkOfflineAssetsToFolder(
      QUrl::fromLocalFile(moved_source_root).toString());
  catalog_assets.setAvailabilityFilter(QStringLiteral("all"));
  if (relinked_count != runtime_media_paths.size() ||
      !wait_for_condition([&]() {
        if (catalog_assets.loading() || catalog_assets.visibleOnlineCount() != runtime_media_paths.size()) return false;
        for (auto index = 0; index < catalog_assets.assetCount(); ++index) {
          const auto asset = catalog_assets.assetAt(index);
          if (!asset.value(QStringLiteral("online")).toBool() ||
              !asset.value(QStringLiteral("filePath")).toString().startsWith(moved_source_root)) return false;
        }
        return true;
      }, 60000)) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("relink"));
    report.insert(QStringLiteral("relinkedCount"), relinked_count);
    (void)persist_report();
    return fail("the production catalog relink action should restore all moved camera originals");
  }
  report.insert(QStringLiteral("moveAndRelink"), QJsonObject{
      {QStringLiteral("movedFolder"), moved_source_root},
      {QStringLiteral("offlineBeforeRelink"), runtime_media_paths.size()},
      {QStringLiteral("relinkedCount"), relinked_count},
      {QStringLiteral("onlineAfterRelink"), catalog_assets.visibleOnlineCount()},
  });
  if (!persist_report()) return fail("the move and relink receipt should be writable before process reopen");

  CatalogAssetModel reopened_catalog;
  reopened_catalog.refresh();
  if (!wait_for_condition([&]() {
        return !reopened_catalog.loading() && reopened_catalog.assetCount() == runtime_media_paths.size();
      }, 60000)) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("catalog-reopen"));
    (void)persist_report();
    return fail("a fresh catalog model should reopen all three persisted imported photos");
  }
  auto culling_persisted = false;
  auto recipe_persisted = false;
  auto relinks_persisted = true;
  for (const auto& id : asset_ids) {
    const auto asset = reopened_catalog.assetById(id);
    const auto path = asset.value(QStringLiteral("filePath")).toString();
    relinks_persisted &= asset.value(QStringLiteral("online")).toBool() && path.startsWith(moved_source_root);
    if (id == picked_asset_id) {
      culling_persisted = asset.value(QStringLiteral("picked")).toBool() &&
                          asset.value(QStringLiteral("rating")).toInt() == 5;
      recipe_persisted = !asset.value(QStringLiteral("adjustmentPatchJson")).toString().isEmpty();
    }
    if (id == third_asset_id) {
      culling_persisted &= asset.value(QStringLiteral("rejected")).toBool();
      recipe_persisted &= asset.value(QStringLiteral("adjustmentPatchJson")).toString() == third_patch_before_batch;
    }
  }
  if (!culling_persisted || !recipe_persisted || !relinks_persisted) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("durable-reopen-check"));
    (void)persist_report();
    return fail("fresh catalog reopen should preserve culling, selective recipes, and relinked paths");
  }

  for (auto* old_root : engine.rootObjects()) {
    if (auto* window = qobject_cast<QQuickWindow*>(old_root)) window->close();
    delete old_root;
  }
  process_layout_events(4);

  const auto child_receipt_path = QDir(run_root).filePath(QStringLiteral("process-reopen.json"));
  QProcessEnvironment child_environment = QProcessEnvironment::systemEnvironment();
  child_environment.insert(QStringLiteral("PHOTO_EDITOR_SMOKE_STOP_AFTER_REAL_SHOOT_WORKFLOW"),
                           QStringLiteral("1"));
  child_environment.insert(QStringLiteral("PHOTO_EDITOR_REAL_SHOOT_REOPEN_CHILD"), QStringLiteral("1"));
  child_environment.insert(QStringLiteral("PHOTO_EDITOR_REAL_SHOOT_REOPEN_RECEIPT"),
                           QDir(run_root).filePath(QStringLiteral("workflow.json")));
  child_environment.insert(QStringLiteral("PHOTO_EDITOR_REAL_SHOOT_CHILD_RECEIPT"), child_receipt_path);
  child_environment.insert(QStringLiteral("PHOTO_EDITOR_REAL_SHOOT_PARENT_PID"),
                           QString::number(QCoreApplication::applicationPid()));
  child_environment.insert(QStringLiteral("PHOTO_EDITOR_REAL_SHOOT_PROOF_DIR"), proof_root);
  child_environment.insert(QStringLiteral("PHOTO_EDITOR_APP_DATA_DIR"), runtime_root.path());
  child_environment.insert(QStringLiteral("PHOTO_EDITOR_CACHE_DIR"), cache_root.path());
  child_environment.insert(QStringLiteral("PHOTO_EDITOR_DATABASE_URL"), database_url);
  QProcess reopen_process;
  reopen_process.setProcessEnvironment(child_environment);
  reopen_process.setWorkingDirectory(QDir::currentPath());
  reopen_process.setProgram(QCoreApplication::applicationFilePath());
  reopen_process.start();
  const auto child_started = reopen_process.waitForStarted(10000);
  const auto child_finished = child_started && reopen_process.waitForFinished(180000);
  if (!child_finished) {
    reopen_process.kill();
    (void)reopen_process.waitForFinished(5000);
  }
  const auto child_stdout = QString::fromUtf8(reopen_process.readAllStandardOutput());
  const auto child_stderr = QString::fromUtf8(reopen_process.readAllStandardError());
  QFile child_receipt_file(child_receipt_path);
  QJsonObject child_reopen_receipt;
  if (child_receipt_file.open(QIODevice::ReadOnly)) {
    child_reopen_receipt = QJsonDocument::fromJson(child_receipt_file.readAll()).object();
  }
  const auto database_url_hash = QString::fromLatin1(
      QCryptographicHash::hash(database_url.toUtf8(), QCryptographicHash::Sha256).toHex());
  const auto child_reopen_passed = child_finished &&
      reopen_process.exitStatus() == QProcess::NormalExit && reopen_process.exitCode() == 0 &&
      child_reopen_receipt.value(QStringLiteral("schema")).toString() ==
          QStringLiteral("looksy-real-shoot-process-reopen:v1") &&
      child_reopen_receipt.value(QStringLiteral("status")).toString() == QStringLiteral("passed") &&
      child_reopen_receipt.value(QStringLiteral("workflowRunId")).toString() == run_id &&
      child_reopen_receipt.value(QStringLiteral("processSeparationVerified")).toBool() &&
      child_reopen_receipt.value(QStringLiteral("appDataRoot")).toString() == runtime_root.path() &&
      child_reopen_receipt.value(QStringLiteral("cacheRoot")).toString() == cache_root.path() &&
      child_reopen_receipt.value(QStringLiteral("databaseUrlSha256")).toString() == database_url_hash &&
      child_reopen_receipt.value(QStringLiteral("mainWindowExposed")).toBool() &&
      child_reopen_receipt.value(QStringLiteral("exportBytesPersisted")).toBool() &&
      child_reopen_receipt.value(QStringLiteral("pickedThumbnailImageReady")).toBool() &&
      child_reopen_receipt.value(QStringLiteral("allGalleryThumbnailsReady")).toBool() &&
      child_reopen_receipt.value(QStringLiteral("readyGalleryThumbnailCount")).toInt() == 3 &&
      child_reopen_receipt.value(QStringLiteral("pickedThumbnailPixelBins")).toInt() >= 8 &&
      child_reopen_receipt.value(QStringLiteral("pickedThumbnailLuminanceRange")).toInt() >= 10;
  if (!child_reopen_passed) {
    report.insert(QStringLiteral("status"), QStringLiteral("failed"));
    report.insert(QStringLiteral("failureStage"), QStringLiteral("process-reopen"));
    report.insert(QStringLiteral("childProcessStarted"), child_started);
    report.insert(QStringLiteral("childProcessFinished"), child_finished);
    report.insert(QStringLiteral("childExitCode"), reopen_process.exitCode());
    report.insert(QStringLiteral("childReceiptPath"), child_receipt_path);
    report.insert(QStringLiteral("childStdout"), child_stdout);
    report.insert(QStringLiteral("childStderr"), child_stderr);
    (void)persist_report();
    std::cerr << "Real-shoot child reopen diagnostic: stdout=" << child_stdout.toStdString()
              << " stderr=" << child_stderr.toStdString() << '\n';
    return fail("a separate application process should reopen the existing catalog, visible Main Library, edits, relinks, and export");
  }
  report.insert(QStringLiteral("reopen"), QJsonObject{
      {QStringLiteral("freshCatalogModel"), child_reopen_receipt.value(QStringLiteral("freshCatalogModel"))},
      {QStringLiteral("freshProcess"), true},
      {QStringLiteral("processId"), child_reopen_receipt.value(QStringLiteral("processId"))},
      {QStringLiteral("parentProcessId"), QCoreApplication::applicationPid()},
      {QStringLiteral("mainQmlReloaded"), child_reopen_receipt.value(QStringLiteral("mainQmlReloaded"))},
      {QStringLiteral("mainWindowVisible"), child_reopen_receipt.value(QStringLiteral("mainWindowVisible"))},
      {QStringLiteral("mainWindowExposed"), child_reopen_receipt.value(QStringLiteral("mainWindowExposed"))},
      {QStringLiteral("mainWindowScreenshot"), child_reopen_receipt.value(QStringLiteral("mainWindowScreenshot"))},
      {QStringLiteral("pickedThumbnailImageReady"), child_reopen_receipt.value(QStringLiteral("pickedThumbnailImageReady"))},
      {QStringLiteral("allGalleryThumbnailsReady"), child_reopen_receipt.value(QStringLiteral("allGalleryThumbnailsReady"))},
      {QStringLiteral("readyGalleryThumbnailCount"), child_reopen_receipt.value(QStringLiteral("readyGalleryThumbnailCount"))},
      {QStringLiteral("pickedThumbnailCapturePath"), child_reopen_receipt.value(QStringLiteral("pickedThumbnailCapturePath"))},
      {QStringLiteral("pickedThumbnailPixelBins"), child_reopen_receipt.value(QStringLiteral("pickedThumbnailPixelBins"))},
      {QStringLiteral("pickedThumbnailLuminanceRange"), child_reopen_receipt.value(QStringLiteral("pickedThumbnailLuminanceRange"))},
      {QStringLiteral("catalogRows"), child_reopen_receipt.value(QStringLiteral("catalogRows"))},
      {QStringLiteral("sameIsolatedRuntime"), true},
      {QStringLiteral("cullingPersisted"), culling_persisted && child_reopen_receipt.value(QStringLiteral("cullingPersisted")).toBool()},
      {QStringLiteral("selectiveRecipePersisted"), recipe_persisted && child_reopen_receipt.value(QStringLiteral("refinedRecipesPersisted")).toBool()},
      {QStringLiteral("serializedMaskExposure"), child_reopen_receipt.value(QStringLiteral("refinedMaskExposure"))},
      {QStringLiteral("relinkPersisted"), relinks_persisted && child_reopen_receipt.value(QStringLiteral("assetPathsAndAvailabilityPersisted")).toBool()},
      {QStringLiteral("exportBytesPersisted"), child_reopen_receipt.value(QStringLiteral("exportBytesPersisted"))},
      {QStringLiteral("exportSha256"), child_reopen_receipt.value(QStringLiteral("exportSha256"))},
      {QStringLiteral("childReceiptPath"), child_receipt_path},
  });
  report.insert(QStringLiteral("status"), QStringLiteral("passed"));
  report.insert(QStringLiteral("limits"), QJsonArray{
      QStringLiteral("The workflow used copied real Canon CR2 files in a temporary isolated runtime."),
      QStringLiteral("Human artistic/color approval and clean-machine qualification were not assessed."),
      QStringLiteral("The child process reopened the persisted catalog and Main.qml while the parent kept the isolated managed catalog alive; abrupt-crash recovery was not assessed."),
  });
  if (!persist_report()) return fail("the completed real-shoot acceptance artifact should be writable");

  engine.rootContext()->setContextProperty("catalogAssets", &catalog_assets);
  return EXIT_SUCCESS;
}

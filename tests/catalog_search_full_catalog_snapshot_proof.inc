int export_full_catalog_similarity_snapshot(
    photo_editor::desktop::CatalogAssetModel& catalog,
    const QString& destination,
    int expected_photo_count,
    const std::function<bool()>& all_recipes_ready,
    const std::function<int()>& recipe_load_error_count,
    int timeout_ms = 1200000,
    bool refresh_catalog = true) {
  const auto invalid = [](const QString& message) {
    std::cerr << message.toStdString() << '\n';
    return EXIT_FAILURE;
  };
  const QFileInfo destination_info(destination);
  if (!destination_info.isAbsolute() || destination_info.exists() || destination_info.isSymLink() ||
      expected_photo_count < 1 || expected_photo_count > 100000 || !all_recipes_ready || !recipe_load_error_count)
    return invalid(QStringLiteral("Full-catalog snapshot destination or expected scope is invalid."));
  if (catalog.loading() || catalog.deleting() || catalog.importing())
    return invalid(QStringLiteral("Catalog must be idle before the isolated full-catalog refresh."));
  if (refresh_catalog) catalog.refresh();
  if (!wait_for_condition([&] { return !catalog.loading(); }, timeout_ms,
                          "full-catalog-search-snapshot-load"))
    return invalid(QStringLiteral("Timed out loading the catalog snapshot for full-catalog similarity search."));
  if (!catalog.errorMessage().isEmpty())
    return invalid(QStringLiteral("Catalog snapshot failed: %1").arg(catalog.errorMessage()));
  if (catalog.deleting() || catalog.importing())
    return invalid(QStringLiteral("Catalog import or delete work must settle before the search snapshot."));

  const auto initial = catalog.similaritySearchAssets();
  if (initial.isEmpty() || initial.size() > 100000 || catalog.totalAssetCount() != initial.size())
    return invalid(QStringLiteral("Full-catalog scope mismatch: visible=%1 total=%2.")
        .arg(initial.size()).arg(catalog.totalAssetCount()));
  int initial_photo_count = 0;
  for (const auto& value : initial)
    if (value.toMap().value(QStringLiteral("mediaType")).toString() == QStringLiteral("photo")) ++initial_photo_count;
  if (initial_photo_count != expected_photo_count)
    return invalid(QStringLiteral("Full-catalog photo count mismatch: visible=%1 expected=%2.")
        .arg(initial_photo_count).arg(expected_photo_count));
  catalog.requestAllAssetRecipes();
  if (!wait_for_condition([&] { return all_recipes_ready() || recipe_load_error_count() > 0; }, timeout_ms,
                          "full-catalog-search-recipes"))
    return invalid(QStringLiteral("Timed out hydrating every catalog recipe for current-preview provenance."));
  if (recipe_load_error_count() > 0)
    return invalid(QStringLiteral("Could not hydrate every catalog recipe; error_count=%1.").arg(recipe_load_error_count()));

  const auto initial_ids = [&] {
    QSet<QString> ids;
    for (const auto& value : initial) ids.insert(value.toMap().value(QStringLiteral("assetId")).toString());
    return ids;
  }();
  if (initial_ids.size() != initial.size() || initial_ids.contains(QString{}))
    return invalid(QStringLiteral("Full-catalog snapshot contains empty or duplicate asset IDs."));

  catalog.setVisibleAssetRange(0, static_cast<int>(initial.size()) - 1);
  if (!wait_for_condition([&] {
        return !catalog.loading() && all_recipes_ready() && recipe_load_error_count() == 0 &&
            catalog.activeThumbnailWorkerCount() == 0 && catalog.pendingThumbnailWorkerCount() == 0;
      }, timeout_ms, "full-catalog-search-previews"))
    return invalid(QStringLiteral("Timed out rendering current catalog previews for the full search scope."));

  const auto current = catalog.similaritySearchAssets();
  QSet<QString> current_ids;
  QJsonArray assets;
  int photo_count = 0;
  int current_preview_photo_count = 0;
  static const QSet<QString> expected_keys{
      QStringLiteral("assetId"), QStringLiteral("filePath"), QStringLiteral("thumbnailSource"),
      QStringLiteral("mediaType"), QStringLiteral("searchPreviewCurrent")};
  for (const auto& value : current) {
    const auto asset = value.toMap();
    QSet<QString> keys;
    for (auto it = asset.cbegin(); it != asset.cend(); ++it) keys.insert(it.key());
    const auto id = asset.value(QStringLiteral("assetId")).toString();
    const auto path = asset.value(QStringLiteral("filePath")).toString();
    const auto preview = asset.value(QStringLiteral("thumbnailSource")).toString();
    const auto media_type = asset.value(QStringLiteral("mediaType")).toString();
    const auto is_current = asset.value(QStringLiteral("searchPreviewCurrent")).toBool();
    if (keys != expected_keys || id.isEmpty() || current_ids.contains(id))
      return invalid(QStringLiteral("Catalog exposed an invalid compact similarity-search asset."));
    current_ids.insert(id);
    if (media_type == QStringLiteral("photo")) {
      ++photo_count;
      if (is_current) {
        ++current_preview_photo_count;
        const QUrl preview_url(preview);
        const auto local_preview = preview_url.isLocalFile() ? preview_url.toLocalFile()
            : preview_url.scheme().isEmpty() ? preview : QString{};
        const QFileInfo preview_info(local_preview);
        if (local_preview.isEmpty() || !preview_info.isAbsolute() || !preview_info.isFile() ||
            !preview_info.isReadable())
          return invalid(QStringLiteral("Catalog marked a search preview current but its local file is unavailable."));
      }
    }
    assets.append(QJsonObject{
        {QStringLiteral("assetId"), id},
        {QStringLiteral("filePath"), path},
        {QStringLiteral("thumbnailSource"), preview},
        {QStringLiteral("mediaType"), media_type},
        {QStringLiteral("searchPreviewCurrent"), is_current},
    });
  }
  if (current.size() != initial.size() || current_ids != initial_ids ||
      current.size() != catalog.totalAssetCount() || photo_count != expected_photo_count)
    return invalid(QStringLiteral("Catalog scope changed while current previews were settling."));

  const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("assets"), assets}})
      .toJson(QJsonDocument::Compact);
  if (bytes.isEmpty() || bytes.size() > 16 * 1024 * 1024)
    return invalid(QStringLiteral("Full-catalog compact search snapshot exceeds 16 MiB."));
  QSaveFile file(destination);
  if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
      file.write(bytes) != bytes.size() || !file.commit())
    return invalid(QStringLiteral("Could not write the private compact search snapshot."));

  const auto skipped_current_previews = photo_count - current_preview_photo_count;
  const auto receipt = QJsonDocument(QJsonObject{
      {QStringLiteral("snapshotSha256"), QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
      {QStringLiteral("snapshotAssetCount"), current.size()},
      {QStringLiteral("photoCount"), photo_count},
      {QStringLiteral("currentPreviewPhotoCount"), current_preview_photo_count},
      {QStringLiteral("photosWithoutCurrentPreview"), skipped_current_previews},
      {QStringLiteral("allRecipesReady"), true},
      {QStringLiteral("thumbnailWorkersDrained"), true},
      {QStringLiteral("imagePathsPrintedInReceipt"), false},
  }).toJson(QJsonDocument::Compact);
  std::cout << receipt.constData() << '\n';
  return EXIT_SUCCESS;
}

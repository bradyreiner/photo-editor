namespace {
QString saved_board_preview_path(const QVariantMap& document) {
  const auto key = document.value("documentId").toString() + QLatin1Char('|') + document.value("updatedAt").toString();
  const auto hash = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
  return QDir(carousel_documents_root()).filePath(QStringLiteral("gallery-previews/") + hash + QStringLiteral(".png"));
}

void cache_saved_board_summaries(const QVariantList& documents) {
  static QMutex mutex;
  QMutexLocker lock(&mutex);
  QFile file(carousel_document_index_path());
  QJsonArray entries;
  if (file.open(QIODevice::ReadOnly)) entries = QJsonDocument::fromJson(file.readAll()).object().value("documents").toArray();
  const auto before = entries;
  for (const auto& value : documents) {
    auto entry = QJsonObject::fromVariantMap(value.toMap());
    entry.remove(QStringLiteral("previewSource"));
    const auto id = entry.value("documentId").toString();
    if (id.isEmpty()) continue;
    bool found = false;
    for (int index = 0; index < entries.size(); ++index) if (entries.at(index).toObject().value("documentId").toString() == id) {
      const auto created = entries.at(index).toObject().value("createdAt").toString();
      if (!created.isEmpty()) entry.insert("createdAt", created);
      if (saved_document_timestamp(entry) >= saved_document_timestamp(entries.at(index).toObject())) entries[index] = entry;
      found = true; break;
    }
    if (!found) entries.append(entry);
  }
  if (entries != before) static_cast<void>(save_legacy_document_index(entries));
}

QVariantMap collect_saved_board_summaries() {
  QMap<QString, QJsonObject> entries;
  for (const auto& value : load_legacy_document_index()) {
    const auto entry = value.toObject();
    if (!entry.value("documentId").toString().isEmpty()) entries.insert(entry.value("documentId").toString(), entry);
  }
  QString output, error;
  QVariantList catalog_entries;
  const bool fetched = AppBootstrap::runCatalogHelper(QStringLiteral("catalog_carousel_documents"),
      {QStringLiteral("list-summaries")}, &output, &error, 5000);
  const auto parsed = QJsonDocument::fromJson(output.toUtf8());
  if (fetched && parsed.isArray()) for (const auto& value : parsed.array()) {
    const auto stored = value.toObject();
    const auto document = stored.value("document_json").toObject();
    const auto id = stored.value("document_id").toString();
    if (id.isEmpty()) continue;
    auto entry = legacy_document_entry(id, stored.value("name").toString(), stored.value("document_format").toString(),
        stored.value("slide_count").toInt(), stored.value("created_at").toString(),
        document.value("documentSavedAtUtc").toString().isEmpty() ? stored.value("updated_at").toString() : document.value("documentSavedAtUtc").toString());
    entry.insert("projectName", document.value("projectName").toString());
    entry.insert("documentArchived", document.value("documentArchived").toBool());
    catalog_entries.append(entry.toVariantMap());
    if (!entries.contains(id) || saved_document_timestamp(entry) >= saved_document_timestamp(entries.value(id))) entries[id] = entry;
  }
  cache_saved_board_summaries(catalog_entries);
  QVariantList documents;
  for (const auto& entry : entries) {
    if (entry.value("documentArchived").toBool()) continue;
    auto document = entry.toVariantMap();
    const auto preview = saved_board_preview_path(document);
    document.insert("previewSource", QFileInfo::exists(preview) ? QUrl::fromLocalFile(preview).toString() : QString{});
    documents.append(document);
  }
  std::sort(documents.begin(), documents.end(), [](const auto& lhs, const auto& rhs) {
    const auto left = lhs.toMap(), right = rhs.toMap();
    const auto a = saved_document_timestamp(QJsonObject::fromVariantMap(left)), b = saved_document_timestamp(QJsonObject::fromVariantMap(right));
    return a == b ? left.value("documentId").toString() < right.value("documentId").toString() : a > b;
  });
  return {{"documents", documents}, {"error", fetched && parsed.isArray() ? QString{} :
      QStringLiteral("Could not refresh the catalog. Showing locally saved boards. Try Refresh.")}};
}
}  // namespace

QVariantList CarouselDocumentController::recentDocuments() const {
  return collect_saved_board_summaries().value("documents").toList();
}

void CarouselDocumentController::refreshRecentBoardDocuments(bool force) {
  if (recent_board_documents_loading_) {
    if (force) recent_board_documents_refresh_pending_ = true;
    return;
  }
  const auto now = QDateTime::currentMSecsSinceEpoch();
  if (!force && recent_board_documents_loaded_at_ > 0 && now - recent_board_documents_loaded_at_ < 30000) return;
  recent_board_documents_loading_ = true;
  recent_board_documents_error_.clear();
  emit recentBoardDocumentsChanged();
  const auto revision = recent_board_documents_revision_;
  auto* watcher = new QFutureWatcher<QVariantMap>(this);
  recent_board_documents_watcher_ = watcher;
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, revision]() {
    const auto result = watcher->result();
    watcher->deleteLater(); recent_board_documents_watcher_ = nullptr;
    recent_board_documents_loading_ = false;
    recent_board_documents_ = result.value("documents").toList();
    recent_board_documents_error_ = result.value("error").toString();
    recent_board_documents_loaded_at_ = QDateTime::currentMSecsSinceEpoch();
    emit recentBoardDocumentsChanged();
    const auto again = recent_board_documents_refresh_pending_ || revision != recent_board_documents_revision_;
    recent_board_documents_refresh_pending_ = false;
    if (again) refreshRecentBoardDocuments(true);
  });
  watcher->setFuture(QtConcurrent::run([] { return collect_saved_board_summaries(); }));
}

void CarouselDocumentController::requestBoardThumbnail(const QString& document_id) {
  QVariantMap document;
  for (const auto& value : recent_board_documents_) if (value.toMap().value("documentId") == document_id) { document = value.toMap(); break; }
  if (document.isEmpty()) return;
  const auto path = saved_board_preview_path(document);
  if (QFileInfo::exists(path) || pending_board_thumbnails_.contains(path)) return;
  pending_board_thumbnails_.insert(path);
  auto* watcher = new QFutureWatcher<bool>(this);
  connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, path, document_id]() {
    pending_board_thumbnails_.remove(path);
    const auto saved = watcher->result(); watcher->deleteLater();
    if (!saved) return;
    for (auto& value : recent_board_documents_) {
      auto current = value.toMap();
      if (current.value("documentId") == document_id && saved_board_preview_path(current) == path) {
        current.insert("previewSource", QUrl::fromLocalFile(path).toString()); value = current;
        emit recentBoardDocumentsChanged(); break;
      }
    }
  });
  watcher->setFuture(QtConcurrent::run(&previewThreadPool(), [document_id, path]() {
    QString output;
    QJsonObject stored;
    if (AppBootstrap::runCatalogHelper(QStringLiteral("catalog_carousel_documents"),
            {QStringLiteral("get"), document_id}, &output, nullptr, 5000))
      stored = QJsonDocument::fromJson(output.toUtf8()).object();
    stored = preferred_saved_document(stored, load_legacy_document_payload(document_id));
    auto object = stored.value(QStringLiteral("document_json"));
    if (!object.isObject()) object = stored.value(QStringLiteral("document"));
    if (!object.isObject()) return false;
    ExportSnapshot empty{};
    CarouselDocumentController renderer(empty);
    if (!renderer.loadDocumentObject(object.toObject()) || renderer.slideCount() == 0) return false;
    const auto size = renderer.exportPixelSize().scaled(QSize(512, 512), Qt::KeepAspectRatio);
    const auto image = renderer.renderSlideImage(0, size, false);
    if (image.isNull()) return false;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && image.save(&file, "PNG") && file.commit();
  }));
}

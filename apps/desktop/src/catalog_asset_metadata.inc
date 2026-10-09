// Included in catalog_asset_model.cpp so every existing catalog fixture uses
// the same persistence and recovery implementation as the application.
namespace {
bool valid_metadata_fields(const QVariantMap& fields) {
  const auto json = QJsonObject::fromVariantMap(fields);
  const QStringList allowed{"title","caption","creator","copyright","location","captureTime"};
  for (auto it = json.begin(); it != json.end(); ++it) {
    if (it.key() == QStringLiteral("keywords")) {
      if (!it.value().isArray() || it.value().toArray().size() > 256) return false;
      for (const auto& word : it.value().toArray()) if (!word.isString() || word.toString().toUtf8().size() > 512) return false;
    } else {
      if (!allowed.contains(it.key()) || !it.value().isString() || it.value().toString().toUtf8().size() > 16384) return false;
      if (it.key() == QStringLiteral("captureTime") && !it.value().toString().isEmpty() &&
          !QDateTime::fromString(it.value().toString(), Qt::ISODate).isValid()) return false;
    }
  }
  return true;
}
}
QString CatalogAssetModel::metadataAssetId(const CatalogAssetItem& item) const {
  return item.virtual_copy && !item.source_asset_id.isEmpty() ? item.source_asset_id : item.asset_id;
}
QString CatalogAssetModel::originalPreviewSource(const QString& file_path) const {
  if (file_path.trimmed().isEmpty()) return {};
  const auto path = QFileInfo(file_path).absoluteFilePath();
  return QStringLiteral("image://library-original/source?path=") + QString::fromLatin1(QUrl::toPercentEncoding(path)) +
      QStringLiteral("&revision=") + QString::fromLatin1(QUrl::toPercentEncoding(linked_image_file_revision(path)));
}
QVariantMap CatalogAssetModel::selectedMetadata() const {
  const auto* item = selectedItem();
  return item ? asset_metadata_.value(metadataAssetId(*item)) : QVariantMap{};
}
void CatalogAssetModel::setSortOrder(const QString& order) {
  const QStringList valid{"imported-desc", "capture-asc", "capture-desc", "filename-asc", "rating-desc"};
  if (!valid.contains(order) || sort_order_ == order) return;
  sort_order_ = order;
  applyFilters(selectedAssetId());
  emit filtersChanged();
}
bool CatalogAssetModel::saveMetadataOutbox(const QHash<QString, QVariantMap>& records) {
  if (!metadata_outbox_readable_) return false;
  QJsonObject object;
  for (auto it = records.cbegin(); it != records.cend(); ++it) object.insert(it.key(), QJsonObject::fromVariantMap(it.value()));
  const auto bytes = QJsonDocument(QJsonObject{{"version",1},{"records",object}}).toJson(QJsonDocument::Compact);
  if (bytes.size() > 16 * 1024 * 1024) {
    metadata_status_ = QStringLiteral("Too many pending metadata changes. Sync the catalog before applying another batch."); emit metadataStateChanged(); return false;
  }
  QDir().mkpath(AppBootstrap::appDataLocation());
  QSaveFile file(QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("metadata-outbox.json")));
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
    metadata_status_ = QStringLiteral("Metadata could not be preserved locally. Changes were not acknowledged.");
    emit metadataStateChanged(); return false;
  }
  return true;
}
void CatalogAssetModel::initializeMetadataPersistence() {
  QDir().mkpath(AppBootstrap::appDataLocation());
  metadata_outbox_lock_ = std::make_unique<QLockFile>(QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("metadata-outbox.lock")));
  metadata_outbox_lock_->setStaleLockTime(0);
  if (!metadata_outbox_lock_->tryLock(0)) {
    metadata_outbox_readable_ = false;
    metadata_status_ = QStringLiteral("Metadata changes are active in another window. Close that window and reopen this library to edit metadata here.");
    return;
  }
  metadata_process_.setProcessChannelMode(QProcess::SeparateChannels);
  metadata_timeout_.setSingleShot(true);
  connect(&metadata_timeout_, &QTimer::timeout, this, [this] { metadata_process_.kill(); });
  connect(&metadata_process_, &QProcess::finished, this, &CatalogAssetModel::finishMetadataPersistence);
  connect(&metadata_process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) finishMetadataPersistence(-1, QProcess::CrashExit);
  });
  QFile file(QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("metadata-outbox.json")));
  if (file.exists()) {
    QJsonParseError error{};
    const auto doc = file.open(QIODevice::ReadOnly) && file.size() <= 16 * 1024 * 1024 ? QJsonDocument::fromJson(file.readAll(), &error) : QJsonDocument{};
    metadata_outbox_readable_ = doc.isObject() && error.error == QJsonParseError::NoError && doc.object().value("version") == QJsonValue(1) && doc.object().value("records").isObject();
    const auto records = doc.object().value("records").toObject();
    for (auto it = records.begin(); it != records.end(); ++it) {
      if (it.key().isEmpty() || it.key() != it.key().trimmed() || !it.value().isObject() || !valid_metadata_fields(it.value().toObject().toVariantMap())) metadata_outbox_readable_ = false;
      pending_metadata_.insert(it.key(), it.value().toObject().toVariantMap());
    }
    if (!metadata_outbox_readable_) {
      pending_metadata_.clear();
      metadata_status_ = QStringLiteral("Metadata recovery could not be read. The original file has been preserved.");
    } else {
      asset_metadata_ = pending_metadata_;
      if (!pending_metadata_.isEmpty()) metadata_status_ = QStringLiteral("Recovering pending metadata.");
      QTimer::singleShot(0, this, &CatalogAssetModel::startMetadataPersistence);
    }
  }
  connect(&metadata_file_watcher_, &QFutureWatcher<QVariantMap>::finished, this, [this] {
    metadata_file_busy_ = false;
    const auto result = metadata_file_watcher_.result();
    if (result.contains(QStringLiteral("records"))) {
      auto next = pending_metadata_;
      const auto records = result.value(QStringLiteral("records")).toMap();
      QHash<QString, QVariantMap> merged;
      for (auto it = records.cbegin(); it != records.cend(); ++it) {
        auto value = asset_metadata_.value(it.key());
        const auto fields = it.value().toMap();
        for (auto field = fields.cbegin(); field != fields.cend(); ++field) value.insert(field.key(), field.value());
        merged.insert(it.key(), value); next.insert(it.key(), value);
      }
      if (!records.isEmpty() && !saveMetadataOutbox(next)) { emit metadataStateChanged(); return; }
      if (!records.isEmpty()) {
        pending_metadata_ = next;
        for (auto it = records.cbegin(); it != records.cend(); ++it) {
          deferred_metadata_ids_.remove(it.key());
          asset_metadata_.insert(it.key(), merged.value(it.key()));
          if (snapshot_fetch_active_ || snapshot_parse_active_) changed_metadata_during_snapshot_.insert(it.key());
        }
        metadata_persistence_blocked_ = false;
        rebuildBurstMetadata(); rebuildIntelligenceMetadata();
        applyFilters(selectedAssetId()); emit selectedAssetChanged();
        startMetadataPersistence();
      }
    }
    metadata_status_ = result.value(QStringLiteral("status")).toString();
    emit metadataStateChanged();
  });
}
bool CatalogAssetModel::saveMetadataToSelected(const QVariantMap& patch) {
  if (!metadata_outbox_readable_ || patch.isEmpty() || metadataFileBusy()) return false;
  const QStringList allowed{"keywords","title","caption","creator","copyright","location","captureTime"};
  QVariantMap normalized;
  for (auto it = patch.cbegin(); it != patch.cend(); ++it) {
    if (!allowed.contains(it.key())) return false;
    if (it.key() == QStringLiteral("keywords")) {
      if (it.value().toStringList().size() > 256) return false;
      QStringList words;
      for (const auto& word : it.value().toStringList()) {
        const auto value = word.trimmed();
        if (value.toUtf8().size() > 512) return false;
        if (!value.isEmpty() && !words.contains(value, Qt::CaseInsensitive)) words.append(value);
      }
      if (words.size() > 256) return false;
      normalized.insert(it.key(), words);
    } else {
      const auto value = it.value().toString().trimmed();
      if (value.toUtf8().size() > 16384) return false;
      if (it.key() == QStringLiteral("captureTime") && !value.isEmpty() && !QDateTime::fromString(value, Qt::ISODate).isValid()) {
        metadata_status_ = QStringLiteral("Capture time must use ISO format, for example 2026-10-07T15:30:00-06:00."); emit metadataStateChanged(); return false;
      }
      normalized.insert(it.key(), value);
    }
  }
  auto ids = selectedAssetIds();
  if (ids.isEmpty() && !selectedAssetId().isEmpty()) ids.append(selectedAssetId());
  if (ids.isEmpty()) return false;
  auto next = pending_metadata_;
  QHash<QString, QVariantMap> changed;
  for (const auto& id : ids) {
    const auto index = all_asset_index_by_id_.value(id, -1);
    if (index < 0 || all_assets_[index].media_type != QStringLiteral("photo")) continue;
    const auto source = metadataAssetId(all_assets_[index]);
    auto record = asset_metadata_.value(source);
    for (auto it = normalized.cbegin(); it != normalized.cend(); ++it) record.insert(it.key(), it.value());
    next.insert(source, record); changed.insert(source, record);
  }
  if (changed.isEmpty() || !saveMetadataOutbox(next)) return false;
  pending_metadata_ = next;
  for (auto it = changed.cbegin(); it != changed.cend(); ++it) {
    deferred_metadata_ids_.remove(it.key());
    asset_metadata_.insert(it.key(), it.value());
    if (snapshot_fetch_active_ || snapshot_parse_active_) changed_metadata_during_snapshot_.insert(it.key());
  }
  metadata_persistence_blocked_ = false;
  metadata_status_ = QStringLiteral("Saved locally; syncing %1 metadata records.").arg(pending_metadata_.size());
  rebuildBurstMetadata(); rebuildIntelligenceMetadata();
  applyFilters(selectedAssetId()); emit selectedAssetChanged(); emit metadataStateChanged();
  startMetadataPersistence(); return true;
}
void CatalogAssetModel::retryMetadataPersistence() {
  deferred_metadata_ids_.clear();
  metadata_persistence_blocked_ = false;
  startMetadataPersistence();
}
void CatalogAssetModel::startMetadataPersistence() {
  if (!active_metadata_.isEmpty() || pending_metadata_.isEmpty() || metadata_persistence_blocked_ || !metadata_outbox_readable_) return;
  QJsonArray records;
  auto ids = pending_metadata_.keys(); ids.sort();
  for (const auto& id : ids) {
    if (deferred_metadata_ids_.contains(id)) continue;
    if (records.size() >= 256) break;
    active_metadata_.insert(id, pending_metadata_.value(id));
    records.append(QJsonObject{{"asset_id",id},{"metadata",QJsonObject::fromVariantMap(pending_metadata_.value(id))}});
  }
  if (records.isEmpty()) return;
  metadata_request_ = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/looksy-metadata-XXXXXX.json"));
  const auto bytes = QJsonDocument(QJsonObject{{"records",records}}).toJson(QJsonDocument::Compact);
  QProcessEnvironment environment;
  QString error;
  if (!metadata_request_->open() || metadata_request_->write(bytes) != bytes.size() || !metadata_request_->flush() || !AppBootstrap::prepareProcessEnvironment(&environment, &error)) {
    finishMetadataPersistence(-1, QProcess::CrashExit); return;
  }
  metadata_request_->close();
  metadata_process_.setProcessEnvironment(environment);
  metadata_process_.setWorkingDirectory(QDir::currentPath());
  metadata_timeout_.start(30000);
  metadata_process_.start(stateProgram(), stateArguments({QStringLiteral("set-asset-metadata"), metadata_request_->fileName()}));
}
void CatalogAssetModel::finishMetadataPersistence(int exit_code, QProcess::ExitStatus exit_status) {
  if (active_metadata_.isEmpty()) return;
  metadata_timeout_.stop();
  QJsonParseError error{};
  const auto receipt = QJsonDocument::fromJson(metadata_process_.readAllStandardOutput(), &error).object();
  bool valid = exit_code == 0 && exit_status == QProcess::NormalExit && error.error == QJsonParseError::NoError && receipt.value("applied").isArray() && receipt.value("missing").isArray();
  QSet<QString> seen, applied;
  for (const auto& key : {QStringLiteral("applied"),QStringLiteral("missing")}) {
    for (const auto& value : receipt.value(key).toArray()) {
      const auto id = value.toString();
      if (!active_metadata_.contains(id) || seen.contains(id)) valid = false;
      seen.insert(id); if (key == QStringLiteral("applied")) applied.insert(id);
    }
  }
  valid = valid && seen.size() == active_metadata_.size();
  auto remaining = pending_metadata_;
  if (valid) {
    for (const auto& value : receipt.value("missing").toArray()) deferred_metadata_ids_.insert(value.toString());
    for (const auto& id : applied) {
      if (remaining.value(id) == active_metadata_.value(id)) remaining.remove(id);
      if (snapshot_fetch_active_ || snapshot_parse_active_) changed_metadata_during_snapshot_.insert(id);
    }
  }
  const bool saved = saveMetadataOutbox(remaining);
  if (saved) pending_metadata_ = remaining;
  metadata_persistence_blocked_ = !valid || !saved;
  active_metadata_.clear(); metadata_request_.reset();
  if (saved) metadata_status_ = metadata_persistence_blocked_ || !deferred_metadata_ids_.isEmpty() ? QStringLiteral("Metadata is retained locally. Retry to sync the catalog.") : pending_metadata_.isEmpty() ? QStringLiteral("Metadata saved to catalog.") : QStringLiteral("Syncing metadata.");
  emit metadataStateChanged();
  if (!metadata_persistence_blocked_) QTimer::singleShot(0, this, &CatalogAssetModel::startMetadataPersistence);
}
bool CatalogAssetModel::readSelectedMetadataSidecars() {
  if (metadataFileBusy() || !metadata_outbox_readable_) return false;
  QVariantMap targets;
  for (const auto& id : selectedAssetIds()) {
    const auto index = all_asset_index_by_id_.value(id,-1);
    if (index >= 0 && all_assets_[index].media_type == QStringLiteral("photo")) targets.insert(metadataAssetId(all_assets_[index]), all_assets_[index].file_path);
  }
  if (targets.isEmpty()) return false;
  metadata_file_busy_ = true;
  metadata_status_ = QStringLiteral("Reading metadata sidecars."); emit metadataStateChanged();
  metadata_file_watcher_.setFuture(QtConcurrent::run([targets] {
    QVariantMap records; int failed = 0;
    for (auto it = targets.cbegin(); it != targets.cend(); ++it) {
      QString error;
      const auto fields = read_metadata_sidecar(metadata_sidecar_path(it.value().toString()), &error);
      if (!error.isEmpty() || !valid_metadata_fields(fields)) { ++failed; continue; }
      records.insert(it.key(),fields);
    }
    return QVariantMap{{"records",records},{"status",QStringLiteral("Read %1 sidecars; %2 unreadable or missing. Imported fields are saved locally for catalog sync.").arg(records.size()).arg(failed)}};
  }));
  return true;
}
bool CatalogAssetModel::writeSelectedMetadataSidecars() {
  if (metadataFileBusy() || !metadata_outbox_readable_) return false;
  QVariantMap targets;
  QHash<QString, QString> sidecar_paths;
  for (const auto& id : selectedAssetIds()) {
    const auto index = all_asset_index_by_id_.value(id,-1);
    if (index < 0 || all_assets_[index].media_type != QStringLiteral("photo")) continue;
    const auto& item = all_assets_[index];
    const auto path = metadata_sidecar_path(item.file_path);
    const auto fields = asset_metadata_.value(metadataAssetId(item));
    const QFileInfo sidecar(path);
    const auto resolved = sidecar.canonicalFilePath();
    const auto key = (resolved.isEmpty() ? QDir(sidecar.absolutePath()).canonicalPath() + QStringLiteral("/") + sidecar.fileName() : resolved).toCaseFolded();
    const auto existing_path = sidecar_paths.value(key, path);
    if (targets.contains(existing_path) && targets.value(existing_path).toMap() != fields) {
      metadata_status_ = QStringLiteral("Selected RAW/JPEG files share a sidecar and have different metadata. Select one file or align their metadata first."); emit metadataStateChanged(); return false;
    }
    sidecar_paths.insert(key, existing_path);
    targets.insert(existing_path, fields);
  }
  if (targets.isEmpty()) return false;
  metadata_file_busy_ = true;
  metadata_status_ = QStringLiteral("Writing XMP sidecars."); emit metadataStateChanged();
  metadata_file_watcher_.setFuture(QtConcurrent::run([targets] {
    int saved = 0; QStringList errors;
    for (auto it = targets.cbegin(); it != targets.cend(); ++it) {
      QString error;
      if (write_metadata_sidecar(it.key(),it.value().toMap(),&error)) ++saved;
      else errors.append(error);
    }
    return QVariantMap{{"status",QStringLiteral("Wrote %1 XMP sidecars; %2 failed.%3").arg(saved).arg(errors.size()).arg(errors.isEmpty() ? QString() : QStringLiteral(" ") + errors.first())}};
  }));
  return true;
}

int CatalogAssetModel::confirmedDuplicateGroupCount() const {
  QSet<QString> groups;
  for (const auto& value : confirmed_duplicate_groups_) groups.insert(value);
  return static_cast<int>(groups.size());
}
void CatalogAssetModel::initializeDuplicateAnalysis() {
  connect(&duplicate_analysis_watcher_, &QFutureWatcher<CatalogDuplicateAnalysisResult>::finished, this, [this] {
    duplicate_analysis_busy_ = false;
    const auto result = duplicate_analysis_watcher_.result();
    if (!result.cancelled) {
      for (auto it = duplicate_analysis_sources_.cbegin(); it != duplicate_analysis_sources_.cend(); ++it) confirmed_duplicate_groups_.remove(it.key());
      for (const auto& group : result.content_groups) {
        QStringList current;
        for (const auto& id : group.asset_ids) {
          const auto index = all_asset_index_by_id_.value(id,-1);
          if (index >= 0 && all_assets_[index].file_path == duplicate_analysis_sources_.value(id)) current.append(id);
        }
        if (current.size() > 1) for (const auto& id : current) confirmed_duplicate_groups_.insert(id,group.sha256);
      }
      rebuildIntelligenceMetadata();
      applyFilters(selectedAssetId());
      duplicate_analysis_status_ = QStringLiteral("Found %1 groups of byte-identical originals. %2 files skipped; no files changed.").arg(confirmedDuplicateGroupCount()).arg(result.skippedFileCount());
    } else duplicate_analysis_status_ = QStringLiteral("Duplicate analysis canceled; no files changed.");
    duplicate_analysis_sources_.clear();
    emit duplicateAnalysisChanged();
  });
}
bool CatalogAssetModel::analyzeDuplicates() {
  if (duplicateAnalysisBusy()) return false;
  QVector<CatalogDuplicateCandidate> candidates;
  duplicate_analysis_sources_.clear();
  for (const auto& item : assets_) {
    if (item.media_type != QStringLiteral("photo") || item.virtual_copy || item.file_path.isEmpty()) continue;
    candidates.push_back({item.asset_id,item.file_path,-1,-1});
    duplicate_analysis_sources_.insert(item.asset_id,item.file_path);
  }
  duplicate_analysis_cancellation_ = std::make_shared<std::atomic_bool>(false);
  duplicate_analysis_busy_ = true;
  duplicate_analysis_status_ = QStringLiteral("Checking visible originals by size and SHA-256. You can keep working.");
  duplicate_analysis_watcher_.setFuture(QtConcurrent::run([candidates,token=duplicate_analysis_cancellation_] {
    return analyze_catalog_duplicates(candidates,token);
  }));
  emit duplicateAnalysisChanged(); return true;
}
void CatalogAssetModel::cancelDuplicateAnalysis() {
  if (duplicate_analysis_cancellation_) duplicate_analysis_cancellation_->store(true, std::memory_order_relaxed);
}
void CatalogAssetModel::applyConfirmedDuplicateGroups() {
  QHash<QString, QVector<int>> groups;
  for (int index = 0; index < static_cast<int>(all_assets_.size()); ++index) {
    const auto group = confirmed_duplicate_groups_.value(all_assets_[index].asset_id);
    if (!group.isEmpty()) groups[group].append(index);
  }
  for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
    for (int position = 0; position < static_cast<int>(it.value().size()); ++position) {
      auto& item = all_assets_[it.value()[position]];
      item.duplicate_group_key = QStringLiteral("sha256:") + it.key();
      item.duplicate_group_size = static_cast<int>(it.value().size());
      item.duplicate_group_position = position + 1;
      item.best_shot_suggested = false;
    }
  }
}

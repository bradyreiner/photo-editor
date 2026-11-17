// The catalog owns admission, review state and the single batch-history entry.
// Workers own only immutable snapshots, cancellation and temporary preview files.
bool CatalogAssetModel::prepareShootMatch(const QString& reference_id, const QStringList& target_ids,
                                         const QVariantMap& requested_options) {
  const auto access = customer_edit_access_.enter();
  if (!access || shootMatchBusy() || shoot_match_state_.value("state").toString() == QStringLiteral("applying")) return false;
  const auto rejected = [&](const QString& message) {
    shoot_match_state_ = {{"state", "error"}, {"error", message}};
    emit shootMatchChanged(); return false;
  };
  QVariantMap options{{"lighting", requested_options.value("lighting", true)},
      {"look", requested_options.value("look", true)},
      {"lightingAmount", requested_options.value("lightingAmount", 1.0)},
      {"lookAmount", requested_options.value("lookAmount", .75)}};
  const double lighting_amount = options.value("lightingAmount").toDouble();
  const double look_amount = options.value("lookAmount").toDouble();
  if (!std::isfinite(lighting_amount) || !std::isfinite(look_amount) || lighting_amount < 0 || lighting_amount > 1 ||
      look_amount < 0 || look_amount > 1 ||
      (!(options.value("lighting").toBool() && lighting_amount > 0) && !(options.value("look").toBool() && look_amount > 0)))
    return rejected(QStringLiteral("Enable Lighting or Look and choose an amount between 0 and 100%."));
  options.insert("lighting", options.value("lighting").toBool() && lighting_amount > 0);
  options.insert("look", options.value("look").toBool() && look_amount > 0);
  QStringList ids;
  for (const auto& id : target_ids) if (id != reference_id && !ids.contains(id)) ids.append(id);
  if (reference_id.isEmpty() || ids.isEmpty() || ids.size() > shoot_match_maximum_targets)
    return rejected(QStringLiteral("Choose a reference and between 1 and %1 target photos.").arg(shoot_match_maximum_targets));
  auto all_ids = ids; all_ids.prepend(reference_id);
  if (!ensureAssetRecipesLoaded(all_ids, QStringLiteral("preparing reference matching")))
    return rejected(QStringLiteral("Loading saved edits. Try Match previews again when the photos are ready."));
  QVariantList snapshots;
  qint64 recipe_bytes = 0;
  for (const auto& id : all_ids) {
    auto asset = assetById(id);
    if (asset.value("mediaType").toString() != QStringLiteral("photo") || transient_adjustment_asset_ids_.contains(id))
      return rejected(QStringLiteral("Every selected item must be a photo with its current edits saved."));
    const auto path = asset.value("filePath").toString();
    const auto stamp = shoot_match_source_stamp(path);
    if (!stamp.value("exists").toBool()) return rejected(QStringLiteral("A selected photo is offline or unreadable. Relink it before matching."));
    recipe_bytes += asset.value("adjustmentPatchJson").toString().size() * qint64(sizeof(QChar));
    if (recipe_bytes > 128LL * 1024 * 1024)
      return rejected(QStringLiteral("These photos contain large layered edits. Match a smaller group."));
    asset.insert("sourceStamp", stamp);
    snapshots.append(asset);
  }
  if (!QDir().mkpath(AppBootstrap::cacheLocation())) return rejected(QStringLiteral("The preview cache is unavailable."));
  auto directory = std::make_shared<QTemporaryDir>(QDir(AppBootstrap::cacheLocation()).filePath(QStringLiteral("shoot-match-XXXXXX")));
  if (!directory->isValid()) return rejected(QStringLiteral("The preview directory could not be created."));
  shoot_match_plan_.clear();
  shoot_match_preview_directory_ = directory;
  auto cancellation = std::make_shared<std::atomic_bool>(false);
  shoot_match_cancellation_ = cancellation;
  shoot_match_state_ = {{"state", "running"}, {"targetCount", ids.size()}, {"referenceId", reference_id}, {"options", options}};
  emit shootMatchChanged();
  auto* watcher = new QFutureWatcher<QVariantMap>(this);
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, snapshots, directory, cancellation] {
    const auto result = watcher->result(); watcher->deleteLater();
    shoot_match_cancellation_.reset();
    if (cancellation->load(std::memory_order_relaxed)) {
      shoot_match_state_ = {{"state", "cancelled"}};
      shoot_match_plan_.clear();
    } else if (!result.value("error").toString().isEmpty()) {
      shoot_match_state_ = {{"state", "error"}, {"error", result.value("error")}};
    } else {
      shoot_match_plan_ = result;
      shoot_match_plan_.insert("snapshots", snapshots);
      auto visible = result;
      auto rows = visible.value("results").toList();
      for (auto& value : rows) { auto row = value.toMap(); row.remove("afterPatchJson"); value = row; }
      visible.insert("results", rows); visible.insert("state", "review");
      shoot_match_state_ = visible;
    }
    emit shootMatchChanged();
  });
  static QThreadPool pool;
  static const bool configured = [] { pool.setMaxThreadCount(1); return true; }();
  Q_UNUSED(configured);
  watcher->setFuture(QtConcurrent::run(&pool, [snapshots, options, directory, cancellation] {
    try {
      const auto reference = snapshots.first().toMap();
      const auto path = reference.value("filePath").toString();
      EditRecipeState state;
      const auto payload = reference.value("adjustmentPatchJson").toString();
      if (!payload.trimmed().isEmpty()) {
        const auto parsed = parse_adjustment_payload_or_legacy_recipe(payload);
        if (!parsed.ok()) return QVariantMap{{"error", "The reference edits could not be read."}};
        state = *parsed.state;
      }
      const auto linked_revision = linked_photo_layer_sources_revision(state.layers);
      QImage source;
      photo_editor::pipeline::DecodedImageMetadata metadata;
      if (!decode_source_image(path, &source, 160, state, &metadata, {.cancellation_requested = cancellation.get()}))
        return QVariantMap{{"error", "The reference could not be decoded."}};
      const auto rendered = render_shoot_match_image(source, state, metadata, cancellation.get());
      const auto features = shoot_match_features(rendered);
      const auto reference_path = directory->filePath(QStringLiteral("reference.png"));
      if (!features.valid) return QVariantMap{{"error", "The reference preview could not be rendered."}};
      QImage review_source;
      photo_editor::pipeline::DecodedImageMetadata review_metadata;
      if (!decode_source_image(path, &review_source, 640, state, &review_metadata, {.cancellation_requested = cancellation.get()}) ||
          !render_shoot_match_image(review_source, state, review_metadata, cancellation.get()).save(reference_path))
        return QVariantMap{{"error", "The reference comparison could not be saved."}};
      QVariantList results;
      for (qsizetype index = 1; index < snapshots.size(); ++index) {
        if (cancellation->load(std::memory_order_relaxed)) break;
        const auto asset = snapshots[index].toMap();
        auto row = match_shoot_photo(asset, features, options, directory->path(), int(index), cancellation.get());
        if (shoot_match_source_stamp(asset.value("filePath").toString()) != asset.value("sourceStamp").toMap())
          row = {{"assetId", asset.value("assetId")}, {"name", QFileInfo(asset.value("filePath").toString()).fileName()},
                 {"ok", false}, {"error", "The source file changed while matching. Build previews again."}};
        results.append(row);
      }
      if (shoot_match_source_stamp(path) != reference.value("sourceStamp").toMap() ||
          linked_photo_layer_sources_revision(state.layers) != linked_revision)
        return QVariantMap{{"error", "The reference file changed. Build previews again."}};
      return QVariantMap{{"results", results}, {"referenceId", reference.value("assetId")}, {"referenceLinkedRevision", linked_revision},
          {"referenceName", QFileInfo(path).fileName()}, {"referenceSource", QUrl::fromLocalFile(reference_path).toString()}, {"options", options}};
    } catch (const std::exception& error) {
      return QVariantMap{{"error", QStringLiteral("Matching failed: %1").arg(QString::fromUtf8(error.what()))}};
    } catch (...) { return QVariantMap{{"error", "Matching failed. Your saved edits were preserved."}}; }
  }));
  return true;
}

void CatalogAssetModel::cancelShootMatch() {
  if (!shoot_match_cancellation_) return;
  shoot_match_cancellation_->store(true, std::memory_order_relaxed);
  shoot_match_state_.insert("state", "cancelling");
  emit shootMatchChanged();
}

void CatalogAssetModel::clearShootMatch() {
  if (shoot_match_state_.value("state").toString() == QStringLiteral("applying")) return;
  if (shootMatchBusy()) { cancelShootMatch(); return; }
  shoot_match_state_ = {{"state", "idle"}};
  shoot_match_plan_.clear(); shoot_match_preview_directory_.reset();
  emit shootMatchChanged();
}

bool CatalogAssetModel::applyReviewedShootMatch(const QStringList& asset_ids) {
  const auto access = customer_edit_access_.enter();
  if (!access || shootMatchBusy() || shoot_match_state_.value("state").toString() != QStringLiteral("review")) return false;
  const auto rejected = [&](const QString& message) {
    shoot_match_state_.insert("error", message); emit shootMatchChanged(); return false;
  };
  if (asset_ids.isEmpty()) return rejected(QStringLiteral("Select at least one proposed edit to apply."));
  QSet<QString> selected;
  for (const auto& id : asset_ids) {
    if (selected.contains(id)) return rejected(QStringLiteral("Each target can appear only once."));
    selected.insert(id);
  }
  const auto snapshots = shoot_match_plan_.value("snapshots").toList();
  auto ready_ids = asset_ids; ready_ids.append(shoot_match_plan_.value("referenceId").toString());
  if (!ensureAssetRecipesLoaded(ready_ids, QStringLiteral("applying reviewed matches")))
    return rejected(QStringLiteral("Loading current edits. Try Apply again."));
  QHash<QString, QVariantMap> captured;
  for (const auto& value : snapshots) {
    const auto snapshot = value.toMap();
    const auto id = snapshot.value("assetId").toString(); captured.insert(id, snapshot);
    if (!selected.contains(id) && id != shoot_match_plan_.value("referenceId").toString()) continue;
    const auto current = assetById(id);
    if (current.value("mediaType").toString() != QStringLiteral("photo") ||
        current.value("filePath") != snapshot.value("filePath") ||
        current.value("adjustmentPatchJson") != snapshot.value("adjustmentPatchJson") ||
        transient_adjustment_asset_ids_.contains(id) ||
        shoot_match_source_stamp(current.value("filePath").toString()) != snapshot.value("sourceStamp").toMap())
      return rejected(QStringLiteral("A reviewed photo or its source changed. Build previews again before applying."));
  }
  const auto reference_payload = captured.value(shoot_match_plan_.value("referenceId").toString()).value("adjustmentPatchJson").toString();
  const auto reference_decoded = reference_payload.isEmpty() ? AdjustmentPatchDecodeResult{} : parse_adjustment_payload_or_legacy_recipe(reference_payload);
  const auto reference_layers = reference_decoded.ok() ? reference_decoded.state->layers : QVariantList{};
  if (linked_photo_layer_sources_revision(reference_layers) != shoot_match_plan_.value("referenceLinkedRevision").toString())
    return rejected(QStringLiteral("A linked reference source changed. Build previews again."));
  QHash<QString, QString> before, after;
  for (const auto& value : shoot_match_plan_.value("results").toList()) {
    const auto row = value.toMap(); const auto id = row.value("assetId").toString();
    if (!selected.contains(id)) continue;
    if (!row.value("ok").toBool() || !row.value("changed").toBool() || !captured.contains(id))
      return rejected(QStringLiteral("One selected target has no applicable proposal."));
    const auto current_payload = captured.value(id).value("adjustmentPatchJson").toString();
    const auto current_decoded = current_payload.isEmpty() ? AdjustmentPatchDecodeResult{} : parse_adjustment_payload_or_legacy_recipe(current_payload);
    const auto current_layers = current_decoded.ok() ? current_decoded.state->layers : QVariantList{};
    if (linked_photo_layer_sources_revision(current_layers) != row.value("linkedSourcesRevision").toString())
      return rejected(QStringLiteral("A linked layer source changed. Build previews again."));
    const auto decoded = parse_adjustment_patch_json(row.value("afterPatchJson").toString());
    if (!decoded.ok() || decoded.scope != AdjustmentPatchScope::AssetBound)
      return rejected(QStringLiteral("One proposed edit could not be validated."));
    const auto asset_index = all_asset_index_by_id_.value(id, -1);
    if (asset_index < 0 || asset_index >= all_assets_.size()) return rejected(QStringLiteral("A target is no longer in this catalog."));
    before.insert(id, all_assets_[asset_index].adjustment_patch_json);
    after.insert(id, row.value("afterPatchJson").toString());
  }
  if (after.size() != selected.size()) return rejected(QStringLiteral("A selected target is outside this review."));
  shoot_match_state_.remove("error");
  shoot_match_state_.insert("state", "applying"); emit shootMatchChanged();
  const auto count = applyExactAdjustmentPatches(after);
  if (count > 0) {
    batch_recipe_undo_stack_.push_back({.before_adjustment_patch_jsons = before, .after_adjustment_patch_jsons = after,
        .label = QStringLiteral("Matched %1 photos to %2").arg(count).arg(shoot_match_plan_.value("referenceName").toString())});
    if (batch_recipe_undo_stack_.size() > 100) batch_recipe_undo_stack_.removeFirst();
    batch_recipe_redo_stack_.clear(); emit batchRecipeHistoryChanged();
  }
  shoot_match_state_.insert("state", "applied"); shoot_match_state_.insert("appliedCount", count);
  setStatus(QStringLiteral("Applied reviewed reference matching to %1 photos.").arg(count));
  emit shootMatchChanged(); return count == after.size();
}

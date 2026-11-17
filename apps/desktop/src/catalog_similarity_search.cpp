#include "catalog_similarity_search.h"
#include "catalog_asset_model.h"

#include <QDebug>
#include <QHash>
#include <QTimer>

namespace photo_editor::desktop {
CatalogSimilaritySearch::CatalogSimilaritySearch(CatalogAssetModel* catalog, const QString& catalog_namespace, QObject* parent)
    : QObject(parent), catalog_(catalog), namespace_(catalog_namespace) {
  Q_ASSERT(catalog);
  connect(catalog, &CatalogAssetModel::searchInputsChanged, this, [this](const QStringList& ids) {
    if (projecting_) return;
    if (ids.isEmpty()) {
      invalidate(false, QStringLiteral("searchInputsChanged: empty/global"));
      return;
    }
    for (const auto& id : ids) {
      if (!scope_ids_.contains(id)) {
        if ((preparing_ || search_.running()) && !filter_owned_ &&
            visibleScopeIdentities() != scope_identity_) {
          invalidate(false, QStringLiteral("searchInputsChanged: out-of-scope id=%1 changed photo scope").arg(id));
          return;
        }
        continue;
      }
      // The query snapshot is frozen only after every scoped photo has a
      // current preview. Recipe and preview changes before that point are
      // incorporated into that authoritative snapshot.
      if (preparing_) {
        if (visibleScopeIdentities() != scope_identity_)
          invalidate(false, QStringLiteral("searchInputsChanged: id=%1 changed captured photo identity during preparation").arg(id));
        if (preparing_) continue;
        return;
      }
      invalidate(false, QStringLiteral("searchInputsChanged: captured id=%1 after preparation").arg(id));
      return;
    }
  });
  connect(catalog, &CatalogAssetModel::filtersChanged, this, [this] { invalidate(true, QStringLiteral("filtersChanged")); });
  connect(catalog, &QAbstractItemModel::modelAboutToBeReset, this, [this] { invalidate(true, QStringLiteral("modelAboutToBeReset")); });
  connect(catalog, &QAbstractItemModel::rowsAboutToBeRemoved, this, [this] { scheduleScopeRevalidation(); });
  connect(catalog, &QAbstractItemModel::rowsAboutToBeInserted, this, [this] { scheduleScopeRevalidation(); });
  connect(catalog, &CatalogAssetModel::loadingChanged, this, [this] {
    if (catalog_ && catalog_->loading()) invalidate(false, QStringLiteral("catalog loading started"));
  });
  connect(catalog, &QAbstractItemModel::dataChanged, this,
      [this](const QModelIndex& first, const QModelIndex& last, const QList<int>& roles) {
    if (projecting_) return;
    using Model = CatalogAssetModel;
    const bool identity_role = roles.isEmpty() || roles.contains(Model::AssetIdRole) ||
        roles.contains(Model::FilePathRole) || roles.contains(Model::MediaTypeRole);
    const bool search_input_role = roles.isEmpty() || roles.contains(Model::ThumbnailSourceRole) ||
        roles.contains(Model::AdjustmentPatchJsonRole) || roles.contains(Model::OnlineRole);
    if (!identity_role && !search_input_role) return;

    const auto current_identities = visibleScopeIdentities();
    QStringList role_numbers;
    role_numbers.reserve(roles.size());
    for (const auto role : roles) role_numbers.append(QString::number(role));
    const auto role_text = role_numbers.join(QLatin1Char(','));
    for (auto row = first.row(); row <= last.row(); ++row) {
      const auto id = catalog_ ? catalog_->data(catalog_->index(row, 0), Model::AssetIdRole).toString() : QString{};
      if (!scope_ids_.contains(id)) {
        if (identity_role) scheduleScopeRevalidation();
        continue;
      }
      if (identity_role) {
        const auto captured = scope_identity_.constFind(id);
        const auto current = current_identities.constFind(id);
        if (captured == scope_identity_.cend() || current == current_identities.cend() ||
            captured.value() != current.value()) {
          invalidate(false, QStringLiteral("dataChanged: photo identity changed id=%1 roles=%2")
              .arg(id, role_text));
          return;
        }
      }
      if (search_input_role && !preparing_) {
        invalidate(false, QStringLiteral("dataChanged: search input changed id=%1 roles=%2 preparing=%3 projecting=%4")
            .arg(id, role_text).arg(preparing_).arg(projecting_));
        return;
      }
    }
  });
  connect(catalog, &CatalogAssetModel::searchPreviewReadinessChanged, this, [this] {
    if (preparing_) updatePreparation();
  });
  connect(catalog, &QObject::destroyed, this, [this] {
    catalog_.clear();
    ++revision_;
    ++preparation_generation_;
    preparing_ = false;
    preparation_pending_ids_.clear();
    preparation_asset_ids_.clear();
    preparation_photo_ids_.clear();
    scope_ids_.clear();
    scope_identity_.clear();
    completed_ = false;
    search_.cancel();
    releaseSearchPreviewRequest();
  });
  connect(&search_, &LocalSimilaritySearch::batchFinished, this, [this] {
    completed_ = true;
    showResults();
  });
  connect(&search_, &LocalSimilaritySearch::changed, this, [this] {
    if (preview_request_held_ && !preparing_ && !launching_search_ && !search_.running())
      releaseSearchPreviewRequest();
  });
}

QHash<QString, QPair<QString, QString>> CatalogSimilaritySearch::visibleScopeIdentities() const {
  QHash<QString, QPair<QString, QString>> identities;
  if (!catalog_) return identities;
  const auto assets = catalog_->similaritySearchAssets();
  identities.reserve(assets.size());
  for (const auto& value : assets) {
    const auto asset = value.toMap();
    if (asset.value(QStringLiteral("mediaType")).toString() != QStringLiteral("photo")) continue;
    const auto id = asset.value(QStringLiteral("assetId")).toString();
    if (id.isEmpty()) continue;
    identities.insert(id, {asset.value(QStringLiteral("filePath")).toString(),
        asset.value(QStringLiteral("mediaType")).toString()});
  }
  return identities;
}

void CatalogSimilaritySearch::scheduleScopeRevalidation() {
  if (projecting_ || scope_revalidation_scheduled_ || scope_identity_.isEmpty()) return;
  scope_revalidation_scheduled_ = true;
  const auto ticket = ++scope_revalidation_ticket_;
  QTimer::singleShot(0, this, [this, ticket] {
    if (ticket != scope_revalidation_ticket_) return;
    scope_revalidation_scheduled_ = false;
    revalidateScope();
  });
}

void CatalogSimilaritySearch::revalidateScope() {
  if (scope_identity_.isEmpty() || !catalog_) return;
  if (filter_owned_) {
    invalidate(false, QStringLiteral("deferred rows change while result projection is owned"));
    return;
  }
  if (visibleScopeIdentities() != scope_identity_)
    invalidate(false, QStringLiteral("deferred rows change altered filtered photo scope"));
}

void CatalogSimilaritySearch::invalidate(bool projection_event, const QString& reason) {
  if (projecting_ && projection_event) return;
  if (!preparing_ && !filter_owned_ && !completed_ && !search_.running() && search_.results().isEmpty()) return;
  if (qEnvironmentVariableIsSet("LOOKSY_TRACE_SIMILARITY_INVALIDATION")) {
    qWarning().noquote() << QStringLiteral("similarity_search.invalidate reason='%1' preparing=%2 projecting=%3 filterOwned=%4 completed=%5 running=%6 resultCount=%7")
        .arg(reason).arg(preparing_).arg(projecting_).arg(filter_owned_).arg(completed_)
        .arg(search_.running()).arg(search_.results().size());
  }
  const auto revision = ++revision_;
  ++scope_revalidation_ticket_;
  scope_revalidation_scheduled_ = false;
  ++preparation_generation_;
  preparing_ = false;
  preparation_pending_ids_.clear();
  preparation_asset_ids_.clear();
  preparation_photo_ids_.clear();
  scope_ids_.clear();
  scope_identity_.clear();
  completed_ = false;
  search_.invalidate();
  releaseSearchPreviewRequest();
  // Never mutate the model from inside its reset/data-change notification.
  // Repeated changes replace this cleanup ticket; a newer query supersedes it.
  if (filter_owned_) QTimer::singleShot(0, this, [this, revision] {
    if (revision == revision_ && filter_owned_) clearFilter();
  });
}

void CatalogSimilaritySearch::search(const QString& query, int limit) {
  if (!catalog_ || catalog_->loading() || catalog_->deleting() || search_.running()) return;
  ++scope_revalidation_ticket_;
  scope_revalidation_scheduled_ = false;
  ++preparation_generation_;
  search_.cancel();
  completed_ = false;
  clearFilter();
  // A prior detail-search filter is also removed before taking the current view.
  // Other camera, date, rating, project and text filters remain in effect.
  projecting_ = true;
  catalog_->clearVisualSearchResults();
  projecting_ = false;
  const auto visible_assets = catalog_->similaritySearchAssets();
  QVariantList assets;
  assets.reserve(visible_assets.size());
  for (const auto& value : visible_assets) {
    const auto asset = value.toMap();
    if (asset.value(QStringLiteral("mediaType")).toString() == QStringLiteral("photo"))
      assets.push_back(value);
  }
  scope_ids_.clear();
  scope_identity_.clear();
  preparation_asset_ids_.clear();
  preparation_photo_ids_.clear();
  QSet<QString> seen_ids;
  qint64 reference_bytes = 0;
  for (const auto& value : assets) {
    const auto asset = value.toMap();
    if (asset.value(QStringLiteral("mediaType")).toString() != QStringLiteral("photo")) continue;
    const auto id = value.toMap().value("assetId").toString();
    if (!id.isEmpty() && !seen_ids.contains(id)) {
      seen_ids.insert(id);
      preparation_asset_ids_.append(id);
      scope_ids_.insert(id);
      preparation_photo_ids_.append(id);
      scope_identity_.insert(id, {asset.value(QStringLiteral("filePath")).toString(),
          asset.value(QStringLiteral("mediaType")).toString()});
    }
    reference_bytes += (id.size() + asset.value(QStringLiteral("filePath")).toString().size() +
        asset.value(QStringLiteral("thumbnailSource")).toString().size() +
        asset.value(QStringLiteral("mediaType")).toString().size()) * qint64{2};
  }
  if (!search_.configured() || query.trimmed().isEmpty() || query.size() > 2000 ||
      namespace_.isEmpty() || namespace_.size() > 4096 || limit < 1 || limit > 1000 ||
      assets.isEmpty() || assets.size() > 100000 || reference_bytes > 128 * 1024 * 1024 ||
      preparation_asset_ids_.size() != assets.size()) {
    search_.search(query, assets, namespace_, limit);
    return;
  }

  preparation_query_ = query;
  preparation_limit_ = limit;
  preparation_pending_ids_.clear();
  for (const auto& id : preparation_photo_ids_) preparation_pending_ids_.insert(id);
  preparing_ = true;
  preview_request_held_ = true;
  const auto generation = preparation_generation_;
  search_.beginCatalogPreparation(
      preparation_query_,
      QStringLiteral("Preparing current previews for %1 photos…")
          .arg(preparation_asset_ids_.size()));
  catalog_->requestSearchPreviews(preparation_photo_ids_);
  QTimer::singleShot(10 * 60 * 1000, this, [this, generation] {
    if (!preparing_ || generation != preparation_generation_) return;
    preparing_ = false;
    preparation_pending_ids_.clear();
    preparation_asset_ids_.clear();
    preparation_photo_ids_.clear();
    completed_ = false;
    search_.failCatalogPreparation(
        QStringLiteral("Preparing current previews timed out. No photos were searched; narrow the Library view and retry."));
    releaseSearchPreviewRequest();
  });
  if (preparing_ && generation == preparation_generation_) updatePreparation();
}

void CatalogSimilaritySearch::updatePreparation() {
  if (!preparing_ || !catalog_) return;
  const auto readiness = catalog_->searchPreviewReadiness(preparation_asset_ids_);
  preparation_pending_ids_.clear();
  const auto pending_ids = readiness.value(QStringLiteral("pendingAssetIds")).toStringList();
  for (const auto& id : pending_ids) preparation_pending_ids_.insert(id);

  if (readiness.value(QStringLiteral("complete")).toBool()) {
    // A thumbnail publication can emit readiness from a nested selection
    // callback before its final searchInputsChanged signal. Freeze the query
    // after that complete publication has returned to the event loop.
    const auto generation = preparation_generation_;
    QTimer::singleShot(0, this, [this, generation] { startPreparedSearch(generation); });
    return;
  }

  const auto ready_count = readiness.value(QStringLiteral("readyAssetIds")).toStringList().size();
  const auto unavailable_count = readiness.value(QStringLiteral("unavailableAssetIds")).toStringList().size();
  search_.updateCatalogPreparationStatus(
      QStringLiteral("Preparing current previews · %1/%2 ready · %3 waiting · %4 unavailable")
          .arg(ready_count)
          .arg(preparation_asset_ids_.size())
          .arg(pending_ids.size())
          .arg(unavailable_count));
}

void CatalogSimilaritySearch::startPreparedSearch(quint64 generation) {
  if (!preparing_ || generation != preparation_generation_ || !catalog_ || catalog_->loading() || catalog_->deleting()) return;
  const auto readiness = catalog_->searchPreviewReadiness(preparation_asset_ids_);
  if (!readiness.value(QStringLiteral("complete")).toBool()) {
    updatePreparation();
    return;
  }

  if (visibleScopeIdentities() != scope_identity_) {
    invalidate(false, QStringLiteral("prepared snapshot changed filtered photo identity"));
    return;
  }
  const auto current_assets = catalog_->similaritySearchAssets();
  QHash<QString, QVariantMap> by_id;
  for (const auto& value : current_assets) {
    const auto asset = value.toMap();
    by_id.insert(asset.value(QStringLiteral("assetId")).toString(), asset);
  }
  QVariantList snapshot;
  snapshot.reserve(preparation_asset_ids_.size());
  for (const auto& id : preparation_asset_ids_) {
    const auto it = by_id.constFind(id);
    if (it == by_id.cend()) {
      invalidate(false, QStringLiteral("prepared snapshot missing captured photo id=%1").arg(id));
      return;
    }
    snapshot.append(it.value());
  }

  const auto query = preparation_query_;
  const auto limit = preparation_limit_;
  preparing_ = false;
  preparation_pending_ids_.clear();
  launching_search_ = true;
  search_.endCatalogPreparation();
  search_.search(query, snapshot, namespace_, limit);
  launching_search_ = false;
  if (!search_.running()) releaseSearchPreviewRequest();
}

void CatalogSimilaritySearch::releaseSearchPreviewRequest() {
  if (!preview_request_held_) return;
  preview_request_held_ = false;
  if (catalog_) catalog_->releaseSearchPreviews();
}

void CatalogSimilaritySearch::showResults() {
  if (!catalog_ || !completed_ || search_.running()) return;
  ++revision_;
  ++scope_revalidation_ticket_;
  scope_revalidation_scheduled_ = false;
  projecting_ = true;
  filter_owned_ = true;
  catalog_->setVisualSearchResults(search_.resultIds());
  projecting_ = false;
}

void CatalogSimilaritySearch::clearFilter() {
  ++revision_;
  const bool owned = filter_owned_;
  filter_owned_ = false;
  if (!catalog_ || !owned) return;
  projecting_ = true;
  catalog_->clearVisualSearchResults();
  projecting_ = false;
}

void CatalogSimilaritySearch::cancel() {
  ++revision_;
  ++scope_revalidation_ticket_;
  scope_revalidation_scheduled_ = false;
  ++preparation_generation_;
  preparing_ = false;
  preparation_pending_ids_.clear();
  preparation_asset_ids_.clear();
  preparation_photo_ids_.clear();
  scope_ids_.clear();
  scope_identity_.clear();
  completed_ = false;
  search_.cancel();
  releaseSearchPreviewRequest();
  clearFilter();
}
}  // namespace photo_editor::desktop

#include "catalog_similarity_search.h"
#include "catalog_asset_model.h"

#include <QTimer>

namespace photo_editor::desktop {
CatalogSimilaritySearch::CatalogSimilaritySearch(CatalogAssetModel* catalog, const QString& catalog_namespace, QObject* parent)
    : QObject(parent), catalog_(catalog), namespace_(catalog_namespace) {
  Q_ASSERT(catalog);
  connect(catalog, &CatalogAssetModel::searchInputsChanged, this, [this](const QStringList& ids) {
    if (ids.isEmpty()) { invalidate(false); return; }
    for (const auto& id : ids) if (scope_ids_.contains(id)) { invalidate(false); return; }
  });
  connect(catalog, &CatalogAssetModel::filtersChanged, this, [this] { invalidate(true); });
  connect(catalog, &QAbstractItemModel::modelAboutToBeReset, this, [this] { invalidate(true); });
  connect(catalog, &QAbstractItemModel::rowsAboutToBeRemoved, this, [this] { invalidate(false); });
  connect(catalog, &QAbstractItemModel::rowsAboutToBeInserted, this, [this] { invalidate(false); });
  connect(catalog, &CatalogAssetModel::loadingChanged, this, [this] {
    if (catalog_ && catalog_->loading()) invalidate(false);
  });
  connect(catalog, &QAbstractItemModel::dataChanged, this,
      [this](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
    using Model = CatalogAssetModel;
    for (const auto role : {Model::AssetIdRole, Model::FilePathRole, Model::ThumbnailSourceRole,
                           Model::AdjustmentPatchJsonRole, Model::MediaTypeRole, Model::OnlineRole}) {
      if (roles.isEmpty() || roles.contains(role)) { invalidate(false); return; }
    }
  });
  connect(&search_, &LocalSimilaritySearch::batchFinished, this, [this] {
    completed_ = true;
    showResults();
  });
}

void CatalogSimilaritySearch::invalidate(bool projection_event) {
  if (projecting_ && projection_event) return;
  if (!filter_owned_ && !completed_ && !search_.running() && search_.results().isEmpty()) return;
  const auto revision = ++revision_;
  completed_ = false;
  search_.invalidate();
  // Never mutate the model from inside its reset/data-change notification.
  // Repeated changes replace this cleanup ticket; a newer query supersedes it.
  if (filter_owned_) QTimer::singleShot(0, this, [this, revision] {
    if (revision == revision_ && filter_owned_) clearFilter();
  });
}

void CatalogSimilaritySearch::search(const QString& query, int limit) {
  if (!catalog_ || catalog_->loading() || catalog_->deleting() || search_.running()) return;
  completed_ = false;
  clearFilter();
  // A prior detail-search filter is also removed before taking the current view.
  // Other camera, date, rating, project and text filters remain in effect.
  projecting_ = true;
  catalog_->clearVisualSearchResults();
  projecting_ = false;
  const auto assets = catalog_->visibleAssets();
  scope_ids_.clear();
  for (const auto& value : assets) scope_ids_.insert(value.toMap().value("assetId").toString());
  search_.search(query, assets, namespace_, limit);
}

void CatalogSimilaritySearch::showResults() {
  if (!catalog_ || !completed_ || search_.running()) return;
  ++revision_;
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
  completed_ = false;
  search_.cancel();
  clearFilter();
}
}  // namespace photo_editor::desktop

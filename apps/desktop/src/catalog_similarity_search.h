#pragma once

#include "local_similarity_search.h"
#include <QPointer>
#include <QSet>

namespace photo_editor::desktop {
class CatalogAssetModel;

// Owns the live catalog binding, not just the lifetime of a QML panel.
class CatalogSimilaritySearch final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QObject* controller READ qmlController CONSTANT)
 public:
  CatalogSimilaritySearch(CatalogAssetModel* catalog, const QString& catalog_namespace, QObject* parent = nullptr);
  LocalSimilaritySearch* controller() { return &search_; }
  QObject* qmlController() { return &search_; }
  Q_INVOKABLE void search(const QString& query, int limit = 32);
  Q_INVOKABLE void showResults();
  Q_INVOKABLE void clearFilter();
  Q_INVOKABLE void cancel();
 private:
  void invalidate(bool projection_event);
  QPointer<CatalogAssetModel> catalog_;
  QString namespace_;
  QSet<QString> scope_ids_;
  LocalSimilaritySearch search_;
  bool projecting_ = false, filter_owned_ = false, completed_ = false;
  quint64 revision_ = 0;
};
}  // namespace photo_editor::desktop

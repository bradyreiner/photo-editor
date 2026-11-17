#pragma once

#include "local_similarity_search.h"
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QPair>

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
  void invalidate(bool projection_event, const QString& reason);
  void scheduleScopeRevalidation();
  void revalidateScope();
  [[nodiscard]] QHash<QString, QPair<QString, QString>> visibleScopeIdentities() const;
  void updatePreparation();
  void startPreparedSearch(quint64 generation);
  void releaseSearchPreviewRequest();
  QPointer<CatalogAssetModel> catalog_;
  QString namespace_;
  QSet<QString> scope_ids_;
  QHash<QString, QPair<QString, QString>> scope_identity_;
  QStringList preparation_asset_ids_;
  QStringList preparation_photo_ids_;
  QSet<QString> preparation_pending_ids_;
  QString preparation_query_;
  int preparation_limit_ = 32;
  LocalSimilaritySearch search_;
  bool projecting_ = false, filter_owned_ = false, completed_ = false;
  bool preparing_ = false, preview_request_held_ = false, launching_search_ = false;
  bool scope_revalidation_scheduled_ = false;
  quint64 revision_ = 0, preparation_generation_ = 0, scope_revalidation_ticket_ = 0;
};
}  // namespace photo_editor::desktop

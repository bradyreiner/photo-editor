#pragma once

#include "visual_search_controller.h"
#include <functional>

namespace photo_editor::desktop {
class CatalogAssetModel;

// Owns a search snapshot, not library selection. Only the local confirmation UI
// may call confirmTargets; MCP deliberately exposes no confirmation command.
class SearchEditWorkflow final : public QObject {
  Q_OBJECT
  Q_PROPERTY(VisualSearchController* search READ search CONSTANT)
  Q_PROPERTY(QString phase READ phase NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString confirmationToken READ confirmationToken NOTIFY changed)
  Q_PROPERTY(QStringList confirmedIds READ confirmedIds NOTIFY changed)
 public:
  explicit SearchEditWorkflow(CatalogAssetModel* catalog, QObject* parent = nullptr);
  VisualSearchController* search() { return &search_; }
  QString phase() const { return phase_; }
  QString status() const { return status_; }
  QString confirmationToken() const { return confirmation_token_; }
  QStringList confirmedIds() const { return confirmed_ids_; }
  void setDraftBackend(std::function<bool()> busy,
                       std::function<QVariantMap(const QStringList&, const QString&)> start);
  Q_INVOKABLE QVariantMap start(const QString& instruction, const QVariantMap& model);
  Q_INVOKABLE void scanMore();
  Q_INVOKABLE void cancel();
  Q_INVOKABLE QVariantMap confirmTargets(const QString& token, const QStringList& ids,
                                        const QString& edit_instructions);
  Q_INVOKABLE QVariantMap snapshot() const;
  void draftFinished(const QString& request_id, const QVariantMap& report);
 signals:
  void changed();
  void draftStarted(const QStringList& ids);
 private:
  QVariantMap reject(const QString& message);
  CatalogAssetModel* catalog_;
  VisualSearchController search_;
  std::function<bool()> busy_;
  std::function<QVariantMap(const QStringList&, const QString&)> start_draft_;
  QString phase_ = QStringLiteral("idle");
  QString status_;
  QString confirmation_token_;
  QString request_id_;
  QStringList confirmed_ids_;
  QHash<QString, QByteArray> source_snapshots_;
};
}  // namespace photo_editor::desktop

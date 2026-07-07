#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace photo_editor::desktop {

class AgentMcpClient final : public QObject {
  Q_OBJECT

 public:
  explicit AgentMcpClient(QObject* parent = nullptr);

  [[nodiscard]] QString endpoint() const;
  void setEndpoint(const QString& endpoint);

  [[nodiscard]] int timeoutMs() const;
  void setTimeoutMs(int timeout_ms);

  [[nodiscard]] QStringList allowedTools() const;
  void setAllowedTools(const QStringList& allowed_tools);
  void clearAllowedTools();
  void setCancellationPredicate(std::function<bool()> predicate);
  void clearCancellationPredicate();

  [[nodiscard]] QVariantList cachedTools() const;
  [[nodiscard]] QStringList cachedToolNames() const;
  [[nodiscard]] bool hasCachedTool(const QString& name) const;

  Q_INVOKABLE QVariantMap refreshToolCache(const QString& run_id = QString());
  Q_INVOKABLE QVariantMap callTool(const QString& name, const QVariantMap& arguments = QVariantMap());
  Q_INVOKABLE void cancelActiveRequest();
  Q_INVOKABLE void resetCancellation();

 private:
  [[nodiscard]] QVariantMap postJsonRpc(const QVariantMap& request);
  [[nodiscard]] bool cancellationRequested() const;

  QString endpoint_;
  int timeout_ms_ = 30000;
  QStringList allowed_tools_;
  QVariantList cached_tools_;
  QString cached_run_id_;
  int next_request_id_ = 1;
  bool cancellation_requested_ = false;
  std::function<bool()> cancellation_predicate_;
};

}  // namespace photo_editor::desktop

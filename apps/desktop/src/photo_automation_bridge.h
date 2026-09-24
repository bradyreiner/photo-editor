#pragma once

#include <QByteArray>
#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QQueue>
#include <functional>

class QLocalServer;
class QLocalSocket;
class QTcpServer;
class QTcpSocket;
class QIODevice;

namespace photo_editor::desktop {

class AgentEditController;
class CatalogAssetModel;
class PhotoMcpServer;
class SearchEditWorkflow;

class PhotoAutomationBridge final : public QObject {
  Q_OBJECT

 public:
  PhotoAutomationBridge(
      AgentEditController* agent_edit,
      CatalogAssetModel* catalog_model,
      QObject* parent = nullptr);
  ~PhotoAutomationBridge() override;

  [[nodiscard]] bool running() const;
  [[nodiscard]] QString endpointName() const;
  [[nodiscard]] QString endpointKind() const;
  [[nodiscard]] QString mcpEndpoint() const;
  [[nodiscard]] QString mcpEndpointKind() const;
  [[nodiscard]] QStringList mcpCapabilities() const;
  [[nodiscard]] int mcpConnectedClientCount() const;
  [[nodiscard]] int mcpTotalClientCount() const;
  [[nodiscard]] QString mcpLastToolCallName() const;
  [[nodiscard]] QString mcpLastToolCallStatus() const;
  [[nodiscard]] QString mcpLastToolCallAt() const;
  [[nodiscard]] QString token() const;
  [[nodiscard]] QString sessionFilePath() const;
  [[nodiscard]] QString errorMessage() const;

  bool start(
      bool enable_mcp_http = false,
      const QStringList& mcp_capabilities = {QStringLiteral("read")});
  void stop();
  void setSearchEditWorkflow(SearchEditWorkflow* workflow) { search_edit_ = workflow; }
  void setEditAccessPolicy(std::function<bool()> policy) { edit_access_policy_ = std::move(policy); }
  [[nodiscard]] QJsonObject handleJsonRequest(const QJsonObject& request) const;

 private:
  std::function<bool()> edit_access_policy_;
  SearchEditWorkflow* search_edit_ = nullptr;
  void acceptPendingConnections();
  void acceptPendingTcpConnections();
  void acceptPendingMcpHttpConnections();
  void readSocketRequest(QIODevice* socket);
  void readMcpHttpRequest(QTcpSocket* socket);
  void reply(QIODevice* socket, const QJsonObject& response) const;
  void replyMcpHttp(
      QTcpSocket* socket,
      int status_code,
      const QByteArray& status_text,
      const QJsonObject& response) const;
  void replyMcpHttpSse(QTcpSocket* socket) const;
  [[nodiscard]] bool validMcpAuthorization(const QByteArray& authorization) const;
  [[nodiscard]] bool admitMcpRequest();
  [[nodiscard]] QJsonObject handleCommand(const QString& command, const QJsonObject& args) const;
  [[nodiscard]] bool writeSessionFile();
  void removeSessionFile() const;
  void recordMcpToolCall(const QString& tool_name, const QString& status);

  AgentEditController* agent_edit_ = nullptr;
  CatalogAssetModel* catalog_model_ = nullptr;
  QLocalServer* server_ = nullptr;
  QTcpServer* tcp_server_ = nullptr;
  QTcpServer* mcp_http_server_ = nullptr;
  PhotoMcpServer* mcp_server_ = nullptr;
  QString endpoint_name_;
  QString endpoint_kind_;
  QString mcp_endpoint_;
  QString mcp_endpoint_kind_;
  QStringList mcp_capabilities_;
  int mcp_connected_client_count_ = 0;
  int mcp_total_client_count_ = 0;
  QString mcp_last_tool_call_name_;
  QString mcp_last_tool_call_status_;
  QString mcp_last_tool_call_at_;
  QQueue<qint64> mcp_request_times_ms_;
  int mcp_active_tool_calls_ = 0;
  QString token_;
  QString session_file_path_;
  QString error_message_;
};

}  // namespace photo_editor::desktop

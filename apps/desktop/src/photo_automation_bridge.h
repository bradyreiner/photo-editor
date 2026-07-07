#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>

class QLocalServer;
class QLocalSocket;
class QTcpServer;
class QTcpSocket;
class QIODevice;

namespace photo_editor::desktop {

class AgentEditController;
class CatalogAssetModel;
class PhotoMcpServer;

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
  [[nodiscard]] int mcpConnectedClientCount() const;
  [[nodiscard]] int mcpTotalClientCount() const;
  [[nodiscard]] QString mcpLastToolCallName() const;
  [[nodiscard]] QString mcpLastToolCallStatus() const;
  [[nodiscard]] QString mcpLastToolCallAt() const;
  [[nodiscard]] QString token() const;
  [[nodiscard]] QString sessionFilePath() const;
  [[nodiscard]] QString errorMessage() const;

  bool start();
  void stop();
  [[nodiscard]] QJsonObject handleJsonRequest(const QJsonObject& request) const;

 private:
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
  int mcp_connected_client_count_ = 0;
  int mcp_total_client_count_ = 0;
  QString mcp_last_tool_call_name_;
  QString mcp_last_tool_call_status_;
  QString mcp_last_tool_call_at_;
  QString token_;
  QString session_file_path_;
  QString error_message_;
};

}  // namespace photo_editor::desktop

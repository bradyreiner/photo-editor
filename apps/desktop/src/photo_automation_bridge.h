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
  [[nodiscard]] QString token() const;
  [[nodiscard]] QString sessionFilePath() const;
  [[nodiscard]] QString errorMessage() const;

  bool start();
  void stop();
  [[nodiscard]] QJsonObject handleJsonRequest(const QJsonObject& request) const;

 private:
  void acceptPendingConnections();
  void acceptPendingTcpConnections();
  void readSocketRequest(QIODevice* socket);
  void reply(QIODevice* socket, const QJsonObject& response) const;
  [[nodiscard]] QJsonObject handleCommand(const QString& command, const QJsonObject& args) const;
  [[nodiscard]] bool writeSessionFile();
  void removeSessionFile() const;

  AgentEditController* agent_edit_ = nullptr;
  CatalogAssetModel* catalog_model_ = nullptr;
  QLocalServer* server_ = nullptr;
  QTcpServer* tcp_server_ = nullptr;
  QString endpoint_name_;
  QString endpoint_kind_;
  QString token_;
  QString session_file_path_;
  QString error_message_;
};

}  // namespace photo_editor::desktop

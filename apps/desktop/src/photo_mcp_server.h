#pragma once

#include <QJsonObject>
#include <QObject>

namespace photo_editor::desktop {

class PhotoAutomationBridge;

class PhotoMcpServer final : public QObject {
  Q_OBJECT

 public:
  explicit PhotoMcpServer(PhotoAutomationBridge* automation_bridge, QObject* parent = nullptr);

  [[nodiscard]] QJsonObject handleJsonRpc(const QJsonObject& request) const;
  [[nodiscard]] QJsonObject toolsListResult() const;
  [[nodiscard]] QJsonObject callTool(const QString& name, const QJsonObject& arguments) const;

 private:
  PhotoAutomationBridge* automation_bridge_ = nullptr;
};

}  // namespace photo_editor::desktop

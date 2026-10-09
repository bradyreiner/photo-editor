#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>

namespace photo_editor::desktop {

class PhotoAutomationBridge;

class PhotoMcpServer final : public QObject {
  Q_OBJECT

 public:
  explicit PhotoMcpServer(PhotoAutomationBridge* automation_bridge, QObject* parent = nullptr,
                          bool internal_agent = false);

  [[nodiscard]] QJsonObject handleJsonRpc(const QJsonObject& request) const;
  [[nodiscard]] QJsonObject handleJsonRpc(
      const QJsonObject& request,
      const QStringList& capabilities) const;
  [[nodiscard]] QJsonObject toolsListResult() const;
  [[nodiscard]] QJsonObject toolsListResult(const QStringList& capabilities) const;
  [[nodiscard]] QJsonObject callTool(const QString& name, const QJsonObject& arguments) const;
  [[nodiscard]] QJsonObject callTool(
      const QString& name,
      const QJsonObject& arguments,
      const QStringList& capabilities) const;

 private:
  PhotoAutomationBridge* automation_bridge_ = nullptr;
  bool internal_agent_ = false;
};

}  // namespace photo_editor::desktop

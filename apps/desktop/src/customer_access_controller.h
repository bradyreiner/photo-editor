#pragma once

#include <QObject>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTimer>

namespace photo_editor::desktop {

class CustomerAccessController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool configured READ configured CONSTANT)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(bool active READ active NOTIFY changed)
  Q_PROPERTY(bool editingAllowed READ editingAllowed NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString plan READ plan NOTIFY changed)
  Q_PROPERTY(QString expires READ expires NOTIFY changed)
  Q_PROPERTY(QString customerId READ customerId NOTIFY changed)
 public:
  explicit CustomerAccessController(QObject* parent = nullptr);
  [[nodiscard]] bool configured() const;
  [[nodiscard]] bool busy() const { return busy_; }
  [[nodiscard]] bool active() const { return configured() && editingAllowed(); }
  [[nodiscard]] bool editingAllowed() const { return allows(QStringLiteral("editor")); }
  [[nodiscard]] QString status() const { return status_; }
  [[nodiscard]] QString plan() const;
  [[nodiscard]] QString expires() const;
  [[nodiscard]] QString customerId() const;
  Q_INVOKABLE bool allows(const QString& capability) const;
  Q_INVOKABLE void startTrial();
  Q_INVOKABLE void activate(const QString& license_key);
  Q_INVOKABLE void refresh();
  Q_INVOKABLE void recoverPurchase() const;
 signals:
  void changed();
 private:
  bool ensureCredential();
  void request(const QString& path, const QJsonObject& input, bool authenticated);
  void loadLease();
  QNetworkAccessManager network_;
  QTimer expiry_timer_;
  QTimer refresh_timer_;
  QByteArray credential_;
  QJsonObject claims_;
  QString status_;
  bool busy_ = false;
};

}  // namespace photo_editor::desktop

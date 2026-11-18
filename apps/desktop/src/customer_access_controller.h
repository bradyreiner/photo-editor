#pragma once

#include <QObject>
#include <QJsonObject>
#include <QVariantList>
#include <QNetworkAccessManager>
#include <QTimer>

namespace photo_editor::desktop {

class CustomerCredentialStore;

struct CustomerAccessConfig {
  QString apiUrl;
  QString issuer;
  QByteArray primaryPublicKey;
  QString primaryKeyId;
  QByteArray trustedKeysJson = QByteArrayLiteral("{}");
  QString appVersion;
  int appMajor = 1;
  bool allowInsecureLoopbackForTests = false;
  QString purchaseUrl;
  QString subscriptionPurchaseUrl;
  QString billingPortalUrl;
};

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
  Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
  Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
  Q_PROPERTY(bool canSignOut READ canSignOut NOTIFY changed)
  Q_PROPERTY(QString accountEmail READ accountEmail NOTIFY changed)
  Q_PROPERTY(bool emailCodePending READ emailCodePending NOTIFY changed)
  Q_PROPERTY(QVariantList accountLicenses READ accountLicenses NOTIFY changed)
  Q_PROPERTY(QString currentInstallationId READ currentInstallationId NOTIFY changed)
  Q_PROPERTY(QString purchaseUrl READ purchaseUrl CONSTANT)
  Q_PROPERTY(QString subscriptionPurchaseUrl READ subscriptionPurchaseUrl CONSTANT)
  Q_PROPERTY(QString billingPortalUrl READ billingPortalUrl CONSTANT)
 public:
  explicit CustomerAccessController(QObject* parent = nullptr);
  CustomerAccessController(CustomerAccessConfig config, CustomerCredentialStore* credential_store,
                           QObject* parent = nullptr);
  [[nodiscard]] bool configured() const;
  [[nodiscard]] bool busy() const { return busy_; }
  [[nodiscard]] bool active() const { return configured() && editingAllowed(); }
  [[nodiscard]] bool editingAllowed() const { return allows(QStringLiteral("editor")); }
  [[nodiscard]] QString status() const { return status_; }
  [[nodiscard]] QString plan() const;
  [[nodiscard]] QString expires() const;
  [[nodiscard]] QString customerId() const;
  [[nodiscard]] QVariantList devices() const { return devices_; }
  [[nodiscard]] bool signedIn() const { return !account_id_.isEmpty(); }
  [[nodiscard]] bool canSignOut() const { return !credential_.isEmpty(); }
  [[nodiscard]] QString accountEmail() const { return account_email_; }
  [[nodiscard]] bool emailCodePending() const { return !email_challenge_id_.isEmpty(); }
  [[nodiscard]] QVariantList accountLicenses() const { return account_licenses_; }
  [[nodiscard]] QString currentInstallationId() const { return current_installation_id_; }
  [[nodiscard]] QString purchaseUrl() const { return config_.purchaseUrl; }
  [[nodiscard]] QString subscriptionPurchaseUrl() const { return config_.subscriptionPurchaseUrl; }
  [[nodiscard]] QString billingPortalUrl() const { return config_.billingPortalUrl; }
  Q_INVOKABLE bool allows(const QString& capability) const;
  Q_INVOKABLE void startTrial();
  Q_INVOKABLE void activate(const QString& license_key);
  Q_INVOKABLE void recoverAccess(const QString& license_key);
  Q_INVOKABLE void refresh();
  Q_INVOKABLE void loadDevices();
  Q_INVOKABLE void revokeDevice(const QString& installation_id);
  Q_INVOKABLE void deactivateCurrentDevice();
  Q_INVOKABLE void recoverPurchase() const;
  Q_INVOKABLE void requestEmailSignIn(const QString& email);
  Q_INVOKABLE void verifyEmailSignIn(const QString& code);
  Q_INVOKABLE void cancelEmailSignIn();
  Q_INVOKABLE void loadAccount();
  Q_INVOKABLE void selectAccountLicense(const QString& license_id);
  Q_INVOKABLE void redeemVip(const QString& code);
  Q_INVOKABLE void signOut();
  Q_INVOKABLE void startCheckout(const QString& offer);
  Q_INVOKABLE void openBillingPortal();
 signals:
  void changed();
 private:
  bool ensureCredential();
  void request(const QString& path, const QJsonObject& input, bool authenticated,
               const QByteArray& method = QByteArrayLiteral("POST"),
               const QByteArray& replacement_credential = {});
  void applyVerifiedLease(const QByteArray& token, const QByteArray& credential,
                         bool replace_credential, const QString& failure_status = {});
  void clearLocalAccess(bool clear_credential = true);
  void loadLease();
  void applyAccount(const QJsonObject& response);
  void finishSignOut(bool server_revoked);
  [[nodiscard]] QString leaseKey() const;
  [[nodiscard]] QString clockKey() const;
  CustomerAccessConfig config_;
  CustomerCredentialStore* credential_store_ = nullptr;
  QNetworkAccessManager network_;
  QTimer expiry_timer_;
  QTimer refresh_timer_;
  QByteArray credential_;
  QByteArray pending_recovery_credential_;
  QByteArray pending_recovery_key_digest_;
  QJsonObject claims_;
  QVariantList devices_;
  QVariantList account_licenses_;
  QString account_id_;
  QString account_email_;
  QString email_challenge_id_;
  QString current_installation_id_;
  QString status_;
  bool busy_ = false;
  bool deactivation_pending_ = false;
};

}  // namespace photo_editor::desktop

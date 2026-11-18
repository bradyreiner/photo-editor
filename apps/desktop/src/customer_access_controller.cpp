#include "customer_access_controller.h"
#include "customer_credential_store.h"
#include "customer_lease.h"
#include "customer_platform_config.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

namespace photo_editor::desktop {
namespace {

CustomerAccessConfig compiledConfig() {
  CustomerAccessConfig config;
  config.apiUrl = QString::fromUtf8(LOOKSY_CUSTOMER_API_URL);
  config.issuer = config.apiUrl;
  config.primaryPublicKey = QByteArray::fromBase64(LOOKSY_ENTITLEMENT_PUBLIC_KEY);
  config.primaryKeyId = QString::fromUtf8(LOOKSY_ENTITLEMENT_KEY_ID);
  config.trustedKeysJson = QByteArray(LOOKSY_ENTITLEMENT_TRUSTED_KEYS_JSON);
  config.appVersion = QString::fromUtf8(LOOKSY_APP_VERSION);
  config.appMajor = LOOKSY_APP_MAJOR;
  config.purchaseUrl = QString::fromUtf8(LOOKSY_PURCHASE_URL);
  config.subscriptionPurchaseUrl = QString::fromUtf8(LOOKSY_SUBSCRIPTION_PURCHASE_URL);
  config.billingPortalUrl = QString::fromUtf8(LOOKSY_BILLING_PORTAL_URL);
  return config;
}

CustomerAccessConfig completeConfig(CustomerAccessConfig config) {
  if (config.issuer.isEmpty()) config.issuer = config.apiUrl;
  return config;
}

QByteArray newCredential() {
  QByteArray random(32, Qt::Uninitialized);
  for (int offset = 0; offset < random.size(); offset += 4) {
    const auto value = QRandomGenerator::system()->generate();
    for (int byte = 0; byte < 4; ++byte)
      random[offset + byte] = static_cast<char>((value >> (byte * 8)) & 0xffU);
  }
  return random.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
}  // namespace

CustomerAccessController::CustomerAccessController(QObject* parent)
    : CustomerAccessController(compiledConfig(), nullptr, parent) {}

CustomerAccessController::CustomerAccessController(CustomerAccessConfig config,
    CustomerCredentialStore* credential_store, QObject* parent)
    : QObject(parent), config_(completeConfig(std::move(config))),
      credential_store_(credential_store ? credential_store : &systemCustomerCredentialStore(config_.issuer)), network_(this) {
  if (!configured()) {
    status_ = config_.apiUrl.isEmpty()
        ? QStringLiteral("This version uses receipt-based ownership. Keep your purchase receipt.")
        : QStringLiteral("The license service configuration is invalid. Viewing, saving, and exporting remain available. Contact support.");
    return;
  }
  credential_ = credential_store_->load(&status_);
  if (!credential_.isEmpty()) loadLease();
  else if (status_.isEmpty()) status_ = QStringLiteral("Start a guest trial or activate your purchase.");
  expiry_timer_.setInterval(60000);
  connect(&expiry_timer_, &QTimer::timeout, this, &CustomerAccessController::loadLease);
  expiry_timer_.start();
  refresh_timer_.setInterval(6 * 60 * 60 * 1000);
  connect(&refresh_timer_, &QTimer::timeout, this, &CustomerAccessController::refresh);
  refresh_timer_.start();
  if (!credential_.isEmpty()) {
    QTimer::singleShot(0, this, &CustomerAccessController::refresh);
    QTimer::singleShot(0, this, &CustomerAccessController::loadDevices);
  }
}

QString CustomerAccessController::leaseKey() const {
  return config_.issuer.isEmpty() ? QStringLiteral("customerAccess/lease") :
      QStringLiteral("customerAccess/%1/lease").arg(customerCredentialScope(config_.issuer));
}
QString CustomerAccessController::clockKey() const {
  return config_.issuer.isEmpty() ? QStringLiteral("customerAccess/lastObservedTime") :
      QStringLiteral("customerAccess/%1/lastObservedTime").arg(customerCredentialScope(config_.issuer));
}
bool CustomerAccessController::configured() const {
  const QUrl url(config_.apiUrl);
  const bool test_loopback = config_.allowInsecureLoopbackForTests && url.scheme() == QStringLiteral("http") &&
      (url.host() == QStringLiteral("127.0.0.1") || url.host() == QStringLiteral("localhost"));
  return url.isValid() && (url.scheme() == QStringLiteral("https") || test_loopback) && !url.host().isEmpty() &&
      url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment() &&
      validCustomerKeyConfiguration(config_.primaryPublicKey, config_.primaryKeyId, config_.trustedKeysJson);
}
QString CustomerAccessController::plan() const { return claims_.value(QStringLiteral("plan")).toString(); }
QString CustomerAccessController::customerId() const { return claims_.value(QStringLiteral("sub")).toString(); }
QString CustomerAccessController::expires() const {
  if (claims_.isEmpty()) return {};
  return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(claims_.value(QStringLiteral("exp")).toDouble()))
      .toLocalTime().toString(QStringLiteral("MMM d, yyyy h:mm AP"));
}
bool CustomerAccessController::allows(const QString& capability) const {
  if (config_.apiUrl.isEmpty()) return true;
  if (capability == QStringLiteral("view") || capability == QStringLiteral("save") || capability == QStringLiteral("export")) return true;
  if (!configured()) return false;
  if (claims_.value(QStringLiteral("exp")).toDouble() <= static_cast<double>(QDateTime::currentSecsSinceEpoch())) return false;
  return claims_.value(QStringLiteral("features")).toArray().contains(capability);
}
bool CustomerAccessController::ensureCredential() {
  if (!configured() || busy_) return false;
  if (!credential_.isEmpty()) return true;
  QString error;
  credential_ = credential_store_->load(&error);
  if (!error.isEmpty()) { status_ = error; emit changed(); return false; }
  if (!credential_.isEmpty()) return true;
  const auto generated = newCredential();
  if (!credential_store_->save(generated, &error)) {
    status_ = error; emit changed(); return false;
  }
  credential_ = generated;
  return true;
}
void CustomerAccessController::loadLease() {
  if (!configured() || credential_.isEmpty()) return;
  QSettings settings;
  const auto result = verifyCustomerLeaseWithTrustedKeys(settings.value(leaseKey()).toByteArray(),
      config_.primaryPublicKey, config_.primaryKeyId, config_.trustedKeysJson, config_.issuer, credential_,
      QDateTime::currentSecsSinceEpoch(), settings.value(clockKey(), 0).toLongLong(), config_.appMajor);
  claims_ = result.claims;
  if (result.valid()) {
    settings.setValue(clockKey(), qMax(settings.value(clockKey(), 0).toLongLong(), QDateTime::currentSecsSinceEpoch()));
    status_ = QStringLiteral("Access is active. Offline access is available until %1.").arg(expires());
  } else status_ = result.error;
  emit changed();
}
void CustomerAccessController::request(const QString& path, const QJsonObject& input, bool authenticated,
    const QByteArray& method, const QByteArray& replacement_credential) {
  if (replacement_credential.isEmpty() && method != QByteArrayLiteral("GET") && !ensureCredential()) return;
  if (authenticated && credential_.isEmpty()) return;
  busy_ = true;
  if (path != QStringLiteral("/v1/devices") && path != QStringLiteral("/v1/account")) status_ = QStringLiteral("Contacting the license service…");
  emit changed();
  QNetworkRequest network_request(QUrl(config_.apiUrl + path));
  network_request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  network_request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  network_request.setTransferTimeout(15000);
  if (authenticated) network_request.setRawHeader("Authorization", "Bearer " + credential_);
  QNetworkReply* reply = nullptr;
  if (method == QByteArrayLiteral("GET")) reply = network_.get(network_request);
  else reply = network_.post(network_request, QJsonDocument(input).toJson(QJsonDocument::Compact));
  reply->setReadBufferSize(32769);
  connect(reply, &QNetworkReply::readyRead, reply, [reply] { if (reply->bytesAvailable() > 32768) reply->abort(); });
  connect(reply, &QNetworkReply::finished, this, [this, reply, path, replacement_credential, authenticated] {
    const auto http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto network_error = reply->error();
    const auto data = reply->read(32768);
    reply->deleteLater();
    const auto object = QJsonDocument::fromJson(data).object();
    busy_ = false;
    if (path == QStringLiteral("/v1/account/logout")) {
      finishSignOut(http == 200 && network_error == QNetworkReply::NoError && object.value(QStringLiteral("ok")).toBool());
      return;
    }
    if (http == 200 && network_error == QNetworkReply::NoError) {
      if (path == QStringLiteral("/v1/billing/checkout") || path == QStringLiteral("/v1/billing/portal")) {
        const QUrl destination(object.value(QStringLiteral("url")).toString());
        const auto safe_origin = [&destination](const QString& value) {
          const QUrl configured(value);
          return configured.scheme() == QStringLiteral("https") && !configured.host().isEmpty() &&
              destination.host() == configured.host() && destination.port(443) == configured.port(443);
        };
        const bool paddle_host = destination.port(443) == 443 && (destination.host() == QStringLiteral("paddle.com") ||
            destination.host().endsWith(QStringLiteral(".paddle.com")));
        const bool permitted = destination.isValid() && destination.scheme() == QStringLiteral("https") &&
            destination.userInfo().isEmpty() && (paddle_host || safe_origin(config_.purchaseUrl) ||
                safe_origin(config_.subscriptionPurchaseUrl) || safe_origin(config_.billingPortalUrl));
        if (!permitted || !QDesktopServices::openUrl(destination))
          status_ = QStringLiteral("The secure billing page could not be opened. Contact support.");
        else status_ = QStringLiteral("Billing opened in your browser. Refresh access after completing checkout.");
        emit changed(); return;
      }
      if (path == QStringLiteral("/v1/account/email/request")) {
        email_challenge_id_ = object.value(QStringLiteral("challengeId")).toString();
        status_ = email_challenge_id_.isEmpty()
            ? QStringLiteral("Sign-in could not start. Try again.")
            : QStringLiteral("Check your email for the eight-digit sign-in code.");
        emit changed();
        return;
      }
      if (path == QStringLiteral("/v1/account")) {
        applyAccount(object);
        emit changed();
        return;
      }
      if (path == QStringLiteral("/v1/account/email/verify")) {
        const auto account = object.value(QStringLiteral("account")).toObject();
        if (account.value(QStringLiteral("id")).toString().isEmpty() || account.value(QStringLiteral("email")).toString().isEmpty()) {
          status_ = QStringLiteral("Sign-in could not be confirmed. Your existing access was preserved.");
          emit changed(); return;
        }
        email_challenge_id_.clear();
        applyAccount(object);
        // Account identity is separate from editing entitlement. An expired trial
        // can sign in successfully without receiving a new offline lease.
        if (!object.value(QStringLiteral("lease")).isString()) {
          QSettings().remove(leaseKey());
          claims_ = {};
          devices_.clear(); current_installation_id_.clear();
          status_ = QStringLiteral("Signed in. Choose a license or redeem a VIP code to enable editing.");
          emit changed();
          loadAccount();
          return;
        }
      }
      if (object.contains(QStringLiteral("account"))) applyAccount(object);
      if (path == QStringLiteral("/v1/devices")) {
        devices_.clear();
        const auto array = object.value(QStringLiteral("devices")).toArray();
        for (const auto& item : array) if (item.isObject()) devices_.append(item.toObject().toVariantMap());
        current_installation_id_ = object.value(QStringLiteral("current")).toString();
        emit changed();
        loadAccount();
        return;
      }
      if (path == QStringLiteral("/v1/devices/revoke")) {
        if (deactivation_pending_) {
          QString error;
          const bool credential_cleared = credential_store_->clear(&error);
          clearLocalAccess(credential_cleared);
          if (!credential_cleared) {
            status_ = error;
          } else {
            status_ = QStringLiteral("This installation has been deactivated.");
          }
          deactivation_pending_ = false;
          emit changed();
          return;
        }
        status_ = QStringLiteral("Device access has been revoked.");
        emit changed();
        loadDevices();
        return;
      }
      const auto token = object.value(QStringLiteral("lease")).toString().toLatin1();
      applyVerifiedLease(token, replacement_credential.isEmpty() ? credential_ : replacement_credential,
                         !replacement_credential.isEmpty(), replacement_credential.isEmpty() ? QString{} :
                         QStringLiteral("Purchase recovery failed. Your existing installation access was preserved."));
      return;
    }
    const auto code = object.value(QStringLiteral("error")).toString();
    const bool refresh_revoked = path == QStringLiteral("/v1/refresh") && (http == 401 || http == 403);
    const bool auth_revoked = authenticated &&
        (code == QStringLiteral("device_revoked") || code == QStringLiteral("authentication_required") ||
         code == QStringLiteral("account_disabled") || code == QStringLiteral("account_session_expired"));
    if (refresh_revoked || auth_revoked) {
      QSettings().remove(leaseKey()); claims_ = {};
      devices_.clear(); current_installation_id_.clear();
      account_id_.clear(); account_email_.clear(); account_licenses_.clear();
    }
    if (code == QStringLiteral("account_session_required") || code == QStringLiteral("account_sign_in_required") ||
        code == QStringLiteral("account_disabled") || code == QStringLiteral("account_session_expired")) {
      account_id_.clear(); account_email_.clear(); account_licenses_.clear();
    }
    if (code == QStringLiteral("invalid_email_code") || code == QStringLiteral("invalid_code") ||
        code == QStringLiteral("challenge_expired") || code == QStringLiteral("invalid_or_expired_code"))
      status_ = QStringLiteral("That sign-in code is invalid or expired. Request a new code and try again.");
    else if (code == QStringLiteral("sign_out_required")) status_ = QStringLiteral("Sign out before signing in with a different email address.");
    else if (code == QStringLiteral("account_session_expired")) status_ = QStringLiteral("Your account session has expired. Sign in again with an email code.");
    else if (code == QStringLiteral("account_disabled")) status_ = QStringLiteral("This account is disabled. Contact support.");
    else if (code == QStringLiteral("rate_limited")) status_ = QStringLiteral("Too many attempts. Wait a moment before trying again.");
    else if (path == QStringLiteral("/v1/account/email/request")) status_ = QStringLiteral("Sign-in email could not be sent. Check the address and try again.");
    else if (path == QStringLiteral("/v1/account/email/verify")) status_ = QStringLiteral("Sign-in could not be completed. Request a new code and try again.");
    else if (path.startsWith(QStringLiteral("/v1/billing/"))) status_ = QStringLiteral("Billing could not be opened. Sign in and try again when the service is available.");
    else if (path == QStringLiteral("/v1/vip/redeem")) status_ = QStringLiteral("That VIP code could not be redeemed. Check its expiry and usage limit or contact support.");
    else if (path == QStringLiteral("/v1/account")) {
      // Network failure must not change verified offline editing access.
      if (auth_revoked) status_ = QStringLiteral("This session has ended. Sign in again or contact support.");
      emit changed(); return;
    }
    else if (path == QStringLiteral("/v1/devices")) status_ = QStringLiteral("Could not load your device list. Try again when the service is available.");
    else if (code == QStringLiteral("device_limit")) status_ = QStringLiteral("Your device limit is reached. Revoke an old device or contact support.");
    else if (code == QStringLiteral("invalid_license")) status_ = QStringLiteral("That purchase key could not be verified. Check it and try again.");
    else if (http == 401 || http == 403) status_ = QStringLiteral("Access could not be renewed. Check your purchase or contact support.");
    else if (!replacement_credential.isEmpty()) status_ = QStringLiteral("Purchase recovery failed. Your existing installation access was preserved.");
    else status_ = QStringLiteral("The license service is unavailable. Existing offline access remains valid until its expiry.");
    if (path == QStringLiteral("/v1/devices/revoke")) deactivation_pending_ = false;
    emit changed();
    if (path == QStringLiteral("/v1/refresh")) loadAccount();
  });
}

void CustomerAccessController::applyVerifiedLease(const QByteArray& token, const QByteArray& credential,
    bool replace_credential, const QString& failure_status) {
  QSettings settings;
  const auto verified = verifyCustomerLeaseWithTrustedKeys(token, config_.primaryPublicKey, config_.primaryKeyId,
      config_.trustedKeysJson, config_.issuer, credential, QDateTime::currentSecsSinceEpoch(),
      settings.value(clockKey(), 0).toLongLong(), config_.appMajor);
  if (!verified.valid()) {
    status_ = failure_status.isEmpty() ? verified.error : failure_status;
    emit changed();
    return;
  }
  if (replace_credential) {
    QString error;
    if (!credential_store_->save(credential, &error)) {
      status_ = QStringLiteral("Purchase verified, but secure storage could not save access. Keep Looksy open and retry recovery with the same key.");
      emit changed();
      return;
    }
    credential_ = credential;
    pending_recovery_credential_.clear();
    pending_recovery_key_digest_.clear();
    account_id_.clear(); account_email_.clear(); account_licenses_.clear();
    devices_.clear(); current_installation_id_.clear();
  }
  settings.setValue(leaseKey(), token);
  settings.setValue(clockKey(), qMax(settings.value(clockKey(), 0).toLongLong(), QDateTime::currentSecsSinceEpoch()));
  claims_ = verified.claims;
  status_ = QStringLiteral("Access is active. Offline access is available until %1.").arg(expires());
  emit changed();
  loadDevices();
}

void CustomerAccessController::clearLocalAccess(bool clear_credential) {
  if (clear_credential) credential_.clear();
  pending_recovery_credential_.clear(); pending_recovery_key_digest_.clear();
  claims_ = {}; devices_.clear(); current_installation_id_.clear();
  account_id_.clear(); account_email_.clear(); account_licenses_.clear(); email_challenge_id_.clear();
  QSettings settings; settings.remove(leaseKey()); settings.remove(clockKey());
}

void CustomerAccessController::applyAccount(const QJsonObject& response) {
  const auto account = response.value(QStringLiteral("account")).toObject();
  account_id_ = account.value(QStringLiteral("id")).toString();
  account_email_ = account.value(QStringLiteral("email")).toString();
  if (response.contains(QStringLiteral("licenses"))) {
    account_licenses_.clear();
    for (const auto& item : response.value(QStringLiteral("licenses")).toArray())
      if (item.isObject()) account_licenses_.append(item.toObject().toVariantMap());
  }
  if (account_id_.isEmpty()) account_licenses_.clear();
}
void CustomerAccessController::requestEmailSignIn(const QString& email) {
  const auto address = email.trimmed().toLower();
  if (!QRegularExpression(QStringLiteral("^[^\\s@]+@[^\\s@]+\\.[^\\s@]+$")).match(address).hasMatch() || address.size() > 254) {
    status_ = QStringLiteral("Enter a valid email address."); emit changed(); return;
  }
  if (!ensureCredential()) return;
  request(QStringLiteral("/v1/account/email/request"), {{QStringLiteral("email"), address}}, true);
}
void CustomerAccessController::verifyEmailSignIn(const QString& code) {
  const auto value = code.trimmed();
  if (email_challenge_id_.isEmpty() || !QRegularExpression(QStringLiteral("^[0-9]{8}$")).match(value).hasMatch()) {
    status_ = QStringLiteral("Enter the eight-digit code from your email."); emit changed(); return;
  }
  request(QStringLiteral("/v1/account/email/verify"), {{QStringLiteral("challengeId"), email_challenge_id_},
      {QStringLiteral("code"), value}}, true);
}
void CustomerAccessController::cancelEmailSignIn() {
  if (busy_) return;
  email_challenge_id_.clear(); emit changed();
}
void CustomerAccessController::loadAccount() {
  if (!configured() || busy_ || credential_.isEmpty()) return;
  request(QStringLiteral("/v1/account"), {}, true, QByteArrayLiteral("GET"));
}
void CustomerAccessController::selectAccountLicense(const QString& license_id) {
  if (!signedIn() || license_id.trimmed().isEmpty()) return;
  request(QStringLiteral("/v1/account/license"), {{QStringLiteral("licenseId"), license_id.trimmed()}}, true);
}
void CustomerAccessController::redeemVip(const QString& code) {
  if (!signedIn() || code.trimmed().isEmpty()) return;
  request(QStringLiteral("/v1/vip/redeem"), {{QStringLiteral("code"), code.trimmed()}}, true);
}
void CustomerAccessController::startCheckout(const QString& offer) {
  if (!signedIn() || (offer != QStringLiteral("one-time") && offer != QStringLiteral("subscription"))) return;
  request(QStringLiteral("/v1/billing/checkout"), {{QStringLiteral("offer"), offer}}, true);
}
void CustomerAccessController::openBillingPortal() {
  if (!signedIn()) return;
  request(QStringLiteral("/v1/billing/portal"), {}, true);
}
void CustomerAccessController::signOut() {
  if (!configured() || busy_ || credential_.isEmpty()) return;
  request(QStringLiteral("/v1/account/logout"), {}, true);
}
void CustomerAccessController::finishSignOut(bool server_revoked) {
  QString error;
  const bool cleared = credential_store_->clear(&error);
  const auto previous_account_id = account_id_;
  const auto previous_account_email = account_email_;
  clearLocalAccess(cleared);
  if (!cleared) { account_id_ = previous_account_id; account_email_ = previous_account_email; }
  if (!cleared) status_ = QStringLiteral("Sign-out could not remove the secure credential. Unlock secure storage and retry. %1").arg(error);
  else if (!server_revoked) status_ = QStringLiteral("Signed out on this computer. The service could not confirm session revocation.");
  else status_ = QStringLiteral("Signed out. You can still view, save, and export existing work.");
  emit changed();
}
void CustomerAccessController::startTrial() {
  if (!ensureCredential()) return;
  request(QStringLiteral("/v1/trial"), {{QStringLiteral("credential"), QString::fromLatin1(credential_)}}, false);
}
void CustomerAccessController::activate(const QString& license_key) {
  if (license_key.trimmed().isEmpty()) return;
  request(QStringLiteral("/v1/activate"), {{QStringLiteral("licenseKey"), license_key.trimmed()}}, true);
}
void CustomerAccessController::recoverAccess(const QString& license_key) {
  const auto key = license_key.trimmed();
  if (!configured() || busy_ || key.isEmpty()) return;
  const auto key_digest = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256);
  if (pending_recovery_credential_.isEmpty() || pending_recovery_key_digest_ != key_digest) {
    pending_recovery_credential_ = newCredential();
    pending_recovery_key_digest_ = key_digest;
  }
  const auto replacement = pending_recovery_credential_;
  QJsonObject input{{QStringLiteral("licenseKey"), key},
      {QStringLiteral("credential"), QString::fromLatin1(replacement)}};
  if (!credential_.isEmpty()) input.insert(QStringLiteral("previousCredential"), QString::fromLatin1(credential_));
  request(QStringLiteral("/v1/recover"), input, false,
      QByteArrayLiteral("POST"), replacement);
}
void CustomerAccessController::refresh() {
  if (credential_.isEmpty() || busy_) return;
#if defined(Q_OS_MACOS) && defined(Q_PROCESSOR_ARM_64)
  const auto platform = QStringLiteral("mac-arm");
#elif defined(Q_OS_MACOS)
  const auto platform = QStringLiteral("mac-intel");
#elif defined(Q_OS_WIN)
  const auto platform = QStringLiteral("windows");
#else
  const auto platform = QStringLiteral("linux");
#endif
  request(QStringLiteral("/v1/refresh"), {{QStringLiteral("version"), config_.appVersion},
      {QStringLiteral("platform"), platform}}, true);
}
void CustomerAccessController::loadDevices() {
  if (credential_.isEmpty() || busy_) return;
  request(QStringLiteral("/v1/devices"), {}, true, QByteArrayLiteral("GET"));
}
void CustomerAccessController::revokeDevice(const QString& installation_id) {
  const auto id = installation_id.trimmed();
  if (id.isEmpty() || id == current_installation_id_ || busy_ || credential_.isEmpty()) return;
  request(QStringLiteral("/v1/devices/revoke"), {{QStringLiteral("id"), id}}, true);
}
void CustomerAccessController::deactivateCurrentDevice() {
  if (current_installation_id_.isEmpty() || credential_.isEmpty() || busy_) return;
  deactivation_pending_ = true;
  request(QStringLiteral("/v1/devices/revoke"), {{QStringLiteral("id"), current_installation_id_}}, true);
}
void CustomerAccessController::recoverPurchase() const {
  QDesktopServices::openUrl(QUrl(QStringLiteral("https://app.lemonsqueezy.com/my-orders/")));
}
}  // namespace photo_editor::desktop

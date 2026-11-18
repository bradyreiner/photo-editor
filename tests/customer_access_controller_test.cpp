#include "customer_access_controller.h"
#include "customer_credential_store.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>

#include <openssl/evp.h>

#include <iostream>
#include <functional>
#include <memory>

using namespace photo_editor::desktop;
namespace {
class MemoryCredentialStore final : public CustomerCredentialStore {
 public:
  QByteArray credential;
  bool failClear = false;
  bool failSave = false;
  QByteArray load(QString*) override { return credential; }
  bool save(const QByteArray& value, QString* error) override {
    if (failSave) { *error = QStringLiteral("test secure store save failed"); return false; }
    credential = value; return true;
  }
  bool clear(QString* error) override {
    if (failClear) { *error = QStringLiteral("test secure store clear failed"); return false; }
    credential.clear(); return true;
  }
};

bool waitFor(const std::function<bool()>& condition) {
  QElapsedTimer timer; timer.start();
  while (!condition() && timer.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(2);
  }
  return condition();
}

QByteArray urlEncode(const QByteArray& data) {
  return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

class TestSigner final {
 public:
  TestSigner() {
    EVP_PKEY_CTX* context = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    EVP_PKEY* generated = nullptr;
    if (!context || EVP_PKEY_keygen_init(context) != 1 || EVP_PKEY_keygen(context, &generated) != 1) {
      EVP_PKEY_CTX_free(context);
      return;
    }
    key_.reset(generated);
    EVP_PKEY_CTX_free(context);
    unsigned char raw[32]{};
    size_t size = sizeof(raw);
    if (EVP_PKEY_get_raw_public_key(key_.get(), raw, &size) == 1)
      public_key = QByteArray(reinterpret_cast<const char*>(raw), static_cast<qsizetype>(size));
  }
  QByteArray public_key;
  QByteArray lease(const QByteArray& credential, const QString& issuer) const {
    const auto now = QDateTime::currentSecsSinceEpoch();
    QJsonObject header{{QStringLiteral("alg"), QStringLiteral("EdDSA")},
        {QStringLiteral("kid"), QStringLiteral("test-key")}, {QStringLiteral("typ"), QStringLiteral("JWT")}};
    QJsonObject claims;
    claims.insert(QStringLiteral("iss"), issuer);
    claims.insert(QStringLiteral("aud"), QStringLiteral("looksy-desktop"));
    claims.insert(QStringLiteral("sub"), QStringLiteral("customer:controller-test"));
    claims.insert(QStringLiteral("schema"), 1);
    claims.insert(QStringLiteral("device"), QString::fromLatin1(QCryptographicHash::hash(
        credential, QCryptographicHash::Sha256).toHex()));
    claims.insert(QStringLiteral("iat"), static_cast<double>(now - 1));
    claims.insert(QStringLiteral("exp"), static_cast<double>(now + 3600));
    claims.insert(QStringLiteral("mode"), QStringLiteral("subscription"));
    claims.insert(QStringLiteral("major"), 1);
    claims.insert(QStringLiteral("features"), QJsonArray{QStringLiteral("editor")});
    claims.insert(QStringLiteral("plan"), QStringLiteral("Studio"));
    const auto body = urlEncode(QJsonDocument(header).toJson(QJsonDocument::Compact)) + '.' +
        urlEncode(QJsonDocument(claims).toJson(QJsonDocument::Compact));
    EVP_MD_CTX* raw_context = EVP_MD_CTX_new();
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(raw_context, EVP_MD_CTX_free);
    if (!key_ || !context || EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key_.get()) != 1) return {};
    unsigned char signature[64]{};
    size_t signature_size = sizeof(signature);
    if (EVP_DigestSign(context.get(), signature, &signature_size,
            reinterpret_cast<const unsigned char*>(body.constData()), static_cast<size_t>(body.size())) != 1) return {};
    return body + '.' + urlEncode(QByteArray(reinterpret_cast<const char*>(signature), static_cast<qsizetype>(signature_size)));
  }
 private:
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key_{nullptr, EVP_PKEY_free};
};

class FakeService final : public QObject {
 public:
  explicit FakeService(const TestSigner& signer, QString issuer) : signer_(signer), issuer_(std::move(issuer)) {
    QObject::connect(&server_, &QTcpServer::newConnection, this, [this] {
      while (auto* socket = server_.nextPendingConnection()) {
        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
      }
    });
    server_.listen(QHostAddress::LocalHost, 0);
  }
  QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort()); }
  QString issuer() const { return issuer_; }
  void setIssuer(const QString& value) { issuer_ = value; }
  QString errorString() const { return server_.errorString(); }
  QString lastPath;
  QStringList paths;
  QByteArray lastAuthorization;
  QJsonObject lastInput;
  QJsonObject lastRecoveryInput;
  QByteArray lastRecoveryAuthorization;
  QList<QByteArray> recoveryCredentials;
  bool badNextRecovery = false;
  bool recoveryUnavailable = false;
  bool unauthorizedRefresh = false;
  bool expiredRefresh = false;
  bool accountSessionExpired = false;
  bool accountVerified = false;
  bool badVipLease = false;
  bool logoutUnavailable = false;
  bool listening() const { return server_.isListening(); }
 private:
  void read(QTcpSocket* socket) {
    auto& buffer = buffers_[socket];
    buffer += socket->readAll();
    const auto split = buffer.indexOf("\r\n\r\n");
    if (split < 0) return;
    const auto header = buffer.left(split);
    const auto content_length_marker = header.toLower().indexOf("content-length:");
    int body_size = 0;
    if (content_length_marker >= 0) {
      const auto start = content_length_marker + QByteArray("content-length:").size();
      body_size = header.mid(start).trimmed().split('\n').first().trimmed().toInt();
    }
    if (buffer.size() < split + 4 + body_size) return;
    const auto request_header = header.split('\n');
    const auto request_line = request_header.value(0).trimmed().split(' ');
    lastPath = QString::fromLatin1(request_line.value(1));
    paths.append(lastPath);
    lastAuthorization.clear();
    for (const auto& line : request_header) {
      if (line.toLower().startsWith("authorization:")) lastAuthorization = line.mid(line.indexOf(':') + 1).trimmed();
    }
    const auto body = buffer.mid(split + 4, body_size);
    lastInput = QJsonDocument::fromJson(body).object();
    if (lastPath == QStringLiteral("/v1/recover")) {
      lastRecoveryInput = lastInput;
      lastRecoveryAuthorization = lastAuthorization;
      recoveryCredentials.append(lastInput.value(QStringLiteral("credential")).toString().toLatin1());
    }
    int status = 200;
    QJsonObject response;
    if (lastPath == QStringLiteral("/v1/billing/checkout") || lastPath == QStringLiteral("/v1/billing/portal")) {
      response.insert(QStringLiteral("url"), QStringLiteral("https://paddle.com.attacker.test/checkout"));
    } else if (lastPath == QStringLiteral("/v1/account/email/request")) {
      response.insert(QStringLiteral("challengeId"), QStringLiteral("challenge-test"));
    } else if (lastPath == QStringLiteral("/v1/account/email/verify")) {
      if (lastInput.value(QStringLiteral("code")).toString() != QStringLiteral("12345678")) {
        status = 401; response.insert(QStringLiteral("error"), QStringLiteral("invalid_email_code"));
      } else {
        accountVerified = true;
        response.insert(QStringLiteral("account"), QJsonObject{{QStringLiteral("id"), QStringLiteral("account:test")},
            {QStringLiteral("email"), QStringLiteral("owner@example.test")}});
        response.insert(QStringLiteral("accessError"), QStringLiteral("trial_expired"));
      }
    } else if (lastPath == QStringLiteral("/v1/account") && accountSessionExpired) {
      status = 401; response.insert(QStringLiteral("error"), QStringLiteral("account_session_expired"));
    } else if (lastPath == QStringLiteral("/v1/account")) {
      response.insert(QStringLiteral("account"), accountVerified
          ? QJsonValue(QJsonObject{{QStringLiteral("id"), QStringLiteral("account:test")},
              {QStringLiteral("email"), QStringLiteral("owner@example.test")}}) : QJsonValue(QJsonValue::Null));
      response.insert(QStringLiteral("licenses"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("vip:test")},
          {QStringLiteral("plan"), QStringLiteral("VIP")}}});
    } else if (lastPath == QStringLiteral("/v1/vip/redeem") || lastPath == QStringLiteral("/v1/account/license")) {
      const auto credential = lastAuthorization.mid(QByteArrayLiteral("Bearer ").size());
      response.insert(QStringLiteral("lease"), badVipLease ? QStringLiteral("bad.lease.signature")
          : QString::fromLatin1(signer_.lease(credential, issuer_)));
    } else if (lastPath == QStringLiteral("/v1/account/logout")) {
      if (logoutUnavailable) { status = 503; response.insert(QStringLiteral("error"), QStringLiteral("service_unavailable")); }
      else { accountVerified = false; response.insert(QStringLiteral("ok"), true); }
    } else if (lastPath == QStringLiteral("/v1/recover")) {
      if (recoveryUnavailable) {
        status = 503;
        response.insert(QStringLiteral("error"), QStringLiteral("service_unavailable"));
      } else if (badNextRecovery) {
        badNextRecovery = false;
        response.insert(QStringLiteral("lease"), QStringLiteral("not.a.signed-lease"));
      } else {
        response.insert(QStringLiteral("lease"), QString::fromLatin1(signer_.lease(
            lastInput.value(QStringLiteral("credential")).toString().toLatin1(), issuer_)));
      }
    } else if (lastPath == QStringLiteral("/v1/refresh")) {
      if (accountSessionExpired) {
        status = 401; response.insert(QStringLiteral("error"), QStringLiteral("account_session_expired"));
      } else if (expiredRefresh) {
        status = 403; response.insert(QStringLiteral("error"), QStringLiteral("trial_expired"));
      } else if (unauthorizedRefresh) {
        status = 401;
        response.insert(QStringLiteral("error"), QStringLiteral("device_revoked"));
      } else {
        const auto credential = lastAuthorization.mid(QByteArrayLiteral("Bearer ").size());
        response.insert(QStringLiteral("lease"), QString::fromLatin1(signer_.lease(credential, issuer_)));
      }
    } else if (lastPath == QStringLiteral("/v1/activate")) {
      status = 403;
      response.insert(QStringLiteral("error"), QStringLiteral("invalid_license"));
    } else if (lastPath == QStringLiteral("/v1/devices")) {
      response.insert(QStringLiteral("devices"), QJsonArray{
          QJsonObject{{QStringLiteral("id"), QStringLiteral("device-current")}, {QStringLiteral("platform"), QStringLiteral("mac-arm")}, {QStringLiteral("revoked"), false}},
          QJsonObject{{QStringLiteral("id"), QStringLiteral("device-old")}, {QStringLiteral("platform"), QStringLiteral("windows")}, {QStringLiteral("revoked"), false}}});
      response.insert(QStringLiteral("current"), QStringLiteral("device-current"));
    } else if (lastPath == QStringLiteral("/v1/devices/revoke")) {
      response.insert(QStringLiteral("ok"), true);
    } else {
      status = 404;
      response.insert(QStringLiteral("error"), QStringLiteral("not_found"));
    }
    const auto json = QJsonDocument(response).toJson(QJsonDocument::Compact);
    socket->write("HTTP/1.1 " + QByteArray::number(status) + " OK\r\nContent-Type: application/json\r\nContent-Length: " +
        QByteArray::number(json.size()) + "\r\nConnection: close\r\n\r\n" + json);
    socket->disconnectFromHost();
    buffers_.remove(socket);
  }
  const TestSigner& signer_;
  QString issuer_;
  QTcpServer server_;
  QHash<QTcpSocket*, QByteArray> buffers_;
};
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir settings_dir;
  if (!settings_dir.isValid()) { std::cerr << "settings fixture setup failed\n"; return 1; }
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyControllerTest"));
  QCoreApplication::setApplicationName(QStringLiteral("CustomerAccess"));
  QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settings_dir.path());
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());

  TestSigner signer;
  if (signer.public_key.size() != 32) { std::cerr << "test signing key setup failed\n"; return 1; }
  FakeService service(signer, {});
  if (!service.listening()) { std::cerr << "fake service listen failed: " << service.errorString().toStdString() << '\n'; return 1; }
  service.setIssuer(service.baseUrl());
  CustomerAccessConfig config;
  config.apiUrl = service.baseUrl(); config.issuer = service.issuer(); config.primaryPublicKey = signer.public_key;
  config.primaryKeyId = QStringLiteral("test-key"); config.appVersion = QStringLiteral("0.1.0"); config.appMajor = 1;
  config.allowInsecureLoopbackForTests = true;
  const auto scope = customerCredentialScope(config.issuer);
  const auto lease_key = QStringLiteral("customerAccess/%1/lease").arg(scope);
  const auto clock_key = QStringLiteral("customerAccess/%1/lastObservedTime").arg(scope);
  if (scope == customerCredentialScope(QStringLiteral("https://other.example.test/api")) ||
      customerCredentialScope(QStringLiteral("https://EXAMPLE.test:443/api/")) !=
          customerCredentialScope(QStringLiteral("https://example.test/api")) ||
      customerCredentialScope({}) != QStringLiteral("installation-v1") ||
      &systemCustomerCredentialStore(QStringLiteral("https://stage.example.test")) ==
          &systemCustomerCredentialStore(QStringLiteral("https://production.example.test"))) {
    std::cerr << "issuer credential namespaces are not isolated or canonical\n"; return 1;
  }
  MemoryCredentialStore credentials;
  CustomerAccessController controller(config, &credentials);
  if (!controller.configured()) { std::cerr << "injected service config rejected\n"; return 1; }
  controller.recoverAccess(QStringLiteral("  LS-TEST-KEY  "));
  if (!waitFor([&] { return !controller.busy() && controller.active() && controller.currentInstallationId() == QStringLiteral("device-current"); })) {
    std::cerr << "recovery/list timeout: " << controller.status().toStdString() << " path="
              << service.lastPath.toStdString() << " devices=" << controller.devices().size() << '\n'; return 1;
  }
  if (service.lastPath != QStringLiteral("/v1/account") || credentials.credential.isEmpty() ||
      controller.customerId() != QStringLiteral("customer:controller-test") || controller.devices().size() != 2) {
    std::cerr << "recovery assertions failed: path=" << service.lastPath.toStdString() << " devices="
              << controller.devices().size() << " customer=" << controller.customerId().toStdString() << '\n'; return 1;
  }
  auto active_credential = credentials.credential;
  {
    auto other_config = config;
    other_config.issuer = QStringLiteral("https://other.example.test/api");
    CustomerAccessController other(other_config, &credentials);
    if (other.active() || !other.plan().isEmpty()) {
      std::cerr << "another issuer loaded the current issuer's cached lease\n"; return 1;
    }
  }
  if (service.lastRecoveryInput.value(QStringLiteral("licenseKey")).toString() != QStringLiteral("LS-TEST-KEY") ||
      service.lastRecoveryInput.value(QStringLiteral("credential")).toString().toLatin1() != active_credential ||
      !service.lastRecoveryAuthorization.isEmpty() || service.paths.count(QStringLiteral("/v1/recover")) != 1) {
    std::cerr << "recovery payload assertions failed\n"; return 1;
  }
  auto last_observed_time = QSettings().value(clock_key).toLongLong();
  if (last_observed_time <= 0) { std::cerr << "verified lease clock was not persisted\n"; return 1; }
  controller.activate(QStringLiteral("WRONG-KEY"));
  if (!waitFor([&] { return !controller.busy() && service.lastPath == QStringLiteral("/v1/activate"); })) {
    std::cerr << "invalid-key activation timeout\n"; return 1;
  }
  if (!controller.active() || controller.currentInstallationId() != QStringLiteral("device-current") ||
      controller.devices().size() != 2 || QSettings().value(lease_key).toByteArray().isEmpty()) {
    std::cerr << "invalid-key activation cleared valid access\n"; return 1;
  }

  auto cached_lease = QSettings().value(lease_key).toByteArray();
  const auto clock_before_failed_recovery = QSettings().value(clock_key).toLongLong();
  credentials.failSave = true;
  controller.recoverAccess(QStringLiteral("LS-TEST-KEY"));
  if (!waitFor([&] { return !controller.busy() && controller.status().contains(QStringLiteral("Keep Looksy open")); })) {
    std::cerr << "keychain-save recovery timeout\n"; return 1;
  }
  const auto save_retry_credential = service.lastRecoveryInput.value(QStringLiteral("credential")).toString().toLatin1();
  if (credentials.credential != active_credential || save_retry_credential.isEmpty() || save_retry_credential == active_credential ||
      QSettings().value(lease_key).toByteArray() != cached_lease ||
      QSettings().value(clock_key).toLongLong() != clock_before_failed_recovery ||
      !controller.active() || controller.currentInstallationId() != QStringLiteral("device-current") || controller.devices().size() != 2) {
    std::cerr << "successful server recovery with failed secure save changed existing access\n"; return 1;
  }
  credentials.failSave = false;
  controller.recoverAccess(QStringLiteral("LS-TEST-KEY"));
  if (!waitFor([&] { return !controller.busy() && credentials.credential == save_retry_credential &&
      controller.currentInstallationId() == QStringLiteral("device-current"); })) {
    std::cerr << "same-credential secure-save retry timeout\n"; return 1;
  }
  if (service.lastRecoveryInput.value(QStringLiteral("credential")).toString().toLatin1() != save_retry_credential ||
      service.recoveryCredentials.value(service.recoveryCredentials.size() - 1) != save_retry_credential) {
    std::cerr << "secure-save retry generated a different credential\n"; return 1;
  }
  active_credential = credentials.credential;
  cached_lease = QSettings().value(lease_key).toByteArray();
  last_observed_time = QSettings().value(clock_key).toLongLong();

  service.recoveryUnavailable = true;
  controller.recoverAccess(QStringLiteral("LS-TEST-KEY"));
  if (!waitFor([&] { return !controller.busy() && controller.status().contains(QStringLiteral("preserved")); })) {
    std::cerr << "recovery outage timeout\n"; return 1;
  }
  const auto outage_retry_credential = service.lastRecoveryInput.value(QStringLiteral("credential")).toString().toLatin1();
  service.recoveryUnavailable = false;
  if (credentials.credential != active_credential || outage_retry_credential.isEmpty() ||
      QSettings().value(lease_key).toByteArray() != cached_lease ||
      !controller.active() || controller.currentInstallationId() != QStringLiteral("device-current") || controller.devices().size() != 2) {
    std::cerr << "recovery outage changed existing access\n"; return 1;
  }
  controller.recoverAccess(QStringLiteral("LS-TEST-KEY"));
  if (!waitFor([&] { return !controller.busy() && credentials.credential == outage_retry_credential &&
      controller.currentInstallationId() == QStringLiteral("device-current"); })) {
    std::cerr << "same-credential outage retry timeout\n"; return 1;
  }
  if (service.lastRecoveryInput.value(QStringLiteral("credential")).toString().toLatin1() != outage_retry_credential) {
    std::cerr << "outage retry generated a different credential\n"; return 1;
  }
  active_credential = credentials.credential;
  cached_lease = QSettings().value(lease_key).toByteArray();
  last_observed_time = QSettings().value(clock_key).toLongLong();

  controller.revokeDevice(QStringLiteral("device-old"));
  if (!waitFor([&] { return !controller.busy() && service.paths.count(QStringLiteral("/v1/devices/revoke")) == 1 &&
      service.paths.count(QStringLiteral("/v1/devices")) >= 2; })) { std::cerr << "revoke/list timeout\n"; return 1; }
  if (service.lastAuthorization != "Bearer " + active_credential) { std::cerr << "revoke authorization mismatch\n"; return 1; }
  service.unauthorizedRefresh = true;
  controller.refresh();
  if (!waitFor([&] { return !controller.busy() && controller.currentInstallationId().isEmpty() &&
      controller.devices().isEmpty(); })) { std::cerr << "revoked refresh did not clear devices\n"; return 1; }
  if (controller.active() || !QSettings().value(lease_key).toByteArray().isEmpty()) {
    std::cerr << "revoked refresh retained lease state\n"; return 1;
  }
  service.unauthorizedRefresh = false;
  controller.refresh();
  if (!waitFor([&] { return !controller.busy() && controller.active() &&
      controller.currentInstallationId() == QStringLiteral("device-current") && controller.devices().size() == 2; })) {
    std::cerr << "refresh did not restore device state\n"; return 1;
  }
  // A successful refresh can advance the monotonic clock. Snapshot immediately
  // before the failed recovery so only that operation is required to preserve it.
  last_observed_time = QSettings().value(clock_key).toLongLong();
  cached_lease = QSettings().value(lease_key).toByteArray();
  service.badNextRecovery = true;
  controller.recoverAccess(QStringLiteral("LS-TEST-KEY"));
  if (!waitFor([&] { return !controller.busy() && controller.status().contains(QStringLiteral("preserved")); })) {
    std::cerr << "bad recovery timeout: " << controller.status().toStdString() << '\n'; return 1;
  }
  if (credentials.credential != active_credential || !controller.active() ||
      QSettings().value(lease_key).toByteArray() != cached_lease ||
      QSettings().value(clock_key).toLongLong() != last_observed_time) {
    std::cerr << "failed recovery changed access state: clock before=" << last_observed_time
              << " after=" << QSettings().value(clock_key).toLongLong()
              << " active=" << controller.active()
              << " credential_unchanged=" << (credentials.credential == active_credential)
              << " lease_unchanged=" << (QSettings().value(lease_key).toByteArray() == cached_lease) << '\n'; return 1;
  }

  credentials.failClear = true;
  controller.deactivateCurrentDevice();
  if (!waitFor([&] { return !controller.busy() && controller.status().contains(QStringLiteral("test secure store clear failed")); })) {
    std::cerr << "deactivation timeout\n"; return 1;
  }
  if (service.lastPath != QStringLiteral("/v1/devices/revoke")) { std::cerr << "deactivation route mismatch\n"; return 1; }
  if (credentials.credential != active_credential || !controller.currentInstallationId().isEmpty() || controller.active() ||
      !QSettings().value(lease_key).toByteArray().isEmpty()) {
    std::cerr << "revoked local cache was not cleared safely\n"; return 1;
  }
  credentials.failClear = false;
  controller.refresh();
  if (!waitFor([&] { return !controller.busy() && controller.active(); })) {
    std::cerr << "pre-account lease fixture failed\n"; return 1;
  }
  controller.requestEmailSignIn(QStringLiteral(" Owner@Example.Test "));
  if (!waitFor([&] { return !controller.busy() && controller.emailCodePending(); }) ||
      service.lastInput.value(QStringLiteral("email")).toString() != QStringLiteral("owner@example.test") ||
      service.lastAuthorization != "Bearer " + active_credential) {
    std::cerr << "email challenge request did not use normalized address and stored bearer\n"; return 1;
  }
  controller.verifyEmailSignIn(QStringLiteral("00000000"));
  if (!waitFor([&] { return !controller.busy(); }) || controller.signedIn() || !controller.active() || !controller.emailCodePending()) {
    std::cerr << "invalid email code granted account or access\n"; return 1;
  }
  controller.verifyEmailSignIn(QStringLiteral("12345678"));
  if (!waitFor([&] { return !controller.busy() && controller.signedIn(); }) || controller.active() ||
      controller.emailCodePending() || controller.accountEmail() != QStringLiteral("owner@example.test") ||
      !QSettings().value(lease_key).toByteArray().isEmpty() ||
      controller.accountLicenses().size() != 1 || !controller.allows(QStringLiteral("save"))) {
    std::cerr << "email identity without lease changed expired-trial editing rights\n"; return 1;
  }
  controller.startCheckout(QStringLiteral("one-time"));
  if (!waitFor([&] { return !controller.busy(); }) || service.lastPath != QStringLiteral("/v1/billing/checkout") ||
      service.lastInput.value(QStringLiteral("offer")).toString() != QStringLiteral("one-time") ||
      service.lastAuthorization != "Bearer " + active_credential ||
      !controller.status().contains(QStringLiteral("secure billing page"))) {
    std::cerr << "checkout did not authenticate or reject an untrusted billing URL\n"; return 1;
  }
  controller.openBillingPortal();
  if (!waitFor([&] { return !controller.busy(); }) || service.lastPath != QStringLiteral("/v1/billing/portal") ||
      !controller.status().contains(QStringLiteral("secure billing page"))) {
    std::cerr << "portal did not reject an untrusted billing URL\n"; return 1;
  }
  service.expiredRefresh = true;
  controller.refresh();
  if (!waitFor([&] { return !controller.busy() && controller.signedIn(); }) || controller.active() || controller.accountLicenses().size() != 1) {
    std::cerr << "expired editing lease hid the recoverable account session\n"; return 1;
  }
  service.expiredRefresh = false;
  service.badVipLease = true;
  controller.redeemVip(QStringLiteral("synthetic-vip-code"));
  if (!waitFor([&] { return !controller.busy(); }) || controller.active()) {
    std::cerr << "invalid VIP signature granted access\n"; return 1;
  }
  service.badVipLease = false;
  controller.redeemVip(QStringLiteral("synthetic-vip-code"));
  if (!waitFor([&] { return !controller.busy() && controller.active(); }) || !controller.signedIn()) {
    std::cerr << "verified VIP lease was not admitted\n"; return 1;
  }
  controller.selectAccountLicense(QStringLiteral("vip:test"));
  if (!waitFor([&] { return !controller.busy() && controller.active(); }) ||
      !service.paths.contains(QStringLiteral("/v1/account/license"))) {
    std::cerr << "linked account license selection failed\n"; return 1;
  }
  service.accountSessionExpired = true;
  controller.refresh();
  if (!waitFor([&] { return !controller.busy(); }) || controller.active() || controller.signedIn() ||
      credentials.credential != active_credential || !controller.canSignOut()) {
    std::cerr << "expired account session retained access or lost the reauthentication bearer\n"; return 1;
  }
  controller.requestEmailSignIn(QStringLiteral("owner@example.test"));
  if (!waitFor([&] { return !controller.busy() && controller.emailCodePending(); }) ||
      service.lastAuthorization != "Bearer " + active_credential) {
    std::cerr << "expired account session could not request email reauthentication\n"; return 1;
  }
  service.accountSessionExpired = false;
  controller.verifyEmailSignIn(QStringLiteral("12345678"));
  if (!waitFor([&] { return !controller.busy() && controller.signedIn(); })) {
    std::cerr << "expired account session reauthentication failed\n"; return 1;
  }
  credentials.failClear = true;
  controller.signOut();
  if (!waitFor([&] { return !controller.busy(); }) || controller.active() || credentials.credential.isEmpty() ||
      !controller.status().contains(QStringLiteral("retry"))) {
    std::cerr << "secure-store failure was presented as completed sign-out\n"; return 1;
  }
  credentials.failClear = false;
  service.logoutUnavailable = true;
  controller.signOut();
  if (!waitFor([&] { return !controller.busy(); }) || controller.active() || controller.signedIn() ||
      !credentials.credential.isEmpty() || !controller.accountLicenses().isEmpty() ||
      !QSettings().value(lease_key).toByteArray().isEmpty() ||
      !controller.allows(QStringLiteral("view")) || !controller.allows(QStringLiteral("save")) ||
      !controller.allows(QStringLiteral("export")) || !controller.status().contains(QStringLiteral("could not confirm"))) {
    std::cerr << "offline sign-out retained usable local access or blocked existing work\n"; return 1;
  }
  std::cout << "Verified purchase recovery, email account identity, VIP signatures, secure storage failure and offline sign-out preserve access boundaries.\n";
}

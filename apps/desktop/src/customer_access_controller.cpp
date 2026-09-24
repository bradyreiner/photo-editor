#include "customer_access_controller.h"
#include "customer_credential_store.h"
#include "customer_lease.h"
#include "customer_platform_config.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QSettings>
#include <QUrl>

namespace photo_editor::desktop {
namespace {
constexpr auto lease_key = "customerAccess/lease";
constexpr auto clock_key = "customerAccess/lastObservedTime";
}

CustomerAccessController::CustomerAccessController(QObject* parent) : QObject(parent), network_(this) {
  if (!configured()) {
    status_ = QByteArray(LOOKSY_CUSTOMER_API_URL).isEmpty()
        ? QStringLiteral("This version uses receipt-based ownership. Keep your purchase receipt.")
        : QStringLiteral("The license service configuration is invalid. Viewing, saving, and exporting remain available. Contact support.");
    return;
  }
  credential_ = loadCustomerCredential(&status_);
  if (!credential_.isEmpty()) loadLease();
  else if (status_.isEmpty()) status_ = QStringLiteral("Start a guest trial or activate your purchase.");
  expiry_timer_.setInterval(60000);
  connect(&expiry_timer_, &QTimer::timeout, this, &CustomerAccessController::loadLease);
  expiry_timer_.start();
  refresh_timer_.setInterval(6 * 60 * 60 * 1000);
  connect(&refresh_timer_, &QTimer::timeout, this, &CustomerAccessController::refresh);
  refresh_timer_.start();
  if (!credential_.isEmpty()) QTimer::singleShot(0, this, &CustomerAccessController::refresh);
}

bool CustomerAccessController::configured() const {
  const QUrl url(QString::fromUtf8(LOOKSY_CUSTOMER_API_URL));
  return url.isValid() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty() &&
      url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment() &&
      QByteArray::fromBase64(LOOKSY_ENTITLEMENT_PUBLIC_KEY).size() == 32;
}
QString CustomerAccessController::plan() const { return claims_.value(QStringLiteral("plan")).toString(); }
QString CustomerAccessController::customerId() const { return claims_.value(QStringLiteral("sub")).toString(); }
QString CustomerAccessController::expires() const {
  if (claims_.isEmpty()) return {};
  return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(claims_.value(QStringLiteral("exp")).toDouble()))
      .toLocalTime().toString(QStringLiteral("MMM d, yyyy h:mm AP"));
}
bool CustomerAccessController::allows(const QString& capability) const {
  // Only an intentionally unconfigured receipt-only build bypasses licensing.
  // A malformed key/URL in an enabled build must not unlock editing.
  if (QByteArray(LOOKSY_CUSTOMER_API_URL).isEmpty()) return true;
  if (capability == QStringLiteral("view") || capability == QStringLiteral("save") || capability == QStringLiteral("export")) return true;
  if (!configured()) return false;
  if (claims_.value(QStringLiteral("exp")).toDouble() <= static_cast<double>(QDateTime::currentSecsSinceEpoch())) return false;
  return claims_.value(QStringLiteral("features")).toArray().contains(capability);
}
bool CustomerAccessController::ensureCredential() {
  if (!configured() || busy_) return false;
  if (!credential_.isEmpty()) return true;
  QString error;
  credential_ = loadCustomerCredential(&error);
  if (!error.isEmpty()) { status_ = error; emit changed(); return false; }
  if (!credential_.isEmpty()) return true;
  QByteArray random(32, Qt::Uninitialized);
  for (int offset = 0; offset < random.size(); offset += 4) {
    const auto value = QRandomGenerator::system()->generate();
    for (int byte = 0; byte < 4; ++byte) random[offset + byte] = static_cast<char>((value >> (byte * 8)) & 0xffU);
  }
  credential_ = random.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
  if (!saveCustomerCredential(credential_, &error)) {
    credential_.clear(); status_ = error; emit changed(); return false;
  }
  return true;
}
void CustomerAccessController::loadLease() {
  if (!configured() || credential_.isEmpty()) return;
  QSettings settings;
  const auto result = verifyCustomerLease(settings.value(lease_key).toByteArray(),
      QByteArray::fromBase64(LOOKSY_ENTITLEMENT_PUBLIC_KEY), QString::fromUtf8(LOOKSY_ENTITLEMENT_KEY_ID),
      QString::fromUtf8(LOOKSY_CUSTOMER_API_URL), credential_, QDateTime::currentSecsSinceEpoch(),
      settings.value(clock_key, 0).toLongLong(), LOOKSY_APP_MAJOR);
  claims_ = result.claims;
  if (result.valid()) {
    settings.setValue(clock_key, qMax(settings.value(clock_key, 0).toLongLong(), QDateTime::currentSecsSinceEpoch()));
    status_ = QStringLiteral("Access is active. Offline access is available until %1.").arg(expires());
  } else status_ = result.error;
  emit changed();
}
void CustomerAccessController::request(const QString& path, const QJsonObject& input, bool authenticated) {
  if (!ensureCredential()) return;
  busy_ = true; status_ = QStringLiteral("Contacting the license service…"); emit changed();
  QNetworkRequest request(QUrl(QString::fromUtf8(LOOKSY_CUSTOMER_API_URL) + path));
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(15000);
  if (authenticated) request.setRawHeader("Authorization", "Bearer " + credential_);
  auto* reply = network_.post(request, QJsonDocument(input).toJson(QJsonDocument::Compact));
  reply->setReadBufferSize(32769);
  connect(reply, &QNetworkReply::readyRead, reply, [reply] {
    if (reply->bytesAvailable() > 32768) reply->abort();
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, path] {
    busy_ = false;
    const auto http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto data = reply->read(32768); reply->deleteLater();
    const auto object = QJsonDocument::fromJson(data).object();
    if (http == 200 && reply->error() == QNetworkReply::NoError) {
      const auto token = object.value(QStringLiteral("lease")).toString().toLatin1();
      QSettings settings;
      const auto verified = verifyCustomerLease(token, QByteArray::fromBase64(LOOKSY_ENTITLEMENT_PUBLIC_KEY),
          QString::fromUtf8(LOOKSY_ENTITLEMENT_KEY_ID), QString::fromUtf8(LOOKSY_CUSTOMER_API_URL), credential_,
          QDateTime::currentSecsSinceEpoch(), settings.value(clock_key, 0).toLongLong(), LOOKSY_APP_MAJOR);
      if (verified.valid()) { settings.setValue(lease_key, token); loadLease(); return; }
      status_ = verified.error;
    } else {
      // Authoritative revocation clears access; outages preserve the signed cache.
      if (path == QStringLiteral("/v1/refresh") && (http == 401 || http == 403)) {
        QSettings().remove(lease_key); claims_ = {};
      }
      const auto code = object.value(QStringLiteral("error")).toString();
      if (code == QStringLiteral("device_limit")) status_ = QStringLiteral("Your device limit is reached. Contact support to release an old device.");
      else if (http == 401 || http == 403) status_ = QStringLiteral("Access could not be renewed. Check your purchase or contact support.");
      else status_ = QStringLiteral("The license service is unavailable. Existing offline access remains valid until its expiry.");
    }
    emit changed();
  });
}
void CustomerAccessController::startTrial() {
  if (!ensureCredential()) return;
  request(QStringLiteral("/v1/trial"), {{QStringLiteral("credential"), QString::fromLatin1(credential_)}}, false);
}
void CustomerAccessController::activate(const QString& license_key) {
  if (license_key.trimmed().isEmpty()) return;
  // The service can register a new installation directly from a valid purchase.
  request(QStringLiteral("/v1/activate"), {{QStringLiteral("licenseKey"), license_key.trimmed()}}, true);
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
  request(QStringLiteral("/v1/refresh"), {{QStringLiteral("version"), QString::fromUtf8(LOOKSY_APP_VERSION)},
      {QStringLiteral("platform"), platform}}, true);
}
void CustomerAccessController::recoverPurchase() const {
  QDesktopServices::openUrl(QUrl(QStringLiteral("https://app.lemonsqueezy.com/my-orders/")));
}
}  // namespace photo_editor::desktop

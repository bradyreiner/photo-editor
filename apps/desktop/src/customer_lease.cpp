#include "customer_lease.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>
#include <memory>

#if LOOKSY_HAS_ENTITLEMENT_CRYPTO
#include <openssl/evp.h>
#endif

namespace photo_editor::desktop {
namespace {
bool readTrustedKeys(const QByteArray& primary_public_key,
    const QString& primary_key_id, const QByteArray& json, QJsonObject* output) {
  static const QRegularExpression key_id_pattern(QStringLiteral("^[A-Za-z0-9_-]{1,80}$"));
  static const QRegularExpression key_pattern(QStringLiteral("^[A-Za-z0-9+/]{43}=$"));
  if (primary_public_key.size() != 32 || !key_id_pattern.match(primary_key_id).hasMatch() || json.size() > 8192) return false;
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(json.isEmpty() ? QByteArray("{}") : json, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
  auto keys = document.object();
  if (keys.size() > 4) return false;
  for (auto it = keys.constBegin(); it != keys.constEnd(); ++it) {
    if (!key_id_pattern.match(it.key()).hasMatch() || !it.value().isString() ||
        !key_pattern.match(it.value().toString()).hasMatch()) return false;
    const auto encoded = it.value().toString().toLatin1();
    const auto decoded = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (decoded.size() != 32 || decoded.toBase64() != encoded) return false;
    if (it.key() == primary_key_id && decoded != primary_public_key) return false;
  }
  keys.insert(primary_key_id, QString::fromLatin1(primary_public_key.toBase64()));
  *output = keys;
  return true;
}
}  // namespace

bool validCustomerKeyConfiguration(const QByteArray& primary_public_key,
    const QString& primary_key_id, const QByteArray& trusted_keys_json) {
  QJsonObject keys;
  return readTrustedKeys(primary_public_key, primary_key_id, trusted_keys_json, &keys);
}

CustomerLeaseResult verifyCustomerLeaseWithTrustedKeys(
    const QByteArray& token, const QByteArray& primary_public_key,
    const QString& primary_key_id, const QByteArray& trusted_keys_json,
    const QString& issuer, const QByteArray& credential, qint64 now,
    qint64 last_observed_time, int app_major) {
  QJsonObject keys;
  if (!readTrustedKeys(primary_public_key, primary_key_id, trusted_keys_json, &keys)) {
    return {{}, QStringLiteral("Invalid license key configuration.")};
  }
  if (token.size() > 16384) return {{}, QStringLiteral("Invalid access record.")};
  const auto parts = token.split('.');
  if (parts.size() != 3) return {{}, QStringLiteral("Invalid access record.")};
  const auto header = QJsonDocument::fromJson(QByteArray::fromBase64(parts[0],
      QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors)).object();
  const auto kid = header.value(QStringLiteral("kid")).toString();
  if (!keys.contains(kid)) return {{}, QStringLiteral("Unsupported access signature.")};
  return verifyCustomerLease(token, QByteArray::fromBase64(keys.value(kid).toString().toLatin1()),
      kid, issuer, credential, now, last_observed_time, app_major);
}

CustomerLeaseResult verifyCustomerLease(
    const QByteArray& token, const QByteArray& public_key, const QString& key_id,
    const QString& issuer, const QByteArray& credential, qint64 now,
    qint64 last_observed_time, int app_major) {
  const auto fail = [](const QString& message) { return CustomerLeaseResult{{}, message}; };
  if (token.size() > 16384 || public_key.size() != 32 || issuer.isEmpty() || key_id.isEmpty()) {
    return fail(QStringLiteral("Invalid license configuration."));
  }
  const auto parts = token.split('.');
  if (parts.size() != 3) return fail(QStringLiteral("Invalid access record."));
  static const QRegularExpression encoding(QStringLiteral("^[A-Za-z0-9_-]+$"));
  for (const auto& part : parts) {
    if (!encoding.match(QString::fromLatin1(part)).hasMatch()) return fail(QStringLiteral("Invalid access record."));
  }
  const auto decode = [](const QByteArray& value) {
    return QByteArray::fromBase64(value, QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
  };
  const auto header = QJsonDocument::fromJson(decode(parts[0])).object();
  if (header.value(QStringLiteral("alg")).toString() != QStringLiteral("EdDSA") ||
      header.value(QStringLiteral("kid")).toString() != key_id ||
      header.value(QStringLiteral("typ")).toString() != QStringLiteral("JWT") ||
      header.contains(QStringLiteral("crit"))) return fail(QStringLiteral("Unsupported access signature."));
  const auto signature = decode(parts[2]);
  if (signature.size() != 64) return fail(QStringLiteral("Invalid access signature."));
#if LOOKSY_HAS_ENTITLEMENT_CRYPTO
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
      EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
          reinterpret_cast<const unsigned char*>(public_key.constData()),
          static_cast<size_t>(public_key.size())), EVP_PKEY_free);
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  const auto message = parts[0] + '.' + parts[1];
  if (!key || !context || EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) != 1 ||
      EVP_DigestVerify(context.get(), reinterpret_cast<const unsigned char*>(signature.constData()),
          static_cast<size_t>(signature.size()), reinterpret_cast<const unsigned char*>(message.constData()),
          static_cast<size_t>(message.size())) != 1) return fail(QStringLiteral("Access signature could not be verified."));
#else
  return fail(QStringLiteral("License verification is unavailable in this build."));
#endif
  const auto claims = QJsonDocument::fromJson(decode(parts[1])).object();
  const auto issued = claims.value(QStringLiteral("iat")).toDouble(-1);
  const auto expires = claims.value(QStringLiteral("exp")).toDouble(-1);
  if (!std::isfinite(issued) || !std::isfinite(expires) || std::floor(issued) != issued ||
      std::floor(expires) != expires || issued < 0 || expires <= issued ||
      issued > static_cast<double>(now + 300) || now + 300 < last_observed_time) {
    return fail(QStringLiteral("Check the system date and refresh your license."));
  }
  if (expires <= static_cast<double>(now)) return fail(QStringLiteral("Access has expired. Refresh your license or activate a purchase."));
  const auto digest = QCryptographicHash::hash(credential, QCryptographicHash::Sha256).toHex();
  if (claims.value(QStringLiteral("iss")).toString() != issuer ||
      claims.value(QStringLiteral("aud")).toString() != QStringLiteral("looksy-desktop") ||
      claims.value(QStringLiteral("device")).toString().toLatin1() != digest ||
      claims.value(QStringLiteral("schema")).toInt() != 1 ||
      claims.value(QStringLiteral("sub")).toString().isEmpty()) return fail(QStringLiteral("Access record does not belong to this installation."));
  const auto mode = claims.value(QStringLiteral("mode")).toString();
  if (mode != QStringLiteral("trial") && mode != QStringLiteral("perpetual") && mode != QStringLiteral("subscription")) {
    return fail(QStringLiteral("Unsupported license type."));
  }
  if (mode == QStringLiteral("perpetual") && claims.value(QStringLiteral("major")).toInt(-1) != app_major) {
    return fail(QStringLiteral("This purchase covers a different major version."));
  }
  if (!claims.value(QStringLiteral("features")).isArray()) return fail(QStringLiteral("Invalid feature permissions."));
  for (const auto& feature : claims.value(QStringLiteral("features")).toArray()) {
    if (!feature.isString() || !QStringList{QStringLiteral("editor"), QStringLiteral("agent"), QStringLiteral("beta")}.contains(feature.toString())) {
      return fail(QStringLiteral("Unsupported feature permission."));
    }
  }
  return {claims, {}};
}

}  // namespace photo_editor::desktop

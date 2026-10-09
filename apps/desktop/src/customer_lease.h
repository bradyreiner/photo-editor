#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace photo_editor::desktop {

struct CustomerLeaseResult {
  QJsonObject claims;
  QString error;
  [[nodiscard]] bool valid() const { return error.isEmpty(); }
};

// Only the pinned key can authorize capabilities. Never trust decoded JWT claims.
[[nodiscard]] CustomerLeaseResult verifyCustomerLease(
    const QByteArray& token, const QByteArray& public_key, const QString& key_id,
    const QString& issuer, const QByteArray& credential, qint64 now,
    qint64 last_observed_time, int app_major);

// Additional public keys are compiled into the desktop, never fetched from a
// lease or remote key URL. The primary key remains supported for older builds.
[[nodiscard]] bool validCustomerKeyConfiguration(
    const QByteArray& primary_public_key, const QString& primary_key_id,
    const QByteArray& trusted_keys_json);
[[nodiscard]] CustomerLeaseResult verifyCustomerLeaseWithTrustedKeys(
    const QByteArray& token, const QByteArray& primary_public_key,
    const QString& primary_key_id, const QByteArray& trusted_keys_json,
    const QString& issuer, const QByteArray& credential, qint64 now,
    qint64 last_observed_time, int app_major);

}  // namespace photo_editor::desktop

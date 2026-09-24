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

}  // namespace photo_editor::desktop

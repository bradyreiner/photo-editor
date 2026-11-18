#pragma once

#include <QByteArray>
#include <QString>

#include <memory>

namespace photo_editor::desktop {
[[nodiscard]] QString customerCredentialScope(const QString& issuer);
[[nodiscard]] QByteArray loadCustomerCredential(QString* error, const QString& issuer = {});
[[nodiscard]] bool saveCustomerCredential(const QByteArray& credential, QString* error, const QString& issuer = {});
[[nodiscard]] bool clearCustomerCredential(QString* error, const QString& issuer = {});

class CustomerCredentialStore {
 public:
  virtual ~CustomerCredentialStore() = default;
  [[nodiscard]] virtual QByteArray load(QString* error) = 0;
  [[nodiscard]] virtual bool save(const QByteArray& credential, QString* error) = 0;
  [[nodiscard]] virtual bool clear(QString* error) = 0;
};

[[nodiscard]] CustomerCredentialStore& systemCustomerCredentialStore(const QString& issuer = {});
}  // namespace photo_editor::desktop

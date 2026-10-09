#pragma once

#include <QByteArray>
#include <QString>

#include <memory>

namespace photo_editor::desktop {
[[nodiscard]] QByteArray loadCustomerCredential(QString* error);
[[nodiscard]] bool saveCustomerCredential(const QByteArray& credential, QString* error);
[[nodiscard]] bool clearCustomerCredential(QString* error);

class CustomerCredentialStore {
 public:
  virtual ~CustomerCredentialStore() = default;
  [[nodiscard]] virtual QByteArray load(QString* error) = 0;
  [[nodiscard]] virtual bool save(const QByteArray& credential, QString* error) = 0;
  [[nodiscard]] virtual bool clear(QString* error) = 0;
};

[[nodiscard]] CustomerCredentialStore& systemCustomerCredentialStore();
}  // namespace photo_editor::desktop

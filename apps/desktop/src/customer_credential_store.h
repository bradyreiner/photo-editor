#pragma once

#include <QByteArray>
#include <QString>

namespace photo_editor::desktop {
[[nodiscard]] QByteArray loadCustomerCredential(QString* error);
[[nodiscard]] bool saveCustomerCredential(const QByteArray& credential, QString* error);
}  // namespace photo_editor::desktop

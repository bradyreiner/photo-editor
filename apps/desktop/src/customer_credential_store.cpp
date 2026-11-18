#include "customer_credential_store.h"

#include <QCryptographicHash>
#include <QHash>
#include <QUrl>

#ifdef Q_OS_MACOS
#include <Security/Security.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <wincred.h>
#endif

namespace photo_editor::desktop {
QString customerCredentialScope(const QString& issuer) {
  if (issuer.isEmpty()) return QStringLiteral("installation-v1");
  QUrl canonical(issuer);
  canonical.setScheme(canonical.scheme().toLower());
  canonical.setHost(canonical.host().toLower());
  if ((canonical.scheme() == QStringLiteral("https") && canonical.port() == 443) ||
      (canonical.scheme() == QStringLiteral("http") && canonical.port() == 80)) canonical.setPort(-1);
  canonical = canonical.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash);
  return QStringLiteral("installation-v2-") + QString::fromLatin1(QCryptographicHash::hash(
      canonical.toEncoded(QUrl::FullyEncoded), QCryptographicHash::Sha256).toHex());
}
#ifdef Q_OS_MACOS
namespace {
CFMutableDictionaryRef query(const QString& scope) {
  auto result = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(result, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(result, kSecAttrService, CFSTR("Looksy customer access"));
  const auto account = CFStringCreateWithCString(nullptr, scope.toUtf8().constData(), kCFStringEncodingUTF8);
  CFDictionarySetValue(result, kSecAttrAccount, account);
  CFRelease(account);
  return result;
}
}
#endif

QByteArray loadCustomerCredential(QString* error, const QString& issuer) {
#ifdef Q_OS_MACOS
  auto attributes = query(customerCredentialScope(issuer));
  CFDictionarySetValue(attributes, kSecReturnData, kCFBooleanTrue);
  CFTypeRef data = nullptr;
  const auto status = SecItemCopyMatching(attributes, &data);
  CFRelease(attributes);
  if (status == errSecItemNotFound) return {};
  if (status != errSecSuccess || !data) {
    *error = QStringLiteral("Unlock your login keychain to use your license.");
    return {};
  }
  const auto bytes = static_cast<CFDataRef>(data);
  const QByteArray result(reinterpret_cast<const char*>(CFDataGetBytePtr(bytes)), CFDataGetLength(bytes));
  CFRelease(data);
  return result;
#elif defined(Q_OS_WIN)
  PCREDENTIALW stored = nullptr;
  const auto target = (QStringLiteral("Looksy/customer-") + customerCredentialScope(issuer)).toStdWString();
  if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &stored)) {
    if (GetLastError() != ERROR_NOT_FOUND) *error = QStringLiteral("Windows Credential Manager is unavailable.");
    return {};
  }
  const QByteArray result(reinterpret_cast<const char*>(stored->CredentialBlob), stored->CredentialBlobSize);
  CredFree(stored);
  return result;
#else
  Q_UNUSED(issuer);
  *error = QStringLiteral("Secure license storage is not supported on this platform.");
  return {};
#endif
}

bool saveCustomerCredential(const QByteArray& credential, QString* error, const QString& issuer) {
#ifdef Q_OS_MACOS
  auto attributes = query(customerCredentialScope(issuer));
  const auto data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(credential.constData()), credential.size());
  const void* keys[] = {kSecValueData};
  const void* values[] = {data};
  const auto update = CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  auto status = SecItemUpdate(attributes, update);
  if (status == errSecItemNotFound) {
    CFDictionarySetValue(attributes, kSecValueData, data);
    CFDictionarySetValue(attributes, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
    status = SecItemAdd(attributes, nullptr);
  }
  CFRelease(update); CFRelease(data); CFRelease(attributes);
  if (status == errSecSuccess) return true;
#elif defined(Q_OS_WIN)
  CREDENTIALW stored{};
  stored.Type = CRED_TYPE_GENERIC;
  const auto target = (QStringLiteral("Looksy/customer-") + customerCredentialScope(issuer)).toStdWString();
  stored.TargetName = const_cast<wchar_t*>(target.c_str());
  stored.CredentialBlobSize = static_cast<DWORD>(credential.size());
  stored.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(credential.constData()));
  stored.Persist = CRED_PERSIST_LOCAL_MACHINE;
  if (CredWriteW(&stored, 0)) return true;
#else
  Q_UNUSED(credential);
  Q_UNUSED(issuer);
#endif
  *error = QStringLiteral("Your installation credential could not be saved securely.");
  return false;
}

bool clearCustomerCredential(QString* error, const QString& issuer) {
#ifdef Q_OS_MACOS
  auto attributes = query(customerCredentialScope(issuer));
  const auto status = SecItemDelete(attributes);
  CFRelease(attributes);
  if (status == errSecSuccess || status == errSecItemNotFound) return true;
#elif defined(Q_OS_WIN)
  const auto target = (QStringLiteral("Looksy/customer-") + customerCredentialScope(issuer)).toStdWString();
  if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) ||
      GetLastError() == ERROR_NOT_FOUND) return true;
#else
  Q_UNUSED(issuer);
  *error = QStringLiteral("Secure license storage is not supported on this platform.");
  return false;
#endif
  *error = QStringLiteral("Your installation credential could not be cleared securely.");
  return false;
}

namespace {
class SystemCustomerCredentialStore final : public CustomerCredentialStore {
 public:
  explicit SystemCustomerCredentialStore(QString issuer) : issuer_(std::move(issuer)) {}
  QByteArray load(QString* error) override { return loadCustomerCredential(error, issuer_); }
  bool save(const QByteArray& credential, QString* error) override {
    return saveCustomerCredential(credential, error, issuer_);
  }
  bool clear(QString* error) override { return clearCustomerCredential(error, issuer_); }
 private:
  QString issuer_;
};
}

CustomerCredentialStore& systemCustomerCredentialStore(const QString& issuer) {
  static QHash<QString, std::shared_ptr<SystemCustomerCredentialStore>> stores;
  const auto scope = customerCredentialScope(issuer);
  auto& store = stores[scope];
  if (!store) store = std::make_shared<SystemCustomerCredentialStore>(issuer);
  return *store;
}
}  // namespace photo_editor::desktop

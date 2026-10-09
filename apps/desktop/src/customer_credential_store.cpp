#include "customer_credential_store.h"

#ifdef Q_OS_MACOS
#include <Security/Security.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <wincred.h>
#endif

namespace photo_editor::desktop {
#ifdef Q_OS_MACOS
namespace {
CFMutableDictionaryRef query() {
  auto result = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(result, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(result, kSecAttrService, CFSTR("Looksy customer access"));
  CFDictionarySetValue(result, kSecAttrAccount, CFSTR("installation-v1"));
  return result;
}
}
#endif

QByteArray loadCustomerCredential(QString* error) {
#ifdef Q_OS_MACOS
  auto attributes = query();
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
  if (!CredReadW(L"Looksy/customer-installation-v1", CRED_TYPE_GENERIC, 0, &stored)) {
    if (GetLastError() != ERROR_NOT_FOUND) *error = QStringLiteral("Windows Credential Manager is unavailable.");
    return {};
  }
  const QByteArray result(reinterpret_cast<const char*>(stored->CredentialBlob), stored->CredentialBlobSize);
  CredFree(stored);
  return result;
#else
  *error = QStringLiteral("Secure license storage is not supported on this platform.");
  return {};
#endif
}

bool saveCustomerCredential(const QByteArray& credential, QString* error) {
#ifdef Q_OS_MACOS
  auto attributes = query();
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
  stored.TargetName = const_cast<wchar_t*>(L"Looksy/customer-installation-v1");
  stored.CredentialBlobSize = static_cast<DWORD>(credential.size());
  stored.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(credential.constData()));
  stored.Persist = CRED_PERSIST_LOCAL_MACHINE;
  if (CredWriteW(&stored, 0)) return true;
#else
  Q_UNUSED(credential);
#endif
  *error = QStringLiteral("Your installation credential could not be saved securely.");
  return false;
}

bool clearCustomerCredential(QString* error) {
#ifdef Q_OS_MACOS
  auto attributes = query();
  const auto status = SecItemDelete(attributes);
  CFRelease(attributes);
  if (status == errSecSuccess || status == errSecItemNotFound) return true;
#elif defined(Q_OS_WIN)
  if (CredDeleteW(L"Looksy/customer-installation-v1", CRED_TYPE_GENERIC, 0) ||
      GetLastError() == ERROR_NOT_FOUND) return true;
#else
  *error = QStringLiteral("Secure license storage is not supported on this platform.");
  return false;
#endif
  *error = QStringLiteral("Your installation credential could not be cleared securely.");
  return false;
}

namespace {
class SystemCustomerCredentialStore final : public CustomerCredentialStore {
 public:
  QByteArray load(QString* error) override { return loadCustomerCredential(error); }
  bool save(const QByteArray& credential, QString* error) override {
    return saveCustomerCredential(credential, error);
  }
  bool clear(QString* error) override { return clearCustomerCredential(error); }
};
}

CustomerCredentialStore& systemCustomerCredentialStore() {
  static SystemCustomerCredentialStore store;
  return store;
}
}  // namespace photo_editor::desktop

#include "customer_lease.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <iostream>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QFile file(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/fixtures/customer-lease.json"));
  if (!file.open(QIODevice::ReadOnly)) return 1;
  const auto fixture = QJsonDocument::fromJson(file.readAll()).object();
  const auto key = QByteArray::fromBase64(fixture.value("publicKey").toString().toLatin1());
  const auto tokens = fixture.value("tokens").toObject();
  const auto verify = [&](const QByteArray& token, qint64 last_observed = 0) {
    return photo_editor::desktop::verifyCustomerLease(token, key, "looksy-1", "https://accounts.looksy.test",
        QByteArray(43, 'a'), 2000000010, last_observed, 1).valid();
  };
  for (auto it = tokens.begin(); it != tokens.end(); ++it) {
    const auto expected = it.key() == "valid" || it.key() == "trial";
    if (verify(it.value().toString().toLatin1()) != expected) {
      std::cerr << "Unexpected verification for " << it.key().toStdString() << '\n'; return 1;
    }
  }
  auto token = tokens.value("valid").toString().toLatin1();
  if (verify(token, 2000000500)) return 1;
  auto parts = token.split('.');
  auto payload = QJsonDocument::fromJson(QByteArray::fromBase64(parts[1], QByteArray::Base64UrlEncoding)).object();
  payload.insert("major", 9);
  parts[1] = QJsonDocument(payload).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
  if (verify(parts.join('.')) || verify(QByteArray(20000, 'a'))) return 1;
  std::cout << "Verified Node-to-native Ed25519 interoperability, tampering, binding, expiry, rollback and feature boundaries.\n";
  return 0;
}

#include "customer_lease.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <iostream>
#include <memory>
#include <openssl/evp.h>

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
  // Prove a rotation overlap with two independent signatures, and rejection
  // when the retired key is removed. All keys here are ephemeral test material.
  std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> key_context(
      EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr), EVP_PKEY_CTX_free);
  EVP_PKEY* generated_key = nullptr;
  if (!key_context || EVP_PKEY_keygen_init(key_context.get()) != 1 ||
      EVP_PKEY_keygen(key_context.get(), &generated_key) != 1) return 1;
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> new_key(generated_key, EVP_PKEY_free);
  QByteArray new_public_key(32, Qt::Uninitialized);
  size_t public_size = 32;
  if (EVP_PKEY_get_raw_public_key(new_key.get(),
      reinterpret_cast<unsigned char*>(new_public_key.data()), &public_size) != 1 || public_size != 32) return 1;
  const auto trusted = QJsonDocument(QJsonObject{{"looksy-1", QString::fromLatin1(key.toBase64())}})
      .toJson(QJsonDocument::Compact);
  const auto rotation_verify = [&](const QByteArray& candidate, const QByteArray& trust = QByteArray("{}")) {
    return photo_editor::desktop::verifyCustomerLeaseWithTrustedKeys(candidate,
        new_public_key, "looksy-2", trust, "https://accounts.looksy.test",
        QByteArray(43, 'a'), 2000000010, 0, 1).valid();
  };
  if (!rotation_verify(token, trusted) || rotation_verify(token)) return 1;
  auto rotated_parts = token.split('.');
  auto rotated_header = QJsonDocument::fromJson(QByteArray::fromBase64(
      rotated_parts[0], QByteArray::Base64UrlEncoding)).object();
  rotated_header.insert("kid", "looksy-2");
  rotated_parts[0] = QJsonDocument(rotated_header).toJson(QJsonDocument::Compact)
      .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
  const auto signed_message = rotated_parts[0] + '.' + rotated_parts[1];
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> signing_context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  QByteArray rotated_signature(64, Qt::Uninitialized);
  size_t signature_size = 64;
  if (!signing_context || EVP_DigestSignInit(signing_context.get(), nullptr, nullptr, nullptr, new_key.get()) != 1 ||
      EVP_DigestSign(signing_context.get(), reinterpret_cast<unsigned char*>(rotated_signature.data()),
          &signature_size, reinterpret_cast<const unsigned char*>(signed_message.constData()),
          static_cast<size_t>(signed_message.size())) != 1 || signature_size != 64) return 1;
  rotated_parts[2] = rotated_signature.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
  if (!rotation_verify(rotated_parts.join('.'), trusted) || !rotation_verify(rotated_parts.join('.'))) return 1;
  const auto conflict = QJsonDocument(QJsonObject{{"looksy-2", QString::fromLatin1(key.toBase64())}}).toJson();
  if (rotation_verify(rotated_parts.join('.'), conflict) || rotation_verify(token, "[]") ||
      rotation_verify(token, "{broken}") || rotation_verify(token, "{\"looksy-1\":\"bad\"}")) return 1;
  auto parts = token.split('.');
  auto payload = QJsonDocument::fromJson(QByteArray::fromBase64(parts[1], QByteArray::Base64UrlEncoding)).object();
  payload.insert("major", 9);
  parts[1] = QJsonDocument(payload).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
  if (verify(parts.join('.')) || verify(QByteArray(20000, 'a'))) return 1;
  std::cout << "Verified Ed25519 interoperability, key rotation/retirement, tampering, binding, expiry, rollback and feature boundaries.\n";
  return 0;
}

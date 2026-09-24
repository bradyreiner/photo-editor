#include "search_runtime_bundle.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>

using namespace photo_editor::desktop;
namespace {
QString digest(const QByteArray& bytes) {
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
bool write(const QString& path, const QByteArray& bytes) {
  QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().size() == 4 && app.arguments().at(1) == "--verify") {
    const auto result = verify_search_runtime_bundle(app.arguments().at(2), app.arguments().at(3));
    std::cout << QJsonDocument(QJsonObject{{"ok", result.ok}, {"error", result.error}, {"executable", result.executable},
        {"files", result.files}, {"bytes", result.bytes}}).toJson().constData();
    return result.ok ? 0 : 1;
  }
  QTemporaryDir root;
  const auto executable = root.filePath("framehouse-search-worker");
  const auto receipt_path = root.filePath("framehouse-search-runtime.json");
  const QByteArray content("fixture worker");
  if (!root.isValid() || !write(executable, content) ||
      !QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
    return fail("fixture creation failed");
  const QJsonObject files{{"framehouse-search-worker", QJsonObject{{"bytes", content.size()}, {"sha256", digest(content)}}}};
  const auto receipt = QJsonDocument(QJsonObject{{"schema", "framehouse-search-runtime-bundle-v1"},
      {"platform", "Darwin"}, {"architecture", "arm64"}, {"files", files}}).toJson();
  if (!write(receipt_path, receipt)) return fail("receipt creation failed");
  const auto trusted = digest(receipt);
  auto result = verify_search_runtime_bundle(root.path(), trusted);
  if (!result.ok || result.files != 1 || result.bytes != content.size()) return fail("valid inventory rejected");
  std::atomic_bool cancelled{true};
  if (verify_search_runtime_bundle(root.path(), trusted, &cancelled).ok) return fail("cancellation ignored");
  if (verify_search_runtime_bundle(root.path(), QString(64, '0')).ok) return fail("untrusted receipt accepted");
  if (!write(executable, QByteArray(content.size(), 'x')) || verify_search_runtime_bundle(root.path(), trusted).ok)
    return fail("same-sized corruption accepted");
  if (!write(executable, content) || !write(root.filePath("extra"), "unexpected") ||
      verify_search_runtime_bundle(root.path(), trusted).ok) return fail("unexpected runtime file accepted");
  if (!QFile::remove(root.filePath("extra"))) return fail("fixture cleanup failed");
  if (!QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner) ||
      verify_search_runtime_bundle(root.path(), trusted).ok) return fail("non-executable entry admitted");
  return 0;
}

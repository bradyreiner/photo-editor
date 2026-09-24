#include "search_runtime_bundle.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace photo_editor::desktop {
SearchRuntimeVerification verify_search_runtime_bundle(
    const QString& root, const QString& expected_receipt_sha256, const std::atomic_bool* cancelled) {
  SearchRuntimeVerification result;
  auto fail = [&](const QString& message) { result.error = message; return result; };
  static const QRegularExpression sha_pattern(QStringLiteral("^[0-9a-f]{64}$"));
  const QFileInfo root_info(root);
  if (!sha_pattern.match(expected_receipt_sha256).hasMatch() || !root_info.isAbsolute() ||
      root_info.isSymLink() || !root_info.isDir()) return fail("Search runtime root or trusted build digest is invalid.");
  const auto canonical_root = root_info.canonicalFilePath();
  const QDir directory(root);
  QFile receipt(directory.filePath("framehouse-search-runtime.json"));
  if (QFileInfo(receipt).isSymLink() || !receipt.open(QIODevice::ReadOnly) || receipt.size() > 8 * 1024 * 1024)
    return fail("Search runtime receipt is missing or invalid.");
  const auto bytes = receipt.readAll();
  if (receipt.error() != QFileDevice::NoError ||
      QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) != expected_receipt_sha256)
    return fail("Search runtime receipt does not match this app build.");
  const auto document = QJsonDocument::fromJson(bytes);
  const auto object = document.object();
  const auto files = object.value("files").toObject();
  if (object.value("schema") != "framehouse-search-runtime-bundle-v1" ||
      object.value("platform") != "Darwin" || object.value("architecture") != "arm64" ||
      files.isEmpty() || files.size() > 20000) return fail("Unsupported search runtime inventory.");
  QSet<QString> seen;
  QDirIterator entries(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                       QDirIterator::Subdirectories);
  while (entries.hasNext()) {
    if (cancelled && cancelled->load()) return fail("Search runtime verification canceled.");
    entries.next();
    const auto info = entries.fileInfo();
    const auto relative = directory.relativeFilePath(info.filePath());
    if (relative == "framehouse-search-runtime.json") continue;
    if (info.isDir() && !info.isSymLink()) continue;
    if (!files.contains(relative)) return fail("Unexpected file in search runtime: " + relative);
    const auto entry = files.value(relative).toObject();
    const auto canonical = info.canonicalFilePath();
    if (canonical.isEmpty() || !canonical.startsWith(canonical_root + '/'))
      return fail("Search runtime entry escapes its bundle: " + relative);
    seen.insert(relative);
    if (info.isSymLink()) {
      const auto link = entry.value("link").toString();
      if (entry.size() != 1 || link.isEmpty() || QDir::isAbsolutePath(link) ||
          QDir::cleanPath(info.symLinkTarget()) != QDir::cleanPath(info.dir().absoluteFilePath(link)))
        return fail("Search runtime library link changed: " + relative);
      continue;
    }
    const auto size = entry.value("bytes").toDouble(-1);
    const auto expected = entry.value("sha256").toString();
    if (!info.isFile() || entry.size() != 2 || size < 0 || size > 4.0 * 1024 * 1024 * 1024 ||
        std::floor(size) != size || !sha_pattern.match(expected).hasMatch() || info.size() != static_cast<qint64>(size))
      return fail("Search runtime file size/type changed: " + relative);
    result.bytes += info.size();
    if (result.bytes > 4LL * 1024 * 1024 * 1024) return fail("Search runtime exceeds package budget.");
    QFile file(info.filePath());
    if (!file.open(QIODevice::ReadOnly)) return fail("Search runtime file is unreadable: " + relative);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
      if (cancelled && cancelled->load()) return fail("Search runtime verification canceled.");
      const auto chunk = file.read(1024 * 1024);
      if (file.error() != QFileDevice::NoError || (chunk.isEmpty() && !file.atEnd()))
        return fail("Could not read search runtime: " + relative);
      hash.addData(chunk);
    }
    if (QString::fromLatin1(hash.result().toHex()) != expected)
      return fail("Search runtime checksum changed: " + relative);
  }
  if (seen.size() != files.size()) return fail("Search runtime files are missing.");
  const auto executable = directory.filePath("framehouse-search-worker");
  if (!files.contains("framehouse-search-worker") || !QFileInfo(executable).isExecutable())
    return fail("Search runtime executable is unavailable.");
  result.ok = true; result.executable = executable; result.files = static_cast<int>(seen.size());
  return result;
}
}  // namespace photo_editor::desktop

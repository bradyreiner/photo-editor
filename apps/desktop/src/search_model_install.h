#pragma once

#include "agent_model_download.h"

namespace photo_editor::desktop {

// SHA-256 values are independently checked against the original pinned Git/LFS
// identities by the Python installer; no mutable Hub metadata is trusted here.
inline AgentModelArtifact pinned_search_model_artifact() {
  return {"google/siglip2-base-patch16-224", "Apache-2.0",
      "https://huggingface.co/google/siglip2-base-patch16-224/resolve/75de2d55ec2d0b4efc50b3e9ad70dba96a7b2fa2", {
      {"README.md", "39ac3705d62af9ffa1a14675b8ccb220a75f2d81acd530e564a3b1e3dfe418d8", 3375},
      {"config.json", "fe8b5fe6d5734360678fd71c11c21e1ea3364bd8598d34295d9206335973ffd7", 253},
      {"model.safetensors", "612923381c76ec5a9bed335d1c48827e3f2e506ac31b044b63b2031fadee6a0b", 1500800904},
      {"preprocessor_config.json", "9b36b57ebaf20f09bf4c22100ccc21877ea6bfe5aead0c00c59f8af8ccefacfc", 394},
      {"special_tokens_map.json", "baec30ea10906f16adb8c18af7a34023002c1746542612b8b41c9f09e1351351", 636},
      {"tokenizer.json", "cb9140fae3ac5122c972d37adf83e1248471a38147ad76f8215c8872c6fd8322", 34363039},
      {"tokenizer.model", "61a7b147390c64585d6c3543dd6fc636906c9af3865a5548f27f31aee1d4c8e2", 4241003},
      {"tokenizer_config.json", "14afe629fe4959b9e0d51e1852b8d9f7ad074f90a1a7125a4fcdd17f06e78fc8", 47164}}};
}

struct SearchModelInstallRequest {
  QString root;
  QString local_source; // Explicit local copy, never implicit adoption or move.
  bool download_confirmed = false;
  bool allow_loopback_fixture = false;
  int inactivity_timeout_ms = 30000;
  std::function<qint64(const QString&)> available_bytes_for_fixture = {};
};

inline bool search_model_source_allowed(const QUrl& url, const QUrl& origin, bool fixture) {
  if (!url.isValid() || !url.userInfo().isEmpty() || !url.fragment().isEmpty()) return false;
  if (fixture) return origin.scheme() == "http" && origin.host() == "127.0.0.1" &&
      url.scheme() == "http" && url.authority() == origin.authority();
  static const QSet<QString> hosts{"huggingface.co", "cdn-lfs.huggingface.co", "cdn-lfs.hf.co",
      "cdn-lfs-us-1.hf.co", "cdn-lfs-eu-1.hf.co", "cas-bridge.xethub.hf.co",
      "us.aws.cdn.hf.co", "us.gcp.cdn.hf.co", "cas-server.xethub.hf.co", "cas-server.xethub-eu.hf.co",
      "transfer.xethub.hf.co", "transfer.xethub-eu.hf.co"};
  return url.scheme() == "https" && url.port(443) == 443 && hosts.contains(url.host());
}

// Worker-only operation. Fixed production pin, no executable/Python process,
// bounded buffering, resumable app-owned partials and recoverable replacements.
// The fixture pin is accepted only with the explicit native-test flag.
inline QVariantMap install_search_model(const SearchModelInstallRequest& request,
    const std::shared_ptr<AgentModelDownloadState>& state,
    const AgentModelArtifact& fixture_pin = {}) {
  using namespace agent_model_download_detail;
  const auto pin = request.allow_loopback_fixture ? fixture_pin : pinned_search_model_artifact();
  QVariantMap result{{"ok", false}, {"model", pin.model}};
  auto fail = [&](const QString& error) {
    result.insert("error", error); result.insert("canceled", state->canceled.load()); return result;
  };
  if (state->canceled.load()) return fail("Model setup paused.");
  if (request.local_source.isEmpty() && !request.download_confirmed)
    return fail("Confirm before downloading the 1.54 GB search model.");
  const QUrl origin(pin.source);
  if (pin.files.isEmpty() || pin.files.size() > 128 || !search_model_source_allowed(origin, origin, request.allow_loopback_fixture))
    return fail("Invalid pinned model source.");
  QSet<QString> names;
  qint64 total = 0;
  const QRegularExpression sha("^[0-9a-f]{64}$");
  for (const auto& file : pin.files) {
    if (!safe_relative_path(file.relative_path) || file.relative_path.contains('/') ||
        file.relative_path.startsWith('.') || names.contains(file.relative_path) || file.bytes <= 0 ||
        file.bytes > 4LL * 1024 * 1024 * 1024 || !sha.match(QString::fromLatin1(file.sha256)).hasMatch())
      return fail("Invalid search artifact pin.");
    names.insert(file.relative_path); total += file.bytes;
  }
  if (total > 4LL * 1024 * 1024 * 1024) return fail("Search model exceeds storage budget.");
  const QFileInfo target(request.root);
  const auto parent = target.dir().absolutePath();
  if (!target.isAbsolute() || target.isSymLink() || target.fileName() != "siglip2-base-patch16-224" ||
      QFileInfo(parent).canonicalFilePath() != parent || !QFileInfo(parent).isDir())
    return fail("Search setup requires an existing private parent and the managed model directory.");
  if (!request.local_source.isEmpty()) {
    const QFileInfo source(request.local_source);
    if (!source.isAbsolute() || source.isSymLink() || !source.isDir() ||
        source.canonicalFilePath() == target.canonicalFilePath() ||
        target.absoluteFilePath().startsWith(source.canonicalFilePath() + '/'))
      return fail("Choose a separate existing local model folder; setup cannot write inside the source.");
  }
  if (!target.exists()) {
    if (!QDir().mkdir(request.root) ||
        !QFile::setPermissions(request.root, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
      return fail("Cannot create the private search model store.");
  }
  const auto root = QFileInfo(request.root).canonicalFilePath();
  if (root != target.absoluteFilePath() || !QFileInfo(root).isDir()) return fail("Unsafe search model store.");
  QLockFile lock(QDir(root).filePath(".framehouse-search-install.lock"));
  lock.setStaleLockTime(0);
  if (!lock.tryLock(0)) return fail("Another search setup owns this store.");
  const QByteArray marker = QJsonDocument(QJsonObject{{"schema", "framehouse-search-model-store-v1"},
      {"model", pin.model}, {"source", pin.source}}).toJson(QJsonDocument::Compact);
  const auto marker_path = QDir(root).filePath(".framehouse-search-store.json");
  if (!safe_file(marker_path)) return fail("Unsafe search model ownership marker.");
  QFile owner(marker_path);
  if (owner.exists()) {
    if (!owner.open(QIODevice::ReadOnly) || owner.size() > 4096 || owner.readAll() != marker)
      return fail("Search model store ownership does not match this application pin.");
  } else {
    auto entries = QDir(root).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    entries.removeAll(".framehouse-search-install.lock");
    if (!entries.isEmpty()) return fail("Refusing to modify an unowned model folder. Import it into an empty managed store.");
    QSaveFile output(marker_path); output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(marker) != marker.size() || !output.commit())
      return fail("Cannot establish private model store ownership.");
  }
  owner.close();
  AgentModelDownloadRequest transport{root, pin, request.allow_loopback_fixture, request.inactivity_timeout_ms,
      request.available_bytes_for_fixture};
  auto remaining = [&](qsizetype start) {
    qint64 bytes = 0;
    for (qsizetype i = start; i < pin.files.size(); ++i) {
      const auto& file = pin.files[i];
      const QFileInfo ready(QDir(root).filePath(file.relative_path));
      const QFileInfo partial(QDir(root).filePath(".framehouse-" + QString::fromLatin1(file.sha256) + ".part"));
      if (safe_file(ready.filePath()) && ready.size() == file.bytes) continue; // estimate, not integrity
      bytes += file.bytes - (safe_file(partial.filePath()) && partial.size() <= file.bytes ? qMax(0LL, partial.size()) : 0LL);
    }
    return bytes;
  };
  auto error = storage_error(transport, root, remaining(0));
  if (!error.isEmpty()) return fail(error);
  QNetworkAccessManager network;
  network.setProxy(QNetworkProxy::NoProxy);
  qint64 completed = 0;
  int reused = 0;
  QStringList quarantined;
  for (qsizetype i = 0; i < pin.files.size(); ++i) {
    if (state->canceled.load()) return fail("Model setup paused.");
    const auto& file = pin.files[i];
    const auto destination = QDir(root).filePath(file.relative_path);
    const auto partial_path = QDir(root).filePath(".framehouse-" + QString::fromLatin1(file.sha256) + ".part");
    if (!safe_file(destination) || !safe_file(partial_path)) return fail("Unsafe search artifact path.");
    QFile ready(destination);
    if (ready.exists() && ready.size() == file.bytes) {
      QCryptographicHash hash(QCryptographicHash::Sha256);
      if (!ready.open(QIODevice::ReadOnly) || !hash_prefix(ready, hash, file.bytes, state, completed, total))
        return fail("Cannot verify existing search model data.");
      if (hash.result().toHex() == file.sha256) { completed += file.bytes; ++reused; continue; }
    }
    ready.close();
    QFile partial(partial_path);
    if (!partial.open(QIODevice::ReadWrite) || partial.size() > file.bytes ||
        !partial.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) return fail("Cannot open private search partial data.");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 offset = partial.size();
    if (!hash_prefix(partial, hash, offset, state, completed, total)) return fail("Cannot verify search partial data.");
    auto space = [&] {
      error = storage_error(transport, root, file.bytes - offset + remaining(i + 1));
      return error.isEmpty();
    };
    if (!space()) return fail(error);
    QElapsedTimer disk_timer; disk_timer.start();
    qint64 bytes_since_check = 0;
    auto consume = [&](const QByteArray& bytes) {
      if (bytes_since_check >= 64LL * 1024 * 1024 || disk_timer.elapsed() >= 1000) {
        if (!space()) return false;
        disk_timer.restart(); bytes_since_check = 0;
      }
      if (state->canceled.load() || bytes.size() > file.bytes - offset || partial.write(bytes) != bytes.size()) return false;
      hash.addData(bytes); offset += bytes.size(); bytes_since_check += bytes.size();
      state->publish(request.local_source.isEmpty() ? "Downloading search model" : "Importing local search model", completed + offset, total);
      return true;
    };
    if (offset < file.bytes && !request.local_source.isEmpty()) {
      const auto source_path = QDir(QFileInfo(request.local_source).canonicalFilePath()).filePath(file.relative_path);
      QFile input(source_path);
      if (!safe_file(source_path) || !input.open(QIODevice::ReadOnly) || input.size() != file.bytes || !input.seek(offset))
        return fail("Local model is missing an exact-sized pinned file: " + file.relative_path);
      while (offset < file.bytes) {
        if (state->canceled.load()) return fail("Model setup paused; partial copy retained.");
        const auto bytes = input.read(qMin(1024LL * 1024, file.bytes - offset));
        if (bytes.isEmpty() || input.error() != QFileDevice::NoError || !consume(bytes)) return fail("Cannot copy local search artifact.");
      }
    } else if (offset < file.bytes) {
      auto url = origin; url.setPath(origin.path() + '/' + file.relative_path);
      const auto transfer_error = transfer(network, url, origin, transport, state, file.bytes, offset, [&] {
        offset = 0; hash.reset(); return partial.resize(0) && partial.seek(0) && space();
      }, consume, [&](const QUrl& next) { return search_model_source_allowed(next, origin, request.allow_loopback_fixture); });
      if (!transfer_error.isEmpty()) return fail(error.isEmpty() ? transfer_error : error);
    }
    if (!partial.flush() || partial.size() != file.bytes) return fail("Cannot save search artifact.");
    if (hash.result().toHex() != file.sha256) {
      partial.close();
      const bool removed = QFile::remove(partial_path);
      return fail(removed ? "Search checksum failed; only the app-owned partial was discarded. Retry setup." : "Search checksum failed; could not discard its partial cache.");
    }
    partial.close();
    if (state->canceled.load()) return fail("Model setup paused.");
    QString backup;
    if (QFileInfo::exists(destination)) {
      backup = destination + ".rejected-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
      if (!QFile::rename(destination, backup)) return fail("Cannot preserve replaced search model data.");
      quarantined.append(backup); result.insert("quarantinedPaths", quarantined);
    }
    if (!QFile::rename(partial_path, destination)) {
      if (!backup.isEmpty()) QFile::rename(backup, destination);
      return fail("Cannot publish verified search artifact.");
    }
    completed += file.bytes;
  }
  if (state->canceled.load()) return fail("Model setup paused.");
  result = verify_agent_model_artifact(root, pin, std::shared_ptr<std::atomic_bool>(state, &state->canceled));
  result.insert("reusedFiles", reused); result.insert("quarantinedPaths", quarantined);
  result.insert("canceled", state->canceled.load() && !result.value("ok").toBool());
  if (result.value("ok").toBool()) state->publish("Search model verified", total, total);
  return result;
}

}  // namespace photo_editor::desktop

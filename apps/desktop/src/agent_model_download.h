#pragma once

#include "agent_model_artifact.h"

#include <QEventLoop>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStorageInfo>
#include <QElapsedTimer>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent>
#include <functional>

namespace photo_editor::desktop {

struct AgentModelDownloadRequest {
  QString root;
  AgentModelArtifact artifact;
  // Only native loopback fixtures enable this. No preference/environment or
  // controller input can weaken the production HTTPS policy.
  bool allow_loopback_fixture = false;
  int inactivity_timeout_ms = 30000;
  std::function<qint64(const QString&)> available_bytes_for_fixture = {};
};

struct AgentModelDownloadState {
  std::atomic_bool canceled{false};
  QMutex mutex;
  QVariantMap progress;
  void publish(const QString& status, qint64 completed, qint64 total) {
    QMutexLocker lock(&mutex);
    const auto fraction = total > 0 ? static_cast<double>(completed) / static_cast<double>(total) : 0.0;
    progress = {{"status", status}, {"completedBytes", completed}, {"totalBytes", total},
        {"progress", qBound(0.0, fraction, 1.0)},
        {"progressPercent", static_cast<int>(qBound(0.0, fraction * 100.0, 100.0))}};
  }
  QVariantMap snapshot() {
    QMutexLocker lock(&mutex);
    return progress;
  }
};

namespace agent_model_download_detail {

inline bool safe_relative_path(const QString& path) {
  return !path.isEmpty() && path != "." && path != ".." &&
      !QFileInfo(path).isAbsolute() && QDir::cleanPath(path) == path && !path.startsWith("../");
}

inline bool safe_directory(const QString& root, const QString& relative) {
  if (!safe_relative_path(relative)) return false;
  auto current = root;
  for (const auto& part : relative.split('/')) {
    current = QDir(current).filePath(part);
    const QFileInfo info(current);
    if (info.isSymLink()) return false;
    if (!info.exists() && !QDir().mkdir(current)) return false;
    if (!QFileInfo(current).isDir() || QFileInfo(current).canonicalFilePath() != current) return false;
  }
  return true;
}

inline bool safe_file(const QString& path) {
  const QFileInfo info(path);
  return !info.isSymLink() && (!info.exists() || (info.isFile() && info.canonicalFilePath() == path));
}

inline bool source_allowed(const QUrl& url, const QUrl& origin, bool fixture) {
  if (!url.isValid() || !url.userInfo().isEmpty() || !url.fragment().isEmpty()) return false;
  if (fixture && origin.scheme() == "http" && origin.host() == "127.0.0.1")
    return url.scheme() == "http" && url.authority() == origin.authority();
  if (url.scheme() != "https" || url.port(443) != 443) return false;
  return url.host() == origin.host() || url.host().endsWith(QStringLiteral(".r2.cloudflarestorage.com"));
}

inline bool valid_pin(const AgentModelDownloadRequest& request) {
  const auto& pin = request.artifact;
  const QUrl source(pin.source);
  if (pin.model.isEmpty() || pin.files.size() < 2 || pin.files.size() > 128 ||
      source.host().isEmpty() || !source_allowed(source, source, request.allow_loopback_fixture) || !source.query().isEmpty() ||
      !source.path().contains("/manifests/") || !source.path().startsWith("/v2/") ||
      !pin.files.first().relative_path.startsWith("manifests/") || pin.files.first().bytes > 65536) return false;
  const QRegularExpression digest(QStringLiteral("^[0-9a-f]{64}$"));
  QSet<QString> paths;
  qint64 total = 0;
  for (qsizetype i = 0; i < pin.files.size(); ++i) {
    const auto& file = pin.files[i];
    if (!safe_relative_path(file.relative_path) || paths.contains(file.relative_path) ||
        !digest.match(QString::fromLatin1(file.sha256)).hasMatch() || file.bytes <= 0 ||
        file.bytes > 1024LL * 1024LL * 1024LL * 1024LL ||
        (i > 0 && file.relative_path != "blobs/sha256-" + QString::fromLatin1(file.sha256))) return false;
    paths.insert(file.relative_path);
    total += file.bytes;
  }
  return total <= 1024LL * 1024LL * 1024LL * 1024LL;
}

inline bool manifest_matches(const QByteArray& bytes, const AgentModelArtifact& pin) {
  if (bytes.size() != pin.files.first().bytes ||
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != pin.files.first().sha256) return false;
  const auto doc = QJsonDocument::fromJson(bytes);
  const auto object = doc.object();
  if (!doc.isObject() || object.value("schemaVersion").toInt() != 2 ||
      !object.value("config").isObject() || !object.value("layers").isArray()) return false;
  QHash<QString, qint64> expected;
  for (qsizetype i = 1; i < pin.files.size(); ++i)
    expected.insert("sha256:" + QString::fromLatin1(pin.files[i].sha256), pin.files[i].bytes);
  auto references = object.value("layers").toArray();
  references.append(object.value("config"));
  QSet<QString> seen;
  for (const auto& value : references) {
    const auto ref = value.toObject();
    const auto digest = ref.value("digest").toString();
    if (!value.isObject() || !expected.contains(digest) ||
        ref.value("size").toInteger(-1) != expected.value(digest)) return false;
    seen.insert(digest);
  }
  return seen.size() == expected.size();
}

inline constexpr qint64 storage_headroom_bytes = 1024LL * 1024LL * 1024LL;

// Admission estimate only: file sizes do not establish model integrity. The
// worker hashes cached files and revises its disk budget before replacing any
// corrupt payload. Never count paths outside the private canonical store.
inline qint64 estimated_cached_blob_bytes(const QString& root, const AgentModelArtifactFile& file) {
  if (root.isEmpty() || !safe_relative_path(file.relative_path) || file.bytes <= 0 ||
      file.relative_path != "blobs/sha256-" + QString::fromLatin1(file.sha256)) return 0;
  const QFileInfo complete(QDir(root).filePath(file.relative_path));
  if (complete.isFile() && !complete.isSymLink() && complete.canonicalFilePath() == complete.absoluteFilePath() &&
      complete.size() == file.bytes) return file.bytes;
  const QFileInfo partial(QDir(root).filePath("blobs/.framehouse-" + QString::fromLatin1(file.sha256) + ".part"));
  if (partial.isFile() && !partial.isSymLink() && partial.canonicalFilePath() == partial.absoluteFilePath() &&
      partial.size() >= 0 && partial.size() <= file.bytes) return partial.size();
  return 0;
}

inline qint64 estimated_remaining_bytes(const QString& root, const AgentModelArtifact& pin, qsizetype first_blob = 1) {
  // QSaveFile needs a fresh manifest allocation even when a manifest exists.
  qint64 remaining = pin.files.first().bytes;
  for (qsizetype i = first_blob; i < pin.files.size(); ++i)
    remaining += pin.files[i].bytes - estimated_cached_blob_bytes(root, pin.files[i]);
  return remaining;
}

inline qint64 available_storage_bytes(const AgentModelDownloadRequest& request, const QString& root) {
  if (request.allow_loopback_fixture && request.available_bytes_for_fixture)
    return request.available_bytes_for_fixture(root);
  const QStorageInfo storage(root);
  return storage.isValid() && storage.isReady() ? storage.bytesAvailable() : -1;
}

inline QString storage_error(const AgentModelDownloadRequest& request, const QString& root, qint64 remaining) {
  const auto available = available_storage_bytes(request, root);
  if (available < 0) return QStringLiteral("The private model store is unavailable. Check the disk and retry.");
  const auto required = remaining + storage_headroom_bytes;
  if (available >= required) return {};
  return QStringLiteral("The remaining model data needs %1 GB free, including 1 GiB headroom. Cached files may require repair after verification. Free space and retry; partial downloads are retained.")
      .arg(static_cast<double>(required) / 1e9, 0, 'f', 2);
}

// All networking and file writes happen on the worker. Buffering is bounded;
// request cancellation aborts the actual transfer, not an unrelated CLI client.
// A reset callback handles servers that ignore Range and return the full object.
inline QString transfer(QNetworkAccessManager& network, QUrl url, const QUrl& origin,
    const AgentModelDownloadRequest& request, const std::shared_ptr<AgentModelDownloadState>& state,
    qint64 expected, qint64 offset, const std::function<bool()>& reset,
    const std::function<bool(const QByteArray&)>& consume,
    const std::function<bool(const QUrl&)>& pinned_source_policy = {}) {
  for (int redirects = 0; redirects <= 4; ++redirects) {
    if (state->canceled.load()) return QStringLiteral("Download paused.");
    if (pinned_source_policy ? !pinned_source_policy(url) :
        !source_allowed(url, origin, request.allow_loopback_fixture)) return QStringLiteral("Unsafe model download redirect rejected.");
    QNetworkRequest query(url);
    query.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    query.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    query.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    query.setRawHeader("Accept-Encoding", "identity");
    query.setTransferTimeout(request.inactivity_timeout_ms);
    if (offset > 0) query.setRawHeader("Range", "bytes=" + QByteArray::number(offset) + "-" + QByteArray::number(expected - 1));
    auto* reply = network.get(query);
    reply->setReadBufferSize(256 * 1024);
    QEventLoop loop;
    QTimer cancellation;
    cancellation.setInterval(25);
    bool headers_checked = false;
    bool redirect = false;
    QString error;
    qint64 received = offset;
    auto abort = [&](const QString& reason) {
      error = reason;
      reply->abort();
    };
    auto check_headers = [&] {
      if (headers_checked) return;
      const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      if (status == 0) return;
      headers_checked = true;
      if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        redirect = true;
        reply->abort();
        return;
      }
      if (status != 200 && !(offset > 0 && status == 206)) {
        abort(QStringLiteral("Model download returned HTTP %1.").arg(status));
        return;
      }
      const auto encoding = reply->rawHeader("Content-Encoding");
      if (!encoding.isEmpty() && encoding != "identity") {
        abort(QStringLiteral("Encoded model payload rejected."));
        return;
      }
      if (status == 206) {
        const auto range = "bytes " + QByteArray::number(offset) + "-" + QByteArray::number(expected - 1) + "/" + QByteArray::number(expected);
        if (reply->rawHeader("Content-Range") != range) {
          abort(QStringLiteral("Model download returned an unexpected byte range."));
          return;
        }
      }
      const qint64 response_bytes = status == 200 ? expected : expected - offset;
      const auto length = reply->rawHeader("Content-Length");
      bool length_valid = false;
      const auto declared = length.toLongLong(&length_valid);
      if (!length.isEmpty() && (!length_valid || declared != response_bytes)) {
        abort(QStringLiteral("Model payload size differs from the application pin."));
        return;
      }
      if (offset > 0 && status == 200) {
        if (!reset()) {
          abort(QStringLiteral("Cannot restart the partial model download."));
          return;
        }
        received = 0;
      }
    };
    auto read = [&] {
      check_headers();
      if (!headers_checked || redirect || !error.isEmpty()) return;
      while (reply->bytesAvailable() > 0) {
        if (state->canceled.load()) {
          abort(QStringLiteral("Download paused."));
          return;
        }
        const auto chunk = reply->read(64 * 1024);
        if (chunk.isEmpty()) break;
        if (chunk.size() > expected - received) {
          abort(QStringLiteral("Model payload exceeded its pinned size."));
          return;
        }
        if (!consume(chunk)) {
          abort(QStringLiteral("Cannot save model payload. Check free space and permissions."));
          return;
        }
        received += chunk.size();
      }
    };
    QObject::connect(reply, &QNetworkReply::metaDataChanged, &loop, check_headers);
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, read);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&cancellation, &QTimer::timeout, &loop, [&] {
      if (state->canceled.load()) abort(QStringLiteral("Download paused."));
    });
    cancellation.start();
    if (!reply->isFinished()) loop.exec();
    cancellation.stop();
    read();
    const auto next = url.resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
    const auto network_error = reply->error();
    delete reply;
    if (state->canceled.load()) return QStringLiteral("Download paused.");
    if (!error.isEmpty()) return error;
    if (redirect) {
      url = next;
      continue;
    }
    if (network_error != QNetworkReply::NoError || received != expected)
      return QStringLiteral("Model transfer interrupted. Retry to resume the partial download.");
    return {};
  }
  return QStringLiteral("Too many model download redirects.");
}

inline bool hash_prefix(QFile& file, QCryptographicHash& hash, qint64 bytes,
    const std::shared_ptr<AgentModelDownloadState>& state, qint64 base, qint64 total) {
  if (!file.seek(0)) return false;
  qint64 read_bytes = 0;
  while (read_bytes < bytes) {
    if (state->canceled.load()) return false;
    const auto chunk = file.read(qMin(4LL * 1024LL * 1024LL, bytes - read_bytes));
    if (chunk.isEmpty() || file.error() != QFileDevice::NoError) return false;
    hash.addData(chunk);
    read_bytes += chunk.size();
    state->publish(QStringLiteral("Checking existing model data"), base + read_bytes, total);
  }
  return true;
}

}  // namespace agent_model_download_detail

inline QVariantMap agent_model_download_space_plan(const QString& store, const AgentModelArtifact& pin) {
  using namespace agent_model_download_detail;
  if (!valid_pin({store, pin, true})) return {{"valid", false}};
  const auto root = QFileInfo(store).canonicalFilePath();
  const auto remaining = estimated_remaining_bytes(root, pin);
  return {{"valid", true}, {"artifactBytes", pin.downloadBytes()},
      {"estimatedCachedBytes", pin.downloadBytes() - remaining}, {"estimatedRemainingBytes", remaining},
      {"requiredFreeBytes", remaining + storage_headroom_bytes},
      {"storageHeadroomBytes", storage_headroom_bytes}, {"cacheEstimateRequiresVerification", true}};
}

inline QVariantMap download_agent_model_artifact(const AgentModelDownloadRequest& request,
    const std::shared_ptr<AgentModelDownloadState>& state) {
  using namespace agent_model_download_detail;
  const auto& pin = request.artifact;
  QVariantMap result{{"ok", false}, {"model", pin.model}};
  auto fail = [&](const QString& message) {
    result.insert(QStringLiteral("error"), message);
    result.insert(QStringLiteral("canceled"), state->canceled.load());
    return result;
  };
  if (state->canceled.load()) return fail(QStringLiteral("Download paused."));
  const auto root = QFileInfo(request.root).canonicalFilePath();
  if (!valid_pin(request) || !QFileInfo(request.root).isAbsolute() || root.isEmpty() || root == QDir::rootPath() ||
      !QFileInfo(root).isDir() || QFileInfo(request.root).isSymLink())
    return fail(QStringLiteral("Invalid model pin or private download store."));
  QLockFile lock(QDir(root).filePath(QStringLiteral(".framehouse-model-download.lock")));
  lock.setStaleLockTime(0);
  if (!lock.tryLock(0)) return fail(QStringLiteral("Another model download owns this store."));
  const auto admission_error = storage_error(request, root, estimated_remaining_bytes(root, pin));
  if (!admission_error.isEmpty()) return fail(admission_error);
  QNetworkAccessManager network;
  network.setProxy(QNetworkProxy::NoProxy);
  const QUrl source(pin.source);
  const auto total = pin.downloadBytes();
  QByteArray manifest;
  state->publish(QStringLiteral("Checking pinned model manifest"), 0, total);
  auto error = transfer(network, source, source, request, state, pin.files.first().bytes, 0,
      [] { return false; }, [&](const QByteArray& chunk) { manifest.append(chunk); return true; });
  if (!error.isEmpty()) return fail(error);
  // No blob request or model payload publication precedes this full pin check.
  if (!manifest_matches(manifest, pin)) return fail(QStringLiteral("The registry manifest does not match the approved model pin. No model blobs were downloaded."));
  if (!safe_directory(root, QStringLiteral("blobs"))) return fail(QStringLiteral("Unsafe model blob directory."));
  qint64 completed = manifest.size();
  QStringList quarantined;
  for (qsizetype i = 1; i < pin.files.size(); ++i) {
    if (state->canceled.load()) return fail(QStringLiteral("Download paused."));
    const auto& file = pin.files[i];
    const auto destination = QDir(root).filePath(file.relative_path);
    const auto partial_path = QDir(root).filePath("blobs/.framehouse-" + QString::fromLatin1(file.sha256) + ".part");
    if (!safe_file(destination) || !safe_file(partial_path)) return fail(QStringLiteral("Unsafe model payload path."));
    if (QFileInfo(destination).size() == file.bytes) {
      QFile existing(destination);
      QCryptographicHash hash(QCryptographicHash::Sha256);
      if (!existing.open(QIODevice::ReadOnly) || !hash_prefix(existing, hash, file.bytes, state, completed, total))
        return fail(QStringLiteral("Cannot verify existing model data."));
      if (hash.result().toHex() == file.sha256) {
        completed += file.bytes;
        continue;
      }
    }
    QFile partial(partial_path);
    if (!partial.open(QIODevice::ReadWrite) || partial.size() > file.bytes)
      return fail(QStringLiteral("Cannot open the partial model download."));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 offset = partial.size();
    if (!hash_prefix(partial, hash, offset, state, completed, total))
      return fail(QStringLiteral("Cannot verify partial model data."));
    QString disk_error;
    const auto check_space = [&] {
      // This blob's complete-file estimate is no longer trusted: its hash was
      // missing or wrong. Reserve its actual remaining replacement bytes.
      disk_error = storage_error(request, root,
          file.bytes - offset + estimated_remaining_bytes(root, pin, i + 1));
      return disk_error.isEmpty();
    };
    if (!check_space()) return fail(disk_error);
    if (offset < file.bytes) {
      auto blob = source;
      blob.setPath(source.path().left(source.path().lastIndexOf("/manifests/")) +
          "/blobs/sha256:" + QString::fromLatin1(file.sha256));
      QElapsedTimer disk_check_timer;
      disk_check_timer.start();
      qint64 bytes_since_disk_check = 0;
      error = transfer(network, blob, source, request, state, file.bytes, offset, [&] {
        hash.reset();
        offset = 0;
        return partial.resize(0) && partial.seek(0) && check_space();
      }, [&](const QByteArray& chunk) {
        if (bytes_since_disk_check >= 64LL * 1024LL * 1024LL || disk_check_timer.elapsed() >= 1000) {
          if (!check_space()) return false;
          disk_check_timer.restart();
          bytes_since_disk_check = 0;
        }
        if (partial.write(chunk) != chunk.size()) return false;
        hash.addData(chunk);
        offset += chunk.size();
        bytes_since_disk_check += chunk.size();
        state->publish(QStringLiteral("Downloading pinned model data"), completed + offset, total);
        return true;
      });
      if (!error.isEmpty()) return fail(disk_error.isEmpty() ? error : disk_error);
    }
    if (!partial.flush() || partial.size() != file.bytes) return fail(QStringLiteral("Cannot finish saving the model payload."));
    if (hash.result().toHex() != file.sha256) {
      partial.close();
      // Only the app-owned partial cache is discarded. Existing final files
      // and manifests remain unchanged on a failed transfer/hash.
      const bool removed = QFile::remove(partial_path);
      return fail(removed ? QStringLiteral("Downloaded data failed SHA-256 verification; its partial cache was discarded. Retry download.")
                          : QStringLiteral("Downloaded data failed SHA-256 verification; could not discard its partial cache."));
    }
    partial.close();
    if (state->canceled.load()) return fail(QStringLiteral("Download paused."));
    QString backup;
    if (QFileInfo::exists(destination)) {
      backup = destination + ".rejected-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
      if (!QFile::rename(destination, backup)) return fail(QStringLiteral("Cannot preserve the replaced model file."));
      quarantined.append(backup);
    }
    if (!QFile::rename(partial_path, destination)) {
      if (!backup.isEmpty()) QFile::rename(backup, destination);
      return fail(QStringLiteral("Cannot publish the verified model payload."));
    }
    completed += file.bytes;
  }
  if (state->canceled.load()) return fail(QStringLiteral("Download paused."));
  const auto commit_space_error = storage_error(request, root, pin.files.first().bytes);
  if (!commit_space_error.isEmpty()) return fail(commit_space_error);
  const auto manifest_path = QDir(root).filePath(pin.files.first().relative_path);
  const auto parent = QFileInfo(pin.files.first().relative_path).path();
  if (!safe_directory(root, parent) || !safe_file(manifest_path)) return fail(QStringLiteral("Unsafe model manifest path."));
  // Ollama sees the named model only after every pinned blob is complete.
  QSaveFile output(manifest_path);
  output.setDirectWriteFallback(false);
  if (!output.open(QIODevice::WriteOnly) || output.write(manifest) != manifest.size() ||
      state->canceled.load() || !output.commit()) return fail(QStringLiteral("Cannot publish the verified model manifest."));
  state->publish(QStringLiteral("Downloaded; verifying model files"), total, total);
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("completedBytes"), total);
  result.insert(QStringLiteral("quarantinedPaths"), quarantined);
  return result;
}

// One owned worker, with coalesced UI-thread progress. Destruction requests
// cancellation without blocking the UI; the worker owns no controller pointers.
class ManagedAgentModelDownload final : public QObject {
 public:
  explicit ManagedAgentModelDownload(QObject* parent = nullptr) : QObject(parent) {
    timer_.setInterval(100);
    connect(&timer_, &QTimer::timeout, this, [this] {
      if (on_progress) on_progress(state_->snapshot());
    });
    connect(&watcher_, &QFutureWatcher<QVariantMap>::finished, this, [this] {
      timer_.stop();
      auto result = watcher_.result();
      // Publishing the manifest is the commit point. A pause arriving after
      // that point must not relabel an already installed artifact as canceled.
      if (state_->canceled.load() && !result.value(QStringLiteral("ok")).toBool()) {
        result.insert(QStringLiteral("canceled"), true);
      }
      if (on_finished) on_finished(result);
    });
  }
  ~ManagedAgentModelDownload() override { cancel(); }
  void start(const AgentModelDownloadRequest& request) {
    if (started_) return;
    started_ = true;
    const auto state = state_;
    watcher_.setFuture(QtConcurrent::run([request, state] {
      try { return download_agent_model_artifact(request, state); }
      catch (...) { return QVariantMap{{"ok", false}, {"error", "Model download worker failed."}, {"canceled", state->canceled.load()}}; }
    }));
    timer_.start();
  }
  void cancel() { state_->canceled.store(true); }
  std::function<void(const QVariantMap&)> on_progress;
  std::function<void(const QVariantMap&)> on_finished;
 private:
  bool started_ = false;
  std::shared_ptr<AgentModelDownloadState> state_ = std::make_shared<AgentModelDownloadState>();
  QFutureWatcher<QVariantMap> watcher_;
  QTimer timer_;
};

}  // namespace photo_editor::desktop

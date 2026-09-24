#include "agent_model_download.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

using namespace photo_editor::desktop;

static bool wait_for(const std::function<bool()>& condition, int deadline = 8000) {
  QElapsedTimer timer;
  timer.start();
  while (!condition() && timer.elapsed() < deadline) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  return condition();
}

static bool write_file(const QString& path, const QByteArray& data) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

struct RegistryFixture {
  QTcpServer server;
  AgentModelArtifact pin;
  QByteArray manifest;
  QHash<QByteArray, QByteArray> blobs;
  QVector<QByteArray> requests;
  QVector<QByteArray> ranges;
  QString mode;
  bool interrupted = false;
  int active_sockets = 0;

  bool start() {
    if (!server.listen(QHostAddress::LocalHost, 0)) return false;
    pin = {"fixture:vision", "Apache-2.0",
        QStringLiteral("http://127.0.0.1:%1/v2/library/fixture/manifests/vision").arg(server.serverPort()), {}};
    QJsonArray layers;
    QJsonObject config;
    QVector<AgentModelArtifactFile> files;
    for (const auto& data : {QByteArray(512 * 1024, 'w'), QByteArray("config"), QByteArray("params")}) {
      const auto digest = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
      blobs.insert(digest, data);
      files.append({"blobs/sha256-" + QString::fromLatin1(digest), digest, data.size()});
      QJsonObject ref{{"digest", "sha256:" + QString::fromLatin1(digest)}, {"size", data.size()}};
      if (data == "config") config = ref;
      else layers.append(ref);
    }
    layers.append(layers.last()); // registry can repeat a physical layer
    manifest = QJsonDocument(QJsonObject{{"schemaVersion", 2}, {"config", config}, {"layers", layers}}).toJson(QJsonDocument::Compact);
    pin.files.append({"manifests/registry.ollama.ai/library/fixture/vision",
        QCryptographicHash::hash(manifest, QCryptographicHash::Sha256).toHex(), manifest.size()});
    pin.files.append(files);
    QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
      while (auto* socket = server.nextPendingConnection()) {
        ++active_sockets;
        QObject::connect(socket, &QTcpSocket::disconnected, socket, [this, socket] {
          --active_sockets;
          socket->deleteLater();
        });
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
          auto input = socket->property("request").toByteArray() + socket->readAll();
          socket->setProperty("request", input);
          if (!input.contains("\r\n\r\n") || socket->property("handled").toBool()) return;
          socket->setProperty("handled", true);
          const auto path = input.split(' ').value(1);
          requests.append(path);
          QByteArray range;
          for (const auto& line : input.split('\n'))
            if (line.toLower().startsWith("range:")) range = line.mid(6).trimmed();
          ranges.append(range);
          if (path.contains("/manifests/")) {
            auto bytes = manifest;
            if (mode == "bad-manifest") bytes[0] = '!';
            respond(socket, bytes, 0, bytes.size());
            return;
          }
          const auto digest = path.mid(path.lastIndexOf(':') + 1);
          auto bytes = blobs.value(digest);
          if (bytes.isEmpty()) {
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
          }
          const bool weight = bytes.size() > 10000;
          qint64 offset = 0;
          if (!range.isEmpty() && mode != "ignore-range") offset = range.mid(6).split('-').first().toLongLong();
          if (weight && mode == "unsafe-redirect") {
            socket->write("HTTP/1.1 307 Temporary Redirect\r\nLocation: http://localhost:1/not-allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
          }
          if (weight && mode == "redirect-loop") {
            socket->write("HTTP/1.1 307 Temporary Redirect\r\nLocation: " + path + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
          }
          if (weight && mode == "chunked-oversize") {
            bytes.append('!');
            socket->write("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n" +
                QByteArray::number(bytes.size(), 16) + "\r\n" + bytes + "\r\n0\r\n\r\n");
            socket->disconnectFromHost();
            return;
          }
          if (weight && mode == "encoded") {
            socket->write("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: " + QByteArray::number(bytes.size()) +
                "\r\nConnection: close\r\n\r\n" + bytes);
            socket->disconnectFromHost();
            return;
          }
          if (weight && mode == "bad-hash") bytes[0] = '!';
          if (weight && mode == "oversize") {
            respond(socket, bytes, offset, bytes.size() + 1);
            return;
          }
          if (weight && mode == "bad-range" && offset > 0) {
            respond(socket, bytes, offset + 1, bytes.size());
            return;
          }
          if (weight && mode == "stall") return;
          if (weight && mode == "interrupt" && !interrupted) {
            interrupted = true;
            socket->write(headers(offset, bytes.size()));
            socket->write(bytes.mid(offset, 128 * 1024));
            socket->disconnectFromHost();
            return;
          }
          if (weight && mode == "slow") {
            socket->write(headers(offset, bytes.size()));
            auto* timer = new QTimer(socket);
            timer->setInterval(2);
            QObject::connect(timer, &QTimer::timeout, socket, [socket, timer, bytes, offset]() mutable {
              if (socket->state() != QAbstractSocket::ConnectedState) { timer->stop(); return; }
              const auto chunk = bytes.mid(offset, 4096);
              socket->write(chunk);
              offset += chunk.size();
              if (offset >= bytes.size()) { timer->stop(); socket->disconnectFromHost(); }
            });
            timer->start();
            return;
          }
          respond(socket, bytes, offset, bytes.size());
        });
      }
    });
    return true;
  }

  static QByteArray headers(qint64 offset, qint64 total) {
    auto result = offset > 0 ? QByteArray("HTTP/1.1 206 Partial Content\r\n") : QByteArray("HTTP/1.1 200 OK\r\n");
    result += "Content-Length: " + QByteArray::number(total - offset) + "\r\n";
    if (offset > 0) result += "Content-Range: bytes " + QByteArray::number(offset) + "-" +
        QByteArray::number(total - 1) + "/" + QByteArray::number(total) + "\r\n";
    return result + "Connection: close\r\n\r\n";
  }
  static void respond(QTcpSocket* socket, const QByteArray& bytes, qint64 offset, qint64 total) {
    socket->write(headers(offset, total));
    socket->write(bytes.mid(offset));
    socket->disconnectFromHost();
  }
};

static QVariantMap run_download(RegistryFixture& registry, const QString& root, int timeout = 1000,
    std::function<qint64(const QString&)> available_bytes = {}) {
  auto state = std::make_shared<AgentModelDownloadState>();
  const AgentModelDownloadRequest request{root, registry.pin, true, timeout, std::move(available_bytes)};
  auto future = QtConcurrent::run([request, state] { return download_agent_model_artifact(request, state); });
  if (!wait_for([&] { return future.isFinished(); })) {
    state->canceled.store(true);
    wait_for([&] { return future.isFinished(); });
    return {{"ok", false}, {"error", "test deadline exceeded"}};
  }
  return future.result();
}

static QVariantMap registry_preflight() {
  // Read-only public network evidence. Deliberately skip the weight blob and
  // never publish a model/store: only manifest/config/license/parameters bytes.
  using namespace agent_model_download_detail;
  const auto pin = pinned_agent_model_artifact("qwen3-vl:32b");
  const AgentModelDownloadRequest request{{}, pin};
  const auto state = std::make_shared<AgentModelDownloadState>();
  QNetworkAccessManager network;
  network.setProxy(QNetworkProxy::NoProxy);
  const QUrl source(pin.source);
  QByteArray manifest;
  auto error = transfer(network, source, source, request, state, pin.files.first().bytes, 0,
      [] { return false; }, [&](const QByteArray& chunk) { manifest += chunk; return true; });
  if (!error.isEmpty() || !manifest_matches(manifest, pin)) return {{"ok", false}, {"error", error.isEmpty() ? "manifest pin mismatch" : error}};
  qint64 bytes_read = manifest.size();
  for (qsizetype i = 2; i < pin.files.size(); ++i) {
    const auto& file = pin.files[i];
    auto url = source;
    url.setPath(source.path().left(source.path().lastIndexOf("/manifests/")) + "/blobs/sha256:" + QString::fromLatin1(file.sha256));
    QByteArray bytes;
    error = transfer(network, url, source, request, state, file.bytes, 0,
        [] { return false; }, [&](const QByteArray& chunk) { bytes += chunk; return true; });
    if (!error.isEmpty() || QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != file.sha256)
      return {{"ok", false}, {"error", error.isEmpty() ? "small blob pin mismatch" : error}};
    bytes_read += bytes.size();
    if (i == pin.files.size() - 1) {
      const auto expected = bytes;
      bytes = bytes.left(5);
      error = transfer(network, url, source, request, state, file.bytes, 5,
          [&] { bytes.clear(); return true; }, [&](const QByteArray& chunk) { bytes += chunk; bytes_read += chunk.size(); return true; });
      if (!error.isEmpty() || bytes != expected) return {{"ok", false}, {"error", error.isEmpty() ? "resumed blob mismatch" : error}};
    }
  }
  return {{"ok", true}, {"manifestSha256", QString::fromLatin1(pin.files.first().sha256)},
      {"publicBytesRead", bytes_read}, {"smallBlobsVerified", 3}, {"rangeRetryVerified", true},
      {"weightsDownloaded", false}, {"installAttempted", false}};
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().contains("--registry-preflight")) {
    auto future = QtConcurrent::run(registry_preflight);
    if (!wait_for([&] { return future.isFinished(); }, 180000)) return EXIT_FAILURE;
    const auto result = future.result();
    std::cout << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).constData() << '\n';
    return result.value("ok").toBool() ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  RegistryFixture registry;
  if (!registry.start()) { std::cerr << "fixture requires loopback permission\n"; return EXIT_FAILURE; }
  auto require = [](bool ok, const char* message) {
    if (!ok) std::cerr << message << '\n';
    return ok;
  };
  {
    QTemporaryDir root;
    registry.mode = "normal";
    const auto full = agent_model_download_space_plan(root.path(), registry.pin);
    if (!require(full.value("estimatedCachedBytes").toLongLong() == 0 &&
          full.value("requiredFreeBytes").toLongLong() == registry.pin.downloadBytes() + 1073741824LL,
          "cold storage plan must reserve all payload bytes and headroom")) return EXIT_FAILURE;
    const auto before = registry.requests.size();
    const auto insufficient = full.value("requiredFreeBytes").toLongLong() - 1;
    const auto rejected = run_download(registry, root.path(), 1000, [insufficient](const QString&) { return insufficient; });
    if (!require(!rejected.value("ok").toBool() && rejected.value("error").toString().contains("Free space") &&
          registry.requests.size() == before, "insufficient storage must reject before network transfer")) return EXIT_FAILURE;
    QDir(root.path()).mkdir("blobs");
    const auto partial = QDir(root.path()).filePath("blobs/.framehouse-" + QString::fromLatin1(registry.pin.files[1].sha256) + ".part");
    const auto prefix = registry.blobs.value(registry.pin.files[1].sha256).left(262144);
    if (!write_file(partial, prefix)) return EXIT_FAILURE;
    const auto resumed = agent_model_download_space_plan(root.path(), registry.pin);
    const auto resume_budget = resumed.value("requiredFreeBytes").toLongLong() + 100;
    if (!require(resumed.value("estimatedCachedBytes").toLongLong() == prefix.size() &&
          resumed.value("cacheEstimateRequiresVerification").toBool() && resume_budget < full.value("requiredFreeBytes").toLongLong(),
          "resume plan must credit only the existing partial size without claiming verification")) return EXIT_FAILURE;
    if (!require(run_download(registry, root.path(), 1000, [resume_budget](const QString&) { return resume_budget; }).value("ok").toBool() &&
          verify_agent_model_artifact(root.path(), registry.pin).value("ok").toBool(),
          "valid partial download must resume with less than full-model free space")) return EXIT_FAILURE;
    const auto complete = agent_model_download_space_plan(root.path(), registry.pin);
    if (!require(complete.value("estimatedRemainingBytes").toLongLong() == registry.pin.files.first().bytes,
          "complete cached blobs still need an atomic manifest allocation")) return EXIT_FAILURE;
  }
  {
    QTemporaryDir root;
    registry.mode = "normal";
    QDir(root.path()).mkdir("blobs");
    const auto path = QDir(root.path()).filePath(registry.pin.files[1].relative_path);
    if (!write_file(path, QByteArray(registry.pin.files[1].bytes, '!'))) return EXIT_FAILURE;
    const auto estimated = agent_model_download_space_plan(root.path(), registry.pin);
    const auto budget = estimated.value("requiredFreeBytes").toLongLong() + 100;
    const auto before = registry.requests.size();
    const auto rejected = run_download(registry, root.path(), 1000, [budget](const QString&) { return budget; });
    if (!require(!rejected.value("ok").toBool() && rejected.value("error").toString().contains("repair") &&
          registry.requests.size() == before + 1 && QFileInfo::exists(path) &&
          !QFileInfo::exists(QDir(root.path()).filePath(registry.pin.files.first().relative_path)),
          "corrupt complete cache must revise replacement space before any blob request")) return EXIT_FAILURE;
  }
  {
    QTemporaryDir root;
    registry.mode = "normal";
    QDir(root.path()).mkdir("blobs");
    for (qsizetype i = 1; i < registry.pin.files.size(); ++i) {
      const auto& file = registry.pin.files[i];
      if (!write_file(QDir(root.path()).filePath(file.relative_path), registry.blobs.value(file.sha256))) return EXIT_FAILURE;
    }
    int checks = 0;
    const auto lost_disk = run_download(registry, root.path(), 1000, [&](const QString&) -> qint64 {
      return ++checks == 1 ? 2LL * 1073741824LL : -1;
    });
    if (!require(!lost_disk.value("ok").toBool() && lost_disk.value("error").toString().contains("unavailable") && checks == 2 &&
          !QFileInfo::exists(QDir(root.path()).filePath(registry.pin.files.first().relative_path)),
          "disk availability must be rechecked before manifest publication")) return EXIT_FAILURE;
  }
  std::cout << "storage budget: cold admission, partial credit, corruption repair and precommit disk loss pass\n";
  {
    QTemporaryDir root;
    registry.mode = "bad-manifest";
    const auto start = registry.requests.size();
    const auto result = run_download(registry, root.path());
    if (!require(!result.value("ok").toBool() && registry.requests.size() == start + 1 &&
          !QFileInfo::exists(QDir(root.path()).filePath("blobs")), "changed manifest must fail before any blob request")) return EXIT_FAILURE;
  }
  {
    QTemporaryDir root;
    const auto before = registry.requests.size();
    auto state = std::make_shared<AgentModelDownloadState>();
    state->canceled.store(true);
    const auto canceled = download_agent_model_artifact({root.path(), registry.pin, true}, state);
    if (!require(canceled.value("canceled").toBool() && registry.requests.size() == before,
          "pre-start cancellation must make no requests")) return EXIT_FAILURE;
    state = std::make_shared<AgentModelDownloadState>();
    auto invalid = registry.pin;
    invalid.files[1].relative_path = "../outside";
    if (!require(!download_agent_model_artifact({root.path(), invalid, true}, state).value("ok").toBool() &&
          registry.requests.size() == before, "invalid pin paths must fail before network or filesystem mutations")) return EXIT_FAILURE;
    QLockFile lock(QDir(root.path()).filePath(".framehouse-model-download.lock"));
    if (!lock.tryLock(0) || !require(!download_agent_model_artifact({root.path(), registry.pin, true}, state).value("ok").toBool() &&
          registry.requests.size() == before, "concurrent store ownership must reject before network requests")) return EXIT_FAILURE;
  }
  for (const auto& mode : {"normal", "interrupt", "ignore-range", "bad-range", "bad-hash", "oversize",
       "chunked-oversize", "encoded", "unsafe-redirect", "redirect-loop", "stall"}) {
    QTemporaryDir root;
    registry.mode = QString::fromLatin1(mode);
    registry.interrupted = false;
    const auto partial = QDir(root.path()).filePath("blobs/.framehouse-" + QString::fromLatin1(registry.pin.files[1].sha256) + ".part");
    if (registry.mode == "ignore-range" || registry.mode == "bad-range") {
      QDir(root.path()).mkdir("blobs");
      if (!write_file(partial, registry.blobs.value(registry.pin.files[1].sha256).left(4096))) return EXIT_FAILURE;
    }
    auto result = run_download(registry, root.path(), 150);
    if (registry.mode == "interrupt") {
      if (!require(!result.value("ok").toBool() && QFileInfo(partial).size() == 128 * 1024 &&
            !QFileInfo::exists(QDir(root.path()).filePath(registry.pin.files.first().relative_path)), "interruption must retain partial data without publishing manifest")) return EXIT_FAILURE;
      const auto resumed_start = registry.ranges.size();
      result = run_download(registry, root.path());
      if (!require(registry.ranges.mid(resumed_start).contains("bytes=131072-524287"), "retry must request the remaining pinned byte range")) return EXIT_FAILURE;
    }
    const bool should_pass = registry.mode == "normal" || registry.mode == "interrupt" || registry.mode == "ignore-range";
    if (!require(result.value("ok").toBool() == should_pass, mode)) {
      std::cerr << result.value("error").toString().toStdString() << '\n';
      return EXIT_FAILURE;
    }
    if (should_pass) {
      if (!require(verify_agent_model_artifact(root.path(), registry.pin).value("ok").toBool(), "downloaded artifact must pass the independent full verifier")) return EXIT_FAILURE;
      const auto before = registry.requests.size();
      if (!require(run_download(registry, root.path()).value("ok").toBool() && registry.requests.size() == before + 1,
            "existing verified blobs must be reused without network blob transfers")) return EXIT_FAILURE;
    } else if (!require(!QFileInfo::exists(QDir(root.path()).filePath(registry.pin.files.first().relative_path)), "failed transfer must not publish its manifest")) return EXIT_FAILURE;
    if (!require(QFileInfo(partial).size() <= registry.pin.files[1].bytes, "partial cache must stay within the pinned byte budget")) return EXIT_FAILURE;
    if (registry.mode == "bad-hash" && !require(!QFileInfo::exists(partial), "bad-hash app-owned partial must be discarded for explicit retry")) return EXIT_FAILURE;
    std::cout << "download transport: " << mode << " pass\n";
  }
  {
    QTemporaryDir root;
    registry.mode = "normal";
    QDir(root.path()).mkdir("blobs");
    const auto destination = QDir(root.path()).filePath(registry.pin.files[1].relative_path);
    if (!write_file(destination, QByteArray("previous corrupt file"))) return EXIT_FAILURE;
    const auto result = run_download(registry, root.path());
    const auto preserved = result.value("quarantinedPaths").toStringList();
    if (!require(result.value("ok").toBool() && preserved.size() == 1 && QFileInfo::exists(preserved.first()) &&
          verify_agent_model_artifact(root.path(), registry.pin).value("ok").toBool(), "repair must preserve the corrupt prior file and publish verified replacement")) return EXIT_FAILURE;
  }
  {
    QTemporaryDir root;
    QTemporaryDir outside;
    registry.mode = "normal";
    if (!QFile::link(outside.path(), QDir(root.path()).filePath("blobs"))) return EXIT_FAILURE;
    const auto before = registry.requests.size();
    if (!require(!run_download(registry, root.path()).value("ok").toBool() && registry.requests.size() == before + 1 &&
          QDir(outside.path()).entryList(QDir::Files).isEmpty(), "symlink directory must reject writes before blob transfer")) return EXIT_FAILURE;
  }
  {
    QTemporaryDir root;
    registry.mode = "slow";
    const auto partial = QDir(root.path()).filePath("blobs/.framehouse-" + QString::fromLatin1(registry.pin.files[1].sha256) + ".part");
    ManagedAgentModelDownload download;
    bool done = false;
    bool ui_callbacks = true;
    QVariantMap result;
    download.on_progress = [&](const QVariantMap&) { ui_callbacks &= QThread::currentThread() == app.thread(); };
    download.on_finished = [&](const QVariantMap& value) { ui_callbacks &= QThread::currentThread() == app.thread(); result = value; done = true; };
    QElapsedTimer launch;
    launch.start();
    download.start({root.path(), registry.pin, true});
    if (!require(launch.elapsed() < 100 && wait_for([&] { return QFileInfo(partial).size() >= 65536; }), "native downloader must start asynchronously and stream partial data")) return EXIT_FAILURE;
    download.cancel();
    if (!require(wait_for([&] { return done; }) && result.value("canceled").toBool() && !result.value("ok").toBool() && ui_callbacks &&
          wait_for([&] { return registry.active_sockets == 0; }), "pause must stop the actual network request and publish on the UI thread")) return EXIT_FAILURE;
    registry.mode = "normal";
    if (!require(run_download(registry, root.path()).value("ok").toBool(), "explicit retry after pause must resume and verify")) return EXIT_FAILURE;
  }
  {
    QTemporaryDir root;
    registry.mode = "stall";
    auto* download = new ManagedAgentModelDownload;
    bool callback_after_destroy = false;
    download->on_finished = [&](const QVariantMap&) { callback_after_destroy = true; };
    const auto before = registry.requests.size();
    download->start({root.path(), registry.pin, true});
    if (!wait_for([&] { return registry.requests.size() >= before + 2; })) return EXIT_FAILURE;
    QElapsedTimer destroy;
    destroy.start();
    delete download;
    if (!require(destroy.elapsed() < 100 && wait_for([&] { return registry.active_sockets == 0; }) && !callback_after_destroy,
          "destruction must cancel without blocking or late controller callbacks")) return EXIT_FAILURE;
  }
  std::cout << "pinned downloader: preflight, exact hashes, bounded transport, resume, repair, cancellation and lifetime pass\n";
  return EXIT_SUCCESS;
}

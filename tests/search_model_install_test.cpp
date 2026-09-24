#include "search_model_install.h"
#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

using namespace photo_editor::desktop;
namespace {
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
bool write(const QString& path, const QByteArray& data) {
  QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
bool wait(auto predicate, int timeout = 15000) {
  QElapsedTimer elapsed; elapsed.start();
  while (!predicate() && elapsed.elapsed() < timeout) { QCoreApplication::processEvents(); QThread::msleep(1); }
  return predicate();
}
QVariantMap run(const SearchModelInstallRequest& request, const AgentModelArtifact& pin = {},
               std::shared_ptr<AgentModelDownloadState> state = std::make_shared<AgentModelDownloadState>()) {
  auto future = QtConcurrent::run([request, pin, state] { return install_search_model(request, state, pin); });
  if (!wait([&] { return future.isFinished(); }, 60000)) return {{"ok", false}, {"error", "fixture timeout"}};
  return future.result();
}
struct Fixture {
  QTemporaryDir parent, source;
  QTcpServer server;
  AgentModelArtifact pin;
  QHash<QByteArray, QByteArray> data;
  QString mode;
  int requests = 0;
  bool range_seen = false;
  bool start() {
    if (!parent.isValid() || !source.isValid() || !server.listen(QHostAddress::LocalHost, 0)) return false;
    pin = {"fixture", "Apache-2.0", QString("http://127.0.0.1:%1/resolve/pinned").arg(server.serverPort()), {}};
    data.insert("config.json", "config"); data.insert("model.safetensors", QByteArray(256 * 1024, 'w'));
    for (const QByteArray& name : {QByteArray("config.json"), QByteArray("model.safetensors")}) {
      const auto bytes = data.value(name);
      pin.files.append({QString::fromUtf8(name), QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(), bytes.size()});
      if (!write(source.filePath(QString::fromUtf8(name)), bytes)) return false;
    }
    QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
      while (auto* socket = server.nextPendingConnection()) {
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
          const auto input = socket->property("input").toByteArray() + socket->readAll();
          socket->setProperty("input", input);
          if (!input.contains("\r\n\r\n") || socket->property("handled").toBool()) return;
          socket->setProperty("handled", true); ++requests;
          const auto name = input.split(' ').value(1).split('/').last();
          auto bytes = data.value(name);
          qint64 offset = 0;
          for (const auto& line : input.split('\n')) if (line.toLower().startsWith("range:")) {
            range_seen = true;
            if (mode != "ignore-range") offset = line.mid(line.indexOf('=') + 1).split('-').first().toLongLong();
          }
          if (mode == "redirect") {
            socket->write("HTTP/1.1 302 Found\r\nLocation: http://localhost:1/forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
          } else if (mode == "stall") {
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(bytes.size()) + "\r\n\r\n");
            return;
          } else {
            if (mode == "bad") bytes[0] = '!';
            QByteArray headers = offset > 0 ? "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " +
                QByteArray::number(offset) + "-" + QByteArray::number(bytes.size() - 1) + "/" + QByteArray::number(bytes.size()) + "\r\n"
                : QByteArray("HTTP/1.1 200 OK\r\n");
            headers += "Content-Length: " + QByteArray::number(bytes.size() - offset) + "\r\nConnection: close\r\n\r\n";
            const auto body = bytes.mid(offset);
            socket->write(headers + (mode == "interrupt" ? body.left(body.size() / 2) : body));
          }
          socket->disconnectFromHost();
        });
      }
    });
    return true;
  }
  QString root() const { return QDir(QFileInfo(parent.path()).canonicalFilePath()).filePath("siglip2-base-patch16-224"); }
  SearchModelInstallRequest request(bool copy = false) const {
    return {root(), copy ? source.path() : QString(), !copy, true, 1000, [](const QString&) { return 8LL * 1024 * 1024 * 1024; }};
  }
};
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().size() == 3 && app.arguments().at(1) == "--live-config-repair") {
    const auto root = app.arguments().at(2);
    const auto pin = pinned_search_model_artifact();
    if (!verify_agent_model_artifact(root, pin).value("ok").toBool() ||
        !QFileInfo::exists(QDir(root).filePath(".framehouse-search-store.json")))
      return fail("network proof needs an already verified isolated managed store");
    const auto config = QDir(root).filePath("config.json");
    const auto backup = config + ".proof-backup-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QFile::rename(config, backup)) return fail("could not preserve isolated config fixture");
    auto result = run({root, {}, true});
    if (!result.value("ok").toBool()) {
      if (!QFileInfo::exists(config)) QFile::rename(backup, config);
      std::cerr << result.value("error").toString().toStdString(); return 1;
    }
    if (result.value("reusedFiles").toInt() != 7) return fail("network proof unexpectedly replaced other artifacts");
    result.insert("downloadedConfigBytes", 253); result.insert("preservedConfigBackup", backup);
    std::cout << QJsonDocument::fromVariant(result).toJson().constData(); return 0;
  }
  if (app.arguments().size() == 4 && app.arguments().at(1) == "--live-copy") {
    const auto source = app.arguments().at(2), root = app.arguments().at(3);
    if (QFileInfo::exists(root)) return fail("live output must be a new managed-model directory");
    const auto before = verify_agent_model_artifact(source, pinned_search_model_artifact());
    if (!before.value("ok").toBool()) return fail("live source pin failed");
    int ticks = 0; QTimer timer; timer.start(10);
    QObject::connect(&timer, &QTimer::timeout, [&] { ++ticks; });
    QElapsedTimer elapsed; elapsed.start();
    auto result = run({root, source});
    const auto milliseconds = elapsed.elapsed();
    if (!result.value("ok").toBool()) { std::cerr << result.value("error").toString().toStdString(); return 1; }
    const auto again = run({root, source});
    const auto after = verify_agent_model_artifact(source, pinned_search_model_artifact());
    if (!again.value("ok").toBool() || again.value("reusedFiles").toInt() != 8 ||
        !after.value("ok").toBool() || before.value("fingerprint") != after.value("fingerprint")) return fail("live reuse/source-preservation failed");
    result.insert("copyMilliseconds", milliseconds); result.insert("heartbeatTicks", ticks);
    result.insert("reusedOnRetry", 8); result.insert("sourceUnchanged", true);
    std::cout << QJsonDocument::fromVariant(result).toJson().constData(); return 0;
  }
  {
    Fixture f; if (!f.start()) return fail("localhost fixture failed");
    auto request = f.request(); request.download_confirmed = false;
    if (run(request, f.pin).value("ok").toBool() || QFileInfo::exists(f.root()) || f.requests) return fail("download occurred without confirmation");
    auto result = run(f.request(true), f.pin);
    if (!result.value("ok").toBool() || f.requests != 0) {
      std::cerr << QJsonDocument::fromVariant(result).toJson().constData(); return fail("offline local import failed");
    }
    result = run(f.request(true), f.pin);
    if (!result.value("ok").toBool() || result.value("reusedFiles").toInt() != 2) return fail("retry did not reuse verified model");
    if (!write(QDir(f.root()).filePath("model.safetensors"), QByteArray(256 * 1024, 'x'))) return fail("corrupt fixture failed");
    result = run(f.request(true), f.pin);
    if (!result.value("ok").toBool() || result.value("quarantinedPaths").toStringList().size() != 1) return fail("repair did not preserve corrupt original");
    QLockFile held(QDir(f.root()).filePath(".framehouse-search-install.lock"));
    if (!held.tryLock() || run(f.request(true), f.pin).value("ok").toBool()) return fail("concurrent installer admitted");
  }
  for (const auto& mode : {QString("resume"), QString("ignore-range")}) {
    Fixture f; if (!f.start()) return fail("localhost fixture failed");
    f.mode = "interrupt";
    if (run(f.request(), f.pin).value("ok").toBool()) return fail("truncated download admitted");
    f.mode = mode;
    if (!run(f.request(), f.pin).value("ok").toBool() || !f.range_seen) return fail("range retry failed");
  }
  for (const auto& mode : {QString("bad"), QString("redirect")}) {
    Fixture f; if (!f.start()) return fail("localhost fixture failed"); f.mode = mode;
    if (run(f.request(), f.pin).value("ok").toBool() || QFileInfo::exists(QDir(f.root()).filePath("config.json")))
      return fail("unsafe or corrupt download was published");
    f.mode.clear(); if (!run(f.request(), f.pin).value("ok").toBool()) return fail("failed download was not recoverable");
  }
  {
    Fixture f; if (!f.start()) return fail("localhost fixture failed"); f.mode = "stall";
    auto state = std::make_shared<AgentModelDownloadState>();
    QTimer cancel; cancel.setInterval(10); cancel.start();
    QObject::connect(&cancel, &QTimer::timeout, [&] { if (f.requests) state->canceled.store(true); });
    const auto result = run(f.request(), f.pin, state);
    if (result.value("ok").toBool() || !result.value("canceled").toBool()) return fail("active transfer cancellation failed");
    cancel.stop(); f.mode.clear(); if (!run(f.request(), f.pin).value("ok").toBool()) return fail("canceled setup could not retry");
  }
  {
    Fixture f; if (!f.start()) return fail("localhost fixture failed");
    QDir().mkdir(f.root()); write(QDir(f.root()).filePath("user-data"), "do not alter");
    if (run(f.request(true), f.pin).value("ok").toBool()) return fail("unowned directory overwritten");
  }
  {
    Fixture f; if (!f.start()) return fail("localhost fixture failed");
    auto request = f.request(true); request.available_bytes_for_fixture = [](const QString&) { return 0LL; };
    if (run(request, f.pin).value("ok").toBool() || QFileInfo::exists(QDir(f.root()).filePath("config.json"))) return fail("disk budget bypassed");
  }
  {
    Fixture f; if (!f.start()) return fail("localhost fixture failed");
    const auto config = f.source.filePath("config.json"), saved = f.source.filePath("saved-config");
    if (!QFile::rename(config, saved) || !QFile::link(saved, config)) return fail("source symlink fixture failed");
    if (run(f.request(true), f.pin).value("ok").toBool() || QFileInfo::exists(QDir(f.root()).filePath("config.json")))
      return fail("symlinked source was imported");
    if (!QFile::remove(config) || !QFile::rename(saved, config) || !run(f.request(true), f.pin).value("ok").toBool())
      return fail("source repair retry failed");
    const auto destination = QDir(f.root()).filePath("config.json");
    if (!QFile::remove(destination) || !QFile::link(config, destination)) return fail("target symlink fixture failed");
    if (run(f.request(true), f.pin).value("ok").toBool() || !QFileInfo(destination).isSymLink())
      return fail("symlinked destination modified or admitted");
  }
  {
    Fixture f; if (!f.start()) return fail("localhost fixture failed");
    auto request = f.request(true);
    request.root = QDir(QFileInfo(f.source.path()).canonicalFilePath()).filePath("siglip2-base-patch16-224");
    if (run(request, f.pin).value("ok").toBool() || QFileInfo::exists(request.root)) return fail("copy wrote into source folder");
  }
  const QUrl origin(pinned_search_model_artifact().source);
  for (const auto& url : {QUrl("http://huggingface.co/file"), QUrl("https://evil.hf.co/file"), QUrl("https://huggingface.co:444/file"),
                          QUrl("https://user@huggingface.co/file"), QUrl("https://cas-bridge.xethub.hf.co.evil.com/file")})
    if (search_model_source_allowed(url, origin, false)) return fail("unsafe production redirect allowed");
  return 0;
}

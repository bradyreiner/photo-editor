#include <cstdlib>
#include <iostream>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QtConcurrent>
#include "managed_agent_runtime.h"
#include "agent_runtime_paths.h"

using photo_editor::desktop::ManagedAgentRuntime;

namespace photo_editor::desktop {
struct ManagedAgentRuntimeTestAccess {
  static bool verificationActive(const ManagedAgentRuntime& runtime) {
    return runtime.verification_active_;
  }
  static void queueCompletedVerification(ManagedAgentRuntime& runtime, const QString& store) {
    runtime.model_store_ = store;
    runtime.ready_ = true;
    runtime.installed_models_ = {QStringLiteral("qwen3-vl:32b")};
    const auto generation = runtime.generation_;
    runtime.verification_active_ = true;
    runtime.verification_watcher_.setFuture(QtConcurrent::run([generation] {
      // This models a successful hash whose files disappear before delivery to
      // the UI thread. No production weights or external server are needed.
      return QVariantMap{{"ok", true}, {"model", "qwen3-vl:32b"},
          {"fingerprint", "previous-file-fingerprint"},
          {"generation", QVariant::fromValue(generation)}};
    }));
    runtime.verification_watcher_.waitForFinished();
  }
};
}

static bool wait_for(const std::function<bool()>& condition, int deadline = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (!condition() && timer.elapsed() < deadline) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  return condition();
}

static int fake_server(QCoreApplication& app) {
  const auto store = qEnvironmentVariable("OLLAMA_MODELS");
  const QUrl endpoint(qEnvironmentVariable("OLLAMA_HOST"));
  if (endpoint.host() != QStringLiteral("127.0.0.1") || store.isEmpty() ||
      qEnvironmentVariable("OLLAMA_NO_CLOUD") != QStringLiteral("1") ||
      qEnvironmentVariable("OLLAMA_NOPRUNE") != QStringLiteral("1")) return EXIT_FAILURE;
  QTcpServer server;
  if (!server.listen(QHostAddress::LocalHost, static_cast<quint16>(endpoint.port()))) return EXIT_FAILURE;
  const auto bad_version = store.endsWith(QStringLiteral("bad-version"));
  server.setProperty("transientFailures", store.endsWith(QStringLiteral("warming")) ? 4 : 0);
  if (!store.endsWith(QStringLiteral("unowned-response")))
    std::cerr << "Listening on " << endpoint.authority().toStdString() << " (version 0.33.3)\n" << std::flush;
  QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
    while (auto* socket = server.nextPendingConnection()) {
      QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, bad_version, &server] {
        auto bytes = socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", bytes);
        if (!bytes.contains("\r\n\r\n")) return;
        const auto failures = server.property("transientFailures").toInt();
        if (failures > 0) {
          server.setProperty("transientFailures", failures - 1);
          socket->write("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}");
          socket->disconnectFromHost();
          return;
        }
        const auto body = bytes.startsWith("GET /api/version ")
            ? (bad_version ? QByteArray("{\"version\":\"wrong\"}") : QByteArray("{\"version\":\"0.33.3\"}"))
            : QByteArray("{\"models\":[{\"name\":\"fixture:vision\"}]}");
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                      QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
      });
    }
  });
  if (store.endsWith(QStringLiteral("crash-once"))) {
    QFile marker(QDir(store).filePath(QStringLiteral("already-crashed")));
    if (!marker.exists()) {
      if (!marker.open(QIODevice::WriteOnly)) return EXIT_FAILURE;
      marker.close();
      QTimer::singleShot(1000, &app, [] { QCoreApplication::exit(7); });
    }
  }
  return app.exec();
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto args = app.arguments();
  if (args.contains(QStringLiteral("serve"))) return fake_server(app);
  QTemporaryDir temp;
  if (!temp.isValid()) return EXIT_FAILURE;
  const auto self = QCoreApplication::applicationFilePath();
  const bool live = args.size() == 3 && args[1] == QStringLiteral("--live");
  if (!live) {
    bool stale_ready_published = false;
    ManagedAgentRuntime stale_verification;
    QObject::connect(&stale_verification, &ManagedAgentRuntime::changed, &app, [&] {
      stale_ready_published |= !stale_verification.snapshot().value("verifiedModels").toStringList().isEmpty();
    });
    photo_editor::desktop::ManagedAgentRuntimeTestAccess::queueCompletedVerification(stale_verification, temp.path());
    if (!photo_editor::desktop::ManagedAgentRuntimeTestAccess::verificationActive(stale_verification)) {
      std::cerr << "verification must stay active until its queued completion is consumed\n";
      return EXIT_FAILURE;
    }
    if (!wait_for([&] {
          return !stale_verification.snapshot().value("modelVerification").toMap().isEmpty();
        }) || stale_ready_published ||
        !stale_verification.snapshot().value("verifiedModels").toStringList().isEmpty()) {
      std::cerr << "completed verification must recheck files before publishing readiness\n";
      return EXIT_FAILURE;
    }
    photo_editor::desktop::ManagedAgentRuntimeTestAccess::queueCompletedVerification(stale_verification, temp.path());
    stale_verification.setModelInstalling(QStringLiteral("qwen3-vl:32b"), true);
    if (!wait_for([&] {
          return !photo_editor::desktop::ManagedAgentRuntimeTestAccess::verificationActive(stale_verification);
        }) || stale_ready_published ||
        stale_verification.snapshot().value("modelVerification").toMap().value("qwen3-vl:32b").toMap().value("state").toString() != "downloading") {
      std::cerr << "installation must invalidate a completed but undelivered verification\n";
      return EXIT_FAILURE;
    }
    stale_verification.setModelInstalling(QStringLiteral("qwen3-vl:32b"), false);
    if (!wait_for([&] {
          return stale_verification.snapshot().value("modelVerification").toMap().value("qwen3-vl:32b").toMap().value("state").toString() == "failed";
        })) return EXIT_FAILURE;
    photo_editor::desktop::ManagedAgentRuntimeTestAccess::queueCompletedVerification(stale_verification, temp.path());
    stale_verification.stop();
    if (!wait_for([&] {
          return !photo_editor::desktop::ManagedAgentRuntimeTestAccess::verificationActive(stale_verification);
        }) || stale_ready_published || stale_verification.snapshot().value("ready").toBool()) {
      std::cerr << "stopping must invalidate a queued verification without republishing readiness\n";
      return EXIT_FAILURE;
    }
    std::cout << "verification handoff: missing files, queued completion, install invalidation and stop pass\n";
  }
  ManagedAgentRuntime runtime;
  QElapsedTimer startup;
  startup.start();
  if (!runtime.start(live ? args[2] : self, QDir(temp.path()).filePath(QStringLiteral("models")))) return EXIT_FAILURE;
  if (startup.elapsed() > 100) return EXIT_FAILURE;
  if (!wait_for([&] { return runtime.snapshot().value("ready").toBool(); }, live ? 20000 : 5000)) {
    std::cerr << "runtime failed readiness: " << runtime.snapshot().value("error").toString().toStdString() << '\n';
    return EXIT_FAILURE;
  }
  const auto snapshot = runtime.snapshot();
  if (!snapshot.value("ownsProcess").toBool() || snapshot.value("version").toString() != "0.33.3" ||
      !photo_editor::desktop::agent_runtime_endpoint_valid(snapshot.value("endpoint").toString())) return EXIT_FAILURE;
  if (live && !snapshot.value("installedModels").toStringList().isEmpty()) return EXIT_FAILURE;
  if (!live) {
    runtime.setModelInstalling(QStringLiteral("fixture:vision"), true);
    if (!runtime.snapshot().value("verifiedModels").toStringList().isEmpty() ||
        runtime.snapshot().value("modelVerification").toMap().value("fixture:vision").toMap().value("state").toString() != "downloading")
      return EXIT_FAILURE;
    runtime.setModelInstalling(QStringLiteral("fixture:vision"), false);
    if (!runtime.snapshot().value("verifiedModels").toStringList().isEmpty()) return EXIT_FAILURE;
  }
  runtime.stop();
  if (runtime.snapshot().value("ready").toBool() ||
      !wait_for([&] { return runtime.snapshot().value("state").toString() == "stopped"; })) return EXIT_FAILURE;
  QTcpSocket stopped_probe;
  const QUrl previous_endpoint(snapshot.value("endpoint").toString());
  stopped_probe.connectToHost(previous_endpoint.host(), static_cast<quint16>(previous_endpoint.port()));
  if (stopped_probe.waitForConnected(250)) return EXIT_FAILURE;
  if (live) {
    std::cout << "live bundled runtime: async readiness, empty private store, owned shutdown verified\n";
    return EXIT_SUCCESS;
  }
  QTcpServer external;
  if (!external.listen(QHostAddress::LocalHost, 0)) return EXIT_FAILURE;
  if (runtime.start(self, QDir(temp.path()).filePath("collision"), external.serverPort()) ||
      runtime.snapshot().value("ownsProcess").toBool() || !external.isListening()) return EXIT_FAILURE;
  const auto locked_store = QDir(temp.path()).filePath("locked-store");
  QDir().mkpath(locked_store);
  QLockFile other_owner(QDir(locked_store).filePath(".framehouse-owner.lock"));
  if (!other_owner.tryLock(0) || runtime.start(self, locked_store) ||
      runtime.snapshot().value("ownsProcess").toBool()) return EXIT_FAILURE;
  other_owner.unlock();
  if (!runtime.start(self, QDir(temp.path()).filePath("warming")) ||
      !wait_for([&] { return runtime.snapshot().value("ready").toBool(); })) {
    std::cerr << "startup must tolerate transient health failures within its startup deadline\n";
    return EXIT_FAILURE;
  }
  runtime.stop();
  if (!wait_for([&] { return runtime.snapshot().value("state").toString() == "stopped"; })) return EXIT_FAILURE;
  if (!runtime.start(self, QDir(temp.path()).filePath("bad-version")) ||
      !wait_for([&] { return runtime.snapshot().value("state").toString() == "failed"; }) ||
      runtime.snapshot().value("ready").toBool()) return EXIT_FAILURE;
  if (!wait_for([&] { return !runtime.snapshot().value("ownsProcess").toBool(); })) return EXIT_FAILURE;
  if (!runtime.start(self, QDir(temp.path()).filePath("unowned-response"))) return EXIT_FAILURE;
  wait_for([&] { return runtime.snapshot().value("ready").toBool(); }, 600);
  if (runtime.snapshot().value("ready").toBool()) return EXIT_FAILURE;
  runtime.stop();
  if (!wait_for([&] { return runtime.snapshot().value("state").toString() == "stopped"; })) return EXIT_FAILURE;
  if (!runtime.start(self, QDir(temp.path()).filePath("crash-once")) ||
      !wait_for([&] { return runtime.snapshot().value("ready").toBool(); }) ||
      !wait_for([&] { return runtime.snapshot().value("state").toString() == "failed"; })) return EXIT_FAILURE;
  runtime.retry();
  if (!wait_for([&] { return runtime.snapshot().value("ready").toBool(); })) return EXIT_FAILURE;
  runtime.stop();
  if (!wait_for([&] { return runtime.snapshot().value("state").toString() == "stopped"; })) return EXIT_FAILURE;
  if (!external.isListening()) return EXIT_FAILURE;
  std::cout << "managed runtime: readiness, ownership, collision, bad-version, crash and retry pass\n";
  return EXIT_SUCCESS;
}

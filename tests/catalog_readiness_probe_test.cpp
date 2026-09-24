#include "app_bootstrap.h"
#include "catalog_readiness_probe.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <iostream>

using photo_editor::desktop::AppBootstrap;

static bool probe(QString* error) {
  return photo_editor::desktop::probe_catalog_readiness(error);
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
  return 77;
#else
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  const auto write = [&](const QString& name, const QByteArray& bytes) {
    const auto path = root.filePath(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
  };
  const auto read = [&](const QString& name) {
    QFile file(root.filePath(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
  };
  if (!write("rust/Cargo.toml", "") || !write("reply", "{\"ready\":true}") ||
      !write("resolver-status", "0") || !write("health-status", "0")) return 1;
  const QByteArray script = R"SH(#!/bin/sh
case "$0" in
  *catalog_local_db)
    printf 'resolve\n' >> "$PHOTO_EDITOR_APP_DATA_DIR/calls"
    sleep 0.25
    printf 'postgresql://unit/managed\n'
    exit "$(cat "$PHOTO_EDITOR_APP_DATA_DIR/resolver-status")" ;;
  *)
    printf 'health %s %s\n' "$1" "$PHOTO_EDITOR_DATABASE_URL" >> "$PHOTO_EDITOR_APP_DATA_DIR/calls"
    cat "$PHOTO_EDITOR_APP_DATA_DIR/reply"
    exit "$(cat "$PHOTO_EDITOR_APP_DATA_DIR/health-status")" ;;
esac
)SH";
  for (const auto name : {"catalog_local_db", "catalog_export_jobs"}) {
    const auto path = QStringLiteral("rust/target/debug/") + name;
    if (!write(path, script) || !QFile::setPermissions(root.filePath(path),
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return 1;
  }
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", root.path().toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qunsetenv("PHOTO_EDITOR_DATABASE_URL");
  const auto invoke = [&](QString* error, qint64* admission = nullptr) {
    QElapsedTimer timer;
    timer.start();
    auto future = QtConcurrent::run([&] { return probe(error); });
    if (admission) *admission = timer.elapsed();
    while (!future.isFinished() && timer.elapsed() < 10000) {
      app.processEvents();
      QThread::msleep(1);
    }
    return future.result();
  };
  QJsonArray elapsed_samples;
  int ticks = 0;
  QTimer heartbeat;
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
  heartbeat.start(10);
  qint64 max_admission = 0;
  for (int trial = 0; trial < 5; ++trial) {
    QElapsedTimer elapsed;
    elapsed.start();
    QString error;
    qint64 admission = 0;
    if (!invoke(&error, &admission) || !error.isEmpty()) return 1;
    max_admission = std::max(max_admission, admission);
    elapsed_samples.append(elapsed.elapsed());
  }
  const auto calls = read("calls");
  const auto resolves = calls.count("resolve\n");
  const auto health_calls = calls.count("health health postgresql://unit/managed\n");
  std::cout << QJsonDocument(QJsonObject{{"elapsed_ms", elapsed_samples},
      {"resolutions", resolves}, {"health_calls", health_calls},
      {"admission_ms", max_admission}, {"heartbeat_ticks", ticks}})
                   .toJson(QJsonDocument::Compact).toStdString() << '\n';
  if (health_calls != 5 || max_admission >= 50 || ticks < 20) return 1;
  if (app.arguments().contains("--measure")) return 0;
  if (resolves != 5) return 1;

  // Explicit authority must pass through unchanged and bypass managed startup.
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://unit/explicit");
  write("calls", "");
  QString error;
  if (!invoke(&error) || read("calls") != "health health postgresql://unit/explicit\n") return 1;
  for (const auto reply : {"not-json", "[]", "{\"ready\":false}", "{\"ready\":\"true\"}"}) {
    write("reply", reply);
    error.clear();
    if (invoke(&error) || error.isEmpty()) return 1;
  }
  write("reply", "{\"ready\":true}");
  write("health-status", "1");
  error.clear();
  if (invoke(&error) || error.isEmpty()) return 1;
  qunsetenv("PHOTO_EDITOR_DATABASE_URL");
  write("calls", "");
  write("resolver-status", "1");
  error.clear();
  if (invoke(&error) || error.isEmpty() || read("calls").contains("health")) return 1;
  std::cout << "Readiness authority, failure and worker responsiveness checks passed\n";
  return 0;
#endif
}

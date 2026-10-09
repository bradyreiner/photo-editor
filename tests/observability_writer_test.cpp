#include "app_bootstrap.h"
#include "observability.h"
#include "observability_writer.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif

QString photo_editor::desktop::AppBootstrap::logLocation() {
  return qEnvironmentVariable("PHOTO_EDITOR_OBSERVABILITY_FIXTURE_ROOT");
}

namespace {
namespace logs = photo_editor::desktop::observability;
using Writer = photo_editor::desktop::detail::ObservabilityWriter<QByteArray>;
using namespace std::chrono_literals;
QString intercepted_message;

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
QByteArray read(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly | QIODevice::Text), "Cannot read log");
  return file.readAll();
}
QList<QJsonObject> records(const QString& path) {
  QList<QJsonObject> result;
  for (const auto& line : read(path).split('\n')) {
    if (line.isEmpty()) continue;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(line, &error);
    require(error.error == QJsonParseError::NoError && document.isObject(), "Invalid JSONL record");
    result.push_back(document.object());
  }
  return result;
}
void queue_contract(const QString& path) {
  std::promise<void> entered, release;
  auto released = release.get_future().share();
  std::atomic<bool> correct_thread{true};
  Writer* owner = nullptr;
  Writer writer(path, [&](const QByteArray& value) {
    correct_thread.store(correct_thread.load() && owner->isWriterThread());
    if (value == "0\n") { entered.set_value(); released.wait(); }
    return value;
  }, 192, 3);
  owner = &writer;
  require(writer.statistics().opened, "Writer did not open its file");
  require(writer.submit("0\n", 64, false) == Writer::Result::accepted, "First admission failed");
  require(entered.get_future().wait_for(5s) == std::future_status::ready, "Writer did not enter serializer");
  writer.submit("1\n", 64, false);
  writer.submit("2\n", 64, false);
  auto pending = std::async(std::launch::async, [&] { return writer.submit("3\n", 64, true); });
  QElapsedTimer timer;timer.start();
  while (writer.statistics().backpressure_waits == 0 && timer.elapsed() < 5000) std::this_thread::sleep_for(1ms);
  const auto full = writer.statistics();
  const bool bounded = full.backpressure_waits == 1 && full.pending_events == 3 &&
      full.pending_bytes == 192 && full.peak_bytes == 192;
  auto flush = std::async(std::launch::async, [&] { return writer.flush(); });
  const bool barrier_waited = flush.wait_for(10ms) == std::future_status::timeout;
  release.set_value();
  require(bounded, "Active batch escaped queue budget or backpressure did not block");
  require(barrier_waited && flush.get(), "Flush returned before admitted records were written");
  require(pending.get() == Writer::Result::accepted && writer.flush(), "Blocked admission was lost");
  require(read(path) == "0\n1\n2\n3\n", "FIFO records changed during backpressure");
  require(correct_thread.load(), "Serialization ran on a producer thread");
  require(writer.submit("oversized\n", 4096, true) == Writer::Result::accepted,
          "Single oversized record was discarded or deadlocked");
  require(writer.statistics().peak_bytes == 4096 && writer.statistics().pending_events == 0,
          "Oversized record accounting did not drain");
  writer.shutdown();writer.shutdown();
  QByteArray late("late\n");
  require(writer.submit(std::move(late), 64, false) == Writer::Result::stopped && late == "late\n",
          "Stopped writer consumed a rejected record or accepted new work");
}
void shutdown_contract(const QString& path) {
  std::promise<void> entered, release;
  auto released = release.get_future().share();
  Writer writer(path, [&](const QByteArray& value) { entered.set_value();released.wait();return value; }, 64, 1);
  writer.submit("admitted\n", 64, false);
  require(entered.get_future().wait_for(5s) == std::future_status::ready, "Shutdown fixture did not start");
  auto pending = std::async(std::launch::async, [&] { return writer.submit("rejected\n", 64, false); });
  auto stopped = std::async(std::launch::async, [&] { writer.shutdown(); });
  const bool rejected = pending.wait_for(5s) == std::future_status::ready && pending.get() == Writer::Result::stopped;
  const bool drain_waited = stopped.wait_for(10ms) == std::future_status::timeout;
  release.set_value();stopped.get();
  require(rejected && drain_waited, "Shutdown did not reject new work and wait for admitted work");
  require(read(path) == "admitted\n", "Shutdown lost an admitted record");
}
int child(const QString& root, const QString& mode) {
#if defined(Q_OS_UNIX)
  const rlimit no_core{0, 0};setrlimit(RLIMIT_CORE, &no_core);
#endif
  qputenv("PHOTO_EDITOR_OBSERVABILITY_FIXTURE_ROOT", root.toUtf8());
  auto run = root + "/run";
  if (mode == "failure") {
    QFile blocker(root + "/blocked");require(blocker.open(QIODevice::WriteOnly), "Cannot create blocked path");blocker.close();
    run = root + "/blocked/run";
  }
  qputenv("PHOTO_EDITOR_OBSERVABILITY_RUN_DIR", run.toUtf8());
  qputenv("PHOTO_EDITOR_OBSERVABILITY_CONSOLE", mode == "compact" || mode == "json" || mode == "over_budget" ? mode.toUtf8() : QByteArray("quiet"));
  qputenv("PHOTO_EDITOR_LOG_COLOR", "never");
  qputenv("PHOTO_EDITOR_LOG_LEVEL", "info");
  qputenv("PHOTO_EDITOR_SESSION_ID", "contract-session");
  logs::initialize();
  if (mode == "pattern") qSetMessagePattern("%{threadid}|%{category}|%{function}|%{message}");
  if (mode == "exit" || mode == "fatal") {
    for (int index = 0; index < 5000; ++index)
      logs::logInfo(photoEditorPerfLog(), "preview.exit.fixture", {{"index", index}});
    if (mode == "fatal") qFatal("fatal fixture");
    return 0;  // Intentionally no explicit flush: the exit hook must drain.
  }
  const auto previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
    intercepted_message = message;
  });
  logs::logInfo(photoEditorPerfLog(), "custom.handler.fixture", {{"value", 17}});
  qInstallMessageHandler(previous);
  require(intercepted_message == logs::structuredMessage("custom.handler.fixture", {{"value", 17}}),
          "Prepared event bypassed a custom Qt handler or changed its message");
  for (int index = 0; index < 20; ++index) {
    auto source = QStringLiteral("temporary-source-%1.cpp").arg(index).toUtf8();
    QByteArray function("temporary_function");
    QMessageLogger(source.constData(), 100 + index, function.constData()).info(photoEditorPerfLog())
        .noquote() << QStringLiteral("{\"event\":\"preview.generic.fixture\",\"index\":%1}").arg(index);
    source.fill('z');function.fill('z');
  }
  logs::logInfo(photoEditorPerfLog(), "preview.structured.fixture",
      {{"payload", QString::fromUtf8("café\n\"quoted\"")}, {"full_resolution", false}, {"elapsed_ms", 17.25}});
  QLoggingCategory disabled("photo_editor.disabled.fixture");
  disabled.setEnabled(QtInfoMsg, false);
  logs::logInfo(disabled, "must.not.appear");
  qInfo().noquote() << "not JSON { broken";
  if (mode == "pattern") {
    const QMessageLogContext context("pattern.cpp", 71, "producerPattern", "default");
    QFile expected(root + "/expected-console.txt");
    require(expected.open(QIODevice::WriteOnly), "Cannot save expected producer console format");
    expected.write(qFormatLogMessage(QtWarningMsg, context, "warning delivery barrier").toUtf8());
    expected.close();
    QMessageLogger("pattern.cpp", 71, "producerPattern").warning().noquote() << "warning delivery barrier";
  } else {
    qWarning().noquote() << "warning delivery barrier";
  }
  if (mode == "failure") {
    require(!logs::flush(), "I/O failure was reported as successful delivery");
    return 0;
  }
  require(records(run + "/desktop.jsonl").size() == 23, "Warning returned before previous records were visible");
  QLoggingCategory repeated("qt.logging.fixture");
  for (int index = 0; index < 25; ++index) qCWarning(repeated).noquote() << "repeated warning";
  logs::logInfo(photoEditorPerfLog(), "overridden.event", {{"event", 42}});
  qCritical().noquote() << "critical delivery barrier";
  logs::logWarning(photoEditorAppLog(), "agent.edit.run_failed",
      {{"run_id", "agent-failure-fixture"}, {"model_id", "fixture-model"},
       {"stage", "draft-apply"}, {"reason", "Composition inspection failed: specific fixture reason"},
       {"errors", QStringList{"Composition inspection failed: specific fixture reason"}}});
  logs::logWarning(photoEditorAppLog(), "agent.edit.tool_failed",
      {{"run_id", "agent-failure-fixture"}, {"tool_name", "photo.inspect_composition"},
       {"reason", "Unknown composition argument: fontFamily"}});
  require(logs::flush(), "Explicit flush failed");
  return 0;
}
void process_contract(const QString& root, const QString& mode) {
  const auto dir = root + QLatin1Char('/') + mode;
  QDir().mkpath(dir);
  QProcess process;
  process.start(QCoreApplication::applicationFilePath(), {"--child", dir, mode});
  require(process.waitForStarted(5000) && process.waitForFinished(30000), "Logger child timed out");
  const auto error = process.readAllStandardError();
  if (mode == "fatal") require(process.exitStatus() == QProcess::CrashExit, "Fatal logging did not abort");
  else require(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, error.constData());
  if (mode == "failure") {
    require(error.contains("diagnostic log write failed") && error.contains("preview.structured.fixture"),
            "File failure did not preserve diagnostic content on stderr");
    return;
  }
  const auto lines = records(dir + "/run/desktop.jsonl");
  if (mode == "exit" || mode == "fatal") {
    require(lines.size() == (mode == "fatal" ? 5001 : 5000), "Process exit lost queued records");
    for (int index = 0; index < 5000; ++index)
      require(lines[index].value("fields").toObject().value("index").toInt(-1) == index, "Exit drain changed record order");
    if (mode == "fatal") require(lines.last().value("severity") == "fatal" && lines.last().value("message") == "fatal fixture", "Fatal record was not flushed");
    return;
  }
  require(lines.size() == 52, "Logger lost records or bypassed category suppression");
  require(lines[50].value("severity") == "warning" &&
          lines[50].value("fields").toObject().value("run_id") == "agent-failure-fixture" &&
          lines[50].value("fields").toObject().value("reason") == "Composition inspection failed: specific fixture reason" &&
          lines[51].value("fields").toObject().value("tool_name") == "photo.inspect_composition",
          "The durable log must retain the specific agent failure and tool context");
  require(error.contains("Composition inspection failed: specific fixture reason") &&
          error.contains("Unknown composition argument: fontFamily") &&
          error.contains("agent-failure-fixture"),
          "Agent failure details must remain visible in every console mode");
  for (int index = 0; index < 20; ++index) {
    const auto& record = lines[index];
    require(record.value("file") == QStringLiteral("temporary-source-%1.cpp").arg(index) &&
            record.value("line").toInt() == 100 + index && record.value("function") == "temporary_function",
            "Deferred logging referenced expired source context");
    require(record.value("thread_id") == record.value("fields").toObject().value("thread"), "Generic JSON lost producer thread identity");
  }
  require(lines[20].value("fields").toObject().value("payload") == QString::fromUtf8("café\n\"quoted\""), "Unicode or escaping changed");
  require(lines[21].value("message") == "not JSON { broken", "Plain message became invalid JSON fields");
  require(!lines[48].contains("event") && lines[48].value("fields").toObject().value("event").toInt() == 42,
          "Prepared fields changed overridden non-string event semantics");
  if (mode == "compact" || mode == "json") require(error.contains("\"event\":42"), "Event without a name lost its original console format");
  require(error.count("qt.logging.fixture") == 3, "Warning console rate limiting changed");
  require(error.contains("warning delivery barrier") && error.contains("critical delivery barrier"), "Important console output disappeared");
  if (mode == "pattern") require(error.contains(read(dir + "/expected-console.txt")), "Custom console pattern lost producer thread/context");
  if (mode == "quiet") require(!error.contains("preview.structured.fixture"), "Quiet mode printed routine events");
  if (mode == "compact" || mode == "json") require(error.contains("preview.structured.fixture"), "Console mode lost routine events");
}
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  try {
    if (argc == 4 && QString::fromLocal8Bit(argv[1]) == "--child")
      return child(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    QTemporaryDir root;require(root.isValid(), "Cannot create isolated fixture root");
    queue_contract(root.path() + "/queue.jsonl");
    shutdown_contract(root.path() + "/shutdown.jsonl");
    for (const auto& mode : {"quiet", "compact", "json", "over_budget", "pattern", "exit", "fatal", "failure"})
      process_contract(root.path(), QLatin1String(mode));
    std::cout << "observability_writer_contract_passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';return 1;
  }
}

#include <cstdlib>
#include <iostream>
#include <QCoreApplication>
#include <QProcess>
#include "agent_runtime_paths.h"

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  using namespace photo_editor::desktop;
  if (app.arguments().contains(QStringLiteral("--environment-child"))) {
    std::cout << qEnvironmentVariable("OLLAMA_HOST").toStdString() << '|'
              << qEnvironmentVariable("OLLAMA_NO_CLOUD").toStdString() << '|'
              << qEnvironmentVariable("OLLAMA_DEBUG_LOG_REQUESTS").toStdString();
    return EXIT_SUCCESS;
  }
  for (const auto& value : {"http://127.0.0.1:11435", "http://[::1]:12345/", "http://localhost:11434"}) {
    if (!agent_runtime_endpoint_valid(QString::fromLatin1(value))) return EXIT_FAILURE;
  }
  for (const auto& value : {"http://example.com:11434", "https://localhost:11434", "http://127.0.0.1",
       "http://0.0.0.0:11434", "http://127.0.0.1:0", "http://127.0.0.1:11434/api",
       "http://user:secret@localhost:11434", "http://localhost:11434?target=remote",
       "http://localhost:11434#remote", "http://localhost:65536"}) {
    if (agent_runtime_endpoint_valid(QString::fromLatin1(value))) return EXIT_FAILURE;
  }
  qputenv("OLLAMA_HOST", "https://unrelated.example:443");
  qputenv("OLLAMA_DEBUG_LOG_REQUESTS", "1");
  qputenv("HTTP_PROXY", "http://unrelated.example:8888");
  const auto environment = agent_runtime_client_environment(QStringLiteral("http://127.0.0.1:12345"));
  if (environment.value(QStringLiteral("OLLAMA_HOST")) != QStringLiteral("http://127.0.0.1:12345") ||
      environment.value(QStringLiteral("OLLAMA_NO_CLOUD")) != QStringLiteral("1") ||
      environment.contains(QStringLiteral("OLLAMA_DEBUG_LOG_REQUESTS")) ||
      environment.contains(QStringLiteral("HTTP_PROXY"))) return EXIT_FAILURE;
  if (qEnvironmentVariable("OLLAMA_HOST") != QStringLiteral("https://unrelated.example:443"))
    return EXIT_FAILURE;  // Only the child environment is modified.
  QProcess child;
  child.setProcessEnvironment(environment);
  child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--environment-child")});
  if (!child.waitForStarted(2000) || !child.waitForFinished(2000) || child.exitCode() != 0 ||
      child.readAllStandardOutput() != QByteArray("http://127.0.0.1:12345|1|")) return EXIT_FAILURE;
  qputenv("PHOTO_EDITOR_OLLAMA_BINARY", "/missing/explicit-runtime");
  if (!agent_runtime_binary().isEmpty()) return EXIT_FAILURE;
  qputenv("PHOTO_EDITOR_OLLAMA_BINARY", QCoreApplication::applicationFilePath().toUtf8());
  if (agent_runtime_binary() != QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath())
    return EXIT_FAILURE;
  std::cout << "runtime endpoint, child environment, and explicit binary authority pass\n";
  return EXIT_SUCCESS;
}

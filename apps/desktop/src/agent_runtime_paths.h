#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QReadWriteLock>
#include <QVariantMap>
#include <QStandardPaths>
#include <QUrl>

namespace photo_editor::desktop {

struct ManagedAgentRuntimeRegistry {
  QReadWriteLock lock;
  QVariantMap value;
};
inline ManagedAgentRuntimeRegistry& managed_agent_runtime_registry() {
  static ManagedAgentRuntimeRegistry registry;
  return registry;
}
inline QVariantMap managed_agent_runtime_snapshot() {
  auto& registry = managed_agent_runtime_registry();
  QReadLocker guard(&registry.lock);
  return registry.value;
}
inline void publish_managed_agent_runtime(const QVariantMap& value) {
  auto& registry = managed_agent_runtime_registry();
  QWriteLocker guard(&registry.lock);
  registry.value = value;
}

inline bool agent_runtime_endpoint_valid(const QString& endpoint) {
  const QUrl url(endpoint, QUrl::StrictMode);
  return url.isValid() && url.scheme() == QStringLiteral("http") &&
      (url.host() == QStringLiteral("127.0.0.1") || url.host() == QStringLiteral("localhost") ||
       url.host() == QStringLiteral("::1")) && url.port() > 0 && url.port() <= 65535 &&
      url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment() &&
      (url.path().isEmpty() || url.path() == QStringLiteral("/"));
}

inline QString agent_runtime_binary() {
  const auto executable = [](const QString& path) {
    const QFileInfo info(path);
    return info.isFile() && info.isExecutable() ? info.canonicalFilePath() : QString();
  };
  const auto override_path = qEnvironmentVariable("PHOTO_EDITOR_OLLAMA_BINARY").trimmed();
  // An invalid explicit override is an error, never a silent switch of runtime.
  if (!override_path.isEmpty()) return executable(override_path);
  const auto bundled = executable(QDir(QCoreApplication::applicationDirPath())
      .filePath(QStringLiteral("../Resources/agent-runtime/ollama")));
  if (!bundled.isEmpty()) return bundled;
  return executable(QStandardPaths::findExecutable(QStringLiteral("ollama")));
}

inline QProcessEnvironment agent_runtime_client_environment(const QString& endpoint) {
  auto environment = QProcessEnvironment::systemEnvironment();
  // The selected endpoint is authoritative even if a shell points OLLAMA_HOST
  // elsewhere. Callers validate it before launching any client process.
  environment.insert(QStringLiteral("OLLAMA_HOST"), endpoint);
  environment.insert(QStringLiteral("OLLAMA_NO_CLOUD"), QStringLiteral("1"));
  environment.insert(QStringLiteral("OLLAMA_NOHISTORY"), QStringLiteral("1"));
  environment.remove(QStringLiteral("OLLAMA_DEBUG_LOG_REQUESTS"));
  environment.remove(QStringLiteral("HTTP_PROXY"));
  environment.remove(QStringLiteral("http_proxy"));
  environment.remove(QStringLiteral("ALL_PROXY"));
  environment.remove(QStringLiteral("all_proxy"));
  return environment;
}

}  // namespace photo_editor::desktop

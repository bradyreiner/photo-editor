#pragma once

#include "app_bootstrap.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace photo_editor::desktop {

// Call on a worker: managed resolution and the read probe may block.
inline bool probe_catalog_readiness(QString* error_message = nullptr) {
  if (error_message) error_message->clear();
  if (!AppBootstrap::ensureRuntimeDirectories()) {
    if (error_message) {
      *error_message = QStringLiteral("Unable to create the application runtime directories.");
    }
    return false;
  }
  // runCatalogHelper prepares the environment, including managed service startup.
  // Calling ensureCatalogService first would repeat that blocking helper round trip.
  // An explicit URL bypasses managed startup, but must still pass the read probe.
  QString output;
  if (!AppBootstrap::runCatalogHelper(QStringLiteral("catalog_export_jobs"), {QStringLiteral("health")},
          &output, error_message, 5000)) {
    return false;
  }
  // Leave large job payloads to recovery after Library admission.
  if (!QJsonDocument::fromJson(output.toUtf8()).object()
           .value(QStringLiteral("ready")).toBool()) {
    if (error_message) {
      *error_message = QStringLiteral("Catalog readiness check returned an invalid response.");
    }
    return false;
  }
  return true;
}

}  // namespace photo_editor::desktop

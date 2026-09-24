#pragma once

#include "linked_image_source.h"

#include <QDir>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <functional>

namespace photo_editor::desktop {

inline QStringList linked_photo_layer_source_paths(const QVariantList& layers) {
  QStringList paths;
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("kind")).toString().trimmed().toLower() == QStringLiteral("source")) {
      const auto path = layer.value(QStringLiteral("sourcePath")).toString();
      if (!path.isEmpty()) paths.append(QFileInfo(path).absoluteFilePath());
    }
    paths.append(linked_photo_layer_source_paths(layer.value(QStringLiteral("children")).toList()));
    paths.append(linked_photo_layer_source_paths(layer.value(QStringLiteral("layers")).toList()));
  }
  paths.removeDuplicates();
  return paths;
}

// Owned by the document controller. No decode, recipe mutation or history writes.
// Directory watches survive atomic replacement; bounded polling also recovers
// removed directories and missed/unsupported filesystem notifications.
class LinkedImageSourceMonitor final : public QObject {
 public:
  explicit LinkedImageSourceMonitor(QObject* parent, std::function<void(const QStringList&)> changed)
      : QObject(parent), watcher_(this), debounce_(this), poll_(this), changed_(std::move(changed)) {
    debounce_.setSingleShot(true);
    debounce_.setInterval(150);
    poll_.setInterval(1500);
    const auto schedule = [this](const QString&) {
      if (!debounce_.isActive()) debounce_.start();
    };
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, schedule);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, schedule);
    connect(&debounce_, &QTimer::timeout, this, [this] { check_sources(); });
    connect(&poll_, &QTimer::timeout, this, [this] { check_sources(); });
  }

  void set_paths(const QStringList& paths) {
    QSet<QString> next;
    for (const auto& path : paths) {
      if (!path.isEmpty()) next.insert(QFileInfo(path).absoluteFilePath());
    }
    if (next == paths_) return;
    for (const auto& path : paths_) {
      if (!next.contains(path)) revisions_.remove(path);
    }
    for (const auto& path : next) {
      if (!paths_.contains(path)) revisions_.insert(path, linked_image_file_revision(path));
    }
    paths_ = next;
    debounce_.stop();
    if (paths_.isEmpty()) poll_.stop();
    else if (!poll_.isActive()) poll_.start();
    rebuild_watches();
  }

  [[nodiscard]] QStringList paths() const { return paths_.values(); }

 private:
  void rebuild_watches() {
    QSet<QString> desired;
    for (const auto& path : paths_) {
      const QFileInfo file(path);
      if (file.isFile()) desired.insert(path);
      auto directory = file.absoluteDir();
      while (!directory.exists()) {
        const auto parent = QFileInfo(directory.absolutePath()).absolutePath();
        if (parent == directory.absolutePath()) break;
        directory = QDir(parent);
      }
      if (directory.exists()) desired.insert(directory.absolutePath());
    }
    const auto watched = watcher_.files() + watcher_.directories();
    const QSet<QString> current(watched.begin(), watched.end());
    const auto removed = current - desired;
    const auto added = desired - current;
    if (!removed.isEmpty()) watcher_.removePaths(removed.values());
    if (!added.isEmpty()) watcher_.addPaths(added.values());
  }

  void check_sources() {
    QStringList changed;
    for (const auto& path : paths_) {
      const auto revision = linked_image_file_revision(path);
      if (revisions_.value(path) != revision) {
        revisions_.insert(path, revision);
        changed.append(path);
      }
    }
    rebuild_watches();
    if (!changed.isEmpty()) changed_(changed);
  }

  QFileSystemWatcher watcher_;
  QTimer debounce_;
  QTimer poll_;
  QSet<QString> paths_;
  QHash<QString, QString> revisions_;
  std::function<void(const QStringList&)> changed_;
};

}  // namespace photo_editor::desktop

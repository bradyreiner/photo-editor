#pragma once

#include "agent_task_journal.h"

#include <QFileInfo>
#include <QJsonParseError>

namespace photo_editor::desktop {

// Owns one newly created output, never arbitrary files beneath a directory.
// Enroll snapshot() while absent, then authenticate the producer's completion
// receipt before checkpointing it. Restoration waits for the actual producer.
class AgentCreatedFileResource final {
 public:
  AgentCreatedFileResource(QString root, QString path, std::function<void()> cancel,
                          std::function<bool()> quiescent)
      : root_(QDir::cleanPath(root)), path_(QDir::cleanPath(path)),
        cancel_(std::move(cancel)), quiescent_(std::move(quiescent)) {}

  [[nodiscard]] QByteArray snapshot() const {
    if (!validPath()) return {};
    const QFileInfo info(path_);
    QJsonObject state{{"contract", "agent-created-file-v1"}, {"path", path_},
        {"exists", info.exists()}};
    if (info.exists()) {
      if (!info.isFile() || info.isSymLink()) return {};
      QFile file(path_);
      if (!file.open(QIODevice::ReadOnly)) return {};
      QCryptographicHash digest(QCryptographicHash::Sha256);
      if (!digest.addData(&file)) return {};
      state.insert("sha256", QString::fromLatin1(digest.result().toHex()));
      state.insert("size", file.size());
    }
    return QJsonDocument(state).toJson(QJsonDocument::Compact);
  }

  // This authorizes an exact native receipt, not whatever happens to be on disk.
  [[nodiscard]] bool recordCreatedReceipt(const QString& sha256) {
    if (!QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(sha256).hasMatch()) return false;
    const auto bytes = snapshot();
    const auto state = QJsonDocument::fromJson(bytes).object();
    if (!state.value("exists").toBool() || state.value("sha256").toString() != sha256) return false;
    if (!authored_.isEmpty() && authored_ != bytes) return false;
    authored_ = bytes;
    return true;
  }

  [[nodiscard]] AgentTaskJournal::ResourceAdapter adapter() {
    return {[this] { return snapshot(); },
        [this](const QByteArray& baseline) { return restoreAbsent(baseline); }, cancel_};
  }

 private:
  [[nodiscard]] bool validPath() const {
    if (!QDir(root_).isAbsolute() || !QDir(path_).isAbsolute() ||
        !path_.startsWith(root_ + QDir::separator()) || path_ == root_) return false;
    const QFileInfo root_info(root_);
    if (root_info.isSymLink() || (root_info.exists() && !root_info.isDir())) return false;
    // Reject symlinked components, including dangling links, on every read and
    // restore. A replaced parent must not redirect deletion to another output.
    auto current = root_;
    for (const auto& component : QDir(root_).relativeFilePath(path_).split(QDir::separator())) {
      if (component.isEmpty() || component == "." || component == "..") return false;
      current = QDir(current).filePath(component);
      const QFileInfo info(current);
      if (info.isSymLink()) return false;
      if (current != path_ && info.exists() && !info.isDir()) return false;
    }
    return true;
  }

  [[nodiscard]] bool restoreAbsent(const QByteArray& baseline) {
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(baseline, &error);
    const auto before = doc.object();
    if (error.error != QJsonParseError::NoError || !doc.isObject() ||
        before.value("contract") != "agent-created-file-v1" || before.value("path") != path_ ||
        !before.contains("exists") || before.value("exists").toBool() ||
        !validPath() || !quiescent_ || !quiescent_()) return false;
    const auto current = snapshot();
    if (current.isEmpty()) return false;
    if (!QJsonDocument::fromJson(current).object().value("exists").toBool()) return true;
    if (authored_.isEmpty() || current != authored_) return false;
    if (!QFile::remove(path_)) return false;
    // Only the exact empty output folder may be removed. Other files survive.
    const auto parent = QFileInfo(path_).dir().absolutePath();
    if (parent != root_) static_cast<void>(QDir().rmdir(parent));
    return snapshot() == baseline;
  }

  QString root_;
  QString path_;
  QByteArray authored_;
  std::function<void()> cancel_;
  std::function<bool()> quiescent_;
};

}  // namespace photo_editor::desktop

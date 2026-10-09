#pragma once

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QVariantMap>
#include <algorithm>
#include <functional>

namespace photo_editor::desktop {

// Stage the complete delivery together, then publish without replacing files.
// The receipt can later support task reversal; rollback preserves files changed
// outside this worker after publication. This is not a durable task journal.
class JobOutputTransaction final {
 public:
  explicit JobOutputTransaction(const QString& output_path)
      : directory_(QFileInfo(output_path).absolutePath()),
        stage_(QDir(directory_).filePath(QStringLiteral(".looksy-delivery-XXXXXX"))) {}
  bool valid() const { return stage_.isValid(); }
  QString stagedPath(const QString& name) const { return QDir(stage_.path()).filePath(name); }

  static QString digest(const QString& path, const std::function<bool()>& canceled = {}) {
    const QFileInfo info(path);
    if (!info.exists() || info.isSymLink()) return {};
    QStringList files;
    if (info.isDir()) {
      QDirIterator entries(path, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
          QDirIterator::Subdirectories);
      while (entries.hasNext()) {
        if (canceled && canceled()) return {};
        entries.next();
        if (entries.fileInfo().isSymLink()) return {};
        files.append(entries.filePath());
      }
      std::sort(files.begin(), files.end());
    } else files.append(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(info.isDir() ? QByteArray("directory\n") : QByteArray("file\n"));
    for (const auto& file_path : files) {
      if (canceled && canceled()) return {};
      const QFileInfo file_info(file_path);
      const auto name = QDir(path).relativeFilePath(file_path).toUtf8();
      hash.addData(QByteArray::number(name.size()) + ':' + name + ':' + QByteArray::number(file_info.size()) + ':');
      if (file_info.isDir()) { hash.addData(QByteArray("directory\n")); continue; }
      QFile file(file_path);
      if (!file.open(QIODevice::ReadOnly)) return {};
      while (!file.atEnd()) {
        if (canceled && canceled()) return {};
        const auto chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) return {};
        hash.addData(chunk);
      }
      hash.addData(QByteArray("\0", 1));
    }
    return QString::fromLatin1(hash.result().toHex());
  }

  QVariantMap publish(const std::function<bool()>& canceled = {}) {
    if (!valid()) return failure("Unable to stage the timeline delivery.");
    const auto names = QDir(stage_.path()).entryList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    if (names.isEmpty()) return failure("The timeline renderer produced no artifacts.");
    QVariantList planned;
    for (const auto& name : names) {
      if (canceled && canceled()) return failure("Timeline delivery canceled.");
      const auto source = stagedPath(name);
      const auto target = QDir(directory_).filePath(name);
      if (QFileInfo::exists(target) || QFileInfo(target).isSymLink())
        return failure("A delivery file already exists. Its contents were preserved.");
      const auto hash = digest(source, canceled);
      if (hash.isEmpty()) return failure("Delivery verification was interrupted or an artifact could not be read.");
      planned.append(QVariantMap{{"path", target}, {"name", name}, {"sha256", hash}, {"directory", QFileInfo(source).isDir()}});
    }
    for (const auto& item : planned) {
      const auto artifact = item.toMap();
      const auto source = stagedPath(artifact.value("name").toString());
      const auto target = artifact.value("path").toString();
      if ((canceled && canceled()) || QFileInfo::exists(target) || QFileInfo(target).isSymLink() ||
          !(artifact.value("directory").toBool() ? QDir().rename(source, target) : QFile::rename(source, target))) {
        const auto restored = rollback();
        auto result = failure("Timeline delivery was interrupted before all artifacts were published.");
        result.insert("rollback", restored);
        return result;
      }
      published_.append(artifact);
    }
    if (canceled && canceled()) {
      auto result = failure("Timeline delivery canceled.");
      result.insert("rollback", rollback());
      return result;
    }
    return {{"ok", true}, {"artifacts", published_}};
  }

  QVariantMap rollback() {
    auto result = rollbackArtifacts(published_);
    published_ = result.value("remainingArtifacts").toList();
    return result;
  }
  static QVariantMap rollbackArtifacts(const QVariantList& artifacts) {
    QVariantList retained;
    QStringList conflicts;
    for (const auto& item : artifacts) {
      const auto artifact = item.toMap();
      const auto path = artifact.value("path").toString();
      if (!QFileInfo::exists(path) && !QFileInfo(path).isSymLink()) continue;
      if (digest(path) != artifact.value("sha256").toString() ||
          !(artifact.value("directory").toBool() ? QDir(path).removeRecursively() : QFile::remove(path))) {
        retained.append(artifact);
        conflicts.append(path);
      }
    }
    return {{"ok", retained.isEmpty()}, {"preservedArtifacts", conflicts}, {"remainingArtifacts", retained}};
  }
  QVariantList artifacts() const { return published_; }

 private:
  static QVariantMap failure(const QString& message) { return {{"ok", false}, {"error", message}}; }
  QString directory_;
  QTemporaryDir stage_;
  QVariantList published_;
};

}  // namespace photo_editor::desktop

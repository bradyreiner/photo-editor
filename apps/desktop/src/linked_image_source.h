#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QVariantList>

#if defined(Q_OS_UNIX)
#include <sys/stat.h>
#endif

namespace photo_editor::desktop {

// A cheap filesystem revision, not an immutable content hash. Include change
// time and inode so same-size rewrites/restored mtimes and atomic replacements
// invalidate caches without reading large RAW files on the UI thread.
inline QString linked_image_file_revision(const QString& path) {
  const QFileInfo info(path);
  auto revision = QStringLiteral("file-revision-v1|%1|%2|%3|%4|%5")
      .arg(info.absoluteFilePath()).arg(info.isFile()).arg(info.isReadable())
      .arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
#if defined(Q_OS_UNIX)
  struct stat status {};
  if (::stat(QFile::encodeName(info.absoluteFilePath()).constData(), &status) == 0) {
#if defined(Q_OS_MACOS)
    const auto modified = status.st_mtimespec;
    const auto changed = status.st_ctimespec;
#else
    const auto modified = status.st_mtim;
    const auto changed = status.st_ctim;
#endif
    revision += QStringLiteral("|%1|%2|%3|%4|%5|%6")
        .arg(static_cast<qulonglong>(status.st_dev))
        .arg(static_cast<qulonglong>(status.st_ino))
        .arg(static_cast<qlonglong>(modified.tv_sec))
        .arg(static_cast<qlonglong>(modified.tv_nsec))
        .arg(static_cast<qlonglong>(changed.tv_sec))
        .arg(static_cast<qlonglong>(changed.tv_nsec));
  }
#endif
  return QString::fromLatin1(QCryptographicHash::hash(
      revision.toUtf8(), QCryptographicHash::Sha256).toHex());
}

inline QString linked_photo_layer_sources_revision(const QVariantList& layers) {
  QByteArray revisions;
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("kind")).toString().trimmed().toLower() ==
        QStringLiteral("source")) {
      revisions += linked_image_file_revision(
          layer.value(QStringLiteral("sourcePath")).toString()).toLatin1();
    }
    revisions += linked_photo_layer_sources_revision(
        layer.value(QStringLiteral("children")).toList()).toLatin1();
    revisions += linked_photo_layer_sources_revision(
        layer.value(QStringLiteral("layers")).toList()).toLatin1();
  }
  return revisions.isEmpty() ? QString{} : QString::fromLatin1(
      QCryptographicHash::hash(revisions, QCryptographicHash::Sha256).toHex());
}

// Match the current flat layer compiler's enabled/solo/content precedence.
inline QString linked_photo_layer_source_file_error(
    const QVariantList& layers, QStringList* failing_layer_ids = nullptr) {
  auto solo = false;
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("kind")).toString().trimmed().toLower() !=
            QStringLiteral("group") && layer.value(QStringLiteral("solo")).toBool()) {
      solo = true;
    }
  }
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("kind")).toString().trimmed().toLower() !=
            QStringLiteral("source") ||
        !layer.value(QStringLiteral("enabled"), true).toBool() ||
        layer.value(QStringLiteral("opacity"), 1.0).toDouble() <= 0.0 ||
        (solo && !layer.value(QStringLiteral("solo")).toBool()) ||
        (!layer.value(QStringLiteral("pixelData")).toByteArray().isEmpty() &&
         layer.value(QStringLiteral("pixelWidth")).toInt() > 0 &&
         layer.value(QStringLiteral("pixelHeight")).toInt() > 0)) {
      continue;
    }
    const auto path = layer.value(QStringLiteral("sourcePath")).toString();
    const QFileInfo info(path);
    if (path.isEmpty() || !info.isFile() || !info.isReadable()) {
      if (failing_layer_ids != nullptr) {
        *failing_layer_ids = {layer.value(QStringLiteral("id")).toString()};
      }
      return QStringLiteral("Linked image layer '%1' is missing or unreadable: %2. Restore the source file or disable the layer.")
          .arg(layer.value(QStringLiteral("name"), layer.value(QStringLiteral("id"))).toString(), path);
    }
  }
  return {};
}

}  // namespace photo_editor::desktop

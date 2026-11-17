#pragma once

#include <QDir>
#include <QString>
#include <QUrl>

namespace photo_editor::desktop {

// QML Image.source treats a slash-rooted string as a resource path. Timeline
// metadata can contain either native URL sources or raw absolute poster paths,
// so normalize only the presentation projection and leave authored metadata
// unchanged.
[[nodiscard]] inline QString timeline_presentation_source_url(
    const QString& source) {
  if (source.isEmpty()) {
    return source;
  }

  const QUrl parsed(source, QUrl::TolerantMode);
  if (parsed.isValid() && !parsed.scheme().isEmpty()) {
    return source;
  }
  if (QDir::isAbsolutePath(source)) {
    return QUrl::fromLocalFile(source).toString();
  }
  return source;
}

}  // namespace photo_editor::desktop

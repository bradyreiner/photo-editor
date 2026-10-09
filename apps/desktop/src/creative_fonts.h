#pragma once

#include <QDirIterator>
#include <QFontDatabase>
#include <QStringList>

namespace photo_editor::desktop {

// Register application-local fonts before controllers or render workers start.
// The files and their original OFL licenses are embedded in the executable.
[[nodiscard]] inline QStringList register_creative_fonts() {
  QStringList failures;
  QDirIterator fonts(QStringLiteral(":/photo-editor/fonts"),
      {QStringLiteral("*.ttf")}, QDir::Files, QDirIterator::Subdirectories);
  while (fonts.hasNext()) {
    const auto path = fonts.next();
    if (QFontDatabase::addApplicationFont(path) < 0) failures.push_back(path);
  }
  return failures;
}

}  // namespace photo_editor::desktop

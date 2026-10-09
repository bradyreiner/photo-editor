#include "creative_fonts.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFontInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRawFont>

#include <iostream>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QFile manifest(QStringLiteral(":/photo-editor/fonts/manifest.json"));
  if (!manifest.open(QIODevice::ReadOnly)) return 1;
  const auto families = QJsonDocument::fromJson(manifest.readAll())
      .object().value(QStringLiteral("families")).toArray();
  if (families.size() != 8 ||
      !photo_editor::desktop::register_creative_fonts().isEmpty()) return 1;
  for (const auto& entry : families) {
    const auto family = entry.toObject();
    const auto name = family.value(QStringLiteral("family")).toString();
    if (!QFontDatabase::families().contains(name)) {
      std::cerr << "Missing bundled family: " << name.toStdString() << '\n';
      return 1;
    }
    const auto raw = QRawFont::fromFont(QFont(name, 32));
    if (!raw.isValid() || !raw.supportsCharacter(QChar('A'))) return 1;
    bool licensed = false;
    for (const auto& file_entry : family.value(QStringLiteral("files")).toArray()) {
      const auto file = file_entry.toObject();
      const auto path = file.value(QStringLiteral("path")).toString();
      QFile resource(QStringLiteral(":/photo-editor/fonts/") + path);
      if (!resource.open(QIODevice::ReadOnly)) return 1;
      const auto bytes = resource.readAll();
      const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
      if (hash != file.value(QStringLiteral("sha256")).toString().toLatin1()) return 1;
      if (path.endsWith(QStringLiteral("OFL.txt")))
        licensed = bytes.contains("SIL OPEN FONT LICENSE Version 1.1");
    }
    if (!licensed) return 1;
  }
  std::cout << "Eight bundled families load with verified bytes and licenses\n";
  return 0;
}

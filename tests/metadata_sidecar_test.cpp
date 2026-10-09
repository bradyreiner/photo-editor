#include "catalog_metadata_io.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QVector>

#include <iostream>

using namespace photo_editor::desktop;

namespace {

QByteArray read_file(const QString& path);

bool expect(bool condition, const char* message) {
  if (condition) return true;
  std::cerr << message << '\n';
  return false;
}

bool write_file(const QString& path, const QByteArray& bytes) {
  QSaveFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

bool preserves_custom_namespace(const QByteArray& xml) {
  QXmlStreamReader reader(xml);
  bool custom_rating = false;
  bool custom_flag = false;
  bool custom_attribute = false;
  while (!reader.atEnd()) {
    reader.readNext();
    if (!reader.isStartElement()) continue;
    for (const auto& attribute : reader.attributes()) {
      if (attribute.namespaceUri() == QStringLiteral("https://example.test/ns/1.0/") &&
          attribute.name() == QStringLiteral("mode") && attribute.value() == QStringLiteral("manual")) {
        custom_attribute = true;
      }
    }
    if (reader.namespaceUri() == QStringLiteral("https://example.test/ns/1.0/") &&
        reader.name() == QStringLiteral("rating") && reader.readElementText() == QStringLiteral("4")) {
      custom_rating = true;
    } else if (reader.namespaceUri() == QStringLiteral("https://example.test/ns/1.0/") &&
               reader.name() == QStringLiteral("flag")) {
      custom_flag = reader.readElementText() == QStringLiteral("keep this extension");
    }
  }
  return !reader.hasError() && custom_rating && custom_flag && custom_attribute;
}

struct XmpListEntry {
  QString language;
  QString value;
};

QVector<XmpListEntry> read_list_entries(const QByteArray& xml, const QString& property_name) {
  QXmlStreamReader reader(xml);
  QVector<XmpListEntry> entries;
  int depth = 0;
  int property_depth = -1;
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement()) {
      ++depth;
      if (property_depth < 0 &&
          reader.namespaceUri() == QStringLiteral("http://purl.org/dc/elements/1.1/") &&
          reader.name() == property_name) {
        property_depth = depth;
      } else if (property_depth >= 0 &&
                 reader.namespaceUri() == QStringLiteral("http://www.w3.org/1999/02/22-rdf-syntax-ns#") &&
                 reader.name() == QStringLiteral("li")) {
        entries.push_back({xml_language(reader), reader.readElementText()});
        --depth;
      }
    } else if (reader.isEndElement()) {
      if (depth == property_depth) break;
      --depth;
    }
  }
  return entries;
}

int run() {
  QTemporaryDir temporary;
  if (!expect(temporary.isValid(), "Could not create temporary XMP fixture")) return 1;

  const auto raw = QDir(temporary.path()).filePath(QStringLiteral("frame.CR3"));
  const auto jpeg = QDir(temporary.path()).filePath(QStringLiteral("frame.JPG"));
  if (!expect(metadata_sidecar_path(raw) == metadata_sidecar_path(jpeg),
              "RAW/JPEG siblings should resolve to one standard basename XMP sidecar")) return 1;
  if (!expect(metadata_sidecar_path(raw) == QDir(temporary.path()).filePath(QStringLiteral("frame.xmp")),
              "Sidecar path should use the source basename without its extension")) return 1;
  if (!expect(metadata_sidecar_path(raw) != metadata_sidecar_path(
                  QDir(temporary.path()).filePath(QStringLiteral("other/frame.CR3"))),
              "Different source directories must not share sidecar paths")) return 1;

  const auto path = metadata_sidecar_path(raw);
  const QVariantMap initial{
      {QStringLiteral("title"), QStringLiteral("Mountain light")},
      {QStringLiteral("caption"), QStringLiteral("A quiet ridge at dawn")},
      {QStringLiteral("creator"), QStringLiteral("A. Photographer")},
      {QStringLiteral("copyright"), QStringLiteral("© 2026 A. Photographer")},
      {QStringLiteral("keywords"), QStringList{QStringLiteral("Nepal"), QStringLiteral("alpine"), QStringLiteral("dawn")}},
      {QStringLiteral("location"), QStringLiteral("Khumbu, Nepal")},
      {QStringLiteral("captureTime"), QStringLiteral("2026-10-07T06:14:12-06:00")}};
  QString error;
  if (!expect(write_metadata_sidecar(path, initial, &error), "Could not write a new XMP sidecar")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }
  const auto first_read = read_metadata_sidecar(path, &error);
  if (!expect(error.isEmpty(), "New sidecar did not parse") ||
      !expect(first_read.value(QStringLiteral("title")).toString() == initial.value(QStringLiteral("title")).toString(), "Title did not round trip") ||
      !expect(first_read.value(QStringLiteral("caption")).toString() == initial.value(QStringLiteral("caption")).toString(), "Caption did not round trip") ||
      !expect(first_read.value(QStringLiteral("creator")).toString() == initial.value(QStringLiteral("creator")).toString(), "Creator did not round trip") ||
      !expect(first_read.value(QStringLiteral("copyright")).toString() == initial.value(QStringLiteral("copyright")).toString(), "Copyright did not round trip") ||
      !expect(first_read.value(QStringLiteral("keywords")).toStringList() == initial.value(QStringLiteral("keywords")).toStringList(), "Keywords did not round trip") ||
      !expect(first_read.value(QStringLiteral("location")).toString() == initial.value(QStringLiteral("location")).toString(), "Location did not round trip") ||
      !expect(first_read.value(QStringLiteral("captureTime")).toString() == initial.value(QStringLiteral("captureTime")).toString(), "Capture time did not round trip")) return 1;

  const QByteArray extension = R"xml(<?xml version="1.0"?>
<x:xmpmeta xmlns:x="adobe:ns:meta/">
  <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
    <rdf:Description rdf:about="" xmlns:ext="https://example.test/ns/1.0/">
      <ext:rating>4</ext:rating>
      <ext:flag ext:mode="manual">keep this extension</ext:flag>
    </rdf:Description>
  </rdf:RDF>
</x:xmpmeta>)xml";
  if (!expect(write_file(path, extension), "Could not prepare XMP extension fixture")) return 1;
  auto updated = initial;
  updated.insert(QStringLiteral("title"), QStringLiteral("Updated title"));
  if (!expect(write_metadata_sidecar(path, updated, &error), "Updating existing XMP failed")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }
  if (!expect(preserves_custom_namespace(read_file(path)), "Unknown XMP namespace element or attribute was lost")) return 1;
  const auto updated_read = read_metadata_sidecar(path, &error);
  if (!expect(error.isEmpty() && updated_read.value(QStringLiteral("title")).toString() == QStringLiteral("Updated title"),
              "Known fields did not update alongside preserved extension data")) return 1;

  // Editing the app's primary creator/title/caption values should preserve
  // the other standard XMP sequence and localized Alt entries verbatim.
  const auto localized_path = QDir(temporary.path()).filePath(QStringLiteral("localized.xmp"));
  const QByteArray localized = R"xml(<?xml version="1.0"?>
<x:xmpmeta xmlns:x="adobe:ns:meta/">
  <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
    <rdf:Description rdf:about="" xmlns:dc="http://purl.org/dc/elements/1.1/">
      <dc:title><rdf:Alt>
        <rdf:li xml:lang="fr">Titre français</rdf:li>
        <rdf:li xml:lang="x-default">Original title</rdf:li>
        <rdf:li xml:lang="es">Título original</rdf:li>
      </rdf:Alt></dc:title>
      <dc:description><rdf:Alt>
        <rdf:li xml:lang="de">Deutsche Bildunterschrift</rdf:li>
        <rdf:li xml:lang="x-default">Original caption</rdf:li>
      </rdf:Alt></dc:description>
      <dc:rights><rdf:Alt>
        <rdf:li xml:lang="fr">Droits français</rdf:li>
      </rdf:Alt></dc:rights>
      <dc:creator><rdf:Seq>
        <rdf:li>Primary creator</rdf:li>
        <rdf:li>Secondary creator</rdf:li>
        <rdf:li>Organization credit</rdf:li>
      </rdf:Seq></dc:creator>
    </rdf:Description>
  </rdf:RDF>
</x:xmpmeta>)xml";
  if (!expect(write_file(localized_path, localized), "Could not prepare localized XMP fixture")) return 1;
  error.clear();
  const auto localized_read = read_metadata_sidecar(localized_path, &error);
  if (!expect(error.isEmpty(), "Localized XMP did not parse") ||
      !expect(localized_read.value(QStringLiteral("title")).toString() == QStringLiteral("Original title"),
              "Title should select x-default when present") ||
      !expect(localized_read.value(QStringLiteral("caption")).toString() == QStringLiteral("Original caption"),
              "Caption should select x-default when present") ||
      !expect(localized_read.value(QStringLiteral("copyright")).toString() == QStringLiteral("Droits français"),
              "Alt without x-default should select its first available translation") ||
      !expect(localized_read.value(QStringLiteral("creator")).toString() == QStringLiteral("Primary creator"),
              "Creator should select its first sequence entry")) return 1;

  const QVariantMap localized_edits{
      {QStringLiteral("title"), QStringLiteral("Edited title")},
      {QStringLiteral("caption"), QStringLiteral("Edited caption")},
      {QStringLiteral("copyright"), QStringLiteral("Edited rights")},
      {QStringLiteral("creator"), QStringLiteral("Edited creator")}};
  if (!expect(write_metadata_sidecar(localized_path, localized_edits, &error),
              "Could not update localized XMP fields")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }
  const auto written_xml = read_file(localized_path);
  const auto title_entries = read_list_entries(written_xml, QStringLiteral("title"));
  const auto caption_entries = read_list_entries(written_xml, QStringLiteral("description"));
  const auto rights_entries = read_list_entries(written_xml, QStringLiteral("rights"));
  const auto creator_entries = read_list_entries(written_xml, QStringLiteral("creator"));
  error.clear();
  const auto edited_read = read_metadata_sidecar(localized_path, &error);
  if (!expect(error.isEmpty(), "Localized XMP writer did not produce a well-formed sidecar") ||
      !expect(edited_read.value(QStringLiteral("title")).toString() == QStringLiteral("Edited title") &&
                  edited_read.value(QStringLiteral("caption")).toString() == QStringLiteral("Edited caption") &&
                  edited_read.value(QStringLiteral("copyright")).toString() == QStringLiteral("Edited rights") &&
                  edited_read.value(QStringLiteral("creator")).toString() == QStringLiteral("Edited creator"),
              "Edited default values should read back through the production XMP parser")) return 1;
  if (!expect(title_entries.size() == 3 && title_entries.at(0).language == QStringLiteral("fr") &&
                  title_entries.at(0).value == QStringLiteral("Titre français") &&
                  title_entries.at(1).language == QStringLiteral("x-default") &&
                  title_entries.at(1).value == QStringLiteral("Edited title") &&
                  title_entries.at(2).language == QStringLiteral("es") &&
                  title_entries.at(2).value == QStringLiteral("Título original"),
              "Updating title should preserve every non-default language") ||
      !expect(caption_entries.size() == 2 && caption_entries.at(0).language == QStringLiteral("de") &&
                  caption_entries.at(0).value == QStringLiteral("Deutsche Bildunterschrift") &&
                  caption_entries.at(1).language == QStringLiteral("x-default") &&
                  caption_entries.at(1).value == QStringLiteral("Edited caption"),
              "Updating caption should preserve localized alternates") ||
      !expect(rights_entries.size() == 2 && rights_entries.at(0).language == QStringLiteral("fr") &&
                  rights_entries.at(0).value == QStringLiteral("Droits français") &&
                  rights_entries.at(1).language == QStringLiteral("x-default") &&
                  rights_entries.at(1).value == QStringLiteral("Edited rights"),
              "Updating an Alt without x-default should add the default and preserve translations") ||
      !expect(creator_entries.size() == 3 && creator_entries.at(0).value == QStringLiteral("Edited creator") &&
                  creator_entries.at(1).value == QStringLiteral("Secondary creator") &&
                  creator_entries.at(2).value == QStringLiteral("Organization credit"),
              "Updating creator should preserve secondary sequence entries")) return 1;

  const auto malformed_path = QDir(temporary.path()).filePath(QStringLiteral("malformed.xmp"));
  const QByteArray malformed("<x:xmpmeta><rdf:RDF><rdf:Description><dc:title>unfinished");
  if (!expect(write_file(malformed_path, malformed), "Could not prepare malformed XMP fixture")) return 1;
  const auto before = QFileInfo(malformed_path).size();
  const auto malformed_read = read_metadata_sidecar(malformed_path, &error);
  if (!expect(malformed_read.isEmpty() && !error.isEmpty(), "Malformed XMP was accepted")) return 1;
  error.clear();
  if (!expect(!write_metadata_sidecar(malformed_path, initial, &error) && !error.isEmpty(),
              "Malformed XMP should reject writes")) return 1;
  QFile malformed_file(malformed_path);
  if (!expect(malformed_file.open(QIODevice::ReadOnly), "Could not reopen malformed XMP")) return 1;
  if (!expect(malformed_file.readAll() == malformed && QFileInfo(malformed_path).size() == before,
              "Failed XMP parsing modified the original sidecar")) return 1;

  const auto oversized_path = QDir(temporary.path()).filePath(QStringLiteral("oversized.xmp"));
  const QByteArray oversized(8 * 1024 * 1024 + 1, 'x');
  if (!expect(write_file(oversized_path, oversized), "Could not prepare oversized XMP fixture")) return 1;
  error.clear();
  if (!expect(read_metadata_sidecar(oversized_path, &error).isEmpty() && !error.isEmpty(),
              "Oversized XMP should be rejected before parsing")) return 1;
  error.clear();
  if (!expect(!write_metadata_sidecar(oversized_path, initial, &error) && !error.isEmpty(),
              "Oversized existing XMP should reject replacement")) return 1;
  if (!expect(read_file(oversized_path) == oversized,
              "Oversized XMP failure modified the original sidecar")) return 1;

  std::cout << "XMP sidecar contract passed: standard RAW/JPEG basename sharing, supported-field roundtrip, extension namespace preservation, malformed/oversized-file preservation.\n";
  return 0;
}

QByteArray read_file(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  return run();
}

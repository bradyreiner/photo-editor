#pragma once

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QVariantMap>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace photo_editor::desktop {
inline QString metadata_sidecar_path(const QString& source) {
  const QFileInfo file(source);
  return file.dir().filePath(file.completeBaseName() + QStringLiteral(".xmp"));
}
inline QString metadata_field_for_xml(const QString& uri, const QString& name) {
  if (uri == QStringLiteral("http://purl.org/dc/elements/1.1/")) {
    if (name == QStringLiteral("subject")) return QStringLiteral("keywords");
    if (name == QStringLiteral("description")) return QStringLiteral("caption");
    if (name == QStringLiteral("rights")) return QStringLiteral("copyright");
    if (name == QStringLiteral("creator")) return QStringLiteral("creator");
    if (name == QStringLiteral("title")) return QStringLiteral("title");
  }
  if (uri == QStringLiteral("http://ns.adobe.com/photoshop/1.0/") && name == QStringLiteral("Location")) return QStringLiteral("location");
  if (uri == QStringLiteral("http://ns.adobe.com/exif/1.0/") && name == QStringLiteral("DateTimeOriginal")) return QStringLiteral("captureTime");
  return {};
}
inline bool metadata_uses_language_alternatives(const QString& key) {
  return key == QStringLiteral("title") || key == QStringLiteral("caption") ||
      key == QStringLiteral("copyright");
}
inline QVariantMap read_metadata_sidecar(const QString& path, QString* error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) {
    *error = QStringLiteral("Could not read sidecar: %1").arg(path); return {};
  }
  QXmlStreamReader reader(file.readAll());
  QVariantMap result;
  while (!reader.atEnd()) {
    reader.readNext();
    if (!reader.isStartElement()) continue;
    for (const auto& attribute : reader.attributes()) {
      const auto key = metadata_field_for_xml(attribute.namespaceUri().toString(), attribute.name().toString());
      if (!key.isEmpty() && key != QStringLiteral("keywords")) result.insert(key, attribute.value().toString());
    }
    const auto key = metadata_field_for_xml(reader.namespaceUri().toString(), reader.name().toString());
    if (key.isEmpty()) continue;
    QStringList entries;
    QString default_entry;
    bool has_default_entry = false;
    QString scalar;
    int depth = 1;
    while (!reader.atEnd() && depth > 0) {
      reader.readNext();
      if (reader.isStartElement()) {
        ++depth;
        if (reader.namespaceUri() == QStringLiteral("http://www.w3.org/1999/02/22-rdf-syntax-ns#") && reader.name() == QStringLiteral("li")) {
          QString language;
          for (const auto& attribute : reader.attributes()) {
            if (attribute.namespaceUri() == QStringLiteral("http://www.w3.org/XML/1998/namespace") &&
                attribute.name() == QStringLiteral("lang")) {
              language = attribute.value().toString();
              break;
            }
          }
          const auto value = reader.readElementText();
          entries.append(value);
          if (language.compare(QStringLiteral("x-default"), Qt::CaseInsensitive) == 0) {
            default_entry = value;
            has_default_entry = true;
          }
          --depth;
        }
      } else if (reader.isEndElement()) --depth;
      else if (reader.isCharacters() && depth == 1) scalar += reader.text();
    }
    if (key == QStringLiteral("keywords")) result.insert(key, entries);
    else if (!entries.isEmpty()) {
      result.insert(key, metadata_uses_language_alternatives(key) && has_default_entry
                              ? default_entry : entries.first());
    } else result.insert(key, scalar.trimmed());
  }
  if (reader.hasError()) { *error = QStringLiteral("Malformed sidecar preserved: %1").arg(path); return {}; }
  return result;
}
inline void write_metadata_field(QXmlStreamWriter& writer, const QString& key, const QVariant& value) {
  const QList<QPair<QString,QString>> fields{{"title","title"},{"caption","description"},{"creator","creator"},{"copyright","rights"},{"keywords","subject"}};
  for (const auto& [field, xml] : fields) {
    if (key != field) continue;
    writer.writeStartElement(QStringLiteral("dc:") + xml);
    const auto container = key == QStringLiteral("keywords") ? QStringLiteral("rdf:Bag") : key == QStringLiteral("creator") ? QStringLiteral("rdf:Seq") : QStringLiteral("rdf:Alt");
    writer.writeStartElement(container);
    const auto values = key == QStringLiteral("keywords") ? value.toStringList() : QStringList{value.toString()};
    for (const auto& entry : values) {
      writer.writeStartElement(QStringLiteral("rdf:li"));
      if (container == QStringLiteral("rdf:Alt")) writer.writeAttribute(QStringLiteral("xml:lang"), QStringLiteral("x-default"));
      writer.writeCharacters(entry); writer.writeEndElement();
    }
    writer.writeEndElement(); writer.writeEndElement();
    return;
  }
  if (key == QStringLiteral("location")) writer.writeTextElement(QStringLiteral("photoshop:Location"), value.toString());
  else if (key == QStringLiteral("captureTime")) writer.writeTextElement(QStringLiteral("exif:DateTimeOriginal"), value.toString());
}
inline void write_metadata_fields(QXmlStreamWriter& writer, const QVariantMap& metadata) {
  writer.writeNamespace(QStringLiteral("http://purl.org/dc/elements/1.1/"), QStringLiteral("dc"));
  writer.writeNamespace(QStringLiteral("http://www.w3.org/1999/02/22-rdf-syntax-ns#"), QStringLiteral("rdf"));
  writer.writeNamespace(QStringLiteral("http://ns.adobe.com/photoshop/1.0/"), QStringLiteral("photoshop"));
  writer.writeNamespace(QStringLiteral("http://ns.adobe.com/exif/1.0/"), QStringLiteral("exif"));
  const QStringList fields{"title","caption","creator","copyright","keywords","location","captureTime"};
  for (const auto& key : fields) {
    if (metadata.contains(key)) write_metadata_field(writer, key, metadata.value(key));
  }
}
inline void write_metadata_namespaces(QXmlStreamWriter& writer) {
  writer.writeNamespace(QStringLiteral("http://purl.org/dc/elements/1.1/"), QStringLiteral("dc"));
  writer.writeNamespace(QStringLiteral("http://www.w3.org/1999/02/22-rdf-syntax-ns#"), QStringLiteral("rdf"));
  writer.writeNamespace(QStringLiteral("http://ns.adobe.com/photoshop/1.0/"), QStringLiteral("photoshop"));
  writer.writeNamespace(QStringLiteral("http://ns.adobe.com/exif/1.0/"), QStringLiteral("exif"));
}
inline QSet<QString> existing_metadata_properties(
    const QByteArray& xml,
    const QVariantMap& metadata,
    bool* has_description,
    bool* valid) {
  constexpr auto rdf_uri = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
  QSet<QString> properties;
  QXmlStreamReader reader(xml);
  while (!reader.atEnd()) {
    reader.readNext();
    if (!reader.isStartElement()) continue;
    if (reader.namespaceUri() == QString::fromLatin1(rdf_uri) &&
        reader.name() == QStringLiteral("Description")) {
      if (has_description) *has_description = true;
    }
    const auto key = metadata_field_for_xml(reader.namespaceUri().toString(), reader.name().toString());
    if (!key.isEmpty() && metadata.contains(key)) properties.insert(key);
  }
  if (valid) *valid = !reader.hasError();
  return properties;
}
inline bool is_rdf_list_container(const QXmlStreamReader& reader) {
  return reader.namespaceUri() == QStringLiteral("http://www.w3.org/1999/02/22-rdf-syntax-ns#") &&
      (reader.name() == QStringLiteral("Alt") || reader.name() == QStringLiteral("Seq") ||
       reader.name() == QStringLiteral("Bag"));
}
inline QString xml_language(const QXmlStreamReader& reader) {
  for (const auto& attribute : reader.attributes()) {
    if (attribute.namespaceUri() == QStringLiteral("http://www.w3.org/XML/1998/namespace") &&
        attribute.name() == QStringLiteral("lang")) {
      return attribute.value().toString();
    }
  }
  return {};
}
inline void write_updated_metadata_property(
    QXmlStreamReader& reader,
    QXmlStreamWriter& writer,
    const QString& key,
    const QVariant& value) {
  constexpr auto rdf_uri = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
  writer.writeCurrentToken(reader);
  int depth = 1;
  int container_depth = -1;
  int list_item_index = 0;
  bool has_list_container = false;
  bool value_written = false;
  bool list_value_written = false;
  bool language_alternative = false;
  QString container_prefix;

  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.isStartElement()) {
      ++depth;
      if (depth == 2 && is_rdf_list_container(reader)) {
        has_list_container = true;
        container_depth = depth;
        language_alternative = metadata_uses_language_alternatives(key) &&
            reader.name() == QStringLiteral("Alt");
        container_prefix = reader.qualifiedName().toString();
        const auto colon = container_prefix.indexOf(QLatin1Char(':'));
        if (colon >= 0) container_prefix.truncate(colon);
        else container_prefix.clear();
        writer.writeCurrentToken(reader);
        continue;
      }
      if (has_list_container && depth == container_depth + 1 &&
          reader.namespaceUri() == QString::fromLatin1(rdf_uri) &&
          reader.name() == QStringLiteral("li")) {
        const bool update_default = language_alternative &&
            xml_language(reader).compare(QStringLiteral("x-default"), Qt::CaseInsensitive) == 0;
        const bool update_creator = key == QStringLiteral("creator") && list_item_index == 0;
        ++list_item_index;
        if (update_default || update_creator) {
          writer.writeCurrentToken(reader);
          reader.skipCurrentElement();
          writer.writeCharacters(value.toString());
          writer.writeEndElement();
          --depth;
          list_value_written = true;
          continue;
        }
      }
      if (!has_list_container && depth == 2) {
        // A known property encoded as a scalar or a non-RDF resource is being
        // edited. Keep its enclosing property/attributes and replace its value.
        reader.skipCurrentElement();
        --depth;
        if (!value_written) {
          writer.writeCharacters(value.toString());
          value_written = true;
        }
        continue;
      }
      writer.writeCurrentToken(reader);
    } else if (reader.isEndElement()) {
      if (has_list_container && depth == container_depth && !list_value_written &&
          (language_alternative || key == QStringLiteral("creator"))) {
        const auto item_name = container_prefix.isEmpty()
            ? QStringLiteral("li") : container_prefix + QStringLiteral(":li");
        writer.writeStartElement(item_name);
        if (language_alternative) writer.writeAttribute(QStringLiteral("xml:lang"), QStringLiteral("x-default"));
        writer.writeCharacters(value.toString());
        writer.writeEndElement();
        list_value_written = true;
      }
      if (depth == 1 && !has_list_container && !value_written) {
        writer.writeCharacters(value.toString());
        value_written = true;
      }
      writer.writeCurrentToken(reader);
      --depth;
      if (depth == 0) break;
    } else if (reader.isCharacters() && !has_list_container && depth == 1 &&
               !reader.text().trimmed().isEmpty()) {
      if (!value_written) {
        writer.writeCharacters(value.toString());
        value_written = true;
      }
    } else {
      writer.writeCurrentToken(reader);
    }
  }
}
inline bool write_metadata_sidecar(const QString& path, const QVariantMap& metadata, QString* error) {
  QByteArray previous;
  QFile input(path);
  if (input.exists()) {
    if (!input.open(QIODevice::ReadOnly) || input.size() > 8 * 1024 * 1024) { *error = QStringLiteral("Could not read existing sidecar: %1").arg(path); return false; }
    previous = input.readAll();
  }
  QByteArray output;
  QXmlStreamWriter writer(&output);
  writer.setAutoFormatting(true);
  if (previous.isEmpty()) {
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("x:xmpmeta")); writer.writeNamespace(QStringLiteral("adobe:ns:meta/"), QStringLiteral("x"));
    writer.writeStartElement(QStringLiteral("rdf:RDF")); writer.writeNamespace(QStringLiteral("http://www.w3.org/1999/02/22-rdf-syntax-ns#"), QStringLiteral("rdf"));
    writer.writeStartElement(QStringLiteral("rdf:Description")); writer.writeAttribute(QStringLiteral("rdf:about"), QString());
    write_metadata_fields(writer, metadata);
    writer.writeEndElement(); writer.writeEndElement(); writer.writeEndElement(); writer.writeEndDocument();
  } else {
    bool has_description = false;
    bool valid = false;
    const auto existing_properties = existing_metadata_properties(
        previous, metadata, &has_description, &valid);
    if (!valid || !has_description) {
      *error = QStringLiteral("Existing sidecar is malformed or unsupported and was preserved: %1").arg(path);
      return false;
    }
    QXmlStreamReader reader(previous);
    bool initialized_first_description = false;
    QSet<QString> written_properties;
    while (!reader.atEnd()) {
      reader.readNext();
      if (reader.isStartElement() && reader.namespaceUri() == QStringLiteral("http://www.w3.org/1999/02/22-rdf-syntax-ns#") && reader.name() == QStringLiteral("Description")) {
        const bool first_description = !initialized_first_description;
        writer.writeStartElement(reader.qualifiedName().toString());
        for (const auto& ns : reader.namespaceDeclarations()) {
          const auto prefix = ns.prefix().toString();
          if (first_description && QStringList{"dc","rdf","photoshop","exif"}.contains(prefix)) continue;
          writer.writeNamespace(ns.namespaceUri().toString(), prefix);
        }
        for (const auto& attr : reader.attributes()) {
          if (!metadata.contains(metadata_field_for_xml(attr.namespaceUri().toString(), attr.name().toString()))) writer.writeAttribute(attr.qualifiedName().toString(), attr.value().toString());
        }
        if (first_description) {
          write_metadata_namespaces(writer);
          const QStringList fields{"title","caption","creator","copyright","keywords","location","captureTime"};
          for (const auto& key : fields) {
            if (!metadata.contains(key) || existing_properties.contains(key)) continue;
            write_metadata_field(writer, key, metadata.value(key));
            written_properties.insert(key);
          }
          initialized_first_description = true;
        }
      } else if (reader.isStartElement() && metadata.contains(metadata_field_for_xml(reader.namespaceUri().toString(), reader.name().toString()))) {
        const auto key = metadata_field_for_xml(reader.namespaceUri().toString(), reader.name().toString());
        if (written_properties.contains(key)) {
          reader.skipCurrentElement();
          continue;
        }
        if (key == QStringLiteral("keywords")) {
          reader.skipCurrentElement();
          write_metadata_field(writer, key, metadata.value(key));
        } else {
          write_updated_metadata_property(reader, writer, key, metadata.value(key));
        }
        written_properties.insert(key);
      } else writer.writeCurrentToken(reader);
    }
    if (reader.hasError() || !initialized_first_description) { *error = QStringLiteral("Existing sidecar is malformed or unsupported and was preserved: %1").arg(path); return false; }
  }
  if (writer.hasError()) { *error = QStringLiteral("Could not encode metadata sidecar."); return false; }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(output) != output.size() || !file.commit()) { *error = QStringLiteral("Could not save sidecar: %1").arg(path); return false; }
  return true;
}
} // namespace photo_editor::desktop

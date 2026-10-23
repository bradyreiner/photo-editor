#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QCoreApplication>
#include <QColor>
#include <QCryptographicHash>
#include <QHostAddress>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QTemporaryDir>
#include <QVector>

#include "agent_edit_recipe_patch.h"
#include "agent_edit_style.h"
#include "agent_local_model.h"
#include "agent_design_schemas.h"
#include "agent_mcp_metadata_projection.h"
#include "agent_runtime_paths.h"
#include "agent_tool_call_recovery.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

QVariantMap style_by_id(const QString& id) {
  const auto styles = photo_editor::desktop::built_in_agent_edit_styles();
  for (const auto& style_value : styles) {
    const auto style = style_value.toMap();
    if (style.value(QStringLiteral("id")).toString() == id) {
      return style;
    }
  }
  return {};
}

bool has_citation_field(const QVariantList& citations, const QString& field) {
  for (const auto& citation_value : citations) {
    const auto citation = citation_value.toMap();
    if (citation.value(QStringLiteral("field")).toString() == field) {
      return true;
    }
  }
  return false;
}

QJsonObject schema_branch_for_decision(
    const QJsonObject& schema, const QString& decision) {
  for (const auto& branch_value : schema.value(QStringLiteral("oneOf")).toArray()) {
    const auto branch = branch_value.toObject();
    const auto decision_schema = branch.value(QStringLiteral("properties")).toObject()
                                     .value(QStringLiteral("decision")).toObject();
    if (decision_schema.value(QStringLiteral("enum")).toArray().contains(decision))
      return branch;
  }
  return {};
}

QJsonObject schema_branch_for_composition_operation(
    const QJsonObject& schema, const QString& operation) {
  for (const auto& branch_value : schema.value(QStringLiteral("oneOf")).toArray()) {
    const auto branch = branch_value.toObject();
    const auto operation_values = branch.value(QStringLiteral("properties")).toObject()
        .value(QStringLiteral("operation")).toObject().value(QStringLiteral("enum")).toArray();
    if (operation_values.contains(operation)) return branch;
  }
  return {};
}

QJsonObject schema_branch_for_tool_operation(
    const QJsonObject& schema, const QString& tool_name, const QString& operation) {
  const auto tool_branch = schema_branch_for_decision(schema, QStringLiteral("tool_call_plan"));
  const auto call_variants = tool_branch.value(QStringLiteral("properties")).toObject()
      .value(QStringLiteral("toolCalls")).toObject()
      .value(QStringLiteral("items")).toObject().value(QStringLiteral("oneOf")).toArray();
  for (const auto& call_value : call_variants) {
    const auto call_properties = call_value.toObject().value(QStringLiteral("properties")).toObject();
    if (!call_properties.value(QStringLiteral("name")).toObject()
            .value(QStringLiteral("enum")).toArray().contains(tool_name))
      continue;
    const auto arguments = call_properties.value(QStringLiteral("arguments")).toObject();
    const auto direct_operation = arguments.value(QStringLiteral("properties")).toObject()
        .value(QStringLiteral("operation")).toObject();
    if (direct_operation.value(QStringLiteral("enum")).toArray().contains(operation) ||
        direct_operation.value(QStringLiteral("const")).toString() == operation)
      return arguments;
    for (const auto& operation_value : arguments.value(QStringLiteral("oneOf")).toArray()) {
      const auto operation_branch = operation_value.toObject();
      const auto operation_schema = operation_branch.value(QStringLiteral("properties")).toObject()
          .value(QStringLiteral("operation")).toObject();
      if (operation_schema.value(QStringLiteral("enum")).toArray().contains(operation) ||
          operation_schema.value(QStringLiteral("const")).toString() == operation)
        return operation_branch;
    }
  }
  return {};
}

bool schema_has_properties_alongside_union(const QJsonValue& value) {
  if (value.isObject()) {
    const auto object = value.toObject();
    if (object.contains(QStringLiteral("properties")) &&
        (object.contains(QStringLiteral("oneOf")) ||
         object.contains(QStringLiteral("anyOf"))))
      return true;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
      if (schema_has_properties_alongside_union(it.value())) return true;
  } else if (value.isArray()) {
    for (const auto& child : value.toArray())
      if (schema_has_properties_alongside_union(child)) return true;
  }
  return false;
}

QVariantMap model_by_id(const QVariantList& models, const QString& id) {
  for (const auto& model_value : models) {
    const auto model = model_value.toMap();
    if (model.value(QStringLiteral("id")).toString() == id) {
      return model;
    }
  }
  return {};
}

QJsonObject read_json_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QJsonDocument::fromJson(file.readAll()).object();
}

QString repository_file_path(const QString& relative_path) {
  const QStringList starts{QDir::currentPath(), QCoreApplication::applicationDirPath()};
  for (auto start : starts) {
    for (int depth = 0; depth < 10 && !start.isEmpty(); ++depth) {
      const auto candidate = QDir(start).filePath(relative_path);
      if (QFileInfo::exists(candidate)) return candidate;
      const auto parent = QFileInfo(start).dir().absolutePath();
      if (parent == start) break;
      start = parent;
    }
  }
  return {};
}

QJsonValue prompt_json_value_after(const QString& prompt, const QString& heading) {
  auto start = prompt.indexOf(heading);
  if (start < 0) return {};
  start += heading.size();
  while (start < prompt.size() && prompt.at(start).isSpace()) ++start;
  if (start >= prompt.size() ||
      (prompt.at(start) != QLatin1Char('{') && prompt.at(start) != QLatin1Char('[')))
    return {};

  QVector<QChar> closers;
  bool in_string = false;
  bool escaped = false;
  auto end = start;
  for (; end < prompt.size(); ++end) {
    const auto character = prompt.at(end);
    if (in_string) {
      if (escaped) escaped = false;
      else if (character == QLatin1Char('\\')) escaped = true;
      else if (character == QLatin1Char('"')) in_string = false;
      continue;
    }
    if (character == QLatin1Char('"')) {
      in_string = true;
      continue;
    }
    if (character == QLatin1Char('{')) closers.append(QLatin1Char('}'));
    else if (character == QLatin1Char('[')) closers.append(QLatin1Char(']'));
    else if ((character == QLatin1Char('}') || character == QLatin1Char(']')) &&
        !closers.isEmpty()) {
      if (closers.last() != character) return {};
      closers.removeLast();
      if (closers.isEmpty()) {
        ++end;
        break;
      }
    }
  }
  if (!closers.isEmpty()) return {};
  const auto document = QJsonDocument::fromJson(prompt.mid(start, end - start).toUtf8());
  if (document.isObject()) return document.object();
  if (document.isArray()) return document.array();
  return {};
}

QVariant resolve_prompt_sha256_references(
    const QVariant& value,
    const QVariantList& sha256_values,
    bool* ok) {
  constexpr auto marker = "$h1";
  if (value.metaType().id() == QMetaType::QVariantMap) {
    const auto map = value.toMap();
    if (map.contains(QString::fromLatin1(marker))) {
      const auto index_value = map.value(QString::fromLatin1(marker));
      const auto index_type = index_value.metaType().id();
      const bool numeric_type = index_type == QMetaType::Int ||
          index_type == QMetaType::UInt || index_type == QMetaType::LongLong ||
          index_type == QMetaType::ULongLong || index_type == QMetaType::Double;
      bool integral_number = numeric_type;
      if (index_type == QMetaType::Double) {
        const auto number = index_value.toDouble();
        integral_number = std::isfinite(number) && std::floor(number) == number;
      }
      bool index_ok = false;
      const auto index = index_value.toLongLong(&index_ok);
      if (map.size() != 1 || !integral_number || !index_ok || index < 0 ||
          index >= sha256_values.size() ||
          sha256_values.at(static_cast<qsizetype>(index)).metaType().id() != QMetaType::QString) {
        if (ok != nullptr) *ok = false;
        return {};
      }
      const auto hash = sha256_values.at(static_cast<qsizetype>(index)).toString();
      static const QRegularExpression hash_pattern(QStringLiteral("^[0-9a-f]{64}$"));
      if (!hash_pattern.match(hash).hasMatch()) {
        if (ok != nullptr) *ok = false;
        return {};
      }
      return hash;
    }
    QVariantMap decoded;
    for (auto iterator = map.cbegin(); iterator != map.cend(); ++iterator)
      decoded.insert(iterator.key(), resolve_prompt_sha256_references(
          iterator.value(), sha256_values, ok));
    return decoded;
  }
  if (value.metaType().id() == QMetaType::QVariantList) {
    QVariantList decoded;
    for (const auto& entry : value.toList())
      decoded.append(resolve_prompt_sha256_references(entry, sha256_values, ok));
    return decoded;
  }
  return value;
}

QVariantList decode_lossless_columnar_rows(
    const QVariantMap& encoded,
    bool* ok,
    const QVariantList& referenced_ids = {}) {
  if (ok != nullptr) *ok = false;
  if (encoded.value(QStringLiteral("encoding")).toString() !=
      QStringLiteral("lossless-columnar-rows-v1")) return {};
  QStringList fields;
  QSet<QString> seen_fields;
  for (const auto& field_value : encoded.value(QStringLiteral("fields")).toList()) {
    if (field_value.metaType().id() != QMetaType::QString ||
        field_value.toString().isEmpty() || seen_fields.contains(field_value.toString())) return {};
    fields.append(field_value.toString());
    seen_fields.insert(field_value.toString());
  }
  const auto rows = encoded.value(QStringLiteral("rows")).toList();
  const auto identity_field = encoded.value(QStringLiteral("identityField")).toString();
  QVariantList row_ids;
  if (encoded.contains(QStringLiteral("rowIdsReference"))) {
    if (encoded.value(QStringLiteral("rowIdsReference")).metaType().id() != QMetaType::Bool ||
        !encoded.value(QStringLiteral("rowIdsReference")).toBool() ||
        encoded.contains(QStringLiteral("rowIds")) || referenced_ids.isEmpty()) return {};
    row_ids = referenced_ids;
  } else {
    if (encoded.contains(QStringLiteral("rowIds")) &&
        encoded.value(QStringLiteral("rowIds")).metaType().id() != QMetaType::QVariantList) return {};
    row_ids = encoded.value(QStringLiteral("rowIds")).toList();
  }
  const auto common_fields_value = encoded.value(QStringLiteral("commonFields"));
  if (encoded.contains(QStringLiteral("commonFields")) &&
      common_fields_value.metaType().id() != QMetaType::QVariantMap) return {};
  const auto common_fields = common_fields_value.toMap();
  const auto shared_mask_value = encoded.value(QStringLiteral("presenceMask"));
  if (encoded.contains(QStringLiteral("presenceMask")) &&
      shared_mask_value.metaType().id() != QMetaType::QString) return {};
  const auto shared_mask = shared_mask_value.toString();
  QVariantList masks;
  if (encoded.contains(QStringLiteral("presenceMasks"))) {
    masks = encoded.value(QStringLiteral("presenceMasks")).toList();
    if (masks.size() != rows.size()) return {};
    for (const auto& mask : masks)
      if (mask.metaType().id() != QMetaType::QString) return {};
  }
  if (!identity_field.isEmpty() && row_ids.size() != rows.size()) return {};
  if (identity_field.isEmpty() && !row_ids.isEmpty()) return {};
  if (!identity_field.isEmpty() && (seen_fields.contains(identity_field) || common_fields.contains(identity_field))) return {};
  if (encoded.contains(QStringLiteral("presenceMask")) &&
      encoded.contains(QStringLiteral("presenceMasks"))) return {};
  if (!encoded.contains(QStringLiteral("presenceMask")) &&
      !encoded.contains(QStringLiteral("presenceMasks")) && !rows.isEmpty()) return {};
  for (auto it = common_fields.cbegin(); it != common_fields.cend(); ++it)
    if (it.key().isEmpty() || seen_fields.contains(it.key())) return {};

  QVariantList decoded;
  decoded.reserve(rows.size());
  QSet<QString> seen_row_ids;
  for (qsizetype row_index = 0; row_index < rows.size(); ++row_index) {
    const auto values = rows.at(row_index).toList();
    const auto mask = encoded.contains(QStringLiteral("presenceMask"))
        ? shared_mask : masks.at(row_index).toString();
    if (values.size() != fields.size() || mask.size() != fields.size()) return {};
    QVariantMap row = common_fields;
    if (!identity_field.isEmpty()) {
      const auto id = row_ids.at(row_index);
      if (id.metaType().id() != QMetaType::QString || id.toString().trimmed().isEmpty()) return {};
      if (seen_row_ids.contains(id.toString())) return {};
      seen_row_ids.insert(id.toString());
      row.insert(identity_field, id);
    }
    for (qsizetype field_index = 0; field_index < fields.size(); ++field_index) {
      const auto present = mask.at(field_index);
      if (present == QLatin1Char('1'))
        row.insert(fields.at(field_index), values.at(field_index));
      else if (present != QLatin1Char('0') || !values.at(field_index).isNull())
        return {};
    }
    decoded.append(row);
  }
  if (ok != nullptr) *ok = true;
  return decoded;
}

QVariantList decode_prompt_graphic_layer_rows(const QVariantMap& encoded, bool* ok) {
  if (ok != nullptr) *ok = false;
  bool base_rows_ok = false;
  auto rows = decode_lossless_columnar_rows(encoded, &base_rows_ok);
  const auto has_typography_table = encoded.contains(QStringLiteral("textTypographyByLayerId"));
  const auto has_layout_table = encoded.contains(QStringLiteral("layoutMetricsByLayerId"));
  if (!base_rows_ok) return {};
  if (!has_typography_table && !has_layout_table) {
    if (ok != nullptr) *ok = true;
    return rows;
  }
  if (!has_typography_table || !has_layout_table ||
      encoded.value(QStringLiteral("textTypographyByLayerId")).metaType().id() != QMetaType::QVariantMap ||
      encoded.value(QStringLiteral("layoutMetricsByLayerId")).metaType().id() != QMetaType::QVariantMap)
    return {};
  bool typography_rows_ok = false;
  bool layout_rows_ok = false;
  const auto typography_rows = decode_lossless_columnar_rows(
      encoded.value(QStringLiteral("textTypographyByLayerId")).toMap(), &typography_rows_ok);
  const auto layout_rows = decode_lossless_columnar_rows(
      encoded.value(QStringLiteral("layoutMetricsByLayerId")).toMap(), &layout_rows_ok);
  if (!typography_rows_ok || !layout_rows_ok ||
      typography_rows.size() != rows.size() || layout_rows.size() != rows.size()) return {};
  QHash<QString, QVariantMap> typography_by_id;
  QHash<QString, QVariantMap> layout_by_id;
  for (const auto& row_value : typography_rows) {
    auto row = row_value.toMap();
    const auto id = row.take(QStringLiteral("id")).toString();
    if (id.trimmed().isEmpty() || typography_by_id.contains(id)) return {};
    typography_by_id.insert(id, row);
  }
  for (const auto& row_value : layout_rows) {
    auto row = row_value.toMap();
    const auto id = row.take(QStringLiteral("id")).toString();
    if (id.trimmed().isEmpty() || layout_by_id.contains(id)) return {};
    layout_by_id.insert(id, row);
  }
  QSet<QString> seen_ids;
  for (auto& row_value : rows) {
    auto row = row_value.toMap();
    const auto id = row.value(QStringLiteral("id")).toString();
    if (id.trimmed().isEmpty() || seen_ids.contains(id) ||
        row.contains(QStringLiteral("textTypography")) || row.contains(QStringLiteral("layoutMetrics")) ||
        !typography_by_id.contains(id) || !layout_by_id.contains(id)) return {};
    seen_ids.insert(id);
    row.insert(QStringLiteral("textTypography"), typography_by_id.value(id));
    row.insert(QStringLiteral("layoutMetrics"), layout_by_id.value(id));
    row_value = row;
  }
  if (ok != nullptr) *ok = true;
  return rows;
}

class FakeOllamaServer final {
 public:
  FakeOllamaServer() {
    server_ = new QTcpServer(QCoreApplication::instance());
    QObject::connect(server_, &QTcpServer::newConnection, server_, [this]() { acceptConnections(); });
  }

  [[nodiscard]] bool start() {
    return server_->listen(QHostAddress::LocalHost, 0) ||
           server_->listen(QHostAddress::AnyIPv4, 0);
  }

  [[nodiscard]] QString endpoint() const {
    return QStringLiteral("http://127.0.0.1:%1").arg(server_->serverPort());
  }

  void setResponse(const QString& path, const QJsonObject& response) {
    responses_.insert(path, response);
    response_sequences_.remove(path);
    response_statuses_.insert(path, 200);
    chunked_paths_.remove(path);
  }

  void setResponseSequence(const QString& path, const QList<QJsonObject>& responses) {
    setResponse(path, responses.last());
    response_sequences_.insert(path, responses);
  }

  [[nodiscard]] QList<QJsonObject> requestsForPath(const QString& path) const {
    QList<QJsonObject> result;
    for (const auto& request : requests_)
      if (request.value(QStringLiteral("path")).toString() == path) result.append(request);
    return result;
  }

  void setErrorResponse(const QString& path, int status_code, const QJsonObject& response) {
    responses_.insert(path, response);
    response_statuses_.insert(path, status_code);
    chunked_paths_.remove(path);
  }

  void setChunkedResponse(const QString& path, const QJsonObject& response) {
    responses_.insert(path, response);
    response_statuses_.insert(path, 200);
    chunked_paths_.insert(path);
  }

  void setResponseDelay(int delay_ms) { response_delay_ms_ = delay_ms; }
  void setDisconnectWithoutResponse(bool enabled) { disconnect_without_response_ = enabled; }

  [[nodiscard]] QJsonObject lastRequestForPath(const QString& path) const {
    for (auto index = requests_.size() - 1; index >= 0; --index) {
      const auto request = requests_.at(index);
      if (request.value(QStringLiteral("path")).toString() == path) {
        return request;
      }
    }
    return {};
  }

 private:
  void acceptConnections() {
    while (server_->hasPendingConnections()) {
      auto* socket = server_->nextPendingConnection();
      socket->setParent(server_);
      socket->setProperty("buffer", QByteArray{});
      QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() { readRequest(socket); });
      QObject::connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
  }

  void readRequest(QTcpSocket* socket) {
    auto buffer = socket->property("buffer").toByteArray();
    buffer.append(socket->readAll());
    const auto header_end = buffer.indexOf("\r\n\r\n");
    if (header_end < 0) {
      socket->setProperty("buffer", buffer);
      return;
    }

    const auto header = buffer.left(header_end);
    const auto lines = header.split('\n');
    const auto request_line = QString::fromLatin1(lines.value(0)).trimmed();
    const auto request_parts = request_line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const auto path = request_parts.size() >= 2 ? request_parts.at(1) : QString{};
    auto content_length = -1;
    for (const auto& line_bytes : lines) {
      const auto line = QString::fromLatin1(line_bytes).trimmed();
      if (line.toLower().startsWith(QStringLiteral("content-length:"))) {
        auto ok = false;
        content_length = line.mid(QStringLiteral("content-length:").size()).trimmed().toInt(&ok);
        if (!ok) {
          content_length = -1;
        }
      }
    }
    if (content_length < 0) {
      writeResponse(socket, 411, QJsonObject{{QStringLiteral("error"), QStringLiteral("content length required")}});
      return;
    }

    const auto body_start = header_end + 4;
    if (buffer.size() - body_start < content_length) {
      socket->setProperty("buffer", buffer);
      return;
    }
    const auto document = QJsonDocument::fromJson(buffer.mid(body_start, content_length));
    auto request = document.object();
    request.insert(QStringLiteral("path"), path);
    requests_.append(request);

    if (disconnect_without_response_) {
      socket->disconnectFromHost();
      return;
    }

    if (!responses_.contains(path)) {
      writeResponse(socket, 404, QJsonObject{{QStringLiteral("error"), QStringLiteral("missing fake response")}});
      return;
    }
    auto response = responses_.value(path);
    auto& sequence = response_sequences_[path];
    if (!sequence.isEmpty()) response = sequence.takeFirst();
    const auto status = response_statuses_.value(path, 200);
    const auto chunked = chunked_paths_.contains(path);
    const auto streaming = request.value(QStringLiteral("stream")).toBool();
    QTimer::singleShot(response_delay_ms_, Qt::PreciseTimer, socket, [socket, response, status, chunked, streaming]() {
      if (streaming) writeStreamingResponse(socket, response);
      else if (chunked) writeChunkedResponse(socket, response);
      else writeResponse(socket, status, response);
    });
  }

  static void writeStreamingResponse(QTcpSocket* socket, const QJsonObject& response) {
    const auto content = response.value(QStringLiteral("message")).toObject()
                             .value(QStringLiteral("content")).toString();
    if (content.isEmpty()) {
      writeResponse(socket, 500, QJsonObject{{QStringLiteral("error"), QStringLiteral("missing stream content")}});
      return;
    }
    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n");
    socket->flush();
    QList<QByteArray> events;
    for (qsizetype offset = 0; offset < content.size(); offset += 18) {
      const auto part = content.mid(offset, 18);
      events.append(QJsonDocument(QJsonObject{
          {QStringLiteral("message"), QJsonObject{{QStringLiteral("content"), part}}},
          {QStringLiteral("done"), false},
      }).toJson(QJsonDocument::Compact) + '\n');
    }
    events.append(QJsonDocument(QJsonObject{
        {QStringLiteral("done"), true},
        {QStringLiteral("done_reason"), QStringLiteral("stop")},
    }).toJson(QJsonDocument::Compact) + '\n');
    for (qsizetype index = 0; index < events.size(); ++index) {
      const auto event = events.at(index);
      QByteArray encoded;
      encoded.append(QByteArray::number(event.size(), 16));
      encoded.append("\r\n");
      encoded.append(event);
      encoded.append("\r\n");
      QTimer::singleShot(static_cast<int>(index) * 2, socket,
          [socket, encoded, index, count = events.size()]() {
            if (socket->state() == QAbstractSocket::UnconnectedState) return;
            socket->write(encoded);
            if (index + 1 == count) {
              socket->write("0\r\n\r\n");
              socket->flush();
              socket->disconnectFromHost();
            } else {
              socket->flush();
            }
          });
    }
  }

  static void writeChunkedResponse(QTcpSocket* socket, const QJsonObject& response) {
    const auto body = QJsonDocument(response).toJson(QJsonDocument::Compact);
    const auto split = std::max<qsizetype>(1, body.size() / 2);
    const auto first = body.left(split);
    const auto second = body.mid(split);
    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n");
    for (const auto& chunk : {first, second}) {
      if (chunk.isEmpty()) {
        continue;
      }
      socket->write(QByteArray::number(chunk.size(), 16));
      socket->write("\r\n");
      socket->write(chunk);
      socket->write("\r\n");
    }
    socket->write("0\r\n\r\n");
    socket->flush();
    socket->disconnectFromHost();
  }

  static void writeResponse(QTcpSocket* socket, int status_code, const QJsonObject& response) {
    const auto body = QJsonDocument(response).toJson(QJsonDocument::Compact);
    QByteArray header;
    header.append("HTTP/1.1 ");
    header.append(QByteArray::number(status_code));
    header.append(status_code == 200 ? " OK" : " Error");
    header.append("\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ");
    header.append(QByteArray::number(body.size()));
    header.append("\r\n\r\n");
    socket->write(header);
    socket->write(body);
    socket->flush();
    socket->disconnectFromHost();
  }

  QTcpServer* server_ = nullptr;
  QMap<QString, QJsonObject> responses_;
  QMap<QString, QList<QJsonObject>> response_sequences_;
  QMap<QString, int> response_statuses_;
  QSet<QString> chunked_paths_;
  QVector<QJsonObject> requests_;
  int response_delay_ms_ = 0;
  bool disconnect_without_response_ = false;
};

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("AgentLocalModelTest"));

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("temporary runtime directory should be created");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());

  const auto reservation_rows = [](const QString& model_name,
                                   const QList<QSize>& sizes,
                                   int attached_count) {
    return photo_editor::desktop::agent_detail::ollama_image_context_reservations(
        model_name, sizes, attached_count);
  };
  const auto reservation_tokens = [](const QVariantList& rows) {
    QList<int> tokens;
    for (const auto& row : rows)
      tokens.append(row.toMap().value(QStringLiteral("reserveTokens")).toInt());
    return tokens;
  };
  const auto image_reservation_total = [](const QVariantMap& components) {
    qint64 tokens = 0;
    for (const auto& row : components.value(QStringLiteral("imageContextReservations")).toList())
      tokens += row.toMap().value(QStringLiteral("reserveTokens")).toLongLong();
    return tokens;
  };
  const auto qwen_small_reservations = reservation_rows(
      QStringLiteral("qwen2.5vl:7b"), {QSize(768, 768), QSize(128, 96)}, 2);
  if (reservation_tokens(qwen_small_reservations) != QList<int>{2048, 2048} ||
      qwen_small_reservations.at(0).toMap().value(QStringLiteral("modelFamily")).toString() !=
          QStringLiteral("qwen2.5vl") ||
      qwen_small_reservations.at(0).toMap().value(QStringLiteral("transmittedWidth")).toInt() != 768 ||
      !qwen_small_reservations.at(0).toMap().value(QStringLiteral("dimensionsValid")).toBool())
    return fail("recognized Qwen2.5-VL images at the transmitted 768-pixel boundary must receive the conservative 2048-token reservation");
  for (const auto& family_case : {
           qMakePair(QStringLiteral("Qwen3-VL:8B"), QStringLiteral("qwen3vl")),
           qMakePair(QStringLiteral("qwen3.5:9B"), QStringLiteral("qwen3.5")),
           qMakePair(QStringLiteral("qwen3.6:35B"), QStringLiteral("qwen3.6"))}) {
    const auto rows = reservation_rows(family_case.first, {QSize(128, 96)}, 1);
    if (reservation_tokens(rows) != QList<int>{2048} ||
        rows.first().toMap().value(QStringLiteral("modelFamily")).toString() != family_case.second)
      return fail("recognized Qwen visual model families must use the same conservative small-image reservation");
  }
  const auto large_qwen_reservations = reservation_rows(
      QStringLiteral("qwen2.5vl:7b"), {QSize(769, 768)}, 1);
  const auto unknown_model_reservations = reservation_rows(
      QStringLiteral("qwen25vl-unverified"), {QSize(128, 96)}, 1);
  const auto missing_dimensions_reservations = reservation_rows(
      QStringLiteral("qwen2.5vl:7b"), {}, 1);
  const auto malformed_dimensions_reservations = reservation_rows(
      QStringLiteral("qwen2.5vl:7b"), {QSize(0, 96), QSize(96, -1)}, 2);
  const auto partial_dimensions_reservations = reservation_rows(
      QStringLiteral("qwen2.5vl:7b"), {QSize(128, 96)}, 2);
  if (reservation_tokens(large_qwen_reservations) != QList<int>{4096} ||
      reservation_tokens(unknown_model_reservations) != QList<int>{4096} ||
      unknown_model_reservations.first().toMap().value(QStringLiteral("modelFamily")).toString() !=
          QStringLiteral("unrecognized") ||
      reservation_tokens(missing_dimensions_reservations) != QList<int>{4096} ||
      missing_dimensions_reservations.first().toMap().value(QStringLiteral("dimensionsValid")).toBool() ||
      reservation_tokens(malformed_dimensions_reservations) != QList<int>{4096, 4096} ||
      malformed_dimensions_reservations.first().toMap().value(QStringLiteral("dimensionsValid")).toBool() ||
      reservation_tokens(partial_dimensions_reservations) != QList<int>{2048, 4096} ||
      partial_dimensions_reservations.at(1).toMap().value(QStringLiteral("dimensionsValid")).toBool())
    return fail("unknown, oversized, missing, malformed, and unbound image dimensions must retain the conservative 4096-token fallback");

  const auto app14_qwen36_images = reservation_rows(QStringLiteral("qwen3.6:35b"),
      {QSize(1024, 1536), QSize(1024, 284), QSize(771, 98), QSize(798, 98)}, 4);
  const auto grid_boundary_images = reservation_rows(QStringLiteral("qwen3.5:9b"),
      {QSize(1600, 1600), QSize(1601, 1600), QSize(769, 768)}, 3);
  if (reservation_tokens(app14_qwen36_images) != QList<int>{2048, 2048, 2048, 2048} ||
      app14_qwen36_images.first().toMap().value(QStringLiteral("ceilingGridTokens")).toInt() != 1536 ||
      app14_qwen36_images.first().toMap().value(QStringLiteral("gridAlignmentPixels")).toInt() != 32 ||
      reservation_tokens(grid_boundary_images) != QList<int>{3072, 4096, 2048} ||
      grid_boundary_images.first().toMap().value(QStringLiteral("ceilingGridTokens")).toInt() != 2500 ||
      reservation_tokens(reservation_rows(QStringLiteral("qwen3-vl:8b"), {QSize(1024, 1536)}, 1)) != QList<int>{4096} ||
      reservation_tokens(reservation_rows(QStringLiteral("qwen3.6-unverified"), {QSize(1024, 1536)}, 1)) != QList<int>{4096})
    return fail("The App14 Qwen3.5/3.6 processor grid must eliminate the 768px cliff while retaining other-family/unknown/large fallbacks");

  const QVariantList role_copy{QStringLiteral("AFTER THE LIGHT"), QStringLiteral("SAMPLE / FIELD NOTES 01"),
                              QStringLiteral("A STUDY IN BLUE HOUR")};
  const QVariantList role_actions{
      QVariantMap{{"kind", "text"}, {"summary", "Add kicker SAMPLE / FIELD NOTES 01 above the headline."}},
      QVariantMap{{"kind", "text"}, {"summary", "Add headline AFTER THE LIGHT at least 3x the kicker/footer."}},
      QVariantMap{{"kind", "text"}, {"summary", "Add footer A STUDY IN BLUE HOUR matching the kicker."}}};
  const auto bound_roles = photo_editor::desktop::agent_detail::approved_typography_role_copy(role_actions, role_copy);
  if (bound_roles.value("headline").toString() != role_copy.at(0).toString() ||
      bound_roles.value("kicker").toString() != role_copy.at(1).toString() ||
      bound_roles.value("footer").toString() != role_copy.at(2).toString() ||
      photo_editor::desktop::agent_detail::approved_typography_role_copy(
          QVariantList{role_actions.at(2), role_actions.at(1), role_actions.at(0)}, role_copy) != bound_roles)
    return fail("App14 comparative role mentions must not bind headline copy/size as footer or depend on action order");
  const auto copy_contains_role = photo_editor::desktop::agent_detail::approved_typography_role_copy(
      {QVariantMap{{"summary", "Add headline Footer Light at least 3x kicker/footer."}}},
      {QStringLiteral("Footer Light")});
  const auto ambiguous_action_roles = photo_editor::desktop::agent_detail::approved_typography_role_copy(
      {QVariantMap{{"summary", "Add headline AFTER THE LIGHT and A STUDY IN BLUE HOUR."}}}, role_copy);
  const auto conflicting_actions = photo_editor::desktop::agent_detail::approved_typography_role_copy(
      {QVariantMap{{"summary", "Add headline AFTER THE LIGHT."}},
       QVariantMap{{"summary", "Add headline A STUDY IN BLUE HOUR."}}}, role_copy);
  if (copy_contains_role.value("headline").toString() != QStringLiteral("Footer Light") ||
      !copy_contains_role.value("footer").toString().isEmpty() ||
      !ambiguous_action_roles.isEmpty() || !conflicting_actions.isEmpty())
    return fail("Role words inside copy, multi-copy actions and contradictory bindings must not produce guessed hierarchy evidence");

  const QVariantMap clipped_headline{
      {"id", "headline"}, {"kind", "text"}, {"x", 79}, {"y", 499},
      {"pixelWidth", 1290}, {"pixelHeight", 184}, {"canvasWidth", 1024}, {"canvasHeight", 1536},
      {"scale", 1.0}, {"rotation", 0.0}, {"textTypography", QVariantMap{{"size", 150.0}}}};
  const auto fit_guidance = photo_editor::desktop::agent_detail::typography_hierarchy_fit_guidance(clipped_headline, 3.0);
  auto rotated_headline = clipped_headline;
  rotated_headline.insert("rotation", 45.0);
  if (!fit_guidance.value("available").toBool() || !fit_guidance.value("approximationOnly").toBool() ||
      fit_guidance.value("availableWidthAtProposedMargins").toDouble() != 866.0 ||
      fit_guidance.value("headlineEffectiveSizeProxy").toDouble() > 101.0 ||
      fit_guidance.value("smallerRoleEffectiveSizeUpperProxy").toDouble() > 34.0 ||
      photo_editor::desktop::agent_detail::typography_hierarchy_fit_guidance(rotated_headline, 3.0).value("available").toBool() ||
      photo_editor::desktop::agent_detail::typography_hierarchy_fit_guidance({}, 3.0).value("available").toBool())
    return fail("App15 measured fit guidance must explain joint type-size feasibility and decline missing or rotated geometry");

  const auto models = photo_editor::desktop::available_agent_local_models();
  if (models.isEmpty() ||
      models.first().toMap().value(QStringLiteral("id")).toString() !=
          photo_editor::desktop::default_agent_local_model_id()) {
    return fail("local model registry should expose the deterministic rulebook model");
  }
  const auto deterministic_model = models.first().toMap();
  if (deterministic_model.value(QStringLiteral("status")).toString() != QStringLiteral("available") ||
      !deterministic_model.value(QStringLiteral("available")).toBool() ||
      deterministic_model.value(QStringLiteral("configured")).toBool()) {
    return fail("built-in local model should expose available status metadata");
  }

  const auto models_directory = QDir(runtime_root.path()).filePath(QStringLiteral("agent-models"));
  if (!QDir().mkpath(models_directory)) {
    return fail("configured model directory should be created");
  }
  const auto configured_model_path = QDir(runtime_root.path()).filePath(QStringLiteral("models/local-model.bin"));
  if (!QDir().mkpath(QFileInfo(configured_model_path).dir().absolutePath())) {
    return fail("configured model artifact directory should be created");
  }
  FakeOllamaServer fake_ollama_server;
  const auto fake_ollama_server_available = fake_ollama_server.start();
  if (fake_ollama_server_available) {
    fake_ollama_server.setResponse(
        QStringLiteral("/api/show"),
        QJsonObject{
            {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
            {QStringLiteral("details"),
             QJsonObject{
                 {QStringLiteral("family"), QStringLiteral("qwen2")},
                 {QStringLiteral("parameter_size"), QStringLiteral("7B")},
                 {QStringLiteral("quantization_level"), QStringLiteral("Q4_K_M")},
             }},
        });
  }
  QFile configured_model_file(configured_model_path);
  if (!configured_model_file.open(QIODevice::WriteOnly)) {
    return fail("configured model artifact should be writable");
  }
  configured_model_file.write("local model placeholder");
  configured_model_file.close();
  QFile manifest_file(QDir(models_directory).filePath(QStringLiteral("models.json")));
  if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return fail("configured model manifest should be writable");
  }
  manifest_file.write(R"json({
    "models": [
      {
        "id": "manifest-local",
        "name": "Manifest Local",
        "backend": "local",
        "path": "models/local-model.bin",
        "status": "loading",
        "description": "Configured local model metadata"
      },
      {
        "id": "missing-local",
        "name": "Missing Local",
        "backend": "local",
        "path": "models/missing.bin"
      },
      {
        "id": "rulebook-deterministic",
        "name": "Duplicate Built In",
        "backend": "local"
      },
      {
        "id": "cloud-default-blocked",
        "name": "Cloud Default Blocked",
        "backend": "cloud"
      },
      {
        "id": "cloud-explicit-opt-in",
        "name": "Cloud Explicit Opt In",
        "backend": "cloud",
        "allowCloud": true
      },
      {
        "id": "ollama-qwen-test",
        "name": "Ollama Qwen Test",
        "backend": "ollama",
        "endpoint": ")json");
  manifest_file.write(
      fake_ollama_server_available ? fake_ollama_server.endpoint().toUtf8()
                                   : QByteArrayLiteral("http://127.0.0.1:11434"));
  manifest_file.write(R"json(",
        "model": "qwen2.5vl:7b",
        "vision": true,
        "requestTimeoutMs": 45000,
        "contextWindow": 16384,
        "maxImageEdge": 1024,
        "status": "available",
        "description": "Fake Ollama test model"
      },
      {
        "id": "ollama-app09-budget-test",
        "name": "Ollama App09 Budget Test",
        "backend": "ollama",
        "endpoint": ")json");
  manifest_file.write(
      fake_ollama_server_available ? fake_ollama_server.endpoint().toUtf8()
                                   : QByteArrayLiteral("http://127.0.0.1:11434"));
  manifest_file.write(R"json(",
        "model": "qwen2.5vl:7b",
        "vision": true,
        "requestTimeoutMs": 45000,
        "contextWindow": 24576,
        "planningMaxOutputTokens": 1536,
        "planningImageEdge": 1024,
        "maxImageEdge": 1024,
        "status": "available",
        "description": "Captured App09 budget regression fixture"
      }
    ]
  })json");
  manifest_file.close();
  const auto discovered_models = photo_editor::desktop::available_agent_local_models();
  const auto manifest_model = model_by_id(discovered_models, QStringLiteral("manifest-local"));
  if (manifest_model.isEmpty() ||
      manifest_model.value(QStringLiteral("status")).toString() != QStringLiteral("loading") ||
      manifest_model.value(QStringLiteral("available")).toBool() ||
      !manifest_model.value(QStringLiteral("configured")).toBool() ||
      manifest_model.value(QStringLiteral("path")).toString().isEmpty()) {
    return fail("configured local model manifest should expose loading status metadata");
  }
  const auto missing_model = model_by_id(discovered_models, QStringLiteral("missing-local"));
  if (missing_model.isEmpty() ||
      missing_model.value(QStringLiteral("status")).toString() != QStringLiteral("failed") ||
      missing_model.value(QStringLiteral("available")).toBool() ||
      missing_model.value(QStringLiteral("statusReason")).toString().isEmpty()) {
    return fail("configured local model with missing path should be reported as failed");
  }
  const auto duplicate_model = model_by_id(discovered_models, QStringLiteral("rulebook-deterministic"));
  if (duplicate_model.value(QStringLiteral("configured")).toBool()) {
    return fail("configured local model should not override a built-in model id");
  }
  const auto cloud_default_blocked =
      model_by_id(discovered_models, QStringLiteral("cloud-default-blocked"));
  if (cloud_default_blocked.isEmpty() ||
      cloud_default_blocked.value(QStringLiteral("status")).toString() != QStringLiteral("failed") ||
      cloud_default_blocked.value(QStringLiteral("available")).toBool() ||
      !cloud_default_blocked.value(QStringLiteral("requiresCloudOptIn")).toBool() ||
      cloud_default_blocked.value(QStringLiteral("cloudManifestOptIn")).toBool() ||
      cloud_default_blocked.value(QStringLiteral("cloudEnvironmentOptIn")).toBool()) {
    return fail("configured cloud models should be unavailable unless the manifest explicitly opts in");
  }
  const auto cloud_without_environment =
      model_by_id(discovered_models, QStringLiteral("cloud-explicit-opt-in"));
  if (cloud_without_environment.isEmpty() ||
      cloud_without_environment.value(QStringLiteral("status")).toString() != QStringLiteral("failed") ||
      cloud_without_environment.value(QStringLiteral("available")).toBool() ||
      !cloud_without_environment.value(QStringLiteral("cloudManifestOptIn")).toBool() ||
      cloud_without_environment.value(QStringLiteral("cloudEnvironmentOptIn")).toBool()) {
    return fail("configured cloud models should also require an environment opt-in");
  }
  qputenv("PHOTO_EDITOR_ENABLE_CLOUD_MODELS", QByteArrayLiteral("1"));
  const auto cloud_opted_in_models = photo_editor::desktop::available_agent_local_models();
  const auto cloud_with_environment =
      model_by_id(cloud_opted_in_models, QStringLiteral("cloud-explicit-opt-in"));
  if (cloud_with_environment.isEmpty() ||
      cloud_with_environment.value(QStringLiteral("status")).toString() != QStringLiteral("unavailable") ||
      cloud_with_environment.value(QStringLiteral("available")).toBool() ||
      !cloud_with_environment.value(QStringLiteral("cloudManifestOptIn")).toBool() ||
      !cloud_with_environment.value(QStringLiteral("cloudEnvironmentOptIn")).toBool() ||
      cloud_with_environment.value(QStringLiteral("statusReason")).toString().isEmpty()) {
    return fail("explicit cloud opt-in should be visible but should not enable an unimplemented cloud runner");
  }
  qunsetenv("PHOTO_EDITOR_ENABLE_CLOUD_MODELS");

  const auto ollama_model = model_by_id(discovered_models, QStringLiteral("ollama-qwen-test"));
  if (ollama_model.isEmpty() ||
      ollama_model.value(QStringLiteral("backend")).toString() != QStringLiteral("ollama") ||
      ollama_model.value(QStringLiteral("taskPlanningProtocol")).toString() != QStringLiteral("ollama-chat") ||
      ollama_model.value(QStringLiteral("model")).toString() != QStringLiteral("qwen2.5vl:7b") ||
      !ollama_model.value(QStringLiteral("vision")).toBool() ||
      ollama_model.value(QStringLiteral("requestTimeoutMs")).toInt() != 45000 ||
      ollama_model.value(QStringLiteral("contextWindow")).toInt() != 16384 ||
      ollama_model.value(QStringLiteral("maxImageEdge")).toInt() != 1024 ||
      !ollama_model.value(QStringLiteral("available")).toBool() ||
      ollama_model.value(QStringLiteral("requiresCloudOptIn")).toBool()) {
    return fail("configured Ollama models should be discovered as local available backends");
  }
  if (photo_editor::desktop::default_agent_local_model_id() != QStringLiteral("ollama-qwen-test")) {
    return fail("the first available configured Ollama model should be the real default instead of the deterministic fallback");
  }
  if (!fake_ollama_server_available) {
    qputenv(
        "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
        QByteArrayLiteral(R"json({"model":"qwen2.5vl:7b","details":{"family":"qwen2","parameter_size":"7B","quantization_level":"Q4_K_M"}})json"));
  }
  const auto ollama_probe = photo_editor::desktop::probe_agent_local_model(QStringLiteral("ollama-qwen-test"));
  const auto show_request = fake_ollama_server_available
                                ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/show"))
                                : QJsonObject{{QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")}};
  if (!ollama_probe.value(QStringLiteral("ok")).toBool() ||
      !ollama_probe.value(QStringLiteral("available")).toBool() ||
      ollama_probe.value(QStringLiteral("status")).toString() != QStringLiteral("available") ||
      ollama_probe.value(QStringLiteral("modelFamily")).toString() != QStringLiteral("qwen2") ||
      ollama_probe.value(QStringLiteral("parameterSize")).toString() != QStringLiteral("7B") ||
      ollama_probe.value(QStringLiteral("quantizationLevel")).toString() != QStringLiteral("Q4_K_M") ||
      show_request.value(QStringLiteral("model")).toString() != QStringLiteral("qwen2.5vl:7b")) {
    return fail("Ollama probe should call the local show API and expose model details");
  }
  if (!fake_ollama_server_available) {
    qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  }

  if (fake_ollama_server_available) {
    const auto response_for = [](const QJsonObject& object) {
      return QJsonObject{
          {QStringLiteral("message"), QJsonObject{{QStringLiteral("content"),
              QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact))}}},
          {QStringLiteral("done"), true},
          {QStringLiteral("done_reason"), QStringLiteral("stop")},
      };
    };
    photo_editor::desktop::AgentLocalModelTaskSessionRequest task_request;
    task_request.model_id = QStringLiteral("ollama-qwen-test");
    task_request.brief = QVariantMap{
        {QStringLiteral("prompt"), QStringLiteral("Make an event poster for my photo")},
        {QStringLiteral("version"), 4},
    };
    task_request.context = QVariantMap{
        {QStringLiteral("targetIds"), QStringList{QStringLiteral("asset-17")}},
        {QStringLiteral("blankCanvas"), false},
        {QStringLiteral("inspirationIds"), QStringList{QStringLiteral("look-3")}},
    };
    QString streamed_activity;
    task_request.activity_received = [&streamed_activity](const QString& activity) {
      streamed_activity = activity;
    };
    const QJsonObject question_response{
        {QStringLiteral("decision"), QStringLiteral("ask_questions")},
        {QStringLiteral("activity"), QStringLiteral("Identifying essential missing details")},
        {QStringLiteral("questions"), QJsonArray{QJsonObject{
            {QStringLiteral("id"), QStringLiteral("event_date")},
            {QStringLiteral("prompt"), QStringLiteral("What date should the poster show?")},
            {QStringLiteral("reason"), QStringLiteral("The prompt does not include a date.")},
            {QStringLiteral("required"), true},
            {QStringLiteral("inputType"), QStringLiteral("text")},
            {QStringLiteral("options"), QJsonArray{}},
        }}},
        {QStringLiteral("plan"), QJsonObject{
            {QStringLiteral("summary"), QStringLiteral("Prepare an editable event poster around the supplied photo.")},
            {QStringLiteral("outcome"), QStringLiteral("An editable event poster with the confirmed date.")},
            {QStringLiteral("actions"), QJsonArray{QJsonObject{
                {QStringLiteral("kind"), QStringLiteral("text")},
                {QStringLiteral("summary"), QStringLiteral("Prepare editable poster title and date text.")},
            }}},
            {QStringLiteral("exactCopy"), QJsonArray{}},
            {QStringLiteral("deliverables"), QJsonArray{QStringLiteral("Editable event poster")}},
            {QStringLiteral("constraints"), QJsonArray{QStringLiteral("Keep the original photo")}},
        }},
        {QStringLiteral("reason"), QString()},
        {QStringLiteral("warnings"), QJsonArray{}},
    };
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), response_for(question_response));
    const auto question_result = photo_editor::desktop::plan_agent_task_session(task_request);
    const auto task_chat_request = fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"));
    if (!task_chat_request.value(QStringLiteral("messages")).toArray().first().toObject()
            .value(QStringLiteral("content")).toString().contains(QStringLiteral("looksy-workmanship-v1")))
      return fail("Conversation planning must share the workmanship and truthful delivery contract");
    if (!question_result.ok || question_result.decision != QStringLiteral("ask_questions") ||
        question_result.questions.size() != 1 ||
        question_result.questions.first().toMap().value(QStringLiteral("id")).toString() !=
            QStringLiteral("event_date") ||
        !question_result.plan.isEmpty() ||
        question_result.prompt_audit.value(QStringLiteral("toolDispatchAvailable")).toBool() ||
        streamed_activity != QStringLiteral("Identifying essential missing details") ||
        !task_chat_request.value(QStringLiteral("stream")).toBool() ||
        task_chat_request.contains(QStringLiteral("tools")) ||
        !question_result.prompt_audit.value(QStringLiteral("readOnly")).toBool()) {
      return fail("local task planning should stream factual activity and ask structured read-only questions");
    }

    task_request.answers.insert(QStringLiteral("exact_copy"), QStringLiteral("Autumn Open House"));
    const QJsonObject ready_response{
        {QStringLiteral("decision"), QStringLiteral("plan_ready")},
        {QStringLiteral("activity"), QStringLiteral("Preparing the requested outcome")},
        {QStringLiteral("questions"), QJsonArray{}},
        {QStringLiteral("plan"), QJsonObject{
            {QStringLiteral("summary"), QStringLiteral("Create an editable event poster.")},
            {QStringLiteral("outcome"), QStringLiteral("An editable poster with the supplied wording.")},
            {QStringLiteral("actions"), QJsonArray{QJsonObject{
                {QStringLiteral("kind"), QStringLiteral("text")},
                {QStringLiteral("summary"), QStringLiteral("Set the supplied headline on editable text layers.")},
            }}},
            {QStringLiteral("exactCopy"), QJsonArray{QJsonObject{
                {QStringLiteral("text"), QStringLiteral("Autumn Open House")},
                {QStringLiteral("source"), QStringLiteral("answer")},
            }}},
            {QStringLiteral("deliverables"), QJsonArray{QStringLiteral("Editable event poster")}},
            {QStringLiteral("constraints"), QJsonArray{QStringLiteral("Keep the original photo")}},
        }},
        {QStringLiteral("reason"), QString()},
        {QStringLiteral("warnings"), QJsonArray{}},
    };
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), response_for(ready_response));
    const auto ready_result = photo_editor::desktop::plan_agent_task_session(task_request);
    const auto ready_chat_request = fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"));
    const auto exact_copy = ready_result.plan.value(QStringLiteral("exactCopy")).toList();
    if (!ready_result.ok || ready_result.decision != QStringLiteral("plan_ready") ||
        ready_result.plan.value(QStringLiteral("briefVersion")).toInt() != 4 ||
        exact_copy.size() != 1 ||
        ready_result.plan.value(QStringLiteral("actions")).toList().size() != 1 ||
        exact_copy.first().toMap().value(QStringLiteral("source")).toString() != QStringLiteral("answer")) {
      return fail("structured local planning should preserve only exact user-provided copy");
    }
    const auto task_plan_schema = ready_chat_request.value(QStringLiteral("format")).toObject()
        .value(QStringLiteral("properties")).toObject().value(QStringLiteral("plan")).toObject();
    const auto task_plan_properties = task_plan_schema.value(QStringLiteral("properties")).toObject();
    const auto task_actions_schema = task_plan_properties.value(QStringLiteral("actions")).toObject();
    const auto task_action_item_schema = task_actions_schema.value(QStringLiteral("items")).toObject();
    const auto task_action_properties = task_action_item_schema.value(QStringLiteral("properties")).toObject();
    const auto task_action_kind_schema = task_action_properties.value(QStringLiteral("kind")).toObject();
    if (!task_plan_schema.value(QStringLiteral("required")).toArray().contains(QStringLiteral("actions")) ||
        task_actions_schema.value(QStringLiteral("type")).toString() != QStringLiteral("array") ||
        task_actions_schema.value(QStringLiteral("minItems")).toInt() != 1 ||
        task_actions_schema.value(QStringLiteral("maxItems")).toInt() != 6 ||
        !task_action_item_schema.value(QStringLiteral("required")).toArray().contains(QStringLiteral("kind")) ||
        !task_action_item_schema.value(QStringLiteral("required")).toArray().contains(QStringLiteral("summary")) ||
        !task_action_kind_schema.value(QStringLiteral("enum")).toArray().contains(QStringLiteral("text")) ||
        task_action_properties.value(QStringLiteral("summary")).toObject()
            .value(QStringLiteral("minLength")).toInt() != 1) {
      return fail("the local response schema must require one to six typed outcome actions");
    }

    const auto rejected_action_plan = [&](QJsonObject response) {
      fake_ollama_server.setResponse(QStringLiteral("/api/chat"), response_for(response));
      return photo_editor::desktop::plan_agent_task_session(task_request);
    };
    auto missing_actions_response = ready_response;
    auto missing_actions_plan = missing_actions_response.value(QStringLiteral("plan")).toObject();
    missing_actions_plan.remove(QStringLiteral("actions"));
    missing_actions_response.insert(QStringLiteral("plan"), missing_actions_plan);
    const auto missing_actions_result = rejected_action_plan(missing_actions_response);
    const auto missing_actions_failure = missing_actions_result.prompt_audit
        .value(QStringLiteral("planValidationFailure")).toMap();
    const auto missing_actions_candidate = missing_actions_failure.value(QStringLiteral("candidatePlan")).toMap();
    if (missing_actions_result.ok || !missing_actions_result.plan.isEmpty() ||
        !missing_actions_result.prompt_audit.value(QStringLiteral("readOnly")).toBool() ||
        missing_actions_result.prompt_audit.value(QStringLiteral("toolDispatchAvailable")).toBool() ||
        missing_actions_failure.value(QStringLiteral("code")).toString() != QStringLiteral("invalid_typed_actions") ||
        !missing_actions_failure.value(QStringLiteral("instruction")).toString().contains(QStringLiteral("read-only")) ||
        missing_actions_candidate.isEmpty() ||
        missing_actions_candidate.value(QStringLiteral("summary")).toString().isEmpty()) {
      return fail("a ready task plan missing typed actions must be rejected without an executable plan and retain bounded typed failure context");
    }

    auto empty_actions_response = ready_response;
    auto empty_actions_plan = empty_actions_response.value(QStringLiteral("plan")).toObject();
    empty_actions_plan.insert(QStringLiteral("actions"), QJsonArray{});
    empty_actions_response.insert(QStringLiteral("plan"), empty_actions_plan);
    const auto empty_actions_result = rejected_action_plan(empty_actions_response);
    const auto empty_actions_failure = empty_actions_result.prompt_audit
        .value(QStringLiteral("planValidationFailure")).toMap();
    const auto empty_actions_candidate = empty_actions_failure.value(QStringLiteral("candidatePlan")).toMap();
    if (empty_actions_result.ok || !empty_actions_result.plan.isEmpty() ||
        !empty_actions_result.prompt_audit.value(QStringLiteral("readOnly")).toBool() ||
        empty_actions_result.prompt_audit.value(QStringLiteral("toolDispatchAvailable")).toBool() ||
        empty_actions_failure.value(QStringLiteral("code")).toString() != QStringLiteral("invalid_typed_actions") ||
        empty_actions_failure.value(QStringLiteral("instruction")).toString().trimmed().isEmpty() ||
        empty_actions_candidate.isEmpty() ||
        empty_actions_candidate.value(QStringLiteral("actions")).toList().size() != 0) {
      return fail("a ready task plan with no outcome actions must be rejected and retain its typed candidate plan");
    }

    auto excessive_actions_response = ready_response;
    auto excessive_actions_plan = excessive_actions_response.value(QStringLiteral("plan")).toObject();
    QJsonArray excessive_actions;
    for (int index = 0; index < 7; ++index)
      excessive_actions.append(QJsonObject{
          {QStringLiteral("kind"), QStringLiteral("text")},
          {QStringLiteral("summary"), QStringLiteral("Outcome action %1").arg(index + 1)},
      });
    excessive_actions_plan.insert(QStringLiteral("actions"), excessive_actions);
    excessive_actions_response.insert(QStringLiteral("plan"), excessive_actions_plan);
    const auto excessive_actions_result = rejected_action_plan(excessive_actions_response);
    const auto excessive_actions_failure = excessive_actions_result.prompt_audit
        .value(QStringLiteral("planValidationFailure")).toMap();
    const auto excessive_actions_candidate = excessive_actions_failure.value(QStringLiteral("candidatePlan")).toMap();
    if (excessive_actions_result.ok || !excessive_actions_result.plan.isEmpty() ||
        !excessive_actions_result.prompt_audit.value(QStringLiteral("readOnly")).toBool() ||
        excessive_actions_result.prompt_audit.value(QStringLiteral("toolDispatchAvailable")).toBool() ||
        excessive_actions_failure.value(QStringLiteral("code")).toString() != QStringLiteral("invalid_typed_actions") ||
        excessive_actions_failure.value(QStringLiteral("instruction")).toString().trimmed().isEmpty() ||
        excessive_actions_candidate.value(QStringLiteral("actions")).toList().size() != 7) {
      return fail("a ready task plan with more than six actions must fail closed without an executable plan");
    }

    auto redundant_question_response = ready_response;
    redundant_question_response.insert(QStringLiteral("activity"), QStringLiteral("Laying out the thumbnail"));
    redundant_question_response.insert(QStringLiteral("questions"), question_response.value(QStringLiteral("questions")));
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), response_for(redundant_question_response));
    const auto redundant_question_result = photo_editor::desktop::plan_agent_task_session(task_request);
    if (!redundant_question_result.ok || redundant_question_result.decision != QStringLiteral("plan_ready") ||
        !redundant_question_result.questions.isEmpty())
      return fail("cosmetic progress wording and stale optional questions must not block an otherwise valid ready plan");

    auto invalid_action_response = ready_response;
    auto invalid_action_plan = invalid_action_response.value(QStringLiteral("plan")).toObject();
    invalid_action_plan.insert(QStringLiteral("actions"), QJsonArray{QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("delete_sources")},
        {QStringLiteral("summary"), QStringLiteral("Remove originals.")},
    }});
    invalid_action_response.insert(QStringLiteral("plan"), invalid_action_plan);
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), response_for(invalid_action_response));
    const auto invalid_action_result = photo_editor::desktop::plan_agent_task_session(task_request);
    if (invalid_action_result.ok || !invalid_action_result.plan.isEmpty() ||
        !invalid_action_result.error_message.contains(QStringLiteral("action"))) {
      return fail("unsupported outcome actions must not become an approved plan");
    }


    auto canvas_response = ready_response;
    auto canvas_plan = canvas_response.value(QStringLiteral("plan")).toObject();
    canvas_plan.insert(QStringLiteral("canvasSettings"), QJsonObject{
        {"name", "Autumn Open House"}, {"width", 2550}, {"height", 3300}, {"resolution", 300},
        {"background", "white"}, {"color", ""}});
    canvas_response.insert(QStringLiteral("plan"), canvas_plan);
    task_request.context.insert(QStringLiteral("blankCanvas"), true);
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), response_for(canvas_response));
    const auto canvas_result = photo_editor::desktop::plan_agent_task_session(task_request);
    if (!canvas_result.ok || canvas_result.plan.value(QStringLiteral("canvasSettings")).toMap().value("width").toInt() != 2550)
      return fail("A blank-canvas plan must preserve approved output settings without creating a file");
    const auto canvas_request_schema = fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
        .value(QStringLiteral("format")).toObject().value(QStringLiteral("properties")).toObject()
        .value(QStringLiteral("plan")).toObject();
    if (!canvas_request_schema.value(QStringLiteral("required")).toArray().contains(QStringLiteral("canvasSettings")))
      return fail("Blank canvas dimensions must be required in the actual model response schema, not only checked after generation");
    auto bad_settings = canvas_plan.value(QStringLiteral("canvasSettings")).toObject();
    bad_settings.insert("width", 8192);
    bad_settings.insert("height", 8192);
    canvas_plan.insert(QStringLiteral("canvasSettings"), bad_settings);
    canvas_response.insert(QStringLiteral("plan"), canvas_plan);
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), response_for(canvas_response));
    if (photo_editor::desktop::plan_agent_task_session(task_request).ok)
      return fail("Unallocatable canvas settings must not become an approved creation plan");
    task_request.context.insert(QStringLiteral("blankCanvas"), false);

    // A new fixture sequence must start at its own first response rather than
    // inherit the process-global position from an earlier conversation.
    qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON", QJsonDocument(QJsonArray{
        response_for(question_response), response_for(ready_response)}).toJson(QJsonDocument::Compact));
    const auto first_sequence_result = photo_editor::desktop::plan_agent_task_session(task_request);
    qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON", QJsonDocument(QJsonArray{
        response_for(ready_response), response_for(question_response)}).toJson(QJsonDocument::Compact));
    const auto replacement_sequence_result = photo_editor::desktop::plan_agent_task_session(task_request);
    qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON");
    if (!first_sequence_result.ok || first_sequence_result.decision != QStringLiteral("ask_questions") ||
        !replacement_sequence_result.ok || replacement_sequence_result.decision != QStringLiteral("plan_ready"))
      return fail("Replacing a fake model sequence must reset its response position");

    auto invented_copy_response = ready_response;
    auto invented_plan = invented_copy_response.value(QStringLiteral("plan")).toObject();
    invented_plan.insert(QStringLiteral("exactCopy"), QJsonArray{QJsonObject{
        {QStringLiteral("text"), QStringLiteral("FREE ADMISSION")},
        {QStringLiteral("source"), QStringLiteral("proposed")},
    }});
    invented_plan.insert(QStringLiteral("deliverables"), QJsonArray{QStringLiteral("Poster headline text")});
    invented_copy_response.insert(QStringLiteral("plan"), invented_plan);
    task_request.answers.clear();
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), response_for(invented_copy_response));
    const auto invented_result = photo_editor::desktop::plan_agent_task_session(task_request);
    if (!invented_result.ok || invented_result.decision != QStringLiteral("ask_questions") ||
        invented_result.questions.size() != 1 || !invented_result.plan.isEmpty() ||
        invented_result.questions.first().toMap().value(QStringLiteral("id")).toString() !=
            QStringLiteral("exact_copy")) {
      return fail("unsourced exact copy must be withheld and replaced with a required wording question");
    }

    task_request.brief.insert(QStringLiteral("prompt"),
                              QStringLiteral("Suggest a catchy headline for this event poster."));
    auto suggested_copy_response = ready_response;
    auto suggested_plan = suggested_copy_response.value(QStringLiteral("plan")).toObject();
    suggested_plan.insert(QStringLiteral("exactCopy"), QJsonArray{QJsonObject{
        {QStringLiteral("text"), QStringLiteral("Celebrate the Season")},
        {QStringLiteral("source"), QStringLiteral("proposed")},
    }});
    suggested_plan.insert(QStringLiteral("deliverables"), QJsonArray{QStringLiteral("Poster headline text")});
    suggested_copy_response.insert(QStringLiteral("plan"), suggested_plan);
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), response_for(suggested_copy_response));
    const auto suggested_copy_result = photo_editor::desktop::plan_agent_task_session(task_request);
    const auto suggested_copy = suggested_copy_result.plan.value(QStringLiteral("exactCopy")).toList();
    if (!suggested_copy_result.ok || suggested_copy_result.decision != QStringLiteral("plan_ready") ||
        !suggested_copy_result.prompt_audit.value(QStringLiteral("creativeCopyAllowed")).toBool() ||
        !suggested_copy_result.plan.value(QStringLiteral("copySuggestionApprovalRequired")).toBool() ||
        suggested_copy.size() != 1 ||
        suggested_copy.first().toMap().value(QStringLiteral("source")).toString() != QStringLiteral("proposed") ||
        !suggested_copy.first().toMap().value(QStringLiteral("needsApproval")).toBool()) {
      return fail("explicitly requested creative copy should be labeled as a proposed item requiring Start approval");
    }
    auto proposed_date_response = suggested_copy_response;
    auto proposed_date_plan = proposed_date_response.value(QStringLiteral("plan")).toObject();
    proposed_date_plan.insert(QStringLiteral("exactCopy"), QJsonArray{QJsonObject{
        {QStringLiteral("text"), QStringLiteral("October 20: Celebrate the Season")},
        {QStringLiteral("source"), QStringLiteral("proposed")},
    }});
    proposed_date_response.insert(QStringLiteral("plan"), proposed_date_plan);
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), response_for(proposed_date_response));
    const auto proposed_date_result = photo_editor::desktop::plan_agent_task_session(task_request);
    if (!proposed_date_result.ok || proposed_date_result.decision != QStringLiteral("ask_questions")) {
      return fail("unknown factual copy should ask a skippable question instead of failing planning");
    }
    task_request.brief.insert(QStringLiteral("useDefaults"), true);
    const auto placeholder_result = photo_editor::desktop::plan_agent_task_session(task_request);
    if (!placeholder_result.ok || placeholder_result.decision != QStringLiteral("plan_ready") ||
        placeholder_result.plan.value(QStringLiteral("exactCopy")).toList().first().toMap()
            .value(QStringLiteral("text")).toString() != QStringLiteral("[Headline / event details]")) {
      return fail("using judgment must replace unknown factual copy with a visible editable placeholder");
    }
    const auto default_schema = fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
        .value(QStringLiteral("format")).toObject().value(QStringLiteral("properties")).toObject();
    if (default_schema.value(QStringLiteral("decision")).toObject().value(QStringLiteral("enum"))
            .toArray().contains(QStringLiteral("ask_questions")) ||
        default_schema.value(QStringLiteral("questions")).toObject().value(QStringLiteral("maxItems")).toInt() != 0) {
      return fail("the model response schema must end clarification when the user chooses defaults");
    }
    task_request.brief.remove(QStringLiteral("useDefaults"));
    task_request.brief.insert(QStringLiteral("prompt"), QStringLiteral("Make a thumbnail using this photo."));
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), response_for(suggested_copy_response));
    const auto ordinary_copy = photo_editor::desktop::plan_agent_task_session(task_request);
    if (!ordinary_copy.ok || ordinary_copy.decision != QStringLiteral("plan_ready") ||
        !ordinary_copy.plan.value(QStringLiteral("copySuggestionApprovalRequired")).toBool()) {
      return fail("an ordinary creative prompt must allow reviewable proposed wording without magic prompt syntax");
    }
    auto existing_response = ready_response;
    auto existing_plan = existing_response.value(QStringLiteral("plan")).toObject();
    existing_plan.insert(QStringLiteral("exactCopy"), QJsonArray{});
    existing_response.insert(QStringLiteral("plan"), existing_plan);
    task_request.brief.insert(QStringLiteral("prompt"), QStringLiteral("Improve this title layout, keep existing text."));
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), response_for(existing_response));
    const auto existing_text = photo_editor::desktop::plan_agent_task_session(task_request);
    if (!existing_text.ok || existing_text.decision != QStringLiteral("plan_ready"))
      return fail("preserving existing text must not demand that the user retype every title");
    task_request.brief.insert(QStringLiteral("prompt"), QStringLiteral("Make a thumbnail, do not generate text."));
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), response_for(suggested_copy_response));
    if (photo_editor::desktop::plan_agent_task_session(task_request).decision != QStringLiteral("ask_questions"))
      return fail("an explicit prohibition on generated wording must remain respected");
  }

  const auto warm_style = style_by_id(QStringLiteral("warm-editorial-wedding"));
  if (warm_style.isEmpty()) {
    return fail("warm built-in style should exist");
  }

  photo_editor::desktop::AgentLocalModelPlanRequest request;
  request.model_id = QStringLiteral("rulebook-deterministic");
  const auto context_budget_summary = [](const auto& plan) {
    const auto components = plan.prompt_audit.value(QStringLiteral("promptComponents")).toMap();
    const QVariantMap summary{
        {QStringLiteral("modelId"), plan.model_id},
        {QStringLiteral("errorCode"), plan.prompt_audit.value(QStringLiteral("errorCode"))},
        {QStringLiteral("error"), plan.error_message.simplified().left(180)},
        {QStringLiteral("requestSent"), plan.prompt_audit.value(QStringLiteral("requestSent"))},
        {QStringLiteral("aggregateTextUtf8Bytes"),
            components.value(QStringLiteral("aggregateTextUtf8Bytes"))},
        {QStringLiteral("estimatedContextTokensBeforeCap"),
            components.value(QStringLiteral("estimatedContextTokensBeforeCap"))},
        {QStringLiteral("contextTokenLimit"), components.value(QStringLiteral("contextTokenLimit"))},
        {QStringLiteral("contextTokensRequested"),
            components.value(QStringLiteral("contextTokensRequested"))},
        {QStringLiteral("systemPromptUtf8Bytes"),
            components.value(QStringLiteral("systemPromptUtf8Bytes"))},
        {QStringLiteral("userPromptUtf8Bytes"),
            components.value(QStringLiteral("userPromptUtf8Bytes"))},
        {QStringLiteral("attachedImageCount"), components.value(QStringLiteral("attachedImageCount"))},
        {QStringLiteral("planningMaxOutputTokens"),
            components.value(QStringLiteral("planningMaxOutputTokens"))},
    };
    return QString::fromUtf8(QJsonDocument::fromVariant(summary)
        .toJson(QJsonDocument::Compact));
  };
  const auto plan_with_app09_budget_model = [](
      photo_editor::desktop::AgentLocalModelPlanRequest planned_request) {
    planned_request.model_id = QStringLiteral("ollama-app09-budget-test");
    return photo_editor::desktop::plan_agent_recipe_patch(planned_request);
  };
  request.style = warm_style;
  request.user_instructions = QStringLiteral("Keep it bright and soft but preserve highlights.");
  request.strategy_phase = QStringLiteral("sample");
  request.memory_packet = QVariantMap{
      {QStringLiteral("phase"), QStringLiteral("sample")},
      {QStringLiteral("includedMemoryKinds"), QStringList{QStringLiteral("style"), QStringLiteral("tool")}},
      {QStringLiteral("rawVaultIncluded"), false},
  };
  const auto plan = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (!plan.ok || plan.recipe_patch.isEmpty() || plan.plan_summary.isEmpty()) {
    return fail("deterministic local model should return a structured edit plan");
  }
  if (plan.prompt_audit.value(QStringLiteral("backend")).toString() != QStringLiteral("local") ||
      plan.prompt_audit.value(QStringLiteral("strategyPhase")).toString() != QStringLiteral("sample") ||
      !plan.prompt_audit.value(QStringLiteral("deterministicPlanning")).toBool() ||
      plan.prompt_audit.value(QStringLiteral("temperature")).toDouble() != 0.0 ||
      plan.prompt_audit.value(QStringLiteral("rawMemoryVaultIncluded")).toBool() ||
      !plan.prompt_audit.value(QStringLiteral("userPrompt")).toString().contains(QStringLiteral("warm-editorial-wedding")) ||
      !plan.prompt_audit.value(QStringLiteral("userPrompt")).toString().contains(QStringLiteral("Phase: sample")) ||
      plan.prompt_audit.value(QStringLiteral("userPromptCharCount")).toInt() <= 0) {
    return fail("deterministic local model should record phase-scoped deterministic prompt audit metadata");
  }
  if (plan.style_constraint_citations.isEmpty() ||
      !has_citation_field(
          plan.style_constraint_citations,
          QStringLiteral("constraints.max_exposure_shift")) ||
      !has_citation_field(
          plan.style_constraint_citations,
          QStringLiteral("constraints.max_temperature_shift")) ||
      !has_citation_field(
          plan.style_constraint_citations,
          QStringLiteral("constraints.color_constraints.max_hsl_channel_shift")) ||
      !has_citation_field(
          plan.style_constraint_citations,
          QStringLiteral("constraints.color_constraints.max_color_grading_saturation")) ||
      !has_citation_field(
          plan.style_constraint_citations,
          QStringLiteral("validation.highlight_clip_limit_percent"))) {
    return fail("local model plan should cite active style constraints and validation gates");
  }
  const auto color_patch = plan.recipe_patch.value(QStringLiteral("color")).toMap();
  const auto tone_patch = plan.recipe_patch.value(QStringLiteral("globalTone")).toMap();
  const auto color_mixer_patch = plan.recipe_patch.value(QStringLiteral("colorMixer")).toMap();
  const auto color_grading_patch = plan.recipe_patch.value(QStringLiteral("colorGrading")).toMap();
  if (color_patch.value(QStringLiteral("temperature")).toDouble() <= 0.0 ||
      tone_patch.value(QStringLiteral("highlights")).toDouble() >= 0.0) {
    return fail("warm style plan should warm color and protect highlights");
  }
  auto calibrated_request = request;
  calibrated_request.memory_packet.insert(
      QStringLiteral("includedMemoryKinds"),
      QStringList{QStringLiteral("style"), QStringLiteral("tool"),
                  QStringLiteral("photographer-calibration")});
  calibrated_request.memory_packet.insert(
      QStringLiteral("photographerCalibrationCard"),
      QVariantMap{
          {QStringLiteral("contract"),
           QStringLiteral("agent-photographer-calibration-card-v1")},
          {QStringLiteral("available"), true},
          {QStringLiteral("profileDigestSha256"),
           QString(64, QLatin1Char('a'))},
          {QStringLiteral("issueTagCounts"),
           QVariantMap{{QStringLiteral("too-warm"), 1}}},
          {QStringLiteral("validationConcernKeys"),
           QStringList{QStringLiteral("warmth")}},
          {QStringLiteral("planningGuidance"),
           QStringList{QStringLiteral(
               "Prioritize naturalness and avoid excess warmth.")}},
          {QStringLiteral("numericThresholdOverridesAllowed"), false},
      });
  const auto calibrated_plan =
      photo_editor::desktop::plan_agent_recipe_patch(calibrated_request);
  const auto calibrated_color =
      calibrated_plan.recipe_patch.value(QStringLiteral("color")).toMap();
  if (!calibrated_plan.ok ||
      calibrated_color.value(QStringLiteral("temperature")).toDouble() <= 0.0 ||
      calibrated_color.value(QStringLiteral("temperature")).toDouble() >=
          color_patch.value(QStringLiteral("temperature")).toDouble() ||
      !calibrated_plan.prompt_audit
           .value(QStringLiteral("photographerCalibrationAvailable"))
           .toBool() ||
      !calibrated_plan.prompt_audit
           .value(QStringLiteral("photographerCalibrationApplied"))
           .toBool() ||
      !calibrated_plan.prompt_audit
           .value(QStringLiteral("photographerCalibrationAdjustedGroups"))
           .toStringList()
           .contains(QStringLiteral("warmth")) ||
      calibrated_plan.prompt_audit
          .value(QStringLiteral(
              "photographerCalibrationNumericThresholdOverrides"))
          .toBool() ||
      calibrated_plan.warnings.isEmpty() ||
      !calibrated_plan.plan_summary.contains(
          QStringLiteral("blind photographer calibration"))) {
    return fail("deterministic planning should conservatively temper a matching response using project-local photographer calibration without changing thresholds");
  }
  auto north_star_request = request;
  north_star_request.memory_packet.insert(
      QStringLiteral("includedMemoryKinds"),
      QStringList{QStringLiteral("style"), QStringLiteral("tool"),
                  QStringLiteral("project-brief")});
  north_star_request.memory_packet.insert(
      QStringLiteral("projectNorthStar"),
      QVariantMap{
          {QStringLiteral("contract"),
           QStringLiteral("agent-project-north-star-memory-v1")},
          {QStringLiteral("available"), true},
          {QStringLiteral("projectKey"), QStringLiteral("Wedding 2026")},
          {QStringLiteral("brief"),
           QStringLiteral(
               "Keep the assignment warm, monochrome, and tightly cropped.")},
          {QStringLiteral("digestSha256"), QString(64, QLatin1Char('c'))},
          {QStringLiteral("authority"), QStringLiteral("qualitative-only")},
      });
  north_star_request.user_instructions = QStringLiteral(
      "Keep it in color, make it cool and natural, and preserve framing.");
  const auto north_star_plan =
      photo_editor::desktop::plan_agent_recipe_patch(north_star_request);
  if (!north_star_plan.ok ||
      north_star_plan.edit_intent.value(QStringLiteral("colorMode")).toString() !=
          QStringLiteral("color") ||
      north_star_plan.edit_intent.value(QStringLiteral("palette")).toString() !=
          QStringLiteral("cool") ||
      north_star_plan.edit_intent.value(QStringLiteral("crop")).toString() !=
          QStringLiteral("preserve") ||
      north_star_plan.recipe_patch
              .value(QStringLiteral("color"))
              .toMap()
              .value(QStringLiteral("temperature"))
              .toDouble() >= 0.0 ||
      !north_star_plan.prompt_audit
           .value(QStringLiteral("projectNorthStarAvailable"))
           .toBool() ||
      north_star_plan.prompt_audit
              .value(QStringLiteral("projectNorthStarDigest"))
              .toString() != QString(64, QLatin1Char('c')) ||
      !north_star_plan.prompt_audit
           .value(QStringLiteral("projectNorthStarCurrentRequestWins"))
           .toBool() ||
      !north_star_plan.prompt_audit
           .value(QStringLiteral("userPrompt"))
           .toString()
           .contains(QStringLiteral("Project North Star: Keep the assignment warm")) ||
      !north_star_plan.prompt_audit
           .value(QStringLiteral("userPrompt"))
           .toString()
           .contains(QStringLiteral("Current request: Keep it in color"))) {
    return fail("project North Star should shape deterministic planning while explicit current-request color, palette, and framing constraints win conflicts");
  }
  if (color_mixer_patch.value(QStringLiteral("hslChannel")).toString() != QStringLiteral("Oranges") ||
      color_mixer_patch.value(QStringLiteral("orangesSaturation")).toDouble() > 12.0 ||
      !color_mixer_patch.value(QStringLiteral("skinToneProtectionEnabled")).toBool() ||
      color_grading_patch.value(QStringLiteral("gradingMidtonesSaturation")).toDouble() > 12.0 ||
      !color_grading_patch.value(QStringLiteral("colorHarmonyEnabled")).toBool()) {
    return fail("warm style plan should honor color-specific HSL, grading, harmony, and protected-skin constraints");
  }
  const auto validation =
      photo_editor::desktop::validate_agent_recipe_patch(plan.recipe_patch, warm_style);
  if (!validation.valid) {
    std::cerr << validation.errors.join(QStringLiteral("; ")).toStdString() << '\n';
    return fail("local model plans must satisfy recipe patch validation");
  }

  auto constrained_style = warm_style;
  auto constraints = constrained_style.value(QStringLiteral("constraints")).toMap();
  constraints.insert(QStringLiteral("max_exposure_shift"), 0.1);
  constraints.insert(QStringLiteral("max_saturation_shift"), 4.0);
  constrained_style.insert(QStringLiteral("constraints"), constraints);
  request.style = constrained_style;
  request.user_instructions = QStringLiteral("Make it bright and colorful.");
  const auto constrained_plan = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (!constrained_plan.ok) {
    return fail("constrained style should still produce a plan");
  }
  const auto constrained_tone =
      constrained_plan.recipe_patch.value(QStringLiteral("globalTone")).toMap();
  const auto constrained_color =
      constrained_plan.recipe_patch.value(QStringLiteral("color")).toMap();
  if (constrained_tone.value(QStringLiteral("exposure")).toDouble() > 0.1 ||
      constrained_color.value(QStringLiteral("vibrance")).toDouble() > 4.0) {
    return fail("local model plan should honor style max shift constraints");
  }

  request.style = warm_style;
  request.user_instructions =
      QStringLiteral("Make it super contrasty black and white and give it a better crop.");
  const auto explicit_intent_plan =
      photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto explicit_color =
      explicit_intent_plan.recipe_patch.value(QStringLiteral("color")).toMap();
  const auto explicit_tone =
      explicit_intent_plan.recipe_patch.value(QStringLiteral("globalTone")).toMap();
  const auto explicit_crop =
      explicit_intent_plan.recipe_patch.value(QStringLiteral("crop")).toMap();
  if (!explicit_intent_plan.ok ||
      explicit_intent_plan.edit_intent.value(QStringLiteral("colorMode")).toString() !=
          QStringLiteral("monochrome") ||
      explicit_intent_plan.edit_intent.value(QStringLiteral("contrast")).toString() !=
          QStringLiteral("strong") ||
      explicit_intent_plan.edit_intent.value(QStringLiteral("crop")).toString() ==
          QStringLiteral("preserve") ||
      explicit_color.value(QStringLiteral("saturation")).toDouble() != -100.0 ||
      explicit_tone.value(QStringLiteral("contrast")).toDouble() < 35.0 ||
      explicit_crop.value(QStringLiteral("cropWidth")).toDouble() >= 0.98 ||
      explicit_crop.value(QStringLiteral("cropHeight")).toDouble() >= 0.98 ||
      explicit_intent_plan.recipe_patch.contains(QStringLiteral("colorGrading")) ||
      explicit_intent_plan.recipe_patch.contains(QStringLiteral("colorMixer"))) {
    return fail("explicit monochrome, strong-contrast, and crop instructions must survive style constraints");
  }

  request.user_instructions =
      QStringLiteral("Make it cooler, quieter, and more editorial.");
  const auto alternative_direction_plan =
      photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto alternative_color =
      alternative_direction_plan.recipe_patch.value(QStringLiteral("color")).toMap();
  const auto alternative_tone =
      alternative_direction_plan.recipe_patch.value(QStringLiteral("globalTone")).toMap();
  if (!alternative_direction_plan.ok ||
      alternative_direction_plan.edit_intent.value(QStringLiteral("palette")).toString() !=
          QStringLiteral("cool") ||
      alternative_direction_plan.edit_intent.value(QStringLiteral("contrast")).toString() !=
          QStringLiteral("soft") ||
      alternative_color.value(QStringLiteral("temperature")).toDouble() >= 0.0 ||
      alternative_color.value(QStringLiteral("saturation")).toDouble() >= 0.0 ||
      alternative_tone.value(QStringLiteral("contrast")).toDouble() >= 0.0) {
    return fail("explicit user direction should override conflicting warm style adjectives in the deterministic plan");
  }

  request.current_adjustment_patch_json = QStringLiteral(
      R"json({"format":"photo-editor-adjustment-patch","version":1,"scope":"asset-bound","adjustmentState":{"globalAdjustments":{"exposure":1.25,"contrast":10.0,"highlights":-18.0,"shadows":44.0,"whites":8.0,"blacks":14.0,"temperature":5.0,"tint":1.0,"vibrance":18.0,"saturation":4.0}}})json");
  request.user_instructions =
      QStringLiteral("Make this image brighter overall while preserving natural color.");
  const auto relative_brightness_plan =
      photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto relative_brightness_tone =
      relative_brightness_plan.recipe_patch.value(QStringLiteral("globalTone")).toMap();
  if (!relative_brightness_plan.ok ||
      relative_brightness_tone.value(QStringLiteral("exposure")).toDouble() <= 1.25 ||
      relative_brightness_tone.value(QStringLiteral("contrast")).toDouble() != 10.0) {
    return fail("directional brightness edits should move from the current recipe instead of replacing it with a neutral-style absolute value");
  }

  request.user_instructions =
      QStringLiteral("Make the foliage more vibrant with richer color.");
  const auto relative_saturation_plan =
      photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto relative_saturation_color =
      relative_saturation_plan.recipe_patch.value(QStringLiteral("color")).toMap();
  if (!relative_saturation_plan.ok ||
      relative_saturation_plan.edit_intent.value(QStringLiteral("saturation")).toString() !=
          QStringLiteral("more") ||
      relative_saturation_color.value(QStringLiteral("vibrance")).toDouble() <= 18.0 ||
      relative_saturation_color.value(QStringLiteral("saturation")).toDouble() <= 4.0 ||
      relative_saturation_plan.recipe_patch.contains(QStringLiteral("colorGrading")) ||
      relative_saturation_plan.recipe_patch.contains(QStringLiteral("colorMixer"))) {
    return fail("explicit richer-color edits should increase current color intensity without adding unrelated style color transforms");
  }

  request.user_instructions =
      QStringLiteral("Make this portrait warmer overall while keeping the tone unchanged.");
  const auto preserve_tone_warm_plan =
      photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto preserve_tone_warm_global =
      preserve_tone_warm_plan.recipe_patch.value(QStringLiteral("globalTone")).toMap();
  const auto preserve_tone_warm_color =
      preserve_tone_warm_plan.recipe_patch.value(QStringLiteral("color")).toMap();
  if (!preserve_tone_warm_plan.ok ||
      preserve_tone_warm_color.value(QStringLiteral("temperature")).toDouble() <= 5.0 ||
      preserve_tone_warm_global.value(QStringLiteral("exposure")).toDouble() != 1.25 ||
      preserve_tone_warm_global.value(QStringLiteral("contrast")).toDouble() != 10.0) {
    return fail("explicit warmer edits with tone preservation should change current white balance without rewriting tone");
  }
  request.current_adjustment_patch_json.clear();

  auto reference_style = warm_style;
  auto reference_constraints = reference_style.value(QStringLiteral("constraints")).toMap();
  auto reference_color_constraints =
      reference_constraints.value(QStringLiteral("color_constraints")).toMap();
  reference_color_constraints.insert(
      QStringLiteral("preferred_temperature_bias"), QStringLiteral("neutral"));
  reference_constraints.insert(QStringLiteral("color_constraints"), reference_color_constraints);
  reference_style.insert(QStringLiteral("constraints"), reference_constraints);
  reference_style.insert(
      QStringLiteral("learnedProfile"),
      QVariantMap{
          {QStringLiteral("source_asset_count"), 1},
          {QStringLiteral("editTendencies"),
           QVariantMap{
               {QStringLiteral("averageExposure"), -0.53},
               {QStringLiteral("averageContrast"), -89.0},
               {QStringLiteral("averageHighlights"), 81.0},
               {QStringLiteral("averageShadows"), 24.0},
               {QStringLiteral("averageTemperature"), -23.9},
               {QStringLiteral("averageTint"), 16.8},
               {QStringLiteral("averageVibrance"), 94.0},
               {QStringLiteral("averageSaturation"), -27.0},
           }},
      });
  request.style = reference_style;
  request.user_instructions = QStringLiteral("Follow the selected inspiration and make the color funky.");
  const auto reference_plan = photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto reference_tone = reference_plan.recipe_patch.value(QStringLiteral("globalTone")).toMap();
  const auto reference_color = reference_plan.recipe_patch.value(QStringLiteral("color")).toMap();
  if (!reference_plan.ok ||
      !reference_plan.prompt_audit.value(QStringLiteral("learnedReferenceApplied")).toBool() ||
      reference_plan.prompt_audit.value(QStringLiteral("learnedReferenceAssetCount")).toInt() != 1 ||
      std::abs(reference_tone.value(QStringLiteral("exposure")).toDouble() - (-0.53)) > 0.001 ||
      std::abs(reference_tone.value(QStringLiteral("contrast")).toDouble() - (-89.0)) > 0.001 ||
      std::abs(reference_color.value(QStringLiteral("temperature")).toDouble() - (-23.9)) > 0.001 ||
      std::abs(reference_color.value(QStringLiteral("vibrance")).toDouble() - 94.0) > 0.001 ||
      std::abs(reference_color.value(QStringLiteral("saturation")).toDouble() - (-27.0)) > 0.001) {
    return fail("deterministic fallback should apply the selected reference profile instead of a generic recipe");
  }

  const auto fast_highlight_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/fast-highlight.png"));
  if (!QDir().mkpath(QFileInfo(fast_highlight_path).dir().absolutePath())) {
    return fail("fast local vision fixture directory should be created");
  }
  QImage fast_highlight_fixture(800, 600, QImage::Format_RGB32);
  fast_highlight_fixture.fill(QColor(66, 74, 76));
  for (auto y = 40; y < 250; ++y) {
    for (auto x = 575; x < 770; ++x) {
      fast_highlight_fixture.setPixelColor(x, y, QColor(254, 248, 225));
    }
  }
  if (!fast_highlight_fixture.save(fast_highlight_path)) {
    return fail("fast local vision fixture should be writable");
  }
  photo_editor::desktop::AgentLocalModelPlanRequest fast_highlight_request;
  fast_highlight_request.model_id = QStringLiteral("ollama-qwen-test");
  fast_highlight_request.style = warm_style;
  fast_highlight_request.asset = QVariantMap{
      {QStringLiteral("assetId"), QStringLiteral("fast-highlight")},
      {QStringLiteral("filePath"), fast_highlight_path},
  };
  fast_highlight_request.current_adjustment_patch_json =
      QStringLiteral("{\"kind\":\"photo-editor-adjustment-patch\",\"version\":1,\"scope\":\"asset-bound\",\"groups\":{}}");
  fast_highlight_request.user_instructions = QStringLiteral(
      "Keep realistic color and framing. Subdue only the bright upper-right window so the rest remains unchanged.");
  QElapsedTimer fast_highlight_timer;
  fast_highlight_timer.start();
  const auto fast_highlight_plan =
      photo_editor::desktop::plan_agent_recipe_patch(fast_highlight_request);
  const auto fast_highlight_elapsed_ms = fast_highlight_timer.elapsed();
  const auto fast_highlight_actions =
      fast_highlight_plan.edit_intent.value(QStringLiteral("localizedActions")).toList();
  const auto fast_highlight_action =
      fast_highlight_actions.isEmpty() ? QVariantMap{} : fast_highlight_actions.first().toMap();
  const auto fast_highlight_findings =
      fast_highlight_plan.edit_intent.value(QStringLiteral("visualFindings")).toList();
  const auto fast_highlight_finding =
      fast_highlight_findings.isEmpty() ? QVariantMap{} : fast_highlight_findings.first().toMap();
  const auto fast_highlight_masks = fast_highlight_plan.recipe_patch
                                        .value(QStringLiteral("masks"))
                                        .toMap()
                                        .value(QStringLiteral("appendMaskStack"))
                                        .toList();
  const auto fast_highlight_mask =
      fast_highlight_masks.isEmpty() ? QVariantMap{} : fast_highlight_masks.first().toMap();
  const auto fast_highlight_region =
      fast_highlight_action.value(QStringLiteral("region")).toMap();
  const auto fast_highlight_mask_id =
      fast_highlight_action.value(QStringLiteral("maskId")).toString();
  const auto fast_highlight_replay =
      photo_editor::desktop::plan_agent_recipe_patch(fast_highlight_request);
  const auto fast_highlight_replay_actions =
      fast_highlight_replay.edit_intent.value(QStringLiteral("localizedActions")).toList();
  const auto fast_highlight_replay_mask_id =
      fast_highlight_replay_actions.isEmpty()
          ? QString{}
          : fast_highlight_replay_actions.first()
                .toMap()
                .value(QStringLiteral("maskId"))
                .toString();
  if (!fast_highlight_plan.ok ||
      fast_highlight_plan.model_id != QStringLiteral("fast-local-vision") ||
      fast_highlight_plan.prompt_audit.value(QStringLiteral("plannerMode")).toString() !=
          QStringLiteral("fast-local-highlight") ||
      fast_highlight_plan.prompt_audit.value(QStringLiteral("modelInvoked")).toBool() ||
      fast_highlight_plan.edit_intent.value(QStringLiteral("globalChangePolicy")).toString() !=
          QStringLiteral("preserve") ||
      fast_highlight_actions.size() != 1 ||
      fast_highlight_findings.size() != 1 ||
      fast_highlight_mask_id.isEmpty() ||
      fast_highlight_finding.value(QStringLiteral("maskId")).toString() !=
          fast_highlight_mask_id ||
      fast_highlight_mask.value(QStringLiteral("id")).toString() !=
          fast_highlight_mask_id ||
      fast_highlight_replay_mask_id != fast_highlight_mask_id ||
      fast_highlight_region.value(QStringLiteral("centerX")).toDouble() < 0.65 ||
      fast_highlight_region.value(QStringLiteral("centerY")).toDouble() > 0.45 ||
      fast_highlight_plan.recipe_patch.value(QStringLiteral("masks"))
          .toMap()
          .value(QStringLiteral("appendMaskStack"))
          .toList()
          .size() != 1 ||
      fast_highlight_plan.recipe_patch.contains(QStringLiteral("globalTone")) ||
      fast_highlight_elapsed_ms >= 1000) {
    return fail("explicit localized highlight requests should use an immediate grounded planner with linked finding, action, and mask identities");
  }

  const auto fake_image_path = QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/source-file.raw"));
  if (!QDir().mkpath(QFileInfo(fake_image_path).dir().absolutePath())) {
    return fail("fake proof fixture directory should be created");
  }
  QFile fake_image_file(fake_image_path);
  if (!fake_image_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return fail("fake proof fixture should be writable");
  }
  fake_image_file.write("fake source bytes");
  fake_image_file.close();
  const auto fake_proof_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/fake-rendered-proof.png"));
  QFile fake_proof_file(fake_proof_path);
  if (!fake_proof_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return fail("fake rendered proof fixture should be writable");
  }
  const QByteArray fake_proof_bytes("fake rendered proof bytes");
  fake_proof_file.write(fake_proof_bytes);
  fake_proof_file.close();
  const QJsonObject model_plan{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"), QStringLiteral("Fake Ollama warm proof edit")},
      {QStringLiteral("styleConstraintCitations"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("field"), QStringLiteral("constraints.max_exposure_shift")},
               {QStringLiteral("reason"), QStringLiteral("Limits exposure changes.")},
           },
           QJsonObject{
               {QStringLiteral("field"), QStringLiteral("allowedTools.color")},
               {QStringLiteral("reason"), QStringLiteral("Color edits are allowed.")},
           },
       }},
      {QStringLiteral("editIntent"),
       QJsonObject{
           {QStringLiteral("goalSummary"), QStringLiteral("Create a polished warm reference-led edit.")},
           {QStringLiteral("colorMode"), QStringLiteral("color")},
           {QStringLiteral("contrast"), QStringLiteral("natural")},
           {QStringLiteral("brightness"), QStringLiteral("preserve")},
           {QStringLiteral("palette"), QStringLiteral("warm")},
           {QStringLiteral("crop"), QStringLiteral("preserve")},
           {QStringLiteral("detail"), QStringLiteral("natural")},
           {QStringLiteral("mood"), QStringLiteral("editorial")},
           {QStringLiteral("referenceStrength"), 1.0},
           {QStringLiteral("requestedActions"), QJsonArray{QStringLiteral("tone"), QStringLiteral("color")}},
           {QStringLiteral("requiresApproval"), QJsonArray{}},
           {QStringLiteral("confidence"), 0.92},
           {QStringLiteral("globalChangePolicy"), QStringLiteral("allow")},
           {QStringLiteral("visualFindings"),
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("kind"), QStringLiteral("subject_exposure")},
                    {QStringLiteral("target"), QStringLiteral("subject")},
                    {QStringLiteral("severity"), QStringLiteral("medium")},
                    {QStringLiteral("observation"),
                     QStringLiteral("The central couple can take a restrained local lift.")},
                    {QStringLiteral("confidence"), 0.88},
                    {QStringLiteral("region"),
                     QJsonObject{
                         {QStringLiteral("centerX"), 0.50},
                         {QStringLiteral("centerY"), 0.52},
                         {QStringLiteral("radiusX"), 0.24},
                         {QStringLiteral("radiusY"), 0.34},
                     }},
                },
            }},
           {QStringLiteral("localizedActions"),
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("target"), QStringLiteral("subject")},
                    {QStringLiteral("maskShape"), QStringLiteral("radial")},
                    {QStringLiteral("selectionMode"), QStringLiteral("semantic")},
                    {QStringLiteral("reason"),
                     QStringLiteral("Lift the complete visible subject without changing the background.")},
                    {QStringLiteral("confidence"), 0.86},
                    {QStringLiteral("region"),
                     QJsonObject{
                         {QStringLiteral("centerX"), 0.50},
                         {QStringLiteral("centerY"), 0.52},
                         {QStringLiteral("radiusX"), 0.24},
                         {QStringLiteral("radiusY"), 0.34},
                         {QStringLiteral("feather"), 0.72},
                     }},
                    {QStringLiteral("adjustments"),
                     QJsonObject{
                         {QStringLiteral("exposure"), 0.24},
                         {QStringLiteral("shadows"), 12.0},
                     }},
                },
            }},
           {QStringLiteral("retouchActions"), QJsonArray{}},
      }},
      {QStringLiteral("warnings"), QJsonArray{QStringLiteral("fake-server-warning")}},
  };
  const QJsonObject fake_ollama_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"), QString()},
           {QStringLiteral("thinking"), QString::fromUtf8(QJsonDocument(model_plan).toJson(QJsonDocument::Compact))},
       }},
  };
  const auto fake_ollama_request_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("ollama-request.json"));
  if (fake_ollama_server_available) {
    fake_ollama_server.setChunkedResponse(QStringLiteral("/api/chat"), fake_ollama_response);
  } else {
    qputenv(
        "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
        QJsonDocument(fake_ollama_response).toJson(QJsonDocument::Compact));
    qputenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH", fake_ollama_request_path.toUtf8());
  }
  request.model_id = QStringLiteral("ollama-qwen-test");
  auto learned_warm_style = warm_style;
  learned_warm_style.insert(
      QStringLiteral("learnedProfile"),
      QVariantMap{
          {QStringLiteral("source_asset_count"), 3},
          {QStringLiteral("usable_recipe_count"), 3},
          {QStringLiteral("styleSummary"), QStringLiteral("Learned from Brady's warm edited wedding references.")},
          {QStringLiteral("editTendencies"),
           QVariantMap{
               {QStringLiteral("averageExposure"), 0.18},
               {QStringLiteral("averageTemperature"), 240.0},
           }},
      });
  learned_warm_style.insert(
      QStringLiteral("toolKnowledge"),
      QVariantMap{{QStringLiteral("hsl"), QStringLiteral("Treat orange/red channels as skin-sensitive.")}});
  learned_warm_style.insert(
      QStringLiteral("qualityModel"),
      QVariantMap{{QStringLiteral("priorities"), QStringList{QStringLiteral("preserve highlight detail")}}});
  learned_warm_style.insert(
      QStringLiteral("projectWorkflow"),
      QVariantMap{{QStringLiteral("editRepresentativeSamplesFirst"), true}});
  request.style = learned_warm_style;
  request.asset = QVariantMap{
      {QStringLiteral("filePath"), fake_image_path},
      {QStringLiteral("proofImagePath"), fake_proof_path},
  };
  request.current_adjustment_patch_json =
      QStringLiteral("{\"kind\":\"photo-editor-adjustment-patch\",\"version\":1,\"scope\":\"asset-bound\",\"groups\":{}}");
  request.user_instructions = QStringLiteral("Use the image and make a polished warm edit.");
  request.strategy_phase = QStringLiteral("batch");
  request.memory_packet = QVariantMap{
      {QStringLiteral("phase"), QStringLiteral("batch")},
      {QStringLiteral("includedMemoryKinds"),
       QStringList{
           QStringLiteral("approved-sample-recipe"),
           QStringLiteral("cluster-summary"),
           QStringLiteral("style-constraints"),
           QStringLiteral("photographer-calibration"),
      }},
      {QStringLiteral("rawVaultIncluded"), false},
      {QStringLiteral("projectNorthStar"),
       QVariantMap{
           {QStringLiteral("contract"),
            QStringLiteral("agent-project-north-star-memory-v1")},
           {QStringLiteral("available"), true},
           {QStringLiteral("projectKey"), QStringLiteral("Wedding 2026")},
           {QStringLiteral("brief"),
            QStringLiteral(
                "Keep this wedding intimate and documentary; protect skin, highlights, and unposed moments across every delivery.")},
           {QStringLiteral("digestSha256"), QString(64, QLatin1Char('c'))},
           {QStringLiteral("authority"), QStringLiteral("qualitative-only")},
       }},
      {QStringLiteral("approvedSampleRecipeCard"),
       QVariantMap{
           {QStringLiteral("available"), true},
           {QStringLiteral("validationOk"), true},
           {QStringLiteral("runId"), QStringLiteral("approved-run-1")},
           {QStringLiteral("scope"), QStringLiteral("selected-assets")},
           {QStringLiteral("recipePatch"),
            QVariantMap{
                {QStringLiteral("globalTone"),
                 QVariantMap{{QStringLiteral("exposure"), 0.12}}},
            }},
       }},
      {QStringLiteral("approvedProjectTasteCard"),
       QVariantMap{
           {QStringLiteral("available"), true},
           {QStringLiteral("projectKey"), QStringLiteral("Wedding 2026")},
           {QStringLiteral("twoSpeedPreference"),
            QVariantMap{
                {QStringLiteral("contract"),
                 QStringLiteral("agent-two-speed-taste-signal-v1")},
                {QStringLiteral("available"), true},
                {QStringLiteral("choiceCount"), 3},
                {QStringLiteral("firstLookCount"), 2},
                {QStringLiteral("deepReviewCount"), 1},
                {QStringLiteral("minimumEvidenceCount"), 2},
                {QStringLiteral("enoughEvidence"), true},
                {QStringLiteral("preferredVersion"),
                 QStringLiteral("first-look")},
                {QStringLiteral("influencesPlanning"), true},
                {QStringLiteral("planningGuidance"),
                 QStringLiteral(
                     "Prefer restrained, direct edits close to the proved first look unless this photo or request clearly benefits from deeper refinement.")},
                {QStringLiteral("authority"),
                 QStringLiteral("project-local-qualitative-guidance-only")},
                {QStringLiteral("currentRequestWins"), true},
                {QStringLiteral("automaticVersionSelectionAllowed"), false},
                {QStringLiteral("automaticCommitAllowed"), false},
            }},
           {QStringLiteral("corrections"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("feedback"),
                     QStringLiteral("Keep skin less warm and leave the crop looser.")},
                    {QStringLiteral("revisedFromRunId"),
                     QStringLiteral("rejected-project-run-1")},
                    {QStringLiteral("approvedRunId"),
                     QStringLiteral("approved-correction-run-1")},
                    {QStringLiteral("rejectedRecipeExamples"), QVariantList{}},
                    {QStringLiteral("approvedRecipeExamples"), QVariantList{}},
                },
            }},
           {QStringLiteral("rejectedDrafts"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("runId"),
                     QStringLiteral("discarded-project-run-1")},
                    {QStringLiteral("recipeExamples"), QVariantList{}},
                    {QStringLiteral("inferredReason"), false},
                },
            }},
           {QStringLiteral("exemplars"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("runId"), QStringLiteral("approved-project-run-1")},
                    {QStringLiteral("userIntent"), QStringLiteral("Keep skin warm and highlights restrained.")},
                    {QStringLiteral("assetExamples"),
                     QVariantList{
                         QVariantMap{
                             {QStringLiteral("assetId"), QStringLiteral("approved-asset-1")},
                             {QStringLiteral("recipeDiff"),
                              QVariantMap{
                                  {QStringLiteral("globalTone"),
                                   QVariantMap{
                                       {QStringLiteral("exposure"),
                                        QVariantMap{
                                            {QStringLiteral("before"), 0.0},
                                            {QStringLiteral("after"), 0.12},
                                            {QStringLiteral("delta"), 0.12},
                                        }},
                                   }},
                              }},
                         },
                     }},
                },
           }},
       }},
      {QStringLiteral("photographerCalibrationCard"),
       QVariantMap{
           {QStringLiteral("available"), true},
           {QStringLiteral("profileDigestSha256"),
            QString(64, QLatin1Char('b'))},
           {QStringLiteral("weakestDimension"),
            QStringLiteral("naturalness")},
           {QStringLiteral("validationConcernKeys"),
            QStringList{QStringLiteral("warmth")}},
           {QStringLiteral("planningGuidance"),
            QStringList{QStringLiteral(
                "Prioritize naturalness and use restrained warmth.")}},
           {QStringLiteral("numericThresholdOverridesAllowed"), false},
           {QStringLiteral("professionalReadinessClaimed"), false},
       }},
  };
  const auto ollama_plan = photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto fake_request = fake_ollama_server_available
                                ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
                                : read_json_file(fake_ollama_request_path);
  const auto fake_messages = fake_request.value(QStringLiteral("messages")).toArray();
  const auto fake_format = fake_request.value(QStringLiteral("format")).toObject();
  const auto fake_edit_intent_branch = schema_branch_for_decision(
      fake_format, QStringLiteral("edit_intent"));
  const auto fake_needs_input_branch = schema_branch_for_decision(
      fake_format, QStringLiteral("needs_user_input"));
  const auto fake_cannot_complete_branch = schema_branch_for_decision(
      fake_format, QStringLiteral("cannot_complete_safely"));
  const auto fake_edit_intent_branch_properties =
      fake_edit_intent_branch.value(QStringLiteral("properties")).toObject();
  const auto fake_needs_input_branch_properties =
      fake_needs_input_branch.value(QStringLiteral("properties")).toObject();
  const auto fake_cannot_complete_branch_properties =
      fake_cannot_complete_branch.value(QStringLiteral("properties")).toObject();
  const auto fake_edit_intent_schema =
      fake_edit_intent_branch_properties.value(QStringLiteral("editIntent")).toObject();
  const auto fake_edit_intent_required =
      fake_edit_intent_schema.value(QStringLiteral("required")).toArray();
  QJsonArray fake_decision_values;
  for (const auto& branch_value : fake_format.value(QStringLiteral("oneOf")).toArray()) {
    const auto branch_decision = branch_value.toObject().value(QStringLiteral("properties"))
        .toObject().value(QStringLiteral("decision")).toObject().value(QStringLiteral("enum")).toArray();
    if (!branch_decision.isEmpty()) fake_decision_values.append(branch_decision.first());
  }
  const auto fake_system_prompt =
      fake_messages.isEmpty()
          ? QString{}
          : fake_messages.first().toObject().value(QStringLiteral("content")).toString();
  const auto fake_user_message = fake_messages.size() >= 2 ? fake_messages.at(1).toObject() : QJsonObject{};
  const auto fake_user_prompt = fake_user_message.value(QStringLiteral("content")).toString();
  const auto fake_ollama_request_ok =
      fake_request.value(QStringLiteral("model")).toString() == QStringLiteral("qwen2.5vl:7b") &&
      !fake_request.value(QStringLiteral("stream")).toBool(true) &&
      !fake_request.value(QStringLiteral("think")).toBool(true) &&
      fake_request.value(QStringLiteral("keep_alive")).toString() == QStringLiteral("10m") &&
      fake_request.value(QStringLiteral("options")).toObject().value(QStringLiteral("temperature")).toDouble(-1.0) == 0.0 &&
      fake_request.value(QStringLiteral("options")).toObject().value(QStringLiteral("num_predict")).toInt() == 640 &&
      fake_request.value(QStringLiteral("options")).toObject().value(QStringLiteral("num_ctx")).toInt() >= 12288 &&
      fake_request.value(QStringLiteral("options")).toObject().value(QStringLiteral("num_ctx")).toInt() <= 24576 &&
      fake_request.value(QStringLiteral("format")).isObject() &&
      !fake_format.contains(QStringLiteral("properties")) &&
      !fake_format.contains(QStringLiteral("type")) &&
      !fake_format.contains(QStringLiteral("required")) &&
      fake_format.value(QStringLiteral("oneOf")).toArray().size() == 3 &&
      fake_edit_intent_branch.value(QStringLiteral("required")).toArray().contains(QStringLiteral("decision")) &&
      fake_edit_intent_branch.value(QStringLiteral("required")).toArray().contains(QStringLiteral("summary")) &&
      fake_edit_intent_branch.value(QStringLiteral("required")).toArray().contains(QStringLiteral("styleConstraintCitations")) &&
      fake_edit_intent_branch.value(QStringLiteral("required")).toArray().contains(QStringLiteral("editIntent")) &&
      fake_needs_input_branch.value(QStringLiteral("required")).toArray().contains(QStringLiteral("userQuestions")) &&
      fake_cannot_complete_branch.value(QStringLiteral("required")).toArray().contains(QStringLiteral("reason")) &&
      fake_needs_input_branch_properties.value(QStringLiteral("userQuestions")).toObject()
          .value(QStringLiteral("minItems")).toInt() == 1 &&
      fake_cannot_complete_branch_properties.value(QStringLiteral("reason")).toObject()
          .value(QStringLiteral("minLength")).toInt() == 1 &&
      fake_edit_intent_required.contains(QStringLiteral("visualFindings")) &&
      fake_edit_intent_required.contains(QStringLiteral("localizedActions")) &&
      fake_edit_intent_required.contains(QStringLiteral("globalChangePolicy")) &&
      !fake_edit_intent_required.contains(QStringLiteral("colorMode")) &&
      !fake_edit_intent_required.contains(QStringLiteral("crop")) &&
      fake_decision_values.contains(QStringLiteral("edit_intent")) &&
      !fake_system_prompt.contains(QStringLiteral("compact wire format")) &&
      fake_user_prompt.contains(QStringLiteral("Strategy phase: batch")) &&
      fake_user_prompt.contains(QStringLiteral("Phase: batch")) &&
      fake_user_prompt.contains(QStringLiteral("Retrieved memory packet JSON")) &&
      fake_user_prompt.contains(QStringLiteral("approved-run-1")) &&
      fake_user_prompt.contains(QStringLiteral("projectNorthStar")) &&
      fake_user_prompt.contains(QStringLiteral("Keep this wedding intimate and documentary")) &&
      fake_user_prompt.contains(QStringLiteral("the current request wins every conflict")) &&
      fake_user_prompt.contains(QStringLiteral("Never infer numeric recipe changes, culling, retouch, or destructive authority")) &&
      fake_user_prompt.contains(QStringLiteral("Compact relevant examples JSON")) &&
      fake_user_prompt.contains(QStringLiteral("approved-sample-recipe")) &&
      fake_user_prompt.contains(QStringLiteral("approved-project-edit")) &&
      fake_user_prompt.contains(QStringLiteral("approved-project-correction")) &&
      fake_user_prompt.contains(QStringLiteral("rejected-project-edit")) &&
      fake_user_prompt.contains(QStringLiteral("Keep skin less warm")) &&
      fake_user_prompt.contains(QStringLiteral("never invent a rejection reason")) &&
      fake_user_prompt.contains(QStringLiteral("approved-project-run-1")) &&
      fake_user_prompt.contains(QStringLiteral("Wedding 2026")) &&
      fake_user_prompt.contains(QStringLiteral("preference evidence only for the active project")) &&
      fake_user_prompt.contains(QStringLiteral("twoSpeedPreference")) &&
      fake_user_prompt.contains(QStringLiteral("agent-two-speed-taste-signal-v1")) &&
      fake_user_prompt.contains(QStringLiteral("Prefer restrained, direct edits close to the proved first look")) &&
      fake_user_prompt.contains(QStringLiteral("current request, visible photo, active style, and safety policy always win")) &&
      fake_user_prompt.contains(QStringLiteral("Never select First Look or Deep Review automatically")) &&
      fake_user_prompt.contains(QStringLiteral("never commit from this preference")) &&
      fake_user_prompt.contains(QStringLiteral("Preserving framing or protecting highlights does not forbid")) &&
      fake_user_prompt.contains(QStringLiteral("photographerCalibrationCard")) &&
      fake_user_prompt.contains(QStringLiteral("Prioritize naturalness and use restrained warmth")) &&
      fake_user_prompt.contains(QStringLiteral("qualitative guidance only")) &&
      fake_user_prompt.contains(QStringLiteral("never claim professional readiness")) &&
      fake_user_prompt.contains(QStringLiteral("semantic editIntent")) &&
      fake_user_prompt.contains(QStringLiteral("Ground visualFindings in visible evidence")) &&
      fake_user_prompt.contains(QStringLiteral("bounded local change")) &&
      fake_user_prompt.contains(QStringLiteral("globalChangePolicy=preserve")) &&
      fake_user_prompt.contains(QStringLiteral("target=object")) &&
      fake_user_prompt.contains(QStringLiteral("targetLabel")) &&
      fake_user_prompt.contains(QStringLiteral("selectionMode=guided")) &&
      fake_user_prompt.contains(QStringLiteral("selectionCandidates")) &&
      fake_user_prompt.contains(QStringLiteral("do not guess silently")) &&
      fake_user_prompt.contains(QStringLiteral("recentVisualReferents")) &&
      fake_user_prompt.contains(QStringLiteral("exact active asset")) &&
      fake_user_prompt.contains(QStringLiteral("never resolve a vague pronoun by guessing")) &&
      fake_user_prompt.contains(QStringLiteral("Treat semantic and guided selection as requests")) &&
      fake_user_prompt.contains(QStringLiteral("learnedProfile")) &&
      fake_user_prompt.contains(QStringLiteral("toolKnowledge")) &&
      fake_user_prompt.contains(QStringLiteral("qualityModel")) &&
      fake_user_prompt.contains(QStringLiteral("projectWorkflow"));
  const auto fake_ollama_saw_image =
      !fake_user_message.value(QStringLiteral("images")).toArray().isEmpty();
  const auto fake_ollama_image_payload =
      fake_user_message.value(QStringLiteral("images")).toArray().isEmpty()
          ? QString()
          : fake_user_message.value(QStringLiteral("images")).toArray().first().toString();
  const auto ollama_findings =
      ollama_plan.edit_intent.value(QStringLiteral("visualFindings")).toList();
  const auto ollama_actions =
      ollama_plan.edit_intent.value(QStringLiteral("localizedActions")).toList();
  const auto ollama_masks = ollama_plan.recipe_patch
                                .value(QStringLiteral("masks"))
                                .toMap()
                                .value(QStringLiteral("appendMaskStack"))
                                .toList();
  const auto ollama_finding =
      ollama_findings.isEmpty() ? QVariantMap{} : ollama_findings.first().toMap();
  const auto ollama_action =
      ollama_actions.isEmpty() ? QVariantMap{} : ollama_actions.first().toMap();
  const auto ollama_mask =
      ollama_masks.isEmpty() ? QVariantMap{} : ollama_masks.first().toMap();
  const auto ollama_mask_id =
      ollama_action.value(QStringLiteral("maskId")).toString();
  if (!ollama_plan.ok ||
      ollama_plan.decision != QStringLiteral("recipe_patch") ||
      ollama_plan.plan_summary != QStringLiteral("Fake Ollama warm proof edit") ||
      ollama_plan.prompt_audit.value(QStringLiteral("backend")).toString() != QStringLiteral("ollama") ||
      ollama_plan.prompt_audit.value(QStringLiteral("strategyPhase")).toString() != QStringLiteral("batch") ||
      !ollama_plan.prompt_audit.value(QStringLiteral("deterministicPlanning")).toBool() ||
      ollama_plan.prompt_audit.value(QStringLiteral("temperature")).toDouble() != 0.0 ||
      ollama_plan.prompt_audit.value(QStringLiteral("memoryPacketPhase")).toString() != QStringLiteral("batch") ||
      ollama_plan.prompt_audit.value(QStringLiteral("fewShotExampleCount")).toInt() != 4 ||
      !ollama_plan.prompt_audit
           .value(QStringLiteral("photographerCalibrationAvailable"))
           .toBool() ||
      ollama_plan.prompt_audit
              .value(QStringLiteral("photographerCalibrationDigest"))
              .toString() != QString(64, QLatin1Char('b')) ||
      !ollama_plan.prompt_audit
           .value(QStringLiteral("projectNorthStarAvailable"))
           .toBool() ||
      ollama_plan.prompt_audit
              .value(QStringLiteral("projectNorthStarDigest"))
              .toString() != QString(64, QLatin1Char('c')) ||
      !ollama_plan.prompt_audit
           .value(QStringLiteral("projectNorthStarCurrentRequestWins"))
           .toBool() ||
      ollama_plan.prompt_audit.value(QStringLiteral("rawMemoryVaultIncluded")).toBool() ||
      !ollama_plan.prompt_audit.value(QStringLiteral("userPrompt")).toString().contains(QStringLiteral("semantic editIntent")) ||
      !ollama_plan.prompt_audit.value(QStringLiteral("imageIncluded")).toBool() ||
      ollama_plan.prompt_audit.value(QStringLiteral("imageByteCount")).toInt() != fake_proof_bytes.size() ||
      ollama_plan.prompt_audit.value(QStringLiteral("imageSourceKind")).toString() != QStringLiteral("proof") ||
      ollama_plan.prompt_audit.value(QStringLiteral("imageSourcePath")).toString() != fake_proof_path ||
      !ollama_plan.prompt_audit.value(QStringLiteral("transport")).toMap().value(QStringLiteral("ok")).toBool() ||
      ollama_plan.prompt_audit.value(QStringLiteral("ollamaResponseSource")).toString() != QStringLiteral("message.thinking") ||
      ollama_plan.prompt_audit.value(QStringLiteral("generationSchemaContract")).toString() !=
          QStringLiteral("agent-operation-discriminated-generation-v2") ||
      ollama_plan.prompt_audit.value(QStringLiteral("generationSchemaByteCount")).toInt() >= 12000 ||
      !ollama_plan.prompt_audit.value(QStringLiteral("responseHasEditIntent")).toBool() ||
      ollama_plan.prompt_audit.value(QStringLiteral("modelDecision")).toString() != QStringLiteral("edit_intent") ||
      ollama_plan.prompt_audit.value(QStringLiteral("intentCompiler")).toString() != QStringLiteral("rulebook-deterministic-v2") ||
      ollama_plan.prompt_audit.value(QStringLiteral("visualFindingCount")).toInt() != 1 ||
      ollama_plan.prompt_audit.value(QStringLiteral("localizedActionCount")).toInt() != 1 ||
      ollama_plan.prompt_audit.value(QStringLiteral("localizedMaskCount")).toInt() != 1 ||
      ollama_plan.prompt_audit.value(QStringLiteral("localizedMaskWriteMode")).toString() !=
          QStringLiteral("append-preserve-existing") ||
      ollama_plan.prompt_audit.value(QStringLiteral("globalChangePolicy")).toString() !=
          QStringLiteral("allow") ||
      ollama_plan.prompt_audit.value(QStringLiteral("responseCitationCount")).toInt() != 2 ||
      ollama_plan.edit_intent.value(QStringLiteral("palette")).toString() != QStringLiteral("warm") ||
      ollama_plan.edit_intent.value(QStringLiteral("mood")).toString() != QStringLiteral("editorial") ||
      ollama_plan.edit_intent.value(QStringLiteral("visualFindings")).toList().size() != 1 ||
      ollama_plan.edit_intent.value(QStringLiteral("localizedActions")).toList().size() != 1 ||
      ollama_mask_id.isEmpty() ||
      ollama_finding.value(QStringLiteral("maskId")).toString() != ollama_mask_id ||
      ollama_mask.value(QStringLiteral("id")).toString() != ollama_mask_id ||
      ollama_action.value(QStringLiteral("selectionMode")).toString() !=
          QStringLiteral("semantic") ||
      ollama_mask.value(QStringLiteral("agentSelectionMode")).toString() !=
          QStringLiteral("semantic") ||
      ollama_mask.value(QStringLiteral("agentSemanticTarget")).toString() !=
          QStringLiteral("subject") ||
      ollama_plan.recipe_patch.value(QStringLiteral("color")).toMap().value(QStringLiteral("temperature")).toDouble() <= 0.0 ||
      ollama_plan.recipe_patch.value(QStringLiteral("masks"))
              .toMap()
              .value(QStringLiteral("appendMaskStack"))
              .toList()
              .size() != 1 ||
      ollama_plan.style_constraint_citations.size() != 2 ||
      !ollama_plan.warnings.contains(QStringLiteral("fake-server-warning")) ||
      !fake_ollama_request_ok ||
      !fake_ollama_saw_image ||
      fake_ollama_image_payload != QString::fromLatin1(fake_proof_bytes.toBase64())) {
    const auto audit = ollama_plan.prompt_audit;
    const auto response_fields = audit.value(QStringLiteral("responseFieldNames")).toStringList();
    const auto color_patch = ollama_plan.recipe_patch.value(QStringLiteral("color")).toMap();
    const auto bounded_finding = ollama_finding.isEmpty() ? QVariantMap{} : QVariantMap{
        {QStringLiteral("kind"), ollama_finding.value(QStringLiteral("kind"))},
        {QStringLiteral("target"), ollama_finding.value(QStringLiteral("target"))},
        {QStringLiteral("maskId"), ollama_finding.value(QStringLiteral("maskId"))},
        {QStringLiteral("observation"), ollama_finding.value(QStringLiteral("observation")).toString().left(180)},
    };
    const auto bounded_action = ollama_action.isEmpty() ? QVariantMap{} : QVariantMap{
        {QStringLiteral("target"), ollama_action.value(QStringLiteral("target"))},
        {QStringLiteral("maskId"), ollama_action.value(QStringLiteral("maskId"))},
        {QStringLiteral("selectionMode"), ollama_action.value(QStringLiteral("selectionMode"))},
        {QStringLiteral("reason"), ollama_action.value(QStringLiteral("reason")).toString().left(180)},
    };
    const auto bounded_mask = ollama_mask.isEmpty() ? QVariantMap{} : QVariantMap{
        {QStringLiteral("id"), ollama_mask.value(QStringLiteral("id"))},
        {QStringLiteral("agentSelectionMode"), ollama_mask.value(QStringLiteral("agentSelectionMode"))},
        {QStringLiteral("agentSemanticTarget"), ollama_mask.value(QStringLiteral("agentSemanticTarget"))},
    };
    const QVariantMap planner_diagnostic{
        {QStringLiteral("ok"), ollama_plan.ok},
        {QStringLiteral("error"), ollama_plan.error_message.left(500)},
        {QStringLiteral("decision"), ollama_plan.decision},
        {QStringLiteral("summary"), ollama_plan.plan_summary.left(180)},
        {QStringLiteral("audit"), QVariantMap{
            {QStringLiteral("backend"), audit.value(QStringLiteral("backend"))},
            {QStringLiteral("strategyPhase"), audit.value(QStringLiteral("strategyPhase"))},
            {QStringLiteral("memoryPacketPhase"), audit.value(QStringLiteral("memoryPacketPhase"))},
            {QStringLiteral("fewShotExampleCount"), audit.value(QStringLiteral("fewShotExampleCount"))},
            {QStringLiteral("generationSchemaContract"), audit.value(QStringLiteral("generationSchemaContract"))},
            {QStringLiteral("generationSchemaByteCount"), audit.value(QStringLiteral("generationSchemaByteCount"))},
            {QStringLiteral("transport"), audit.value(QStringLiteral("transport"))},
            {QStringLiteral("ollamaResponseSource"), audit.value(QStringLiteral("ollamaResponseSource"))},
            {QStringLiteral("responseFieldNames"), response_fields},
            {QStringLiteral("responseCharCount"), audit.value(QStringLiteral("responseCharCount"))},
            {QStringLiteral("responsePreview"), audit.value(QStringLiteral("responsePreview")).toString().left(500)},
            {QStringLiteral("responseCitationCount"), audit.value(QStringLiteral("responseCitationCount"))},
            {QStringLiteral("responseHasEditIntent"), audit.value(QStringLiteral("responseHasEditIntent"))},
            {QStringLiteral("modelDecision"), audit.value(QStringLiteral("modelDecision"))},
            {QStringLiteral("intentCompiler"), audit.value(QStringLiteral("intentCompiler"))},
            {QStringLiteral("globalChangePolicy"), audit.value(QStringLiteral("globalChangePolicy"))},
            {QStringLiteral("visualFindingCount"), audit.value(QStringLiteral("visualFindingCount"))},
            {QStringLiteral("localizedActionCount"), audit.value(QStringLiteral("localizedActionCount"))},
            {QStringLiteral("localizedMaskCount"), audit.value(QStringLiteral("localizedMaskCount"))},
            {QStringLiteral("localizedMaskWriteMode"), audit.value(QStringLiteral("localizedMaskWriteMode"))},
            {QStringLiteral("imageIncluded"), audit.value(QStringLiteral("imageIncluded"))},
            {QStringLiteral("imageByteCount"), audit.value(QStringLiteral("imageByteCount"))},
            {QStringLiteral("imageSourceKind"), audit.value(QStringLiteral("imageSourceKind"))},
            {QStringLiteral("imageSourcePath"), audit.value(QStringLiteral("imageSourcePath"))},
        }},
        {QStringLiteral("expected"), QVariantMap{
            {QStringLiteral("schemaByteCountLessThan"), 2048},
            {QStringLiteral("visualFindings"), 1},
            {QStringLiteral("localizedActions"), 1},
            {QStringLiteral("localizedMasks"), 1},
            {QStringLiteral("citations"), 2},
            {QStringLiteral("fakeProofBytes"), fake_proof_bytes.size()},
        }},
        {QStringLiteral("actual"), QVariantMap{
            {QStringLiteral("visualFindings"), ollama_findings.size()},
            {QStringLiteral("firstFinding"), bounded_finding},
            {QStringLiteral("localizedActions"), ollama_actions.size()},
            {QStringLiteral("firstAction"), bounded_action},
            {QStringLiteral("localizedMasks"), ollama_masks.size()},
            {QStringLiteral("firstMask"), bounded_mask},
            {QStringLiteral("palette"), ollama_plan.edit_intent.value(QStringLiteral("palette"))},
            {QStringLiteral("mood"), ollama_plan.edit_intent.value(QStringLiteral("mood"))},
            {QStringLiteral("colorTemperature"), color_patch.value(QStringLiteral("temperature"))},
            {QStringLiteral("citationCount"), ollama_plan.style_constraint_citations.size()},
            {QStringLiteral("warningCount"), ollama_plan.warnings.size()},
            {QStringLiteral("fakeRequestOk"), fake_ollama_request_ok},
            {QStringLiteral("fakeSawImage"), fake_ollama_saw_image},
            {QStringLiteral("fakeImageByteCount"), QByteArray::fromBase64(fake_ollama_image_payload.toLatin1()).size()},
        }},
    };
    std::cerr << "Ollama proof/schema contract diagnostic="
              << QJsonDocument::fromVariant(planner_diagnostic)
                     .toJson(QJsonDocument::Compact).left(5000).constData()
              << std::endl;
    return fail("Ollama backend should send rendered proof/schema context and recover structured JSON from Qwen thinking responses");
  }
  {
    auto required = request;
    required.force_model_planning = true;
    required.allow_deterministic_fallback = true;
    const auto required_path = runtime_root.filePath(QStringLiteral("required-planning-proof.png"));
    QImage proof(20, 20, QImage::Format_RGB32);
    proof.fill(QColor(60, 90, 120));
    if (!proof.save(required_path, "PNG")) return fail("Write a required planning proof");
    QFile proof_file(required_path);
    if (!proof_file.open(QIODevice::ReadOnly)) return fail("Read the required proof bytes");
    const auto digest = QString::fromLatin1(QCryptographicHash::hash(proof_file.readAll(), QCryptographicHash::Sha256).toHex());
    proof_file.close();
    required.asset.insert(QStringLiteral("proofImagePath"), required_path);
    required.asset.insert(QStringLiteral("planningProofRequired"), true);
    required.asset.insert(QStringLiteral("planningProofSha256"), digest);
    const auto valid = photo_editor::desktop::plan_agent_recipe_patch(required);
    if (!valid.ok || valid.prompt_audit.value(QStringLiteral("imageSourcePath")) != required_path)
      return fail("A required rendered image must be sent through the actual model transport");
    const auto detail_path = runtime_root.filePath(QStringLiteral("required-planning-detail.png"));
    QImage detail_image(14, 10, QImage::Format_RGB32);
    detail_image.fill(QColor(120, 80, 150));
    if (!detail_image.save(detail_path, "PNG")) return fail("Write a targeted planning detail proof");
    const auto detail_digest = [&detail_path]() {
      QFile file(detail_path);
      if (!file.open(QIODevice::ReadOnly)) return QString();
      return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
    }();
    auto detail_request = required;
    detail_request.asset.insert(QStringLiteral("additionalProofImagePaths"), QStringList{detail_path});
    detail_request.asset.insert(QStringLiteral("visualEvidence"), QVariantMap{
        {QStringLiteral("targetedDetailProofs"), QVariantList{QVariantMap{
            {QStringLiteral("imagePath"), detail_path},
            {QStringLiteral("imageSha256"), detail_digest},
            {QStringLiteral("sourceOverviewSha256"), digest},
            {QStringLiteral("available"), true},
            {QStringLiteral("omitted"), false},
            {QStringLiteral("layerId"), QStringLiteral("planned-title")},
            {QStringLiteral("pixelBounds"), QVariantMap{
                {QStringLiteral("x"), 1}, {QStringLiteral("y"), 2},
                {QStringLiteral("width"), 14}, {QStringLiteral("height"), 10}}},
        }}},
    });
    const auto detailed_plan = photo_editor::desktop::plan_agent_recipe_patch(detail_request);
    const auto detailed_request_json = fake_ollama_server_available
        ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
        : read_json_file(fake_ollama_request_path);
    const auto detailed_messages = detailed_request_json.value(QStringLiteral("messages")).toArray();
    const auto detailed_images = detailed_messages.size() >= 2
        ? detailed_messages.at(1).toObject().value(QStringLiteral("images")).toArray()
        : QJsonArray{};
    if (!detailed_plan.ok ||
        detailed_plan.prompt_audit.value(QStringLiteral("additionalProofImageCount")).toInt() != 1 ||
        detailed_images.size() != 2) {
      return fail("planning must send the hashed overview together with its bound targeted detail crop");
    }
    auto changed_detail_request = detail_request;
    auto changed_detail_evidence = changed_detail_request.asset.value(QStringLiteral("visualEvidence")).toMap();
    auto changed_detail_list = changed_detail_evidence.value(QStringLiteral("targetedDetailProofs")).toList();
    auto changed_detail = changed_detail_list.first().toMap();
    changed_detail.insert(QStringLiteral("imageSha256"), QString(64, QLatin1Char('0')));
    changed_detail_list[0] = changed_detail;
    changed_detail_evidence.insert(QStringLiteral("targetedDetailProofs"), changed_detail_list);
    changed_detail_request.asset.insert(QStringLiteral("visualEvidence"), changed_detail_evidence);
    const auto invalid_detail_plan = photo_editor::desktop::plan_agent_recipe_patch(changed_detail_request);
    if (invalid_detail_plan.ok ||
        invalid_detail_plan.prompt_audit.value(QStringLiteral("errorCode")) != QStringLiteral("planning_proof_unavailable")) {
      return fail("changed targeted detail evidence must stop model planning without proof fallback");
    }
    for (int scenario = 0; scenario < 3; ++scenario) {
      auto rejected = required;
      if (scenario == 0) rejected.asset.insert(QStringLiteral("proofImagePath"), QStringLiteral("/missing-required-proof.png"));
      if (scenario == 1) rejected.asset.insert(QStringLiteral("planningProofSha256"), QString(64, '0'));
      if (scenario == 2) {
        QFile corrupt(required_path);
        if (!corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail("Write an unreadable required proof");
        const auto bytes = QByteArrayLiteral("not a rendered image");
        corrupt.write(bytes);
        corrupt.close();
        rejected.asset.insert(QStringLiteral("planningProofSha256"), QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
      }
      const auto unavailable = photo_editor::desktop::plan_agent_recipe_patch(rejected);
      if (unavailable.ok || unavailable.prompt_audit.value(QStringLiteral("errorCode")) != QStringLiteral("planning_proof_unavailable") ||
          unavailable.prompt_audit.value(QStringLiteral("fallbackUsed")).toBool())
        return fail("Missing, changed and corrupt required proofs must stop without source-image or deterministic fallback");
    }
  }
  const auto ollama_validation =
      photo_editor::desktop::validate_agent_recipe_patch(ollama_plan.recipe_patch, warm_style);
  if (!ollama_validation.valid) {
    std::cerr << ollama_validation.errors.join(QStringLiteral("; ")).toStdString() << '\n';
    return fail("Ollama model recipe patches should satisfy recipe patch validation");
  }

  // Exercise the real socket path: unlimited waiting must still complete,
  // honor Stop, and report a disconnected server without waiting forever.
  if (fake_ollama_server_available) {
    const auto original_manifest = read_json_file(manifest_file.fileName());
    auto unlimited_manifest = original_manifest;
    auto entries = unlimited_manifest.value("models").toArray();
    qsizetype unlimited_model_index = -1;
    for (qsizetype index = 0; index < entries.size(); ++index) {
      if (entries.at(index).toObject().value(QStringLiteral("id")).toString() == request.model_id) {
        unlimited_model_index = index;
        break;
      }
    }
    if (unlimited_model_index < 0)
      return fail("the unlimited-wait fixture must locate the selected model by id");
    auto model = entries.at(unlimited_model_index).toObject();
    model.insert("requestTimeoutMs", -1);
    entries.replace(unlimited_model_index, model);
    unlimited_manifest.insert("models", entries);
    const auto write_manifest = [&](const QJsonObject& value) {
      if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
      manifest_file.write(QJsonDocument(value).toJson());
      manifest_file.close();
      return true;
    };
    if (!write_manifest(unlimited_manifest)) return fail("unlimited fixture manifest must be writable");
    auto unlimited_request = request;
    // Keep this transport-contract turn intentionally small so a 16K context
    // is sufficient; this test exercises timeout behavior, not budget policy.
    unlimited_request.style = warm_style;
    unlimited_request.asset = QVariantMap{
        {QStringLiteral("filePath"), fake_image_path},
        {QStringLiteral("proofImagePath"), fake_proof_path},
    };
    unlimited_request.current_adjustment_patch_json = QStringLiteral("{}");
    unlimited_request.user_instructions = QStringLiteral("Use the image and make a polished warm edit.");
    unlimited_request.memory_packet.clear();
    unlimited_request.few_shot_examples.clear();
    unlimited_request.available_tool_schemas.clear();
    unlimited_request.prior_tool_results.clear();
    unlimited_request.strategy_phase = QStringLiteral("sample");
    unlimited_request.allow_tool_call_plan = false;
    unlimited_request.require_tool_call_plan = false;
    unlimited_request.force_model_planning = true;
    unlimited_request.allow_deterministic_fallback = false;
    const auto delay_ms = app.arguments().contains("--unlimited-wait-contract") ? 61000 : 150;
    fake_ollama_server.setResponseDelay(delay_ms);
    const auto waited_plan = photo_editor::desktop::plan_agent_recipe_patch(unlimited_request);
    const auto waited_transport = waited_plan.prompt_audit.value("transport").toMap();
    const auto waited_components = waited_plan.prompt_audit.value("promptComponents").toMap();
    if (!waited_plan.ok || !waited_plan.prompt_audit.value("requestSent").toBool() ||
        waited_transport.value("timeoutMs").toInt() != -1 ||
        waited_transport.value("durationMs").toLongLong() < delay_ms) {
      const QVariantMap diagnostic{
          {QStringLiteral("errorCode"), waited_plan.prompt_audit.value(QStringLiteral("errorCode"))},
          {QStringLiteral("errorMessage"), waited_plan.error_message},
          {QStringLiteral("requestSent"), waited_plan.prompt_audit.value(QStringLiteral("requestSent"))},
          {QStringLiteral("transport"), waited_transport},
          {QStringLiteral("promptComponents"), waited_components},
      };
      std::cerr << "Unlimited-wait request diagnostic="
                << QJsonDocument::fromVariant(diagnostic).toJson(QJsonDocument::Compact).toStdString()
                << '\n';
      return fail("an unlimited model request must wait for the response and complete");
    }
    std::cout << "Unlimited model request completed after "
              << waited_transport.value("durationMs").toLongLong() << " ms.\n";

    // Missing configuration also means unlimited, including older manifests.
    model.remove("requestTimeoutMs");
    entries.replace(unlimited_model_index, model);
    unlimited_manifest.insert("models", entries);
    if (!write_manifest(unlimited_manifest)) return fail("default fixture manifest must be writable");
    fake_ollama_server.setResponseDelay(150);
    const auto default_plan = photo_editor::desktop::plan_agent_recipe_patch(unlimited_request);
    if (!default_plan.ok || default_plan.prompt_audit.value("transport").toMap().value("timeoutMs").toInt() != -1)
      return fail("model requests must default to unlimited waiting");

    QElapsedTimer cancel_timer;
    cancel_timer.start();
    fake_ollama_server.setResponseDelay(2000);
    unlimited_request.cancellation_requested = [&]() { return cancel_timer.elapsed() >= 120; };
    const auto canceled_plan = photo_editor::desktop::plan_agent_recipe_patch(unlimited_request);
    if (canceled_plan.ok || canceled_plan.prompt_audit.value("transport").toMap().value("errorCode") != "cancelled" ||
        cancel_timer.elapsed() >= 1500)
      return fail("Stop must interrupt an unlimited model request promptly");
    unlimited_request.cancellation_requested = {};
    fake_ollama_server.setResponseDelay(0);
    fake_ollama_server.setDisconnectWithoutResponse(true);
    const auto disconnected_plan = photo_editor::desktop::plan_agent_recipe_patch(unlimited_request);
    fake_ollama_server.setDisconnectWithoutResponse(false);
    if (disconnected_plan.ok || disconnected_plan.prompt_audit.value("transport").toMap().value("errorCode") != "disconnected")
      return fail("an unlimited request must stop when the server disconnects");
    if (!write_manifest(original_manifest)) return fail("original fixture manifest must be restored");
  } else if (app.arguments().contains("--unlimited-wait-contract")) {
    return fail("the unlimited wait contract requires a local socket listener");
  }

  // A successful semantic response may omit colorMode. Explicit user intent
  // still owns the compiler, rather than falling back to the style's color grade.
  auto monochrome_request = request;
  monochrome_request.user_instructions = QStringLiteral(
      "Make it black and white with repeating brady reiner text in white and red behind the subject.");
  auto incomplete_color_plan = model_plan;
  auto incomplete_color_intent = incomplete_color_plan.value("editIntent").toObject();
  incomplete_color_intent.remove("colorMode");
  incomplete_color_intent.insert("saturation", ", ");
  incomplete_color_plan.insert("editIntent", incomplete_color_intent);
  auto incomplete_color_response = fake_ollama_response;
  incomplete_color_response.insert("message", QJsonObject{
      {"role", "assistant"},
      {"content", QString::fromUtf8(QJsonDocument(incomplete_color_plan).toJson(QJsonDocument::Compact))}});
  if (fake_ollama_server_available) {
    fake_ollama_server.setChunkedResponse("/api/chat", incomplete_color_response);
  } else {
    qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
        QJsonDocument(incomplete_color_response).toJson(QJsonDocument::Compact));
  }
  const auto monochrome_plan = photo_editor::desktop::plan_agent_recipe_patch(monochrome_request);
  if (!monochrome_plan.ok ||
      monochrome_plan.edit_intent.value("colorMode").toString() != "monochrome" ||
      monochrome_plan.recipe_patch.value("color").toMap().value("saturation").toDouble() != -100.0 ||
      monochrome_plan.recipe_patch.contains("colorGrading")) {
    return fail("An explicit monochrome request must survive an incomplete local-model color intent");
  }

  auto scope_misread_plan = model_plan;
  auto scope_misread_intent =
      scope_misread_plan.value(QStringLiteral("editIntent")).toObject();
  scope_misread_intent.insert(
      QStringLiteral("globalChangePolicy"), QStringLiteral("preserve"));
  scope_misread_plan.insert(
      QStringLiteral("editIntent"), scope_misread_intent);
  const QJsonObject scope_misread_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"), QString()},
           {QStringLiteral("thinking"),
            QString::fromUtf8(
                QJsonDocument(scope_misread_plan)
                    .toJson(QJsonDocument::Compact))},
       }},
  };
  if (fake_ollama_server_available) {
    fake_ollama_server.setChunkedResponse(
        QStringLiteral("/api/chat"), scope_misread_response);
  } else {
    qputenv(
        "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
        QJsonDocument(scope_misread_response).toJson(QJsonDocument::Compact));
  }
  auto global_scope_request = request;
  global_scope_request.user_instructions = QStringLiteral(
      "Create a polished warm overall look. Keep the framing unchanged and protect highlights.");
  const auto global_scope_plan =
      photo_editor::desktop::plan_agent_recipe_patch(global_scope_request);
  const auto scope_correction =
      global_scope_plan.prompt_audit
          .value(QStringLiteral("globalChangePolicyCorrection"))
          .toMap();
  if (!global_scope_plan.ok || global_scope_plan.recipe_patch.isEmpty() ||
      global_scope_plan.edit_intent
              .value(QStringLiteral("globalChangePolicy"))
              .toString() != QStringLiteral("allow") ||
      scope_correction.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-explicit-scope-authority-v1") ||
      scope_correction.value(QStringLiteral("modelValue")).toString() !=
          QStringLiteral("preserve") ||
      scope_correction.value(QStringLiteral("enforcedValue")).toString() !=
          QStringLiteral("allow") ||
      !scope_correction
           .value(QStringLiteral("currentRequestAuthoritative"))
           .toBool()) {
    return fail("an explicit overall look should remain global when the model mistakes framing or highlight protection for local-only scope");
  }

  const QJsonObject calibrated_critique_content{
      {QStringLiteral("pass"), true},
      {QStringLiteral("summary"),
       QStringLiteral("The restrained warmth remains natural.")},
      {QStringLiteral("issues"), QJsonArray{}},
      {QStringLiteral("repairHints"), QJsonArray{}},
      {QStringLiteral("needsUserReview"), true},
      {QStringLiteral("tasteConfidence"), QStringLiteral("medium")},
  };
  const QJsonObject calibrated_critique_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"),
            QString::fromUtf8(
                QJsonDocument(calibrated_critique_content)
                    .toJson(QJsonDocument::Compact))},
       }},
  };
  if (fake_ollama_server_available) {
    fake_ollama_server.setChunkedResponse(
        QStringLiteral("/api/chat"), calibrated_critique_response);
  } else {
    qputenv(
        "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
        QJsonDocument(calibrated_critique_response)
            .toJson(QJsonDocument::Compact));
  }
  photo_editor::desktop::AgentLocalModelProofCritiqueRequest critique_request;
  // The proof review carries one overview plus one detail image per graphic
  // layer. Use the dedicated 24K test model so a valid five-image review can
  // fit while retaining the 16K model below as an explicit fail-closed case.
  critique_request.model_id = QStringLiteral("ollama-app09-budget-test");
  critique_request.style = learned_warm_style;
  const auto critique_overview_path = runtime_root.filePath(QStringLiteral("critique-overview.png"));
  QImage critique_overview(120, 120, QImage::Format_RGB32);
  critique_overview.fill(QColor(90, 110, 130));
  if (!critique_overview.save(critique_overview_path, "PNG")) {
    return fail("Write valid rendered overview proof image");
  }
  const auto sha256_for_file = [](const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
  };
  const auto critique_overview_sha = sha256_for_file(critique_overview_path);
  critique_request.asset = QVariantMap{
      {QStringLiteral("proofImagePath"), critique_overview_path},
      {QStringLiteral("planningProofRequired"), true},
      {QStringLiteral("planningProofSha256"), critique_overview_sha},
      {QStringLiteral("additionalProofImagePaths"), QStringList{}},
      {QStringLiteral("visualEvidence"), QVariantMap{
          {QStringLiteral("targetedDetailProofs"), QVariantList{}},
      }},
  };
  critique_request.user_instructions =
      QStringLiteral("Keep the edit warm but natural.");
  critique_request.graphic_layers = QVariantList{QVariantMap{
      {QStringLiteral("id"), QStringLiteral("request-headline")},
      {QStringLiteral("name"), QStringLiteral("Poster headline")},
      {QStringLiteral("text"), QStringLiteral("LOOKSY FIELD NOTES")},
      {QStringLiteral("x"), 96},
      {QStringLiteral("y"), 88},
      {QStringLiteral("pixelWidth"), 900},
      {QStringLiteral("pixelHeight"), 180},
      {QStringLiteral("canvasWidth"), 1200},
      {QStringLiteral("canvasHeight"), 1200},
  }};
  const QVariantList composition_receipts{
      QVariantMap{{"toolName", "photo.edit_composition"}, {"ok", true},
          {"arguments", QVariantMap{{"operation", "add_text"}}},
          {"structuredContent", QVariantMap{{"createdLayerId", "tool-title"}}}},
      QVariantMap{{"toolName", "photo.edit_composition"}, {"ok", true},
          {"arguments", QVariantMap{{"operation", "typography"}, {"layerId", "edited-title"}}}},
      QVariantMap{{"toolName", "photo.edit_composition"}, {"ok", false},
          {"arguments", QVariantMap{{"operation", "add_text"}}},
          {"structuredContent", QVariantMap{{"createdLayerId", "rejected-title"}}}},
      QVariantMap{{"toolName", "photo.inspect_composition"}, {"ok", true},
          {"arguments", QVariantMap{{"layerId", "user-title"}}}},
      QVariantMap{{"toolName", "photo.edit_composition"}, {"ok", true},
          {"arguments", QVariantMap{{"operation", "add_text"}}}}};
  const auto composition_title_ids = photo_editor::desktop::agent_composition_text_layer_ids(composition_receipts);
  if (composition_title_ids != QStringList{"tool-title", "edited-title"})
    return fail("Graphic proof must use actual successful composition IDs without inventing missing or rejected IDs");
  const QVariantList proof_layers{
      QVariantMap{{"id", "cutout"}, {"kind", "pixel"}, {"agentGraphic", true}},
      QVariantMap{{"id", "tool-title"}, {"kind", "text"}, {"text", "TOOL TITLE"},
          {"x", 30}, {"y", 40}, {"pixelWidth", 1000}, {"pixelHeight", 50},
          {"canvasWidth", 1280}, {"canvasHeight", 800}, {"scale", 1.2}, {"rotation", 0.0},
          {"textTypography", QVariantMap{{"size", 50}}}},
      QVariantMap{{"id", "edited-title"}, {"kind", "text"}, {"text", "EDITED TITLE"}},
      QVariantMap{{"id", "legacy-title"}, {"kind", "text"}, {"text", "LEGACY TITLE"}, {"agentGraphic", true}},
      QVariantMap{{"id", "user-title"}, {"kind", "text"}, {"text", "USER TITLE"}},
      QVariantMap{{"id", "rejected-title"}, {"kind", "text"}, {"text", "REJECTED TITLE"}}};
  const auto tool_proof_layers = photo_editor::desktop::agent_graphic_proof_layers(proof_layers, composition_title_ids);
  if (tool_proof_layers.size() != 3 || tool_proof_layers.first().toMap().value("text").toString() != "TOOL TITLE" ||
      tool_proof_layers.first().toMap().value("position").toInt() != 1 ||
      tool_proof_layers.first().toMap().value("order").toString() != "top-first" ||
      std::abs(tool_proof_layers.first().toMap().value("scale").toDouble() - 1.2) > 1e-9 ||
      std::abs(tool_proof_layers.first().toMap().value("effectiveTypographySize").toDouble() - 60.0) > 1e-9 ||
      tool_proof_layers.first().toMap().value("layoutMetrics").toMap()
              .value("boundsBasis").toString() != "declared-layer-raster" ||
      tool_proof_layers.first().toMap().value("layoutMetrics").toMap()
              .value("visibleInkBoundsAvailable").toBool() ||
      tool_proof_layers.first().toMap().value("layoutMetrics").toMap()
              .value("effectPaddingIncluded").toBool() ||
      tool_proof_layers.first().toMap().value("renderedBounds").toMap()
              .value("width").toDouble() != 1200.0 ||
      proof_layers.at(1).toMap().contains("agentGraphic"))
    return fail("Tool-created text proof metadata must preserve scale-aware bounds, effective type size, and stack position without tagging unrelated user layers");

  const auto native_color_typography = [](const QString& color, bool include_color,
                                          bool include_untrusted_channels) {
    QVariantMap typography{{QStringLiteral("size"), 32}};
    if (include_color) typography.insert(QStringLiteral("color"), color);
    if (include_untrusted_channels)
      typography.insert(QStringLiteral("nativeColorChannels"),
          QVariantMap{{QStringLiteral("color"), QVariantList{1, 2, 3, 4}}});
    return typography;
  };
  QVariantMap null_native_color_typography{{QStringLiteral("size"), 32}};
  null_native_color_typography.insert(QStringLiteral("color"), QVariant{});
  const QVariantList native_color_source_layers{
      QVariantMap{{QStringLiteral("id"), QStringLiteral("qt-argb-warm-white")},
          {QStringLiteral("kind"), QStringLiteral("text")}, {QStringLiteral("agentGraphic"), true},
          {QStringLiteral("textTypography"), native_color_typography(QStringLiteral("#fff8f0ff"), true, true)}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("rgb-warm-white")},
          {QStringLiteral("kind"), QStringLiteral("text")}, {QStringLiteral("agentGraphic"), true},
          {QStringLiteral("textTypography"), native_color_typography(QStringLiteral("#FFF8F0"), true, false)}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("qt-argb-alpha")},
          {QStringLiteral("kind"), QStringLiteral("text")}, {QStringLiteral("agentGraphic"), true},
          {QStringLiteral("textTypography"), native_color_typography(QStringLiteral("#80FFF8F0"), true, false)}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("invalid-color")},
          {QStringLiteral("kind"), QStringLiteral("text")}, {QStringLiteral("agentGraphic"), true},
          {QStringLiteral("textTypography"), native_color_typography(
              QStringLiteral("not-a-color"), true, true)}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("missing-color")},
          {QStringLiteral("kind"), QStringLiteral("text")}, {QStringLiteral("agentGraphic"), true},
          {QStringLiteral("textTypography"), native_color_typography(QString{}, false, false)}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("null-color")},
          {QStringLiteral("kind"), QStringLiteral("text")}, {QStringLiteral("agentGraphic"), true},
          {QStringLiteral("textTypography"), null_native_color_typography}},
  };
  const auto native_color_proof_layers = photo_editor::desktop::agent_graphic_proof_layers(
      native_color_source_layers, {});
  const auto channel_row_matches = [&native_color_proof_layers](qsizetype index,
      const QString& raw_color, const QVariantList& expected_channels) {
    if (index >= native_color_proof_layers.size()) return false;
    const auto typography = native_color_proof_layers.at(index).toMap()
        .value(QStringLiteral("textTypography")).toMap();
    const auto channels = typography.value(QStringLiteral("nativeColorChannels")).toMap();
    return typography.value(QStringLiteral("color")).toString() == raw_color &&
        channels.value(QStringLiteral("color")).toList() == expected_channels;
  };
  const auto invalid_color_typography = native_color_proof_layers.size() > 3
      ? native_color_proof_layers.at(3).toMap().value(QStringLiteral("textTypography")).toMap()
      : QVariantMap{};
  const auto missing_color_typography = native_color_proof_layers.size() > 4
      ? native_color_proof_layers.at(4).toMap().value(QStringLiteral("textTypography")).toMap()
      : QVariantMap{};
  const auto null_color_typography = native_color_proof_layers.size() > 5
      ? native_color_proof_layers.at(5).toMap().value(QStringLiteral("textTypography")).toMap()
      : QVariantMap{};
  if (native_color_proof_layers.size() != 6 ||
      !channel_row_matches(0, QStringLiteral("#fff8f0ff"), QVariantList{248, 240, 255, 255}) ||
      !channel_row_matches(1, QStringLiteral("#FFF8F0"), QVariantList{255, 248, 240, 255}) ||
      !channel_row_matches(2, QStringLiteral("#80FFF8F0"), QVariantList{255, 248, 240, 128}) ||
      invalid_color_typography.value(QStringLiteral("color")).toString() != QStringLiteral("not-a-color") ||
      invalid_color_typography.contains(QStringLiteral("nativeColorChannels")) ||
      !missing_color_typography.contains(QStringLiteral("size")) ||
      missing_color_typography.contains(QStringLiteral("color")) ||
      missing_color_typography.contains(QStringLiteral("nativeColorChannels")) ||
      !null_color_typography.contains(QStringLiteral("color")) ||
      !null_color_typography.value(QStringLiteral("color")).isNull() ||
      null_color_typography.contains(QStringLiteral("nativeColorChannels")))
    return fail("Text proof metadata must derive Qt ARGB/RGB channel tuples without changing raw colors or trusting invalid, null, or missing values");

  const auto scaled_layout = photo_editor::desktop::agent_graphic_proof_layout_metrics(
      QVariantMap{{"x", 80}, {"y", 20}, {"pixelWidth", 30}, {"pixelHeight", 12},
          {"canvasWidth", 120}, {"canvasHeight", 80}, {"scale", 1.5}, {"rotation", 0.0},
          {"textTypography", QVariantMap{{"size", 32}}}});
  if (!scaled_layout.value("available").toBool() ||
      scaled_layout.value("rasterFrameWithinCanvas").toBool() ||
      std::abs(scaled_layout.value("effectiveTypographySize").toDouble() - 48.0) > 1e-9 ||
      scaled_layout.value("renderedBounds").toMap().value("x").toDouble() != 80.0 ||
      scaled_layout.value("renderedBounds").toMap().value("width").toDouble() != 45.0)
    return fail("The graphic layout proof must reject text whose scaled raster exceeds the canvas and report its effective typography size");
  const auto rotated_layout = photo_editor::desktop::agent_graphic_proof_layout_metrics(
      QVariantMap{{"x", 0}, {"y", 2}, {"pixelWidth", 20}, {"pixelHeight", 10},
          {"canvasWidth", 40}, {"canvasHeight", 40}, {"scale", 1.0}, {"rotation", 90.0}});
  if (!rotated_layout.value("available").toBool() ||
      rotated_layout.value("rasterFrameWithinCanvas").toBool() ||
      rotated_layout.value("renderedBounds").toMap().value("y").toDouble() != -3.0 ||
      rotated_layout.value("renderedBounds").toMap().value("height").toDouble() != 20.0)
    return fail("The graphic layout proof must include rotated raster extents when checking canvas clipping");
  for (const auto& layer : tool_proof_layers) critique_request.graphic_layers.append(layer);
  QStringList critique_detail_paths;
  QVariantList critique_detail_metadata;
  for (qsizetype index = 0; index < critique_request.graphic_layers.size(); ++index) {
    const auto layer = critique_request.graphic_layers.at(index).toMap();
    const auto path = runtime_root.filePath(QStringLiteral("critique-detail-%1.png").arg(index));
    QImage detail(80, 60, QImage::Format_RGB32);
    detail.fill(QColor(150 + static_cast<int>(index), 120, 100));
    if (!detail.save(path, "PNG")) return fail("Write a targeted text-layer detail proof");
    const auto layer_id = layer.value(QStringLiteral("id")).toString();
    critique_detail_paths.append(path);
    critique_detail_metadata.append(QVariantMap{
        {QStringLiteral("imagePath"), path},
        {QStringLiteral("imageSha256"), sha256_for_file(path)},
        {QStringLiteral("sourceOverviewSha256"), critique_overview_sha},
        {QStringLiteral("available"), true},
        {QStringLiteral("omitted"), false},
        {QStringLiteral("layerId"), layer_id},
        {QStringLiteral("pixelBounds"), QVariantMap{
            {QStringLiteral("x"), 10}, {QStringLiteral("y"), 20},
            {QStringLiteral("width"), 80}, {QStringLiteral("height"), 60}}},
    });
  }
  critique_request.asset.insert(QStringLiteral("additionalProofImagePaths"), critique_detail_paths);
  auto critique_visual_evidence = critique_request.asset.value(QStringLiteral("visualEvidence")).toMap();
  critique_visual_evidence.insert(QStringLiteral("targetedDetailProofs"), critique_detail_metadata);
  critique_request.asset.insert(QStringLiteral("visualEvidence"), critique_visual_evidence);
  critique_request.validation_report = QVariantMap{
      {QStringLiteral("ok"), true},
      {QStringLiteral("imageStats"),
       QVariantMap{{QStringLiteral("vectorscope"), QVariantMap{}}}},
      {QStringLiteral("compositionReview"), QVariantMap{
          {QStringLiteral("detailProofReport"), QVariantMap{
              {QStringLiteral("overviewPath"), critique_overview_path},
              {QStringLiteral("overviewSha256"), critique_overview_sha},
              {QStringLiteral("detailProofImagePaths"), critique_detail_paths},
              {QStringLiteral("targetedDetailProofs"), critique_detail_metadata},
              {QStringLiteral("renderedCompositionWidth"), 1929},
              {QStringLiteral("renderedCompositionHeight"), 1197},
          }},
      }},
  };
  critique_request.validation_report.insert(QStringLiteral("masks"), QVariantList{QVariantMap{
      {QStringLiteral("segmentationData"), QByteArray(1024 * 1024, 'Z')},
      {QStringLiteral("generatedMaskCleanupSourceData"), QString(1024 * 1024, QLatin1Char('Z'))},
      {QStringLiteral("temperature"), 12.0}}});
  critique_request.photographer_calibration =
      request.memory_packet
          .value(QStringLiteral("photographerCalibrationCard"))
          .toMap();
  critique_request.project_north_star =
      request.memory_packet.value(QStringLiteral("projectNorthStar")).toMap();
  const auto calibrated_critique =
      photo_editor::desktop::critique_agent_rendered_proof(
          critique_request);
  const auto calibrated_critique_request =
      fake_ollama_server_available
          ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
          : read_json_file(fake_ollama_request_path);
  const auto calibrated_critique_messages =
      calibrated_critique_request.value(QStringLiteral("messages"))
          .toArray();
  if (calibrated_critique_messages.isEmpty() || !calibrated_critique_messages.first().toObject()
          .value(QStringLiteral("content")).toString().contains(QStringLiteral("looksy-workmanship-v1")) ||
      !calibrated_critique_messages.first().toObject()
          .value(QStringLiteral("content")).toString().contains(QStringLiteral("layoutMetrics.renderedBounds")))
    return fail("Rendered review must share the workmanship and evidence contract");
  const auto calibrated_critique_images = calibrated_critique_messages.size() >= 2
      ? calibrated_critique_messages.at(1).toObject().value(QStringLiteral("images")).toArray()
      : QJsonArray{};
  const auto calibrated_critique_prompt =
      calibrated_critique_messages.size() >= 2
          ? calibrated_critique_messages.at(1)
                .toObject()
                .value(QStringLiteral("content"))
                .toString()
          : QString();
  if (calibrated_critique_prompt.size() > 48000 || calibrated_critique_prompt.contains(QString(64, QLatin1Char('Z'))) ||
      !calibrated_critique_prompt.contains(QStringLiteral("payloadOmitted")))
    return fail("proof critique must receive compact metadata, never stored alpha bytes as prompt text");
  auto missing_proof_request = critique_request;
  missing_proof_request.asset = {{QStringLiteral("proofImagePath"), QStringLiteral("/missing-rendered-proof.png")},
                                 {QStringLiteral("filePath"), fake_proof_path}};
  const auto missing_proof_critique = photo_editor::desktop::critique_agent_rendered_proof(missing_proof_request);
  if (missing_proof_critique.ok || missing_proof_critique.pass || !missing_proof_critique.needs_user_review)
    return fail("a missing rendered proof must not be silently replaced with the original image for critique");
  if (!calibrated_critique.ok || !calibrated_critique.pass ||
      !calibrated_critique.needs_user_review ||
      calibrated_critique.prompt_audit.value(QStringLiteral("additionalProofImageCount")).toInt() != 4 ||
      calibrated_critique_images.size() != 5 ||
      !calibrated_critique.prompt_audit
           .value(QStringLiteral("photographerCalibrationAvailable"))
           .toBool() ||
      calibrated_critique.prompt_audit
              .value(QStringLiteral("photographerCalibrationDigest"))
              .toString() != QString(64, QLatin1Char('b')) ||
      !calibrated_critique.prompt_audit
           .value(QStringLiteral("projectNorthStarAvailable"))
           .toBool() ||
      calibrated_critique.prompt_audit
              .value(QStringLiteral("projectNorthStarDigest"))
              .toString() != QString(64, QLatin1Char('c')) ||
      !calibrated_critique.prompt_audit
           .value(QStringLiteral("projectNorthStarCurrentRequestWins"))
           .toBool() ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("Photographer calibration JSON")) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("Prioritize naturalness and use restrained warmth")) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("never change numeric thresholds"),
          Qt::CaseInsensitive) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("Project North Star JSON")) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("Keep this wedding intimate and documentary")) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("current request, active style constraints, deterministic validation, and safety policy win every conflict"),
          Qt::CaseInsensitive) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("compare every expected text string"),
          Qt::CaseInsensitive) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("effectiveTypographySize")) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("renderedBounds")) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("LOOKSY FIELD NOTES")) ||
      !calibrated_critique_prompt.contains(QStringLiteral("TOOL TITLE")) ||
      !calibrated_critique_prompt.contains(QStringLiteral("EDITED TITLE")) ||
      !calibrated_critique_prompt.contains(QStringLiteral("intentional foreground occlusion")) ||
      !calibrated_critique_prompt.contains(QStringLiteral("downsampled evidence")) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("compositionReview.detailProofReport.renderedCompositionWidth")) ||
      !calibrated_critique_prompt.contains(QStringLiteral("1929")) ||
      !calibrated_critique_prompt.contains(QStringLiteral("1197")) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("do not infer them from the thumbnail evidence")) ||
      !calibrated_critique_prompt.contains(
          QStringLiteral("Expected graphic text layers JSON"))) {
    return fail("vision proof critique should consume project calibration, the North Star, and scale-aware text metrics while retaining current-request precedence");
  }

  const auto critique_components = calibrated_critique.prompt_audit
      .value(QStringLiteral("promptComponents")).toMap();
  const auto critique_messages = calibrated_critique_request.value(QStringLiteral("messages")).toArray();
  const auto critique_images = critique_messages.size() >= 2
      ? critique_messages.at(1).toObject().value(QStringLiteral("images")).toArray()
      : QJsonArray{};
  const auto critique_component_bytes = critique_components
      .value(QStringLiteral("systemPromptUtf8Bytes")).toLongLong() +
      critique_components.value(QStringLiteral("userPromptUtf8Bytes")).toLongLong();
  if (!calibrated_critique.prompt_audit.value(QStringLiteral("requestSent")).toBool() ||
      critique_components.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("ollama-proof-critique-prompt-components-v1") ||
      critique_components.value(QStringLiteral("attachedImageCount")).toInt() != 5 ||
      critique_components.value(QStringLiteral("additionalProofImageCount")).toInt() != 4 ||
      critique_images.size() != critique_components.value(QStringLiteral("attachedImageCount")).toInt() ||
      critique_components.value(QStringLiteral("aggregateTextUtf8Bytes")).toLongLong() !=
          critique_component_bytes ||
      critique_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() >
          critique_components.value(QStringLiteral("contextTokenLimit")).toInt() ||
      critique_components.value(QStringLiteral("contextTokenLimit")).toInt() != 24576 ||
      critique_components.value(QStringLiteral("contextTokensRequested")).toInt() != 24576 ||
      critique_components.value(QStringLiteral("maxOutputTokens")).toInt() != 512 ||
      critique_components.value(QStringLiteral("responseSchemaUtf8Bytes")).toInt() <= 0 ||
      calibrated_critique_request.value(QStringLiteral("options")).toObject()
          .value(QStringLiteral("num_ctx")).toInt() != 24576 ||
      calibrated_critique.prompt_audit.value(QStringLiteral("systemPrompt")).toString().isEmpty() ||
      calibrated_critique.prompt_audit.value(QStringLiteral("userPrompt")).toString().isEmpty() ||
      calibrated_critique.prompt_audit.value(QStringLiteral("responseSchema")).toMap().isEmpty())
    return fail("a dispatched five-image critique must audit the complete text envelope, image count and selected model context limit");

  if (fake_ollama_server_available) {
    auto clipped_critique_response = calibrated_critique_response;
    clipped_critique_response.insert(QStringLiteral("done_reason"), QStringLiteral("length"));
    clipped_critique_response.insert(QStringLiteral("eval_count"), 512);
    fake_ollama_server.setResponseSequence(QStringLiteral("/api/chat"),
        {clipped_critique_response, calibrated_critique_response});
    const auto before_critic_recovery = fake_ollama_server.requestsForPath(QStringLiteral("/api/chat")).size();
    const auto recovered_critique = photo_editor::desktop::critique_agent_rendered_proof(critique_request);
    const auto critic_posts = fake_ollama_server.requestsForPath(QStringLiteral("/api/chat")).mid(before_critic_recovery);
    if (!recovered_critique.ok || critic_posts.size() != 2 ||
        recovered_critique.prompt_audit.value(QStringLiteral("responseAttemptCount")).toInt() != 2 ||
        recovered_critique.prompt_audit.value(QStringLiteral("outputRecoveryMaxOutputTokens")).toInt() != 1024 ||
        recovered_critique.prompt_audit.value(QStringLiteral("outputRecoveryEstimatedContextTokens")).toInt() > 24576)
      return fail("A length-terminated proof review must get one bounded read-only recovery before acceptance");
    auto restored_second = critic_posts.last();
    auto restored_options = restored_second.value(QStringLiteral("options")).toObject();
    if (restored_options.value(QStringLiteral("num_predict")).toInt() != 1024)
      return fail("Critic recovery must double only its bounded output allowance");
    restored_options.insert(QStringLiteral("num_predict"), 512);
    restored_second.insert(QStringLiteral("options"), restored_options);
    if (restored_second != critic_posts.first() || critic_posts.last().value(QStringLiteral("messages")).toArray()
        .last().toObject().value(QStringLiteral("images")).toArray().size() != 5)
      return fail("Critic recovery must retain every identical overview/detail pixel and all metadata/schema");
    auto second_clipped_critique = clipped_critique_response;
    second_clipped_critique.insert(QStringLiteral("eval_count"), 1024);
    fake_ollama_server.setResponseSequence(QStringLiteral("/api/chat"),
        {clipped_critique_response, second_clipped_critique, calibrated_critique_response});
    const auto before_failed_critic = fake_ollama_server.requestsForPath(QStringLiteral("/api/chat")).size();
    const auto failed_critique = photo_editor::desktop::critique_agent_rendered_proof(critique_request);
    if (failed_critique.ok || failed_critique.pass || !failed_critique.needs_user_review ||
        fake_ollama_server.requestsForPath(QStringLiteral("/api/chat")).size() - before_failed_critic != 2)
      return fail("A second length-terminated review must require human review and never make a third request");
    fake_ollama_server.setChunkedResponse(QStringLiteral("/api/chat"), calibrated_critique_response);
    if (!photo_editor::desktop::critique_agent_rendered_proof(critique_request).ok)
      return fail("Restore the ordinary one-request review fixture after recovery checks");
  }
  auto last_valid_critique_request = calibrated_critique_request;
  auto missing_report_detail_binding_request = critique_request;
  auto missing_report_binding = missing_report_detail_binding_request.validation_report;
  auto missing_report_review = missing_report_binding.value(QStringLiteral("compositionReview")).toMap();
  auto missing_report_detail = missing_report_review.value(QStringLiteral("detailProofReport")).toMap();
  missing_report_detail.remove(QStringLiteral("targetedDetailProofs"));
  missing_report_review.insert(QStringLiteral("detailProofReport"), missing_report_detail);
  missing_report_binding.insert(QStringLiteral("compositionReview"), missing_report_review);
  missing_report_detail_binding_request.validation_report = missing_report_binding;
  const auto missing_report_detail_binding =
      photo_editor::desktop::critique_agent_rendered_proof(missing_report_detail_binding_request);
  const auto request_after_missing_report_detail_binding = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  if (!missing_report_detail_binding.ok || missing_report_detail_binding.pass ||
      !missing_report_detail_binding.needs_user_review ||
      !missing_report_detail_binding.prompt_audit.value(QStringLiteral("requestSent")).toBool() ||
      missing_report_detail_binding.prompt_audit.value(QStringLiteral("metadataEvidenceComplete")).toBool() ||
      missing_report_detail_binding.prompt_audit.value(QStringLiteral("reportDetailBindingsComplete")).toBool() ||
      !missing_report_detail_binding.prompt_audit.value(QStringLiteral("userPrompt")).toString()
          .contains(QStringLiteral("do not infer missing crop identities")) ||
      request_after_missing_report_detail_binding == last_valid_critique_request)
    return fail("Missing report-side layer mapping must remain human-review-only and never claim a graphic proof pass");
  last_valid_critique_request = request_after_missing_report_detail_binding;

  auto duplicate_graphic_id_request = critique_request;
  auto duplicate_graphic_layers = duplicate_graphic_id_request.graphic_layers;
  auto duplicate_graphic_layer = duplicate_graphic_layers.last().toMap();
  duplicate_graphic_layer.insert(QStringLiteral("id"),
      duplicate_graphic_layers.first().toMap().value(QStringLiteral("id")));
  duplicate_graphic_layers.last() = duplicate_graphic_layer;
  duplicate_graphic_id_request.graphic_layers = duplicate_graphic_layers;
  const auto duplicate_graphic_id_critique =
      photo_editor::desktop::critique_agent_rendered_proof(duplicate_graphic_id_request);
  const auto request_after_duplicate_graphic_id = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  if (duplicate_graphic_id_critique.ok || duplicate_graphic_id_critique.pass ||
      duplicate_graphic_id_critique.prompt_audit.value(QStringLiteral("requestSent")).toBool() ||
      !duplicate_graphic_id_critique.error_message.contains(QStringLiteral("distinct stable identity")) ||
      request_after_duplicate_graphic_id != last_valid_critique_request)
    return fail("duplicate expected graphic IDs must fail before HTTP because one detail crop cannot cover multiple required targets");

  auto critique_16k_request = critique_request;
  critique_16k_request.model_id = QStringLiteral("ollama-qwen-test");
  critique_16k_request.user_instructions =
      QStringLiteral("Bounded 16K negative-case brief: ") + QString(24000, QLatin1Char('x'));
  const auto critique_16k_rejected =
      photo_editor::desktop::critique_agent_rendered_proof(critique_16k_request);
  const auto request_after_critique_16k = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto critique_16k_components = critique_16k_rejected.prompt_audit
      .value(QStringLiteral("promptComponents")).toMap();
  if (critique_16k_rejected.ok || critique_16k_rejected.pass ||
      !critique_16k_rejected.needs_user_review ||
      critique_16k_rejected.prompt_audit.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("proof_critique_context_budget_exceeded") ||
      critique_16k_rejected.prompt_audit.value(QStringLiteral("requestSent")).toBool() ||
      critique_16k_components.value(QStringLiteral("contextTokenLimit")).toInt() != 16384 ||
      critique_16k_components.value(QStringLiteral("attachedImageCount")).toInt() != 5 ||
      critique_16k_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() <= 16384 ||
      critique_16k_rejected.prompt_audit.value(QStringLiteral("metadataEvidenceComplete")).toBool() !=
          calibrated_critique.prompt_audit.value(QStringLiteral("metadataEvidenceComplete")).toBool() ||
      request_after_critique_16k != last_valid_critique_request)
    return fail("a five-image critique exceeding the selected 16K context must keep its audit and fail before HTTP");

  auto oversized_ascii_critique_request = critique_request;
  oversized_ascii_critique_request.user_instructions = QString(120000, QLatin1Char('x'));
  const auto oversized_ascii_critique = photo_editor::desktop::critique_agent_rendered_proof(
      oversized_ascii_critique_request);
  const auto request_after_oversized_ascii = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto oversized_ascii_components = oversized_ascii_critique.prompt_audit
      .value(QStringLiteral("promptComponents")).toMap();
  if (oversized_ascii_critique.ok || oversized_ascii_critique.pass ||
      !oversized_ascii_critique.needs_user_review ||
      oversized_ascii_critique.prompt_audit.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("proof_critique_context_budget_exceeded") ||
      oversized_ascii_critique.prompt_audit.value(QStringLiteral("requestSent")).toBool() ||
      oversized_ascii_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() <=
          oversized_ascii_components.value(QStringLiteral("contextTokenLimit")).toInt() ||
      oversized_ascii_critique.prompt_audit.value(QStringLiteral("userPromptCharCount")).toInt() < 120000 ||
      request_after_oversized_ascii != last_valid_critique_request)
    return fail("an oversized ASCII proof-critique envelope must be fully audited and rejected before HTTP");

  const auto critique_budget_margin = critique_components.value(QStringLiteral("contextTokenLimit")).toInt() -
      critique_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt();
  const auto critique_cjk_count = std::max(1, critique_budget_margin / 2);
  auto oversized_cjk_critique_request = critique_request;
  oversized_cjk_critique_request.user_instructions = QStringLiteral("Additional approved text: ") +
      QString(critique_cjk_count, QChar(0x4e00));
  const auto oversized_cjk_critique = photo_editor::desktop::critique_agent_rendered_proof(
      oversized_cjk_critique_request);
  const auto request_after_oversized_cjk = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto oversized_cjk_components = oversized_cjk_critique.prompt_audit
      .value(QStringLiteral("promptComponents")).toMap();
  const auto cjk_aggregate_bytes = oversized_cjk_components
      .value(QStringLiteral("aggregateTextUtf8Bytes")).toLongLong();
  const auto cjk_image_reservations = oversized_cjk_components
      .value(QStringLiteral("imageContextReservations")).toList();
  const auto cjk_output_reserve = 512 + oversized_cjk_components
      .value(QStringLiteral("maxOutputTokens")).toInt() +
      image_reservation_total(oversized_cjk_components);
  const auto naive_cjk_context_estimate = (cjk_aggregate_bytes + 3) / 4 + cjk_output_reserve;
  if (oversized_cjk_critique.ok || oversized_cjk_critique.pass ||
      !oversized_cjk_critique.needs_user_review ||
      oversized_cjk_critique.prompt_audit.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("proof_critique_context_budget_exceeded") ||
      oversized_cjk_critique.prompt_audit.value(QStringLiteral("requestSent")).toBool() ||
      cjk_image_reservations.size() !=
          oversized_cjk_components.value(QStringLiteral("attachedImageCount")).toInt() ||
      oversized_cjk_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() <= 24576 ||
      naive_cjk_context_estimate > 24576 ||
      oversized_cjk_components.value(QStringLiteral("estimatedNonAsciiCodePointCount")).toInt() < critique_cjk_count ||
      request_after_oversized_cjk != last_valid_critique_request)
    return fail("non-ASCII critique text must use the conservative byte-fallback estimate and fail before HTTP when a quarter-byte heuristic would fit");

  const auto captured_app12_fixture_path = repository_file_path(
      QStringLiteral("tests/fixtures/agent_local_model_app12_critic_envelope.json"));
  QFile captured_app12_fixture_file(captured_app12_fixture_path);
  if (captured_app12_fixture_path.isEmpty() || !captured_app12_fixture_file.open(QIODevice::ReadOnly))
    return fail("Open the captured App12 critic envelope fixture");
  const auto captured_app12_document = QJsonDocument::fromJson(captured_app12_fixture_file.readAll());
  if (!captured_app12_document.isObject()) return fail("Parse the captured App12 critic envelope fixture");
  const auto captured_app12 = captured_app12_document.object();
  const auto captured_graphic_layers = captured_app12.value(QStringLiteral("graphicLayers")).toArray().toVariantList();
  if (captured_graphic_layers.size() != 3)
    return fail("The captured App12 critic fixture must retain its three native text-layer targets");

  const auto captured_overview_path = runtime_root.filePath(QStringLiteral("app12-captured-overview.png"));
  QImage captured_overview(1024, 1536, QImage::Format_RGB32);
  captured_overview.fill(QColor(43, 79, 111));
  if (!captured_overview.save(captured_overview_path, "PNG"))
    return fail("Write the captured App12 overview image into the test runtime");
  const auto captured_overview_sha = sha256_for_file(captured_overview_path);
  QStringList captured_detail_paths;
  QVariantList captured_detail_metadata;
  auto captured_validation = captured_app12.value(QStringLiteral("validationReport")).toObject().toVariantMap();
  auto captured_review = captured_validation.value(QStringLiteral("compositionReview")).toMap();
  auto captured_detail_report = captured_review.value(QStringLiteral("detailProofReport")).toMap();
  auto captured_report_rows = captured_detail_report.value(QStringLiteral("targetedDetailProofs")).toList();
  for (qsizetype index = 0; index < captured_report_rows.size(); ++index) {
    auto row = captured_report_rows.at(index).toMap();
    const auto bounds = row.value(QStringLiteral("pixelBounds")).toMap();
    const auto width = std::max(1, bounds.value(QStringLiteral("width")).toInt());
    const auto height = std::max(1, bounds.value(QStringLiteral("height")).toInt());
    const auto path = runtime_root.filePath(QStringLiteral("app12-captured-detail-%1.png").arg(index));
    QImage detail(width, height, QImage::Format_RGB32);
    detail.fill(QColor(122 + static_cast<int>(index), 96, 75));
    if (!detail.save(path, "PNG")) return fail("Write a captured App12 targeted detail image");
    const auto detail_sha = sha256_for_file(path);
    row.insert(QStringLiteral("imagePath"), path);
    row.insert(QStringLiteral("imageSha256"), detail_sha);
    row.insert(QStringLiteral("sourceOverviewSha256"), captured_overview_sha);
    captured_report_rows[index] = row;
    captured_detail_paths.append(path);
    captured_detail_metadata.append(row);
  }
  if (captured_report_rows.size() != captured_graphic_layers.size())
    return fail("The captured App12 detail proof count must match the native graphic target count");
  captured_review.insert(QStringLiteral("targetedDetailProofs"), captured_report_rows);
  captured_review.insert(QStringLiteral("detailProofImagePaths"), captured_detail_paths);
  captured_detail_report.insert(QStringLiteral("overviewPath"), captured_overview_path);
  captured_detail_report.insert(QStringLiteral("overviewSha256"), captured_overview_sha);
  captured_detail_report.insert(QStringLiteral("detailProofImagePaths"), captured_detail_paths);
  captured_detail_report.insert(QStringLiteral("targetedDetailProofs"), captured_report_rows);
  captured_review.insert(QStringLiteral("detailProofReport"), captured_detail_report);
  captured_validation.insert(QStringLiteral("compositionReview"), captured_review);
  auto captured_photo_evidence = captured_validation
      .value(QStringLiteral("photographicQualityEvidence")).toMap();
  captured_photo_evidence.insert(QStringLiteral("currentProofSha256"), captured_overview_sha);
  captured_validation.insert(QStringLiteral("photographicQualityEvidence"), captured_photo_evidence);

  auto mismatched_live_evidence_validation = captured_validation;
  auto mismatched_live_evidence = mismatched_live_evidence_validation
      .value(QStringLiteral("liveRepairEvidence")).toMap();
  mismatched_live_evidence.remove(QStringLiteral("compositionReviewReference"));
  mismatched_live_evidence.remove(QStringLiteral("targetedDetailProofsReference"));
  auto mismatched_live_review = captured_review;
  mismatched_live_review.insert(QStringLiteral("sourceTaskStateHash"), QString(64, QLatin1Char('d')));
  auto mismatched_live_detail_rows = captured_report_rows;
  if (!mismatched_live_detail_rows.isEmpty()) {
    auto stale_detail = mismatched_live_detail_rows.first().toMap();
    stale_detail.insert(QStringLiteral("imageSha256"), QString(64, QLatin1Char('e')));
    mismatched_live_detail_rows[0] = stale_detail;
  }
  mismatched_live_evidence.insert(QStringLiteral("compositionReview"), mismatched_live_review);
  mismatched_live_evidence.insert(QStringLiteral("targetedDetailProofs"), mismatched_live_detail_rows);
  mismatched_live_evidence_validation.insert(QStringLiteral("liveRepairEvidence"), mismatched_live_evidence);
  const auto mismatched_live_evidence_projection =
      photo_editor::desktop::agent_detail::proof_critique_validation_projection(
          mismatched_live_evidence_validation);
  const auto projected_live_evidence = mismatched_live_evidence_projection
      .value(QStringLiteral("liveRepairEvidence")).toMap();
  if (projected_live_evidence.value(QStringLiteral("compositionReview")).toMap() !=
          mismatched_live_review ||
      projected_live_evidence.value(QStringLiteral("targetedDetailProofs")).toList() !=
          mismatched_live_detail_rows ||
      projected_live_evidence.contains(QStringLiteral("compositionReviewReference")) ||
      projected_live_evidence.contains(QStringLiteral("targetedDetailProofsReference")))
    return fail("Native critique projection must preserve mismatched live repair evidence without false reference aliases");

  photo_editor::desktop::AgentLocalModelProofCritiqueRequest captured_app12_request;
  captured_app12_request.model_id = QStringLiteral("ollama-app09-budget-test");
  captured_app12_request.style = captured_app12.value(QStringLiteral("style")).toObject().toVariantMap();
  captured_app12_request.user_instructions = captured_app12.value(QStringLiteral("userInstructions")).toString();
  captured_app12_request.graphic_layers = captured_graphic_layers;
  captured_app12_request.validation_report = captured_validation;
  captured_app12_request.asset = QVariantMap{
      {QStringLiteral("assetId"), captured_app12.value(QStringLiteral("assetId")).toString()},
      {QStringLiteral("proofImagePath"), captured_overview_path},
      {QStringLiteral("planningProofRequired"), true},
      {QStringLiteral("planningProofSha256"), captured_overview_sha},
      {QStringLiteral("additionalProofImagePaths"), captured_detail_paths},
      {QStringLiteral("visualEvidence"), QVariantMap{
          {QStringLiteral("targetedDetailProofs"), captured_detail_metadata},
      }},
  };
  const auto captured_native_projection =
      photo_editor::desktop::agent_detail::proof_critique_validation_projection(
          captured_validation);
  captured_app12_request.validation_report = captured_native_projection;
  const auto captured_app12_critique = photo_editor::desktop::critique_agent_rendered_proof(
      captured_app12_request);
  const auto captured_app12_critique_request = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto captured_app12_components = captured_app12_critique.prompt_audit
      .value(QStringLiteral("promptComponents")).toMap();
  const auto captured_app12_image_reservations = captured_app12_components
      .value(QStringLiteral("imageContextReservations")).toList();
  const auto captured_app12_messages = captured_app12_critique_request
      .value(QStringLiteral("messages")).toArray();
  const auto captured_app12_prompt = captured_app12_messages.size() >= 2
      ? captured_app12_messages.at(1).toObject().value(QStringLiteral("content")).toString()
      : QString{};
  const auto captured_image_payloads = captured_app12_messages.size() >= 2
      ? captured_app12_messages.at(1).toObject().value(QStringLiteral("images")).toArray()
      : QJsonArray{};
  bool captured_image_reservations_match = captured_image_payloads.size() == 4 &&
      captured_app12_image_reservations.size() == 4;
  bool captured_small_image_observed = false;
  bool captured_large_image_observed = false;
  if (captured_image_reservations_match) {
    for (qsizetype index = 0; index < captured_app12_image_reservations.size(); ++index) {
      const auto reservation = captured_app12_image_reservations.at(index).toMap();
      const auto transmitted_image = QImage::fromData(QByteArray::fromBase64(
          captured_image_payloads.at(index).toString().toLatin1()));
      const auto small_image = !transmitted_image.isNull() &&
          transmitted_image.width() <= 768 && transmitted_image.height() <= 768;
      captured_small_image_observed |= small_image;
      captured_large_image_observed |= !transmitted_image.isNull() && !small_image;
      captured_image_reservations_match &= !transmitted_image.isNull() &&
          reservation.value(QStringLiteral("imageIndex")).toLongLong() == index &&
          reservation.value(QStringLiteral("modelFamily")).toString() == QStringLiteral("qwen2.5vl") &&
          reservation.value(QStringLiteral("dimensionsValid")).toBool() &&
          reservation.value(QStringLiteral("transmittedWidth")).toInt() == transmitted_image.width() &&
          reservation.value(QStringLiteral("transmittedHeight")).toInt() == transmitted_image.height() &&
          reservation.value(QStringLiteral("reserveTokens")).toInt() == (small_image ? 2048 : 4096);
    }
  }
  if (!captured_app12_critique.ok ||
      !captured_app12_critique.prompt_audit.value(QStringLiteral("requestSent")).toBool() ||
      !captured_app12_critique.prompt_audit.value(QStringLiteral("metadataEvidenceComplete")).toBool() ||
      captured_app12_components.value(QStringLiteral("attachedImageCount")).toInt() != 4 ||
      captured_app12_components.value(QStringLiteral("additionalProofImageCount")).toInt() != 3 ||
      captured_image_payloads.size() != 4 ||
      captured_app12_image_reservations.size() != 4 ||
      !captured_image_reservations_match || !captured_small_image_observed ||
      !captured_large_image_observed ||
      captured_app12_components.value(QStringLiteral("contextTokenLimit")).toInt() != 24576 ||
      captured_app12_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() > 24576 ||
      captured_app12_components.value(QStringLiteral("aggregateTextUtf8Bytes")).toLongLong() <= 0 ||
      captured_app12_critique_request.value(QStringLiteral("options")).toObject()
          .value(QStringLiteral("num_ctx")).toInt() != 24576)
    return fail(QStringLiteral("The captured App12 four-image proof envelope must fit and dispatch with complete metadata under the 24K context cap (aggregateBytes=%1 estimate=%2 asciiBytes=%3 nonAsciiBytes=%4 attachedImages=%5 limit=%6)")
        .arg(captured_app12_components.value(QStringLiteral("aggregateTextUtf8Bytes")).toLongLong())
        .arg(captured_app12_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt())
        .arg(captured_app12_components.value(QStringLiteral("estimatedAsciiUtf8Bytes")).toLongLong())
        .arg(captured_app12_components.value(QStringLiteral("estimatedNonAsciiUtf8Bytes")).toLongLong())
        .arg(captured_app12_components.value(QStringLiteral("attachedImageCount")).toInt())
        .arg(captured_app12_components.value(QStringLiteral("contextTokenLimit")).toInt()));
  std::cout << "Captured App12 critic envelope: aggregateBytes="
            << captured_app12_components.value(QStringLiteral("aggregateTextUtf8Bytes")).toLongLong()
            << " estimate="
            << captured_app12_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt()
            << " asciiBytes="
            << captured_app12_components.value(QStringLiteral("estimatedAsciiUtf8Bytes")).toLongLong()
            << " nonAsciiBytes="
            << captured_app12_components.value(QStringLiteral("estimatedNonAsciiUtf8Bytes")).toLongLong()
            << " attachedImages="
            << captured_app12_components.value(QStringLiteral("attachedImageCount")).toInt()
            << " limit="
            << captured_app12_components.value(QStringLiteral("contextTokenLimit")).toInt()
            << '\n';

  const auto mismatched_detail_path = runtime_root.filePath(QStringLiteral("app12-mismatched-detail.png"));
  QImage mismatched_detail(24, 24, QImage::Format_RGB32);
  mismatched_detail.fill(QColor(21, 78, 134));
  if (!mismatched_detail.save(mismatched_detail_path, "PNG"))
    return fail("Write a valid but unbound App12 detail path for the path-integrity regression");
  auto mismatched_asset_path_request = captured_app12_request;
  auto mismatched_asset_detail_paths = captured_detail_paths;
  mismatched_asset_detail_paths[0] = mismatched_detail_path;
  mismatched_asset_path_request.asset.insert(QStringLiteral("additionalProofImagePaths"),
      mismatched_asset_detail_paths);
  const auto mismatched_asset_path_critique =
      photo_editor::desktop::critique_agent_rendered_proof(mismatched_asset_path_request);
  const auto after_mismatched_asset_path = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  auto mismatched_visual_evidence_request = captured_app12_request;
  auto mismatched_visual_evidence = mismatched_visual_evidence_request.asset
      .value(QStringLiteral("visualEvidence")).toMap();
  auto mismatched_visual_details = mismatched_visual_evidence
      .value(QStringLiteral("targetedDetailProofs")).toList();
  auto mismatched_visual_detail = mismatched_visual_details.first().toMap();
  mismatched_visual_detail.insert(QStringLiteral("imagePath"), mismatched_detail_path);
  mismatched_visual_details[0] = mismatched_visual_detail;
  mismatched_visual_evidence.insert(QStringLiteral("targetedDetailProofs"), mismatched_visual_details);
  mismatched_visual_evidence_request.asset.insert(QStringLiteral("visualEvidence"),
      mismatched_visual_evidence);
  const auto mismatched_visual_path_critique =
      photo_editor::desktop::critique_agent_rendered_proof(mismatched_visual_evidence_request);
  const auto after_mismatched_visual_path = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  auto mismatched_report_path_request = captured_app12_request;
  auto mismatched_report_validation = mismatched_report_path_request.validation_report;
  auto mismatched_report_review = mismatched_report_validation
      .value(QStringLiteral("compositionReview")).toMap();
  auto mismatched_report_detail = mismatched_report_review
      .value(QStringLiteral("detailProofReport")).toMap();
  auto mismatched_report_paths = mismatched_report_detail
      .value(QStringLiteral("detailProofImagePaths")).toStringList();
  mismatched_report_paths[0] = mismatched_detail_path;
  mismatched_report_detail.insert(QStringLiteral("detailProofImagePaths"), mismatched_report_paths);
  mismatched_report_review.insert(QStringLiteral("detailProofReport"), mismatched_report_detail);
  mismatched_report_validation.insert(QStringLiteral("compositionReview"), mismatched_report_review);
  mismatched_report_path_request.validation_report = mismatched_report_validation;
  const auto mismatched_report_path_critique =
      photo_editor::desktop::critique_agent_rendered_proof(mismatched_report_path_request);
  const auto after_mismatched_report_path = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  auto mismatched_report_row_request = captured_app12_request;
  auto mismatched_row_validation = mismatched_report_row_request.validation_report;
  auto mismatched_row_review = mismatched_row_validation
      .value(QStringLiteral("compositionReview")).toMap();
  auto mismatched_row_detail = mismatched_row_review
      .value(QStringLiteral("detailProofReport")).toMap();
  auto mismatched_report_rows = mismatched_row_detail
      .value(QStringLiteral("targetedDetailProofs")).toList();
  auto mismatched_report_row = mismatched_report_rows.first().toMap();
  mismatched_report_row.insert(QStringLiteral("imagePath"), mismatched_detail_path);
  mismatched_report_rows[0] = mismatched_report_row;
  mismatched_row_detail.insert(QStringLiteral("targetedDetailProofs"), mismatched_report_rows);
  mismatched_row_review.insert(QStringLiteral("detailProofReport"), mismatched_row_detail);
  mismatched_row_validation.insert(QStringLiteral("compositionReview"), mismatched_row_review);
  mismatched_report_row_request.validation_report = mismatched_row_validation;
  const auto mismatched_report_row_critique =
      photo_editor::desktop::critique_agent_rendered_proof(mismatched_report_row_request);
  const auto after_mismatched_report_row = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  if (mismatched_asset_path_critique.ok || mismatched_asset_path_critique.pass ||
      mismatched_asset_path_critique.prompt_audit.value(QStringLiteral("requestSent")).toBool() ||
      mismatched_visual_path_critique.ok || mismatched_visual_path_critique.pass ||
      mismatched_visual_path_critique.prompt_audit.value(QStringLiteral("requestSent")).toBool() ||
      mismatched_report_path_critique.ok || mismatched_report_path_critique.pass ||
      mismatched_report_path_critique.prompt_audit.value(QStringLiteral("requestSent")).toBool() ||
      mismatched_report_row_critique.ok || mismatched_report_row_critique.pass ||
      mismatched_report_row_critique.prompt_audit.value(QStringLiteral("requestSent")).toBool() ||
      after_mismatched_asset_path != captured_app12_critique_request ||
      after_mismatched_visual_path != captured_app12_critique_request ||
      after_mismatched_report_path != captured_app12_critique_request ||
      after_mismatched_report_row != captured_app12_critique_request)
    return fail("Captured App12 detail paths must match the exact loaded asset and metadata rows before any image index is emitted or request sent");

  bool expected_graphics_decoded = false;
  const auto encoded_graphics = prompt_json_value_after(
      captured_app12_prompt, QStringLiteral("Expected graphic text layers JSON:\n"))
      .toObject().toVariantMap();
  const auto decoded_graphics = decode_prompt_graphic_layer_rows(
      encoded_graphics, &expected_graphics_decoded);
  const auto encoded_validation_wire = prompt_json_value_after(
      captured_app12_prompt, QStringLiteral("Validation report JSON:\n"))
      .toObject().toVariantMap();
  const auto encoded_sha256_values = encoded_validation_wire
      .value(QStringLiteral("hashes")).toList();
  bool sha256_references_resolved = true;
  const auto encoded_validation = resolve_prompt_sha256_references(
      encoded_validation_wire, encoded_sha256_values, &sha256_references_resolved).toMap();
  const auto encoded_review = encoded_validation.value(QStringLiteral("compositionReview")).toMap();
  QVariantList captured_graphic_ids;
  for (const auto& graphic_value : captured_graphic_layers)
    captured_graphic_ids.append(graphic_value.toMap().value(QStringLiteral("id")));
  const auto encoded_native_rows = encoded_validation.value(QStringLiteral("compositionReview")).toMap()
      .value(QStringLiteral("inspection")).toMap()
      .value(QStringLiteral("structuredContent")).toMap().value(QStringLiteral("layers")).toMap();
  bool native_rows_decoded = false;
  const auto decoded_native_rows = decode_lossless_columnar_rows(
      encoded_native_rows, &native_rows_decoded, captured_graphic_ids);
  const auto encoded_geometry_checks = encoded_review.value(QStringLiteral("geometryChecks")).toMap();
  bool geometry_checks_decoded = false;
  const auto decoded_geometry_checks =
      decode_lossless_columnar_rows(
          encoded_geometry_checks, &geometry_checks_decoded, captured_graphic_ids);
  const auto geometry_layout_metric_reference_fields = encoded_review
      .value(QStringLiteral("geometryChecksLayoutMetricsReferenceFields")).toStringList();
  const auto native_expected_rows = captured_native_projection
      .value(QStringLiteral("compositionReview")).toMap()
      .value(QStringLiteral("inspection")).toMap()
      .value(QStringLiteral("structuredContent")).toMap().value(QStringLiteral("layers")).toList();
  const auto geometry_expected_rows = captured_native_projection
      .value(QStringLiteral("compositionReview")).toMap()
      .value(QStringLiteral("geometryChecks")).toList();
  const auto detail_rows_encoded = encoded_validation.value(QStringLiteral("compositionReview")).toMap()
      .value(QStringLiteral("detailProofReport")).toMap()
      .value(QStringLiteral("targetedDetailProofs")).toMap();
  bool detail_rows_decoded = false;
  const auto decoded_detail_rows = decode_lossless_columnar_rows(
      detail_rows_encoded, &detail_rows_decoded, captured_graphic_ids);
  auto captured_graphics_with_native_colors = captured_graphic_layers;
  bool captured_graphic_colors_bind = captured_graphics_with_native_colors.size() == 3;
  for (qsizetype index = 0; captured_graphic_colors_bind &&
       index < captured_graphics_with_native_colors.size(); ++index) {
    auto layer = captured_graphics_with_native_colors.at(index).toMap();
    auto typography = layer.value(QStringLiteral("textTypography")).toMap();
    const auto raw_color = typography.value(QStringLiteral("color"));
    const QColor parsed_color(raw_color.toString());
    if (raw_color.metaType().id() != QMetaType::QString || !parsed_color.isValid()) {
      captured_graphic_colors_bind = false;
      break;
    }
    const auto original_color = raw_color.toString();
    typography.insert(QStringLiteral("nativeColorChannels"), QVariantMap{
        {QStringLiteral("color"), QVariantList{
            parsed_color.red(), parsed_color.green(), parsed_color.blue(), parsed_color.alpha()}}});
    captured_graphic_colors_bind = typography.value(QStringLiteral("color")).toString() == original_color;
    layer.insert(QStringLiteral("textTypography"), typography);
    captured_graphics_with_native_colors[index] = layer;
  }
  bool captured_rows_round_trip = expected_graphics_decoded &&
      captured_graphic_colors_bind && decoded_graphics == captured_graphics_with_native_colors && native_rows_decoded &&
      decoded_native_rows.size() == native_expected_rows.size() && geometry_checks_decoded &&
      decoded_geometry_checks.size() == geometry_expected_rows.size() && detail_rows_decoded &&
      decoded_detail_rows.size() == captured_report_rows.size();
  for (qsizetype index = 0; captured_rows_round_trip && index < native_expected_rows.size(); ++index) {
    const auto source = native_expected_rows.at(index).toMap();
    const auto decoded = decoded_native_rows.at(index).toMap();
    const auto layer_id = source.value(QStringLiteral("id")).toString();
    if (decoded.value(QStringLiteral("id")).toString() != layer_id) {
      captured_rows_round_trip = false;
      break;
    }
    QVariantMap graphic;
    for (const auto& graphic_value : captured_graphic_layers) {
      const auto candidate = graphic_value.toMap();
      if (candidate.value(QStringLiteral("id")).toString() == layer_id) {
        graphic = candidate;
        break;
      }
    }
    if (graphic.isEmpty()) {
      captured_rows_round_trip = false;
      break;
    }
    const auto matches = decoded.value(QStringLiteral("exactFieldMatches")).toStringList();
    for (auto field = source.cbegin(); field != source.cend(); ++field) {
      if (decoded.contains(field.key())) {
        if (decoded.value(field.key()) != field.value()) captured_rows_round_trip = false;
      } else if (!matches.contains(field.key()) || !graphic.contains(field.key()) ||
          graphic.value(field.key()) != field.value()) {
        captured_rows_round_trip = false;
      }
    }
  }
  for (qsizetype index = 0; captured_rows_round_trip && index < geometry_expected_rows.size(); ++index) {
    auto decoded = decoded_geometry_checks.at(index).toMap();
    const auto expected = geometry_expected_rows.at(index).toMap();
    const auto layer_id = decoded.value(QStringLiteral("layerId")).toString();
    if (layer_id != expected.value(QStringLiteral("layerId")).toString()) {
      captured_rows_round_trip = false;
      break;
    }
    if (decoded.value(QStringLiteral("layoutMetricsReference")).toBool()) {
      QVariantMap graphic;
      for (const auto& graphic_value : decoded_graphics) {
        const auto candidate = graphic_value.toMap();
        if (candidate.value(QStringLiteral("id")).toString() == layer_id) {
          graphic = candidate;
          break;
        }
      }
      const auto layout_metrics = graphic.value(QStringLiteral("layoutMetrics")).toMap();
      decoded.remove(QStringLiteral("layoutMetricsReference"));
      if (graphic.isEmpty() || geometry_layout_metric_reference_fields.isEmpty()) {
        captured_rows_round_trip = false;
        break;
      }
      for (const auto& field : geometry_layout_metric_reference_fields) {
        if (!layout_metrics.contains(field)) {
          captured_rows_round_trip = false;
          break;
        }
        decoded.insert(field, layout_metrics.value(field));
      }
    }
    if (captured_rows_round_trip && decoded != expected) captured_rows_round_trip = false;
  }
  for (qsizetype index = 0; captured_rows_round_trip && index < decoded_detail_rows.size(); ++index) {
    auto expected = captured_report_rows.at(index).toMap();
    expected.remove(QStringLiteral("imagePath"));
    expected.insert(QStringLiteral("attachedImageIndex"), index + 1);
    if (decoded_detail_rows.at(index).toMap() != expected) captured_rows_round_trip = false;
  }
  const auto encoded_inspection_content = encoded_review.value(QStringLiteral("inspection")).toMap()
      .value(QStringLiteral("structuredContent")).toMap();
  const auto encoded_display_policy = encoded_inspection_content
      .value(QStringLiteral("displayTextColorPolicy")).toMap();
  const auto raw_photo_evidence = captured_validation
      .value(QStringLiteral("photographicQualityEvidence")).toMap();
  const auto encoded_photo_evidence = encoded_validation
      .value(QStringLiteral("photographicQualityEvidence")).toMap();
  const auto canonical_photo_evidence = captured_native_projection
      .value(QStringLiteral("photographicQualityEvidence")).toMap();
  auto expected_baseline_scalar_stats = canonical_photo_evidence
      .value(QStringLiteral("baselineImageStats")).toMap();
  expected_baseline_scalar_stats.remove(QStringLiteral("histogram"));
  expected_baseline_scalar_stats.remove(QStringLiteral("vectorscope"));
  auto expected_composite_scalar_stats = canonical_photo_evidence
      .value(QStringLiteral("currentCompositeImageStats")).toMap();
  expected_composite_scalar_stats.remove(QStringLiteral("histogram"));
  expected_composite_scalar_stats.remove(QStringLiteral("vectorscope"));
  const auto captured_distribution_projection = encoded_photo_evidence
      .value(QStringLiteral("distributionEvidenceProjection")).toMap();
  const auto captured_photo_projection_round_trip =
      encoded_photo_evidence.value(QStringLiteral("baselineImageStats")).toMap() ==
          expected_baseline_scalar_stats &&
      encoded_photo_evidence.value(QStringLiteral("currentCompositeImageStats")).toMap() ==
          expected_composite_scalar_stats &&
      captured_distribution_projection.value(QStringLiteral("nativeRetained")).toBool() &&
      canonical_photo_evidence.value(QStringLiteral("baselineImageStats")).toMap()
          .contains(QStringLiteral("histogram")) &&
      canonical_photo_evidence.value(QStringLiteral("baselineImageStats")).toMap()
          .contains(QStringLiteral("vectorscope")) &&
      captured_distribution_projection.value(QStringLiteral("omitted")).toStringList().size() == 4;
  const auto expected_warning_list = raw_photo_evidence
      .value(QStringLiteral("currentCompositeWarnings"));
  const auto contains_sha256_reference = [](auto&& self, const QVariant& value) -> bool {
    if (value.metaType().id() == QMetaType::QVariantMap) {
      const auto map = value.toMap();
      if (map.contains(QStringLiteral("$h1"))) return true;
      for (auto iterator = map.cbegin(); iterator != map.cend(); ++iterator)
        if (self(self, iterator.value())) return true;
    } else if (value.metaType().id() == QMetaType::QVariantList) {
      for (const auto& entry : value.toList())
        if (self(self, entry)) return true;
    }
    return false;
  };
  const auto sha256_dictionary_round_trip = !encoded_sha256_values.isEmpty() &&
      contains_sha256_reference(contains_sha256_reference, encoded_validation_wire) &&
      sha256_references_resolved;
  const auto rejects_malformed_sha256_index = [&](const QVariant& index_value) {
    bool reference_ok = true;
    const auto decoded = resolve_prompt_sha256_references(
        QVariantMap{{QStringLiteral("$h1"), index_value}},
        encoded_sha256_values, &reference_ok);
    return !reference_ok && decoded.isNull();
  };
  const auto malformed_sha256_indexes_rejected =
      rejects_malformed_sha256_index(QStringLiteral("0")) &&
      rejects_malformed_sha256_index(0.5) &&
      rejects_malformed_sha256_index(true);
  const auto bound_id_references_round_trip =
      encoded_native_rows.value(QStringLiteral("rowIdsReference")).toBool() &&
      encoded_geometry_checks.value(QStringLiteral("rowIdsReference")).toBool() &&
      detail_rows_encoded.value(QStringLiteral("rowIdsReference")).toBool() &&
      encoded_display_policy.value(QStringLiteral("eligibleLayerIdsReference")).toBool() &&
      encoded_review.value(QStringLiteral("observedLayerOrderIdsTopFirstReference")).toBool() &&
      !encoded_native_rows.contains(QStringLiteral("rowIds")) &&
      !encoded_geometry_checks.contains(QStringLiteral("rowIds")) &&
      !detail_rows_encoded.contains(QStringLiteral("rowIds")) &&
      !encoded_display_policy.contains(QStringLiteral("eligibleLayerIds")) &&
      !encoded_review.contains(QStringLiteral("observedLayerOrderIdsTopFirst"));
  const auto warning_references_round_trip =
      raw_photo_evidence.value(QStringLiteral("introducedCompositeWarnings")) == expected_warning_list &&
      encoded_photo_evidence.value(QStringLiteral("introducedCompositeWarningsReference")).toBool() &&
      !encoded_photo_evidence.contains(QStringLiteral("introducedCompositeWarnings")) &&
      captured_validation.value(QStringLiteral("compositeWarnings")) == expected_warning_list &&
      encoded_validation.value(QStringLiteral("compositeWarningsReference")).toBool() &&
      !encoded_validation.contains(QStringLiteral("compositeWarnings"));
  if (!captured_rows_round_trip || !bound_id_references_round_trip ||
      !warning_references_round_trip || !sha256_dictionary_round_trip ||
      !captured_photo_projection_round_trip || !malformed_sha256_indexes_rejected) {
    const QVariantMap diagnostic{
        {QStringLiteral("capturedRowsRoundTrip"), captured_rows_round_trip},
        {QStringLiteral("boundIdReferencesRoundTrip"), bound_id_references_round_trip},
        {QStringLiteral("warningReferencesRoundTrip"), warning_references_round_trip},
        {QStringLiteral("sha256DictionaryRoundTrip"), sha256_dictionary_round_trip},
        {QStringLiteral("sha256ReferenceCount"), encoded_sha256_values.size()},
        {QStringLiteral("malformedSha256IndexesRejected"), malformed_sha256_indexes_rejected},
        {QStringLiteral("capturedPhotoProjectionRoundTrip"), captured_photo_projection_round_trip},
        {QStringLiteral("nativeRowsDecoded"), native_rows_decoded},
        {QStringLiteral("geometryRowsDecoded"), geometry_checks_decoded},
        {QStringLiteral("detailRowsDecoded"), detail_rows_decoded},
        {QStringLiteral("nativeRows"), decoded_native_rows.size()},
        {QStringLiteral("geometryRows"), decoded_geometry_checks.size()},
        {QStringLiteral("detailRows"), decoded_detail_rows.size()},
    };
    std::cerr << "captured App12 round-trip subchecks="
              << QJsonDocument::fromVariant(diagnostic).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
    return fail("The captured App12 prompt must round-trip graphic/native/geometry/crop fields with stable IDs, null-presence, exact image indices and hashes");
  }
  if (
      encoded_validation.value(QStringLiteral("compositionReview")).toMap()
          .value(QStringLiteral("detailProofReport")).toMap()
          .value(QStringLiteral("overviewImageIndex")).toInt() != 0 ||
      encoded_validation.value(QStringLiteral("compositionReview")).toMap()
          .value(QStringLiteral("detailProofReport")).toMap()
          .value(QStringLiteral("detailProofImageIndices")).toList() != QVariantList{1, 2, 3})
    return fail("The captured App12 prompt must round-trip graphic/native/geometry/crop fields with stable IDs, null-presence, exact image indices and hashes");

  auto reference_collision_request = captured_app12_request;
  // Exercise a real collision: the caller-owned reference keys coexist with
  // the original values that projection must preserve.
  reference_collision_request.validation_report =
      photo_editor::desktop::agent_detail::proof_critique_validation_projection(
          captured_validation);
  auto reference_collision_review = reference_collision_request.validation_report
      .value(QStringLiteral("compositionReview")).toMap();
  reference_collision_review.insert(QStringLiteral("observedLayerOrderIdsTopFirstReference"), false);
  auto reference_collision_inspection = reference_collision_review
      .value(QStringLiteral("inspection")).toMap();
  auto reference_collision_content = reference_collision_inspection
      .value(QStringLiteral("structuredContent")).toMap();
  auto reference_collision_policy = reference_collision_content
      .value(QStringLiteral("displayTextColorPolicy")).toMap();
  reference_collision_policy.insert(QStringLiteral("eligibleLayerIdsReference"), false);
  reference_collision_content.insert(QStringLiteral("displayTextColorPolicy"), reference_collision_policy);
  reference_collision_inspection.insert(QStringLiteral("structuredContent"), reference_collision_content);
  reference_collision_review.insert(QStringLiteral("inspection"), reference_collision_inspection);
  reference_collision_request.validation_report.insert(QStringLiteral("compositionReview"),
      reference_collision_review);
  reference_collision_request.validation_report.insert(QStringLiteral("compositeWarningsReference"), false);
  auto reference_collision_photo_evidence = reference_collision_request.validation_report
      .value(QStringLiteral("photographicQualityEvidence")).toMap();
  reference_collision_photo_evidence.insert(QStringLiteral("introducedCompositeWarningsReference"), false);
  reference_collision_request.validation_report.insert(
      QStringLiteral("photographicQualityEvidence"), reference_collision_photo_evidence);
  const auto reference_collision_critique = photo_editor::desktop::critique_agent_rendered_proof(
      reference_collision_request);
  const auto reference_collision_prompt = reference_collision_critique.prompt_audit
      .value(QStringLiteral("userPrompt")).toString();
  const auto reference_collision_validation = prompt_json_value_after(
      reference_collision_prompt, QStringLiteral("Validation report JSON:\n"))
      .toObject().toVariantMap();
  const auto reference_collision_encoded_review = reference_collision_validation
      .value(QStringLiteral("compositionReview")).toMap();
  const auto reference_collision_encoded_photo = reference_collision_validation
      .value(QStringLiteral("photographicQualityEvidence")).toMap();
  const auto reference_collision_encoded_policy = reference_collision_encoded_review
      .value(QStringLiteral("inspection")).toMap()
      .value(QStringLiteral("structuredContent")).toMap()
      .value(QStringLiteral("displayTextColorPolicy")).toMap();
  const bool observed_order_preserved =
      reference_collision_encoded_review.value(QStringLiteral("observedLayerOrderIdsTopFirst")) ==
          QVariant(captured_graphic_ids) &&
      !reference_collision_encoded_review.value(
          QStringLiteral("observedLayerOrderIdsTopFirstReference")).toBool();
  const bool eligible_ids_preserved =
      reference_collision_encoded_policy.value(QStringLiteral("eligibleLayerIds")) ==
          QVariant(captured_graphic_ids) &&
      !reference_collision_encoded_policy.value(QStringLiteral("eligibleLayerIdsReference")).toBool();
  const bool introduced_warnings_preserved =
      reference_collision_encoded_photo.value(QStringLiteral("introducedCompositeWarnings")) ==
          reference_collision_photo_evidence.value(QStringLiteral("introducedCompositeWarnings")) &&
      !reference_collision_encoded_photo.value(
          QStringLiteral("introducedCompositeWarningsReference")).toBool();
  const bool composite_warnings_preserved =
      reference_collision_validation.value(QStringLiteral("compositeWarnings")) ==
          reference_collision_request.validation_report.value(QStringLiteral("compositeWarnings")) &&
      !reference_collision_validation.value(QStringLiteral("compositeWarningsReference")).toBool();
  if (!reference_collision_critique.ok || !observed_order_preserved ||
      !eligible_ids_preserved || !introduced_warnings_preserved ||
      !composite_warnings_preserved) {
    const auto collision_components = reference_collision_critique.prompt_audit
        .value(QStringLiteral("promptComponents")).toMap();
    const QVariantMap diagnostic{
        {QStringLiteral("ok"), reference_collision_critique.ok},
        {QStringLiteral("errorCode"), reference_collision_critique.prompt_audit
            .value(QStringLiteral("errorCode"))},
        {QStringLiteral("errorMessage"), reference_collision_critique.error_message.left(240)},
        {QStringLiteral("requestSent"), reference_collision_critique.prompt_audit
            .value(QStringLiteral("requestSent"))},
        {QStringLiteral("aggregateTextUtf8Bytes"), collision_components
            .value(QStringLiteral("aggregateTextUtf8Bytes"))},
        {QStringLiteral("estimatedContextTokensBeforeCap"), collision_components
            .value(QStringLiteral("estimatedContextTokensBeforeCap"))},
        {QStringLiteral("contextTokenLimit"), collision_components
            .value(QStringLiteral("contextTokenLimit"))},
        {QStringLiteral("observedOrderPreserved"), observed_order_preserved},
        {QStringLiteral("eligibleIdsPreserved"), eligible_ids_preserved},
        {QStringLiteral("introducedWarningsPreserved"), introduced_warnings_preserved},
        {QStringLiteral("compositeWarningsPreserved"), composite_warnings_preserved},
    };
    std::cerr << "reference collision subchecks="
              << QJsonDocument::fromVariant(diagnostic).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
    return fail("Exact-reference compaction must preserve raw values when a caller already owns a colliding reference field");
  }

  auto primitive_mismatch_request = captured_app12_request;
  auto primitive_mismatch_layers = captured_graphic_layers;
  auto primitive_mismatch_layer = primitive_mismatch_layers.first().toMap();
  const auto primitive_mismatch_id = primitive_mismatch_layer.value(QStringLiteral("id")).toString();
  const auto actual_layer_name = primitive_mismatch_layer.value(QStringLiteral("name"));
  primitive_mismatch_layer.insert(QStringLiteral("name"), QStringLiteral("Intentionally different expected name"));
  primitive_mismatch_layer.insert(QStringLiteral("locked"), true);
  primitive_mismatch_layers[0] = primitive_mismatch_layer;
  primitive_mismatch_request.graphic_layers = primitive_mismatch_layers;
  auto primitive_mismatch_validation = primitive_mismatch_request.validation_report;
  auto primitive_mismatch_review = primitive_mismatch_validation
      .value(QStringLiteral("compositionReview")).toMap();
  primitive_mismatch_review.insert(QStringLiteral("observedGraphicLayers"), primitive_mismatch_layers);
  primitive_mismatch_validation.insert(QStringLiteral("compositionReview"), primitive_mismatch_review);
  primitive_mismatch_request.validation_report = primitive_mismatch_validation;
  const auto primitive_mismatch_critique = photo_editor::desktop::critique_agent_rendered_proof(
      primitive_mismatch_request);
  const auto primitive_mismatch_request_json = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto primitive_mismatch_messages = primitive_mismatch_request_json
      .value(QStringLiteral("messages")).toArray();
  const auto primitive_mismatch_prompt = primitive_mismatch_messages.size() >= 2
      ? primitive_mismatch_messages.at(1).toObject().value(QStringLiteral("content")).toString()
      : QString{};
  const auto primitive_mismatch_validation_prompt = prompt_json_value_after(
      primitive_mismatch_prompt, QStringLiteral("Validation report JSON:\n")).toObject().toVariantMap();
  const auto primitive_mismatch_native_table = primitive_mismatch_validation_prompt
      .value(QStringLiteral("compositionReview")).toMap()
      .value(QStringLiteral("inspection")).toMap().value(QStringLiteral("structuredContent")).toMap()
      .value(QStringLiteral("layers")).toMap();
  QVariantList primitive_mismatch_ids;
  for (const auto& layer_value : primitive_mismatch_layers)
    primitive_mismatch_ids.append(layer_value.toMap().value(QStringLiteral("id")));
  bool primitive_mismatch_decoded = false;
  const auto primitive_mismatch_rows = decode_lossless_columnar_rows(
      primitive_mismatch_native_table, &primitive_mismatch_decoded, primitive_mismatch_ids);
  bool primitive_mismatch_retained = false;
  for (const auto& row_value : primitive_mismatch_rows) {
    const auto row = row_value.toMap();
    if (row.value(QStringLiteral("id")).toString() != primitive_mismatch_id) continue;
    const auto exact_matches = row.value(QStringLiteral("exactFieldMatches")).toStringList();
    primitive_mismatch_retained = row.value(QStringLiteral("name")) == actual_layer_name &&
        row.value(QStringLiteral("locked")).metaType().id() == QMetaType::Bool &&
        !row.value(QStringLiteral("locked")).toBool() &&
        !exact_matches.contains(QStringLiteral("name")) &&
        !exact_matches.contains(QStringLiteral("locked"));
    break;
  }
  if (!primitive_mismatch_critique.ok ||
      !primitive_mismatch_critique.prompt_audit.value(QStringLiteral("requestSent")).toBool() ||
      !primitive_mismatch_decoded || !primitive_mismatch_retained)
    return fail("The critic projection must retain mismatched primitive strings and booleans instead of treating empty scalar JSON as exact duplicates");

  // Budget pressure case derived from the captured envelope: the title is a
  // 108 pt Bold face and the two secondary roles are 36 pt Regular. Keep the
  // expected graphic rows, native readback, and transformed frame metrics in
  // agreement so this exercises the post-style prompt size, not a malformed
  // metadata shortcut.
  auto styled_app12_layers = captured_graphic_layers;
  auto styled_app12_validation = captured_validation;
  auto styled_app12_review = styled_app12_validation
      .value(QStringLiteral("compositionReview")).toMap();
  auto styled_observed_layers = styled_app12_review
      .value(QStringLiteral("observedGraphicLayers")).toList();
  auto styled_geometry = styled_app12_review.value(QStringLiteral("geometryChecks")).toList();
  auto styled_typography = styled_app12_review.value(QStringLiteral("typography")).toMap();
  auto styled_hierarchy = styled_typography.value(QStringLiteral("hierarchy")).toMap();
  auto styled_family_face = styled_typography.value(QStringLiteral("familyFaceReadback")).toList();
  auto styled_inspection = styled_app12_review.value(QStringLiteral("inspection")).toMap();
  auto styled_inspection_content = styled_inspection.value(QStringLiteral("structuredContent")).toMap();
  const auto original_styled_inspection_content = styled_inspection_content;
  auto styled_inspection_layers = styled_inspection_content.value(QStringLiteral("layers")).toList();
  bool styled_app12_variant_valid = styled_app12_layers.size() == 3 &&
      styled_observed_layers.size() == 3 && styled_geometry.size() == 3 &&
      styled_inspection_layers.size() == 3 && styled_family_face.size() == 3;
  const auto update_styled_text_row = [](QVariantMap row, int size, int weight,
                                        const QString& face, int x) {
    const auto old_size = std::max(1, row.value(QStringLiteral("textTypography")).toMap()
        .value(QStringLiteral("size")).toInt());
    const auto ratio = static_cast<double>(size) / static_cast<double>(old_size);
    const auto pixel_width = static_cast<int>(std::lround(
        row.value(QStringLiteral("pixelWidth")).toDouble() * ratio));
    const auto pixel_height = static_cast<int>(std::lround(
        row.value(QStringLiteral("pixelHeight")).toDouble() * ratio));
    const auto scale = row.value(QStringLiteral("scale")).toDouble();
    const auto y = row.value(QStringLiteral("y")).toInt();
    const auto rendered_width = static_cast<int>(std::lround(pixel_width * scale));
    const auto rendered_height = static_cast<int>(std::lround(pixel_height * scale));
    auto text_typography = row.value(QStringLiteral("textTypography")).toMap();
    text_typography.insert(QStringLiteral("size"), size);
    text_typography.insert(QStringLiteral("weight"), weight);
    text_typography.insert(QStringLiteral("face"), face);
    row.insert(QStringLiteral("textTypography"), text_typography);
    row.insert(QStringLiteral("pixelWidth"), pixel_width);
    row.insert(QStringLiteral("pixelHeight"), pixel_height);
    row.insert(QStringLiteral("x"), x);
    if (row.contains(QStringLiteral("effectiveTypographySize")))
      row.insert(QStringLiteral("effectiveTypographySize"), size * scale);
    if (row.contains(QStringLiteral("renderedBounds"))) {
      row.insert(QStringLiteral("renderedBounds"), QVariantMap{
          {QStringLiteral("x"), x}, {QStringLiteral("y"), y},
          {QStringLiteral("width"), rendered_width}, {QStringLiteral("height"), rendered_height}});
    }
    if (row.contains(QStringLiteral("fontSize"))) {
      row.insert(QStringLiteral("fontSize"), size);
      row.insert(QStringLiteral("fontWeight"), weight >= 700
          ? QStringLiteral("bold") : QStringLiteral("regular"));
    }
    if (row.contains(QStringLiteral("layoutMetrics"))) {
      auto metrics = row.value(QStringLiteral("layoutMetrics")).toMap();
      metrics.insert(QStringLiteral("effectiveTypographySize"), size * scale);
      metrics.insert(QStringLiteral("typographySize"), size);
      metrics.insert(QStringLiteral("scale"), scale);
      metrics.insert(QStringLiteral("renderedBounds"), QVariantMap{
          {QStringLiteral("x"), x}, {QStringLiteral("y"), y},
          {QStringLiteral("width"), rendered_width}, {QStringLiteral("height"), rendered_height}});
      metrics.insert(QStringLiteral("unscaledBounds"), QVariantMap{
          {QStringLiteral("x"), x}, {QStringLiteral("y"), y},
          {QStringLiteral("width"), pixel_width}, {QStringLiteral("height"), pixel_height}});
      metrics.insert(QStringLiteral("rasterFrameWithinCanvas"), true);
      row.insert(QStringLiteral("layoutMetrics"), metrics);
    }
    return row;
  };
  for (qsizetype index = 0; styled_app12_variant_valid && index < styled_app12_layers.size(); ++index) {
    auto graphic = styled_app12_layers.at(index).toMap();
    const auto text = graphic.value(QStringLiteral("text")).toString();
    const auto headline = text == QStringLiteral("AFTER THE LIGHT");
    const auto size = headline ? 108 : 36;
    const auto weight = headline ? 700 : 400;
    const auto face = headline ? QStringLiteral("Bold") : QStringLiteral("Regular");
    const auto x = headline ? 40 : graphic.value(QStringLiteral("x")).toInt();
    graphic = update_styled_text_row(graphic, size, weight, face, x);
    styled_app12_layers[index] = graphic;
    styled_observed_layers[index] = graphic;

    bool inspection_row_found = false;
    for (auto& inspection_value : styled_inspection_layers) {
      auto inspection_layer = inspection_value.toMap();
      if (inspection_layer.value(QStringLiteral("id")).toString() !=
          graphic.value(QStringLiteral("id")).toString()) continue;
      inspection_layer = update_styled_text_row(inspection_layer, size, weight, face, x);
      inspection_layer.insert(QStringLiteral("fontFamily"),
          graphic.value(QStringLiteral("textTypography")).toMap().value(QStringLiteral("family")));
      inspection_row_found = true;
      inspection_value = inspection_layer;
      break;
    }
    styled_app12_variant_valid = styled_app12_variant_valid && inspection_row_found;

    for (auto& geometry_value : styled_geometry) {
      auto geometry = geometry_value.toMap();
      if (geometry.value(QStringLiteral("layerId")).toString() !=
          graphic.value(QStringLiteral("id")).toString()) continue;
      const auto metrics = graphic.value(QStringLiteral("layoutMetrics")).toMap();
      geometry.insert(QStringLiteral("effectiveTypographySize"),
          metrics.value(QStringLiteral("effectiveTypographySize")));
      geometry.insert(QStringLiteral("renderedBounds"),
          metrics.value(QStringLiteral("renderedBounds")));
      geometry_value = geometry;
      break;
    }

    const auto role = headline ? QStringLiteral("headlineEffectiveSize")
        : text.startsWith(QStringLiteral("SAMPLE"))
            ? QStringLiteral("kickerEffectiveSize") : QStringLiteral("footerEffectiveSize");
    styled_hierarchy.insert(role,
        graphic.value(QStringLiteral("effectiveTypographySize")));
    for (auto& face_value : styled_family_face) {
      auto face_row = face_value.toMap();
      if (face_row.value(QStringLiteral("layerId")).toString() !=
          graphic.value(QStringLiteral("id")).toString()) continue;
      face_row.insert(QStringLiteral("face"), face);
      face_row.insert(QStringLiteral("faceAvailable"), true);
      face_value = face_row;
      break;
    }
  }
  styled_inspection_content.insert(QStringLiteral("layers"), styled_inspection_layers);
  const auto updated_inspection_json = QString::fromUtf8(
      QJsonDocument::fromVariant(styled_inspection_content).toJson(QJsonDocument::Compact));
  auto styled_inspection_transport = styled_inspection.value(QStringLiteral("content")).toList();
  bool styled_inspection_transport_updated = false;
  for (auto& item_value : styled_inspection_transport) {
    if (!photo_editor::desktop::agent_detail::mcp_content_item_duplicates_structured_content(
            item_value, original_styled_inspection_content)) continue;
    auto item = item_value.toMap();
    item.insert(QStringLiteral("text"), updated_inspection_json);
    item_value = item;
    styled_inspection_transport_updated = true;
  }
  styled_app12_variant_valid = styled_app12_variant_valid && styled_inspection_transport_updated &&
      photo_editor::desktop::agent_detail::mcp_content_item_duplicates_structured_content(
          styled_inspection_transport.first(), styled_inspection_content);
  styled_inspection.insert(QStringLiteral("structuredContent"), styled_inspection_content);
  styled_inspection.insert(QStringLiteral("content"), styled_inspection_transport);
  styled_app12_review.insert(QStringLiteral("inspection"), styled_inspection);
  styled_app12_review.insert(QStringLiteral("observedGraphicLayers"), styled_observed_layers);
  styled_app12_review.insert(QStringLiteral("geometryChecks"), styled_geometry);
  styled_hierarchy.insert(QStringLiteral("headlineEffectiveSize"), 108);
  styled_typography.insert(QStringLiteral("hierarchy"), styled_hierarchy);
  styled_typography.insert(QStringLiteral("hierarchyPassed"), true);
  styled_typography.insert(QStringLiteral("missingFamilyFaceCopy"), QVariantList{});
  styled_typography.insert(QStringLiteral("familyFaceReadback"), styled_family_face);
  styled_app12_review.insert(QStringLiteral("typography"), styled_typography);
  styled_app12_validation.insert(QStringLiteral("compositionReview"), styled_app12_review);
  styled_app12_validation.insert(QStringLiteral("needsUserReview"), true);
  photo_editor::desktop::AgentLocalModelProofCritiqueRequest styled_app12_request = captured_app12_request;
  styled_app12_request.graphic_layers = styled_app12_layers;
  styled_app12_request.validation_report =
      photo_editor::desktop::agent_detail::proof_critique_validation_projection(
          styled_app12_validation);
  styled_app12_request.previous_attempts = QVariantList{QVariantMap{
      {QStringLiteral("attemptIndex"), 0},
      {QStringLiteral("patchHash"), QString(64, QLatin1Char('f'))},
      {QStringLiteral("recipePatch"), QVariantMap{{QStringLiteral("exposure"), 0.1}}},
      {QStringLiteral("proofPath"), captured_overview_path},
      {QStringLiteral("validationReportSha256"), QString::fromLatin1(
          QCryptographicHash::hash(QJsonDocument::fromVariant(captured_validation)
              .toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex())},
      {QStringLiteral("validationReport"), captured_native_projection},
      {QStringLiteral("modelProofCritique"), QVariantMap{
          {QStringLiteral("modelId"), QStringLiteral("ollama-app09-budget-test")},
          {QStringLiteral("ok"), true},
          {QStringLiteral("pass"), false},
          {QStringLiteral("summary"), QStringLiteral("The headline needs larger type and a clearer margin.")},
          {QStringLiteral("issues"), QStringList{QStringLiteral("Headline is not prominent enough.")}},
          {QStringLiteral("repairHints"), QStringList{QStringLiteral("Increase headline type while preserving the exact copy.")}},
          {QStringLiteral("needsUserReview"), true},
          {QStringLiteral("tasteConfidence"), 0.82},
      }},
      {QStringLiteral("qualityScore"), 0.72},
      {QStringLiteral("qualityScorePercent"), 72},
      {QStringLiteral("warnings"), QStringList{QStringLiteral("Headline hierarchy needs review.")}},
      {QStringLiteral("repairSource"), QStringLiteral("initial")},
      {QStringLiteral("needsUserReview"), true},
      {QStringLiteral("stopReasonCandidate"), QStringLiteral("repair_requested")},
  }};
  const auto styled_app12_critique = photo_editor::desktop::critique_agent_rendered_proof(
      styled_app12_request);
  const auto styled_app12_request_json = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto styled_app12_components = styled_app12_critique.prompt_audit
      .value(QStringLiteral("promptComponents")).toMap();
  const auto styled_app12_messages = styled_app12_request_json
      .value(QStringLiteral("messages")).toArray();
  const auto styled_app12_prompt = styled_app12_messages.size() >= 2
      ? styled_app12_messages.at(1).toObject().value(QStringLiteral("content")).toString()
      : QString{};
  const auto styled_previous_attempts = prompt_json_value_after(
      styled_app12_prompt, QStringLiteral("Previous attempts JSON:\n")).toArray();
  const auto styled_previous_attempt = styled_previous_attempts.size() == 1
      ? styled_previous_attempts.first().toObject().toVariantMap() : QVariantMap{};
  const auto styled_previous_binding = styled_previous_attempt
      .value(QStringLiteral("historicalValidationBinding")).toMap();
  const auto expected_prior_report_sha = QCryptographicHash::hash(
      QJsonDocument::fromVariant(captured_validation)
          .toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
  const auto styled_prior_critique = styled_previous_attempt
      .value(QStringLiteral("modelProofCritique")).toMap();
  const auto prior_attempt_summary_is_bound =
      styled_previous_attempt.value(QStringLiteral("contract")).toString() ==
          QStringLiteral("proof-critique-history-summary-v1") &&
      styled_previous_attempt.value(QStringLiteral("validationReportSha256")).toString() ==
          QString::fromLatin1(expected_prior_report_sha) &&
      styled_previous_binding.value(QStringLiteral("assetId")).toString() ==
          captured_native_projection.value(QStringLiteral("assetId")).toString() &&
      styled_previous_binding.value(QStringLiteral("runId")).toString() ==
          captured_native_projection.value(QStringLiteral("compositionReview")).toMap()
              .value(QStringLiteral("inspection")).toMap().value(QStringLiteral("runId")).toString() &&
      styled_previous_binding.value(QStringLiteral("taskId")).toString() ==
          captured_native_projection.value(QStringLiteral("photographicQualityEvidence")).toMap()
              .value(QStringLiteral("currentTaskId")).toString() &&
      styled_previous_binding.value(QStringLiteral("proofSha256")).toString() ==
          captured_native_projection.value(QStringLiteral("photographicQualityEvidence")).toMap()
              .value(QStringLiteral("currentProofSha256")).toString() &&
      !styled_previous_binding.contains(QStringLiteral("taskStateHash")) &&
      !styled_previous_binding.contains(QStringLiteral("sourceFileSha256")) &&
      !styled_previous_binding.contains(QStringLiteral("sourceRecipeSha256")) &&
      styled_prior_critique.value(QStringLiteral("pass")).toBool() == false &&
      styled_prior_critique.value(QStringLiteral("summary")).toString().contains(
          QStringLiteral("larger type")) &&
      styled_prior_critique.value(QStringLiteral("repairHints")).toStringList().size() == 1 &&
      std::abs(styled_previous_attempt.value(QStringLiteral("qualityScore")).toDouble() - 0.72) < 0.0001 &&
      !styled_previous_attempt.contains(QStringLiteral("qualityScorePercent")) &&
      !styled_previous_attempt.contains(QStringLiteral("validationReport")) &&
      styled_previous_attempt.value(QStringLiteral("fullValidationReportOmittedFromCriticContext")).toBool() &&
      !styled_app12_prompt.contains(QStringLiteral("evidenceOmittedForSize"));
  const auto styled_expected_table = prompt_json_value_after(
      styled_app12_prompt, QStringLiteral("Expected graphic text layers JSON:\n"))
      .toObject().toVariantMap();
  bool styled_expected_rows_decoded = false;
  const auto styled_expected_rows = decode_prompt_graphic_layer_rows(
      styled_expected_table, &styled_expected_rows_decoded);
  bool styled_headline_transmitted = false;
  int styled_secondary_count = 0;
  for (const auto& row_value : styled_expected_rows) {
    const auto row = row_value.toMap();
    const auto typography = row.value(QStringLiteral("textTypography")).toMap();
    if (row.value(QStringLiteral("text")).toString() == QStringLiteral("AFTER THE LIGHT")) {
      styled_headline_transmitted = typography.value(QStringLiteral("size")).toInt() == 108 &&
          typography.value(QStringLiteral("weight")).toInt() == 700 &&
          typography.value(QStringLiteral("face")).toString() == QStringLiteral("Bold");
    } else if (typography.value(QStringLiteral("size")).toInt() == 36 &&
               typography.value(QStringLiteral("weight")).toInt() == 400 &&
               typography.value(QStringLiteral("face")).toString() == QStringLiteral("Regular")) {
      ++styled_secondary_count;
    }
  }
  std::cout << "Styled App12 critic envelope: aggregateBytes="
            << styled_app12_components.value(QStringLiteral("aggregateTextUtf8Bytes")).toLongLong()
            << " estimate="
            << styled_app12_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt()
            << " historyBytes="
            << QJsonDocument(styled_previous_attempts).toJson(QJsonDocument::Compact).size()
            << " attachedImages="
            << styled_app12_components.value(QStringLiteral("attachedImageCount")).toInt()
            << " limit="
            << styled_app12_components.value(QStringLiteral("contextTokenLimit")).toInt()
            << '\n';
  if (!styled_app12_variant_valid || !styled_app12_critique.ok ||
      !styled_app12_critique.prompt_audit.value(QStringLiteral("requestSent")).toBool() ||
      !styled_app12_critique.needs_user_review ||
      styled_app12_components.value(QStringLiteral("attachedImageCount")).toInt() != 4 ||
      styled_app12_components.value(QStringLiteral("contextTokenLimit")).toInt() != 24576 ||
      styled_app12_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() > 24576 ||
      styled_app12_messages.size() < 2 ||
      !prior_attempt_summary_is_bound ||
      !styled_expected_rows_decoded || !styled_headline_transmitted || styled_secondary_count != 2 ||
      !styled_app12_prompt.contains(QStringLiteral("108")) ||
      !styled_app12_prompt.contains(QStringLiteral("Bold")) ||
      !styled_app12_prompt.contains(QStringLiteral("Regular")))
    return fail(QStringLiteral(
        "A realistic 108 pt Bold headline and 36 pt Regular bodies must fit the captured four-image critic envelope (aggregateBytes=%1 estimate=%2 limit=%3)")
        .arg(styled_app12_components.value(QStringLiteral("aggregateTextUtf8Bytes")).toLongLong())
        .arg(styled_app12_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt())
        .arg(styled_app12_components.value(QStringLiteral("contextTokenLimit")).toInt()));

  auto missing_detail_request = critique_request;
  missing_detail_request.asset.remove(QStringLiteral("additionalProofImagePaths"));
  const auto missing_detail_critique = photo_editor::desktop::critique_agent_rendered_proof(missing_detail_request);
  auto omitted_detail_request = critique_request;
  auto omitted_visual_evidence = omitted_detail_request.asset.value(QStringLiteral("visualEvidence")).toMap();
  auto omitted_details = omitted_visual_evidence.value(QStringLiteral("targetedDetailProofs")).toList();
  auto omitted_detail = omitted_details.first().toMap();
  omitted_detail.insert(QStringLiteral("omitted"), true);
  omitted_details[0] = omitted_detail;
  omitted_visual_evidence.insert(QStringLiteral("targetedDetailProofs"), omitted_details);
  omitted_detail_request.asset.insert(QStringLiteral("visualEvidence"), omitted_visual_evidence);
  const auto omitted_crop_critique = photo_editor::desktop::critique_agent_rendered_proof(omitted_detail_request);
  if (missing_detail_critique.ok || missing_detail_critique.pass ||
      omitted_crop_critique.ok || omitted_crop_critique.pass) {
    return fail("missing or explicitly omitted targeted composition crops must not permit a proof pass");
  }

  const auto install_critique_response = [&](const QJsonObject& content,
                                            bool done = true,
                                            const QString& done_reason = QStringLiteral("stop")) {
    auto response = calibrated_critique_response;
    auto message = response.value(QStringLiteral("message")).toObject();
    message.insert(QStringLiteral("content"), QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact)));
    response.insert(QStringLiteral("message"), message);
    response.insert(QStringLiteral("done"), done);
    response.insert(QStringLiteral("done_reason"), done_reason);
    response.insert(QStringLiteral("total_duration"), 123000000.0);
    response.insert(QStringLiteral("prompt_eval_duration"), 42000000.0);
    response.insert(QStringLiteral("prompt_eval_count"), 7000);
    response.insert(QStringLiteral("prompt_eval_cached_count"), 6900);
    if (fake_ollama_server_available)
      fake_ollama_server.setChunkedResponse(QStringLiteral("/api/chat"), response);
    else
      qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON", QJsonDocument(response).toJson(QJsonDocument::Compact));
  };
  auto clean_critique_content = calibrated_critique_content;
  clean_critique_content.insert(QStringLiteral("needsUserReview"), false);
  for (const auto& key : {"pass", "needsUserReview", "summary", "issues", "repairHints", "tasteConfidence"}) {
    auto malformed = clean_critique_content;
    malformed.remove(QLatin1String(key));
    install_critique_response(malformed);
    const auto result = photo_editor::desktop::critique_agent_rendered_proof(critique_request);
    if (result.ok || result.pass || !result.needs_user_review || result.error_message.isEmpty())
      return fail("a missing required critique field must fail closed before publishing pass or review state");
  }
  for (const auto& key : {"issues", "repairHints", "warnings"}) {
    auto malformed = clean_critique_content;
    malformed.insert(QLatin1String(key), QJsonArray{QJsonObject{{QStringLiteral("hiddenIssue"), true}}});
    install_critique_response(malformed);
    const auto result = photo_editor::desktop::critique_agent_rendered_proof(critique_request);
    if (result.ok || result.pass || !result.needs_user_review)
      return fail("non-string critique issues must not be silently discarded by the UI");
  }
  for (const auto& key : {"pass", "needsUserReview"}) {
    auto malformed = clean_critique_content;
    malformed.insert(QLatin1String(key), QStringLiteral("true"));
    install_critique_response(malformed);
    const auto result = photo_editor::desktop::critique_agent_rendered_proof(critique_request);
    if (result.ok || result.pass || !result.needs_user_review)
      return fail("critique booleans must not be coerced from strings");
  }
  for (const auto truncated : {false, true}) {
    install_critique_response(clean_critique_content, truncated,
        truncated ? QStringLiteral("length") : QStringLiteral("stop"));
    const auto result = photo_editor::desktop::critique_agent_rendered_proof(critique_request);
    if (result.ok || result.pass || !result.needs_user_review)
      return fail("unfinished or token-limited model output is not completed proof verification");
  }
  install_critique_response(clean_critique_content);
  auto unresolved_request = critique_request;
  unresolved_request.validation_report.insert(QStringLiteral("needsUserReview"), true);
  const auto unresolved = photo_editor::desktop::critique_agent_rendered_proof(unresolved_request);
  if (!unresolved.ok || !unresolved.needs_user_review)
    return fail("the critic cannot waive deterministic human-review requirements");
  unresolved_request.validation_report.insert(QStringLiteral("ok"), false);
  const auto failed_validation = photo_editor::desktop::critique_agent_rendered_proof(unresolved_request);
  if (!failed_validation.ok || failed_validation.pass || !failed_validation.needs_user_review)
    return fail("a model approval cannot override failed deterministic validation");
  const auto clean_critique = photo_editor::desktop::critique_agent_rendered_proof(critique_request);
  const auto timings = clean_critique.prompt_audit.value(QStringLiteral("ollamaServer")).toMap();
  if (!clean_critique.prompt_audit.value(QStringLiteral("userPrompt")).toString()
          .contains(QStringLiteral("named image region")))
    return fail("Visible region requirements must remain explicit in critic instructions");
  if (!clean_critique.ok || !clean_critique.pass || clean_critique.needs_user_review ||
      timings.value(QStringLiteral("totalDurationMs")).toInt() != 123 ||
      timings.value(QStringLiteral("promptEvalDurationMs")).toInt() != 42 ||
      timings.value(QStringLiteral("promptEvalCachedCount")).toInt() != 6900 ||
      timings.contains(QStringLiteral("evalDurationMs")))
    return fail("completed critiques must report real timing/cache evidence without inventing missing metrics");
  auto unchanged_photo_request = critique_request;
  const auto unchanged_photo_asset_id = QStringLiteral("app09-photo-asset");
  unchanged_photo_request.asset.insert(QStringLiteral("assetId"), unchanged_photo_asset_id);
  const auto unchanged_photo_evidence = QVariantMap{
      {QStringLiteral("contract"), QStringLiteral("unchanged-pre-text-composition-photographic-baseline-v1")},
      {QStringLiteral("basis"), QStringLiteral("unchanged-pre-text-composition")},
      {QStringLiteral("assetId"), unchanged_photo_asset_id},
      {QStringLiteral("baselineSourceRecipeSha256"), QString(64, QLatin1Char('a'))},
      {QStringLiteral("baselineProofSha256"), QString(64, QLatin1Char('b'))},
      {QStringLiteral("baselineTaskStateHash"), QString(64, QLatin1Char('c'))},
      {QStringLiteral("normalizedPhotoCompositionSha256"), QString(64, QLatin1Char('d'))},
      {QStringLiteral("baselineImageStats"), QVariantMap{
          {QStringLiteral("highlightClipPercent"), 1.01},
          {QStringLiteral("histogram"), QVariantMap{
              {QStringLiteral("binCount"), 16},
              {QStringLiteral("bins"), QVariantList{1, 2, 3, 4}}}},
          {QStringLiteral("vectorscope"), QVariantMap{
              {QStringLiteral("dominantHueDegreesApprox"), 195},
              {QStringLiteral("hueBinsPercent"), QVariantList{0.1, 0.2, 0.3}}}}}},
      {QStringLiteral("baselineWarnings"), QStringList{QStringLiteral("Pre-existing highlight clipping is above target.")}},
      {QStringLiteral("baselineQualityScoreReport"), QVariantMap{{QStringLiteral("score"), 0.81}}},
      {QStringLiteral("currentCompositeImageStats"), QVariantMap{
          {QStringLiteral("highlightClipPercent"), 2.4},
          {QStringLiteral("histogram"), QVariantMap{
              {QStringLiteral("binCount"), 16},
              {QStringLiteral("bins"), QVariantList{2, 3, 4, 5}}}},
          {QStringLiteral("vectorscope"), QVariantMap{
              {QStringLiteral("dominantHueDegreesApprox"), 196},
              {QStringLiteral("hueBinsPercent"), QVariantList{0.2, 0.3, 0.4}}}}}},
      {QStringLiteral("currentCompositeWarnings"), QStringList{QStringLiteral("Composite highlight pixels include authored white text.")}},
      {QStringLiteral("introducedCompositeWarnings"), QStringList{QStringLiteral("Authored text changes composite highlight clipping.")}},
      {QStringLiteral("baselineHighlightClipPercent"), 1.01},
      {QStringLiteral("compositeHighlightClipPercent"), 2.4},
      {QStringLiteral("highlightClipLimitPercent"), 0.5},
      {QStringLiteral("photoRecipeUnchanged"), true},
      {QStringLiteral("compositePhotoMetricsChangedOnlyByApprovedText"), true},
      {QStringLiteral("explicitPhotoQualityScope"), false},
  };
  unchanged_photo_request.validation_report.insert(QStringLiteral("assetId"), unchanged_photo_asset_id);
  unchanged_photo_request.validation_report.insert(QStringLiteral("photographicQualityEvidence"), unchanged_photo_evidence);
  install_critique_response(clean_critique_content);
  const auto unchanged_photo_critique =
      photo_editor::desktop::critique_agent_rendered_proof(unchanged_photo_request);
  const auto unchanged_photo_prompt = unchanged_photo_critique.prompt_audit
      .value(QStringLiteral("userPrompt")).toString();
  const auto unchanged_photo_validation = prompt_json_value_after(
      unchanged_photo_prompt, QStringLiteral("Validation report JSON:\n")).toObject().toVariantMap();
  const auto unchanged_photo_prompt_evidence = unchanged_photo_validation
      .value(QStringLiteral("photographicQualityEvidence")).toMap();
  auto unbound_photo_request = unchanged_photo_request;
  auto unbound_photo_evidence = unchanged_photo_evidence;
  unbound_photo_evidence.insert(QStringLiteral("compositePhotoMetricsChangedOnlyByApprovedText"), false);
  unbound_photo_request.validation_report.insert(
      QStringLiteral("photographicQualityEvidence"), unbound_photo_evidence);
  install_critique_response(clean_critique_content);
  const auto unbound_photo_critique =
      photo_editor::desktop::critique_agent_rendered_proof(unbound_photo_request);
  const auto photo_scope_audit = unchanged_photo_critique.prompt_audit
      .value(QStringLiteral("photoBaselineScopeAudit")).toMap();
  if (!unchanged_photo_critique.prompt_audit.value(QStringLiteral("scopedUnchangedPhotoGraphicReview")).toBool() ||
      !photo_scope_audit.value(QStringLiteral("verifiedUnchangedPhotoScope")).toBool() ||
      photo_scope_audit.value(QStringLiteral("baselineTaskStateHash")) != unchanged_photo_evidence.value(QStringLiteral("baselineTaskStateHash")) ||
      photo_scope_audit.value(QStringLiteral("normalizedPhotoCompositionSha256")) != unchanged_photo_evidence.value(QStringLiteral("normalizedPhotoCompositionSha256")) ||
      unbound_photo_critique.prompt_audit.value(QStringLiteral("scopedUnchangedPhotoGraphicReview")).toBool() ||
      unbound_photo_critique.prompt_audit.value(QStringLiteral("photoBaselineScopeAudit")).toMap()
          .value(QStringLiteral("verifiedUnchangedPhotoScope")).toBool())
    return fail("Critic scope audit must retain verified source bindings and distinguish an unbound composition");
  const auto unbound_photo_prompt = unbound_photo_critique.prompt_audit
      .value(QStringLiteral("userPrompt")).toString();
  const auto unbound_photo_validation = prompt_json_value_after(
      unbound_photo_prompt, QStringLiteral("Validation report JSON:\n")).toObject().toVariantMap();
  const auto unbound_photo_prompt_evidence = unbound_photo_validation
      .value(QStringLiteral("photographicQualityEvidence")).toMap();
  auto expected_baseline_scalars = unchanged_photo_evidence
      .value(QStringLiteral("baselineImageStats")).toMap();
  expected_baseline_scalars.remove(QStringLiteral("histogram"));
  expected_baseline_scalars.remove(QStringLiteral("vectorscope"));
  auto expected_composite_scalars = unchanged_photo_evidence
      .value(QStringLiteral("currentCompositeImageStats")).toMap();
  expected_composite_scalars.remove(QStringLiteral("histogram"));
  expected_composite_scalars.remove(QStringLiteral("vectorscope"));
  const auto projected_baseline_scalars = unchanged_photo_prompt_evidence
      .value(QStringLiteral("baselineImageStats")).toMap();
  const auto projected_composite_scalars = unchanged_photo_prompt_evidence
      .value(QStringLiteral("currentCompositeImageStats")).toMap();
  const auto distribution_projection = unchanged_photo_prompt_evidence
      .value(QStringLiteral("distributionEvidenceProjection")).toMap();
  const auto scoped_distribution_projection_is_exact =
      distribution_projection.value(QStringLiteral("contract")).toString() ==
          QStringLiteral("photo-scalar-v1") &&
      distribution_projection.value(QStringLiteral("nativeRetained")).toBool() &&
      distribution_projection.value(QStringLiteral("omitted")).toStringList() ==
          QStringList{QStringLiteral("baselineImageStats.histogram"),
              QStringLiteral("baselineImageStats.vectorscope"),
              QStringLiteral("currentCompositeImageStats.histogram"),
              QStringLiteral("currentCompositeImageStats.vectorscope")} &&
      unchanged_photo_evidence.value(QStringLiteral("baselineImageStats")).toMap()
          .contains(QStringLiteral("histogram")) &&
      unchanged_photo_evidence.value(QStringLiteral("baselineImageStats")).toMap()
          .contains(QStringLiteral("vectorscope")) &&
      projected_baseline_scalars == expected_baseline_scalars &&
      projected_composite_scalars == expected_composite_scalars;
  auto explicit_photo_scope_request = unchanged_photo_request;
  auto explicit_photo_scope_evidence = unchanged_photo_evidence;
  explicit_photo_scope_evidence.insert(QStringLiteral("explicitPhotoQualityScope"), true);
  explicit_photo_scope_request.validation_report.insert(
      QStringLiteral("photographicQualityEvidence"), explicit_photo_scope_evidence);
  install_critique_response(clean_critique_content);
  const auto explicit_photo_scope_critique =
      photo_editor::desktop::critique_agent_rendered_proof(explicit_photo_scope_request);
  const auto explicit_photo_scope_prompt = explicit_photo_scope_critique.prompt_audit
      .value(QStringLiteral("userPrompt")).toString();
  const auto explicit_photo_scope_validation = prompt_json_value_after(
      explicit_photo_scope_prompt, QStringLiteral("Validation report JSON:\n")).toObject().toVariantMap();
  const auto explicit_photo_scope_prompt_evidence = explicit_photo_scope_validation
      .value(QStringLiteral("photographicQualityEvidence")).toMap();
  const auto unchanged_photo_prompt_lower = unchanged_photo_prompt.toLower();
  const bool bound_critique_passes = unchanged_photo_critique.ok && unchanged_photo_critique.pass;
  const bool baseline_scope_guidance_present = unchanged_photo_prompt.contains(
      QStringLiteral("Verified unchanged-photo basis"));
  const bool baseline_warning_guidance_present = unchanged_photo_prompt.contains(
      QStringLiteral("baseline warnings/measurements as existing observations")) &&
      unchanged_photo_prompt.contains(QStringLiteral("Pre-existing highlight clipping is above target."));
  const bool baseline_and_composite_metrics_serialized = unchanged_photo_prompt.contains(
      QStringLiteral("baselineImageStats")) &&
      unchanged_photo_prompt.contains(QStringLiteral("baselineQualityScoreReport")) &&
      unchanged_photo_prompt.contains(QStringLiteral("currentCompositeImageStats"));
  const bool baseline_statistics_guidance_present = unchanged_photo_prompt_lower.contains(
      QStringLiteral("use baseline image statistics and thresholds"));
  const bool authored_text_photo_metric_guidance_present = unchanged_photo_prompt.contains(
      QStringLiteral("Baseline warnings alone or text pixels must not trigger photo edits/failure")) &&
      unchanged_photo_prompt.contains(QStringLiteral("full composite pixels and currentCompositeWarnings")) &&
      unchanged_photo_prompt.contains(QStringLiteral("Composite highlight pixels include authored white text."));
  const bool full_graphic_review_guidance_present = unchanged_photo_prompt.contains(
      QStringLiteral("Compare every expected copy with overview/detail pixels")) &&
      unchanged_photo_prompt.contains(QStringLiteral("safe margins")) &&
      unchanged_photo_prompt.contains(QStringLiteral("focal clearance")) &&
      unchanged_photo_prompt.contains(QStringLiteral("LOOKSY FIELD NOTES")) &&
      unchanged_photo_prompt.contains(QStringLiteral("Expected graphic text layers JSON"));
  const bool unbound_review_remains_unmodified = unbound_photo_critique.ok &&
      !unbound_photo_prompt.contains(QStringLiteral("Verified unchanged-photo basis")) &&
      unbound_photo_prompt_evidence.value(QStringLiteral("baselineImageStats")).toMap()
          .contains(QStringLiteral("histogram")) &&
      unbound_photo_prompt_evidence.value(QStringLiteral("baselineImageStats")).toMap()
          .contains(QStringLiteral("vectorscope"));
  const bool explicit_photo_scope_remains_unprojected = explicit_photo_scope_critique.ok &&
      !explicit_photo_scope_prompt.contains(QStringLiteral("Verified unchanged-photo basis")) &&
      !explicit_photo_scope_prompt_evidence.contains(QStringLiteral("distributionEvidenceProjection")) &&
      explicit_photo_scope_prompt_evidence.value(QStringLiteral("baselineImageStats")).toMap()
          .contains(QStringLiteral("histogram")) &&
      explicit_photo_scope_prompt_evidence.value(QStringLiteral("baselineImageStats")).toMap()
          .contains(QStringLiteral("vectorscope"));
  if (!bound_critique_passes || !baseline_scope_guidance_present ||
      !baseline_warning_guidance_present || !baseline_and_composite_metrics_serialized ||
      !baseline_statistics_guidance_present ||
      !authored_text_photo_metric_guidance_present || !full_graphic_review_guidance_present ||
      !unbound_review_remains_unmodified || !explicit_photo_scope_remains_unprojected ||
      !scoped_distribution_projection_is_exact) {
    const QVariantMap diagnostic{
        {QStringLiteral("boundCritiquePasses"), bound_critique_passes},
        {QStringLiteral("baselineScopeGuidancePresent"), baseline_scope_guidance_present},
        {QStringLiteral("baselineWarningGuidancePresent"), baseline_warning_guidance_present},
        {QStringLiteral("baselineAndCompositeMetricsSerialized"), baseline_and_composite_metrics_serialized},
        {QStringLiteral("baselineStatisticsGuidancePresent"), baseline_statistics_guidance_present},
        {QStringLiteral("authoredTextMetricGuidancePresent"), authored_text_photo_metric_guidance_present},
        {QStringLiteral("fullGraphicReviewGuidancePresent"), full_graphic_review_guidance_present},
        {QStringLiteral("unboundReviewRemainsUnmodified"), unbound_review_remains_unmodified},
        {QStringLiteral("explicitPhotoScopeRemainsUnprojected"), explicit_photo_scope_remains_unprojected},
        {QStringLiteral("scopedDistributionProjectionIsExact"), scoped_distribution_projection_is_exact},
        {QStringLiteral("promptBytes"), unchanged_photo_prompt.toUtf8().size()},
        {QStringLiteral("validationJsonOmittedForSize"), unchanged_photo_prompt.contains(
            QStringLiteral("evidenceOmittedForSize"))},
        {QStringLiteral("warningExcerpt"), unchanged_photo_prompt.mid(
            std::max<qsizetype>(0,
                unchanged_photo_prompt.indexOf(QStringLiteral("photographicQualityEvidence")) - 80), 700)},
    };
    std::cerr << "unchanged-photo critique subchecks="
              << QJsonDocument::fromVariant(diagnostic).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
    return fail("Only controller-bound unchanged-photo evidence may narrow photo-quality guidance; baseline observations and full graphic checks remain present");
  }
  auto omitted_evidence_request = critique_request;
  omitted_evidence_request.photographer_calibration.insert(
      QStringLiteral("qualitativeNotes"), QString(5000, QLatin1Char('x')));
  const auto omitted_evidence = photo_editor::desktop::critique_agent_rendered_proof(omitted_evidence_request);
  if (!omitted_evidence.ok || omitted_evidence.pass || !omitted_evidence.needs_user_review ||
      omitted_evidence.prompt_audit.value(QStringLiteral("metadataEvidenceComplete")).toBool())
    return fail("a model must not claim verification when required qualitative evidence was truncated");
  auto contradictory = clean_critique_content;
  contradictory.insert(QStringLiteral("issues"), QJsonArray{QStringLiteral("Skin needs inspection")});
  install_critique_response(contradictory);
  const auto contradicted = photo_editor::desktop::critique_agent_rendered_proof(critique_request);
  if (!contradicted.ok || contradicted.pass || !contradicted.needs_user_review)
    return fail("model issues cannot coexist with a waived review requirement");

  auto object_model_plan = model_plan;
  auto object_edit_intent =
      object_model_plan.value(QStringLiteral("editIntent")).toObject();
  object_edit_intent.insert(
      QStringLiteral("visualFindings"),
      QJsonArray{
          QJsonObject{
              {QStringLiteral("kind"), QStringLiteral("localized_color")},
              {QStringLiteral("target"), QStringLiteral("object")},
              {QStringLiteral("targetLabel"), QStringLiteral("red jacket")},
              {QStringLiteral("severity"), QStringLiteral("medium")},
              {QStringLiteral("observation"),
               QStringLiteral("The red jacket is darker than the surrounding scene.")},
              {QStringLiteral("confidence"), 0.90},
              {QStringLiteral("region"),
               QJsonObject{
                   {QStringLiteral("centerX"), 0.42},
                   {QStringLiteral("centerY"), 0.56},
                   {QStringLiteral("radiusX"), 0.16},
                   {QStringLiteral("radiusY"), 0.24},
               }},
          },
      });
  object_edit_intent.insert(
      QStringLiteral("localizedActions"),
      QJsonArray{
          QJsonObject{
              {QStringLiteral("target"), QStringLiteral("object")},
              {QStringLiteral("targetLabel"), QStringLiteral("red jacket")},
              {QStringLiteral("maskShape"), QStringLiteral("radial")},
              {QStringLiteral("selectionMode"), QStringLiteral("guided")},
              {QStringLiteral("selectionCandidates"),
               QJsonArray{
                   QJsonObject{
                       {QStringLiteral("label"), QStringLiteral("Left red jacket")},
                       {QStringLiteral("confidence"), 0.76},
                       {QStringLiteral("region"),
                        QJsonObject{
                            {QStringLiteral("centerX"), 0.32},
                            {QStringLiteral("centerY"), 0.56},
                            {QStringLiteral("radiusX"), 0.12},
                            {QStringLiteral("radiusY"), 0.22},
                            {QStringLiteral("feather"), 0.70},
                        }},
                   },
                   QJsonObject{
                       {QStringLiteral("label"), QStringLiteral("Right red jacket")},
                       {QStringLiteral("confidence"), 0.72},
                       {QStringLiteral("region"),
                        QJsonObject{
                            {QStringLiteral("centerX"), 0.62},
                            {QStringLiteral("centerY"), 0.54},
                            {QStringLiteral("radiusX"), 0.13},
                            {QStringLiteral("radiusY"), 0.23},
                            {QStringLiteral("feather"), 0.70},
                        }},
                   },
               }},
              {QStringLiteral("reason"),
               QStringLiteral("Lift only the visible red jacket.")},
              {QStringLiteral("confidence"), 0.89},
              {QStringLiteral("region"),
               QJsonObject{
                   {QStringLiteral("centerX"), 0.42},
                   {QStringLiteral("centerY"), 0.56},
                   {QStringLiteral("radiusX"), 0.16},
                   {QStringLiteral("radiusY"), 0.24},
                   {QStringLiteral("feather"), 0.70},
               }},
              {QStringLiteral("adjustments"),
               QJsonObject{{QStringLiteral("exposure"), 0.20}}},
          },
      });
  object_model_plan.insert(QStringLiteral("editIntent"), object_edit_intent);
  object_model_plan.insert(
      QStringLiteral("summary"),
      QStringLiteral("Lift only the visible red jacket"));
  const QJsonObject fake_object_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"), QString()},
           {QStringLiteral("thinking"),
            QString::fromUtf8(
                QJsonDocument(object_model_plan).toJson(QJsonDocument::Compact))},
       }},
  };
  if (fake_ollama_server_available) {
    fake_ollama_server.setChunkedResponse(
        QStringLiteral("/api/chat"), fake_object_response);
  } else {
    qputenv(
        "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
        QJsonDocument(fake_object_response).toJson(QJsonDocument::Compact));
  }
  const auto object_plan =
      photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto object_findings =
      object_plan.edit_intent.value(QStringLiteral("visualFindings")).toList();
  const auto object_actions =
      object_plan.edit_intent.value(QStringLiteral("localizedActions")).toList();
  const auto object_masks = object_plan.recipe_patch
                                .value(QStringLiteral("masks"))
                                .toMap()
                                .value(QStringLiteral("appendMaskStack"))
                                .toList();
  const auto object_finding = object_findings.isEmpty()
                                  ? QVariantMap{}
                                  : object_findings.first().toMap();
  const auto object_action = object_actions.isEmpty()
                                 ? QVariantMap{}
                                 : object_actions.first().toMap();
  const auto object_mask = object_masks.isEmpty()
                               ? QVariantMap{}
                               : object_masks.first().toMap();
  const auto object_mask_id =
      object_action.value(QStringLiteral("maskId")).toString();
  const auto object_candidates =
      object_mask.value(QStringLiteral("agentSelectionCandidates")).toList();
  if (!object_plan.ok ||
      object_finding.value(QStringLiteral("target")).toString() !=
          QStringLiteral("object") ||
      object_finding.value(QStringLiteral("targetLabel")).toString() !=
          QStringLiteral("red jacket") ||
      object_action.value(QStringLiteral("target")).toString() !=
          QStringLiteral("object") ||
      object_action.value(QStringLiteral("targetLabel")).toString() !=
          QStringLiteral("red jacket") ||
      object_action.value(QStringLiteral("selectionMode")).toString() !=
          QStringLiteral("guided") ||
      object_mask_id.isEmpty() ||
      object_finding.value(QStringLiteral("maskId")).toString() != object_mask_id ||
      object_mask.value(QStringLiteral("id")).toString() != object_mask_id ||
      object_mask.value(QStringLiteral("kind")).toString() !=
          QStringLiteral("radial") ||
      object_mask.value(QStringLiteral("agentSelectionMode")).toString() !=
          QStringLiteral("guided") ||
      object_mask.value(QStringLiteral("agentSelectionLabel")).toString() !=
          QStringLiteral("Red jacket") ||
      object_candidates.size() != 2 ||
      object_candidates.at(0).toMap().value(QStringLiteral("id")).toString().isEmpty() ||
      object_candidates.at(0).toMap().value(QStringLiteral("id")).toString() ==
          object_candidates.at(1).toMap().value(QStringLiteral("id")).toString() ||
      object_candidates.at(1).toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Right red jacket") ||
      object_mask.contains(QStringLiteral("agentSemanticTarget"))) {
    std::cerr << "object plan="
              << QJsonDocument::fromVariant(QVariantMap{
                     {QStringLiteral("ok"), object_plan.ok},
                     {QStringLiteral("finding"), object_finding},
                     {QStringLiteral("action"), object_action},
                     {QStringLiteral("mask"), object_mask},
                     {QStringLiteral("audit"), object_plan.prompt_audit},
                 })
                     .toJson(QJsonDocument::Compact)
                     .toStdString()
              << '\n';
    return fail("Ollama open-vocabulary findings should compile into linked, labeled, evidence-guided object-mask requests");
  }
  const auto object_validation =
      photo_editor::desktop::validate_agent_recipe_patch(
          object_plan.recipe_patch, warm_style);
  if (!object_validation.valid) {
    std::cerr << object_validation.errors.join(QStringLiteral("; ")).toStdString()
              << '\n';
    return fail("open-vocabulary guided object patches should satisfy recipe validation");
  }

  const auto flat_highlight_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/no-highlight.png"));
  QImage flat_highlight_fixture(800, 600, QImage::Format_RGB32);
  flat_highlight_fixture.fill(QColor(88, 92, 96));
  if (!flat_highlight_fixture.save(flat_highlight_path)) {
    return fail("flat fast-path rejection fixture should be writable");
  }
  auto no_evidence_request = request;
  no_evidence_request.strategy_phase = QStringLiteral("sample");
  no_evidence_request.asset = QVariantMap{
      {QStringLiteral("assetId"), QStringLiteral("no-highlight")},
      {QStringLiteral("proofImagePath"), flat_highlight_path},
  };
  no_evidence_request.user_instructions = QStringLiteral(
      "Subdue only the bright upper-right window and leave the rest unchanged.");
  const auto no_evidence_plan =
      photo_editor::desktop::plan_agent_recipe_patch(no_evidence_request);
  if (!no_evidence_plan.ok ||
      no_evidence_plan.model_id != QStringLiteral("ollama-qwen-test") ||
      no_evidence_plan.prompt_audit.value(QStringLiteral("backend")).toString() !=
          QStringLiteral("ollama") ||
      !no_evidence_plan.prompt_audit.value(QStringLiteral("plannerMode")).toString().isEmpty()) {
    return fail("fast local vision should defer to the selected model when bright-region evidence is absent");
  }

  const QJsonObject model_tool_plan{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Need live status before planning.")},
      {QStringLiteral("toolCalls"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("name"), QStringLiteral("photo.live_status")},
               {QStringLiteral("arguments"), QJsonObject{}},
               {QStringLiteral("reason"), QStringLiteral("Read current editor state.")},
           },
       }},
  };
  const QJsonObject fake_tool_plan_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(model_tool_plan).toJson(QJsonDocument::Compact))},
       }},
  };
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      QJsonDocument(fake_tool_plan_response).toJson(QJsonDocument::Compact));
  request.allow_tool_call_plan = true;
  request.available_tool_schemas =
      QVariantList{QVariantMap{
          {QStringLiteral("name"), QStringLiteral("photo.live_status")},
          {QStringLiteral("inputSchema"), QVariantMap{{QStringLiteral("type"), QStringLiteral("object")}}},
      }};
  const auto ollama_tool_plan = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (!ollama_tool_plan.ok ||
      ollama_tool_plan.decision != QStringLiteral("tool_call_plan") ||
      ollama_tool_plan.tool_call_plan.size() != 1 ||
      ollama_tool_plan.tool_call_plan.first().toMap().value(QStringLiteral("name")).toString() !=
          QStringLiteral("photo.live_status") ||
      !ollama_tool_plan.recipe_patch.isEmpty()) {
    return fail("Ollama backend should parse a structured tool_call_plan when tool calls are allowed");
  }
  const QJsonObject composition_tool_plan{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Create editable text and place the subject above it.")},
      {QStringLiteral("toolCalls"), QJsonArray{
          QJsonObject{
              {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
              {QStringLiteral("arguments"), QJsonObject{
                  {QStringLiteral("assetId"), QStringLiteral("composition-proof")},
                  {QStringLiteral("operation"), QStringLiteral("add_text")},
                  {QStringLiteral("values"), QJsonObject{
                      {QStringLiteral("text"), QStringLiteral("FLOAT")},
                      {QStringLiteral("x"), 70}, {QStringLiteral("y"), 115}}}}},
              {QStringLiteral("reason"), QStringLiteral("Create the requested editable headline.")}}}}};
  const QJsonObject fake_composition_tool_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"), QJsonObject{
          {QStringLiteral("role"), QStringLiteral("assistant")},
          {QStringLiteral("content"), QString::fromUtf8(
              QJsonDocument(composition_tool_plan).toJson(QJsonDocument::Compact))}}}};
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      QJsonDocument(fake_composition_tool_response).toJson(QJsonDocument::Compact));
  request.user_instructions = QStringLiteral(
      "Add editable FLOAT text and duplicate the existing subject selection above it. For each text layer, choose a native font family and face from one inspected fontFaces entry and set both explicitly.");
  const QJsonObject strict_typography_input_schema{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("additionalProperties"), false},
      {QStringLiteral("minProperties"), 1},
      {QStringLiteral("properties"), QJsonObject{
          {QStringLiteral("family"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          {QStringLiteral("face"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          {QStringLiteral("size"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
              {QStringLiteral("minimum"), 0.25}, {QStringLiteral("maximum"), 8192}}},
          {QStringLiteral("weight"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
          {QStringLiteral("color"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
      }},
  };
  const QJsonObject composition_values_schema{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("additionalProperties"), false},
      {QStringLiteral("properties"), QJsonObject{
          {QStringLiteral("text"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
              {QStringLiteral("description"), QStringLiteral("Exact layer copy.")}}},
          {QStringLiteral("x"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
              {QStringLiteral("description"), QStringLiteral("Integer canvas-pixel coordinate, not normalized. add_text places the text origin; transform sets the layer raster's top-left before rotation. Inspect x/y, textOriginX/Y and pixel dimensions after styling.")}}},
          {QStringLiteral("y"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
              {QStringLiteral("description"), QStringLiteral("Integer canvas-pixel coordinate, not normalized. add_text places the text origin; transform sets the layer raster's top-left before rotation. Inspect x/y, textOriginX/Y and pixel dimensions after styling.")}}},
          {QStringLiteral("scale"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
              {QStringLiteral("description"), QStringLiteral("Uniform layer-size multiplier; 1 is unscaled. The editor clamps transform scale to 0.05 through 32.")}}},
          {QStringLiteral("rotation"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
              {QStringLiteral("description"), QStringLiteral("Layer rotation in degrees about its center; 0 is unrotated. The editor clamps transform rotation to -3600 through 3600.")}}},
          {QStringLiteral("family"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          {QStringLiteral("face"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          {QStringLiteral("size"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
              {QStringLiteral("description"), QStringLiteral("Typography font size in document pixels, not points or screen pixels. Paint size is in canvas pixels. Layer scale multiplies visible size.")}}},
          {QStringLiteral("weight"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
          {QStringLiteral("color"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          {QStringLiteral("description"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
              {QStringLiteral("description"), QStringLiteral("A real optional argument named description.")}}},
          {QStringLiteral("title"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
              {QStringLiteral("title"), QStringLiteral("A schema annotation that must be omitted.")}}},
          {QStringLiteral("default"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
              {QStringLiteral("default"), QStringLiteral("schema default annotation")}}},
          {QStringLiteral("points"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
              {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                  {QStringLiteral("properties"), QJsonObject{
                      {QStringLiteral("x"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                      {QStringLiteral("y"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
                  }}, {QStringLiteral("required"), QJsonArray{QStringLiteral("x"), QStringLiteral("y")}}}}}},
          {QStringLiteral("clipSelection"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
      }},
  };
  const auto composition_operation_enum_condition = [](const QJsonArray& operations) {
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), QJsonObject{{QStringLiteral("operation"), QJsonObject{
            {QStringLiteral("enum"), operations}}}}},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("operation")}},
    };
  };
  const auto composition_operation_condition = [&composition_operation_enum_condition](const QString& operation) {
    return composition_operation_enum_condition(QJsonArray{operation});
  };
  const auto operation_values_condition = [&composition_operation_condition](
      const QString& operation, const QJsonArray& required) {
    return QJsonObject{
        {QStringLiteral("if"), composition_operation_condition(operation)},
        {QStringLiteral("then"), QJsonObject{{QStringLiteral("properties"), QJsonObject{
            {QStringLiteral("values"), QJsonObject{{QStringLiteral("required"), required}}}}}}},
    };
  };
  const QJsonObject typed_composition_input_schema{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("additionalProperties"), false},
      {QStringLiteral("properties"), QJsonObject{
          {QStringLiteral("assetId"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
              {QStringLiteral("description"), QStringLiteral("The exact active asset identifier.")}}},
          {QStringLiteral("operation"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
              {QStringLiteral("enum"), QJsonArray{QStringLiteral("add_text"), QStringLiteral("text"),
                  QStringLiteral("typography"), QStringLiteral("transform"), QStringLiteral("paint")}}}},
          {QStringLiteral("layerId"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
          {QStringLiteral("values"), composition_values_schema},
      }},
      {QStringLiteral("required"), QJsonArray{QStringLiteral("assetId"), QStringLiteral("operation"),
          QStringLiteral("values")}},
      {QStringLiteral("allOf"), QJsonArray{
          QJsonObject{
              {QStringLiteral("if"), composition_operation_enum_condition(
                  QJsonArray{QStringLiteral("text"), QStringLiteral("typography"),
                      QStringLiteral("transform"), QStringLiteral("paint")})},
              {QStringLiteral("then"), QJsonObject{
                  {QStringLiteral("required"), QJsonArray{QStringLiteral("layerId")}},
                  {QStringLiteral("properties"), QJsonObject{
                      {QStringLiteral("layerId"), QJsonObject{
                          {QStringLiteral("type"), QStringLiteral("string")},
                          {QStringLiteral("minLength"), 1},
                          {QStringLiteral("pattern"), QStringLiteral("\\S")}}}}},
              }},
          },
          operation_values_condition(QStringLiteral("add_text"),
              QJsonArray{QStringLiteral("text"), QStringLiteral("x"), QStringLiteral("y")}),
          operation_values_condition(QStringLiteral("text"), QJsonArray{QStringLiteral("text")}),
          operation_values_condition(QStringLiteral("transform"),
              QJsonArray{QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("scale"),
                  QStringLiteral("rotation")}),
          operation_values_condition(QStringLiteral("paint"), QJsonArray{QStringLiteral("points")}),
          QJsonObject{
              {QStringLiteral("if"), composition_operation_condition(QStringLiteral("typography"))},
              {QStringLiteral("then"), QJsonObject{{QStringLiteral("properties"), QJsonObject{
                  {QStringLiteral("values"), strict_typography_input_schema}}}}},
          },
      }},
  };
  request.available_tool_schemas = QVariantList{QVariantMap{
      {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
      {QStringLiteral("inputSchema"), typed_composition_input_schema.toVariantMap()}}};
  const auto saved_fake_response = qgetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  if (fake_ollama_server_available) {
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), fake_composition_tool_response);
    qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  } else {
    qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
        QJsonDocument(fake_composition_tool_response).toJson(QJsonDocument::Compact));
  }
  auto generic_typography_request = request;
  generic_typography_request.user_instructions = QStringLiteral(
      "Add editable FLOAT text and duplicate the existing subject selection above it; typography family and face are optional.");
  generic_typography_request.prior_tool_results.clear();
  const auto generic_typography_plan =
      photo_editor::desktop::plan_agent_recipe_patch(generic_typography_request);
  const auto generic_typography_payload = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto generic_typography_branch = schema_branch_for_tool_operation(
      generic_typography_payload.value(QStringLiteral("format")).toObject(),
      QStringLiteral("photo.edit_composition"), QStringLiteral("typography"));
  const auto generic_typography_required = generic_typography_branch.value(QStringLiteral("properties"))
      .toObject().value(QStringLiteral("values")).toObject().value(QStringLiteral("required")).toArray();
  if (!generic_typography_plan.ok || generic_typography_branch.isEmpty() ||
      generic_typography_required.contains(QStringLiteral("family")) ||
      generic_typography_required.contains(QStringLiteral("face"))) {
    std::cerr << "generic typography audit=" << QJsonDocument::fromVariant(QVariantMap{
        {QStringLiteral("ok"), generic_typography_plan.ok},
        {QStringLiteral("branchFound"), !generic_typography_branch.isEmpty()},
        {QStringLiteral("required"), generic_typography_required.toVariantList()},
    }).toJson(QJsonDocument::Compact).toStdString() << '\n';
    return fail("Typography family and face become generation-required only when the active task explicitly requires the pair");
  }
  if (fake_ollama_server_available) {
    auto recovery_request = generic_typography_request;
    recovery_request.model_id = QStringLiteral("ollama-app09-budget-test");
    recovery_request.require_tool_call_plan = true;
    const QJsonObject truncated_response{
        {QStringLiteral("message"), QJsonObject{
            {QStringLiteral("content"), QStringLiteral("{\"decision\":\"tool_call_plan\",\"summary\":\"unfinished")},
            {QStringLiteral("thinking"), QStringLiteral("private incomplete reasoning")}}},
        {QStringLiteral("done"), true}, {QStringLiteral("done_reason"), QStringLiteral("length")},
        {QStringLiteral("eval_count"), 1536}, {QStringLiteral("prompt_eval_count"), 12000}};
    const auto before_recovery = fake_ollama_server.requestsForPath(QStringLiteral("/api/chat")).size();
    fake_ollama_server.setResponseSequence(QStringLiteral("/api/chat"),
        {truncated_response, fake_composition_tool_response});
    const auto recovered = photo_editor::desktop::plan_agent_recipe_patch(recovery_request);
    const auto recovery_posts = fake_ollama_server.requestsForPath(QStringLiteral("/api/chat")).mid(before_recovery);
    const auto recovery_audit = recovered.prompt_audit;
    const auto response_attempts = recovery_audit.value(QStringLiteral("responseAttempts")).toList();
    if (!recovered.ok || recovered.decision != QStringLiteral("tool_call_plan") ||
        recovery_posts.size() != 2 || response_attempts.size() != 2 ||
        recovery_audit.value(QStringLiteral("outputRecoveryMaxOutputTokens")).toInt() != 3072 ||
        recovery_audit.value(QStringLiteral("outputRecoveryEstimatedContextTokens")).toInt() > 24576 ||
        response_attempts.first().toMap().value(QStringLiteral("doneReason")).toString() != QStringLiteral("length") ||
        !response_attempts.first().toMap().value(QStringLiteral("messageThinkingSha256")).toString().size() ||
        QJsonDocument::fromVariant(recovery_audit).toJson().contains("private incomplete reasoning"))
      return fail("Output-limit recovery must retain diagnostics, hide reasoning and produce one admitted plan within budget");
    auto first_payload = recovery_posts.first();
    auto second_payload = recovery_posts.last();
    auto second_options = second_payload.value(QStringLiteral("options")).toObject();
    if (first_payload.value(QStringLiteral("options")).toObject().value(QStringLiteral("num_predict")).toInt() != 1536 ||
        second_options.value(QStringLiteral("num_predict")).toInt() != 3072)
      return fail("Only the bounded recovery output allowance may increase");
    second_options.insert(QStringLiteral("num_predict"), 1536);
    second_payload.insert(QStringLiteral("options"), second_options);
    if (first_payload != second_payload)
      return fail("Output recovery must preserve every prompt, schema, image and other request option");

    auto parseable_length_response = fake_composition_tool_response;
    parseable_length_response.insert(QStringLiteral("done"), true);
    parseable_length_response.insert(QStringLiteral("done_reason"), QStringLiteral("length"));
    parseable_length_response.insert(QStringLiteral("eval_count"), 1536);
    fake_ollama_server.setResponseSequence(QStringLiteral("/api/chat"),
        {parseable_length_response, fake_composition_tool_response});
    const auto parseable_recovered = photo_editor::desktop::plan_agent_recipe_patch(recovery_request);
    if (!parseable_recovered.ok || parseable_recovered.prompt_audit.value(QStringLiteral("responseAttemptCount")).toInt() != 2)
      return fail("A length-terminated object must not be accepted even when its JSON happens to parse");

    auto unfinished_response = fake_composition_tool_response;
    unfinished_response.insert(QStringLiteral("done"), false);
    unfinished_response.insert(QStringLiteral("done_reason"), QStringLiteral("stop"));
    fake_ollama_server.setResponseSequence(QStringLiteral("/api/chat"),
        {unfinished_response, fake_composition_tool_response});
    const auto unfinished = photo_editor::desktop::plan_agent_recipe_patch(recovery_request);
    if (unfinished.ok || !unfinished.tool_call_plan.isEmpty() ||
        unfinished.prompt_audit.value(QStringLiteral("responseAttemptCount")).toInt() != 1 ||
        unfinished.prompt_audit.value(QStringLiteral("errorCode")).toString() != QStringLiteral("planning_response_unfinished"))
      return fail("An explicitly unfinished provider response must fail before plan admission without retry");

    auto truncated_again = truncated_response;
    truncated_again.insert(QStringLiteral("eval_count"), 3072);
    fake_ollama_server.setResponseSequence(QStringLiteral("/api/chat"),
        {truncated_response, truncated_again, fake_composition_tool_response});
    const auto before_terminal = fake_ollama_server.requestsForPath(QStringLiteral("/api/chat")).size();
    const auto terminal = photo_editor::desktop::plan_agent_recipe_patch(recovery_request);
    if (terminal.ok || !terminal.tool_call_plan.isEmpty() || !terminal.recipe_patch.isEmpty() ||
        terminal.prompt_audit.value(QStringLiteral("responseAttemptCount")).toInt() != 2 ||
        fake_ollama_server.requestsForPath(QStringLiteral("/api/chat")).size() - before_terminal != 2 ||
        terminal.prompt_audit.value(QStringLiteral("fallbackUsed")).toBool())
      return fail("A second truncated response must stop with no admitted tools, recipe or third request");

    auto stopped_response = truncated_response;
    stopped_response.insert(QStringLiteral("done_reason"), QStringLiteral("stop"));
    fake_ollama_server.setResponseSequence(QStringLiteral("/api/chat"),
        {stopped_response, fake_composition_tool_response});
    const auto stopped = photo_editor::desktop::plan_agent_recipe_patch(recovery_request);
    if (stopped.ok || stopped.prompt_audit.value(QStringLiteral("responseAttemptCount")).toInt() != 1)
      return fail("Ordinary malformed output must not trigger truncation recovery");

    fake_ollama_server.setResponseSequence(QStringLiteral("/api/chat"),
        {truncated_response, fake_composition_tool_response});
    const auto before_cancel = fake_ollama_server.requestsForPath(QStringLiteral("/api/chat")).size();
    recovery_request.cancellation_requested = [&]() {
      return fake_ollama_server.requestsForPath(QStringLiteral("/api/chat")).size() > before_cancel;
    };
    const auto cancelled_recovery = photo_editor::desktop::plan_agent_recipe_patch(recovery_request);
    recovery_request.cancellation_requested = {};
    if (cancelled_recovery.ok || !cancelled_recovery.tool_call_plan.isEmpty() ||
        fake_ollama_server.requestsForPath(QStringLiteral("/api/chat")).size() - before_cancel > 1)
      return fail("Cancellation after the first request must prevent another recovery request");

    // Fill only the text budget to its existing cap; do not relax the cap or
    // omit evidence to make output recovery possible.
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), fake_composition_tool_response);
    const auto budget_base = photo_editor::desktop::plan_agent_recipe_patch(recovery_request);
    const auto budget_estimate = budget_base.prompt_audit.value(QStringLiteral("promptComponents")).toMap()
        .value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt();
    recovery_request.user_instructions += QString((24576 - budget_estimate) * 4, QLatin1Char('x'));
    fake_ollama_server.setResponseSequence(QStringLiteral("/api/chat"),
        {truncated_response, fake_composition_tool_response});
    const auto no_headroom = photo_editor::desktop::plan_agent_recipe_patch(recovery_request);
    if (no_headroom.ok || !no_headroom.tool_call_plan.isEmpty() ||
        no_headroom.prompt_audit.value(QStringLiteral("responseAttemptCount")).toInt() != 1 ||
        no_headroom.prompt_audit.value(QStringLiteral("outputRecoveryBlockedReason")).toString() !=
            QStringLiteral("insufficient_context_headroom"))
      return fail("A full existing context budget must prevent output recovery without dropping evidence");
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), fake_composition_tool_response);
  }
  // Large real composition replies must not erase the ID/error trail used by
  // the next planning round. Each UI reply contains repeated layer thumbnails.
  const auto saved_prior_tool_results = request.prior_tool_results;
  const QVariantMap approved_snapshot_envelope{
      {QStringLiteral("kind"), QStringLiteral("approved-task-snapshot")},
      {QStringLiteral("authority"), QStringLiteral("approved-brief-and-deliverables")},
      {QStringLiteral("snapshot"), QVariantMap{
          {QStringLiteral("approvedInstructions"), QStringLiteral("Create the approved editable travel cover using three text layers. Choose a native font family and face from one inspected fontFaces entry and set both explicitly.")},
          {QStringLiteral("exactCopy"), QStringList{QStringLiteral("AFTER THE LIGHT"), QStringLiteral("SAMPLE / FIELD NOTES 01"), QStringLiteral("A STUDY IN BLUE HOUR")}},
          {QStringLiteral("deliverables"), QStringList{QStringLiteral("One editable cover composition on design-sample.")}},
      {QStringLiteral("targetIds"), QStringList{QStringLiteral("design-sample")}},
          {QStringLiteral("taskScopedCapabilities"), QStringList{QStringLiteral("graphics"), QStringLiteral("layers_luts")}},
      {QStringLiteral("plan"), QVariantMap{{QStringLiteral("layout"), QStringLiteral("open sky headline with left-aligned kicker and footer")}}},
      }},
  };
  QVariantList oversized_metadata;
  for (int i = 0; i < 64; ++i) oversized_metadata.append(QString(1000, QLatin1Char('x')));
  const QVariantMap returned_layer{{QStringLiteral("id"), QStringLiteral("returned-text-layer")},
      {QStringLiteral("kind"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("FLOAT")},
      {QStringLiteral("textTypography"), QVariantMap{{QStringLiteral("size"), 88}}},
      {QStringLiteral("thumbnailDiagnostics"), oversized_metadata}};
  for (int i = 0; i < 20; ++i) {
    const QVariantMap content{{QStringLiteral("layers"), QVariantList{returned_layer}},
        {QStringLiteral("masks"), QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("returned-mask-set")}}}},
        {QStringLiteral("createdLayerId"), QStringLiteral("created-text-explicit")},
        {QStringLiteral("paintBusy"), true}, {QStringLiteral("paintError"), QStringLiteral("queued paint status")},
        {QStringLiteral("width"), 1280}, {QStringLiteral("height"), 720}};
    const QVariantMap envelope{
        {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
        {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("assetId"), QStringLiteral("composition")},
            {QStringLiteral("operation"), QStringLiteral("add_text")}}},
        {QStringLiteral("result"), QVariantMap{{QStringLiteral("ok"), true},
            {QStringLiteral("structuredContent"), content},
            {QStringLiteral("toolResult"), QVariantMap{{QStringLiteral("structuredContent"), content}}}}}};
    if (i % 2 == 0) request.prior_tool_results.append(envelope);
    else request.prior_tool_results.append(QVariantMap{
        {QStringLiteral("toolName"), QStringLiteral("photo.edit_composition")},
        {QStringLiteral("arguments"), envelope.value(QStringLiteral("arguments"))},
        {QStringLiteral("ok"), true}, {QStringLiteral("structuredContent"), content},
        {QStringLiteral("content"), QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("text"), QString::fromUtf8(QJsonDocument::fromVariant(content).toJson(QJsonDocument::Compact))}}}}});
  }
  request.prior_tool_results.append(QVariantMap{
      {QStringLiteral("toolName"), QStringLiteral("photo.edit_composition")},
      {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("operation"), QStringLiteral("copy_selection")}}},
      {QStringLiteral("ok"), false},
      {QStringLiteral("error"), QStringLiteral("Selection does not exist.")}});
  request.prior_tool_results.append(QVariantMap{
      {QStringLiteral("toolName"), QStringLiteral("photo.edit_composition")},
      {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("operation"), QStringLiteral("opacity")},
          {QStringLiteral("layerId"), QStringLiteral("returned-text-layer")}}},
      {QStringLiteral("ok"), true}, {QStringLiteral("structuredContent"), QVariantMap{
          {QStringLiteral("changed"), false}, {QStringLiteral("alreadySatisfied"), true}}}});
  const auto composition_tool_result = plan_with_app09_budget_model(request);
  request.prior_tool_results = saved_prior_tool_results;
  const auto composition_prompt = composition_tool_result.prompt_audit.value(QStringLiteral("userPrompt")).toString();
  const auto history_label = QStringLiteral("Prior MCP tool results JSON:\n");
  const auto history_start = composition_prompt.lastIndexOf(history_label);
  const auto history_bytes = composition_prompt.mid(history_start + history_label.size()).toUtf8();
  const auto compact_history = QJsonDocument::fromJson(history_bytes);
  const auto composition_tool_components = composition_tool_result.prompt_audit
      .value(QStringLiteral("promptComponents")).toMap();
  if (!composition_tool_result.ok ||
      composition_tool_components.value(QStringLiteral("contextTokenLimit")).toInt() != 24576 ||
      composition_tool_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() > 24576 ||
      history_start < 0 || !compact_history.isArray() || compact_history.array().size() != 22 ||
      !history_bytes.contains("returned-text-layer") || !history_bytes.contains("returned-mask-set") ||
      !history_bytes.contains("created-text-explicit") || !history_bytes.contains("queued paint status") ||
      !history_bytes.contains("Selection does not exist.") || history_bytes.contains("thumbnailDiagnostics") ||
      history_bytes.contains("evidenceOmittedForSize") || !history_bytes.contains("alreadySatisfied") || history_bytes.size() > 16000)
    return fail(qPrintable(QStringLiteral(
        "Oversized composition results must preserve returned IDs and execution errors in a bounded prompt; context=%1")
        .arg(context_budget_summary(composition_tool_result))));
  for (int i = 0; i < 20; ++i) {
    const auto entry = compact_history.array().at(i).toObject();
    if (entry.value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
        entry.value(QStringLiteral("arguments")).toObject().value(QStringLiteral("operation")).toString() != QStringLiteral("add_text") ||
        entry.value(QStringLiteral("result")).toObject().value(QStringLiteral("structuredContent")).toObject()
            .value(QStringLiteral("createdLayerId")).toString() != QStringLiteral("created-text-explicit"))
      return fail("Live and enveloped composition history must retain operation identity and authoritative returned IDs");
  }

  QVariantMap large_text_typography;
  for (int i = 0; i < 64; ++i)
    large_text_typography.insert(QStringLiteral("proof-detail-%1").arg(i), QString(1200, QLatin1Char('p')));
  const QVariantMap large_proof_layer{{QStringLiteral("id"), QStringLiteral("proof-layer")},
      {QStringLiteral("kind"), QStringLiteral("text")},
      {QStringLiteral("textTypography"), large_text_typography}};
  request.prior_tool_results = QVariantList{
      QVariantMap{{QStringLiteral("toolName"), QStringLiteral("photo.inspect_composition")},
          {QStringLiteral("callId"), QStringLiteral("call-inspect-large-proof")},
          {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("assetId"), QStringLiteral("composition")}}},
          {QStringLiteral("ok"), true},
          {QStringLiteral("structuredContent"), QVariantMap{{QStringLiteral("layers"), QVariantList{large_proof_layer}}}}},
      QVariantMap{{QStringLiteral("toolName"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("callId"), QStringLiteral("call-original-add-text-17")},
          {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("assetId"), QStringLiteral("composition")},
              {QStringLiteral("operation"), QStringLiteral("add_text")},
              {QStringLiteral("text"), QStringLiteral("LIVE UP CLOSE")}}},
          {QStringLiteral("ok"), true},
          {QStringLiteral("structuredContent"), QVariantMap{{QStringLiteral("changed"), true},
              {QStringLiteral("createdLayerId"), QStringLiteral("original-created-text")}}}},
      QVariantMap{{QStringLiteral("toolName"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("callId"), QStringLiteral("call-replay-add-text-18")},
          {QStringLiteral("originalCallId"), QStringLiteral("call-original-add-text-17")},
          {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("assetId"), QStringLiteral("composition")},
              {QStringLiteral("operation"), QStringLiteral("add_text")},
              {QStringLiteral("text"), QStringLiteral("LIVE UP CLOSE")}}},
          {QStringLiteral("ok"), false},
          {QStringLiteral("error"), QStringLiteral("This exact mutation already completed. Continue from its returned IDs and current inspection without replaying it.")},
          {QStringLiteral("structuredContent"), QVariantMap{
              {QStringLiteral("recoverableError"), true}, {QStringLiteral("mutationNotApplied"), true},
              {QStringLiteral("replayPrevented"), true}, {QStringLiteral("alreadyCompleted"), true},
              {QStringLiteral("priorReturnedIds"), QVariantMap{{QStringLiteral("createdLayerId"), QStringLiteral("original-created-text")}}}}}},
  };
  request.prior_tool_results.append(approved_snapshot_envelope);
  const auto receipt_compaction_plan = plan_with_app09_budget_model(request);
  request.prior_tool_results = saved_prior_tool_results;
  const auto receipt_prompt = receipt_compaction_plan.prompt_audit.value(QStringLiteral("userPrompt")).toString();
  const auto receipt_history_start = receipt_prompt.lastIndexOf(history_label);
  const auto receipt_history_bytes = receipt_prompt.mid(receipt_history_start + history_label.size()).toUtf8();
  const auto receipt_history = QJsonDocument::fromJson(receipt_history_bytes);
  const auto receipt_history_object = receipt_history.object();
  const auto receipt_approved_snapshots = receipt_history_object
      .value(QStringLiteral("approvedTaskSnapshots")).toArray();
  const auto receipt_approved_envelope = receipt_approved_snapshots.isEmpty()
      ? QJsonObject{} : receipt_approved_snapshots.first().toObject();
  const auto receipt_approved_snapshot = receipt_approved_envelope
      .value(QStringLiteral("snapshot")).toObject();
  const auto receipt_json = QString::fromUtf8(receipt_history_bytes);
  const auto receipt_prompt_components = receipt_compaction_plan.prompt_audit
      .value(QStringLiteral("promptComponents")).toMap();
  if (!receipt_compaction_plan.ok ||
      receipt_prompt_components.value(QStringLiteral("contextTokenLimit")).toInt() != 24576 ||
      receipt_prompt_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() > 24576 ||
      receipt_history_start < 0 || !receipt_history.isObject() ||
      receipt_history_object.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-tool-history-with-protected-mutation-receipts-v1") ||
      receipt_history_object.value(QStringLiteral("largePriorToolDetailsOmitted")).toBool() != true ||
      !receipt_json.contains(QStringLiteral("This exact mutation already completed.")) ||
      !receipt_json.contains(QStringLiteral("call-original-add-text-17")) ||
      !receipt_json.contains(QStringLiteral("original-created-text")) ||
      !receipt_json.contains(QStringLiteral("replayPrevented")) ||
      !receipt_json.contains(QStringLiteral("alreadyCompleted")) ||
      receipt_approved_snapshots.size() != 1 ||
      receipt_approved_envelope.value(QStringLiteral("authority")).toString() !=
          QStringLiteral("approved-brief-and-deliverables") ||
      receipt_approved_snapshot.value(QStringLiteral("approvedInstructions")).toString() !=
          QStringLiteral("Create the approved editable travel cover using three text layers. Choose a native font family and face from one inspected fontFaces entry and set both explicitly.") ||
      !receipt_approved_snapshot.value(QStringLiteral("exactCopy")).toArray()
           .contains(QStringLiteral("AFTER THE LIGHT")) ||
      receipt_json.contains(QStringLiteral("proof-detail-0")) || receipt_history_bytes.size() > 32000)
    return fail(qPrintable(QStringLiteral(
        "Large proof details must compact away while preserving completed-mutation receipts and the exact approved task scope; context=%1")
        .arg(context_budget_summary(receipt_compaction_plan))));

  const QVariantList budget_font_inventory{
      QVariantMap{{QStringLiteral("family"), QStringLiteral("Avenir Next")},
          {QStringLiteral("faces"), QStringList{QStringLiteral("Regular"), QStringLiteral("Ultra Light")}}},
      QVariantMap{{QStringLiteral("family"), QStringLiteral("Baskerville")},
          {QStringLiteral("faces"), QStringList{QStringLiteral("Regular"), QStringLiteral("Bold")}}},
  };
  const QVariantMap budget_color_policy{
      {QStringLiteral("contract"), QStringLiteral("display-referred-text-suffix-v1")},
      {QStringLiteral("eligibleLayerCount"), 3},
      {QStringLiteral("renderStage"), QStringLiteral("PhotographicOutput")},
      {QStringLiteral("fallbackReason"), QStringLiteral("none")},
  };
  const QVariantMap budget_headline_layer{
      {QStringLiteral("id"), QStringLiteral("cover-headline")},
      {QStringLiteral("kind"), QStringLiteral("text")},
      {QStringLiteral("name"), QStringLiteral("Headline")},
      {QStringLiteral("text"), QStringLiteral("AFTER THE LIGHT")},
      {QStringLiteral("textTypography"), QVariantMap{
          {QStringLiteral("family"), QStringLiteral("Avenir Next")},
          {QStringLiteral("face"), QStringLiteral("Ultra Light")},
          {QStringLiteral("size"), 88}, {QStringLiteral("weight"), 300},
          {QStringLiteral("color"), QStringLiteral("#f2ebdc")}}},
      {QStringLiteral("renderColorDomain"), QStringLiteral("display-referred-srgb")},
      {QStringLiteral("displayReferredTextEligibility"), QStringLiteral("eligible")},
      {QStringLiteral("displayReferredTextPolicyVersion"), QStringLiteral("display-referred-text-suffix-v1")},
      {QStringLiteral("x"), 72}, {QStringLiteral("y"), 190},
  };
  const QVariantMap budget_kicker_layer{
      {QStringLiteral("id"), QStringLiteral("cover-kicker")},
      {QStringLiteral("kind"), QStringLiteral("text")},
      {QStringLiteral("name"), QStringLiteral("Kicker")},
      {QStringLiteral("text"), QStringLiteral("SAMPLE / FIELD NOTES 01")},
      {QStringLiteral("textTypography"), QVariantMap{
          {QStringLiteral("family"), QStringLiteral("Baskerville")},
          {QStringLiteral("face"), QStringLiteral("Regular")},
          {QStringLiteral("size"), 28}}},
  };
  const QVariantMap budget_footer_layer{
      {QStringLiteral("id"), QStringLiteral("cover-footer")},
      {QStringLiteral("kind"), QStringLiteral("text")},
      {QStringLiteral("name"), QStringLiteral("Footer")},
      {QStringLiteral("text"), QStringLiteral("A STUDY IN BLUE HOUR")},
      {QStringLiteral("textTypography"), QVariantMap{
          {QStringLiteral("family"), QStringLiteral("Avenir Next")},
          {QStringLiteral("face"), QStringLiteral("Regular")},
          {QStringLiteral("size"), 26}}},
  };
  const QVariantList budget_three_layers{
      budget_footer_layer, budget_kicker_layer, budget_headline_layer};
  const auto budget_composition_content = [&budget_font_inventory, &budget_color_policy](
      const QVariantList& layers, const QString& created_id = QString{}) {
    QVariantMap content{
        {QStringLiteral("assetId"), QStringLiteral("design-sample")},
        {QStringLiteral("width"), 1600}, {QStringLiteral("height"), 2400},
        {QStringLiteral("order"), QStringList{QStringLiteral("cover-footer"),
            QStringLiteral("cover-kicker"), QStringLiteral("cover-headline")}},
        {QStringLiteral("layers"), layers},
        {QStringLiteral("fontFaces"), budget_font_inventory},
        {QStringLiteral("displayTextColorPolicy"), budget_color_policy},
        {QStringLiteral("changed"), true},
    };
    if (!created_id.isEmpty()) content.insert(QStringLiteral("createdLayerId"), created_id);
    return content;
  };
  const auto budget_tool_receipt = [](const QString& name, const QVariantMap& arguments,
                                       const QVariantMap& content) {
    return QVariantMap{
        {QStringLiteral("name"), name},
        {QStringLiteral("arguments"), arguments},
        {QStringLiteral("result"), QVariantMap{{QStringLiteral("ok"), true},
            {QStringLiteral("structuredContent"), content}}},
    };
  };
  const QVariantMap budget_inspection_arguments{{QStringLiteral("assetId"), QStringLiteral("design-sample")}};
  const QVariantMap budget_initial_content{{QStringLiteral("assetId"), QStringLiteral("design-sample")},
      {QStringLiteral("width"), 1600}, {QStringLiteral("height"), 2400},
      {QStringLiteral("layers"), QVariantList{}},
      {QStringLiteral("fontFaces"), budget_font_inventory}};
  QVariantList oversized_prompt_proof_details;
  for (int index = 0; index < 18; ++index)
    oversized_prompt_proof_details.append(QString(4000, QLatin1Char('p')));
  const QVariantMap oversized_prompt_proof{
      {QStringLiteral("toolName"), QStringLiteral("photo.validate_rendered_proof")},
      {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("assetId"), QStringLiteral("design-sample")}}},
      {QStringLiteral("ok"), true},
      {QStringLiteral("structuredContent"), QVariantMap{
          {QStringLiteral("pass"), true},
          {QStringLiteral("summary"), QStringLiteral("Retained proof summary for the current cover.")},
          {QStringLiteral("imageStats"), QVariantMap{
              {QStringLiteral("histogram"), QVariantMap{{QStringLiteral("highlights"), 0.18}}},
              {QStringLiteral("vectorscope"), QVariantMap{{QStringLiteral("neutralCenter"), true}}}}},
          {QStringLiteral("rawProofDetail"), oversized_prompt_proof_details}}}};
  auto stale_scope_snapshot = approved_snapshot_envelope;
  auto stale_scope_value = stale_scope_snapshot.value(QStringLiteral("snapshot")).toMap();
  stale_scope_value.insert(QStringLiteral("approvedInstructions"),
      QStringLiteral("STALE APPROVED SCOPE MUST NOT SURVIVE"));
  stale_scope_snapshot.insert(QStringLiteral("snapshot"), stale_scope_value);
  auto large_current_scope_snapshot = approved_snapshot_envelope;
  auto large_current_scope_value = large_current_scope_snapshot.value(QStringLiteral("snapshot")).toMap();
  const auto large_current_approved_instructions =
      QStringLiteral("Create the approved editable travel cover using three text layers. ") +
      QString(9000, QLatin1Char('a')) +
      QStringLiteral("UNIQUE MID-BRIEF CONSTRAINT: preserve the approved open-sky headline layout.") +
      QString(9000, QLatin1Char('b'));
  large_current_scope_value.insert(QStringLiteral("approvedInstructions"),
      large_current_approved_instructions);
  large_current_scope_snapshot.insert(QStringLiteral("snapshot"), large_current_scope_value);
  const QVariantMap stale_composition_assessment{
      {QStringLiteral("kind"), QStringLiteral("current-composition-assessment")},
      {QStringLiteral("contract"), QStringLiteral("agent-current-composition-assessment-v1")},
      {QStringLiteral("assetId"), QStringLiteral("old-asset")},
      {QStringLiteral("instruction"), QStringLiteral("STALE ASSESSMENT MUST NOT SURVIVE")},
  };
  const auto current_assessment_instruction = QStringLiteral(
      "Use the current rendered review and native readback to repair the remaining approved copy and hierarchy. ") +
      QString(4200, QLatin1Char('r'));
  const QVariantMap current_composition_assessment{
      {QStringLiteral("kind"), QStringLiteral("current-composition-assessment")},
      {QStringLiteral("contract"), QStringLiteral("agent-current-composition-assessment-v1")},
      {QStringLiteral("assetId"), QStringLiteral("design-sample")},
      {QStringLiteral("batchRound"), 2},
      {QStringLiteral("sourceProofSha256"), QStringLiteral("proof-sha-current-17")},
      {QStringLiteral("sourceRecipeSha256"), QStringLiteral("recipe-sha-current-23")},
      {QStringLiteral("textLayerIds"), QStringList{QStringLiteral("cover-headline"),
          QStringLiteral("cover-kicker"), QStringLiteral("cover-footer")}},
      {QStringLiteral("nativeReview"), QVariantMap{
          {QStringLiteral("ok"), true}, {QStringLiteral("complete"), false},
          {QStringLiteral("missingExpectedCopy"), QStringList{QStringLiteral("A STUDY IN BLUE HOUR")}},
          {QStringLiteral("duplicateExpectedCopy"), QStringList{}},
          {QStringLiteral("typography"), QVariantMap{
              {QStringLiteral("explicitFamilyFaceRequired"), true},
              {QStringLiteral("missingFamilyFaceCopy"), QStringList{}},
              {QStringLiteral("familyFaceReadback"), QVariantList{QVariantMap{
                  {QStringLiteral("text"), QStringLiteral("AFTER THE LIGHT")},
                  {QStringLiteral("layerId"), QStringLiteral("cover-headline")},
                  {QStringLiteral("family"), QStringLiteral("Avenir Next")},
                  {QStringLiteral("face"), QStringLiteral("Ultra Light")},
                  {QStringLiteral("familyAvailable"), true},
                  {QStringLiteral("faceAvailable"), true}}}},
              {QStringLiteral("requiredHierarchyAvailable"), true},
              {QStringLiteral("hierarchyAvailable"), true},
              {QStringLiteral("hierarchyPassed"), true},
              {QStringLiteral("minimumRatio"), 2.0},
              {QStringLiteral("layoutFitGuidance"), fit_guidance},
              {QStringLiteral("hierarchy"), QVariantMap{
                  {QStringLiteral("headlineEffectiveSize"), 88.0},
                  {QStringLiteral("kickerEffectiveSize"), 28.0},
                  {QStringLiteral("footerEffectiveSize"), 26.0}}}}},
          {QStringLiteral("warnings"), QStringList{QStringLiteral("Post-mutation render review is current.")}}}},
      {QStringLiteral("modelCritique"), QVariantMap{
          {QStringLiteral("available"), true}, {QStringLiteral("ok"), true},
          {QStringLiteral("pass"), false}, {QStringLiteral("needsUserReview"), false},
          {QStringLiteral("issues"), QStringList{QStringLiteral("Footer copy is still absent.")}},
          {QStringLiteral("repairHints"), QStringList{QStringLiteral("Add the exact missing footer copy.")}}}},
      {QStringLiteral("revisionBaseline"), QVariantMap{
          {QStringLiteral("source"), QStringLiteral("prior-accepted-native-inspect")},
          {QStringLiteral("assetId"), QStringLiteral("design-sample")},
          {QStringLiteral("sourceProofSha256"), QStringLiteral("proof-sha-before-revision")},
          {QStringLiteral("sourceRecipeSha256"), QStringLiteral("recipe-sha-before-revision")},
          {QStringLiteral("layerIdsTopFirst"), QStringList{QStringLiteral("cover-headline"),
              QStringLiteral("cover-kicker"), QStringLiteral("cover-footer")}},
          {QStringLiteral("textLayers"), QVariantList{QVariantMap{
              {QStringLiteral("id"), QStringLiteral("cover-headline")},
              {QStringLiteral("text"), QStringLiteral("AFTER THE LIGHT")},
              {QStringLiteral("x"), 72.0}, {QStringLiteral("y"), 190.0},
              {QStringLiteral("scale"), 1.0}, {QStringLiteral("rotation"), 0.0},
              {QStringLiteral("layerIndex"), 0},
              {QStringLiteral("textTypography"), QVariantMap{
                  {QStringLiteral("family"), QStringLiteral("Avenir Next")},
                  {QStringLiteral("face"), QStringLiteral("Ultra Light")},
                  {QStringLiteral("size"), 88.0}, {QStringLiteral("weight"), 300}}}}}}}},
      {QStringLiteral("geometryChecks"), QVariantList{QVariantMap{
          {QStringLiteral("layerId"), QStringLiteral("cover-headline")},
          {QStringLiteral("available"), true},
          {QStringLiteral("rasterFrameWithinCanvas"), true},
          {QStringLiteral("boundsBasis"), QStringLiteral("display-referred text frame")},
          {QStringLiteral("transformAvailable"), true},
          {QStringLiteral("layerScale"), 1.0}, {QStringLiteral("layerRotation"), 0.0},
          {QStringLiteral("effectiveTypographySize"), 88.0},
          {QStringLiteral("canvasWidth"), 1600}, {QStringLiteral("canvasHeight"), 2400},
          {QStringLiteral("renderedBounds"), QVariantMap{{QStringLiteral("x"), 72.0},
              {QStringLiteral("y"), 190.0}, {QStringLiteral("w"), 820.0},
              {QStringLiteral("h"), 112.0}, {QStringLiteral("width"), 820.0},
              {QStringLiteral("height"), 112.0},
              {QStringLiteral("untrustedExtra"), true}}}}}},
      {QStringLiteral("requireProductivePlan"), true},
      {QStringLiteral("instruction"), current_assessment_instruction},
  };
  QVariantList five_receipt_history{
      budget_tool_receipt(QStringLiteral("photo.inspect_composition"), budget_inspection_arguments,
          budget_initial_content),
      budget_tool_receipt(QStringLiteral("photo.inspect_composition"), budget_inspection_arguments,
          budget_initial_content),
      budget_tool_receipt(QStringLiteral("photo.edit_composition"),
          QVariantMap{{QStringLiteral("assetId"), QStringLiteral("design-sample")},
              {QStringLiteral("operation"), QStringLiteral("add_text")},
              {QStringLiteral("values"), QVariantMap{{QStringLiteral("text"), QStringLiteral("AFTER THE LIGHT")}}}},
          budget_composition_content(QVariantList{budget_headline_layer}, QStringLiteral("cover-headline"))),
      budget_tool_receipt(QStringLiteral("photo.edit_composition"),
          QVariantMap{{QStringLiteral("assetId"), QStringLiteral("design-sample")},
              {QStringLiteral("operation"), QStringLiteral("add_text")},
              {QStringLiteral("values"), QVariantMap{{QStringLiteral("text"), QStringLiteral("SAMPLE / FIELD NOTES 01")}}}},
          budget_composition_content(QVariantList{budget_kicker_layer, budget_headline_layer},
              QStringLiteral("cover-kicker"))),
      budget_tool_receipt(QStringLiteral("photo.edit_composition"),
          QVariantMap{{QStringLiteral("assetId"), QStringLiteral("design-sample")},
              {QStringLiteral("operation"), QStringLiteral("typography")},
              {QStringLiteral("layerId"), QStringLiteral("cover-headline")},
              {QStringLiteral("values"), QVariantMap{{QStringLiteral("family"), QStringLiteral("Avenir Next")},
                  {QStringLiteral("face"), QStringLiteral("Ultra Light")}, {QStringLiteral("size"), 88}}}},
          budget_composition_content(budget_three_layers)),
      QVariantMap{{QStringLiteral("kind"), QStringLiteral("current-proof-validation")},
          {QStringLiteral("assetId"), QStringLiteral("design-sample")},
          {QStringLiteral("pass"), false},
          {QStringLiteral("issues"), QStringList{QStringLiteral("Historical pre-mutation proof issue.")}}},
      oversized_prompt_proof,
      stale_scope_snapshot,
      large_current_scope_snapshot,
      stale_composition_assessment,
      current_composition_assessment,
  };
  auto budget_prior_history = request.prior_tool_results;
  auto budget_asset = request.asset;
  request.prior_tool_results = five_receipt_history;
  request.asset.insert(QStringLiteral("assetId"), QStringLiteral("design-sample"));
  const auto saved_budget_fake_response = qgetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  auto budget_tool_plan = composition_tool_plan;
  auto budget_tool_call = budget_tool_plan.value(QStringLiteral("toolCalls")).toArray().first().toObject();
  auto budget_arguments = budget_tool_call.value(QStringLiteral("arguments")).toObject();
  budget_arguments.insert(QStringLiteral("assetId"), QStringLiteral("design-sample"));
  budget_tool_call.insert(QStringLiteral("arguments"), budget_arguments);
  budget_tool_plan.insert(QStringLiteral("toolCalls"), QJsonArray{budget_tool_call});
  auto budget_response = fake_composition_tool_response;
  auto budget_message = budget_response.value(QStringLiteral("message")).toObject();
  budget_message.insert(QStringLiteral("content"), QString::fromUtf8(
      QJsonDocument(budget_tool_plan).toJson(QJsonDocument::Compact)));
  budget_response.insert(QStringLiteral("message"), budget_message);
  if (fake_ollama_server_available)
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), budget_response);
  else
    qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
        QJsonDocument(budget_response).toJson(QJsonDocument::Compact));
  const auto prompt_budget_plan = plan_with_app09_budget_model(request);
  if (fake_ollama_server_available)
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), fake_composition_tool_response);
  else if (!saved_budget_fake_response.isEmpty())
    qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON", saved_budget_fake_response);
  else
    qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  request.prior_tool_results = budget_prior_history;
  request.asset = budget_asset;
  const auto prompt_budget_prompt = prompt_budget_plan.prompt_audit
      .value(QStringLiteral("userPrompt")).toString();
  const auto prompt_budget_history_start = prompt_budget_prompt.lastIndexOf(history_label);
  const auto prompt_budget_history_bytes = prompt_budget_prompt
      .mid(prompt_budget_history_start + history_label.size()).toUtf8();
  const auto prompt_budget_history = QJsonDocument::fromJson(prompt_budget_history_bytes).object();
  const auto prompt_budget_state = prompt_budget_history.value(QStringLiteral("currentComposition")).toObject()
      .value(QStringLiteral("result")).toObject().value(QStringLiteral("structuredContent")).toObject();
  const auto prompt_budget_layers = prompt_budget_state.value(QStringLiteral("layers")).toArray();
  const auto prompt_budget_headline = prompt_budget_layers.size() < 3
      ? QJsonObject{} : prompt_budget_layers.last().toObject();
  const auto prompt_budget_typography = prompt_budget_headline.value(QStringLiteral("textTypography")).toObject();
  const auto prompt_budget_font_faces = prompt_budget_state.value(QStringLiteral("fontFaces")).toArray();
  const auto prompt_budget_calls = prompt_budget_plan.tool_call_plan;
  const auto prompt_budget_call_arguments = prompt_budget_calls.isEmpty()
      ? QVariantMap{} : prompt_budget_calls.first().toMap().value(QStringLiteral("arguments")).toMap();
  const auto prompt_budget_proofs = prompt_budget_history.value(QStringLiteral("proofReceipts")).toArray();
  const auto prompt_budget_snapshots = prompt_budget_history.value(QStringLiteral("approvedTaskSnapshots")).toArray();
  const auto prompt_budget_assessment =
      prompt_budget_history.value(QStringLiteral("currentCompositionAssessment")).toObject();
  const auto prompt_budget_components = prompt_budget_plan.prompt_audit
      .value(QStringLiteral("promptComponents")).toMap();
  const auto prompt_budget_revision_baseline =
      prompt_budget_assessment.value(QStringLiteral("revisionBaseline")).toObject();
  const auto prompt_budget_baseline_layers =
      prompt_budget_revision_baseline.value(QStringLiteral("textLayers")).toArray();
  const auto prompt_budget_baseline_headline = prompt_budget_baseline_layers.isEmpty()
      ? QJsonObject{} : prompt_budget_baseline_layers.first().toObject();
  const auto prompt_budget_approved_instructions = prompt_budget_snapshots.isEmpty()
      ? QString{} : prompt_budget_snapshots.first().toObject().value(QStringLiteral("snapshot")).toObject()
          .value(QStringLiteral("approvedInstructions")).toString();
  QStringList prompt_budget_failures;
  const auto check_prompt_budget = [&prompt_budget_failures](bool passed, const QString& label) {
    if (!passed) prompt_budget_failures.append(label);
  };
  check_prompt_budget(prompt_budget_plan.ok, QStringLiteral("plan.ok"));
  check_prompt_budget(prompt_budget_plan.decision == QStringLiteral("tool_call_plan"),
      QStringLiteral("plan.decision"));
  check_prompt_budget(prompt_budget_call_arguments.value(QStringLiteral("assetId")).toString() ==
      QStringLiteral("design-sample"), QStringLiteral("call.assetId"));
  check_prompt_budget(prompt_budget_history_start >= 0, QStringLiteral("history.marker"));
  check_prompt_budget(prompt_budget_history.value(QStringLiteral("currentComposition")).isObject(),
      QStringLiteral("history.currentComposition"));
  check_prompt_budget(prompt_budget_history_bytes.size() <= 32000, QStringLiteral("history.byteBudget"));
  check_prompt_budget(prompt_budget_layers.size() == 3, QStringLiteral("composition.layerCount"));
  check_prompt_budget(prompt_budget_headline.value(QStringLiteral("id")).toString() ==
      QStringLiteral("cover-headline"), QStringLiteral("composition.headlineId"));
  check_prompt_budget(prompt_budget_headline.value(QStringLiteral("text")).toString() ==
      QStringLiteral("AFTER THE LIGHT"), QStringLiteral("composition.headlineText"));
  check_prompt_budget(prompt_budget_typography.value(QStringLiteral("family")).toString() ==
      QStringLiteral("Avenir Next"), QStringLiteral("composition.fontFamily"));
  check_prompt_budget(prompt_budget_typography.value(QStringLiteral("face")).toString() ==
      QStringLiteral("Ultra Light"), QStringLiteral("composition.fontFace"));
  check_prompt_budget(prompt_budget_typography.value(QStringLiteral("size")).toInt() == 88,
      QStringLiteral("composition.fontSize"));
  check_prompt_budget(!prompt_budget_font_faces.isEmpty(), QStringLiteral("composition.fontFaces.present"));
  check_prompt_budget(!prompt_budget_font_faces.isEmpty() &&
      prompt_budget_font_faces.first().toObject().value(QStringLiteral("faces")).toArray()
          .contains(QStringLiteral("Ultra Light")), QStringLiteral("composition.fontFaces.headlineFace"));
  check_prompt_budget(!prompt_budget_state.value(QStringLiteral("displayTextColorPolicy")).toObject().isEmpty(),
      QStringLiteral("composition.displayTextColorPolicy"));
  check_prompt_budget(prompt_budget_headline.value(QStringLiteral("displayReferredTextPolicyVersion")).toString() ==
      QStringLiteral("display-referred-text-suffix-v1"), QStringLiteral("composition.textPolicyVersion"));
  check_prompt_budget(!prompt_budget_proofs.isEmpty(), QStringLiteral("proofReceipts.present"));
  check_prompt_budget(!prompt_budget_proofs.isEmpty() &&
      prompt_budget_proofs.first().toObject().value(QStringLiteral("result")).toObject()
          .value(QStringLiteral("structuredContent")).toObject().contains(QStringLiteral("imageStats")),
      QStringLiteral("proofReceipts.imageStats"));
  check_prompt_budget(prompt_budget_snapshots.size() == 1, QStringLiteral("approvedSnapshots.count"));
  check_prompt_budget(prompt_budget_assessment.value(QStringLiteral("kind")).toString() ==
      QStringLiteral("current-composition-assessment"), QStringLiteral("assessment.kind"));
  check_prompt_budget(prompt_budget_assessment.value(QStringLiteral("assetId")).toString() ==
      QStringLiteral("design-sample"), QStringLiteral("assessment.assetId"));
  check_prompt_budget(prompt_budget_assessment.value(QStringLiteral("batchRound")).toInt() == 2,
      QStringLiteral("assessment.batchRound"));
  check_prompt_budget(prompt_budget_assessment.value(QStringLiteral("sourceProofSha256")).toString() ==
      QStringLiteral("proof-sha-current-17"), QStringLiteral("assessment.proofDigest"));
  check_prompt_budget(prompt_budget_assessment.value(QStringLiteral("sourceRecipeSha256")).toString() ==
      QStringLiteral("recipe-sha-current-23"), QStringLiteral("assessment.recipeDigest"));
  check_prompt_budget(prompt_budget_revision_baseline.value(QStringLiteral("source")).toString() ==
      QStringLiteral("prior-accepted-native-inspect"), QStringLiteral("baseline.source"));
  check_prompt_budget(prompt_budget_revision_baseline.value(QStringLiteral("sourceProofSha256")).toString() ==
      QStringLiteral("proof-sha-before-revision"), QStringLiteral("baseline.proofDigest"));
  check_prompt_budget(prompt_budget_revision_baseline.value(QStringLiteral("sourceRecipeSha256")).toString() ==
      QStringLiteral("recipe-sha-before-revision"), QStringLiteral("baseline.recipeDigest"));
  check_prompt_budget(prompt_budget_revision_baseline.value(QStringLiteral("layerIdsTopFirst")).toArray().size() == 3,
      QStringLiteral("baseline.layerOrder"));
  check_prompt_budget(prompt_budget_baseline_headline.value(QStringLiteral("id")).toString() ==
      QStringLiteral("cover-headline"), QStringLiteral("baseline.headlineId"));
  check_prompt_budget(prompt_budget_baseline_headline.value(QStringLiteral("text")).toString() ==
      QStringLiteral("AFTER THE LIGHT"), QStringLiteral("baseline.headlineText"));
  check_prompt_budget(prompt_budget_baseline_headline.value(QStringLiteral("x")).toDouble() == 72.0,
      QStringLiteral("baseline.headlineX"));
  check_prompt_budget(prompt_budget_baseline_headline.value(QStringLiteral("textTypography")).toObject()
      .value(QStringLiteral("size")).toDouble() == 88.0, QStringLiteral("baseline.headlineSize"));
  check_prompt_budget(prompt_budget_assessment.value(QStringLiteral("textLayerIds")).toArray().size() == 3,
      QStringLiteral("assessment.textLayerIds"));
  const auto prompt_budget_geometry_checks = prompt_budget_assessment.value(QStringLiteral("geometryChecks")).toArray();
  const auto prompt_budget_rendered_bounds = prompt_budget_geometry_checks.isEmpty()
      ? QJsonObject{} : prompt_budget_geometry_checks.first().toObject()
          .value(QStringLiteral("renderedBounds")).toObject();
  check_prompt_budget(!prompt_budget_rendered_bounds.contains(QStringLiteral("untrustedExtra")),
      QStringLiteral("assessment.geometry.untrustedExtra"));
  check_prompt_budget(prompt_budget_rendered_bounds.value(QStringLiteral("width")).toDouble() == 820.0,
      QStringLiteral("assessment.geometry.width"));
  check_prompt_budget(prompt_budget_rendered_bounds.value(QStringLiteral("height")).toDouble() == 112.0,
      QStringLiteral("assessment.geometry.height"));
  check_prompt_budget(prompt_budget_rendered_bounds.value(QStringLiteral("w")).toDouble() == 820.0,
      QStringLiteral("assessment.geometry.w"));
  check_prompt_budget(prompt_budget_rendered_bounds.value(QStringLiteral("h")).toDouble() == 112.0,
      QStringLiteral("assessment.geometry.h"));
  const auto prompt_budget_typography_review = prompt_budget_assessment.value(QStringLiteral("nativeReview"))
      .toObject().value(QStringLiteral("typography")).toObject();
  check_prompt_budget(prompt_budget_typography_review.value(QStringLiteral("hierarchyAvailable")).toBool(),
      QStringLiteral("assessment.hierarchyAvailable"));
  check_prompt_budget(prompt_budget_typography_review.value(QStringLiteral("minimumRatio")).toDouble() == 2.0,
      QStringLiteral("assessment.minimumRatio"));
  check_prompt_budget(prompt_budget_typography_review.value(QStringLiteral("layoutFitGuidance")).toObject().toVariantMap() == fit_guidance,
      QStringLiteral("assessment.measuredFitGuidanceReachesActor"));
  check_prompt_budget(prompt_budget_assessment.value(QStringLiteral("instruction")).toString() ==
      current_assessment_instruction, QStringLiteral("assessment.instruction"));
  check_prompt_budget(prompt_budget_assessment.value(QStringLiteral("requireProductivePlan")).toBool(),
      QStringLiteral("assessment.requireProductivePlan"));
  check_prompt_budget(prompt_budget_assessment.value(QStringLiteral("modelCritique")).toObject()
      .value(QStringLiteral("repairHints")).toArray()
      .contains(QStringLiteral("Add the exact missing footer copy.")), QStringLiteral("assessment.repairHints"));
  check_prompt_budget(prompt_budget_history_bytes.contains("cover-headline"), QStringLiteral("history.headlineId"));
  check_prompt_budget(!prompt_budget_history_bytes.contains("STALE ASSESSMENT MUST NOT SURVIVE"),
      QStringLiteral("history.staleAssessmentDropped"));
  check_prompt_budget(prompt_budget_history.value(QStringLiteral("completedToolCount")).toInt() == 9,
      QStringLiteral("history.completedToolCount"));
  check_prompt_budget(!prompt_budget_history_bytes.contains("STALE APPROVED SCOPE MUST NOT SURVIVE"),
      QStringLiteral("history.staleApprovedScopeDropped"));
  check_prompt_budget(prompt_budget_approved_instructions == large_current_approved_instructions,
      QStringLiteral("approvedSnapshots.latestInstructions"));
  check_prompt_budget(prompt_budget_history_bytes.contains(
      "UNIQUE MID-BRIEF CONSTRAINT: preserve the approved open-sky headline layout."),
      QStringLiteral("history.midBriefConstraint"));
  check_prompt_budget(prompt_budget_history.value(QStringLiteral("mutationReceipts")).toArray().size() == 3,
      QStringLiteral("history.mutationReceiptCount"));
  check_prompt_budget(prompt_budget_plan.prompt_audit.value(QStringLiteral("requestSent")).toBool(),
      QStringLiteral("contextBudget.requestDispatched"));
  check_prompt_budget(prompt_budget_components.value(QStringLiteral("contextTokenLimit")).toInt() == 24576,
      QStringLiteral("contextBudget.selectedModelLimit"));
  check_prompt_budget(prompt_budget_components.value(QStringLiteral("contextTokensRequested")).toInt() == 24576,
      QStringLiteral("contextBudget.requestedModelLimit"));
  check_prompt_budget(prompt_budget_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() <= 24576,
      QStringLiteral("contextBudget.aggregateFitsModel"));
  if (!prompt_budget_failures.isEmpty()) {
    const auto diagnostic = QStringLiteral(
        "Oversized native history preflight failed [%1]; historyBytes=%2, completedToolCount=%3, "
        "layers=%4, fontFamilies=%5, proofReceipts=%6, snapshots=%7, mutations=%8, context=%9")
        .arg(prompt_budget_failures.join(QStringLiteral(", ")))
        .arg(prompt_budget_history_bytes.size())
        .arg(prompt_budget_history.value(QStringLiteral("completedToolCount")).toInt())
        .arg(prompt_budget_layers.size())
        .arg(prompt_budget_font_faces.size())
        .arg(prompt_budget_proofs.size())
        .arg(prompt_budget_snapshots.size())
        .arg(prompt_budget_history.value(QStringLiteral("mutationReceipts")).toArray().size())
        .arg(context_budget_summary(prompt_budget_plan));
    return fail(qPrintable(diagnostic));
  }
  const auto prompt_budget_system = prompt_budget_plan.prompt_audit
      .value(QStringLiteral("systemPrompt")).toString();
  const bool has_revision_baseline_guidance =
      prompt_budget_system.contains(QStringLiteral("revisionBaseline")) &&
      prompt_budget_system.contains(QStringLiteral("apply each requested relative correction once")) &&
      prompt_budget_system.contains(QStringLiteral("Preserve the baseline's stable text-layer IDs")) &&
      prompt_budget_system.contains(QStringLiteral("Verify the result against the latest fresh native readback"));
  const bool has_current_assessment_precedence_guidance =
      prompt_budget_system.contains(QStringLiteral("newest current-composition-assessment")) &&
      prompt_budget_system.contains(QStringLiteral("Treat earlier current-proof-validation as historical baseline only")) &&
      prompt_budget_system.contains(QStringLiteral("latest assessment's requireProductivePlan and instruction")) &&
      prompt_budget_system.contains(QStringLiteral("does not change approved scope or tool permissions"));
  if (!has_revision_baseline_guidance)
    return fail("A matching pre-revision baseline must guide one-time relative typography corrections while preserving approved layer identity and geometry");
  if (!has_current_assessment_precedence_guidance)
    return fail("The newest matching composition assessment must outrank earlier proof validation without broadening approved scope or tool permissions");

  // Replay the captured App09 post-mutation planner envelope from the audit
  // fixture. Only the rendered image path/hash are replaced by a generated PNG.
  const auto app09_fixture_path = repository_file_path(
      QStringLiteral("tests/fixtures/agent_local_model_app09_context_envelope.json"));
  if (app09_fixture_path.isEmpty())
    return fail("The captured App09 prompt fixture must be found from the test binary or working directory");
  const auto app09_fixture = read_json_file(app09_fixture_path);
  if (app09_fixture.value(QStringLiteral("contract")).toString() !=
      QStringLiteral("looksy-captured-app09-planner-envelope-v1"))
    return fail("The App09 replay fixture must identify its captured native planner audit contract");
  const auto app09_captured_audit = app09_fixture.value(QStringLiteral("capturedAudit")).toObject();
  const auto app09_asset_id = app09_fixture.value(QStringLiteral("assetId")).toString();
  const auto app09_headline_id = QStringLiteral("f6338853-914e-459d-ad7d-a11d6f09ea4c");
  const auto app09_kicker_id = QStringLiteral("c1e412b3-6c00-4e86-b6d2-fd28611c39fa");
  const auto app09_footer_id = QStringLiteral("1c8ae36b-7876-421d-a271-72b6a9b0e776");
  const auto app09_user_instructions = app09_fixture.value(QStringLiteral("userInstructions")).toString();
  const auto app09_patch_json = app09_fixture.value(QStringLiteral("currentAdjustmentPatchJson")).toString();
  const auto app09_tools = app09_fixture.value(QStringLiteral("availableToolSchemas")).toArray().toVariantList();
  const auto app09_captured_history = app09_fixture.value(QStringLiteral("priorToolResults")).toArray().toVariantList();
  if (app09_asset_id.isEmpty() || app09_tools.size() != 6 || app09_captured_history.size() != 11 ||
      app09_captured_audit.value(QStringLiteral("fontFamilyCountBeforePromptCompaction")).toInt() != 192 ||
      app09_captured_audit.value(QStringLiteral("fontFamilyCountRetainedInHistory")).toInt() != 48 ||
      app09_patch_json.toUtf8().size() != 10591 || app09_user_instructions.toUtf8().size() != 3410)
    return fail("The captured App09 fixture must retain its actual six tools, 11 results, exact brief/recipe sizes, and 192-to-48 font inventory");

  const auto app09_proof_path = QDir(runtime_root.path()).filePath(
      QStringLiteral("fixtures/app09-captured-replay-proof.png"));
  const auto app09_visual = app09_fixture.value(QStringLiteral("visualEvidence")).toObject();
  QImage app09_proof(
      app09_visual.value(QStringLiteral("proofWidth")).toInt(),
      app09_visual.value(QStringLiteral("proofHeight")).toInt(),
      QImage::Format_RGB32);
  app09_proof.fill(QColor(37, 58, 71));
  if (app09_proof.isNull() || !app09_proof.save(app09_proof_path, "PNG"))
    return fail("The App09 replay must write its generated local proof image");
  QFile app09_proof_file(app09_proof_path);
  if (!app09_proof_file.open(QIODevice::ReadOnly))
    return fail("The generated App09 proof must be readable for SHA-256 binding");
  const auto app09_proof_sha256 = QString::fromLatin1(
      QCryptographicHash::hash(app09_proof_file.readAll(), QCryptographicHash::Sha256).toHex());
  app09_proof_file.close();
  auto app09_visual_evidence = app09_visual;
  app09_visual_evidence.insert(QStringLiteral("currentProofPath"), app09_proof_path);
  app09_visual_evidence.insert(QStringLiteral("proofSha256"), app09_proof_sha256);

  photo_editor::desktop::AgentLocalModelPlanRequest app09_request;
  app09_request.model_id = QStringLiteral("ollama-app09-budget-test");
  app09_request.style = app09_fixture.value(QStringLiteral("style")).toObject().toVariantMap();
  app09_request.memory_packet = app09_fixture.value(QStringLiteral("memoryPacket")).toObject().toVariantMap();
  app09_request.few_shot_examples = app09_fixture.value(QStringLiteral("fewShotExamples")).toArray().toVariantList();
  app09_request.user_instructions = app09_user_instructions;
  app09_request.current_adjustment_patch_json = app09_patch_json;
  app09_request.available_tool_schemas = app09_tools;
  app09_request.prior_tool_results = app09_captured_history;
  app09_request.strategy_phase = app09_captured_audit.value(QStringLiteral("strategyPhase")).toString();
  app09_request.allow_tool_call_plan = true;
  app09_request.require_tool_call_plan = true;
  app09_request.force_model_planning = true;
  app09_request.allow_deterministic_fallback = false;
  app09_request.asset = QVariantMap{
      {QStringLiteral("assetId"), app09_asset_id},
      {QStringLiteral("proofImagePath"), app09_proof_path},
      {QStringLiteral("planningProofRequired"), true},
      {QStringLiteral("planningProofSha256"), app09_proof_sha256},
      {QStringLiteral("visualEvidence"), app09_visual_evidence.toVariantMap()},
  };

  const auto make_app09_typography_call = [app09_asset_id](const QString& layer_id, int size) {
    return QJsonObject{
        {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
        {QStringLiteral("arguments"),
         QJsonObject{
            {QStringLiteral("assetId"), app09_asset_id},
            {QStringLiteral("layerId"), layer_id},
            {QStringLiteral("operation"), QStringLiteral("typography")},
            {QStringLiteral("values"),
             QJsonObject{
                {QStringLiteral("family"), QStringLiteral("Avenir Next")},
                {QStringLiteral("face"), QStringLiteral("Ultra Light")},
                {QStringLiteral("size"), size}}}}},
        {QStringLiteral("reason"), QStringLiteral("Use the inspected family/face and repair the approved hierarchy on the stable layer ID.")},
    };
  };
  const QJsonObject app09_plan_response_content{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Correct all three existing text layers using inspected native typography.")},
      {QStringLiteral("toolCalls"), QJsonArray{
          make_app09_typography_call(app09_headline_id, 160),
          make_app09_typography_call(app09_kicker_id, 48),
          make_app09_typography_call(app09_footer_id, 48)}},
  };
  auto app09_response = fake_composition_tool_response;
  auto app09_message = app09_response.value(QStringLiteral("message")).toObject();
  app09_message.insert(QStringLiteral("content"), QString::fromUtf8(
      QJsonDocument(app09_plan_response_content).toJson(QJsonDocument::Compact)));
  app09_response.insert(QStringLiteral("message"), app09_message);
  const auto saved_app09_fake_response = qgetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  if (fake_ollama_server_available)
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), app09_response);
  else
    qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
        QJsonDocument(app09_response).toJson(QJsonDocument::Compact));

  const auto app09_fit_plan = photo_editor::desktop::plan_agent_recipe_patch(app09_request);
  const auto app09_sent_payload = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto app09_prompt = app09_fit_plan.prompt_audit.value(QStringLiteral("userPrompt")).toString();
  const auto app09_system = app09_fit_plan.prompt_audit.value(QStringLiteral("systemPrompt")).toString();
  const auto app09_components = app09_fit_plan.prompt_audit.value(QStringLiteral("promptComponents")).toMap();
  const auto app09_format = app09_sent_payload.value(QStringLiteral("format")).toObject();
  const auto app09_tool_branch = schema_branch_for_decision(app09_format, QStringLiteral("tool_call_plan"));
  const auto app09_tool_calls_schema = app09_tool_branch.value(QStringLiteral("properties")).toObject()
      .value(QStringLiteral("toolCalls")).toObject();
  const auto app09_tool_variants = app09_tool_calls_schema.value(QStringLiteral("items")).toObject()
      .value(QStringLiteral("oneOf")).toArray();
  QSet<QString> app09_schema_tool_names;
  for (const auto& variant_value : app09_tool_variants) {
    const auto name_choices = variant_value.toObject().value(QStringLiteral("properties")).toObject()
        .value(QStringLiteral("name")).toObject().value(QStringLiteral("enum")).toArray();
    for (const auto& name : name_choices) app09_schema_tool_names.insert(name.toString());
  }
  const auto app09_edit_typography = schema_branch_for_tool_operation(
      app09_format, QStringLiteral("photo.edit_composition"), QStringLiteral("typography"));
  const auto app09_edit_properties = app09_edit_typography.value(QStringLiteral("properties")).toObject();
  const auto app09_layer_ids = app09_edit_properties.value(QStringLiteral("layerId")).toObject()
      .value(QStringLiteral("enum")).toArray();
  const auto app09_value_properties = app09_edit_properties.value(QStringLiteral("values")).toObject()
      .value(QStringLiteral("properties")).toObject();
  const auto canonical_typography_properties = photo_editor::desktop::agent_typography_schema()
      .value(QStringLiteral("properties")).toMap();
  const auto canonical_color_description = canonical_typography_properties
      .value(QStringLiteral("color")).toMap().value(QStringLiteral("description")).toString();
  const auto app09_family_choices = app09_value_properties.value(QStringLiteral("family")).toObject()
      .value(QStringLiteral("enum")).toArray();
  const auto app09_face_choices = app09_value_properties.value(QStringLiteral("face")).toObject()
      .value(QStringLiteral("enum")).toArray();
  const auto section = [](const QString& source, const QString& start_marker, const QString& end_marker) {
    const auto start = source.indexOf(start_marker);
    if (start < 0) return QString{};
    const auto value_start = start + start_marker.size();
    const auto end = source.indexOf(end_marker, value_start);
    return end < value_start ? QString{} : source.mid(value_start, end - value_start).trimmed();
  };
  const auto app09_catalog_text = section(app09_prompt,
      QStringLiteral("Available MCP tool schemas JSON:\n"),
      QStringLiteral("Current rendered planning evidence JSON:\n"));
  const auto app09_catalog = QJsonDocument::fromJson(app09_catalog_text.toUtf8()).array();
  const auto app09_history_start = app09_prompt.lastIndexOf(history_label);
  const auto app09_compacted_history = app09_history_start < 0 ? QJsonObject{}
      : QJsonDocument::fromJson(app09_prompt.mid(
          app09_history_start + history_label.size()).toUtf8()).object();
  const auto app09_history_mutations = app09_compacted_history.value(QStringLiteral("mutationReceipts")).toArray();
  const auto app09_history_assessment = app09_compacted_history.value(QStringLiteral("currentCompositionAssessment")).toObject();
  const auto app09_current_composition = app09_compacted_history.value(QStringLiteral("currentComposition")).toObject()
      .value(QStringLiteral("result")).toObject().value(QStringLiteral("structuredContent")).toObject();
  const auto app09_current_layers = app09_current_composition.value(QStringLiteral("layers")).toArray();
  const auto app09_compacted_native_colors_complete = app09_current_layers.size() == 3 &&
      std::all_of(app09_current_layers.cbegin(), app09_current_layers.cend(), [](const QJsonValue& value) {
        const auto typography = value.toObject().value(QStringLiteral("textTypography")).toObject();
        const auto channels = typography.value(QStringLiteral("nativeColorChannels")).toObject()
            .value(QStringLiteral("color")).toArray();
        return typography.value(QStringLiteral("color")).toString() == QStringLiteral("#ffffffff") &&
            channels.size() == 4 && channels.at(0).toInt() == 255 && channels.at(1).toInt() == 255 &&
            channels.at(2).toInt() == 255 && channels.at(3).toInt() == 255;
      });
  const auto app09_retained_fonts = app09_current_composition.value(QStringLiteral("fontFaces")).toArray();
  const auto app09_user_section = section(app09_prompt,
      QStringLiteral("User instructions:\n"), QStringLiteral("Current adjustment patch JSON:\n"));
  const auto app09_patch_section = section(app09_prompt,
      QStringLiteral("Current adjustment patch JSON:\n"), QStringLiteral("Style rulebook JSON:\n"));
  const auto app09_patch_state = QJsonDocument::fromJson(app09_patch_section.toUtf8()).object()
      .value(QStringLiteral("adjustmentState")).toObject();
  const auto app09_format_bytes = app09_components.value(QStringLiteral("generationSchemaUtf8Bytes")).toLongLong();
  const auto app09_system_bytes = app09_components.value(QStringLiteral("systemPromptUtf8Bytes")).toLongLong();
  const auto app09_user_bytes = app09_components.value(QStringLiteral("userPromptUtf8Bytes")).toLongLong();
  const auto app09_aggregate_bytes = app09_components.value(QStringLiteral("aggregateTextUtf8Bytes")).toLongLong();
  const auto app09_ascii_bytes = app09_components.value(QStringLiteral("estimatedAsciiUtf8Bytes")).toLongLong();
  const auto app09_non_ascii_bytes = app09_components.value(QStringLiteral("estimatedNonAsciiUtf8Bytes")).toLongLong();
  const auto app09_text_tokens = app09_components.value(QStringLiteral("estimatedAggregateTextTokens")).toLongLong();
  const auto app09_context_tokens = app09_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt();
  const auto app09_context_limit = app09_components.value(QStringLiteral("contextTokenLimit")).toInt();
  // The output reservation is exposed in the request audit and sent Ollama
  // options, not in the promptComponents map.
  const auto app09_output_tokens = app09_fit_plan.prompt_audit
      .value(QStringLiteral("planningMaxOutputTokens")).toInt();
  const auto app09_sent_output_tokens = app09_sent_payload.value(QStringLiteral("options"))
      .toObject().value(QStringLiteral("num_predict")).toInt();
  const auto app09_image_count = app09_components.value(QStringLiteral("attachedImageCount")).toInt();
  const auto app09_reserved_tokens = 512 + app09_output_tokens +
      image_reservation_total(app09_components);
  const auto app09_expected_text_tokens = (app09_ascii_bytes + 3) / 4 + app09_non_ascii_bytes;
  const auto app09_image_max_edge = app09_fit_plan.prompt_audit
      .value(QStringLiteral("imageMaxEdge")).toInt();
  auto app09_expected_image_size = app09_proof.size();
  if (app09_expected_image_size.width() > app09_image_max_edge ||
      app09_expected_image_size.height() > app09_image_max_edge) {
    app09_expected_image_size = app09_expected_image_size.scaled(
        QSize(app09_image_max_edge, app09_image_max_edge),
        Qt::KeepAspectRatio);
  }
  const auto app09_image_width = app09_fit_plan.prompt_audit.value(QStringLiteral("imageWidth")).toInt();
  const auto app09_image_height = app09_fit_plan.prompt_audit.value(QStringLiteral("imageHeight")).toInt();
  const auto app09_image_original_bytes = app09_fit_plan.prompt_audit
      .value(QStringLiteral("imageOriginalByteCount")).toLongLong();
  const auto app09_image_transmitted_bytes = app09_fit_plan.prompt_audit
      .value(QStringLiteral("imageByteCount")).toLongLong();
  QJsonObject app09_edit_source_schema;
  for (const auto& tool_value : app09_fixture.value(QStringLiteral("availableToolSchemas")).toArray()) {
    const auto tool = tool_value.toObject();
    if (tool.value(QStringLiteral("name")).toString() == QStringLiteral("photo.edit_composition")) {
      app09_edit_source_schema = tool.value(QStringLiteral("inputSchema")).toObject();
      break;
    }
  }
  QJsonObject app09_edit_guide;
  for (const auto& tool_value : app09_catalog) {
    const auto tool = tool_value.toObject();
    if (tool.value(QStringLiteral("name")).toString() == QStringLiteral("photo.edit_composition")) {
      app09_edit_guide = tool.value(QStringLiteral("argumentGuide")).toObject();
      break;
    }
  }
  const auto app09_source_value_fields = app09_edit_source_schema.value(QStringLiteral("properties"))
      .toObject().value(QStringLiteral("values")).toObject().value(QStringLiteral("properties")).toObject();
  const auto app09_guide_field_semantics = app09_edit_guide.value(QStringLiteral("fieldSemantics")).toObject();
  const auto app09_field_description_matches_source = [&](const QString& field) {
    return app09_guide_field_semantics.value(QStringLiteral("values.%1").arg(field)).toObject()
            .value(QStringLiteral("description")).toString() ==
        app09_source_value_fields.value(field).toObject().value(QStringLiteral("description")).toString();
  };
  const auto app09_required_fields_by_operation = app09_edit_guide
      .value(QStringLiteral("requiredFieldsByOperation")).toObject();
  const auto app09_typography_required = app09_required_fields_by_operation
      .value(QStringLiteral("typography")).toArray();
  const auto app09_transform_required = app09_required_fields_by_operation
      .value(QStringLiteral("transform")).toArray();
  QStringList app09_failures;
  const auto check_app09 = [&app09_failures](bool passed, const QString& label) {
    if (!passed) app09_failures.append(label);
  };
  check_app09(app09_fit_plan.ok && app09_fit_plan.decision == QStringLiteral("tool_call_plan"),
      QStringLiteral("actualReplay.toolCallPlan"));
  check_app09(app09_fit_plan.prompt_audit.value(QStringLiteral("requestSent")).toBool(),
      QStringLiteral("actualReplay.dispatched"));
  check_app09(app09_fit_plan.tool_call_plan.size() == 3,
      QStringLiteral("actualReplay.threeTypographyCorrections"));
  check_app09(app09_system_bytes + app09_user_bytes == app09_aggregate_bytes,
      QStringLiteral("actualReplay.aggregateUtf8Accounting"));
  check_app09(app09_ascii_bytes + app09_non_ascii_bytes == app09_aggregate_bytes &&
      app09_text_tokens == app09_expected_text_tokens &&
      app09_context_tokens == app09_text_tokens + app09_reserved_tokens &&
      app09_sent_output_tokens == app09_output_tokens &&
      app09_output_tokens == app09_captured_audit.value(QStringLiteral("planningMaxOutputTokens")).toInt(),
      QStringLiteral("actualReplay.conservativeAggregateEstimate"));
  check_app09(app09_context_tokens <= app09_context_limit &&
      app09_components.value(QStringLiteral("contextTokensRequested")).toInt() == app09_context_limit,
      QStringLiteral("actualReplay.fitWithoutRaisingModelLimit"));
  check_app09(app09_components.value(QStringLiteral("contextEstimateMethod")).toString().contains(
      QStringLiteral("non_ascii_utf8_bytes")), QStringLiteral("actualReplay.nonAsciiMethodAudited"));
  check_app09(app09_image_count == 1 &&
      app09_components.value(QStringLiteral("imageContextReservations")).toList().size() == app09_image_count &&
      app09_image_transmitted_bytes > 0 &&
      app09_fit_plan.prompt_audit.value(QStringLiteral("imageIncluded")).toBool() &&
      app09_fit_plan.prompt_audit.value(QStringLiteral("imageSourceKind")).toString() == QStringLiteral("proof") &&
      app09_fit_plan.prompt_audit.value(QStringLiteral("imageSourcePath")).toString() == app09_proof_path &&
      app09_image_original_bytes == app09_proof_file.size() &&
      app09_image_width == app09_expected_image_size.width() &&
      app09_image_height == app09_expected_image_size.height() &&
      std::max(app09_image_width, app09_image_height) <= app09_image_max_edge,
      QStringLiteral("actualReplay.boundLocalProofImage"));
  check_app09(app09_components.value(QStringLiteral("availableToolCatalogUtf8Bytes")).toLongLong() <
      app09_captured_audit.value(QStringLiteral("toolCatalogUtf8Bytes")).toVariant().toLongLong() &&
      app09_components.value(QStringLiteral("priorToolHistoryUtf8Bytes")).toLongLong() <
      app09_captured_audit.value(QStringLiteral("priorToolHistoryUtf8Bytes")).toVariant().toLongLong(),
      QStringLiteral("actualReplay.safeCatalogAndHistoryCompaction"));
  check_app09(app09_components.value(QStringLiteral("userGuidanceMode")).toString() ==
      QStringLiteral("scoped_post_mutation_text_composition_v1") &&
      app09_prompt.contains(QStringLiteral("Scoped post-mutation text-composition repair prompt v1")),
      QStringLiteral("actualReplay.narrowTextOnlyGuidanceMode"));
  check_app09(app09_prompt.contains(QStringLiteral("Preserve its exact copy, target asset, deliverables and capabilities")) &&
      app09_prompt.contains(QStringLiteral("preserve the photograph, framing, color and unrelated layers")) &&
      app09_prompt.contains(QStringLiteral("latest native readback and rendered overview/detail evidence")) &&
      app09_prompt.contains(QStringLiteral("intended foreground occlusion")) &&
      app09_prompt.contains(QStringLiteral("Metadata and successful receipts alone do not prove visible correctness")),
      QStringLiteral("actualReplay.protectedBriefAndVisibleGraphicChecksRetained"));
  check_app09(app09_format_bytes > 0 && app09_catalog.size() == 6 &&
      app09_tool_variants.size() == app09_captured_audit.value(
          QStringLiteral("generationSchemaToolVariantCount")).toInt() &&
      app09_schema_tool_names.size() == 6,
      QStringLiteral("actualReplay.allSixCanonicalToolsAndCapturedOperationVariants"));
  check_app09(app09_catalog_text.contains(QStringLiteral("argumentGuide")) &&
      !app09_catalog_text.contains(QStringLiteral("inputSchema")),
      QStringLiteral("actualReplay.derivedGuideDoesNotRepeatFullSchema"));
  check_app09(app09_prompt.contains(QStringLiteral("typed composition tools, their argument guide")) &&
      app09_edit_guide.value(QStringLiteral("type")).toString() == QStringLiteral("object") &&
      !app09_typography_required.isEmpty() && !app09_transform_required.isEmpty() &&
      app09_typography_required.contains(QStringLiteral("layerId")) &&
      app09_transform_required.contains(QStringLiteral("layerId")) &&
      app09_field_description_matches_source(QStringLiteral("x")) &&
      app09_field_description_matches_source(QStringLiteral("y")) &&
      app09_field_description_matches_source(QStringLiteral("size")) &&
      app09_field_description_matches_source(QStringLiteral("scale")) &&
      app09_field_description_matches_source(QStringLiteral("rotation")) &&
      app09_guide_field_semantics.value(QStringLiteral("values.x")).toObject()
          .value(QStringLiteral("description")).toString().contains(QStringLiteral("not normalized")) &&
      app09_guide_field_semantics.value(QStringLiteral("values.size")).toObject()
          .value(QStringLiteral("description")).toString().contains(QStringLiteral("document pixels, not points")) &&
      app09_guide_field_semantics.value(QStringLiteral("values.scale")).toObject()
          .value(QStringLiteral("description")).toString().contains(QStringLiteral("Uniform layer-size multiplier")) &&
      app09_guide_field_semantics.value(QStringLiteral("values.rotation")).toObject()
          .value(QStringLiteral("description")).toString().contains(QStringLiteral("rotation in degrees")),
      QStringLiteral("actualReplay.coordinateTypographyScaleAndRotationSemantics"));
  check_app09(app09_layer_ids.contains(app09_headline_id) && app09_layer_ids.contains(app09_kicker_id) &&
      app09_layer_ids.contains(app09_footer_id) && app09_family_choices.contains(QStringLiteral("Avenir Next")) &&
      app09_face_choices.contains(QStringLiteral("Ultra Light")),
      QStringLiteral("actualReplay.exactNativeIDsAndInspectedFontEnums"));
  check_app09(app09_retained_fonts.size() == 48 &&
      std::any_of(app09_retained_fonts.cbegin(), app09_retained_fonts.cend(), [](const QJsonValue& value) {
        const auto font = value.toObject();
        return font.value(QStringLiteral("family")).toString() == QStringLiteral("Avenir Next") &&
            font.value(QStringLiteral("faces")).toArray().contains(QStringLiteral("Ultra Light"));
      }), QStringLiteral("actualReplay.captured192FamilyInventoryRetains48AndRequestedFace"));
  check_app09(app09_current_layers.size() == 3 &&
      app09_current_layers.at(0).toObject().value(QStringLiteral("id")).toString() == app09_footer_id &&
      app09_current_layers.at(1).toObject().value(QStringLiteral("id")).toString() == app09_kicker_id &&
      app09_current_layers.at(2).toObject().value(QStringLiteral("id")).toString() == app09_headline_id,
      QStringLiteral("actualReplay.latestNativeReadbackRetainsExactLayerOrderAndIDs"));
  check_app09(app09_compacted_native_colors_complete,
      QStringLiteral("actualReplay.nativeColorChannelsPreserveCompactedRawTypography"));
  check_app09(canonical_color_description.contains(QStringLiteral("Prefer opaque six-digit #RRGGBB")) &&
      canonical_color_description.contains(QStringLiteral("Eight-digit hex is Qt #AARRGGBB")) &&
      canonical_color_description.contains(QStringLiteral("never CSS #RRGGBBAA")) &&
      app09_system.contains(QStringLiteral("Read nativeColorChannels as red, green, blue, alpha")) &&
      app09_system.contains(QStringLiteral("Prefer opaque six-digit #RRGGBB")) &&
      app09_system.contains(QStringLiteral("never CSS #RRGGBBAA")),
      QStringLiteral("actualReplay.qtColorSchemaAndGuidance"));
  check_app09(app09_history_assessment.value(QStringLiteral("batchRound")).toInt() == 3 &&
      app09_history_assessment.value(QStringLiteral("requireProductivePlan")).toBool() &&
      !app09_history_assessment.value(QStringLiteral("instruction")).toString().isEmpty() &&
      app09_history_assessment.value(QStringLiteral("sourceRecipeSha256")).toString() ==
          app09_current_composition.value(QStringLiteral("taskStateHash")).toString() &&
      app09_history_assessment.value(QStringLiteral("sourceProofSha256")).toString() ==
          app09_fixture.value(QStringLiteral("visualEvidence")).toObject()
              .value(QStringLiteral("proofSha256")).toString(),
      QStringLiteral("actualReplay.latestAssessmentRemainsBoundToCapturedRecipeAndProof"));
  check_app09(app09_history_mutations.size() == 3 &&
      app09_history_mutations.at(0).toObject().value(QStringLiteral("arguments")).toObject()
          .value(QStringLiteral("values")).toObject().value(QStringLiteral("text")).toString() ==
              QStringLiteral("AFTER THE LIGHT") &&
      app09_history_mutations.at(0).toObject().value(QStringLiteral("outcome")).toObject()
          .value(QStringLiteral("createdLayerId")).toString() == app09_headline_id,
      QStringLiteral("actualReplay.exactAcceptedMutationAndReturnedLayerID"));
  check_app09(app09_user_section.contains(QStringLiteral(
      "identical approvedInstructions in the latest frozen approved-task snapshot")) &&
      !app09_user_section.contains(app09_user_instructions) && app09_prompt.contains(QStringLiteral("AFTER THE LIGHT")),
      QStringLiteral("actualReplay.frozenBriefDeduplicationRetainsApprovedCopy"));
  check_app09(app09_patch_state.contains(QStringLiteral("layers")) &&
      !app09_patch_state.contains(QStringLiteral("photoLayers")),
      QStringLiteral("actualReplay.removesOnlyDuplicatePhotoLayersAlias"));
  check_app09(app09_prompt.contains(QStringLiteral("AFTER THE LIGHT")) &&
      app09_prompt.contains(QStringLiteral("SAMPLE / FIELD NOTES 01")) &&
      app09_prompt.contains(QStringLiteral("A STUDY IN BLUE HOUR")) &&
      app09_prompt.contains(QStringLiteral("Avenir Next")) && app09_prompt.contains(QStringLiteral("Ultra Light")),
      QStringLiteral("actualReplay.copyAndFontChoicesSurviveCompaction"));
  check_app09(app09_fit_plan.tool_call_plan.size() == 3 &&
      app09_fit_plan.tool_call_plan.at(0).toMap().value(QStringLiteral("arguments")).toMap()
          .value(QStringLiteral("layerId")).toString() == app09_headline_id &&
      app09_fit_plan.tool_call_plan.at(0).toMap().value(QStringLiteral("arguments")).toMap()
          .value(QStringLiteral("values")).toMap().value(QStringLiteral("size")).toInt() == 160,
      QStringLiteral("actualReplay.currentCorrectionUsesAcceptedHeadlineID"));
  if (!app09_failures.isEmpty()) {
    const auto short_description = [](const QString& description) {
      return description.simplified().left(64);
    };
    const auto schema_semantic_diagnostic = QStringLiteral("x=%1; size=%2; scale=%3; rotation=%4")
        .arg(short_description(app09_guide_field_semantics.value(QStringLiteral("values.x")).toObject()
                .value(QStringLiteral("description")).toString()),
            short_description(app09_guide_field_semantics.value(QStringLiteral("values.size")).toObject()
                .value(QStringLiteral("description")).toString()),
            short_description(app09_guide_field_semantics.value(QStringLiteral("values.scale")).toObject()
                .value(QStringLiteral("description")).toString()),
            short_description(app09_guide_field_semantics.value(QStringLiteral("values.rotation")).toObject()
                .value(QStringLiteral("description")).toString()));
    return fail(qPrintable(QStringLiteral(
        "Captured App09 fit-and-dispatch replay failed [%1]; systemBytes=%2 userBytes=%3 aggregateBytes=%4 asciiBytes=%5 nonAsciiBytes=%6 textTokens=%7 contextTokens=%8 limit=%9 tools=%10 history=%11 outputTokens=%12/%13 imageSource=%14 image=%15x%16 expected=%17x%18 imageBytes=%19/%20 schema={%21}")
        .arg(app09_failures.join(QStringLiteral(", ")))
        .arg(app09_system_bytes).arg(app09_user_bytes).arg(app09_aggregate_bytes)
        .arg(app09_ascii_bytes).arg(app09_non_ascii_bytes).arg(app09_text_tokens)
        .arg(app09_context_tokens).arg(app09_context_limit).arg(app09_catalog.size())
        .arg(app09_compacted_history.size())
        .arg(app09_output_tokens).arg(app09_sent_output_tokens)
        .arg(app09_fit_plan.prompt_audit.value(QStringLiteral("imageSourceKind")).toString())
        .arg(app09_image_width).arg(app09_image_height)
        .arg(app09_expected_image_size.width()).arg(app09_expected_image_size.height())
        .arg(app09_image_transmitted_bytes).arg(app09_image_original_bytes)
        .arg(schema_semantic_diagnostic)));
  }
  std::cout << "Captured App09 compact planner replay: aggregateTextUtf8Bytes="
            << app09_aggregate_bytes << " asciiUtf8Bytes=" << app09_ascii_bytes
            << " nonAsciiUtf8Bytes=" << app09_non_ascii_bytes
            << " estimatedTextTokens=" << app09_text_tokens
            << " estimatedContextTokens=" << app09_context_tokens
            << " contextTokenLimit=" << app09_context_limit << '\n';

  const auto app09_payload_before_failures = app09_sent_payload;
  auto app09_16k_request = app09_request;
  app09_16k_request.model_id = QStringLiteral("ollama-qwen-test");
  app09_16k_request.user_instructions += QStringLiteral("\nApproved 16K negative-case content: ") +
      QString(24000, QLatin1Char('x'));
  bool app09_16k_snapshot_updated = false;
  for (qsizetype offset = 0; offset < app09_16k_request.prior_tool_results.size(); ++offset) {
    const auto index = app09_16k_request.prior_tool_results.size() - 1 - offset;
    auto history_item = app09_16k_request.prior_tool_results.at(index).toMap();
    if (history_item.value(QStringLiteral("kind")).toString() !=
        QStringLiteral("approved-task-snapshot")) continue;
    auto snapshot = history_item.value(QStringLiteral("snapshot")).toMap();
    if (!snapshot.value(QStringLiteral("targetIds")).toStringList().contains(app09_asset_id)) continue;
    snapshot.insert(QStringLiteral("approvedInstructions"), app09_16k_request.user_instructions);
    history_item.insert(QStringLiteral("snapshot"), snapshot);
    app09_16k_request.prior_tool_results[index] = history_item;
    app09_16k_snapshot_updated = snapshot.value(QStringLiteral("approvedInstructions")).toString() ==
        app09_16k_request.user_instructions;
    break;
  }
  const auto app09_16k_rejected = photo_editor::desktop::plan_agent_recipe_patch(app09_16k_request);
  const auto app09_payload_after_16k = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto app09_16k_components = app09_16k_rejected.prompt_audit
      .value(QStringLiteral("promptComponents")).toMap();
  if (!app09_16k_snapshot_updated || app09_16k_rejected.ok ||
      app09_16k_rejected.prompt_audit.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("planning_context_budget_exceeded") ||
      app09_16k_rejected.prompt_audit.value(QStringLiteral("requestSent")).toBool() ||
      app09_16k_components.value(QStringLiteral("contextTokenLimit")).toInt() != 16384 ||
      app09_16k_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() <= 16384 ||
      app09_payload_after_16k != app09_payload_before_failures) {
    return fail(qPrintable(QStringLiteral(
        "Captured App09 request must fail closed on the declared 16K model and dispatch on 24K only; context=%1")
        .arg(context_budget_summary(app09_16k_rejected))));
  }

  const auto app09_margin_tokens = app09_context_limit - app09_context_tokens;
  auto app09_cjk_request = app09_request;
  QString app09_cjk_base_approved_instructions;
  qsizetype app09_cjk_snapshot_index = -1;
  for (qsizetype offset = 0; offset < app09_cjk_request.prior_tool_results.size(); ++offset) {
    const auto index = app09_cjk_request.prior_tool_results.size() - 1 - offset;
    const auto history_item = app09_cjk_request.prior_tool_results.at(index).toMap();
    if (history_item.value(QStringLiteral("kind")).toString() !=
        QStringLiteral("approved-task-snapshot")) continue;
    const auto snapshot = history_item.value(QStringLiteral("snapshot")).toMap();
    if (snapshot.value(QStringLiteral("targetIds")).toStringList().contains(app09_asset_id)) {
      app09_cjk_base_approved_instructions =
          snapshot.value(QStringLiteral("approvedInstructions")).toString();
      app09_cjk_snapshot_index = index;
      break;
    }
  }
  if (app09_cjk_base_approved_instructions.isEmpty())
    app09_cjk_base_approved_instructions = app09_cjk_request.user_instructions;
  const auto app09_cjk_instructions = app09_cjk_base_approved_instructions +
      QStringLiteral("\nAdditional CJK content: ");
  const auto app09_cjk_count = std::max(1, app09_margin_tokens / 3 + 1);
  app09_cjk_request.user_instructions = app09_cjk_instructions + QString(app09_cjk_count, QChar(0x4e00));
  bool app09_cjk_snapshot_updated = false;
  if (app09_cjk_snapshot_index >= 0) {
    auto history_item = app09_cjk_request.prior_tool_results.at(app09_cjk_snapshot_index).toMap();
    auto snapshot = history_item.value(QStringLiteral("snapshot")).toMap();
    snapshot.insert(QStringLiteral("approvedInstructions"), app09_cjk_request.user_instructions);
    history_item.insert(QStringLiteral("snapshot"), snapshot);
    app09_cjk_request.prior_tool_results[app09_cjk_snapshot_index] = history_item;
    app09_cjk_snapshot_updated = snapshot.value(QStringLiteral("approvedInstructions")).toString() ==
        app09_cjk_request.user_instructions &&
        app09_cjk_request.user_instructions.startsWith(app09_cjk_base_approved_instructions);
  }
  const auto app09_cjk_rejected = photo_editor::desktop::plan_agent_recipe_patch(app09_cjk_request);
  const auto app09_payload_after_cjk = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto app09_cjk_components = app09_cjk_rejected.prompt_audit
      .value(QStringLiteral("promptComponents")).toMap();
  const auto app09_cjk_bytes = app09_cjk_components.value(QStringLiteral("aggregateTextUtf8Bytes")).toLongLong();
  const auto app09_naive_utf8_quarter_tokens = (app09_cjk_bytes + 3) / 4;
  const auto app09_cjk_output_tokens = app09_cjk_rejected.prompt_audit
      .value(QStringLiteral("planningMaxOutputTokens")).toInt();
  const auto app09_cjk_image_reservations = app09_cjk_components
      .value(QStringLiteral("imageContextReservations")).toList();
  const auto app09_cjk_reserve = 512 + app09_cjk_output_tokens +
      image_reservation_total(app09_cjk_components);
  if (app09_cjk_rejected.ok ||
      app09_cjk_rejected.prompt_audit.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("planning_context_budget_exceeded") ||
      app09_cjk_rejected.prompt_audit.value(QStringLiteral("requestSent")).toBool() ||
      !app09_cjk_snapshot_updated ||
      app09_cjk_request.user_instructions.toUtf8().size() <=
          app09_cjk_base_approved_instructions.toUtf8().size() ||
      app09_cjk_image_reservations.size() !=
          app09_cjk_components.value(QStringLiteral("attachedImageCount")).toInt() ||
      app09_cjk_components.value(QStringLiteral("userGuidanceMode")).toString() !=
          QStringLiteral("scoped_post_mutation_text_composition_v1") ||
      app09_cjk_rejected.prompt_audit.value(QStringLiteral("planningMaxOutputTokens")).toInt() !=
          app09_captured_audit.value(QStringLiteral("planningMaxOutputTokens")).toInt() ||
      app09_cjk_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() <= app09_context_limit ||
      app09_naive_utf8_quarter_tokens + app09_cjk_reserve > app09_context_limit ||
      app09_cjk_components.value(QStringLiteral("estimatedNonAsciiCodePointCount")).toLongLong() < app09_cjk_count ||
      app09_payload_after_cjk != app09_payload_before_failures)
    return fail(qPrintable(QStringLiteral(
        "CJK budget replay must preserve the frozen brief (approvedBaseBytes=%1 fixtureBriefBytes=%2), remain in scoped mode, and fail closed; mode=%3 snapshotUpdated=%4 cjk=%5 estimated=%6 naive=%7 reserve=%8 limit=%9")
        .arg(QString::number(app09_cjk_base_approved_instructions.toUtf8().size()))
        .arg(QString::number(app09_user_instructions.toUtf8().size()))
        .arg(app09_cjk_components.value(QStringLiteral("userGuidanceMode")).toString())
        .arg(app09_cjk_snapshot_updated ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(app09_cjk_count)
        .arg(app09_cjk_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt())
        .arg(app09_naive_utf8_quarter_tokens)
        .arg(app09_cjk_reserve)
        .arg(app09_context_limit)));

  auto app09_oversized_request = app09_request;
  app09_oversized_request.user_instructions += QStringLiteral("\nAdditional unsupported ASCII scope follows:\n") +
      QString(120000, QLatin1Char('x'));
  const auto app09_oversized = photo_editor::desktop::plan_agent_recipe_patch(app09_oversized_request);
  const auto app09_payload_after_ascii = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  if (!saved_app09_fake_response.isEmpty())
    qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON", saved_app09_fake_response);
  else
    qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  if (fake_ollama_server_available)
    fake_ollama_server.setResponse(QStringLiteral("/api/chat"), fake_composition_tool_response);
  if (app09_oversized.ok ||
      app09_oversized.prompt_audit.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("planning_context_budget_exceeded") ||
      app09_oversized.prompt_audit.value(QStringLiteral("requestSent")).toBool() ||
      !app09_oversized.tool_call_plan.isEmpty() || !app09_oversized.recipe_patch.isEmpty() ||
      app09_oversized.prompt_audit.value(QStringLiteral("promptComponents")).toMap()
          .value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() <= app09_context_limit ||
      app09_payload_after_ascii != app09_payload_before_failures)
    return fail("A materially oversized captured App09 planner turn must fail closed before dispatch or deterministic fallback");

  request.prior_tool_results = QVariantList{approved_snapshot_envelope};
  for (int i = 0; i < 26; ++i) {
    request.prior_tool_results.append(QVariantMap{
        {QStringLiteral("kind"), QStringLiteral("prior-planning-note")},
        {QStringLiteral("index"), i},
        {QStringLiteral("detail"), QStringLiteral("completed note %1").arg(i)},
    });
  }
  const auto long_history_plan = plan_with_app09_budget_model(request);
  request.prior_tool_results = saved_prior_tool_results;
  const auto long_history_prompt = long_history_plan.prompt_audit
      .value(QStringLiteral("userPrompt")).toString();
  const auto long_history_start = long_history_prompt.lastIndexOf(history_label);
  const auto long_history_bytes = long_history_prompt.mid(long_history_start + history_label.size()).toUtf8();
  const auto long_history = QJsonDocument::fromJson(long_history_bytes).object();
  const auto long_history_snapshots = long_history.value(QStringLiteral("approvedTaskSnapshots")).toArray();
  const auto long_history_snapshot = long_history_snapshots.isEmpty()
      ? QJsonObject{} : long_history_snapshots.first().toObject()
            .value(QStringLiteral("snapshot")).toObject();
  const auto long_history_components = long_history_plan.prompt_audit
      .value(QStringLiteral("promptComponents")).toMap();
  if (!long_history_plan.ok ||
      long_history_components.value(QStringLiteral("contextTokenLimit")).toInt() != 24576 ||
      long_history_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() > 24576 ||
      long_history_start < 0 ||
      long_history.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-task-tool-context-v1") ||
      long_history_snapshots.size() != 1 ||
      long_history_snapshot.value(QStringLiteral("approvedInstructions")).toString() !=
          QStringLiteral("Create the approved editable travel cover using three text layers. Choose a native font family and face from one inspected fontFaces entry and set both explicitly.") ||
      !long_history_snapshot.value(QStringLiteral("targetIds")).toArray()
           .contains(QStringLiteral("design-sample")) || long_history_bytes.size() > 32000)
    return fail(qPrintable(QStringLiteral(
        "Long tool histories must retain the latest frozen approved-task snapshot within the bounded prompt; context=%1")
        .arg(context_budget_summary(long_history_plan))));
  const auto composition_payload = fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"));
  const auto workmanship_prompt = composition_tool_result.prompt_audit.value(QStringLiteral("systemPrompt")).toString();
  if (!workmanship_prompt.contains(QStringLiteral("looksy-workmanship-v1")) ||
      !workmanship_prompt.contains(QStringLiteral("app.command_schema")) ||
      !workmanship_prompt.contains(QStringLiteral("durable save, reopen and export")) ||
      composition_prompt.contains(QStringLiteral("use the available recipe-patch schema to create editable graphics.textLayers")) ||
      composition_prompt.contains(QStringLiteral("call photo.get_recipe_patch_schema before planning")))
    return fail("Construction must use discovered typed tools, rendered repair and truthful delivery without conflicting patch instructions");
  if (workmanship_prompt.contains(QStringLiteral("latest frozen approved-task snapshot")) ||
      composition_prompt.contains(QStringLiteral("approvedTaskSnapshots")))
    return fail("Planner must not imply prior task approval when no approved snapshot was supplied");
  const auto composition_format = composition_payload.value(QStringLiteral("format")).toObject();
  const auto composition_edit_intent_branch = schema_branch_for_decision(
      composition_format, QStringLiteral("edit_intent"));
  const auto composition_tool_branch = schema_branch_for_decision(
      composition_format, QStringLiteral("tool_call_plan"));
  const auto composition_needs_input_branch = schema_branch_for_decision(
      composition_format, QStringLiteral("needs_user_input"));
  const auto composition_cannot_complete_branch = schema_branch_for_decision(
      composition_format, QStringLiteral("cannot_complete_safely"));
  const auto composition_edit_branch_properties = composition_edit_intent_branch
      .value(QStringLiteral("properties")).toObject();
  const auto composition_edit_intent_schema = composition_edit_branch_properties
      .value(QStringLiteral("editIntent")).toObject();
  const auto composition_edit_intent_properties = composition_edit_intent_schema
      .value(QStringLiteral("properties")).toObject();
  const auto composition_tool_branch_properties = composition_tool_branch
      .value(QStringLiteral("properties")).toObject();
  const auto composition_needs_input_properties = composition_needs_input_branch
      .value(QStringLiteral("properties")).toObject();
  const auto composition_cannot_complete_properties = composition_cannot_complete_branch
      .value(QStringLiteral("properties")).toObject();
  const auto composition_tool_calls_schema = composition_tool_branch_properties
      .value(QStringLiteral("toolCalls")).toObject();
  const auto branch_tool_calls = composition_tool_calls_schema;
  const auto branch_tool_call_variants = branch_tool_calls.value(QStringLiteral("items")).toObject()
      .value(QStringLiteral("oneOf")).toArray();
  const auto composition_tool_call_item_schema = branch_tool_calls.value(QStringLiteral("items")).toObject();
  const auto typed_call_variants = composition_tool_call_item_schema.value(QStringLiteral("oneOf")).toArray();
  QJsonArray operation_argument_variants;
  for (const auto& call_variant_value : typed_call_variants)
    operation_argument_variants.append(call_variant_value.toObject()
        .value(QStringLiteral("properties")).toObject()
        .value(QStringLiteral("arguments")).toObject());
  const QJsonObject flattened_composition_schema{
      {QStringLiteral("oneOf"), operation_argument_variants}};
  const auto native_composition_schema_bytes = QJsonDocument(typed_composition_input_schema)
      .toJson(QJsonDocument::Compact).size();
  const auto generated_composition_schema_bytes = QJsonDocument(flattened_composition_schema)
      .toJson(QJsonDocument::Compact).size();
  const auto typography_argument_schema = schema_branch_for_composition_operation(
      flattened_composition_schema, QStringLiteral("typography"));
  const auto transform_argument_schema = schema_branch_for_composition_operation(
      flattened_composition_schema, QStringLiteral("transform"));
  const auto add_text_argument_schema = schema_branch_for_composition_operation(
      flattened_composition_schema, QStringLiteral("add_text"));
  const auto paint_argument_schema = schema_branch_for_composition_operation(
      flattened_composition_schema, QStringLiteral("paint"));
  const auto typography_values_schema = typography_argument_schema.value(QStringLiteral("properties"))
      .toObject().value(QStringLiteral("values")).toObject();
  const auto transform_values_schema = transform_argument_schema.value(QStringLiteral("properties"))
      .toObject().value(QStringLiteral("values")).toObject();
  const auto add_text_values_schema = add_text_argument_schema.value(QStringLiteral("properties"))
      .toObject().value(QStringLiteral("values")).toObject();
  const auto paint_values_schema = paint_argument_schema.value(QStringLiteral("properties"))
      .toObject().value(QStringLiteral("values")).toObject();
  if (fake_ollama_server_available &&
      (composition_format.contains(QStringLiteral("properties")) ||
       typed_call_variants.size() != 5 ||
       schema_has_properties_alongside_union(composition_format) ||
       std::any_of(typed_call_variants.cbegin(), typed_call_variants.cend(), [](const QJsonValue& value) {
         const auto properties = value.toObject().value(QStringLiteral("properties")).toObject();
         return properties.value(QStringLiteral("name")).toObject()
                    .value(QStringLiteral("enum")).toArray() !=
                    QJsonArray{QStringLiteral("photo.edit_composition")} ||
             properties.value(QStringLiteral("arguments")).toObject()
                    .value(QStringLiteral("type")).toString() != QStringLiteral("object");
       })))
    return fail("The outbound decoder schema must use pure operation-discriminated branches for the supplied typed tool");
  QStringList composition_schema_failures;
  const auto record_schema_check = [&composition_schema_failures](bool passed, const QString& name) {
    if (!passed) composition_schema_failures.append(name);
  };
  record_schema_check(flattened_composition_schema.value(QStringLiteral("oneOf")).toArray().size() == 5,
      QStringLiteral("operation-variant-count"));
  record_schema_check(generated_composition_schema_bytes <= native_composition_schema_bytes * 2,
      QStringLiteral("schema-size-bound"));
  record_schema_check(!typography_argument_schema.isEmpty(), QStringLiteral("typography-branch"));
  record_schema_check(!transform_argument_schema.isEmpty(), QStringLiteral("transform-branch"));
  record_schema_check(!paint_argument_schema.isEmpty(), QStringLiteral("paint-branch"));
  record_schema_check(typography_argument_schema.value(QStringLiteral("required")).toArray()
      .contains(QStringLiteral("layerId")), QStringLiteral("typography-layerId-required"));
  record_schema_check(typography_argument_schema.value(QStringLiteral("properties")).toObject()
      .value(QStringLiteral("layerId")).toObject().value(QStringLiteral("minLength")).toInt() == 1,
      QStringLiteral("typography-layerId-minLength"));
  record_schema_check(typography_argument_schema.value(QStringLiteral("properties")).toObject()
      .value(QStringLiteral("layerId")).toObject().value(QStringLiteral("pattern")).toString() == QStringLiteral("\\S"),
      QStringLiteral("typography-layerId-pattern"));
  record_schema_check(typography_values_schema.value(QStringLiteral("additionalProperties")).isBool(),
      QStringLiteral("typography-values-additionalProperties-type"));
  record_schema_check(!typography_values_schema.value(QStringLiteral("additionalProperties")).toBool(),
      QStringLiteral("typography-values-closed"));
  record_schema_check(!typography_values_schema.value(QStringLiteral("properties")).toObject()
      .contains(QStringLiteral("x")) && !typography_values_schema.value(QStringLiteral("properties"))
      .toObject().contains(QStringLiteral("y")), QStringLiteral("typography-no-x-y"));
  record_schema_check(typography_values_schema.value(QStringLiteral("properties")).toObject()
      .contains(QStringLiteral("size")), QStringLiteral("typography-size-property"));
  record_schema_check(typography_values_schema.value(QStringLiteral("required")).toArray()
      .contains(QStringLiteral("family")), QStringLiteral("explicit-typography-family-required"));
  record_schema_check(typography_values_schema.value(QStringLiteral("required")).toArray()
      .contains(QStringLiteral("face")), QStringLiteral("explicit-typography-face-required"));
  record_schema_check(transform_argument_schema.value(QStringLiteral("required")).toArray()
      .contains(QStringLiteral("layerId")), QStringLiteral("transform-layerId-required"));
  record_schema_check(transform_values_schema.value(QStringLiteral("required")).toArray()
      .contains(QStringLiteral("rotation")), QStringLiteral("transform-rotation-required"));
  record_schema_check(transform_values_schema.value(QStringLiteral("properties")).toObject()
      .contains(QStringLiteral("rotation")), QStringLiteral("transform-rotation-property"));
  record_schema_check(add_text_values_schema.value(QStringLiteral("properties")).toObject()
      .contains(QStringLiteral("text")), QStringLiteral("add-text-text-property"));
  record_schema_check(add_text_values_schema.value(QStringLiteral("required")).toArray()
      .contains(QStringLiteral("text")), QStringLiteral("add-text-text-required"));
  record_schema_check(paint_values_schema.value(QStringLiteral("properties")).toObject()
      .contains(QStringLiteral("points")), QStringLiteral("paint-points-property"));
  record_schema_check(!paint_values_schema.value(QStringLiteral("properties")).toObject()
      .contains(QStringLiteral("clipSelection")), QStringLiteral("paint-optional-control-stays-open"));
  record_schema_check(paint_values_schema.value(QStringLiteral("required")).toArray()
      .contains(QStringLiteral("points")), QStringLiteral("paint-points-required"));
  record_schema_check(add_text_values_schema.value(QStringLiteral("additionalProperties")).toBool(),
      QStringLiteral("add-text-values-open"));
  record_schema_check(paint_values_schema.value(QStringLiteral("additionalProperties")).toBool(),
      QStringLiteral("paint-values-open"));
  record_schema_check(!add_text_values_schema.value(QStringLiteral("properties")).toObject()
      .value(QStringLiteral("text")).toObject().contains(QStringLiteral("description")),
      QStringLiteral("decoder-schema-annotation-removed"));
  for (const auto& field : {QStringLiteral("x"), QStringLiteral("y"),
           QStringLiteral("size"), QStringLiteral("scale"), QStringLiteral("rotation")})
    record_schema_check(!add_text_values_schema.value(QStringLiteral("properties")).toObject()
        .value(field).toObject().contains(QStringLiteral("description")),
        QStringLiteral("decoder-property-description-removed-%1").arg(field));
  record_schema_check(!add_text_argument_schema.value(QStringLiteral("properties")).toObject()
      .value(QStringLiteral("assetId")).toObject().contains(QStringLiteral("description")),
      QStringLiteral("decoder-schema-asset-annotation-removed"));
  const auto tool_catalog_start = composition_prompt.indexOf(QStringLiteral("Available MCP tool schemas JSON:\n"));
  const auto tool_catalog_end = composition_prompt.indexOf(
      QStringLiteral("Current rendered planning evidence JSON:\n"), tool_catalog_start);
  const auto tool_catalog_bytes = tool_catalog_start < 0 || tool_catalog_end < tool_catalog_start
      ? QByteArray{} : composition_prompt.mid(
          tool_catalog_start + QStringLiteral("Available MCP tool schemas JSON:\n").size(),
          tool_catalog_end - tool_catalog_start - QStringLiteral("Available MCP tool schemas JSON:\n").size())
              .toUtf8();
  const auto tool_catalog = QJsonDocument::fromJson(tool_catalog_bytes).array();
  QJsonObject composition_catalog_guide;
  for (const auto& tool_value : tool_catalog) {
    const auto tool = tool_value.toObject();
    if (tool.value(QStringLiteral("name")).toString() == QStringLiteral("photo.edit_composition"))
      composition_catalog_guide = tool.value(QStringLiteral("argumentGuide")).toObject();
  }
  const auto catalog_values_properties = composition_catalog_guide
      .value(QStringLiteral("fieldSemantics")).toObject();
  record_schema_check(!composition_catalog_guide.isEmpty(), QStringLiteral("prompt-catalog-has-composition-argument-guide"));
  record_schema_check(catalog_values_properties.value(QStringLiteral("values.description")).toObject()
      .value(QStringLiteral("type")).toString() == QStringLiteral("string"),
      QStringLiteral("argument-property-named-description-retained"));
  record_schema_check(catalog_values_properties.value(QStringLiteral("values.title")).toObject()
      .value(QStringLiteral("type")).toString() == QStringLiteral("string"),
      QStringLiteral("argument-property-named-title-retained"));
  record_schema_check(catalog_values_properties.value(QStringLiteral("values.default")).toObject()
      .value(QStringLiteral("type")).toString() == QStringLiteral("string"),
      QStringLiteral("argument-property-named-default-retained"));
  record_schema_check(catalog_values_properties.value(QStringLiteral("values.description")).toObject()
      .value(QStringLiteral("description")).toString() ==
          QStringLiteral("A real optional argument named description."),
      QStringLiteral("prompt-catalog-preserves-description-annotation"));
  record_schema_check(!catalog_values_properties.value(QStringLiteral("values.title")).toObject().contains(QStringLiteral("title")) &&
      !catalog_values_properties.value(QStringLiteral("values.default")).toObject().contains(QStringLiteral("default")),
      QStringLiteral("prompt-catalog-removes-nonsemantic-schema-annotations"));
  const auto x_description = catalog_values_properties.value(QStringLiteral("values.x")).toObject()
      .value(QStringLiteral("description")).toString();
  const auto y_description = catalog_values_properties.value(QStringLiteral("values.y")).toObject()
      .value(QStringLiteral("description")).toString();
  const auto size_description = catalog_values_properties.value(QStringLiteral("values.size")).toObject()
      .value(QStringLiteral("description")).toString();
  const auto scale_description = catalog_values_properties.value(QStringLiteral("values.scale")).toObject()
      .value(QStringLiteral("description")).toString();
  const auto rotation_description = catalog_values_properties.value(QStringLiteral("values.rotation")).toObject()
      .value(QStringLiteral("description")).toString();
  record_schema_check(x_description.contains(QStringLiteral("canvas-pixel coordinate")) &&
      x_description.contains(QStringLiteral("add_text places the text origin")) &&
      y_description == x_description,
      QStringLiteral("prompt-catalog-preserves-coordinate-units-and-origins"));
  record_schema_check(size_description.contains(QStringLiteral("document pixels")) &&
      scale_description.contains(QStringLiteral("0.05 through 32")) &&
      rotation_description.contains(QStringLiteral("degrees about its center")) &&
      rotation_description.contains(QStringLiteral("-3600 through 3600")),
      QStringLiteral("prompt-catalog-preserves-typography-and-transform-units"));
  if (fake_ollama_server_available && !composition_schema_failures.isEmpty()) {
    const auto failure_message = QStringLiteral("Outbound composition schema contract failed: %1")
        .arg(composition_schema_failures.join(QStringLiteral(", "))).toUtf8();
    return fail(failure_message.constData());
  }
  if (fake_ollama_server_available &&
      (!composition_edit_intent_branch.value(QStringLiteral("required")).toArray().contains(QStringLiteral("editIntent")) ||
       !composition_edit_intent_branch.value(QStringLiteral("required")).toArray().contains(QStringLiteral("styleConstraintCitations")) ||
       !composition_edit_intent_branch.value(QStringLiteral("required")).toArray().contains(QStringLiteral("decision")) ||
       !composition_edit_intent_branch.value(QStringLiteral("required")).toArray().contains(QStringLiteral("summary")) ||
       composition_edit_branch_properties.value(QStringLiteral("editIntent")).toObject()
           .value(QStringLiteral("properties")).toObject().value(QStringLiteral("palette")).toObject()
               .value(QStringLiteral("enum")).toArray().isEmpty() ||
       composition_edit_branch_properties.value(QStringLiteral("editIntent")).toObject()
           .value(QStringLiteral("properties")).toObject()
           .value(QStringLiteral("completionStatus")).toObject()
           .value(QStringLiteral("enum")).toArray() !=
               QJsonArray{QStringLiteral("in_progress"), QStringLiteral("complete")} ||
       !composition_edit_intent_schema.value(QStringLiteral("required")).toArray()
            .contains(QStringLiteral("completionStatus")) ||
       !composition_tool_branch.value(QStringLiteral("required")).toArray().contains(QStringLiteral("toolCalls")) ||
       !composition_tool_branch.value(QStringLiteral("required")).toArray().contains(QStringLiteral("decision")) ||
       !composition_tool_branch.value(QStringLiteral("required")).toArray().contains(QStringLiteral("summary")) ||
       branch_tool_calls.value(QStringLiteral("minItems")).toInt() != 1 ||
       branch_tool_call_variants.size() != 5 ||
       branch_tool_call_variants.first().toObject().value(QStringLiteral("properties")).toObject()
           .value(QStringLiteral("name")).toObject().value(QStringLiteral("enum")).toArray() !=
               QJsonArray{QStringLiteral("photo.edit_composition")} ||
       !composition_needs_input_branch.value(QStringLiteral("required")).toArray().contains(QStringLiteral("userQuestions")) ||
       !composition_needs_input_branch.value(QStringLiteral("required")).toArray().contains(QStringLiteral("decision")) ||
       composition_needs_input_properties.value(QStringLiteral("userQuestions")).toObject()
           .value(QStringLiteral("items")).toObject().value(QStringLiteral("minLength")).toInt() != 1 ||
       !composition_cannot_complete_branch.value(QStringLiteral("required")).toArray().contains(QStringLiteral("reason")) ||
       !composition_cannot_complete_branch.value(QStringLiteral("required")).toArray().contains(QStringLiteral("decision")) ||
       composition_cannot_complete_properties.value(QStringLiteral("reason")).toObject()
           .value(QStringLiteral("minLength")).toInt() != 1 ||
       composition_tool_calls_schema.value(QStringLiteral("minItems")).toInt() != 1 ||
       composition_edit_branch_properties.value(QStringLiteral("styleConstraintCitations")).toObject()
           .value(QStringLiteral("minItems")).toInt() != 1 ||
       composition_needs_input_properties.value(QStringLiteral("userQuestions")).toObject()
           .value(QStringLiteral("minItems")).toInt() != 1 ||
       composition_needs_input_properties.value(QStringLiteral("userQuestions")).toObject()
           .value(QStringLiteral("items")).toObject().value(QStringLiteral("minLength")).toInt() != 1 ||
       composition_cannot_complete_properties.value(QStringLiteral("reason")).toObject()
           .value(QStringLiteral("minLength")).toInt() != 1 ||
       !composition_edit_intent_schema.value(QStringLiteral("required")).toArray().contains(QStringLiteral("goalSummary")) ||
       composition_edit_intent_schema.value(QStringLiteral("properties")).toObject()
           .value(QStringLiteral("goalSummary")).toObject().value(QStringLiteral("minLength")).toInt() != 1 ||
       composition_edit_intent_properties.value(QStringLiteral("colorMode")).toObject()
           .value(QStringLiteral("enum")).toArray().isEmpty() ||
       !composition_edit_intent_properties.value(QStringLiteral("palette")).toObject()
           .value(QStringLiteral("enum")).toArray().contains(QStringLiteral("warm")) ||
       composition_edit_intent_properties.value(QStringLiteral("saturation")).toObject()
           .value(QStringLiteral("type")).toString() != QStringLiteral("string") ||
       composition_edit_intent_properties.value(QStringLiteral("referenceStrength")).toObject()
           .value(QStringLiteral("type")).toString() != QStringLiteral("number") ||
       composition_edit_intent_properties.value(QStringLiteral("requestedActions")).toObject()
           .value(QStringLiteral("items")).toObject().value(QStringLiteral("type")).toString() != QStringLiteral("string") ||
       !composition_edit_intent_schema.value(QStringLiteral("required")).toArray().contains(QStringLiteral("globalChangePolicy")) ||
       !composition_edit_intent_schema.value(QStringLiteral("required")).toArray().contains(QStringLiteral("visualFindings")) ||
       !composition_edit_intent_schema.value(QStringLiteral("required")).toArray().contains(QStringLiteral("localizedActions")) ||
       !composition_edit_intent_schema.value(QStringLiteral("required")).toArray().contains(QStringLiteral("retouchActions")) ||
       composition_edit_intent_properties.value(QStringLiteral("visualFindings")).toObject()
           .value(QStringLiteral("type")).toString() != QStringLiteral("array") ||
       composition_edit_intent_properties.value(QStringLiteral("localizedActions")).toObject()
           .value(QStringLiteral("type")).toString() != QStringLiteral("array") ||
       composition_edit_intent_properties.value(QStringLiteral("retouchActions")).toObject()
           .value(QStringLiteral("type")).toString() != QStringLiteral("array")))
    return fail("Tool-enabled generation must require typed intent and action arrays while exposing the existing photo intent controls");
  const auto composition_messages = composition_payload.value(QStringLiteral("messages")).toArray();
  const auto composition_system_prompt = composition_messages.isEmpty()
      ? QString{} : composition_messages.first().toObject().value(QStringLiteral("content")).toString();
  if (!composition_tool_result.ok || composition_tool_result.decision != QStringLiteral("tool_call_plan") ||
      composition_tool_result.tool_call_plan.isEmpty() ||
      composition_tool_result.tool_call_plan.first().toMap().value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition") ||
      (fake_ollama_server_available &&
       (!composition_system_prompt.contains(QStringLiteral("return decision=tool_call_plan and invoke that tool")) ||
        !composition_system_prompt.contains(QStringLiteral("do not substitute edit_intent")) ||
        !composition_system_prompt.contains(QStringLiteral("Serialize photo.edit_composition arguments in the supplied schema order: assetId, layerId when required, operation, values")) ||
        !composition_system_prompt.contains(QStringLiteral("use them directly and include dependent edits in order")) ||
        !composition_system_prompt.contains(QStringLiteral("request another necessary tool round only")) ||
        composition_tool_calls_schema.value(QStringLiteral("type")).toString() != QStringLiteral("array") ||
        composition_tool_call_item_schema.contains(QStringLiteral("properties")) ||
        composition_tool_call_item_schema.value(QStringLiteral("oneOf")).toArray().size() != 5))) {
    std::cerr << "composition route audit="
              << QJsonDocument::fromVariant(QVariantMap{
                  {QStringLiteral("ok"), composition_tool_result.ok},
                  {QStringLiteral("decision"), composition_tool_result.decision},
                  {QStringLiteral("toolCalls"), composition_tool_result.tool_call_plan},
                  {QStringLiteral("systemPrompt"), composition_system_prompt}})
                     .toJson(QJsonDocument::Compact).toStdString() << '\n';
  }
  if (!saved_fake_response.isEmpty())
    qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON", saved_fake_response);
  if (!composition_tool_result.ok ||
      composition_tool_result.decision != QStringLiteral("tool_call_plan") ||
      composition_tool_result.tool_call_plan.first().toMap()
          .value(QStringLiteral("name")).toString() != QStringLiteral("photo.edit_composition")) {
    return fail("Explicit composition requests must route through typed MCP tools instead of photo-tone edit intent");
  }
  const auto edit_only_schemas = request.available_tool_schemas;
  request.available_tool_schemas.append(QVariantMap{
      {QStringLiteral("name"), QStringLiteral("photo.inspect_composition")},
      {QStringLiteral("inputSchema"), QVariantMap{{QStringLiteral("type"), QStringLiteral("object")}}}});
  const auto inspection_schemas = request.available_tool_schemas;
  const QJsonObject inspect_arguments{{QStringLiteral("assetId"), QStringLiteral("asset-0928f1bcb62c480b")},
      {QStringLiteral("operation"), QStringLiteral("inspect")}, {QStringLiteral("values"), QJsonObject{}}};
  auto inspection_plan = [&](const QJsonObject& arguments) {
    auto response = fake_composition_tool_response;
    auto content = composition_tool_plan;
    auto call = content.value(QStringLiteral("toolCalls")).toArray().first().toObject();
    call.insert(QStringLiteral("arguments"), arguments);
    content.insert(QStringLiteral("toolCalls"), QJsonArray{call});
    auto message = response.value(QStringLiteral("message")).toObject();
    message.insert(QStringLiteral("content"), QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact)));
    response.insert(QStringLiteral("message"), message);
    if (fake_ollama_server_available)
      fake_ollama_server.setResponse(QStringLiteral("/api/chat"), response);
    else
      qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
          QJsonDocument(response).toJson(QJsonDocument::Compact));
    return photo_editor::desktop::plan_agent_recipe_patch(request);
  };
  auto native_inspection_plan = [&](const QJsonObject& arguments, const bool use_app09_budget_model = false) {
    auto response = fake_composition_tool_response;
    auto content = composition_tool_plan;
    auto call = content.value(QStringLiteral("toolCalls")).toArray().first().toObject();
    call.insert(QStringLiteral("name"), QStringLiteral("photo.inspect_composition"));
    call.insert(QStringLiteral("arguments"), arguments);
    content.insert(QStringLiteral("toolCalls"), QJsonArray{call});
    auto message = response.value(QStringLiteral("message")).toObject();
    message.insert(QStringLiteral("content"), QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact)));
    response.insert(QStringLiteral("message"), message);
    const auto saved_native_inspection_fake_response =
        qgetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
    if (fake_ollama_server_available) {
      fake_ollama_server.setResponse(QStringLiteral("/api/chat"), response);
      qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
    } else {
      qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
          QJsonDocument(response).toJson(QJsonDocument::Compact));
    }
    const auto plan = use_app09_budget_model
        ? plan_with_app09_budget_model(request)
        : photo_editor::desktop::plan_agent_recipe_patch(request);
    if (fake_ollama_server_available && !saved_native_inspection_fake_response.isEmpty())
      qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON", saved_native_inspection_fake_response);
    return plan;
  };
  request.available_tool_schemas = inspection_schemas;
  const auto saved_revision_start_history = request.prior_tool_results;
  const auto saved_revision_start_asset = request.asset;
  request.asset.insert(QStringLiteral("assetId"), QStringLiteral("design-sample"));
  request.prior_tool_results = QVariantList{QVariantMap{
      {QStringLiteral("kind"), QStringLiteral("draft-revision")},
      {QStringLiteral("context"), QVariantMap{{QStringLiteral("revisionCompositionBaseline"),
          current_composition_assessment.value(QStringLiteral("revisionBaseline"))}}},
  }};
  const auto initial_revision_context_plan = native_inspection_plan(QJsonObject{
      {QStringLiteral("assetId"), QStringLiteral("design-sample")}});
  request.prior_tool_results = saved_revision_start_history;
  request.asset = saved_revision_start_asset;
  const auto initial_revision_system = initial_revision_context_plan.prompt_audit
      .value(QStringLiteral("systemPrompt")).toString();
  const auto initial_revision_user = initial_revision_context_plan.prompt_audit
      .value(QStringLiteral("userPrompt")).toString();
  if (!initial_revision_context_plan.ok ||
      !initial_revision_system.contains(QStringLiteral("A matching revisionBaseline from the prior accepted native inspection")) ||
      !initial_revision_system.contains(QStringLiteral("apply each requested relative correction once")) ||
      !initial_revision_user.contains(QStringLiteral("cover-headline")) ||
      !initial_revision_user.contains(QStringLiteral("AFTER THE LIGHT")))
    return fail("A draft-revision request must carry its accepted native-inspection baseline into prompt context before any new assessment exists");

  const auto prior_before_font_guidance = request.prior_tool_results;
  request.prior_tool_results.clear();
  const auto inspection_guidance_plan = native_inspection_plan(QJsonObject{
      {QStringLiteral("assetId"), QStringLiteral("asset-0928f1bcb62c480b")}});
  request.prior_tool_results = prior_before_font_guidance;
  const auto inspection_guidance_system = inspection_guidance_plan.prompt_audit
      .value(QStringLiteral("systemPrompt")).toString();
  const bool has_inventory_read_guidance = inspection_guidance_system.contains(
      QStringLiteral("one initial no-values photo.inspect_composition inventory read per asset"));
  const bool blocks_candidate_probing = inspection_guidance_system.contains(
      QStringLiteral("Do not use inspect values to probe alternative candidates"));
  const bool permits_state_readback = inspection_guidance_system.contains(
      QStringLiteral("After a mutation, a fresh photo.inspect_composition readback for the changed layer IDs is mandatory"));
  const bool uses_family_face_inventory = inspection_guidance_system.contains(
      QStringLiteral("fontFaces entries shaped as {family, faces}"));
  const bool permits_static_font_axes = inspection_guidance_system.contains(
      QStringLiteral("empty fontAxes list can describe a valid static font"));
  const bool has_unapproved_snapshot_instruction = inspection_guidance_system.contains(
      QStringLiteral("latest frozen approved-task snapshot"));
  const auto inspection_guidance_user = inspection_guidance_plan.prompt_audit
      .value(QStringLiteral("userPrompt")).toString();
  const bool requires_explicit_completion_status = inspection_guidance_user.contains(
      QStringLiteral("editIntent.completionStatus is required"));
  const bool prevents_passive_in_progress = inspection_guidance_user.contains(
      QStringLiteral("Do not return a passive in_progress intent while a supplied tool can finish"));
  const bool requires_fresh_post_mutation_readback = inspection_guidance_user.contains(
      QStringLiteral("use the subsequent fresh photo.inspect_composition result to check the changed layers before deciding that work is done"));
  if (!inspection_guidance_plan.ok ||
      !has_inventory_read_guidance || !blocks_candidate_probing || !permits_state_readback ||
      !uses_family_face_inventory || !permits_static_font_axes || has_unapproved_snapshot_instruction ||
      !requires_explicit_completion_status || !prevents_passive_in_progress ||
      !requires_fresh_post_mutation_readback) {
    QVariantList available_tool_names;
    for (const auto& schema : request.available_tool_schemas)
      available_tool_names.append(schema.toMap().value(QStringLiteral("name")));
    std::cerr << "font discovery audit=" << QJsonDocument::fromVariant(QVariantMap{
        {QStringLiteral("ok"), inspection_guidance_plan.ok},
        {QStringLiteral("decision"), inspection_guidance_plan.decision},
        {QStringLiteral("error"), inspection_guidance_plan.error_message},
        {QStringLiteral("availableToolNames"), available_tool_names},
        {QStringLiteral("hasInventoryReadGuidance"), has_inventory_read_guidance},
        {QStringLiteral("blocksCandidateProbing"), blocks_candidate_probing},
        {QStringLiteral("permitsStateReadback"), permits_state_readback},
        {QStringLiteral("usesFamilyFaceInventory"), uses_family_face_inventory},
        {QStringLiteral("permitsStaticFontAxes"), permits_static_font_axes},
        {QStringLiteral("hasUnapprovedSnapshotInstruction"), has_unapproved_snapshot_instruction},
        {QStringLiteral("requiresCompletionStatus"), requires_explicit_completion_status},
        {QStringLiteral("preventsPassiveInProgress"), prevents_passive_in_progress},
        {QStringLiteral("requiresFreshPostMutationReadback"), requires_fresh_post_mutation_readback},
        {QStringLiteral("systemPromptBytes"), inspection_guidance_system.toUtf8().size()},
    }).toJson(QJsonDocument::Compact).toStdString() << '\n';
    return fail("Font discovery must use one read-only inspection and must not imply an absent approval snapshot");
  }

  const auto prior_before_approved_context = request.prior_tool_results;
  request.prior_tool_results = QVariantList{approved_snapshot_envelope};
  const auto approved_context_plan = native_inspection_plan(QJsonObject{
      {QStringLiteral("assetId"), QStringLiteral("asset-0928f1bcb62c480b")}});
  request.prior_tool_results = prior_before_approved_context;
  const auto approved_context_system = approved_context_plan.prompt_audit
      .value(QStringLiteral("systemPrompt")).toString();
  const auto approved_context_prompt = approved_context_plan.prompt_audit
      .value(QStringLiteral("userPrompt")).toString();
  const auto approved_context_history_start = approved_context_prompt.lastIndexOf(history_label);
  const auto approved_context_history = QJsonDocument::fromJson(
      approved_context_prompt.mid(approved_context_history_start + history_label.size()).toUtf8());
  const auto approved_context_entries = approved_context_history.array();
  const auto approved_context_envelope = approved_context_entries.isEmpty()
      ? QJsonObject{} : approved_context_entries.first().toObject();
  const auto approved_context_snapshot = approved_context_envelope
      .value(QStringLiteral("snapshot")).toObject();
  if (!approved_context_plan.ok || approved_context_history_start < 0 ||
      !approved_context_history.isArray() || approved_context_entries.size() != 1 ||
      approved_context_envelope.value(QStringLiteral("kind")).toString() !=
          QStringLiteral("approved-task-snapshot") ||
      approved_context_envelope.value(QStringLiteral("authority")).toString() !=
          QStringLiteral("approved-brief-and-deliverables") ||
      approved_context_snapshot.value(QStringLiteral("approvedInstructions")).toString() !=
          QStringLiteral("Create the approved editable travel cover using three text layers. Choose a native font family and face from one inspected fontFaces entry and set both explicitly.") ||
      approved_context_snapshot.value(QStringLiteral("targetIds")).toArray() !=
          QJsonArray{QStringLiteral("design-sample")} ||
      approved_context_snapshot.value(QStringLiteral("taskScopedCapabilities")).toArray() !=
          QJsonArray{QStringLiteral("graphics"), QStringLiteral("layers_luts")} ||
      !approved_context_snapshot.value(QStringLiteral("exactCopy")).toArray()
           .contains(QStringLiteral("AFTER THE LIGHT")) ||
      !approved_context_system.contains(QStringLiteral("Complete the already approved deliverables without asking for a second approval")) ||
      !approved_context_system.contains(QStringLiteral("do not put an already approved construction operation in editIntent.requiresApproval")) ||
      !approved_context_system.contains(QStringLiteral("does not authorize extra deliverables")) ||
      !approved_context_system.contains(QStringLiteral("This task's active instructions explicitly require a native font family and face")) ||
      !approved_context_system.contains(QStringLiteral("Serialize typography values in canonical schema order: color, face, family, then later properties")) ||
      !approved_context_system.contains(QStringLiteral("a summary does not apply a value")) ||
      !approved_context_system.contains(QStringLiteral("calculate the actual ratio from readback values and check the requested threshold exactly")))
    return fail("Only the exact frozen snapshot may remove a repeat approval question, and its approved scope must reach the prompt intact");

  auto untrusted_snapshot_envelope = approved_snapshot_envelope;
  untrusted_snapshot_envelope.insert(QStringLiteral("authority"), QStringLiteral("tool-result"));
  request.prior_tool_results = QVariantList{untrusted_snapshot_envelope};
  const auto untrusted_context_plan = native_inspection_plan(QJsonObject{
      {QStringLiteral("assetId"), QStringLiteral("asset-0928f1bcb62c480b")}});
  request.prior_tool_results = prior_before_approved_context;
  if (!untrusted_context_plan.ok ||
      untrusted_context_plan.prompt_audit.value(QStringLiteral("systemPrompt")).toString()
          .contains(QStringLiteral("latest frozen approved-task snapshot")) ||
      untrusted_context_plan.prompt_audit.value(QStringLiteral("userPrompt")).toString()
          .contains(QStringLiteral("approvedInstructions")))
    return fail("A snapshot-shaped prior result without the production authority marker must not grant task scope");

  QVariantList font_inventory;
  QVariantList font_names;
  for (int family_index = 0; family_index < 64; ++family_index) {
    const auto family = family_index == 0 ? QStringLiteral("Avenir Next")
        : family_index == 1 ? QStringLiteral("Georgia")
                            : QStringLiteral("Installed Family %1").arg(family_index);
    QStringList faces;
    for (int face_index = 0; face_index < 9; ++face_index)
      faces.append(QStringLiteral("Face %1").arg(face_index));
    font_inventory.append(QVariantMap{{QStringLiteral("family"), family},
        {QStringLiteral("faces"), faces}});
    font_names.append(family);
  }
  const QVariantMap display_text_policy{
      {QStringLiteral("renderColorDomain"), QStringLiteral("display-referred-srgb")},
      {QStringLiteral("displayReferredTextEligibility"), QStringLiteral("eligible")},
      {QStringLiteral("displayReferredTextEligibilityReason"), QStringLiteral("plain-root-text")},
      {QStringLiteral("displayReferredTextPolicyVersion"), QStringLiteral("display-referred-text-suffix-v1")},
  };
  const QVariantMap inventory_layer{
      {QStringLiteral("id"), QStringLiteral("cover-headline")},
      {QStringLiteral("kind"), QStringLiteral("text")},
      {QStringLiteral("text"), QStringLiteral("AFTER THE LIGHT")},
      {QStringLiteral("renderColorDomain"), QStringLiteral("display-referred-srgb")},
      {QStringLiteral("displayReferredTextEligibility"), QStringLiteral("eligible")},
      {QStringLiteral("displayReferredTextEligibilityReason"), QStringLiteral("plain-root-text")},
      {QStringLiteral("displayReferredTextPolicyVersion"), QStringLiteral("display-referred-text-suffix-v1")},
  };
  const auto font_history_envelope = [](const QString& tool_name, const QVariantMap& arguments,
                                         const QVariantMap& content) {
    return QVariantMap{
        {QStringLiteral("name"), tool_name},
        {QStringLiteral("arguments"), arguments},
        {QStringLiteral("result"), QVariantMap{{QStringLiteral("ok"), true},
            {QStringLiteral("structuredContent"), content}}},
    };
  };
  const QVariantMap initial_inventory_content{
      {QStringLiteral("assetId"), QStringLiteral("design-sample")},
      {QStringLiteral("width"), 1600}, {QStringLiteral("height"), 2400},
      {QStringLiteral("layers"), QVariantList{inventory_layer}},
      {QStringLiteral("fonts"), font_names},
      {QStringLiteral("fontFaces"), font_inventory},
      {QStringLiteral("displayTextColorPolicy"), display_text_policy},
  };
  const QVariantMap mutation_content{
      {QStringLiteral("assetId"), QStringLiteral("design-sample")},
      {QStringLiteral("width"), 1600}, {QStringLiteral("height"), 2400},
      {QStringLiteral("layers"), QVariantList{inventory_layer}},
      {QStringLiteral("fonts"), font_names},
      {QStringLiteral("fontFaces"), font_inventory},
      {QStringLiteral("displayTextColorPolicy"), display_text_policy},
      {QStringLiteral("createdLayerId"), QStringLiteral("cover-headline")},
  };
  const QVariantMap exact_font_probe_content{
      {QStringLiteral("assetId"), QStringLiteral("design-sample")},
      {QStringLiteral("width"), 1600}, {QStringLiteral("height"), 2400},
      {QStringLiteral("layers"), QVariantList{inventory_layer}},
      {QStringLiteral("fontFaces"), QVariantList{QVariantMap{
          {QStringLiteral("family"), QStringLiteral("Avenir Next")},
          {QStringLiteral("faces"), QStringList{QStringLiteral("Regular"), QStringLiteral("Bold")}}}}},
      {QStringLiteral("fontFamily"), QStringLiteral("Avenir Next")},
      {QStringLiteral("fontFace"), QStringLiteral("Bold")},
      {QStringLiteral("requestedFamilyFaces"), QStringList{QStringLiteral("Regular"), QStringLiteral("Bold")}},
      {QStringLiteral("fontFamilyAvailable"), true},
      {QStringLiteral("fontFaceAvailable"), true},
      {QStringLiteral("fontInspectionRequested"), true},
      {QStringLiteral("fontAxes"), QVariantList{}},
      {QStringLiteral("displayTextColorPolicy"), display_text_policy},
  };
  const auto prior_before_font_projection = request.prior_tool_results;
  const auto asset_before_font_projection = request.asset;
  request.asset.insert(QStringLiteral("assetId"), QStringLiteral("design-sample"));
  request.prior_tool_results = QVariantList{
      font_history_envelope(QStringLiteral("photo.inspect_composition"),
          QVariantMap{{QStringLiteral("assetId"), QStringLiteral("design-sample")}}, initial_inventory_content),
      font_history_envelope(QStringLiteral("photo.edit_composition"),
          QVariantMap{{QStringLiteral("assetId"), QStringLiteral("design-sample")},
              {QStringLiteral("operation"), QStringLiteral("typography")},
              {QStringLiteral("layerId"), QStringLiteral("cover-headline")}}, mutation_content),
      font_history_envelope(QStringLiteral("photo.inspect_composition"),
          QVariantMap{{QStringLiteral("assetId"), QStringLiteral("design-sample")},
              {QStringLiteral("values"), QVariantMap{{QStringLiteral("family"), QStringLiteral("Avenir Next")},
                  {QStringLiteral("face"), QStringLiteral("Bold")}}}}, exact_font_probe_content),
  };
  const auto font_projection_plan = native_inspection_plan(QJsonObject{
      {QStringLiteral("assetId"), QStringLiteral("asset-0928f1bcb62c480b")}}, true);
  request.prior_tool_results = prior_before_font_projection;
  const auto font_projection_prompt = font_projection_plan.prompt_audit
      .value(QStringLiteral("userPrompt")).toString();
  const auto font_projection_start = font_projection_prompt.lastIndexOf(history_label);
  const auto font_projection_bytes = font_projection_prompt
      .mid(font_projection_start + history_label.size()).toUtf8();
  const auto font_projection_history = QJsonDocument::fromJson(font_projection_bytes).array();
  const auto font_projection_payload = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto font_projection_generation_schema = QJsonObject::fromVariantMap(
      font_projection_plan.prompt_audit.value(QStringLiteral("generationSchema")).toMap());
  const auto bounded_font_typography_branch = schema_branch_for_tool_operation(
      font_projection_generation_schema,
      QStringLiteral("photo.edit_composition"), QStringLiteral("typography"));
  const auto bounded_font_values_properties = bounded_font_typography_branch
      .value(QStringLiteral("properties")).toObject()
      .value(QStringLiteral("values")).toObject()
      .value(QStringLiteral("properties")).toObject();
  QStringList projected_families;
  QStringList projected_faces;
  if (font_projection_history.size() == 3) {
    const auto inventory_content = font_projection_history.at(2).toObject()
        .value(QStringLiteral("result")).toObject()
        .value(QStringLiteral("structuredContent")).toObject();
    for (const auto& entry_value : inventory_content.value(QStringLiteral("fontFaces")).toArray()) {
      const auto entry = entry_value.toObject();
      const auto family = entry.value(QStringLiteral("family")).toString();
      if (!family.isEmpty()) projected_families.append(family);
      for (const auto& face_value : entry.value(QStringLiteral("faces")).toArray()) {
        const auto face = face_value.toString();
        if (!face.isEmpty() && !projected_faces.contains(face)) projected_faces.append(face);
      }
    }
  }
  QJsonArray expected_projected_families;
  QJsonArray expected_projected_faces;
  for (const auto& family : projected_families) expected_projected_families.append(family);
  for (const auto& face : projected_faces) expected_projected_faces.append(face);
  const auto latest_font_content = font_projection_history.size() == 3
      ? font_projection_history.last().toObject().value(QStringLiteral("result")).toObject()
          .value(QStringLiteral("structuredContent")).toObject()
      : QJsonObject{};
  const auto projected_font_inventory = latest_font_content.value(QStringLiteral("fontFaces")).toArray();
  const auto projected_text_layer = latest_font_content.value(QStringLiteral("layers")).toArray()
      .first().toObject();
  const auto projected_family = projected_font_inventory.isEmpty()
      ? QJsonObject{} : projected_font_inventory.first().toObject();
  const auto projected_font_inventory_bytes = QJsonDocument(projected_font_inventory)
      .toJson(QJsonDocument::Compact).size();
  const auto projected_family_enum = bounded_font_values_properties.value(QStringLiteral("family"))
      .toObject().value(QStringLiteral("enum")).toArray();
  const auto projected_face_enum = bounded_font_values_properties.value(QStringLiteral("face"))
      .toObject().value(QStringLiteral("enum")).toArray();
  const auto font_projection_components = font_projection_plan.prompt_audit
      .value(QStringLiteral("promptComponents")).toMap();
  QStringList font_projection_failures;
  const auto check_font_projection = [&font_projection_failures](bool passed, const QString& label) {
    if (!passed) font_projection_failures.append(label);
  };
  check_font_projection(font_projection_plan.ok, QStringLiteral("plan.ok"));
  check_font_projection(font_projection_components.value(QStringLiteral("contextTokenLimit")).toInt() == 24576 &&
      font_projection_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() <= 24576,
      QStringLiteral("contextBudget.selected24kModelFits"));
  check_font_projection(!fake_ollama_server_available ||
      font_projection_payload.value(QStringLiteral("format")).toObject() ==
          font_projection_generation_schema,
      QStringLiteral("transport.schemaMatchesGenerationAudit"));
  check_font_projection(font_projection_start >= 0, QStringLiteral("history.marker"));
  check_font_projection(font_projection_history.size() == 3, QStringLiteral("history.entryCount"));
  check_font_projection(font_projection_bytes.size() <= 32000, QStringLiteral("history.byteBudget"));
  check_font_projection(projected_font_inventory.size() == 48, QStringLiteral("inventory.familyCount"));
  check_font_projection(projected_font_inventory_bytes <= 12000, QStringLiteral("inventory.byteBudget"));
  check_font_projection(!projected_font_inventory.isEmpty() &&
      projected_font_inventory.last().toObject().value(QStringLiteral("family")).toString() ==
          QStringLiteral("Installed Family 47"), QStringLiteral("inventory.latestFamily"));
  check_font_projection(projected_family.value(QStringLiteral("family")).toString() ==
      QStringLiteral("Avenir Next"), QStringLiteral("inventory.firstFamily"));
  check_font_projection(projected_family.value(QStringLiteral("faces")).toArray().size() == 8,
      QStringLiteral("inventory.firstFamilyFaceCount"));
  check_font_projection(!latest_font_content.contains(QStringLiteral("fonts")),
      QStringLiteral("inventory.legacyFontsDropped"));
  check_font_projection(latest_font_content.value(QStringLiteral("fontFacesOmittedFamilyCount")).toInt() >= 16,
      QStringLiteral("inventory.omittedFamilyCount"));
  check_font_projection(latest_font_content.value(QStringLiteral("fontFacesOmittedFaceCount")).toInt() >= 48,
      QStringLiteral("inventory.omittedFaceCount"));
  check_font_projection(latest_font_content.value(QStringLiteral("fontFamily")).toString() ==
      QStringLiteral("Avenir Next"), QStringLiteral("fontProbe.family"));
  check_font_projection(latest_font_content.value(QStringLiteral("fontFace")).toString() ==
      QStringLiteral("Bold"), QStringLiteral("fontProbe.face"));
  check_font_projection(latest_font_content.value(QStringLiteral("requestedFamilyFaces")).toArray() ==
      QJsonArray{QStringLiteral("Regular"), QStringLiteral("Bold")}, QStringLiteral("fontProbe.requestedFaces"));
  check_font_projection(latest_font_content.value(QStringLiteral("fontFamilyAvailable")).toBool(),
      QStringLiteral("fontProbe.familyAvailable"));
  check_font_projection(latest_font_content.value(QStringLiteral("fontFaceAvailable")).toBool(),
      QStringLiteral("fontProbe.faceAvailable"));
  check_font_projection(latest_font_content.value(QStringLiteral("fontInspectionRequested")).toBool(),
      QStringLiteral("fontProbe.inspectionRequested"));
  check_font_projection(latest_font_content.value(QStringLiteral("fontAxes")).toArray().isEmpty(),
      QStringLiteral("fontProbe.axes"));
  check_font_projection(latest_font_content.value(QStringLiteral("displayTextColorPolicy")).toObject().size() == 4,
      QStringLiteral("textPolicy.fields"));
  check_font_projection(projected_family_enum == expected_projected_families,
      QStringLiteral("schema.familyEnumMatchesInventory"));
  check_font_projection(projected_face_enum == expected_projected_faces,
      QStringLiteral("schema.faceEnumMatchesInventory"));
  check_font_projection(projected_text_layer.value(QStringLiteral("displayReferredTextPolicyVersion")).toString() ==
      QStringLiteral("display-referred-text-suffix-v1"), QStringLiteral("textPolicy.layerVersion"));
  if (!font_projection_failures.isEmpty()) {
    const auto diagnostic = QStringLiteral(
        "Font inventory projection failed [%1]; historyBytes=%2, historyEntries=%3, "
        "families=%4, inventoryBytes=%5, omittedFamilies=%6, omittedFaces=%7, "
        "familySchemaValues=%8, faceSchemaValues=%9, policyFields=%10, "
        "authoritativeInspection=%11, layerGrounding=%12, groundedIds=%13, "
        "nativeFontInventory=%14, inventoryEntries=%15, typographyBranchEmpty=%16, context=%17")
        .arg(font_projection_failures.join(QStringLiteral(", ")))
        .arg(font_projection_bytes.size())
        .arg(font_projection_history.size())
        .arg(projected_font_inventory.size())
        .arg(projected_font_inventory_bytes)
        .arg(latest_font_content.value(QStringLiteral("fontFacesOmittedFamilyCount")).toInt())
        .arg(latest_font_content.value(QStringLiteral("fontFacesOmittedFaceCount")).toInt())
        .arg(projected_family_enum.size())
        .arg(projected_face_enum.size())
        .arg(latest_font_content.value(QStringLiteral("displayTextColorPolicy")).toObject().size())
        .arg(font_projection_plan.prompt_audit.value(QStringLiteral("compositionGroundingContext")).toMap()
                 .value(QStringLiteral("authoritativeInspectionPresent")).toBool())
        .arg(font_projection_plan.prompt_audit.value(QStringLiteral("compositionGroundingContext")).toMap()
                 .value(QStringLiteral("layerGroundingContextPresent")).toBool())
        .arg(font_projection_plan.prompt_audit.value(QStringLiteral("compositionGroundingContext")).toMap()
                 .value(QStringLiteral("groundedLayerIdCount")).toLongLong())
        .arg(font_projection_plan.prompt_audit.value(QStringLiteral("compositionGroundingContext")).toMap()
                 .value(QStringLiteral("nativeFontInventoryPresent")).toBool())
        .arg(font_projection_plan.prompt_audit.value(QStringLiteral("compositionGroundingContext")).toMap()
                 .value(QStringLiteral("nativeFontInventoryEntryCount")).toLongLong())
        .arg(bounded_font_typography_branch.isEmpty())
        .arg(context_budget_summary(font_projection_plan));
    return fail(qPrintable(diagnostic));
  }

  const auto retained_font_pair_content = QVariantMap{
      {QStringLiteral("assetId"), QStringLiteral("design-sample")},
      {QStringLiteral("width"), 1600}, {QStringLiteral("height"), 2400},
      {QStringLiteral("layers"), QVariantList{inventory_layer}},
      {QStringLiteral("fontFaces"), QVariantList{QVariantMap{
          {QStringLiteral("family"), QStringLiteral("Installed Family 63")},
          {QStringLiteral("faces"), QStringList{QStringLiteral("Face 8")}}}}},
      {QStringLiteral("fontFamily"), QStringLiteral("Installed Family 63")},
      {QStringLiteral("fontFace"), QStringLiteral("Face 8")},
      {QStringLiteral("requestedFamilyFaces"), QStringList{QStringLiteral("Face 8")}},
      {QStringLiteral("fontFamilyAvailable"), true},
      {QStringLiteral("fontFaceAvailable"), true},
      {QStringLiteral("fontInspectionRequested"), true},
      {QStringLiteral("fontAxes"), QVariantList{}},
  };
  const auto font_pair_original_instructions = request.user_instructions;
  request.user_instructions = QStringLiteral(
      "Use a native font family and face for the approved headline and set both explicitly.");
  request.prior_tool_results = QVariantList{
      font_history_envelope(QStringLiteral("photo.inspect_composition"),
          QVariantMap{{QStringLiteral("assetId"), QStringLiteral("design-sample")}}, initial_inventory_content),
      font_history_envelope(QStringLiteral("photo.inspect_composition"),
          QVariantMap{{QStringLiteral("assetId"), QStringLiteral("design-sample")},
              {QStringLiteral("values"), QVariantMap{
                  {QStringLiteral("family"), QStringLiteral("Installed Family 63")},
                  {QStringLiteral("face"), QStringLiteral("Face 8")}}}}, retained_font_pair_content),
  };
  const auto retained_font_pair_plan = native_inspection_plan(QJsonObject{
      {QStringLiteral("assetId"), QStringLiteral("asset-0928f1bcb62c480b")}}, true);
  request.prior_tool_results = prior_before_font_projection;
  const auto retained_font_pair_payload = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto retained_font_pair_branch = schema_branch_for_tool_operation(
      retained_font_pair_payload.value(QStringLiteral("format")).toObject(),
      QStringLiteral("photo.edit_composition"), QStringLiteral("typography"));
  const auto retained_font_pair_properties = retained_font_pair_branch
      .value(QStringLiteral("properties")).toObject()
      .value(QStringLiteral("values")).toObject()
      .value(QStringLiteral("properties")).toObject();
  const auto retained_family_enum = retained_font_pair_properties
      .value(QStringLiteral("family")).toObject().value(QStringLiteral("enum")).toArray();
  const auto retained_face_enum = retained_font_pair_properties
      .value(QStringLiteral("face")).toObject().value(QStringLiteral("enum")).toArray();
  const auto retained_font_pair_prompt = retained_font_pair_plan.prompt_audit
      .value(QStringLiteral("userPrompt")).toString();
  const auto retained_font_pair_components = retained_font_pair_plan.prompt_audit
      .value(QStringLiteral("promptComponents")).toMap();
  if (!retained_font_pair_plan.ok ||
      retained_font_pair_components.value(QStringLiteral("contextTokenLimit")).toInt() != 24576 ||
      retained_font_pair_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() > 24576 ||
      retained_family_enum.size() != 49 ||
      retained_face_enum.size() != 9 ||
      !retained_family_enum.contains(QStringLiteral("Installed Family 63")) ||
      !retained_face_enum.contains(QStringLiteral("Face 8")) ||
      retained_family_enum.contains(QStringLiteral("Installed Family 62")) ||
      !retained_font_pair_prompt.contains(QStringLiteral("Installed Family 63")) ||
      !retained_font_pair_prompt.contains(QStringLiteral("Face 8")))
    return fail(qPrintable(QStringLiteral(
        "The bounded visible font inventory must retain its size while an exact previously inspected family/face pair remains selectable and visible; context=%1")
        .arg(context_budget_summary(retained_font_pair_plan))));

  const auto parse_tool_enabled_content = [&](const QJsonObject& content,
                                               const bool use_app09_budget_model = false) {
    auto response = fake_composition_tool_response;
    auto message = response.value(QStringLiteral("message")).toObject();
    message.insert(QStringLiteral("content"), QString::fromUtf8(
        QJsonDocument(content).toJson(QJsonDocument::Compact)));
    response.insert(QStringLiteral("message"), message);
    if (fake_ollama_server_available) {
      fake_ollama_server.setResponse(QStringLiteral("/api/chat"), response);
      qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
    } else {
      qputenv(QStringLiteral("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON").toUtf8(),
          QJsonDocument(response).toJson(QJsonDocument::Compact));
    }
    return use_app09_budget_model
        ? plan_with_app09_budget_model(request)
        : photo_editor::desktop::plan_agent_recipe_patch(request);
  };
  request.prior_tool_results = QVariantList{font_history_envelope(
      QStringLiteral("photo.inspect_composition"),
      QVariantMap{{QStringLiteral("assetId"), QStringLiteral("design-sample")}},
      initial_inventory_content)};
  const QJsonObject full_inventory_typography_arguments{
      {QStringLiteral("assetId"), QStringLiteral("design-sample")},
      {QStringLiteral("layerId"), QStringLiteral("cover-headline")},
      {QStringLiteral("operation"), QStringLiteral("typography")},
      {QStringLiteral("values"), QJsonObject{
          {QStringLiteral("family"), QStringLiteral("Installed Family 63")},
          {QStringLiteral("face"), QStringLiteral("Face 8")}, {QStringLiteral("size"), 52}}},
  };
  const auto full_inventory_host_validation = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Use the exact native family and face from the full inspection inventory.")},
      {QStringLiteral("toolCalls"), QJsonArray{QJsonObject{
          {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), full_inventory_typography_arguments}}}},
  }, true);
  const auto full_inventory_payload = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto full_inventory_typography_branch = schema_branch_for_tool_operation(
      full_inventory_payload.value(QStringLiteral("format")).toObject(),
      QStringLiteral("photo.edit_composition"), QStringLiteral("typography"));
  const auto full_inventory_generation_properties = full_inventory_typography_branch
      .value(QStringLiteral("properties")).toObject()
      .value(QStringLiteral("values")).toObject()
      .value(QStringLiteral("properties")).toObject();
  const auto full_inventory_components = full_inventory_host_validation.prompt_audit
      .value(QStringLiteral("promptComponents")).toMap();
  if (!full_inventory_host_validation.ok ||
      full_inventory_components.value(QStringLiteral("contextTokenLimit")).toInt() != 24576 ||
      full_inventory_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() > 24576 ||
      full_inventory_generation_properties.value(QStringLiteral("family")).toObject()
          .value(QStringLiteral("enum")).toArray().contains(QStringLiteral("Installed Family 63")) ||
      full_inventory_generation_properties.value(QStringLiteral("face")).toObject()
          .value(QStringLiteral("enum")).toArray().contains(QStringLiteral("Face 8")) ||
      !full_inventory_host_validation.prompt_audit.value(QStringLiteral("toolCallValidation")).toMap()
          .value(QStringLiteral("valid")).toBool())
    return fail(qPrintable(QStringLiteral(
        "The unchanged host validator must accept an exact raw-inventory pair even when the bounded decoder omits an unrequested choice; context=%1")
        .arg(context_budget_summary(full_inventory_host_validation))));
  request.user_instructions = font_pair_original_instructions;
  request.prior_tool_results = prior_before_font_projection;
  request.asset = asset_before_font_projection;

  for (const auto& values : QJsonArray{QJsonObject{}, QJsonObject{{QStringLiteral("family"), QStringLiteral("Georgia")},
           {QStringLiteral("face"), QStringLiteral("Regular")}}}) {
    auto arguments = inspect_arguments;
    arguments.insert(QStringLiteral("values"), values);
    const auto result = inspection_plan(arguments);
    const auto call = result.tool_call_plan.value(0).toMap();
    const auto corrected_arguments = call.value(QStringLiteral("arguments")).toMap();
    if (!result.ok || call.value(QStringLiteral("name")).toString() != QStringLiteral("photo.inspect_composition") ||
        corrected_arguments.contains(QStringLiteral("operation")) ||
        corrected_arguments.value(QStringLiteral("assetId")).toString() != QStringLiteral("asset-0928f1bcb62c480b") ||
        (values.toObject().isEmpty() ? corrected_arguments.contains(QStringLiteral("values")) :
             corrected_arguments.value(QStringLiteral("values")).toMap() != values.toObject().toVariantMap()) ||
        result.prompt_audit.value(QStringLiteral("toolCallCorrections")).toList().size() != 1)
      return fail("Read-only composition aliases must become audited inspection calls without changing asset or font values");
  }
  auto no_values = inspect_arguments;
  no_values.remove(QStringLiteral("values"));
  if (inspection_plan(no_values).tool_call_plan.value(0).toMap().value(QStringLiteral("name")).toString() !=
      QStringLiteral("photo.inspect_composition"))
    return fail("Composition inspection aliases may omit optional values");
  auto layer_arguments = inspect_arguments;
  layer_arguments.insert(QStringLiteral("layerId"), QStringLiteral("user-layer"));
  auto write_arguments = inspect_arguments;
  write_arguments.insert(QStringLiteral("values"), QJsonObject{{QStringLiteral("text"), QStringLiteral("KEEP")}});
  auto malformed_values = inspect_arguments;
  malformed_values.insert(QStringLiteral("values"), QStringLiteral("invalid"));
  for (const auto& arguments : {layer_arguments, write_arguments, malformed_values}) {
    const auto result = inspection_plan(arguments);
    const auto validation = result.prompt_audit.value(QStringLiteral("toolCallValidation")).toMap();
    const auto rejected_calls = validation.value(QStringLiteral("rejectedCalls")).toList();
    const auto rejected = rejected_calls.isEmpty() ? QVariantMap{} : rejected_calls.first().toMap();
    if (result.ok || !result.tool_call_plan.isEmpty() ||
        validation.value(QStringLiteral("valid")).toBool() ||
        !validation.value(QStringLiteral("executionBlocked")).toBool() ||
        rejected.value(QStringLiteral("arguments")).toMap() != arguments.toVariantMap() ||
        !result.prompt_audit.value(QStringLiteral("toolCallCorrections")).toList().isEmpty())
      return fail("Malformed inspection aliases must remain unchanged and fail closed against the supplied edit schema");
  }
  const auto actual_edit_arguments = composition_tool_plan
      .value(QStringLiteral("toolCalls")).toArray().first().toObject()
      .value(QStringLiteral("arguments")).toObject();
  const auto actual_edit_after_inspection = inspection_plan(actual_edit_arguments);
  if (!actual_edit_after_inspection.ok ||
      actual_edit_after_inspection.tool_call_plan.value(0).toMap()
          .value(QStringLiteral("arguments")).toMap() != actual_edit_arguments.toVariantMap() ||
      !actual_edit_after_inspection.prompt_audit.value(QStringLiteral("toolCallCorrections")).toList().isEmpty() ||
      !actual_edit_after_inspection.prompt_audit.value(QStringLiteral("toolCallValidation")).toMap()
          .value(QStringLiteral("valid")).toBool())
    return fail("An actual edit operation must remain unchanged and pass its supplied schema after inspection normalization");
  request.available_tool_schemas = edit_only_schemas;
  const auto unavailable_inspection = inspection_plan(inspect_arguments);
  const auto unavailable_validation = unavailable_inspection.prompt_audit
      .value(QStringLiteral("toolCallValidation")).toMap();
  const auto unavailable_rejections = unavailable_validation
      .value(QStringLiteral("rejectedCalls")).toList();
  if (unavailable_inspection.ok || !unavailable_inspection.tool_call_plan.isEmpty() ||
      !unavailable_validation.value(QStringLiteral("executionBlocked")).toBool() ||
      unavailable_rejections.size() != 1 ||
      unavailable_rejections.first().toMap().value(QStringLiteral("toolName")).toString() !=
          QStringLiteral("photo.edit_composition") ||
      !unavailable_inspection.prompt_audit.value(QStringLiteral("toolCallCorrections")).toList().isEmpty())
    return fail("An inspection alias must be withheld when its read-only tool is not in the supplied set");

  const QJsonArray valid_citations{QJsonObject{
      {QStringLiteral("field"), QStringLiteral("constraints.max_exposure_shift")},
      {QStringLiteral("reason"), QStringLiteral("Keep edits within the active rulebook.")},
  }};
  const auto missing_edit_intent = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"), QStringLiteral("The requested text is placed; color edits remain to plan.")},
      {QStringLiteral("styleConstraintCitations"), valid_citations},
  });
  if (missing_edit_intent.ok ||
      !missing_edit_intent.error_message.contains(QStringLiteral("typed, nonempty editIntent")) ||
      missing_edit_intent.error_message.contains(QStringLiteral("must include style citations")) ||
      !missing_edit_intent.prompt_audit.value(QStringLiteral("fallbackBlocked")).toBool())
    return fail("A tool-enabled final edit_intent without its typed intent must fail distinctly and without fallback");

  const QJsonObject valid_typed_intent{
      {QStringLiteral("goalSummary"), QStringLiteral("Place the requested headline while preserving the photograph.")},
      {QStringLiteral("confidence"), 0.92},
      {QStringLiteral("globalChangePolicy"), QStringLiteral("preserve")},
      {QStringLiteral("visualFindings"), QJsonArray{}},
      {QStringLiteral("localizedActions"), QJsonArray{}},
      {QStringLiteral("retouchActions"), QJsonArray{}},
  };
  const auto missing_citations = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"), QStringLiteral("The requested editable headline is planned.")},
      {QStringLiteral("editIntent"), valid_typed_intent},
  });
  if (missing_citations.ok ||
      !missing_citations.error_message.contains(QStringLiteral("must include style citations")) ||
      missing_citations.error_message.contains(QStringLiteral("typed, nonempty editIntent")))
    return fail("A typed edit intent without required style citations must report the citation failure separately");

  const QJsonObject historical_intent_without_empty_action_arrays{
      {QStringLiteral("goalSummary"), QStringLiteral("Apply the requested color treatment.")},
      {QStringLiteral("confidence"), 0.88},
      {QStringLiteral("globalChangePolicy"), QStringLiteral("allow")},
      {QStringLiteral("colorMode"), QStringLiteral("color")},
      {QStringLiteral("palette"), QStringLiteral("warm")},
  };
  const auto compatible_intent = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"), QStringLiteral("The requested color treatment is planned.")},
      {QStringLiteral("styleConstraintCitations"), valid_citations},
      {QStringLiteral("editIntent"), historical_intent_without_empty_action_arrays},
  });
  if (!compatible_intent.ok ||
      compatible_intent.decision != QStringLiteral("recipe_patch") ||
      compatible_intent.recipe_patch.isEmpty() ||
      compatible_intent.edit_intent.value(QStringLiteral("goalSummary")).toString().isEmpty() ||
      !compatible_intent.edit_intent.value(QStringLiteral("visualFindings")).toList().isEmpty() ||
      !compatible_intent.edit_intent.value(QStringLiteral("localizedActions")).toList().isEmpty() ||
      !compatible_intent.edit_intent.value(QStringLiteral("retouchActions")).toList().isEmpty())
    return fail("A valid typed edit intent with no local actions must remain compatible when optional empty arrays are omitted");

  auto malformed_optional_actions = historical_intent_without_empty_action_arrays;
  malformed_optional_actions.insert(QStringLiteral("visualFindings"), QStringLiteral("none"));
  const auto rejected_malformed_optional_actions = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"), QStringLiteral("Malformed action data is rejected.")},
      {QStringLiteral("styleConstraintCitations"), valid_citations},
      {QStringLiteral("editIntent"), malformed_optional_actions},
  });
  if (rejected_malformed_optional_actions.ok ||
      !rejected_malformed_optional_actions.error_message.contains(QStringLiteral("typed, nonempty editIntent")))
    return fail("Optional action arrays may be omitted but malformed provided arrays must be rejected");

  const QJsonObject completed_composition_intent{
      {QStringLiteral("goalSummary"), QStringLiteral("The requested editable text work is complete; leave the photo unchanged.")},
      {QStringLiteral("completionStatus"), QStringLiteral("complete")},
      {QStringLiteral("confidence"), 0.95},
      {QStringLiteral("globalChangePolicy"), QStringLiteral("preserve")},
      {QStringLiteral("visualFindings"), QJsonArray{}},
      {QStringLiteral("localizedActions"), QJsonArray{}},
      {QStringLiteral("retouchActions"), QJsonArray{}},
  };
  const QJsonObject completed_composition_response{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"), QStringLiteral("The prior native composition edits are complete; no further photo edits are requested.")},
      {QStringLiteral("styleConstraintCitations"), valid_citations},
      {QStringLiteral("editIntent"), completed_composition_intent},
  };
  const auto saved_request_asset = request.asset;
  request.asset.insert(QStringLiteral("assetId"), QStringLiteral("composition-proof"));
  request.prior_tool_results = saved_prior_tool_results;
  const auto first_turn_noop = parse_tool_enabled_content(completed_composition_response);
  if (first_turn_noop.ok ||
      first_turn_noop.prompt_audit.contains(QStringLiteral("nativeCompositionCompletion")))
    return fail("An empty preserve intent must not complete on the first turn without accepted native composition receipts");

  const QString completed_text_layer_id = QStringLiteral("accepted-native-text-layer");
  const QVariantMap accepted_add_text_receipt{
      {QStringLiteral("toolName"), QStringLiteral("photo.edit_composition")},
      {QStringLiteral("arguments"), QVariantMap{
          {QStringLiteral("assetId"), QStringLiteral("composition-proof")},
          {QStringLiteral("operation"), QStringLiteral("add_text")},
          {QStringLiteral("values"), QVariantMap{{QStringLiteral("text"), QStringLiteral("AFTER THE LIGHT")}}}}},
      {QStringLiteral("ok"), true},
      {QStringLiteral("structuredContent"), QVariantMap{
          {QStringLiteral("changed"), true},
          {QStringLiteral("createdLayerId"), completed_text_layer_id}}},
  };
  const QVariantMap accepted_typography_receipt{
      {QStringLiteral("toolName"), QStringLiteral("photo.edit_composition")},
      {QStringLiteral("arguments"), QVariantMap{
          {QStringLiteral("assetId"), QStringLiteral("composition-proof")},
          {QStringLiteral("operation"), QStringLiteral("typography")},
          {QStringLiteral("layerId"), completed_text_layer_id},
          {QStringLiteral("values"), QVariantMap{{QStringLiteral("size"), 64}}}}},
      {QStringLiteral("ok"), true},
      {QStringLiteral("structuredContent"), QVariantMap{{QStringLiteral("changed"), true}}},
  };
  const QVariantMap accepted_readback_receipt{
      {QStringLiteral("toolName"), QStringLiteral("photo.inspect_composition")},
      {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("assetId"), QStringLiteral("composition-proof")}}},
      {QStringLiteral("ok"), true},
      {QStringLiteral("structuredContent"), QVariantMap{
          {QStringLiteral("layers"), QVariantList{QVariantMap{{QStringLiteral("id"), completed_text_layer_id}}}}}},
  };
  const QVariantList accepted_composition_history{
      accepted_add_text_receipt, accepted_typography_receipt, accepted_readback_receipt};
  request.prior_tool_results = accepted_composition_history;
  const auto prior_native_completion = parse_tool_enabled_content(completed_composition_response);
  const auto native_completion_marker = prior_native_completion.prompt_audit
      .value(QStringLiteral("nativeCompositionCompletion")).toMap();
  if (!prior_native_completion.ok ||
      prior_native_completion.decision != QStringLiteral("recipe_patch") ||
      !prior_native_completion.recipe_patch.isEmpty() ||
      native_completion_marker.value(QStringLiteral("requested")).toBool() != true ||
      native_completion_marker.value(QStringLiteral("acceptedMutationCount")).toInt() != 2 ||
      native_completion_marker.value(QStringLiteral("operationNames")).toStringList() !=
          QStringList{QStringLiteral("add_text"), QStringLiteral("typography")} ||
      native_completion_marker.value(QStringLiteral("assetId")).toString() != QStringLiteral("composition-proof") ||
      native_completion_marker.value(QStringLiteral("readbackConfirmed")).toBool() != true)
    return fail("A preserve/no-op completion may be accepted only after matching successful composition mutations and readback");

  auto unfinished_composition_response = completed_composition_response;
  auto unfinished_composition_intent = unfinished_composition_response
      .value(QStringLiteral("editIntent")).toObject();
  unfinished_composition_intent.insert(QStringLiteral("completionStatus"),
      QStringLiteral("in_progress"));
  unfinished_composition_response.insert(QStringLiteral("editIntent"), unfinished_composition_intent);
  const auto unfinished_composition = parse_tool_enabled_content(unfinished_composition_response);
  if (unfinished_composition.ok || unfinished_composition.prompt_audit
          .contains(QStringLiteral("nativeCompositionCompletion")))
    return fail("A preserve/no-op response explicitly marked in progress must not complete the task despite prior receipts");
  auto undeclared_completion_response = completed_composition_response;
  auto undeclared_completion_intent = undeclared_completion_response
      .value(QStringLiteral("editIntent")).toObject();
  undeclared_completion_intent.remove(QStringLiteral("completionStatus"));
  undeclared_completion_response.insert(QStringLiteral("editIntent"), undeclared_completion_intent);
  const auto undeclared_completion = parse_tool_enabled_content(undeclared_completion_response);
  if (undeclared_completion.ok || undeclared_completion.prompt_audit
          .contains(QStringLiteral("nativeCompositionCompletion")))
    return fail("Prior receipts cannot authorize a no-op completion when the planner omitted its completion declaration");

  request.prior_tool_results = QVariantList{accepted_add_text_receipt, accepted_typography_receipt};
  const auto missing_readback_completion = parse_tool_enabled_content(completed_composition_response);
  if (missing_readback_completion.ok || missing_readback_completion.prompt_audit
          .contains(QStringLiteral("nativeCompositionCompletion")))
    return fail("Successful prior mutations without a fresh readback must not authorize no-op completion");
  auto other_asset_history = accepted_composition_history;
  for (auto& prior_value : other_asset_history) {
    auto prior = prior_value.toMap();
    auto arguments = prior.value(QStringLiteral("arguments")).toMap();
    arguments.insert(QStringLiteral("assetId"), QStringLiteral("other-asset"));
    prior.insert(QStringLiteral("arguments"), arguments);
    prior_value = prior;
  }
  request.prior_tool_results = other_asset_history;
  const auto other_asset_completion = parse_tool_enabled_content(completed_composition_response);
  if (other_asset_completion.ok || other_asset_completion.prompt_audit
          .contains(QStringLiteral("nativeCompositionCompletion")))
    return fail("Native receipts for another asset must not authorize no-op completion of this asset");
  request.prior_tool_results = accepted_composition_history;
  request.require_tool_call_plan = true;
  const auto forced_noop_completion = parse_tool_enabled_content(completed_composition_response);
  if (forced_noop_completion.ok || forced_noop_completion.prompt_audit
          .contains(QStringLiteral("nativeCompositionCompletion")))
    return fail("A forced tool-planning recovery turn must not accept a no-op completion");
  request.require_tool_call_plan = false;
  request.prior_tool_results = saved_prior_tool_results;
  request.asset = saved_request_asset;

  const auto empty_tool_calls = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("No calls were supplied.")},
      {QStringLiteral("toolCalls"), QJsonArray{}},
  });
  if (empty_tool_calls.ok ||
      !empty_tool_calls.error_message.contains(QStringLiteral("nonempty toolCalls")) ||
      !empty_tool_calls.prompt_audit.value(QStringLiteral("fallbackBlocked")).toBool())
    return fail("A tool_call_plan without calls must fail before it can represent completed work");

  const auto malformed_tool_call = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Malformed tool arguments.")},
      {QStringLiteral("toolCalls"), QJsonArray{QJsonObject{
          {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), QStringLiteral("not an object")},
      }}},
  });
  if (malformed_tool_call.ok ||
      !malformed_tool_call.error_message.contains(QStringLiteral("typed tool name and object arguments")) ||
      !malformed_tool_call.prompt_audit.value(QStringLiteral("fallbackBlocked")).toBool())
    return fail("Tool-enabled planning must reject untyped tool names or arguments");

  const QJsonObject invalid_typography_arguments{
      {QStringLiteral("assetId"), QStringLiteral("composition-proof")},
      {QStringLiteral("operation"), QStringLiteral("typography")},
      {QStringLiteral("values"), QJsonObject{
          {QStringLiteral("x"), 70}, {QStringLiteral("y"), 115}}},
  };
  const auto invalid_typography_call = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Try a typography update with positioning values.")},
      {QStringLiteral("toolCalls"), QJsonArray{QJsonObject{
          {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), invalid_typography_arguments},
      }}},
  });
  const auto invalid_call_validation = invalid_typography_call.prompt_audit
      .value(QStringLiteral("toolCallValidation")).toMap();
  const auto rejected_call_values = invalid_call_validation
      .value(QStringLiteral("rejectedCalls")).toList();
  const auto rejected_call = rejected_call_values.isEmpty()
      ? QVariantMap{} : rejected_call_values.first().toMap();
  const auto rejected_errors = rejected_call.value(QStringLiteral("errors")).toStringList();
  const auto invalid_original_calls = QJsonArray{QJsonObject{
      {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
      {QStringLiteral("arguments"), invalid_typography_arguments},
  }};
  if (invalid_typography_call.ok || !invalid_typography_call.tool_call_plan.isEmpty() ||
      invalid_call_validation.value(QStringLiteral("valid")).toBool() ||
      !invalid_call_validation.value(QStringLiteral("executionBlocked")).toBool() ||
      invalid_call_validation.value(QStringLiteral("unexecutedCalls")).toList() != invalid_original_calls.toVariantList() ||
      rejected_call.value(QStringLiteral("arguments")).toMap() != invalid_typography_arguments.toVariantMap() ||
      rejected_call.value(QStringLiteral("inputSchema")).toMap() != typed_composition_input_schema.toVariantMap() ||
      std::none_of(rejected_errors.cbegin(), rejected_errors.cend(), [](const QString& error) {
        return error.contains(QStringLiteral("arguments.layerId is required"));
      }) ||
      std::none_of(rejected_errors.cbegin(), rejected_errors.cend(), [](const QString& error) {
        return error.contains(QStringLiteral("arguments.values.x is not allowed"));
      }) ||
      !invalid_typography_call.error_message.contains(QStringLiteral("arguments.layerId is required")) ||
      !invalid_typography_call.error_message.contains(QStringLiteral("arguments.values.x is not allowed")))
    return fail("Invalid generated calls must be blocked with the exact original arguments, supplied schema, missing layer ID, and operation-specific field errors");

  const QJsonObject valid_paint_arguments{
      {QStringLiteral("assetId"), QStringLiteral("composition-proof")},
      {QStringLiteral("operation"), QStringLiteral("paint")},
      {QStringLiteral("layerId"), QStringLiteral("existing-pixel-layer")},
      {QStringLiteral("values"), QJsonObject{
          {QStringLiteral("points"), QJsonArray{QJsonObject{
              {QStringLiteral("x"), 22}, {QStringLiteral("y"), 35}}}},
          {QStringLiteral("size"), 18}, {QStringLiteral("clipSelection"), true},
          {QStringLiteral("description"), QStringLiteral("Optional native argument named description")},
          {QStringLiteral("title"), QStringLiteral("Optional native argument named title")},
          {QStringLiteral("default"), QStringLiteral("Optional native argument named default")}}},
  };
  const auto valid_paint_call = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Use the existing paint controls for a clipped stroke.")},
      {QStringLiteral("toolCalls"), QJsonArray{QJsonObject{
          {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), valid_paint_arguments},
      }}},
  });
  if (!valid_paint_call.ok || valid_paint_call.tool_call_plan.size() != 1 ||
      valid_paint_call.tool_call_plan.first().toMap().value(QStringLiteral("arguments")).toMap() !=
          valid_paint_arguments.toVariantMap() ||
      !valid_paint_call.prompt_audit.value(QStringLiteral("toolCallValidation")).toMap()
          .value(QStringLiteral("valid")).toBool())
    return fail("The schema validator must preserve supported non-typography paint controls and accept a valid typed call");

  auto invalid_optional_paint_arguments = valid_paint_arguments;
  auto invalid_optional_paint_values = invalid_optional_paint_arguments
      .value(QStringLiteral("values")).toObject();
  invalid_optional_paint_values.insert(QStringLiteral("futureUnknownControl"), 7);
  invalid_optional_paint_arguments.insert(QStringLiteral("values"), invalid_optional_paint_values);
  const auto invalid_optional_paint_call = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Try an unadvertised paint control.")},
      {QStringLiteral("toolCalls"), QJsonArray{QJsonObject{
          {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), invalid_optional_paint_arguments},
      }}},
  });
  const auto invalid_optional_paint_validation = invalid_optional_paint_call.prompt_audit
      .value(QStringLiteral("toolCallValidation")).toMap();
  const auto invalid_optional_paint_rejections = invalid_optional_paint_validation
      .value(QStringLiteral("rejectedCalls")).toList();
  const auto invalid_optional_paint_errors = invalid_optional_paint_rejections.isEmpty()
      ? QStringList{}
      : invalid_optional_paint_rejections.first().toMap()
            .value(QStringLiteral("errors")).toStringList();
  if (invalid_optional_paint_call.ok || !invalid_optional_paint_call.tool_call_plan.isEmpty() ||
      !invalid_optional_paint_validation.value(QStringLiteral("executionBlocked")).toBool() ||
      invalid_optional_paint_rejections.isEmpty() ||
      std::none_of(invalid_optional_paint_errors.cbegin(), invalid_optional_paint_errors.cend(),
          [](const QString& error) {
            return error.contains(QStringLiteral("arguments.values.futureUnknownControl is not allowed"));
          }))
    return fail("Open generic decoder branches must still be rejected by the original closed native schema before execution");

  const QJsonObject valid_add_text_arguments{
      {QStringLiteral("assetId"), QStringLiteral("composition-proof")},
      {QStringLiteral("operation"), QStringLiteral("add_text")},
      {QStringLiteral("values"), QJsonObject{
          {QStringLiteral("text"), QStringLiteral("Earlier accepted title")},
          {QStringLiteral("x"), 45}, {QStringLiteral("y"), 90}}},
  };
  const QJsonArray mixed_withheld_calls{
      QJsonObject{
          {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), valid_add_text_arguments},
      },
      QJsonObject{
          {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), invalid_typography_arguments},
      },
  };
  const auto mixed_withheld_plan = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Create text and then update its typography.")},
      {QStringLiteral("toolCalls"), mixed_withheld_calls},
  });
  const auto mixed_validation = mixed_withheld_plan.prompt_audit
      .value(QStringLiteral("toolCallValidation")).toMap();
  if (mixed_withheld_plan.ok || !mixed_withheld_plan.tool_call_plan.isEmpty() ||
      mixed_validation.value(QStringLiteral("unexecutedCalls")).toList() != mixed_withheld_calls.toVariantList() ||
      mixed_validation.value(QStringLiteral("rejectedCalls")).toList().size() != 1 ||
      mixed_validation.value(QStringLiteral("rejectedCalls")).toList().first().toMap()
          .value(QStringLiteral("index")).toInt() != 1)
    return fail("One invalid call must withhold the entire generated plan and preserve every exact unexecuted call");

  const auto saved_grounding_history = request.prior_tool_results;
  const auto saved_grounding_asset = request.asset;
  const auto saved_grounding_instructions = request.user_instructions;
  request.asset.insert(QStringLiteral("assetId"), QStringLiteral("composition-proof"));
  request.user_instructions = QStringLiteral(
      "Create editable text and choose a native font family and face; set both explicitly.");
  const QVariantList grounding_font_inventory{
      QVariantMap{{QStringLiteral("family"), QStringLiteral("Avenir Next")},
          {QStringLiteral("faces"), QStringList{QStringLiteral("Regular"),
              QStringLiteral("Ultra Light"), QStringLiteral("Bold")}}},
      QVariantMap{{QStringLiteral("family"), QStringLiteral("Georgia")},
          {QStringLiteral("faces"), QStringList{QStringLiteral("Regular")}}},
  };
  const auto inspection_receipt = [](const QString& asset_id, const QVariantList& layers,
                                      const QVariantList& font_faces) {
    return QVariantMap{
        {QStringLiteral("toolName"), QStringLiteral("photo.inspect_composition")},
        {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("assetId"), asset_id}}},
        {QStringLiteral("ok"), true},
        {QStringLiteral("structuredContent"), QVariantMap{
            {QStringLiteral("layers"), layers}, {QStringLiteral("fontFaces"), font_faces}}},
    };
  };
  const auto creation_receipt = [](const QString& asset_id, const QString& layer_id) {
    return QVariantMap{
        {QStringLiteral("toolName"), QStringLiteral("photo.edit_composition")},
        {QStringLiteral("arguments"), QVariantMap{
            {QStringLiteral("assetId"), asset_id}, {QStringLiteral("operation"), QStringLiteral("add_text")}}},
        {QStringLiteral("ok"), true},
        {QStringLiteral("structuredContent"), QVariantMap{
            {QStringLiteral("changed"), true}, {QStringLiteral("createdLayerId"), layer_id}}},
    };
  };
  const auto grounded_typography_arguments = [](const QString& layer_id, const QString& family,
                                                 const QString& face) {
    return QJsonObject{
        {QStringLiteral("assetId"), QStringLiteral("composition-proof")},
        {QStringLiteral("layerId"), layer_id},
        {QStringLiteral("operation"), QStringLiteral("typography")},
        {QStringLiteral("values"), QJsonObject{
            {QStringLiteral("face"), face}, {QStringLiteral("family"), family},
            {QStringLiteral("size"), 52}}},
    };
  };
  const QVariantList empty_composition_layers;
  request.prior_tool_results = QVariantList{
      inspection_receipt(QStringLiteral("composition-proof"), empty_composition_layers,
          grounding_font_inventory)};
  const QJsonArray create_then_placeholder_calls{
      QJsonObject{{QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), valid_add_text_arguments}},
      QJsonObject{{QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), grounded_typography_arguments(
              QStringLiteral("headline-layer-id-placeholder"), QStringLiteral("Avenir Next"),
              QStringLiteral("Ultra Light"))}},
  };
  const auto create_then_placeholder = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Create text and then style its returned layer.")},
      {QStringLiteral("toolCalls"), create_then_placeholder_calls},
  });
  const auto placeholder_validation = create_then_placeholder.prompt_audit
      .value(QStringLiteral("toolCallValidation")).toMap();
  const auto placeholder_rejections = placeholder_validation
      .value(QStringLiteral("rejectedCalls")).toList();
  const auto placeholder_rejection = placeholder_rejections.isEmpty()
      ? QVariantMap{} : placeholder_rejections.first().toMap();
  const auto placeholder_errors = placeholder_rejection.value(QStringLiteral("errors")).toStringList();
  const auto placeholder_context = placeholder_rejection
      .value(QStringLiteral("compositionContextEvidence")).toMap();
  const auto placeholder_payload = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto placeholder_format = placeholder_payload.value(QStringLiteral("format")).toObject();
  if (create_then_placeholder.ok || !create_then_placeholder.tool_call_plan.isEmpty() ||
      placeholder_validation.value(QStringLiteral("unexecutedCalls")).toList() !=
          create_then_placeholder_calls.toVariantList() || placeholder_rejections.size() != 1 ||
      placeholder_rejection.value(QStringLiteral("index")).toInt() != 1 ||
      std::none_of(placeholder_errors.cbegin(), placeholder_errors.cend(), [](const QString& error) {
        return error.contains(QStringLiteral("headline-layer-id-placeholder")) &&
            error.contains(QStringLiteral("not grounded"));
      }) || placeholder_context.value(QStringLiteral("requestedLayerIdGrounded")).toBool() ||
      schema_branch_for_tool_operation(placeholder_format, QStringLiteral("photo.edit_composition"),
          QStringLiteral("add_text")).isEmpty() ||
      !schema_branch_for_tool_operation(placeholder_format, QStringLiteral("photo.edit_composition"),
          QStringLiteral("typography")).isEmpty())
    return fail("A same-plan guessed layer ID must withhold the whole plan, while generation keeps creation and omits layer-target operations until a receipt exists");

  const QString grounded_layer_id = QStringLiteral("actual-created-layer-42");
  const QVariantList empty_inspect_then_create{
      inspection_receipt(QStringLiteral("composition-proof"), empty_composition_layers,
          grounding_font_inventory),
      creation_receipt(QStringLiteral("composition-proof"), grounded_layer_id),
  };
  request.prior_tool_results = empty_inspect_then_create;
  const auto grounded_typography = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Apply the chosen face to the returned text layer.")},
      {QStringLiteral("toolCalls"), QJsonArray{QJsonObject{
          {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), grounded_typography_arguments(
              grounded_layer_id, QStringLiteral("Avenir Next"), QStringLiteral("Ultra Light"))}}}},
  });
  const auto grounded_payload = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto grounded_typography_branch = schema_branch_for_tool_operation(
      grounded_payload.value(QStringLiteral("format")).toObject(),
      QStringLiteral("photo.edit_composition"), QStringLiteral("typography"));
  const auto grounded_layer_enum = grounded_typography_branch.value(QStringLiteral("properties"))
      .toObject().value(QStringLiteral("layerId")).toObject().value(QStringLiteral("enum")).toArray();
  if (!grounded_typography.ok || grounded_typography.tool_call_plan.size() != 1 ||
      !grounded_layer_enum.contains(grounded_layer_id))
    return fail("A target operation must be generated and validated only for an exact layer ID returned by successful creation after the latest inspection");

  request.prior_tool_results = QVariantList{
      creation_receipt(QStringLiteral("composition-proof"), grounded_layer_id)};
  const auto creation_receipt_only_plan = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Use the exact ID returned by the successful creation receipt.")},
      {QStringLiteral("toolCalls"), QJsonArray{QJsonObject{
          {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), grounded_typography_arguments(
              grounded_layer_id, QStringLiteral("Avenir Next"), QStringLiteral("Ultra Light"))}}}},
  });
  const auto receipt_only_payload = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto receipt_only_branch = schema_branch_for_tool_operation(
      receipt_only_payload.value(QStringLiteral("format")).toObject(),
      QStringLiteral("photo.edit_composition"), QStringLiteral("typography"));
  const auto receipt_only_layer_enum = receipt_only_branch.value(QStringLiteral("properties"))
      .toObject().value(QStringLiteral("layerId")).toObject().value(QStringLiteral("enum")).toArray();
  if (!creation_receipt_only_plan.ok || !receipt_only_layer_enum.contains(grounded_layer_id))
    return fail("A successful creation receipt alone must ground its exact returned layer ID for external planner callers");

  const QVariantMap validation_recovery_marker{
      {QStringLiteral("kind"), QStringLiteral("tool-call-validation-recovery")},
      {QStringLiteral("toolCallValidation"), QVariantMap{
          {QStringLiteral("valid"), false}, {QStringLiteral("executionBlocked"), true}}},
  };
  request.prior_tool_results = QVariantList{
      inspection_receipt(QStringLiteral("composition-proof"), empty_composition_layers,
          grounding_font_inventory),
      creation_receipt(QStringLiteral("composition-proof"), grounded_layer_id),
      validation_recovery_marker,
  };
  const auto grounded_after_recovery = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Continue the recovery using the earlier successful creation receipt.")},
      {QStringLiteral("toolCalls"), QJsonArray{QJsonObject{
          {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), grounded_typography_arguments(
              grounded_layer_id, QStringLiteral("Avenir Next"), QStringLiteral("Ultra Light"))}}}},
  });
  if (!grounded_after_recovery.ok)
    return fail("A validation-recovery envelope must not erase a known ID from an earlier successful native creation receipt");

  request.prior_tool_results = QVariantList{
      inspection_receipt(QStringLiteral("composition-proof"), empty_composition_layers,
          grounding_font_inventory),
      creation_receipt(QStringLiteral("composition-proof"), grounded_layer_id),
      inspection_receipt(QStringLiteral("composition-proof"), empty_composition_layers,
          grounding_font_inventory),
  };
  const auto deleted_layer_payload_plan = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Attempt to target the layer absent from the latest inspection.")},
      {QStringLiteral("toolCalls"), QJsonArray{QJsonObject{
          {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), grounded_typography_arguments(
              grounded_layer_id, QStringLiteral("Avenir Next"), QStringLiteral("Ultra Light"))}}}},
  });
  const auto deleted_layer_validation = deleted_layer_payload_plan.prompt_audit
      .value(QStringLiteral("toolCallValidation")).toMap();
  const auto deleted_layer_rejections = deleted_layer_validation
      .value(QStringLiteral("rejectedCalls")).toList();
  const auto deleted_layer_errors = deleted_layer_rejections.isEmpty() ? QStringList{}
      : deleted_layer_rejections.first().toMap().value(QStringLiteral("errors")).toStringList();
  if (deleted_layer_payload_plan.ok || !deleted_layer_payload_plan.tool_call_plan.isEmpty() ||
      std::none_of(deleted_layer_errors.cbegin(), deleted_layer_errors.cend(), [](const QString& error) {
        return error.contains(QStringLiteral("actual-created-layer-42")) &&
            error.contains(QStringLiteral("not grounded"));
      }))
    return fail("A later authoritative inspection must remove absent old IDs instead of resurrecting them from earlier receipts");

  request.prior_tool_results = QVariantList{
      inspection_receipt(QStringLiteral("composition-proof"),
          QVariantList{QVariantMap{{QStringLiteral("id"), grounded_layer_id}}}, grounding_font_inventory)};
  const auto unavailable_font_face = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Try the unavailable face name.")},
      {QStringLiteral("toolCalls"), QJsonArray{QJsonObject{
          {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), grounded_typography_arguments(
              grounded_layer_id, QStringLiteral("Avenir Next"), QStringLiteral("Light"))}}}},
  });
  const auto unavailable_face_validation = unavailable_font_face.prompt_audit
      .value(QStringLiteral("toolCallValidation")).toMap();
  const auto unavailable_face_rejections = unavailable_face_validation
      .value(QStringLiteral("rejectedCalls")).toList();
  const auto unavailable_face_errors = unavailable_face_rejections.isEmpty() ? QStringList{}
      : unavailable_face_rejections.first().toMap().value(QStringLiteral("errors")).toStringList();
  const auto unavailable_face_payload = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto unavailable_face_branch = schema_branch_for_tool_operation(
      unavailable_face_payload.value(QStringLiteral("format")).toObject(),
      QStringLiteral("photo.edit_composition"), QStringLiteral("typography"));
  const auto unavailable_face_enum = unavailable_face_branch.value(QStringLiteral("properties"))
      .toObject().value(QStringLiteral("values")).toObject().value(QStringLiteral("properties"))
      .toObject().value(QStringLiteral("face")).toObject().value(QStringLiteral("enum")).toArray();
  const auto available_native_face = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Use the exact face listed for this family.")},
      {QStringLiteral("toolCalls"), QJsonArray{QJsonObject{
          {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), grounded_typography_arguments(
              grounded_layer_id, QStringLiteral("Avenir Next"), QStringLiteral("Ultra Light"))}}}},
  });
  request.prior_tool_results = saved_grounding_history;
  request.asset = saved_grounding_asset;
  request.user_instructions = saved_grounding_instructions;
  if (unavailable_font_face.ok || !unavailable_font_face.tool_call_plan.isEmpty() ||
      std::none_of(unavailable_face_errors.cbegin(), unavailable_face_errors.cend(), [](const QString& error) {
        return error.contains(QStringLiteral("Avenir Next")) && error.contains(QStringLiteral("Light")) &&
            error.contains(QStringLiteral("fontFaces inventory"));
      }) || unavailable_face_enum.contains(QStringLiteral("Light")) ||
      !unavailable_face_enum.contains(QStringLiteral("Ultra Light")) || !available_native_face.ok)
    return fail("An explicit family/face pair must match the retained native inventory exactly, including the spaced Ultra Light face name");

  auto oversized_recovery_schema = typed_composition_input_schema;
  auto oversized_conditions = oversized_recovery_schema.value(QStringLiteral("allOf")).toArray();
  const QJsonObject oversized_future_control{
      {QStringLiteral("type"), QStringLiteral("string")},
      {QStringLiteral("description"), QString(60000, QLatin1Char('z'))},
  };
  const QJsonObject oversized_paint_values{
      {QStringLiteral("properties"), QJsonObject{
          {QStringLiteral("futureEffectControl"), oversized_future_control},
      }},
  };
  const QJsonObject oversized_paint_condition{
      {QStringLiteral("if"), composition_operation_condition(QStringLiteral("paint"))},
      {QStringLiteral("then"), QJsonObject{{QStringLiteral("properties"), QJsonObject{
          {QStringLiteral("values"), oversized_paint_values},
      }}}},
  };
  oversized_conditions.append(oversized_paint_condition);
  oversized_recovery_schema.insert(QStringLiteral("allOf"), oversized_conditions);
  const QVariantList oversized_recovery_schemas{QVariantMap{
      {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
      {QStringLiteral("inputSchema"), oversized_recovery_schema.toVariantMap()},
  }};
  const auto recovery_envelope = photo_editor::desktop::agent_tool_call_validation_recovery_envelope(
      mixed_withheld_plan, oversized_recovery_schemas);
  const auto raw_recovery_bytes = QJsonDocument::fromVariant(recovery_envelope)
      .toJson(QJsonDocument::Compact);
  const QVariantMap earlier_successful_receipt{
      {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
      {QStringLiteral("arguments"), QVariantMap{
          {QStringLiteral("assetId"), QStringLiteral("composition-proof")},
          {QStringLiteral("operation"), QStringLiteral("add_text")}}},
      {QStringLiteral("result"), QVariantMap{
          {QStringLiteral("ok"), true},
          {QStringLiteral("structuredContent"), QVariantMap{
          {QStringLiteral("createdLayerId"), QStringLiteral("returned-earlier-title")}}}}},
  };
  const QVariantMap recovery_approved_snapshot{
      {QStringLiteral("kind"), QStringLiteral("approved-task-snapshot")},
      {QStringLiteral("authority"), QStringLiteral("approved-brief-and-deliverables")},
      {QStringLiteral("snapshot"), QVariantMap{
          {QStringLiteral("approvedInstructions"), QStringLiteral(
              "Create the approved editable title and subtitle on composition-proof.")},
          {QStringLiteral("exactCopy"), QStringList{QStringLiteral("Approved recovery title")}},
          {QStringLiteral("deliverables"), QStringList{QStringLiteral("Editable text layers on composition-proof.")}},
          {QStringLiteral("targetIds"), QStringList{QStringLiteral("composition-proof")}},
          {QStringLiteral("taskScopedCapabilities"), QStringList{QStringLiteral("graphics")}},
      }},
  };
  const auto recovery_native_inspection = inspection_receipt(
      QStringLiteral("composition-proof"),
      QVariantList{QVariantMap{{QStringLiteral("id"), grounded_layer_id},
          {QStringLiteral("kind"), QStringLiteral("text")},
          {QStringLiteral("textTypography"), QVariantMap{
              {QStringLiteral("family"), QStringLiteral("Avenir Next")},
              {QStringLiteral("face"), QStringLiteral("Ultra Light")},
              {QStringLiteral("size"), 52}}}}},
      grounding_font_inventory);
  const QVariantMap latest_recovery_proof{
      {QStringLiteral("kind"), QStringLiteral("current-proof-validation")},
      {QStringLiteral("assetId"), QStringLiteral("composition-proof")},
      {QStringLiteral("pass"), false},
      {QStringLiteral("issues"), QStringList{
          QStringLiteral("latest-rendered-proof-requires-repair")}},
  };
  const auto saved_recovery_asset = request.asset;
  const auto saved_recovery_instructions = request.user_instructions;
  request.asset.insert(QStringLiteral("assetId"), QStringLiteral("composition-proof"));
  request.user_instructions = QStringLiteral(
      "Create the approved title, use a native family and face, and repair the latest proof.");
  request.prior_tool_results = QVariantList{
      recovery_approved_snapshot, recovery_native_inspection, earlier_successful_receipt,
      latest_recovery_proof, recovery_envelope};
  const auto recovery_prompt_plan = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Continue with the grounded native title typography.")},
      {QStringLiteral("toolCalls"), QJsonArray{QJsonObject{
          {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), grounded_typography_arguments(
              grounded_layer_id, QStringLiteral("Avenir Next"), QStringLiteral("Ultra Light"))},
      }}},
  }, true);
  request.prior_tool_results = saved_prior_tool_results;
  request.asset = saved_recovery_asset;
  request.user_instructions = saved_recovery_instructions;
  const auto recovery_prompt = recovery_prompt_plan.prompt_audit
      .value(QStringLiteral("userPrompt")).toString();
  const auto recovery_history_start = recovery_prompt.lastIndexOf(history_label);
  const auto recovery_history_bytes = recovery_prompt.mid(
      recovery_history_start + history_label.size()).toUtf8();
  const auto recovery_history = QJsonDocument::fromJson(recovery_history_bytes);
  const auto recovery_history_entries = recovery_history.array();
  QJsonObject compacted_recovery;
  for (const auto& entry : recovery_history_entries) {
    const auto object = entry.toObject();
    if (object.value(QStringLiteral("kind")).toString() ==
        QStringLiteral("tool-call-validation-recovery")) {
      compacted_recovery = object;
      break;
    }
    const auto nested_recoveries = object.value(QStringLiteral("toolCallValidationRecoveries")).toArray();
    for (const auto& nested_value : nested_recoveries) {
      const auto nested = nested_value.toObject();
      if (nested.value(QStringLiteral("kind")).toString() ==
          QStringLiteral("tool-call-validation-recovery")) {
        compacted_recovery = nested;
        break;
      }
    }
    if (!compacted_recovery.isEmpty()) break;
  }
  const auto compacted_validation = compacted_recovery
      .value(QStringLiteral("toolCallValidation")).toObject();
  const auto compacted_rejected = compacted_validation
      .value(QStringLiteral("rejectedCalls")).toArray();
  const auto compacted_rejection = compacted_rejected.isEmpty()
      ? QJsonObject{} : compacted_rejected.first().toObject();
  const auto recovery_request_payload = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto recovery_payload_messages = recovery_request_payload
      .value(QStringLiteral("messages")).toArray();
  const auto recovery_user_prompt = recovery_payload_messages.size() < 2
      ? QString{} : recovery_payload_messages.at(1).toObject()
          .value(QStringLiteral("content")).toString();
  const auto catalog_label = QStringLiteral("Available MCP tool schemas JSON:\n");
  const auto visual_evidence_label = QStringLiteral("Current rendered planning evidence JSON:\n");
  const auto catalog_start = recovery_user_prompt.indexOf(catalog_label);
  const auto catalog_end = recovery_user_prompt.indexOf(visual_evidence_label, catalog_start);
  const auto catalog_bytes = catalog_start < 0 || catalog_end < catalog_start
      ? QByteArray{} : recovery_user_prompt.mid(
          catalog_start + catalog_label.size(), catalog_end - catalog_start - catalog_label.size())
          .trimmed().toUtf8();
  const auto current_catalog = QJsonDocument::fromJson(catalog_bytes).array();
  const auto current_catalog_entry = current_catalog.isEmpty()
      ? QJsonObject{} : current_catalog.first().toObject();
  const auto current_catalog_guide = current_catalog_entry
      .value(QStringLiteral("argumentGuide")).toObject();
  const auto current_operation_options = current_catalog_guide
      .value(QStringLiteral("operationOptions")).toArray();
  const auto current_operation_requirements = current_catalog_guide
      .value(QStringLiteral("requiredFieldsByOperation")).toObject();
  const auto current_add_text_requirements = current_operation_requirements
      .value(QStringLiteral("add_text")).toArray();
  const auto current_field_semantics = current_catalog_guide
      .value(QStringLiteral("fieldSemantics")).toObject();
  const auto current_x_guide = current_field_semantics
      .value(QStringLiteral("values.x")).toObject();
  const auto current_size_guide = current_field_semantics
      .value(QStringLiteral("values.size")).toObject();
  const auto recovery_format = recovery_request_payload.value(QStringLiteral("format")).toObject();
  const auto recovery_typography_branch = schema_branch_for_tool_operation(
      recovery_format, QStringLiteral("photo.edit_composition"), QStringLiteral("typography"));
  const auto recovery_typography_properties = recovery_typography_branch
      .value(QStringLiteral("properties")).toObject();
  const auto recovery_typography_layer_ids = recovery_typography_properties
      .value(QStringLiteral("layerId")).toObject().value(QStringLiteral("enum")).toArray();
  const QJsonArray expected_recovery_layer_ids{
      grounded_layer_id, QStringLiteral("returned-earlier-title")};
  const auto recovery_typography_values = recovery_typography_properties
      .value(QStringLiteral("values")).toObject().value(QStringLiteral("properties")).toObject();
  const auto recovery_prompt_components = recovery_prompt_plan.prompt_audit
      .value(QStringLiteral("promptComponents")).toMap();
  const auto recovery_tool_catalog_history = recovery_prompt_components
      .value(QStringLiteral("availableToolCatalogUtf8Bytes")).toLongLong();
  const auto recovery_history_component = recovery_prompt_components
      .value(QStringLiteral("priorToolHistoryUtf8Bytes")).toLongLong();
  const auto recovery_attached_images = recovery_prompt_components
      .value(QStringLiteral("attachedImageCount")).toInt();
  const auto expected_attached_images = recovery_prompt_plan.prompt_audit
      .value(QStringLiteral("imageIncluded")).toBool()
          ? 1 + recovery_prompt_plan.prompt_audit
                    .value(QStringLiteral("additionalProofImageCount")).toInt()
          : 0;
  if (raw_recovery_bytes.size() <= 48000 || !recovery_prompt_plan.ok ||
      recovery_history_start < 0 || !recovery_history.isArray() || recovery_history_entries.size() < 4 ||
      recovery_history_bytes.size() > 18000 || compacted_recovery.isEmpty() ||
      !recovery_history_bytes.contains("returned-earlier-title") ||
      !recovery_history_bytes.contains("arguments.layerId is required") ||
      !recovery_history_bytes.contains("arguments.values.x is not allowed") ||
      recovery_history_bytes.contains("futureEffectControl") ||
      !recovery_history_bytes.contains("Approved recovery title") ||
      !recovery_history_bytes.contains("latest-rendered-proof-requires-repair") ||
      !recovery_history_bytes.contains("Avenir Next") ||
      !recovery_history_bytes.contains("Ultra Light") ||
      !recovery_history_bytes.contains("actual-created-layer-42") ||
      compacted_validation.value(QStringLiteral("unexecutedCalls")).toArray().toVariantList() !=
          mixed_withheld_calls.toVariantList() ||
      compacted_recovery.value(QStringLiteral("wholeGeneratedPlanWithheld")).toBool() != true ||
      compacted_recovery.value(QStringLiteral("noCallsInRejectedPlanExecuted")).toBool() != true ||
      compacted_recovery.value(QStringLiteral("acceptedMutationsRemainApplied")).toBool() != true ||
      compacted_recovery.contains(QStringLiteral("currentToolSchemas")) ||
      compacted_recovery.value(QStringLiteral("toolSchemasProvidedSeparately")).toBool() != true ||
      compacted_rejected.size() != 1 ||
      compacted_rejection.value(QStringLiteral("originalCall")).toObject()
          .value(QStringLiteral("arguments")).toObject() != invalid_typography_arguments ||
      compacted_rejection.contains(QStringLiteral("inputSchema")) ||
      current_catalog_entry.value(QStringLiteral("name")).toString() !=
          QStringLiteral("photo.edit_composition") ||
      current_catalog_entry.contains(QStringLiteral("inputSchema")) ||
      !current_operation_options.contains(QStringLiteral("paint")) ||
      !current_operation_options.contains(QStringLiteral("typography")) ||
      !current_add_text_requirements.contains(QStringLiteral("values.text")) ||
      !current_add_text_requirements.contains(QStringLiteral("values.x")) ||
      !current_add_text_requirements.contains(QStringLiteral("values.y")) ||
      !current_x_guide.value(QStringLiteral("description")).toString()
          .contains(QStringLiteral("canvas-pixel coordinate")) ||
      !current_size_guide.value(QStringLiteral("description")).toString()
          .contains(QStringLiteral("document pixels")) ||
      !current_catalog_guide.value(QStringLiteral("familyFaceChoicesInResponseSchema")).toBool() ||
      !current_catalog_guide.value(QStringLiteral("layerIdChoicesInResponseSchema")).toBool() ||
      recovery_typography_layer_ids != expected_recovery_layer_ids ||
      !recovery_typography_values.value(QStringLiteral("family")).toObject()
          .value(QStringLiteral("enum")).toArray().contains(QStringLiteral("Avenir Next")) ||
      !recovery_typography_values.value(QStringLiteral("face")).toObject()
          .value(QStringLiteral("enum")).toArray().contains(QStringLiteral("Ultra Light")) ||
      recovery_prompt_components.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("ollama-planning-prompt-components-v1") ||
      recovery_tool_catalog_history != catalog_bytes.size() ||
      recovery_history_component != recovery_history_bytes.trimmed().size() ||
      recovery_prompt_components.value(QStringLiteral("generationSchemaUtf8Bytes")).toLongLong() !=
          QJsonDocument(recovery_format).toJson(QJsonDocument::Compact).size() ||
      recovery_attached_images != expected_attached_images ||
      recovery_prompt_components.value(QStringLiteral("contextTokensRequested")).toInt() !=
          recovery_prompt_plan.prompt_audit.value(QStringLiteral("contextTokens")).toInt() ||
      recovery_prompt_components.value(QStringLiteral("contextTokenLimit")).toInt() != 24576 ||
      recovery_prompt_components.value(QStringLiteral("contextTokensRequested")).toInt() != 24576 ||
      recovery_prompt_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() <= 0 ||
      recovery_prompt_components.value(QStringLiteral("estimatedContextTokensBeforeCap")).toInt() > 24576 ||
      recovery_prompt_components.value(QStringLiteral("contextTokensRequested")).toInt() < 4096 ||
      recovery_prompt_components.value(QStringLiteral("contextTokensRequested")).toInt() > 24576) {
    const QVariantMap recovery_diagnostic{
        {QStringLiteral("rawRecoveryBytes"), raw_recovery_bytes.size()},
        {QStringLiteral("planOk"), recovery_prompt_plan.ok},
        {QStringLiteral("historyStart"), recovery_history_start},
        {QStringLiteral("historyIsArray"), recovery_history.isArray()},
        {QStringLiteral("historyEntries"), recovery_history_entries.size()},
        {QStringLiteral("historyBytes"), recovery_history_bytes.size()},
        {QStringLiteral("recoveryFound"), !compacted_recovery.isEmpty()},
        {QStringLiteral("historyHasEarlierId"), recovery_history_bytes.contains("returned-earlier-title")},
        {QStringLiteral("historyHasLayerError"), recovery_history_bytes.contains("arguments.layerId is required")},
        {QStringLiteral("historyHasValueError"), recovery_history_bytes.contains("arguments.values.x is not allowed")},
        {QStringLiteral("historyHasApprovedScope"), recovery_history_bytes.contains("Approved recovery title")},
        {QStringLiteral("historyHasLatestProof"), recovery_history_bytes.contains("latest-rendered-proof-requires-repair")},
        {QStringLiteral("historyHasNativeFont"), recovery_history_bytes.contains("Ultra Light")},
        {QStringLiteral("historyHasCreatedId"), recovery_history_bytes.contains("actual-created-layer-42")},
        {QStringLiteral("contextBudget"), context_budget_summary(recovery_prompt_plan)},
        {QStringLiteral("currentCatalogName"), current_catalog_entry.value(QStringLiteral("name"))},
        {QStringLiteral("generationLayerIds"), recovery_typography_layer_ids},
        {QStringLiteral("expectedGenerationLayerIds"), expected_recovery_layer_ids},
        {QStringLiteral("withheldCallsMatch"), compacted_validation.value(QStringLiteral("unexecutedCalls"))
            .toArray().toVariantList() == mixed_withheld_calls.toVariantList()},
        {QStringLiteral("wholePlanWithheld"), compacted_recovery.value(QStringLiteral("wholeGeneratedPlanWithheld"))},
        {QStringLiteral("noRejectedCallsExecuted"), compacted_recovery.value(QStringLiteral("noCallsInRejectedPlanExecuted"))},
        {QStringLiteral("priorAcceptedMutationsRemain"), compacted_recovery.value(QStringLiteral("acceptedMutationsRemainApplied"))},
        {QStringLiteral("duplicateSchemasRemoved"), !compacted_recovery.contains(QStringLiteral("currentToolSchemas")) &&
            compacted_recovery.value(QStringLiteral("toolSchemasProvidedSeparately")).toBool()},
        {QStringLiteral("rejectedCallCount"), compacted_rejected.size()},
        {QStringLiteral("originalRejectedCallMatches"), !compacted_rejected.isEmpty() &&
            compacted_rejected.first().toObject().value(QStringLiteral("originalCall")).toObject()
                .value(QStringLiteral("arguments")).toObject() == invalid_typography_arguments},
        {QStringLiteral("rejectedSchemaRemoved"), !compacted_rejected.isEmpty() &&
            !compacted_rejected.first().toObject().contains(QStringLiteral("inputSchema"))},
        {QStringLiteral("currentCatalogHasPaint"), catalog_bytes.contains("paint")},
        {QStringLiteral("currentCatalogHasTypography"), catalog_bytes.contains("typography")},
        {QStringLiteral("components"), recovery_prompt_components},
        {QStringLiteral("expectedCatalogBytes"), catalog_bytes.size()},
        {QStringLiteral("observedCatalogBytes"), recovery_tool_catalog_history},
        {QStringLiteral("actualFormatMatchesAudit"), recovery_request_payload.value(QStringLiteral("format")) ==
            QJsonObject::fromVariantMap(recovery_prompt_plan.prompt_audit
                .value(QStringLiteral("generationSchema")).toMap())},
    };
    std::cerr << "recovery projection audit=" << QJsonDocument::fromVariant(recovery_diagnostic)
        .toJson(QJsonDocument::Compact).toStdString() << '\n';
    return fail("The real recovery prompt must keep exact calls, earlier IDs, approved scope, fresh proof/font facts, and the separate current schemas within its history budget and report component sizes");
  }

  auto future_composition_schema = typed_composition_input_schema;
  auto future_schema_properties = future_composition_schema.value(QStringLiteral("properties")).toObject();
  auto future_operation_schema = future_schema_properties.value(QStringLiteral("operation")).toObject();
  auto future_operation_values = future_operation_schema.value(QStringLiteral("enum")).toArray();
  future_operation_values.append(QStringLiteral("future_operation"));
  future_operation_schema.insert(QStringLiteral("enum"), future_operation_values);
  future_schema_properties.insert(QStringLiteral("operation"), future_operation_schema);
  future_composition_schema.insert(QStringLiteral("properties"), future_schema_properties);
  request.available_tool_schemas = QVariantList{QVariantMap{
      {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
      {QStringLiteral("inputSchema"), future_composition_schema.toVariantMap()}}};
  const auto future_operation_call = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Use a newly advertised operation with its typed values.")},
      {QStringLiteral("toolCalls"), QJsonArray{QJsonObject{
          {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), QJsonObject{
              {QStringLiteral("assetId"), QStringLiteral("composition-proof")},
              {QStringLiteral("operation"), QStringLiteral("future_operation")},
              {QStringLiteral("values"), QJsonObject{}}}},
      }}},
  });
  const auto future_request = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto future_format = future_request.value(QStringLiteral("format")).toObject();
  const auto future_tool_branch = schema_branch_for_decision(
      future_format, QStringLiteral("tool_call_plan"));
  const auto future_tool_items = future_tool_branch.value(QStringLiteral("properties")).toObject()
      .value(QStringLiteral("toolCalls")).toObject().value(QStringLiteral("items")).toObject();
  QJsonObject future_call_variant;
  for (const auto& call_variant : future_tool_items.value(QStringLiteral("oneOf")).toArray()) {
    const auto call_properties = call_variant.toObject().value(QStringLiteral("properties")).toObject();
    const auto argument = call_properties.value(QStringLiteral("arguments")).toObject();
    if (call_properties.value(QStringLiteral("name")).toObject()
            .value(QStringLiteral("enum")).toArray()
            .contains(QStringLiteral("photo.edit_composition")) &&
        argument.value(QStringLiteral("properties")).toObject()
            .value(QStringLiteral("operation")).toObject().value(QStringLiteral("enum")).toArray()
            .contains(QStringLiteral("future_operation"))) {
      future_call_variant = call_variant.toObject();
      break;
    }
  }
  const auto future_argument_schema = future_call_variant.value(QStringLiteral("properties")).toObject()
      .value(QStringLiteral("arguments")).toObject();
  if (!future_operation_call.ok || future_call_variant.isEmpty() ||
      future_argument_schema.contains(QStringLiteral("oneOf")) ||
      !future_argument_schema.value(QStringLiteral("properties")).toObject()
          .value(QStringLiteral("operation")).toObject().value(QStringLiteral("enum")).toArray()
          .contains(QStringLiteral("future_operation")))
    return fail("Every advertised future composition operation must get its own decoder branch from the canonical enum");

  auto wide_composition_schema = typed_composition_input_schema;
  auto wide_properties = wide_composition_schema.value(QStringLiteral("properties")).toObject();
  auto wide_operation = wide_properties.value(QStringLiteral("operation")).toObject();
  auto wide_operation_enum = wide_operation.value(QStringLiteral("enum")).toArray();
  auto wide_values_schema = wide_properties.value(QStringLiteral("values")).toObject();
  auto wide_values_properties = wide_values_schema.value(QStringLiteral("properties")).toObject();
  for (int index = 0; index < 40; ++index) {
    const auto operation = QStringLiteral("budget_operation_%1").arg(index);
    const auto control = QStringLiteral("optionalBudgetControl%1").arg(index);
    wide_operation_enum.append(operation);
    wide_values_properties.insert(control, QJsonObject{
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("description"), QStringLiteral("A long repeated schema annotation that must not inflate every operation branch.")},
    });
  }
  wide_operation.insert(QStringLiteral("enum"), wide_operation_enum);
  wide_values_schema.insert(QStringLiteral("properties"), wide_values_properties);
  wide_properties.insert(QStringLiteral("operation"), wide_operation);
  wide_properties.insert(QStringLiteral("values"), wide_values_schema);
  wide_composition_schema.insert(QStringLiteral("properties"), wide_properties);
  request.available_tool_schemas = QVariantList{QVariantMap{
      {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
      {QStringLiteral("inputSchema"), wide_composition_schema.toVariantMap()}}};
  const auto wide_optional_call = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Use an optional native control from the wide canonical schema.")},
      {QStringLiteral("toolCalls"), QJsonArray{QJsonObject{
          {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
          {QStringLiteral("arguments"), QJsonObject{
              {QStringLiteral("assetId"), QStringLiteral("composition-proof")},
              {QStringLiteral("operation"), QStringLiteral("budget_operation_0")},
              {QStringLiteral("values"), QJsonObject{{QStringLiteral("optionalBudgetControl0"),
                  QStringLiteral("enabled")}}}}},
      }}},
  });
  const auto wide_request = fake_ollama_server_available
      ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
      : read_json_file(fake_ollama_request_path);
  const auto wide_format = wide_request.value(QStringLiteral("format")).toObject();
  const auto wide_tool_branch = schema_branch_for_decision(wide_format, QStringLiteral("tool_call_plan"));
  const auto wide_variants = wide_tool_branch.value(QStringLiteral("properties")).toObject()
      .value(QStringLiteral("toolCalls")).toObject().value(QStringLiteral("items")).toObject()
      .value(QStringLiteral("oneOf")).toArray();
  const auto wide_future_branch = schema_branch_for_tool_operation(
      wide_format, QStringLiteral("photo.edit_composition"), QStringLiteral("budget_operation_0"));
  const auto wide_future_values = wide_future_branch.value(QStringLiteral("properties")).toObject()
      .value(QStringLiteral("values")).toObject();
  const auto wide_canonical_bytes = QJsonDocument(wide_composition_schema)
      .toJson(QJsonDocument::Compact).size();
  const auto wide_generation_bytes = wide_optional_call.prompt_audit
      .value(QStringLiteral("generationSchemaByteCount")).toLongLong();
  const auto wide_prompt = wide_optional_call.prompt_audit.value(QStringLiteral("userPrompt")).toString();
  const auto wide_catalog_start = wide_prompt.indexOf(QStringLiteral("Available MCP tool schemas JSON:\n"));
  const auto wide_catalog_end = wide_prompt.indexOf(
      QStringLiteral("Current rendered planning evidence JSON:\n"), wide_catalog_start);
  const auto wide_catalog_bytes = wide_catalog_start < 0 || wide_catalog_end < wide_catalog_start
      ? QByteArray{} : wide_prompt.mid(
          wide_catalog_start + QStringLiteral("Available MCP tool schemas JSON:\n").size(),
          wide_catalog_end - wide_catalog_start - QStringLiteral("Available MCP tool schemas JSON:\n").size())
              .toUtf8();
  const auto wide_catalog = QJsonDocument::fromJson(wide_catalog_bytes).array();
  const auto wide_catalog_guide = wide_catalog.isEmpty() ? QJsonObject{}
      : wide_catalog.first().toObject().value(QStringLiteral("argumentGuide")).toObject();
  const auto wide_catalog_control = wide_catalog_guide.value(QStringLiteral("fieldSemantics")).toObject()
      .value(QStringLiteral("values.optionalBudgetControl0")).toObject();
  request.available_tool_schemas = edit_only_schemas;
  const bool wide_optional_call_ok = wide_optional_call.ok;
  const bool wide_branch_count_ok = wide_variants.size() >= 40;
  const bool wide_size_ok = wide_generation_bytes > 0 &&
      wide_generation_bytes <= wide_canonical_bytes * 4;
  const bool wide_values_open =
      wide_future_values.value(QStringLiteral("additionalProperties")).toBool() == true;
  const bool wide_control_not_repeated = !wide_future_values.value(QStringLiteral("properties"))
      .toObject().contains(QStringLiteral("optionalBudgetControl0"));
  const bool wide_catalog_control_preserved =
      wide_catalog_control.value(QStringLiteral("type")).toString() == QStringLiteral("string") &&
      wide_catalog_control.value(QStringLiteral("description")).toString() ==
          QStringLiteral("A long repeated schema annotation that must not inflate every operation branch.");
  if (!wide_optional_call_ok || !wide_branch_count_ok || !wide_size_ok || !wide_values_open ||
      !wide_control_not_repeated || !wide_catalog_control_preserved) {
    std::cerr << "wide-schema diagnostic: call_ok=" << wide_optional_call_ok
              << " variants=" << wide_variants.size()
              << " branch_count_ok=" << wide_branch_count_ok
              << " decoder_bytes=" << wide_generation_bytes
              << " canonical_bytes=" << wide_canonical_bytes
              << " size_ok=" << wide_size_ok
              << " values_open=" << wide_values_open
              << " optional_control_repeated=" << !wide_control_not_repeated
              << " catalog_control_type="
              << wide_catalog_control.value(QStringLiteral("type")).toString().toStdString()
              << " catalog_annotation_preserved="
              << (wide_catalog_control.value(QStringLiteral("description")).toString() ==
                  QStringLiteral("A long repeated schema annotation that must not inflate every operation branch."))
              << '\n';
    return fail("Operation decoder size must grow with branch count, not repeat every optional union field, while the prompt catalog and host validation preserve native control fields and descriptions");
  }
  request.available_tool_schemas = edit_only_schemas;

  const auto unregistered_tool_call = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Call an unlisted tool.")},
      {QStringLiteral("toolCalls"), QJsonArray{QJsonObject{
          {QStringLiteral("name"), QStringLiteral("photo.unlisted_tool")},
          {QStringLiteral("arguments"), QJsonObject{}},
      }}},
  });
  const auto unregistered_validation = unregistered_tool_call.prompt_audit
      .value(QStringLiteral("toolCallValidation")).toMap();
  const auto unregistered_rejections = unregistered_validation
      .value(QStringLiteral("rejectedCalls")).toList();
  const auto unregistered_errors = unregistered_rejections.isEmpty()
      ? QStringList{} : unregistered_rejections.first().toMap()
          .value(QStringLiteral("errors")).toStringList();
  if (unregistered_tool_call.ok || !unregistered_tool_call.tool_call_plan.isEmpty() ||
      unregistered_validation.value(QStringLiteral("valid")).toBool() ||
      !unregistered_validation.value(QStringLiteral("executionBlocked")).toBool() ||
      unregistered_validation.value(QStringLiteral("unexecutedCalls")).toList().size() != 1 ||
      unregistered_rejections.size() != 1 ||
      std::none_of(unregistered_errors.cbegin(), unregistered_errors.cend(), [](const QString& error) {
        return error.contains(QStringLiteral("tool name is not in the supplied tool set"));
      }) ||
      !unregistered_tool_call.error_message.contains(QStringLiteral("failed input-schema validation")) ||
      !unregistered_tool_call.prompt_audit.value(QStringLiteral("fallbackBlocked")).toBool())
    return fail("Tool-enabled planning must reject calls outside the supplied typed tool set");

  request.allow_tool_call_plan = true;
  request.require_tool_call_plan = true;
  request.available_tool_schemas = QVariantList{QVariantMap{
      {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
      {QStringLiteral("inputSchema"), QVariantMap{{QStringLiteral("type"), QStringLiteral("object")}}}}};
  const QJsonObject forced_call_arguments{
      {QStringLiteral("assetId"), QStringLiteral("composition-proof")},
      {QStringLiteral("operation"), QStringLiteral("add_text")},
      {QStringLiteral("values"), QJsonObject{
          {QStringLiteral("text"), QStringLiteral("FLOAT")},
          {QStringLiteral("x"), 70}, {QStringLiteral("y"), 115}}},
  };
  const QJsonObject forced_call{
      {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
      {QStringLiteral("arguments"), forced_call_arguments},
  };
  const auto forced_tool_plan = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
      {QStringLiteral("summary"), QStringLiteral("Continue the unresolved approved composition.")},
      {QStringLiteral("toolCalls"), QJsonArray{forced_call}},
  });
  const auto forced_request = fake_ollama_server_available
                                 ? fake_ollama_server.lastRequestForPath(QStringLiteral("/api/chat"))
                                 : read_json_file(fake_ollama_request_path);
  const auto forced_schema = forced_request.value(QStringLiteral("format")).toObject();
  QJsonArray forced_decisions;
  for (const auto& branch_value : forced_schema.value(QStringLiteral("oneOf")).toArray()) {
    const auto branch_decisions = branch_value.toObject().value(QStringLiteral("properties"))
        .toObject().value(QStringLiteral("decision")).toObject().value(QStringLiteral("enum")).toArray();
    for (const auto& decision_value : branch_decisions)
      if (!forced_decisions.contains(decision_value)) forced_decisions.append(decision_value);
  }
  const auto forced_tool_branch =
      schema_branch_for_decision(forced_schema, QStringLiteral("tool_call_plan"));
  const auto forced_tool_calls_schema = forced_tool_branch.value(QStringLiteral("properties"))
                                            .toObject()
                                            .value(QStringLiteral("toolCalls"))
                                            .toObject();
  const auto forced_tool_item_variants = forced_tool_calls_schema.value(QStringLiteral("items"))
                                             .toObject()
                                             .value(QStringLiteral("oneOf"))
                                             .toArray();
  const auto forced_messages = forced_request.value(QStringLiteral("messages")).toArray();
  const auto forced_system = forced_messages.isEmpty()
                                 ? QString{}
                                 : forced_messages.first().toObject()
                                       .value(QStringLiteral("content")).toString();
  const auto forced_prompt = forced_messages.size() < 2
                                 ? QString{}
                                 : forced_messages.at(1).toObject()
                                       .value(QStringLiteral("content")).toString();
  if (!forced_tool_plan.ok ||
      forced_tool_plan.decision != QStringLiteral("tool_call_plan") ||
      forced_tool_plan.tool_call_plan.size() != 1 ||
      forced_tool_plan.recipe_patch.size() != 0 ||
      forced_decisions.contains(QStringLiteral("edit_intent")) ||
      !forced_decisions.contains(QStringLiteral("tool_call_plan")) ||
      !forced_decisions.contains(QStringLiteral("needs_user_input")) ||
      !forced_decisions.contains(QStringLiteral("cannot_complete_safely")) ||
      !schema_branch_for_decision(forced_schema, QStringLiteral("edit_intent")).isEmpty() ||
      forced_tool_branch.isEmpty() ||
      !forced_tool_branch.value(QStringLiteral("required")).toArray()
           .contains(QStringLiteral("toolCalls")) ||
      forced_tool_calls_schema.value(QStringLiteral("minItems")).toInt() != 1 ||
      forced_tool_item_variants.size() != 1 ||
      forced_tool_item_variants.first().toObject()
              .value(QStringLiteral("properties")).toObject()
              .value(QStringLiteral("name")).toObject()
              .value(QStringLiteral("enum")).toArray() !=
          QJsonArray{QStringLiteral("photo.edit_composition")} ||
      !forced_system.contains(QStringLiteral("do not return edit_intent")) ||
      !forced_prompt.contains(QStringLiteral("Required approved document work remains unresolved")) ||
      !forced_prompt.contains(QStringLiteral("Do not force a particular tool")) ||
      !forced_tool_plan.prompt_audit.value(QStringLiteral("toolCallPlanRequired")).toBool())
    return fail("A forced repair turn must expose only tool, question, and safe-stop decisions and accept a typed tool plan");

  const auto forced_edit_intent = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"), QStringLiteral("Try a numeric adjustment instead.")},
      {QStringLiteral("styleConstraintCitations"), valid_citations},
      {QStringLiteral("editIntent"), valid_typed_intent},
  });
  if (forced_edit_intent.ok ||
      !forced_edit_intent.error_message.contains(QStringLiteral("tool_call_plan is required")) ||
      !forced_edit_intent.prompt_audit.value(QStringLiteral("fallbackBlocked")).toBool() ||
      !forced_edit_intent.prompt_audit.value(QStringLiteral("requiredToolPlanRejected")).toBool())
    return fail("A forced repair turn must reject a final numeric edit intent without deterministic fallback");

  const auto forced_question = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("needs_user_input")},
      {QStringLiteral("summary"), QStringLiteral("A missing fact is needed before continuing.")},
      {QStringLiteral("userQuestions"), QJsonArray{QStringLiteral("Which visible subject should be placed in front?")}},
  });
  const auto forced_safe_stop = parse_tool_enabled_content(QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("cannot_complete_safely")},
      {QStringLiteral("summary"), QStringLiteral("The required operation is unavailable.")},
      {QStringLiteral("reason"), QStringLiteral("No supported selection tool is available for the requested cutout.")},
  });
  if (!forced_question.ok ||
      forced_question.decision != QStringLiteral("needs_user_input") ||
      forced_question.user_questions.isEmpty() ||
      !forced_safe_stop.ok ||
      forced_safe_stop.decision != QStringLiteral("cannot_complete_safely") ||
      forced_safe_stop.safe_stop_reason.isEmpty())
    return fail("Forced repair must retain valid clarification and safe-stop decisions");

  auto forced_without_tools = request;
  forced_without_tools.available_tool_schemas.clear();
  const auto unavailable_forced_plan =
      photo_editor::desktop::plan_agent_recipe_patch(forced_without_tools);
  if (unavailable_forced_plan.ok ||
      !unavailable_forced_plan.prompt_audit.value(QStringLiteral("fallbackBlocked")).toBool() ||
      unavailable_forced_plan.prompt_audit.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("required_tools_unavailable"))
    return fail("A forced tool-plan request with no supported tools must fail before planning or fallback");

  request.available_tool_schemas = inspection_schemas;
  if (!saved_fake_response.isEmpty()) qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON", saved_fake_response);
  else qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  request.allow_tool_call_plan = false;
  request.require_tool_call_plan = false;
  request.available_tool_schemas = {};

  const QJsonObject legacy_crop_intent{
      {QStringLiteral("goalSummary"), QStringLiteral("Tighten the crop as requested and flag the material crop change for review.")},
      {QStringLiteral("confidence"), 0.91},
      {QStringLiteral("crop"), QStringLiteral("tighter")},
      {QStringLiteral("requiresApproval"), QJsonArray{QStringLiteral("large_crop")}},
  };
  const QJsonObject legacy_crop_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"), QJsonObject{
          {QStringLiteral("role"), QStringLiteral("assistant")},
          {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(QJsonObject{
              {QStringLiteral("decision"), QStringLiteral("edit_intent")},
              {QStringLiteral("summary"), QStringLiteral("A tighter crop is prepared for review.")},
              {QStringLiteral("styleConstraintCitations"), valid_citations},
              {QStringLiteral("editIntent"), legacy_crop_intent},
          }).toJson(QJsonDocument::Compact))},
      }},
  };
  qputenv(QStringLiteral("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON").toUtf8(),
      QJsonDocument(legacy_crop_response).toJson(QJsonDocument::Compact));
  auto legacy_crop_request = request;
  legacy_crop_request.force_model_planning = true;
  legacy_crop_request.allow_deterministic_fallback = false;
  legacy_crop_request.user_instructions = QStringLiteral("Make this crop tighter; flag a large crop for approval.");
  const auto legacy_crop_plan = photo_editor::desktop::plan_agent_recipe_patch(legacy_crop_request);
  if (!legacy_crop_plan.ok ||
      legacy_crop_plan.prompt_audit.value(QStringLiteral("modelDecision")).toString() != QStringLiteral("edit_intent") ||
      legacy_crop_plan.edit_intent.value(QStringLiteral("globalChangePolicy")).toString() != QStringLiteral("allow") ||
      !legacy_crop_plan.edit_intent.value(QStringLiteral("requiresApproval")).toStringList()
           .contains(QStringLiteral("large_crop")))
    return fail("Legacy non-tool edit intents may omit globalChangePolicy while retaining the normalized default and crop approval requirement");

  const QJsonObject needs_input_plan{
      {QStringLiteral("decision"), QStringLiteral("needs_user_input")},
      {QStringLiteral("summary"), QStringLiteral("Need the target mood before editing.")},
      {QStringLiteral("userQuestions"),
       QJsonArray{QStringLiteral("Should the set lean warm editorial or neutral documentary?")}},
  };
  const QJsonObject fake_needs_input_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(needs_input_plan).toJson(QJsonDocument::Compact))},
       }},
  };
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      QJsonDocument(fake_needs_input_response).toJson(QJsonDocument::Compact));
  const auto ollama_needs_input = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (!ollama_needs_input.ok ||
      ollama_needs_input.decision != QStringLiteral("needs_user_input") ||
      ollama_needs_input.user_questions.size() != 1 ||
      !ollama_needs_input.recipe_patch.isEmpty()) {
    return fail("Ollama backend should parse needs_user_input as a safe non-draft decision");
  }

  auto empty_questions_plan = needs_input_plan;
  empty_questions_plan.insert(QStringLiteral("userQuestions"), QJsonArray{QStringLiteral(" ")});
  auto empty_questions_response = fake_needs_input_response;
  auto empty_questions_message = empty_questions_response.value(QStringLiteral("message")).toObject();
  empty_questions_message.insert(QStringLiteral("content"), QString::fromUtf8(
      QJsonDocument(empty_questions_plan).toJson(QJsonDocument::Compact)));
  empty_questions_response.insert(QStringLiteral("message"), empty_questions_message);
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      QJsonDocument(empty_questions_response).toJson(QJsonDocument::Compact));
  const auto empty_questions_result = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (empty_questions_result.ok ||
      !empty_questions_result.error_message.contains(QStringLiteral("nonempty string userQuestions")) ||
      !empty_questions_result.recipe_patch.isEmpty())
    return fail("needs_user_input must be rejected when its typed question list has no nonempty question");

  const QJsonObject cannot_complete_plan{
      {QStringLiteral("decision"), QStringLiteral("cannot_complete_safely")},
      {QStringLiteral("summary"), QStringLiteral("Requested retouch exceeds the active rulebook.")},
      {QStringLiteral("reason"), QStringLiteral("The selected style disallows retouch and inpaint tools.")},
  };
  const QJsonObject fake_cannot_complete_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(cannot_complete_plan).toJson(QJsonDocument::Compact))},
       }},
  };
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      QJsonDocument(fake_cannot_complete_response).toJson(QJsonDocument::Compact));
  const auto ollama_cannot_complete = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (!ollama_cannot_complete.ok ||
      ollama_cannot_complete.decision != QStringLiteral("cannot_complete_safely") ||
      ollama_cannot_complete.safe_stop_reason !=
          QStringLiteral("The selected style disallows retouch and inpaint tools.") ||
      !ollama_cannot_complete.recipe_patch.isEmpty()) {
    return fail("Ollama backend should parse cannot_complete_safely as a safe non-draft decision");
  }

  auto missing_reason_plan = cannot_complete_plan;
  missing_reason_plan.remove(QStringLiteral("reason"));
  auto missing_reason_response = fake_cannot_complete_response;
  auto missing_reason_message = missing_reason_response.value(QStringLiteral("message")).toObject();
  missing_reason_message.insert(QStringLiteral("content"), QString::fromUtf8(
      QJsonDocument(missing_reason_plan).toJson(QJsonDocument::Compact)));
  missing_reason_response.insert(QStringLiteral("message"), missing_reason_message);
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      QJsonDocument(missing_reason_response).toJson(QJsonDocument::Compact));
  const auto missing_reason_result = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (missing_reason_result.ok ||
      !missing_reason_result.error_message.contains(QStringLiteral("must include a reason")) ||
      !missing_reason_result.recipe_patch.isEmpty())
    return fail("cannot_complete_safely must be rejected when its required reason is absent");

  const QJsonObject incomplete_recipe_plan{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"), QStringLiteral("Natural portrait direction without a completed intent.")},
      {QStringLiteral("styleConstraintCitations"),
       QJsonArray{QJsonObject{
           {QStringLiteral("field"), QStringLiteral("constraints.max_exposure_shift")},
           {QStringLiteral("reason"), QStringLiteral("Keep the edit conservative.")},
       }}},
      {QStringLiteral("warnings"), QJsonArray{}},
  };
  const QJsonObject fake_incomplete_recipe_response{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"),
            QString::fromUtf8(
                QJsonDocument(incomplete_recipe_plan).toJson(QJsonDocument::Compact))},
       }},
  };
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      QJsonDocument(fake_incomplete_recipe_response).toJson(QJsonDocument::Compact));
  request.user_instructions = QStringLiteral("Make it bright and colorful.");
  const auto recovered_incomplete_plan =
      photo_editor::desktop::plan_agent_recipe_patch(request);
  if (!recovered_incomplete_plan.ok || recovered_incomplete_plan.recipe_patch.isEmpty() ||
      !recovered_incomplete_plan.prompt_audit.value(QStringLiteral("fallbackUsed")).toBool() ||
      recovered_incomplete_plan.prompt_audit.value(QStringLiteral("requestedModelId")).toString() !=
          QStringLiteral("ollama-qwen-test") ||
      recovered_incomplete_plan.warnings.isEmpty()) {
    return fail("incomplete Ollama recipe responses should automatically produce a safe deterministic draft");
  }
  request.user_instructions = QStringLiteral("Warm only the entire background and keep the people unchanged.");
  const auto semantic_command = photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto command_masks = semantic_command.recipe_patch.value(QStringLiteral("masks")).toMap().value(QStringLiteral("appendMaskStack")).toList();
  if (!semantic_command.ok || semantic_command.recipe_patch.size() != 1 || command_masks.size() != 1 ||
      semantic_command.prompt_audit.value(QStringLiteral("modelInvoked")).toBool() ||
      semantic_command.prompt_audit.value(QStringLiteral("plannerMode")).toString() != QStringLiteral("semantic-command-v1") ||
      command_masks.first().toMap().value(QStringLiteral("agentSemanticTarget")).toString() != QStringLiteral("background") ||
      !command_masks.first().toMap().value(QStringLiteral("agentRequireSemanticMask")).toBool() ||
      command_masks.first().toMap().value(QStringLiteral("temperature")).toDouble() != 12.0 ||
      !photo_editor::desktop::validate_agent_recipe_patch(semantic_command.recipe_patch, request.style).valid)
    return fail("a complete simple local command should compile only a required semantic mask with editable adjustments");
  request.user_instructions = QStringLiteral("Warm the background and brighten the face.");
  const auto compound_command = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (compound_command.ok || compound_command.prompt_audit.value(QStringLiteral("plannerMode")).toString() == QStringLiteral("semantic-command-v1"))
    return fail("the fast semantic grammar must not discard extra requested edits");
  request.user_instructions = QStringLiteral("Warm only the entire background and keep the people unchanged.");
  request.force_model_planning = true;
  const auto protected_failure = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (protected_failure.ok || !protected_failure.recipe_patch.isEmpty() ||
      !protected_failure.prompt_audit.value(QStringLiteral("fallbackBlocked")).toBool())
    return fail("a failed localized planner must never become a global deterministic preset");
  request.force_model_planning = false;
  request.user_instructions = QStringLiteral("Make it bright and colorful.");
  request.allow_deterministic_fallback = false;
  const auto confirmed_failure = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (confirmed_failure.ok || !confirmed_failure.recipe_patch.isEmpty())
    return fail("confirmed search/edit requests must not silently substitute another planner");
  request.allow_deterministic_fallback = true;

  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  if (fake_ollama_server_available) {
    fake_ollama_server.setErrorResponse(
        QStringLiteral("/api/chat"),
        503,
        QJsonObject{{QStringLiteral("error"), QStringLiteral("model worker unavailable")}});
    const auto recovered_http_failure =
        photo_editor::desktop::plan_agent_recipe_patch(request);
    const auto failed_transport =
        recovered_http_failure.prompt_audit.value(QStringLiteral("ollamaAttempt"))
            .toMap()
            .value(QStringLiteral("transport"))
            .toMap();
    if (!recovered_http_failure.ok || recovered_http_failure.recipe_patch.isEmpty() ||
        !recovered_http_failure.prompt_audit.value(QStringLiteral("fallbackUsed")).toBool() ||
        failed_transport.value(QStringLiteral("errorCode")).toString() !=
            QStringLiteral("http_status") ||
        failed_transport.value(QStringLiteral("statusCode")).toInt() != 503) {
      std::cerr << "Ollama HTTP recovery result ok="
                << recovered_http_failure.ok
                << " recipeEmpty="
                << recovered_http_failure.recipe_patch.isEmpty()
                << " error=\""
                << recovered_http_failure.error_message.toStdString()
                << "\" audit="
                << QJsonDocument::fromVariant(
                       recovered_http_failure.prompt_audit)
                       .toJson(QJsonDocument::Compact)
                       .toStdString()
                << '\n';
      return fail("Ollama HTTP failures should automatically produce a safe deterministic draft");
    }
  }

  request.model_id = QStringLiteral("missing-model");
  const auto missing_model_plan = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (missing_model_plan.ok || missing_model_plan.error_message.isEmpty()) {
    return fail("unsupported local model ids should be rejected");
  }

  // Managed readiness must not rewrite explicit external endpoint authority.
  if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail("managed manifest fixture");
  manifest_file.write(R"json({"models":[
    {"id":"managed-test","backend":"ollama","runtime":"managed","model":"fixture:vision","vision":true},
    {"id":"external-test","backend":"ollama","endpoint":"http://127.0.0.1:11434","model":"external:vision","vision":true},
    {"id":"fast-existing-test","backend":"ollama","runtime":"external","endpoint":"http://127.0.0.1:11434","model":"fast:vision","vision":true,"existingOllamaCandidate":true,"requestTimeoutMs":60000}
  ]})json");
  manifest_file.close();
  photo_editor::desktop::publish_managed_agent_runtime({{"ready", false}, {"state", "starting"}});
  auto managed = model_by_id(photo_editor::desktop::available_agent_local_models(), "managed-test");
  if (managed.value("available").toBool() || !managed.value("endpoint").toString().isEmpty() ||
      photo_editor::desktop::default_agent_local_model_id() != "managed-test")
    return fail("a managed model must remain selected but unavailable while its owned runtime starts");
  const auto existing_id = QStringLiteral("managed-test-existing-ollama");
  auto existing = model_by_id(photo_editor::desktop::available_agent_local_models(), existing_id);
  if (existing.value("runtime").toString() != "external" || existing.value("available").toBool() ||
      existing.value("endpoint").toString() != "http://127.0.0.1:11434" ||
      existing.contains("artifactVerification"))
    return fail("managed models should offer an existing Ollama connection that requires its own readiness check");
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON", QByteArrayLiteral("{\"details\":{\"family\":\"qwen3vl\"},\"capabilities\":[\"vision\"]}"));
  if (model_by_id(photo_editor::desktop::available_agent_local_models(), "fast-existing-test").value("available").toBool())
    return fail("a registered installed-model candidate requires an explicit connection check");
  const auto fast_probe = photo_editor::desktop::probe_agent_local_model("fast-existing-test");
  const auto fast_existing = model_by_id(photo_editor::desktop::available_agent_local_models(), "fast-existing-test");
  if (!fast_probe.value("available").toBool() || !fast_existing.value("available").toBool() ||
      fast_existing.value("requestTimeoutMs").toInt() != 60000 || fast_existing.value("runtime").toString() != "external")
    return fail("checking a fast installed model must retain its external connection and shorter deadline");
  const auto existing_probe = photo_editor::desktop::probe_agent_local_model(existing_id);
  existing = model_by_id(photo_editor::desktop::available_agent_local_models(), existing_id);
  if (!existing_probe.value("ok").toBool() || !existing.value("available").toBool() ||
      !existing.value("statusReason").toString().isEmpty() ||
      photo_editor::desktop::default_agent_local_model_id() != "managed-test")
    return fail("a successful existing model probe should survive metadata reload without silently changing the selected runtime");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  photo_editor::desktop::publish_managed_agent_runtime({{"ready", true}, {"state", "ready"},
      {"endpoint", "http://127.0.0.1:32123"}, {"installedModels", QStringList{}}});
  managed = model_by_id(photo_editor::desktop::available_agent_local_models(), "managed-test");
  if (managed.value("available").toBool() || !managed.value("statusReason").toString().contains("Download"))
    return fail("server readiness alone does not prove model installation");
  photo_editor::desktop::publish_managed_agent_runtime({{"ready", true}, {"state", "ready"},
      {"endpoint", "http://127.0.0.1:32123"}, {"installedModels", QStringList{"fixture:vision"}}});
  managed = model_by_id(photo_editor::desktop::available_agent_local_models(), "managed-test");
  if (managed.value("available").toBool() ||
      photo_editor::desktop::probe_agent_local_model("managed-test").value("ok").toBool())
    return fail("an installed managed model must not bypass artifact verification through a probe");
  photo_editor::desktop::publish_managed_agent_runtime({{"ready", true}, {"state", "ready"},
      {"endpoint", "http://127.0.0.1:32123"}, {"installedModels", QStringList{"fixture:vision"}},
      {"verifiedModels", QStringList{"fixture:vision"}}});
  const auto managed_models = photo_editor::desktop::available_agent_local_models();
  managed = model_by_id(managed_models, "managed-test");
  if (!managed.value("available").toBool() || managed.value("endpoint").toString() != "http://127.0.0.1:32123" ||
      model_by_id(managed_models, "external-test").value("endpoint").toString() != "http://127.0.0.1:11434")
    return fail("owned runtime endpoint routing must leave external model configurations untouched");
  photo_editor::desktop::publish_managed_agent_runtime({});
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH");
  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_RUN_LIVE_OLLAMA_SMOKE")) {
    const auto live_app_data_root =
        qEnvironmentVariable("PHOTO_EDITOR_LIVE_OLLAMA_APP_DATA_DIR").trimmed();
    const auto live_model_id =
        qEnvironmentVariable(
            "PHOTO_EDITOR_LIVE_OLLAMA_MODEL_ID",
            QStringLiteral("ollama-qwen3-vl-32b"))
            .trimmed();
    if (live_app_data_root.isEmpty()) {
      return fail("live Ollama smoke requires PHOTO_EDITOR_LIVE_OLLAMA_APP_DATA_DIR");
    }
    qputenv("PHOTO_EDITOR_APP_DATA_DIR", live_app_data_root.toUtf8());

    const auto live_probe =
        photo_editor::desktop::probe_agent_local_model(live_model_id);
    if (!live_probe.value(QStringLiteral("ok")).toBool() ||
        !live_probe.value(QStringLiteral("available")).toBool() ||
        live_probe.value(QStringLiteral("backend")).toString() !=
            QStringLiteral("ollama")) {
      std::cerr << "live Ollama probe failed: "
                << QJsonDocument::fromVariant(live_probe)
                       .toJson(QJsonDocument::Compact)
                       .toStdString()
                << '\n';
      return fail("configured live Ollama vision model should be reachable");
    }

    photo_editor::desktop::AgentLocalModelPlanRequest live_request;
    live_request.model_id = live_model_id;
    live_request.style = warm_style;
    live_request.asset = QVariantMap{
        {QStringLiteral("assetId"), QStringLiteral("live-runtime-smoke")},
        {QStringLiteral("filePath"), fast_highlight_path},
    };
    live_request.current_adjustment_patch_json = QStringLiteral(
        "{\"kind\":\"photo-editor-adjustment-patch\",\"version\":1,"
        "\"scope\":\"asset-bound\",\"groups\":{}}");
    live_request.user_instructions = QStringLiteral(
        "Create a restrained cinematic evening look with gentle contrast and "
        "natural color. Keep the framing unchanged and protect the brightest "
        "area from clipping.");
    live_request.strategy_phase = QStringLiteral("sample");
    live_request.force_model_planning = true;

    QElapsedTimer live_plan_timer;
    live_plan_timer.start();
    const auto live_plan =
        photo_editor::desktop::plan_agent_recipe_patch(live_request);
    const auto live_plan_elapsed_ms = live_plan_timer.elapsed();
    const auto live_plan_transport =
        live_plan.prompt_audit.value(QStringLiteral("transport")).toMap();
    const auto live_generation_schema_contract =
        live_plan.prompt_audit
            .value(QStringLiteral("generationSchemaContract"))
            .toString();
    const auto live_generation_schema_byte_count =
        live_plan.prompt_audit
            .value(QStringLiteral("generationSchemaByteCount"))
            .toInt();
    const auto live_scope_correction =
        live_plan.prompt_audit
            .value(QStringLiteral("globalChangePolicyCorrection"))
            .toMap();
    if (!live_plan.ok || live_plan.recipe_patch.isEmpty() ||
        live_plan.edit_intent.isEmpty() ||
        live_plan.prompt_audit.value(QStringLiteral("backend")).toString() !=
            QStringLiteral("ollama") ||
        live_plan.prompt_audit.value(QStringLiteral("fallbackUsed")).toBool() ||
        !live_plan.prompt_audit.value(QStringLiteral("imageIncluded")).toBool() ||
        live_generation_schema_contract !=
            QStringLiteral("agent-operation-discriminated-generation-v2") ||
        live_generation_schema_byte_count <= 0 ||
        live_generation_schema_byte_count >= 12000 ||
        live_plan.edit_intent.value(QStringLiteral("globalChangePolicy")).toString() !=
            QStringLiteral("allow") ||
        (!live_scope_correction.isEmpty() &&
         (live_scope_correction.value(QStringLiteral("contract")).toString() !=
              QStringLiteral("agent-explicit-scope-authority-v1") ||
          !live_scope_correction
               .value(QStringLiteral("currentRequestAuthoritative"))
               .toBool()))) {
      std::cerr << "live Ollama planning failed: "
                << live_plan.error_message.toStdString() << " audit="
                << QJsonDocument::fromVariant(live_plan.prompt_audit)
                       .toJson(QJsonDocument::Compact)
                       .toStdString()
                << '\n';
      return fail("live Ollama should return one usable image-grounded edit plan without fallback");
    }

    photo_editor::desktop::AgentLocalModelProofCritiqueRequest critique_request;
    critique_request.model_id = live_model_id;
    critique_request.style = warm_style;
    critique_request.asset = live_request.asset;
    // This smoke fixture is explicitly the proof under review, not a fallback
    // from a missing production after image to an original photograph.
    critique_request.asset.insert(QStringLiteral("proofImagePath"), fast_highlight_path);
    critique_request.user_instructions = live_request.user_instructions;
    critique_request.validation_report = QVariantMap{
        {QStringLiteral("ok"), true},
        {QStringLiteral("intentMatch"),
         QVariantMap{
             {QStringLiteral("available"), true},
             {QStringLiteral("ok"), true},
             {QStringLiteral("scorePercent"), 96},
         }},
        {QStringLiteral("warnings"), QVariantList{}},
        {QStringLiteral("errors"), QVariantList{}},
    };
    QElapsedTimer live_critique_timer;
    live_critique_timer.start();
    const auto live_critique =
        photo_editor::desktop::critique_agent_rendered_proof(
            critique_request);
    const auto live_critique_elapsed_ms = live_critique_timer.elapsed();
    if (!live_critique.ok || live_critique.summary.trimmed().isEmpty() ||
        live_critique.taste_confidence.trimmed().isEmpty() ||
        live_critique.prompt_audit.value(QStringLiteral("backend")).toString() !=
            QStringLiteral("ollama") ||
        !live_critique.prompt_audit
             .value(QStringLiteral("imageIncluded"))
             .toBool()) {
      std::cerr << "live Ollama proof critique failed: "
                << live_critique.error_message.toStdString() << " audit="
                << QJsonDocument::fromVariant(live_critique.prompt_audit)
                       .toJson(QJsonDocument::Compact)
                       .toStdString()
                << '\n';
      return fail("live Ollama should return one usable image-grounded proof critique");
    }

    const QVariantMap live_receipt{
        {QStringLiteral("contract"),
         QStringLiteral("agent-live-local-runtime-smoke-v1")},
        {QStringLiteral("modelId"), live_model_id},
        {QStringLiteral("ollamaModel"),
         live_probe.value(QStringLiteral("ollamaModel"))},
        {QStringLiteral("modelFamily"),
         live_probe.value(QStringLiteral("modelFamily"))},
        {QStringLiteral("parameterSize"),
         live_probe.value(QStringLiteral("parameterSize"))},
        {QStringLiteral("quantizationLevel"),
         live_probe.value(QStringLiteral("quantizationLevel"))},
        {QStringLiteral("probeAvailable"), true},
        {QStringLiteral("planning"),
         QVariantMap{
             {QStringLiteral("ok"), true},
             {QStringLiteral("decision"), live_plan.decision},
             {QStringLiteral("summary"), live_plan.plan_summary},
             {QStringLiteral("imageIncluded"), true},
             {QStringLiteral("fallbackUsed"), false},
             {QStringLiteral("elapsedMs"), live_plan_elapsed_ms},
             {QStringLiteral("transport"), live_plan_transport},
             {QStringLiteral("ollamaServer"),
              live_plan.prompt_audit.value(QStringLiteral("ollamaServer"))},
             {QStringLiteral("responseCharCount"),
              live_plan.prompt_audit.value(QStringLiteral("responseCharCount"))},
             {QStringLiteral("generationSchemaContract"),
              live_generation_schema_contract},
             {QStringLiteral("generationSchemaByteCount"),
              live_generation_schema_byte_count},
             {QStringLiteral("globalChangePolicy"),
              live_plan.edit_intent.value(QStringLiteral("globalChangePolicy"))},
             {QStringLiteral("globalChangePolicyCorrection"),
              live_scope_correction},
         }},
        {QStringLiteral("proofCritique"),
         QVariantMap{
             {QStringLiteral("ok"), true},
             {QStringLiteral("pass"), live_critique.pass},
             {QStringLiteral("needsUserReview"),
              live_critique.needs_user_review},
             {QStringLiteral("summary"), live_critique.summary},
             {QStringLiteral("tasteConfidence"),
              live_critique.taste_confidence},
             {QStringLiteral("imageIncluded"), true},
             {QStringLiteral("elapsedMs"), live_critique_elapsed_ms},
             {QStringLiteral("transport"),
              live_critique.prompt_audit
                  .value(QStringLiteral("transport"))},
             {QStringLiteral("ollamaServer"),
              live_critique.prompt_audit
                  .value(QStringLiteral("ollamaServer"))},
         }},
        {QStringLiteral("localOnly"), true},
        {QStringLiteral("cloudUsed"), false},
        {QStringLiteral("automaticCommitAllowed"), false},
        {QStringLiteral("photographerApprovalRequired"), true},
    };
    const auto receipt_path =
        qEnvironmentVariable("PHOTO_EDITOR_LIVE_OLLAMA_RECEIPT_PATH")
            .trimmed();
    if (!receipt_path.isEmpty()) {
      QFile receipt_file(receipt_path);
      if (!QDir().mkpath(QFileInfo(receipt_path).absolutePath()) ||
          !receipt_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
          receipt_file.write(
              QJsonDocument::fromVariant(live_receipt)
                  .toJson(QJsonDocument::Indented)) <= 0) {
        return fail("live Ollama smoke receipt should be writable");
      }
      receipt_file.close();
      std::cout << "live_ollama_receipt="
                << receipt_path.toStdString() << '\n';
    }
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  }

  return EXIT_SUCCESS;
}

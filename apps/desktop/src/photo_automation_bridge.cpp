#include "photo_automation_bridge.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonParseError>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QHostAddress>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUuid>
#include <QVariantMap>
#include <QVector>

#include <tuple>

#include "agent_edit_controller.h"
#include "agent_edit_recipe_patch.h"
#include "app_bootstrap.h"
#include "catalog_asset_model.h"
#include "photo_mcp_server.h"

namespace photo_editor::desktop {
namespace {

constexpr auto kProtocol = "photo-editor-automation-jsonl-v1";

QJsonObject ok_response(const QJsonObject& result = {}) {
  return QJsonObject{
      {QStringLiteral("ok"), true},
      {QStringLiteral("result"), result},
  };
}

QJsonObject error_response(const QString& message) {
  return QJsonObject{
      {QStringLiteral("ok"), false},
      {QStringLiteral("error"), message},
  };
}

bool has_only_keys(const QJsonObject& object, const QStringList& keys) {
  for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
    if (!keys.contains(iterator.key())) {
      return false;
    }
  }
  return true;
}

bool is_local_http_origin(const QString& value) {
  return value == QStringLiteral("http://127.0.0.1") ||
         value == QStringLiteral("http://localhost") ||
         value.startsWith(QStringLiteral("http://127.0.0.1:")) ||
         value.startsWith(QStringLiteral("http://localhost:"));
}

bool retouch_key_requires_confirmation(const QString& key) {
  const auto normalized = key.trimmed();
  return normalized == QStringLiteral("retouch") ||
         normalized == QStringLiteral("retouchStack") ||
         normalized == QStringLiteral("retouchMode") ||
         normalized == QStringLiteral("removeStrength") ||
         normalized.startsWith(QStringLiteral("inpaint")) ||
         normalized.startsWith(QStringLiteral("clone")) ||
         normalized.startsWith(QStringLiteral("patchSource")) ||
         normalized == QStringLiteral("healTextureBlend") ||
         normalized == QStringLiteral("healColorBlend");
}

bool value_contains_retouch_patch(const QJsonValue& value) {
  if (value.isObject()) {
    const auto object = value.toObject();
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
      if (retouch_key_requires_confirmation(iterator.key()) ||
          value_contains_retouch_patch(iterator.value())) {
        return true;
      }
    }
  } else if (value.isArray()) {
    const auto array = value.toArray();
    for (const auto& item : array) {
      if (value_contains_retouch_patch(item)) {
        return true;
      }
    }
  }
  return false;
}

QJsonObject variant_map_to_json_object(const QVariantMap& value) {
  return QJsonObject::fromVariantMap(value);
}

QJsonArray variant_list_to_json_array(const QVariantList& value) {
  return QJsonArray::fromVariantList(value);
}

QVariantMap asset_for_bridge_request(const CatalogAssetModel* catalog_model, const QJsonObject& args) {
  if (catalog_model == nullptr) {
    return {};
  }
  const auto requested_asset_id = args.value(QStringLiteral("assetId")).toString().trimmed();
  if (requested_asset_id.isEmpty()) {
    return catalog_model->assetAt(catalog_model->selectedIndex());
  }
  for (auto index = 0; index < catalog_model->assetCount(); ++index) {
    auto asset = catalog_model->assetAt(index);
    if (asset.value(QStringLiteral("assetId")).toString() == requested_asset_id) {
      asset.insert(QStringLiteral("visibleIndex"), index);
      return asset;
    }
  }
  return {};
}

bool contains_only_optional_numbers(
    const QJsonObject& object,
    const QStringList& number_keys,
    QString* invalid_key) {
  for (const auto& key : number_keys) {
    const auto value = object.value(key);
    if (!value.isUndefined() && !value.isDouble()) {
      if (invalid_key != nullptr) {
        *invalid_key = key;
      }
      return false;
    }
  }
  return true;
}

bool contains_only_optional_numbers_or_strings(
    const QJsonObject& object,
    const QStringList& number_keys,
    const QStringList& string_keys,
    QString* invalid_key) {
  for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
    const auto key = iterator.key();
    const auto value = iterator.value();
    if (number_keys.contains(key)) {
      if (!value.isDouble()) {
        if (invalid_key != nullptr) {
          *invalid_key = key;
        }
        return false;
      }
    } else if (string_keys.contains(key)) {
      if (!value.isString()) {
        if (invalid_key != nullptr) {
          *invalid_key = key;
        }
        return false;
      }
    } else {
      if (invalid_key != nullptr) {
        *invalid_key = key;
      }
      return false;
    }
  }
  return true;
}

bool bounded_number(
    const QJsonObject& object,
    const QString& key,
    double minimum,
    double maximum,
    QString* invalid_key) {
  const auto value = object.value(key);
  if (value.isUndefined()) {
    return true;
  }
  const auto number = value.toDouble();
  if (number < minimum || number > maximum) {
    if (invalid_key != nullptr) {
      *invalid_key = key;
    }
    return false;
  }
  return true;
}

QVariantMap mask_patch_from_args(const QJsonObject& args, bool create_defaults) {
  const auto type = args.value(QStringLiteral("type")).toString();
  QVariantMap masks;
  const auto insert_number = [&](const QString& arg_key, const QString& patch_key) {
    if (!args.value(arg_key).isUndefined()) {
      masks.insert(patch_key, args.value(arg_key).toDouble());
    }
  };
  if (type == QStringLiteral("linear")) {
    if (create_defaults) {
      masks.insert(QStringLiteral("linearMaskExposure"), 0.01);
    }
    insert_number(QStringLiteral("exposure"), QStringLiteral("linearMaskExposure"));
    insert_number(QStringLiteral("temperature"), QStringLiteral("linearMaskTemperature"));
    insert_number(QStringLiteral("feather"), QStringLiteral("linearMaskFeather"));
    insert_number(QStringLiteral("position"), QStringLiteral("linearMaskPosition"));
    insert_number(QStringLiteral("rotation"), QStringLiteral("linearMaskRotation"));
  } else if (type == QStringLiteral("radial")) {
    if (create_defaults) {
      masks.insert(QStringLiteral("radialMaskExposure"), 0.01);
    }
    insert_number(QStringLiteral("exposure"), QStringLiteral("radialMaskExposure"));
    insert_number(QStringLiteral("temperature"), QStringLiteral("radialMaskTemperature"));
    insert_number(QStringLiteral("feather"), QStringLiteral("radialMaskFeather"));
    insert_number(QStringLiteral("centerX"), QStringLiteral("radialMaskCenterX"));
    insert_number(QStringLiteral("centerY"), QStringLiteral("radialMaskCenterY"));
    insert_number(QStringLiteral("radiusX"), QStringLiteral("radialMaskRadiusX"));
    insert_number(QStringLiteral("radiusY"), QStringLiteral("radialMaskRadiusY"));
    insert_number(QStringLiteral("rotation"), QStringLiteral("radialMaskRotation"));
  }
  return QVariantMap{{QStringLiteral("masks"), masks}};
}

bool valid_mask_args(const QJsonObject& args, bool create_defaults, QString* error_message) {
  const QStringList string_keys{QStringLiteral("type")};
  const QStringList number_keys{
      QStringLiteral("exposure"),
      QStringLiteral("temperature"),
      QStringLiteral("feather"),
      QStringLiteral("position"),
      QStringLiteral("rotation"),
      QStringLiteral("centerX"),
      QStringLiteral("centerY"),
      QStringLiteral("radiusX"),
      QStringLiteral("radiusY"),
  };
  QString invalid_key;
  if (!contains_only_optional_numbers_or_strings(args, number_keys, string_keys, &invalid_key)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Mask command has invalid or unknown field: %1").arg(invalid_key);
    }
    return false;
  }
  const auto type = args.value(QStringLiteral("type")).toString();
  if (type != QStringLiteral("linear") && type != QStringLiteral("radial")) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Mask command requires type linear or radial.");
    }
    return false;
  }
  const QStringList required_for_create =
      type == QStringLiteral("linear")
          ? QStringList{QStringLiteral("position"), QStringLiteral("rotation")}
          : QStringList{QStringLiteral("centerX"), QStringLiteral("centerY"), QStringLiteral("radiusX"), QStringLiteral("radiusY")};
  if (create_defaults) {
    for (const auto& key : required_for_create) {
      if (args.value(key).isUndefined()) {
        if (error_message != nullptr) {
          *error_message = QStringLiteral("photo_create_mask requires %1 for %2 masks.").arg(key, type);
        }
        return false;
      }
    }
  } else {
    auto has_adjustment = false;
    for (const auto& key : number_keys) {
      if (!args.value(key).isUndefined()) {
        has_adjustment = true;
        break;
      }
    }
    if (!has_adjustment) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("photo_apply_mask_adjustment requires at least one numeric mask field.");
      }
      return false;
    }
  }
  const QVector<std::tuple<QString, double, double>> bounds{
      {QStringLiteral("exposure"), -5.0, 5.0},
      {QStringLiteral("temperature"), -100.0, 100.0},
      {QStringLiteral("feather"), 0.0, 1.0},
      {QStringLiteral("position"), 0.0, 1.0},
      {QStringLiteral("rotation"), -180.0, 180.0},
      {QStringLiteral("centerX"), 0.0, 1.0},
      {QStringLiteral("centerY"), 0.0, 1.0},
      {QStringLiteral("radiusX"), 0.01, 1.0},
      {QStringLiteral("radiusY"), 0.01, 1.0},
  };
  for (const auto& [key, minimum, maximum] : bounds) {
    if (!bounded_number(args, key, minimum, maximum, &invalid_key)) {
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("Mask field %1 must be between %2 and %3.").arg(key).arg(minimum).arg(maximum);
      }
      return false;
    }
  }
  return true;
}

QVariantMap export_preflight_metrics_from_args(const QJsonObject& args) {
  QVariantMap metrics;
  const QStringList metric_keys{
      QStringLiteral("histogramSampleCount"),
      QStringLiteral("shadowClipCount"),
      QStringLiteral("highlightClipCount"),
      QStringLiteral("rawClipSampleCount"),
      QStringLiteral("rawShadowClipCount"),
      QStringLiteral("rawHighlightClipCount"),
  };
  for (const auto& key : metric_keys) {
    if (!args.value(key).isUndefined()) {
      metrics.insert(key, args.value(key).toInt());
    }
  }
  return metrics;
}

}  // namespace

PhotoAutomationBridge::PhotoAutomationBridge(
    AgentEditController* agent_edit,
    CatalogAssetModel* catalog_model,
    QObject* parent)
    : QObject(parent), agent_edit_(agent_edit), catalog_model_(catalog_model) {}

PhotoAutomationBridge::~PhotoAutomationBridge() {
  stop();
}

bool PhotoAutomationBridge::running() const {
  return server_ != nullptr && server_->isListening();
}

QString PhotoAutomationBridge::endpointName() const {
  return endpoint_name_;
}

QString PhotoAutomationBridge::endpointKind() const {
  return endpoint_kind_;
}

QString PhotoAutomationBridge::mcpEndpoint() const {
  return mcp_endpoint_;
}

QString PhotoAutomationBridge::mcpEndpointKind() const {
  return mcp_endpoint_kind_;
}

int PhotoAutomationBridge::mcpConnectedClientCount() const {
  return mcp_connected_client_count_;
}

int PhotoAutomationBridge::mcpTotalClientCount() const {
  return mcp_total_client_count_;
}

QString PhotoAutomationBridge::mcpLastToolCallName() const {
  return mcp_last_tool_call_name_;
}

QString PhotoAutomationBridge::mcpLastToolCallStatus() const {
  return mcp_last_tool_call_status_;
}

QString PhotoAutomationBridge::mcpLastToolCallAt() const {
  return mcp_last_tool_call_at_;
}

QString PhotoAutomationBridge::token() const {
  return token_;
}

QString PhotoAutomationBridge::sessionFilePath() const {
  return session_file_path_;
}

QString PhotoAutomationBridge::errorMessage() const {
  return error_message_;
}

bool PhotoAutomationBridge::start() {
  stop();
  error_message_.clear();
  if (agent_edit_ == nullptr || catalog_model_ == nullptr) {
    error_message_ = QStringLiteral("Automation bridge requires agent and catalog controllers.");
    return false;
  }

  const auto session_dir = QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("automation"));
  if (!QDir().mkpath(session_dir)) {
    error_message_ = QStringLiteral("Unable to create automation runtime directory.");
    return false;
  }
  endpoint_name_ = QStringLiteral("photo-editor-agent-%1").arg(QCoreApplication::applicationPid());
  session_file_path_ = QDir(session_dir).filePath(QStringLiteral("session.json"));
  token_ = QUuid::createUuid().toString(QUuid::WithoutBraces);

  QLocalServer::removeServer(endpoint_name_);
  server_ = new QLocalServer(this);
  connect(server_, &QLocalServer::newConnection, this, &PhotoAutomationBridge::acceptPendingConnections);
  if (!server_->listen(endpoint_name_)) {
    const auto local_error = server_->errorString();
    server_->deleteLater();
    server_ = nullptr;
    tcp_server_ = new QTcpServer(this);
    connect(tcp_server_, &QTcpServer::newConnection, this, &PhotoAutomationBridge::acceptPendingTcpConnections);
    if (!tcp_server_->listen(QHostAddress::LocalHost, 0)) {
      error_message_ =
          QStringLiteral("QLocalServer failed: %1; localhost TCP fallback failed: %2")
              .arg(local_error, tcp_server_->errorString());
      tcp_server_->deleteLater();
      tcp_server_ = nullptr;
      return false;
    }
    endpoint_kind_ = QStringLiteral("tcp-localhost");
    endpoint_name_ = QStringLiteral("127.0.0.1:%1").arg(tcp_server_->serverPort());
  } else {
    endpoint_kind_ = QStringLiteral("qlocalserver");
  }

  mcp_server_ = new PhotoMcpServer(this, this);
  mcp_http_server_ = new QTcpServer(this);
  connect(
      mcp_http_server_,
      &QTcpServer::newConnection,
      this,
      &PhotoAutomationBridge::acceptPendingMcpHttpConnections);
  if (!mcp_http_server_->listen(QHostAddress::LocalHost, 0)) {
    error_message_ = QStringLiteral("MCP localhost HTTP server failed: %1").arg(mcp_http_server_->errorString());
    stop();
    return false;
  }
  mcp_endpoint_kind_ = QStringLiteral("streamable-http-localhost");
  mcp_endpoint_ = QStringLiteral("http://127.0.0.1:%1/mcp").arg(mcp_http_server_->serverPort());

  if (!writeSessionFile()) {
    stop();
    return false;
  }

  return true;
}

void PhotoAutomationBridge::stop() {
  removeSessionFile();
  if (server_ != nullptr) {
    if (server_->isListening()) {
      server_->close();
    }
    QLocalServer::removeServer(endpoint_name_);
    server_->deleteLater();
    server_ = nullptr;
  }
  if (tcp_server_ != nullptr) {
    if (tcp_server_->isListening()) {
      tcp_server_->close();
    }
    tcp_server_->deleteLater();
    tcp_server_ = nullptr;
  }
  if (mcp_http_server_ != nullptr) {
    if (mcp_http_server_->isListening()) {
      mcp_http_server_->close();
    }
    mcp_http_server_->deleteLater();
    mcp_http_server_ = nullptr;
  }
  if (mcp_server_ != nullptr) {
    mcp_server_->deleteLater();
    mcp_server_ = nullptr;
  }
  endpoint_name_.clear();
  endpoint_kind_.clear();
  mcp_endpoint_.clear();
  mcp_endpoint_kind_.clear();
  mcp_connected_client_count_ = 0;
  mcp_total_client_count_ = 0;
  mcp_last_tool_call_name_.clear();
  mcp_last_tool_call_status_.clear();
  mcp_last_tool_call_at_.clear();
  token_.clear();
}

void PhotoAutomationBridge::acceptPendingConnections() {
  if (server_ == nullptr) {
    return;
  }
  while (server_->hasPendingConnections()) {
    auto* socket = server_->nextPendingConnection();
    socket->setParent(this);
    socket->setProperty("buffer", QByteArray{});
    connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { readSocketRequest(socket); });
    connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
  }
}

void PhotoAutomationBridge::acceptPendingTcpConnections() {
  if (tcp_server_ == nullptr) {
    return;
  }
  while (tcp_server_->hasPendingConnections()) {
    auto* socket = tcp_server_->nextPendingConnection();
    socket->setParent(this);
    socket->setProperty("buffer", QByteArray{});
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { readSocketRequest(socket); });
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
  }
}

void PhotoAutomationBridge::acceptPendingMcpHttpConnections() {
  if (mcp_http_server_ == nullptr) {
    return;
  }
  while (mcp_http_server_->hasPendingConnections()) {
    auto* socket = mcp_http_server_->nextPendingConnection();
    socket->setParent(this);
    socket->setProperty("buffer", QByteArray{});
    ++mcp_connected_client_count_;
    ++mcp_total_client_count_;
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { readMcpHttpRequest(socket); });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
      if (mcp_connected_client_count_ > 0) {
        --mcp_connected_client_count_;
      }
      socket->deleteLater();
    });
  }
}

void PhotoAutomationBridge::readSocketRequest(QIODevice* socket) {
  auto buffer = socket->property("buffer").toByteArray();
  buffer.append(socket->readAll());
  auto newline = buffer.indexOf('\n');
  if (newline < 0 && buffer.size() < 1024 * 1024) {
    socket->setProperty("buffer", buffer);
    return;
  }
  if (newline >= 0) {
    buffer = buffer.left(newline);
  }

  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(buffer, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    reply(socket, error_response(QStringLiteral("Request must be a single JSON object.")));
    return;
  }
  reply(socket, handleJsonRequest(document.object()));
}

void PhotoAutomationBridge::readMcpHttpRequest(QTcpSocket* socket) {
  auto buffer = socket->property("buffer").toByteArray();
  buffer.append(socket->readAll());
  const auto header_end = buffer.indexOf("\r\n\r\n");
  if (header_end < 0 && buffer.size() < 1024 * 1024) {
    socket->setProperty("buffer", buffer);
    return;
  }
  if (header_end < 0) {
    replyMcpHttp(
        socket,
        400,
        QByteArrayLiteral("Bad Request"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("Malformed HTTP request.")}});
    return;
  }

  const auto header_bytes = buffer.left(header_end);
  const auto header_lines = header_bytes.split('\n');
  if (header_lines.isEmpty()) {
    replyMcpHttp(
        socket,
        400,
        QByteArrayLiteral("Bad Request"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("Missing HTTP request line.")}});
    return;
  }
  const auto request_line = QString::fromLatin1(header_lines.first()).trimmed();
  const auto request_parts = request_line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (request_parts.size() < 2 ||
      request_parts.at(1) != QStringLiteral("/mcp")) {
    replyMcpHttp(
        socket,
        404,
        QByteArrayLiteral("Not Found"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP endpoint is /mcp.")}});
    return;
  }

  const auto method = request_parts.at(0);
  auto content_length = -1;
  for (int index = 1; index < header_lines.size(); ++index) {
    const auto line = QString::fromLatin1(header_lines.at(index)).trimmed();
    const auto colon = line.indexOf(QLatin1Char(':'));
    if (colon <= 0) {
      continue;
    }
    const auto key = line.left(colon).trimmed().toLower();
    const auto value = line.mid(colon + 1).trimmed();
    if (key == QStringLiteral("content-length")) {
      auto ok = false;
      content_length = value.toInt(&ok);
      if (!ok || content_length < 0 || content_length > 1024 * 1024) {
        replyMcpHttp(
            socket,
            400,
            QByteArrayLiteral("Bad Request"),
            QJsonObject{{QStringLiteral("error"), QStringLiteral("Invalid Content-Length.")}});
        return;
      }
    } else if (key == QStringLiteral("origin")) {
      if (!is_local_http_origin(value)) {
        replyMcpHttp(
            socket,
            403,
            QByteArrayLiteral("Forbidden"),
            QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP localhost endpoint rejects non-local Origin headers.")}});
        return;
      }
    }
  }

  if (method == QStringLiteral("GET")) {
    if (mcp_server_ == nullptr) {
      replyMcpHttp(
          socket,
          503,
          QByteArrayLiteral("Service Unavailable"),
          QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP server is not available.")}});
      return;
    }
    replyMcpHttpSse(socket);
    return;
  }

  if (method != QStringLiteral("POST")) {
    replyMcpHttp(
        socket,
        405,
        QByteArrayLiteral("Method Not Allowed"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP endpoint accepts GET or POST /mcp.")}});
    return;
  }

  if (content_length < 0) {
    replyMcpHttp(
        socket,
        411,
        QByteArrayLiteral("Length Required"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP HTTP requests require Content-Length.")}});
    return;
  }
  const auto body_start = header_end + 4;
  if (buffer.size() - body_start < content_length) {
    socket->setProperty("buffer", buffer);
    return;
  }
  const auto body = buffer.mid(body_start, content_length);
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(body, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    replyMcpHttp(
        socket,
        400,
        QByteArrayLiteral("Bad Request"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP request body must be a JSON-RPC object.")}});
    return;
  }
  if (mcp_server_ == nullptr) {
    replyMcpHttp(
        socket,
        503,
        QByteArrayLiteral("Service Unavailable"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP server is not available.")}});
    return;
  }
  const auto request_object = document.object();
  const auto response = mcp_server_->handleJsonRpc(request_object);
  if (request_object.value(QStringLiteral("method")).toString() == QStringLiteral("tools/call")) {
    const auto params = request_object.value(QStringLiteral("params")).toObject();
    const auto tool_name = params.value(QStringLiteral("name")).toString();
    const auto status = response.contains(QStringLiteral("error")) ||
                                response.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool()
                            ? QStringLiteral("error")
                            : QStringLiteral("ok");
    recordMcpToolCall(tool_name, status);
  }
  replyMcpHttp(socket, 200, QByteArrayLiteral("OK"), response);
}

void PhotoAutomationBridge::reply(QIODevice* socket, const QJsonObject& response) const {
  socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
  socket->write("\n");
  if (auto* local_socket = qobject_cast<QLocalSocket*>(socket)) {
    local_socket->flush();
    local_socket->disconnectFromServer();
  } else if (auto* tcp_socket = qobject_cast<QTcpSocket*>(socket)) {
    tcp_socket->flush();
    tcp_socket->disconnectFromHost();
  }
}

void PhotoAutomationBridge::replyMcpHttp(
    QTcpSocket* socket,
    int status_code,
    const QByteArray& status_text,
    const QJsonObject& response) const {
  const auto body = QJsonDocument(response).toJson(QJsonDocument::Compact);
  QByteArray header;
  header.append("HTTP/1.1 ");
  header.append(QByteArray::number(status_code));
  header.append(' ');
  header.append(status_text);
  header.append("\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ");
  header.append(QByteArray::number(body.size()));
  header.append("\r\n\r\n");
  socket->write(header);
  socket->write(body);
  socket->flush();
  socket->disconnectFromHost();
}

void PhotoAutomationBridge::replyMcpHttpSse(QTcpSocket* socket) const {
  const auto tools_result =
      mcp_server_ != nullptr ? mcp_server_->toolsListResult() : QJsonObject{};
  const auto tools = tools_result.value(QStringLiteral("tools")).toArray();
  QJsonArray tool_names;
  for (const auto& tool_value : tools) {
    const auto name = tool_value.toObject().value(QStringLiteral("name")).toString();
    if (!name.isEmpty()) {
      tool_names.append(name);
    }
  }

  const auto endpoint_event = QJsonObject{
      {QStringLiteral("endpoint"), mcp_endpoint_},
      {QStringLiteral("protocol"), QStringLiteral("model-context-protocol-jsonrpc-2025-06-18")},
      {QStringLiteral("status"), QStringLiteral("ready")},
      {QStringLiteral("transport"), QStringLiteral("streamable-http")},
  };
  const auto tools_event = QJsonObject{
      {QStringLiteral("toolCount"), tools.size()},
      {QStringLiteral("toolNames"), tool_names},
  };

  QByteArray body;
  body.append("event: endpoint\n");
  body.append("data: ");
  body.append(QJsonDocument(endpoint_event).toJson(QJsonDocument::Compact));
  body.append("\n\n");
  body.append("event: tools\n");
  body.append("data: ");
  body.append(QJsonDocument(tools_event).toJson(QJsonDocument::Compact));
  body.append("\n\n");

  QByteArray header;
  header.append("HTTP/1.1 200 OK\r\n");
  header.append("Content-Type: text/event-stream\r\n");
  header.append("Cache-Control: no-cache\r\n");
  header.append("Connection: close\r\n");
  header.append("Content-Length: ");
  header.append(QByteArray::number(body.size()));
  header.append("\r\n\r\n");
  socket->write(header);
  socket->write(body);
  socket->flush();
  socket->disconnectFromHost();
}

QJsonObject PhotoAutomationBridge::handleJsonRequest(const QJsonObject& request) const {
  if (!has_only_keys(
          request,
          {QStringLiteral("id"), QStringLiteral("token"), QStringLiteral("command"), QStringLiteral("args")})) {
    return error_response(QStringLiteral("Request contains unknown fields."));
  }
  if (request.value(QStringLiteral("token")).toString() != token_) {
    return error_response(QStringLiteral("Invalid automation token."));
  }
  const auto command = request.value(QStringLiteral("command")).toString().trimmed();
  if (command.isEmpty()) {
    return error_response(QStringLiteral("Command is required."));
  }
  const auto args_value = request.value(QStringLiteral("args"));
  if (!args_value.isUndefined() && !args_value.isObject()) {
    return error_response(QStringLiteral("Command args must be an object."));
  }

  auto response = handleCommand(command, args_value.toObject());
  if (request.contains(QStringLiteral("id"))) {
    response.insert(QStringLiteral("id"), request.value(QStringLiteral("id")));
  }
  return response;
}

QJsonObject PhotoAutomationBridge::handleCommand(const QString& command, const QJsonObject& args) const {
  if (command == QStringLiteral("photo_live_status")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_live_status does not accept args."));
    }
    return ok_response(QJsonObject{
        {QStringLiteral("appRunning"), true},
        {QStringLiteral("protocol"), QString::fromLatin1(kProtocol)},
        {QStringLiteral("selectedAssetId"), catalog_model_->selectedAssetId()},
        {QStringLiteral("selectedAssetCount"), catalog_model_->selectedAssetCount()},
        {QStringLiteral("selectedAssetIds"), QJsonArray::fromStringList(catalog_model_->selectedAssetIds())},
        {QStringLiteral("agentStatus"), agent_edit_->status()},
        {QStringLiteral("agentRunning"), agent_edit_->running()},
        {QStringLiteral("draftActive"), agent_edit_->draftActive()},
        {QStringLiteral("mcpConnectedClientCount"), mcp_connected_client_count_},
        {QStringLiteral("mcpTotalClientCount"), mcp_total_client_count_},
        {QStringLiteral("mcpLastToolCallName"), mcp_last_tool_call_name_},
        {QStringLiteral("mcpLastToolCallStatus"), mcp_last_tool_call_status_},
        {QStringLiteral("mcpLastToolCallAt"), mcp_last_tool_call_at_},
        {QStringLiteral("errorMessage"), agent_edit_->errorMessage()},
    });
  }

  if (command == QStringLiteral("photo_list_models")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_list_models does not accept args."));
    }
    return ok_response(QJsonObject{
        {QStringLiteral("selectedModelId"), agent_edit_->selectedModelId()},
        {QStringLiteral("models"), variant_list_to_json_array(agent_edit_->availableModels())},
    });
  }

  if (command == QStringLiteral("photo_selected_assets")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_selected_assets does not accept args."));
    }
    return ok_response(QJsonObject{
        {QStringLiteral("selectedAssetId"), catalog_model_->selectedAssetId()},
        {QStringLiteral("selectedAssetIds"), QJsonArray::fromStringList(catalog_model_->selectedAssetIds())},
        {QStringLiteral("selectedAssetCount"), catalog_model_->selectedAssetCount()},
    });
  }

  if (command == QStringLiteral("photo_read_asset")) {
    if (!has_only_keys(args, {QStringLiteral("assetId")}) ||
        (args.contains(QStringLiteral("assetId")) && !args.value(QStringLiteral("assetId")).isString())) {
      return error_response(QStringLiteral("photo_read_asset accepts only an optional assetId string."));
    }
    const auto asset = asset_for_bridge_request(catalog_model_, args);
    if (asset.isEmpty()) {
      return error_response(QStringLiteral("No matching asset is available to read."));
    }
    return ok_response(QJsonObject{
        {QStringLiteral("assetId"), asset.value(QStringLiteral("assetId")).toString()},
        {QStringLiteral("asset"), variant_map_to_json_object(asset)},
    });
  }

  if (command == QStringLiteral("photo_read_recipe")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_read_recipe does not accept args."));
    }
    return ok_response(QJsonObject{
        {QStringLiteral("assetId"), catalog_model_->selectedAssetId()},
        {QStringLiteral("recipeJson"), catalog_model_->selectedRecipeJson()},
    });
  }

  if (command == QStringLiteral("photo_get_recipe_patch_schema")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_get_recipe_patch_schema does not accept args."));
    }
    return ok_response(QJsonObject::fromVariantMap(agent_recipe_patch_tool_schema()));
  }

  if (command == QStringLiteral("photo_get_style_rulebook")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_get_style_rulebook does not accept args."));
    }
    const auto selected_style_id = agent_edit_->selectedStyleId();
    for (const auto& style_value : agent_edit_->availableStyles()) {
      const auto style = style_value.toMap();
      if (style.value(QStringLiteral("id")).toString() == selected_style_id) {
        return ok_response(QJsonObject{
            {QStringLiteral("styleId"), selected_style_id},
            {QStringLiteral("style"), QJsonObject::fromVariantMap(style)},
        });
      }
    }
    return error_response(QStringLiteral("No selected style rulebook is available."));
  }

  if (command == QStringLiteral("photo_validate_recipe_patch")) {
    if (!has_only_keys(args, {QStringLiteral("patch")}) || !args.value(QStringLiteral("patch")).isObject()) {
      return error_response(QStringLiteral("photo_validate_recipe_patch requires only a patch object."));
    }
    return ok_response(variant_map_to_json_object(
        agent_edit_->validateRecipePatch(args.value(QStringLiteral("patch")).toObject().toVariantMap())));
  }

  if (command == QStringLiteral("photo_render_proof")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_render_proof does not accept args."));
    }
    return ok_response(variant_map_to_json_object(agent_edit_->renderProofForSelected()));
  }

  if (command == QStringLiteral("photo_validate_edit")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_validate_edit does not accept args."));
    }
    auto report = agent_edit_->renderProofForSelected();
    report.insert(QStringLiteral("mode"), QStringLiteral("validate-edit"));
    return ok_response(variant_map_to_json_object(report));
  }

  if (command == QStringLiteral("photo_validate_batch")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_validate_batch does not accept args."));
    }
    return ok_response(variant_map_to_json_object(agent_edit_->validateBatchForSelected()));
  }

  if (command == QStringLiteral("photo_export_preflight")) {
    const QStringList allowed_keys{
        QStringLiteral("preset"),
        QStringLiteral("destinationDirectory"),
        QStringLiteral("histogramSampleCount"),
        QStringLiteral("shadowClipCount"),
        QStringLiteral("highlightClipCount"),
        QStringLiteral("rawClipSampleCount"),
        QStringLiteral("rawShadowClipCount"),
        QStringLiteral("rawHighlightClipCount"),
    };
    const QStringList metric_keys{
        QStringLiteral("histogramSampleCount"),
        QStringLiteral("shadowClipCount"),
        QStringLiteral("highlightClipCount"),
        QStringLiteral("rawClipSampleCount"),
        QStringLiteral("rawShadowClipCount"),
        QStringLiteral("rawHighlightClipCount"),
    };
    QString invalid_metric_key;
    if (!has_only_keys(args, allowed_keys) ||
        (!args.value(QStringLiteral("preset")).isUndefined() &&
         !args.value(QStringLiteral("preset")).isString()) ||
        (!args.value(QStringLiteral("destinationDirectory")).isUndefined() &&
         !args.value(QStringLiteral("destinationDirectory")).isString()) ||
        !contains_only_optional_numbers(args, metric_keys, &invalid_metric_key)) {
      return error_response(QStringLiteral("photo_export_preflight accepts only preset, destinationDirectory, and numeric preflight counters."));
    }
    return ok_response(variant_map_to_json_object(agent_edit_->exportPreflightForSelected(
        args.value(QStringLiteral("preset")).toString(),
        args.value(QStringLiteral("destinationDirectory")).toString(),
        export_preflight_metrics_from_args(args))));
  }

  if (command == QStringLiteral("photo_export_batch")) {
    const QStringList allowed_keys{
        QStringLiteral("confirmed"),
        QStringLiteral("preset"),
        QStringLiteral("destinationDirectory"),
        QStringLiteral("histogramSampleCount"),
        QStringLiteral("shadowClipCount"),
        QStringLiteral("highlightClipCount"),
        QStringLiteral("rawClipSampleCount"),
        QStringLiteral("rawShadowClipCount"),
        QStringLiteral("rawHighlightClipCount"),
    };
    const QStringList metric_keys{
        QStringLiteral("histogramSampleCount"),
        QStringLiteral("shadowClipCount"),
        QStringLiteral("highlightClipCount"),
        QStringLiteral("rawClipSampleCount"),
        QStringLiteral("rawShadowClipCount"),
        QStringLiteral("rawHighlightClipCount"),
    };
    QString invalid_metric_key;
    if (!has_only_keys(args, allowed_keys) ||
        !args.value(QStringLiteral("confirmed")).isBool() ||
        (!args.value(QStringLiteral("preset")).isUndefined() &&
         !args.value(QStringLiteral("preset")).isString()) ||
        (!args.value(QStringLiteral("destinationDirectory")).isUndefined() &&
         !args.value(QStringLiteral("destinationDirectory")).isString()) ||
        !contains_only_optional_numbers(args, metric_keys, &invalid_metric_key)) {
      return error_response(QStringLiteral("photo_export_batch requires confirmed plus optional preset, destinationDirectory, and numeric preflight counters."));
    }
    return ok_response(variant_map_to_json_object(agent_edit_->queueExportForSelected(
        args.value(QStringLiteral("preset")).toString(),
        args.value(QStringLiteral("destinationDirectory")).toString(),
        args.value(QStringLiteral("confirmed")).toBool(),
        export_preflight_metrics_from_args(args))));
  }

  if (command == QStringLiteral("photo_apply_recipe_patch_draft")) {
    if (!has_only_keys(args, {QStringLiteral("patch"), QStringLiteral("confirmed")}) ||
        !args.value(QStringLiteral("patch")).isObject() ||
        (!args.value(QStringLiteral("confirmed")).isUndefined() &&
         !args.value(QStringLiteral("confirmed")).isBool())) {
      return error_response(QStringLiteral("photo_apply_recipe_patch_draft requires a patch object and optional confirmed boolean."));
    }
    if (value_contains_retouch_patch(args.value(QStringLiteral("patch"))) &&
        !args.value(QStringLiteral("confirmed")).toBool(false)) {
      return error_response(QStringLiteral("photo_apply_recipe_patch_draft requires confirmed: true for retouch patches."));
    }
    return ok_response(variant_map_to_json_object(
        agent_edit_->applyDraftRecipePatchToSelected(args.value(QStringLiteral("patch")).toObject().toVariantMap())));
  }

  if (command == QStringLiteral("photo_plan_bulk_edit") ||
      command == QStringLiteral("photo_apply_bulk_draft")) {
    if (!has_only_keys(args, {QStringLiteral("instructions")}) ||
        (!args.value(QStringLiteral("instructions")).isUndefined() &&
         !args.value(QStringLiteral("instructions")).isString())) {
      return error_response(QStringLiteral("Bulk edit commands accept only optional instructions."));
    }
    if (catalog_model_->selectedAssetCount() < 2) {
      return error_response(QStringLiteral("Bulk edit commands require at least two selected assets."));
    }
    const auto instructions = args.value(QStringLiteral("instructions")).toString();
    auto report = command == QStringLiteral("photo_plan_bulk_edit")
                      ? agent_edit_->planDraftForSelected(instructions)
                      : agent_edit_->runDraftOnSelected(instructions);
    report.insert(QStringLiteral("automationCommand"), command);
    return ok_response(variant_map_to_json_object(report));
  }

  if (command == QStringLiteral("photo_create_mask") ||
      command == QStringLiteral("photo_apply_mask_adjustment")) {
    QString error_message;
    const auto create_mask = command == QStringLiteral("photo_create_mask");
    if (!valid_mask_args(args, create_mask, &error_message)) {
      return error_response(error_message);
    }
    const auto patch = mask_patch_from_args(args, create_mask);
    auto report = agent_edit_->applyDraftRecipePatchToSelected(patch);
    report.insert(QStringLiteral("mode"), command);
    report.insert(QStringLiteral("maskType"), args.value(QStringLiteral("type")).toString());
    report.insert(QStringLiteral("maskPatch"), patch);
    return ok_response(variant_map_to_json_object(report));
  }

  if (command == QStringLiteral("photo_run_draft_on_selected")) {
    if (!has_only_keys(args, {QStringLiteral("instructions")})) {
      return error_response(QStringLiteral("photo_run_draft_on_selected accepts only instructions."));
    }
    return ok_response(variant_map_to_json_object(
        agent_edit_->runDraftOnSelected(args.value(QStringLiteral("instructions")).toString())));
  }

  if (command == QStringLiteral("photo_commit_agent_draft")) {
    if (!has_only_keys(args, {QStringLiteral("confirmed")}) ||
        !args.value(QStringLiteral("confirmed")).isBool()) {
      return error_response(QStringLiteral("photo_commit_agent_draft requires confirmed."));
    }
    if (!args.value(QStringLiteral("confirmed")).toBool()) {
      return error_response(QStringLiteral("photo_commit_agent_draft requires confirmed: true."));
    }
    return ok_response(variant_map_to_json_object(agent_edit_->commitDraft(true)));
  }

  if (command == QStringLiteral("photo_cancel_agent_run")) {
    if (!has_only_keys(args, {QStringLiteral("reason")}) ||
        (!args.value(QStringLiteral("reason")).isUndefined() &&
         !args.value(QStringLiteral("reason")).isString())) {
      return error_response(QStringLiteral("photo_cancel_agent_run accepts only an optional reason string."));
    }
    return ok_response(variant_map_to_json_object(
        agent_edit_->cancelRun(args.value(QStringLiteral("reason")).toString())));
  }

  if (command == QStringLiteral("photo_rollback_agent_draft")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_rollback_agent_draft does not accept args."));
    }
    return ok_response(variant_map_to_json_object(agent_edit_->rollbackDraft()));
  }

  return error_response(QStringLiteral("Unknown automation command: %1").arg(command));
}

bool PhotoAutomationBridge::writeSessionFile() {
  QSaveFile file(session_file_path_);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    error_message_ = QStringLiteral("Unable to write automation session file.");
    return false;
  }
  const auto payload = QJsonObject{
      {QStringLiteral("schemaVersion"), 1},
      {QStringLiteral("protocol"), QString::fromLatin1(kProtocol)},
      {QStringLiteral("endpointKind"), endpoint_kind_},
      {QStringLiteral("endpoint"), endpoint_name_},
      {QStringLiteral("mcpProtocol"), QStringLiteral("model-context-protocol-jsonrpc-2025-06-18")},
      {QStringLiteral("mcpEndpointKind"), mcp_endpoint_kind_},
      {QStringLiteral("mcpEndpoint"), mcp_endpoint_},
      {QStringLiteral("mcpTransport"), QStringLiteral("streamable-http")},
      {QStringLiteral("mcpConnectedClientCount"), mcp_connected_client_count_},
      {QStringLiteral("mcpTotalClientCount"), mcp_total_client_count_},
      {QStringLiteral("mcpLastToolCallName"), mcp_last_tool_call_name_},
      {QStringLiteral("mcpLastToolCallStatus"), mcp_last_tool_call_status_},
      {QStringLiteral("mcpLastToolCallAt"), mcp_last_tool_call_at_},
      {QStringLiteral("fullServerName"), server_ != nullptr ? server_->fullServerName() : QString{}},
      {QStringLiteral("token"), token_},
      {QStringLiteral("pid"), QCoreApplication::applicationPid()},
      {QStringLiteral("startedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
  };
  file.write(QJsonDocument(payload).toJson(QJsonDocument::Indented));
  if (!file.commit()) {
    error_message_ = QStringLiteral("Unable to commit automation session file.");
    return false;
  }
  return true;
}

void PhotoAutomationBridge::removeSessionFile() const {
  if (!session_file_path_.isEmpty()) {
    QFile::remove(session_file_path_);
  }
}

void PhotoAutomationBridge::recordMcpToolCall(const QString& tool_name, const QString& status) {
  mcp_last_tool_call_name_ = tool_name.trimmed();
  mcp_last_tool_call_status_ = status.trimmed().isEmpty() ? QStringLiteral("unknown") : status.trimmed();
  mcp_last_tool_call_at_ = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

}  // namespace photo_editor::desktop

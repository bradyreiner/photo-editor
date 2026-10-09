#include "photo_automation_bridge.h"
#include "agent_app_command_registry.h"
#include "agent_mcp_client.h"
#include "search_edit_workflow.h"

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
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QVariantMap>
#include <QVector>

#include <tuple>
#include <cmath>

#include "agent_edit_controller.h"
#include "agent_edit_recipe_patch.h"
#include "app_bootstrap.h"
#include "automation_capability_policy.h"
#include "catalog_asset_model.h"
#include "edit_adjustment_patch.h"
#include "photo_mcp_server.h"
#include "photo_composition_tool_contract.h"
#include "agent_design_schemas.h"
#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"

namespace photo_editor::desktop {
namespace {

constexpr auto kProtocol = "photo-editor-automation-jsonl-v1";
constexpr auto kMaximumAutomationBytes = 1024 * 1024;
constexpr auto kMaximumMcpHeaderBytes = 32 * 1024;
constexpr auto kMaximumMcpConnections = 16;
constexpr auto kMaximumMcpRequestsPerMinute = 120;
constexpr auto kMaximumJsonDepth = 32;

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

QJsonObject capability_error_response(const QString& required_capability) {
  auto response = error_response(
      QStringLiteral("Automation capability '%1' is required.").arg(required_capability));
  response.insert(QStringLiteral("errorCode"), QStringLiteral("insufficient_capability"));
  response.insert(QStringLiteral("requiredCapability"), required_capability);
  return response;
}

QJsonObject argument_error_response(const QString& message) {
  auto response = error_response(message);
  response.insert(QStringLiteral("data"), QJsonObject{{QStringLiteral("ok"), false},
      {QStringLiteral("errorCode"), QStringLiteral("invalid_arguments")},
      {QStringLiteral("recoverableError"), true}, {QStringLiteral("mutationNotApplied"), true}});
  return response;
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
  const QUrl origin(value);
  return origin.isValid() &&
         origin.scheme() == QStringLiteral("http") &&
         (origin.host() == QStringLiteral("127.0.0.1") ||
          origin.host() == QStringLiteral("localhost")) &&
         origin.userInfo().isEmpty() && origin.path().isEmpty() &&
         origin.query().isEmpty() && origin.fragment().isEmpty();
}

bool is_local_http_host(const QString& value, quint16 server_port) {
  const QUrl host_url(QStringLiteral("http://") + value);
  return host_url.isValid() && host_url.userInfo().isEmpty() &&
         (host_url.host() == QStringLiteral("127.0.0.1") ||
          host_url.host() == QStringLiteral("localhost")) &&
         host_url.port() == static_cast<int>(server_port) &&
         (host_url.path().isEmpty() || host_url.path() == QStringLiteral("/"));
}

int json_depth(const QJsonValue& value, int current_depth = 1) {
  auto maximum_depth = current_depth;
  if (value.isObject()) {
    const auto object = value.toObject();
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
      maximum_depth = std::max(maximum_depth, json_depth(iterator.value(), current_depth + 1));
    }
  } else if (value.isArray()) {
    for (const auto& item : value.toArray()) {
      maximum_depth = std::max(maximum_depth, json_depth(item, current_depth + 1));
    }
  }
  return maximum_depth;
}

bool constant_time_equal(const QByteArray& supplied, const QByteArray& expected) {
  const auto maximum_size = std::max(supplied.size(), expected.size());
  auto difference = static_cast<unsigned int>(supplied.size() ^ expected.size());
  for (qsizetype index = 0; index < maximum_size; ++index) {
    const auto supplied_byte =
        index < supplied.size() ? static_cast<unsigned char>(supplied.at(index)) : 0U;
    const auto expected_byte =
        index < expected.size() ? static_cast<unsigned char>(expected.at(index)) : 0U;
    difference |= supplied_byte ^ expected_byte;
  }
  return difference == 0U;
}

QString high_entropy_token() {
  QString token;
  token.reserve(64);
  auto* random = QRandomGenerator::system();
  for (auto index = 0; index < 4; ++index) {
    token.append(QStringLiteral("%1").arg(random->generate64(), 16, 16, QLatin1Char('0')));
  }
  return token;
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
    : QObject(parent), agent_edit_(agent_edit), catalog_model_(catalog_model) {
  agent_mcp_endpoint_ = AgentMcpClient::registerInProcessEndpoint(this,
      [this](const QVariantMap& request) {
        PhotoMcpServer server(this, nullptr, true);
        return server.handleJsonRpc(QJsonObject::fromVariantMap(request),
            {QStringLiteral("read"), QStringLiteral("draft")}).toVariantMap();
      });
}

PhotoAutomationBridge::~PhotoAutomationBridge() {
  AgentMcpClient::unregisterInProcessEndpoint(agent_mcp_endpoint_);
  stop();
}

void PhotoAutomationBridge::setTimelineDocumentController(
    TimelineDocumentController* controller) {
  timeline_document_ = controller;
}

void PhotoAutomationBridge::setTimelinePlaybackController(
    TimelinePlaybackController* controller) {
  timeline_playback_ = controller;
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

QStringList PhotoAutomationBridge::mcpCapabilities() const {
  return mcp_capabilities_;
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

bool PhotoAutomationBridge::start(
    bool enable_mcp_http,
    const QStringList& mcp_capabilities) {
  stop();
  error_message_.clear();
  if (agent_edit_ == nullptr || catalog_model_ == nullptr) {
    error_message_ = QStringLiteral("Automation bridge requires agent and catalog controllers.");
    return false;
  }

  const auto allowed_capabilities = automation_capability_names();
  for (const auto& capability : mcp_capabilities) {
    const auto normalized = capability.trimmed().toLower();
    if (!allowed_capabilities.contains(normalized)) {
      error_message_ = QStringLiteral("Unknown MCP capability: %1").arg(capability);
      return false;
    }
    if (!mcp_capabilities_.contains(normalized)) {
      mcp_capabilities_.append(normalized);
    }
  }
  if (mcp_capabilities_.isEmpty()) {
    mcp_capabilities_.append(QStringLiteral("read"));
  }

  const auto session_dir = QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("automation"));
  if (!QDir().mkpath(session_dir)) {
    error_message_ = QStringLiteral("Unable to create automation runtime directory.");
    return false;
  }
  if (!QFile::setPermissions(
          session_dir,
          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
    error_message_ = QStringLiteral("Unable to restrict automation runtime directory permissions.");
    return false;
  }
  endpoint_name_ = QStringLiteral("photo-editor-agent-%1").arg(QCoreApplication::applicationPid());
  session_file_path_ = QDir(session_dir).filePath(QStringLiteral("session.json"));
  token_ = high_entropy_token();

  QLocalServer::removeServer(endpoint_name_);
  server_ = new QLocalServer(this);
  server_->setSocketOptions(QLocalServer::UserAccessOption);
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

  if (enable_mcp_http) {
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
    mcp_endpoint_kind_ = QStringLiteral("streamable-http-localhost-authenticated");
    mcp_endpoint_ = QStringLiteral("http://127.0.0.1:%1/mcp").arg(mcp_http_server_->serverPort());
  }

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
  mcp_capabilities_.clear();
  mcp_connected_client_count_ = 0;
  mcp_total_client_count_ = 0;
  mcp_last_tool_call_name_.clear();
  mcp_last_tool_call_status_.clear();
  mcp_last_tool_call_at_.clear();
  mcp_request_times_ms_.clear();
  mcp_active_tool_calls_ = 0;
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
    if (!socket->peerAddress().isLoopback() ||
        mcp_connected_client_count_ >= kMaximumMcpConnections) {
      connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
      replyMcpHttp(
          socket,
          503,
          QByteArrayLiteral("Service Unavailable"),
          QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP connection limit reached.")}});
      continue;
    }
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
  if (newline < 0 && buffer.size() < kMaximumAutomationBytes) {
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
  if (header_end < 0 && buffer.size() < kMaximumMcpHeaderBytes) {
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
  QByteArray authorization;
  QString host;
  for (int index = 1; index < header_lines.size(); ++index) {
    const auto line_bytes = header_lines.at(index).trimmed();
    const auto colon = line_bytes.indexOf(':');
    if (colon <= 0) {
      continue;
    }
    const auto key = QString::fromLatin1(line_bytes.left(colon)).trimmed().toLower();
    const auto value_bytes = line_bytes.mid(colon + 1).trimmed();
    const auto value = QString::fromLatin1(value_bytes);
    if (key == QStringLiteral("content-length")) {
      auto ok = false;
      content_length = value.toInt(&ok);
      if (!ok || content_length < 0 || content_length > kMaximumAutomationBytes) {
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
    } else if (key == QStringLiteral("authorization")) {
      authorization = value_bytes;
    } else if (key == QStringLiteral("host")) {
      host = value;
    }
  }

  if (mcp_http_server_ == nullptr ||
      !is_local_http_host(host, mcp_http_server_->serverPort())) {
    replyMcpHttp(
        socket,
        400,
        QByteArrayLiteral("Bad Request"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("Valid localhost Host header is required.")}});
    return;
  }

  if (!validMcpAuthorization(authorization)) {
    replyMcpHttp(
        socket,
        401,
        QByteArrayLiteral("Unauthorized"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("Valid MCP authorization is required.")}});
    return;
  }
  if (!admitMcpRequest()) {
    replyMcpHttp(
        socket,
        429,
        QByteArrayLiteral("Too Many Requests"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP request rate limit reached.")}});
    return;
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
  if (json_depth(document.object()) > kMaximumJsonDepth) {
    replyMcpHttp(
        socket,
        400,
        QByteArrayLiteral("Bad Request"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP request JSON nesting is too deep.")}});
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
  const auto is_tool_call =
      request_object.value(QStringLiteral("method")).toString() == QStringLiteral("tools/call");
  if (is_tool_call && mcp_active_tool_calls_ >= 1) {
    replyMcpHttp(
        socket,
        429,
        QByteArrayLiteral("Too Many Requests"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("Another MCP tool call is active.")}});
    return;
  }
  if (is_tool_call) {
    ++mcp_active_tool_calls_;
  }
  const auto response = mcp_server_->handleJsonRpc(request_object, mcp_capabilities_);
  if (is_tool_call) {
    --mcp_active_tool_calls_;
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
  const auto tools_result = mcp_server_ != nullptr
                                ? mcp_server_->toolsListResult(mcp_capabilities_)
                                : QJsonObject{};
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
  return handleJsonRequestWithCapabilities(request, mcp_capabilities_);
}

QJsonObject PhotoAutomationBridge::handleAgentJsonRequest(const QJsonObject& request) const {
  return handleJsonRequestWithCapabilities(request,
      {QStringLiteral("read"), QStringLiteral("draft")});
}

QJsonObject PhotoAutomationBridge::handleJsonRequestWithCapabilities(
    const QJsonObject& request, const QStringList& capabilities) const {
  if (!has_only_keys(
          request,
          {QStringLiteral("id"), QStringLiteral("token"), QStringLiteral("command"), QStringLiteral("args")})) {
    return error_response(QStringLiteral("Request contains unknown fields."));
  }
  if (!constant_time_equal(
          request.value(QStringLiteral("token")).toString().toUtf8(),
          token_.toUtf8())) {
    return error_response(QStringLiteral("Invalid automation token."));
  }
  const auto command = request.value(QStringLiteral("command")).toString().trimmed();
  if (command.isEmpty()) {
    return error_response(QStringLiteral("Command is required."));
  }
  const auto required_capability = automation_capability_for_command(command);
  if (required_capability != QStringLiteral("read") && required_capability != QStringLiteral("export") &&
      command != QStringLiteral("photo_commit_agent_draft") &&
      command != QStringLiteral("photo_cancel_agent_run") &&
      command != QStringLiteral("photo_rollback_agent_draft") &&
      command != QStringLiteral("photo_search_edit_cancel") &&
      edit_access_policy_ && !edit_access_policy_()) {
    return error_response(QStringLiteral("Editing requires an active trial or purchase."));
  }
  if (!capabilities.contains(required_capability)) {
    return capability_error_response(required_capability);
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

bool PhotoAutomationBridge::validMcpAuthorization(const QByteArray& authorization) const {
  if (token_.isEmpty()) {
    return false;
  }
  return constant_time_equal(
      authorization,
      QByteArrayLiteral("Bearer ") + token_.toUtf8());
}

bool PhotoAutomationBridge::admitMcpRequest() {
  const auto now_ms = QDateTime::currentMSecsSinceEpoch();
  constexpr auto window_ms = 60 * 1000;
  while (!mcp_request_times_ms_.isEmpty() &&
         now_ms - mcp_request_times_ms_.head() >= window_ms) {
    mcp_request_times_ms_.dequeue();
  }
  if (mcp_request_times_ms_.size() >= kMaximumMcpRequestsPerMinute) {
    return false;
  }
  mcp_request_times_ms_.enqueue(now_ms);
  return true;
}

QJsonObject PhotoAutomationBridge::handleCommand(const QString& command, const QJsonObject& args) const {
  if (command == QStringLiteral("app_list_commands") || command == QStringLiteral("app_command_schema") ||
      command == QStringLiteral("app_invoke_command") || command == QStringLiteral("app_inspect_command")) {
    if (!app_commands_) return error_response(QStringLiteral("Native app command discovery is unavailable."));
    QVariantMap result;
    if (command == QStringLiteral("app_list_commands")) {
      if (!has_only_keys(args, {QStringLiteral("domain"), QStringLiteral("query"), QStringLiteral("offset"), QStringLiteral("limit")}) ||
          (args.contains("domain") && !args.value("domain").isString()) ||
          (args.contains("query") && !args.value("query").isString()) ||
          (args.contains("offset") && (!args.value("offset").isDouble() || args.value("offset").toDouble() < 0 ||
              args.value("offset").toDouble() > 1000000 || std::floor(args.value("offset").toDouble()) != args.value("offset").toDouble())) ||
          (args.contains("limit") && (!args.value("limit").isDouble() || args.value("limit").toDouble() < 1 ||
              args.value("limit").toDouble() > 64 || std::floor(args.value("limit").toDouble()) != args.value("limit").toDouble())))
        return argument_error_response(QStringLiteral("Discovery accepts domain/query strings, nonnegative integer offset and limit 1–64."));
      result = app_commands_->commands(args.value("domain").toString(), args.value("query").toString(),
          args.value("offset").toInt(), args.value("limit").toInt(24));
    } else {
      if (!args.value("commandId").isString() || !has_only_keys(args,
          command == QStringLiteral("app_command_schema") ? QStringList{"commandId"} : QStringList{"commandId", "values"}) ||
          (command != QStringLiteral("app_command_schema") && !args.value("values").isObject()))
        return argument_error_response(QStringLiteral("Supply a commandId and an object of typed values for invocation."));
      if (command == QStringLiteral("app_inspect_command") &&
          !app_commands_->schema(args.value("commandId").toString()).value("readOnly").toBool())
        return error_response(QStringLiteral("Read capability cannot invoke a mutable native control."));
      result = command == QStringLiteral("app_command_schema") ? app_commands_->schema(args.value("commandId").toString())
          : app_commands_->invoke(args.value("commandId").toString(), args.value("values").toObject().toVariantMap());
    }
    // Keep failure receipts as structured evidence, including any applied state
    // that the native journal captured before rejecting a postcondition.
    if (!result.value("ok").toBool()) {
      auto error = error_response(result.value("error").toString());
      error.insert("data", QJsonObject::fromVariantMap(result));
      return error;
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("photo_inspect_composition") || command == QStringLiteral("photo_edit_composition")) {
    if (!composition_handler_) return error_response(QStringLiteral("Open the photo editor to edit a composition."));
    if (!has_only_keys(args, {QStringLiteral("assetId"), QStringLiteral("operation"), QStringLiteral("layerId"), QStringLiteral("values")}) ||
        !args.value("assetId").isString() ||
        (args.contains("layerId") && !args.value("layerId").isString()) ||
        (args.contains("values") && !args.value("values").isObject()))
      return argument_error_response(QStringLiteral("Composition requires assetId and typed operation values."));
    auto request = args.toVariantMap();
    if (command == QStringLiteral("photo_inspect_composition")) {
      if (args.contains("operation") || args.contains("layerId"))
        return argument_error_response(QStringLiteral("Inspect accepts assetId and optional font family/face values."));
      if (args.contains("values")) {
        const auto values = args.value("values").toObject();
        if (values.size() > 2 || !values.contains("family") ||
            !values.value("family").isString() || values.value("family").toString().trimmed().isEmpty() ||
            (values.contains("face") && !values.value("face").isString()) ||
            (values.contains("face") && values.value("face").toString().trimmed().isEmpty()))
          return argument_error_response(QStringLiteral("Font inspection values require a non-empty family and optional face string."));
      }
      request.insert("operation", "inspect");
    } else if (!args.value("operation").isString() || args.value("operation").toString() == "inspect") {
      return argument_error_response(QStringLiteral("Use inspect_composition to read or supply an edit operation."));
    }
    if (command == QStringLiteral("photo_edit_composition") &&
        composition_layer_target_operations().contains(args.value("operation").toString()) &&
        args.value("layerId").toString().trimmed().isEmpty())
      return argument_error_response(QStringLiteral(
          "This operation requires arguments.layerId identifying an existing layer. Use an inspected layer ID or the createdLayerId from a completed creation call; coordinates in values do not identify a layer."));
    if (command == QStringLiteral("photo_edit_composition") && args.value("operation") == "typography" &&
        !AgentAppCommandRegistry::valueMatchesSchema(request.value("values"), agent_typography_schema()))
      return argument_error_response(QStringLiteral(
          "Typography values must match its partial-update schema: font, size, weight, color, spacing, path or material controls. Do not pass x or y; position a layer with the transform operation. Use numeric weight and preserve the existing layerId."));
    const auto result = composition_handler_(request);
    if (!result.value("ok").toBool()) {
      auto error = error_response(result.value("error").toString());
      error.insert("data", QJsonObject::fromVariantMap(result));
      return error;
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_scope_capture_request")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral(
          "timeline_scope_capture_request does not accept arguments."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->requestProgramScopeCaptureForAutomation();
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    auto payload = result;
    payload.remove(QStringLiteral("ok"));
    return ok_response(QJsonObject::fromVariantMap(payload));
  }
  if (command == QStringLiteral("timeline_scope_summary_inspect")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral(
          "timeline_scope_summary_inspect does not accept arguments."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    auto result = timeline_document_->programColorScopeSummaryForAutomation();
    result.remove(QStringLiteral("ok"));
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_playback_performance_inspect")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral(
          "timeline_playback_performance_inspect does not accept arguments."));
    }
    if (timeline_playback_.isNull()) {
      return error_response(QStringLiteral("Timeline playback is unavailable."));
    }
    auto frame_metrics = timeline_playback_->frameProcessingMetrics();
    const auto sample_count = frame_metrics.value(QStringLiteral("sampleCount")).toLongLong();
    const auto measured_source_key =
        frame_metrics.take(QStringLiteral("sourceKey")).toString();
    const auto result = QJsonObject{
        {QStringLiteral("hasVideoSource"), timeline_playback_->hasVideoSource()},
        {QStringLiteral("mediaReady"), timeline_playback_->mediaReady()},
        {QStringLiteral("playbackHealth"), timeline_playback_->playbackHealth()},
        {QStringLiteral("previewQualityMode"), timeline_playback_->previewQualityMode()},
        {QStringLiteral("effectivePreviewQuality"),
         timeline_playback_->effectivePreviewQuality()},
        {QStringLiteral("effectivePreviewScale"),
         timeline_playback_->effectivePreviewScale()},
        {QStringLiteral("usingProxy"), timeline_playback_->usingProxy()},
        {QStringLiteral("usingRenderCache"), timeline_playback_->usingRenderCache()},
        {QStringLiteral("adaptiveQualityActive"),
         timeline_playback_->adaptiveQualityActive()},
        {QStringLiteral("adaptiveDetailLevel"),
         timeline_playback_->adaptiveDetailLevel()},
        {QStringLiteral("adaptiveQualityReason"),
         timeline_playback_->adaptiveQualityReason()},
        {QStringLiteral("droppedFrameCount"), timeline_playback_->droppedFrameCount()},
        {QStringLiteral("hasFrameTimingSample"), sample_count > 0},
        {QStringLiteral("frameTimingSampleCount"), static_cast<qint64>(sample_count)},
        {QStringLiteral("measuredSourceKey"), measured_source_key},
        {QStringLiteral("frameTimingSmoothing"),
         QStringLiteral("EWMA with alpha 0.15 over completed processed frames")},
        {QStringLiteral("frameProcessingMetrics"),
         QJsonObject::fromVariantMap(frame_metrics)},
        {QStringLiteral("colorCacheDiagnostics"),
         QJsonObject::fromVariantMap(
             timeline_playback_->colorCacheDiagnostics())},
        {QStringLiteral("cacheCounterScope"),
         QStringLiteral("Cumulative since process start; independent of playback diagnostic resets.")},
        {QStringLiteral("timingScope"),
         QStringLiteral("Worker-side video frame conversion and color/effect processing; excludes decoder wait and display presentation.")},
    };
    return ok_response(result);
  }
  if (command == QStringLiteral("timeline_color_inspect")) {
    if (!has_only_keys(args, {QStringLiteral("clipId")}) ||
        !args.value(QStringLiteral("clipId")).isString()) {
      return error_response(QStringLiteral("timeline_color_inspect requires only a clipId string."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->inspectClipColorForAutomation(
        args.value(QStringLiteral("clipId")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_compare_open")) {
    const auto has_sequence_seconds = args.contains(QStringLiteral("sequenceSeconds"));
    const auto has_mode = args.contains(QStringLiteral("mode"));
    if (!has_only_keys(
            args,
            {QStringLiteral("clipId"), QStringLiteral("sequenceSeconds"),
             QStringLiteral("mode")}) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        (has_sequence_seconds && !args.value(QStringLiteral("sequenceSeconds")).isDouble()) ||
        (has_mode && !args.value(QStringLiteral("mode")).isString())) {
      return error_response(QStringLiteral(
          "timeline_color_compare_open requires clipId and optional numeric sequenceSeconds and string mode."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->openProgramComparisonForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        has_sequence_seconds ? args.value(QStringLiteral("sequenceSeconds")).toDouble() : -1.0,
        has_mode ? args.value(QStringLiteral("mode")).toString() : QStringLiteral("wipe"));
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    auto payload = result;
    payload.remove(QStringLiteral("ok"));
    return ok_response(QJsonObject::fromVariantMap(payload));
  }
  if (command == QStringLiteral("timeline_source_color_metadata_inspect")) {
    if (!has_only_keys(args, {QStringLiteral("clipId")}) ||
        !args.value(QStringLiteral("clipId")).isString()) {
      return error_response(QStringLiteral(
          "timeline_source_color_metadata_inspect requires only a clipId string."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->inspectClipSourceColorMetadataForAutomation(
        args.value(QStringLiteral("clipId")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_effects_inspect")) {
    if (!has_only_keys(args, {QStringLiteral("clipId")}) ||
        !args.value(QStringLiteral("clipId")).isString()) {
      return error_response(QStringLiteral("timeline_effects_inspect requires only a clipId string."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->inspectClipEffectsForAutomation(
        args.value(QStringLiteral("clipId")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_grade_inspect")) {
    if (!has_only_keys(args, {QStringLiteral("clipId")}) ||
        !args.value(QStringLiteral("clipId")).isString()) {
      return error_response(QStringLiteral(
          "timeline_color_grade_inspect requires only a clipId string."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->inspectClipColorGradeForAutomation(
        args.value(QStringLiteral("clipId")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_grade_set")) {
    if (!has_only_keys(args, {QStringLiteral("clipId"), QStringLiteral("recipe")}) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("recipe")).isObject()) {
      return error_response(QStringLiteral(
          "timeline_color_grade_set requires a clipId string and recipe object."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->setClipColorGradeRecipeForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        args.value(QStringLiteral("recipe")).toObject());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_grade_layer_edit")) {
    const auto allowed_keys = QStringList{
        QStringLiteral("clipId"), QStringLiteral("operation"),
        QStringLiteral("layerId"), QStringLiteral("layer"),
        QStringLiteral("position"), QStringLiteral("enabled"),
        QStringLiteral("opacity"), QStringLiteral("parameter"),
        QStringLiteral("value"), QStringLiteral("blendMode")};
    if (!has_only_keys(args, allowed_keys) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("operation")).isString()) {
      return error_response(QStringLiteral(
          "timeline_color_grade_layer_edit requires clipId and operation strings."));
    }
    const auto operation =
        args.value(QStringLiteral("operation")).toString().trimmed().toLower();
    const auto supported_operation =
        operation == QStringLiteral("add") ||
        operation == QStringLiteral("remove") ||
        operation == QStringLiteral("move") ||
        operation == QStringLiteral("replace") ||
        operation == QStringLiteral("set_enabled") ||
        operation == QStringLiteral("set_opacity") ||
        operation == QStringLiteral("set_blend_mode") ||
        operation == QStringLiteral("set_parameter");
    const auto needs_layer_id = operation != QStringLiteral("add");
    const auto needs_layer = operation == QStringLiteral("add") ||
                             operation == QStringLiteral("replace");
    const auto needs_position = operation == QStringLiteral("move");
    const auto needs_enabled = operation == QStringLiteral("set_enabled");
    const auto needs_opacity = operation == QStringLiteral("set_opacity");
    const auto needs_blend_mode = operation == QStringLiteral("set_blend_mode");
    const auto needs_parameter = operation == QStringLiteral("set_parameter");
    if (!supported_operation ||
        args.contains(QStringLiteral("layerId")) != needs_layer_id ||
        args.contains(QStringLiteral("layer")) != needs_layer ||
        args.contains(QStringLiteral("enabled")) != needs_enabled ||
        args.contains(QStringLiteral("opacity")) != needs_opacity ||
        args.contains(QStringLiteral("blendMode")) != needs_blend_mode ||
        args.contains(QStringLiteral("parameter")) != needs_parameter ||
        args.contains(QStringLiteral("value")) != needs_parameter ||
        (needs_position && !args.contains(QStringLiteral("position"))) ||
        (!needs_position && operation != QStringLiteral("add") &&
         args.contains(QStringLiteral("position")))) {
      return error_response(QStringLiteral(
          "Provide layerId for remove, move, replace, and per-layer edits; layer for add or replace; position for move and optionally add; enabled only for set_enabled; opacity only for set_opacity; blendMode only for set_blend_mode; parameter and value only for set_parameter."));
    }
    if ((needs_layer_id && !args.value(QStringLiteral("layerId")).isString()) ||
        (needs_layer && !args.value(QStringLiteral("layer")).isObject()) ||
        (needs_enabled && !args.value(QStringLiteral("enabled")).isBool()) ||
        (needs_opacity && !args.value(QStringLiteral("opacity")).isDouble()) ||
        (needs_blend_mode && !args.value(QStringLiteral("blendMode")).isString()) ||
        (needs_parameter &&
         (!args.value(QStringLiteral("parameter")).isString() ||
          !args.value(QStringLiteral("value")).isDouble()))) {
      return error_response(QStringLiteral(
          "layerId, layer, enabled, opacity, blendMode, or parameter has the wrong JSON type."));
    }
    auto position = -1;
    if (args.contains(QStringLiteral("position"))) {
      const auto position_value = args.value(QStringLiteral("position"));
      if (!position_value.isDouble() ||
          position_value.toDouble() != static_cast<double>(position_value.toInt())) {
        return error_response(QStringLiteral("position must be an integer."));
      }
      position = position_value.toInt();
      if (position < 0) {
        return error_response(QStringLiteral("position cannot be negative."));
      }
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->editClipColorGradeLayerForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        operation,
        args.value(QStringLiteral("layerId")).toString(),
        args.value(QStringLiteral("layer")).toObject().toVariantMap(),
        position,
        args.value(QStringLiteral("enabled")).toBool(),
        args.value(QStringLiteral("opacity")).toDouble(),
        args.value(QStringLiteral("parameter")).toString(),
        args.value(QStringLiteral("value")).toDouble(),
        args.value(QStringLiteral("blendMode")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_grade_parameter_set")) {
    if (!has_only_keys(args, {QStringLiteral("clipId"), QStringLiteral("parameter"),
                              QStringLiteral("value")}) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("parameter")).isString() ||
        !args.value(QStringLiteral("value")).isDouble()) {
      return error_response(QStringLiteral(
          "timeline_color_grade_parameter_set requires clipId, parameter, and numeric value."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->setClipColorGradeParameterForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        args.value(QStringLiteral("parameter")).toString(),
        args.value(QStringLiteral("value")).toDouble());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_grade_curve_set")) {
    if (!has_only_keys(args, {QStringLiteral("clipId"), QStringLiteral("layerId"),
                              QStringLiteral("channel"), QStringLiteral("points")}) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("channel")).isString() ||
        !args.value(QStringLiteral("points")).isArray() ||
        (args.contains(QStringLiteral("layerId")) &&
         !args.value(QStringLiteral("layerId")).isString())) {
      return error_response(QStringLiteral(
          "timeline_color_grade_curve_set requires clipId, channel, and points, with an optional layerId."));
    }
    const auto points = args.value(QStringLiteral("points")).toArray();
    if (points.size() > 64) {
      return error_response(QStringLiteral("points cannot contain more than 64 entries."));
    }
    auto curve_points = QVariantList{};
    curve_points.reserve(points.size());
    for (const auto& point_value : points) {
      if (!point_value.isObject()) {
        return error_response(QStringLiteral("Every curve point must be an object."));
      }
      const auto point = point_value.toObject();
      if (!has_only_keys(point, {QStringLiteral("x"), QStringLiteral("y")}) ||
          !point.value(QStringLiteral("x")).isDouble() ||
          !point.value(QStringLiteral("y")).isDouble()) {
        return error_response(QStringLiteral(
            "Every curve point must contain only numeric x and y values."));
      }
      curve_points.push_back(point.toVariantMap());
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->setClipColorGradeCurveForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        args.value(QStringLiteral("layerId")).toString(),
        args.value(QStringLiteral("channel")).toString(), curve_points);
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_grade_clear")) {
    if (!has_only_keys(args, {QStringLiteral("clipId")}) ||
        !args.value(QStringLiteral("clipId")).isString()) {
      return error_response(QStringLiteral(
          "timeline_color_grade_clear requires only a clipId string."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->clearClipColorGradeRecipeForAutomation(
        args.value(QStringLiteral("clipId")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_adjustment_clip_add")) {
    if (!has_only_keys(args, {QStringLiteral("startSeconds"),
                              QStringLiteral("durationSeconds")}) ||
        !args.value(QStringLiteral("startSeconds")).isDouble() ||
        !args.value(QStringLiteral("durationSeconds")).isDouble() ||
        args.value(QStringLiteral("startSeconds")).toDouble() < 0.0 ||
        args.value(QStringLiteral("durationSeconds")).toDouble() <= 0.0) {
      return error_response(QStringLiteral(
          "timeline_adjustment_clip_add requires a nonnegative startSeconds and positive durationSeconds."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->addAdjustmentClipForAutomation(
        args.value(QStringLiteral("startSeconds")).toDouble(),
        args.value(QStringLiteral("durationSeconds")).toDouble());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_clip_opacity_set")) {
    if (!has_only_keys(args, {QStringLiteral("clipId"), QStringLiteral("opacity")}) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("opacity")).isDouble()) {
      return error_response(QStringLiteral(
          "timeline_clip_opacity_set requires a clipId string and numeric opacity."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->setClipOpacityForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        args.value(QStringLiteral("opacity")).toDouble());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_clip_keyframe_edit")) {
    const auto allowed_keys = QStringList{
        QStringLiteral("clipId"), QStringLiteral("operation"),
        QStringLiteral("parameter"), QStringLiteral("localSeconds"),
        QStringLiteral("value"), QStringLiteral("interpolation")};
    if (!has_only_keys(args, allowed_keys) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("operation")).isString() ||
        !args.value(QStringLiteral("parameter")).isString() ||
        !args.value(QStringLiteral("localSeconds")).isDouble()) {
      return error_response(QStringLiteral(
          "timeline_clip_keyframe_edit requires clipId, operation, parameter, and numeric localSeconds."));
    }
    const auto operation =
        args.value(QStringLiteral("operation")).toString().trimmed().toLower();
    const auto is_set = operation == QStringLiteral("set");
    const auto is_remove = operation == QStringLiteral("remove");
    if ((!is_set && !is_remove) ||
        args.contains(QStringLiteral("value")) != is_set ||
        (args.contains(QStringLiteral("interpolation")) &&
         (!is_set || !args.value(QStringLiteral("interpolation")).isString())) ||
        (is_set && !args.value(QStringLiteral("value")).isDouble()) ||
        args.value(QStringLiteral("localSeconds")).toDouble() < 0.0) {
      return error_response(QStringLiteral(
          "Use set with a numeric value and optional interpolation, or remove without either field; localSeconds must be nonnegative."));
    }
    if (args.contains(QStringLiteral("interpolation"))) {
      const auto interpolation = args.value(QStringLiteral("interpolation"))
                                     .toString()
                                     .trimmed()
                                     .toLower();
      if (!QStringList{QStringLiteral("hold"), QStringLiteral("linear"),
                       QStringLiteral("ease-in"), QStringLiteral("ease-out"),
                       QStringLiteral("ease-in-out"), QStringLiteral("bezier")}
               .contains(interpolation)) {
        return error_response(QStringLiteral("Unsupported keyframe interpolation."));
      }
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->editClipKeyframeForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        operation,
        args.value(QStringLiteral("parameter")).toString(),
        args.value(QStringLiteral("localSeconds")).toDouble(),
        args.value(QStringLiteral("value")).toDouble(),
        args.value(QStringLiteral("interpolation")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_effects_edit")) {
    const auto allowed_keys = QStringList{
        QStringLiteral("clipId"), QStringLiteral("operation"),
        QStringLiteral("effectName"), QStringLiteral("amount"),
        QStringLiteral("enabled")};
    if (!has_only_keys(args, allowed_keys) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("operation")).isString()) {
      return error_response(QStringLiteral(
          "timeline_effects_edit requires clipId and operation strings and accepts only effectName, amount, and enabled as optional fields."));
    }
    const auto operation = args.value(QStringLiteral("operation")).toString().trimmed().toLower();
    const auto effect_operation =
        operation == QStringLiteral("add") || operation == QStringLiteral("remove") ||
        operation == QStringLiteral("move_up") || operation == QStringLiteral("move_down") ||
        operation == QStringLiteral("set_amount") || operation == QStringLiteral("set_enabled");
    if (args.contains(QStringLiteral("effectName")) &&
        !args.value(QStringLiteral("effectName")).isString()) {
      return error_response(QStringLiteral("effectName must be a string."));
    }
    if (args.contains(QStringLiteral("amount")) &&
        !args.value(QStringLiteral("amount")).isDouble()) {
      return error_response(QStringLiteral("amount must be a number."));
    }
    if (args.contains(QStringLiteral("enabled")) &&
        !args.value(QStringLiteral("enabled")).isBool()) {
      return error_response(QStringLiteral("enabled must be a boolean."));
    }
    const auto has_effect_name = args.contains(QStringLiteral("effectName"));
    const auto has_amount = args.contains(QStringLiteral("amount"));
    const auto has_enabled = args.contains(QStringLiteral("enabled"));
    const auto amount_expected = operation == QStringLiteral("set_amount");
    const auto enabled_expected = operation == QStringLiteral("set_enabled") ||
                                  operation == QStringLiteral("set_stack_enabled");
    if (effect_operation != has_effect_name || amount_expected != has_amount ||
        enabled_expected != has_enabled) {
      return error_response(QStringLiteral(
          "Provide effectName for add, remove, move, and per-effect operations; provide amount only for set_amount and enabled only for set_enabled or set_stack_enabled."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->editClipEffectStackForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        operation,
        args.value(QStringLiteral("effectName")).toString(),
        args.value(QStringLiteral("amount")).toDouble(),
        args.value(QStringLiteral("enabled")).toBool());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_set_video_input_transform")) {
    if (!has_only_keys(args, {QStringLiteral("clipId"), QStringLiteral("profile")}) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("profile")).isString()) {
      return error_response(QStringLiteral(
          "timeline_color_set_video_input_transform requires clipId and profile strings."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->setClipVideoInputTransformForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        args.value(QStringLiteral("profile")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_set_lut")) {
    if (!has_only_keys(args, {QStringLiteral("clipId"), QStringLiteral("filePath")}) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("filePath")).isString()) {
      return error_response(QStringLiteral(
          "timeline_color_set_lut requires clipId and filePath strings."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->setClipColorLutForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        args.value(QStringLiteral("filePath")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_clear_lut")) {
    if (!has_only_keys(args, {QStringLiteral("clipId")}) ||
        !args.value(QStringLiteral("clipId")).isString()) {
      return error_response(QStringLiteral(
          "timeline_color_clear_lut requires only a clipId string."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->clearClipColorLutForAutomation(
        args.value(QStringLiteral("clipId")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_set_shot_match_gains")) {
    if (!has_only_keys(
            args,
            {QStringLiteral("clipId"), QStringLiteral("redGain"),
             QStringLiteral("greenGain"), QStringLiteral("blueGain")}) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("redGain")).isDouble() ||
        !args.value(QStringLiteral("greenGain")).isDouble() ||
        !args.value(QStringLiteral("blueGain")).isDouble()) {
      return error_response(QStringLiteral(
          "timeline_color_set_shot_match_gains requires clipId and three numeric gains."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->setClipShotMatchGainsForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        args.value(QStringLiteral("redGain")).toDouble(),
        args.value(QStringLiteral("greenGain")).toDouble(),
        args.value(QStringLiteral("blueGain")).toDouble());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("photo_search_edit_start")) {
    if (!has_only_keys(args, {QStringLiteral("instruction")}) || !args.value("instruction").isString())
      return error_response(QStringLiteral("Search requires only an instruction string."));
    if (search_edit_ == nullptr) return error_response(QStringLiteral("Search-and-edit is unavailable."));
    QVariantMap model;
    for (const auto& value : agent_edit_->availableModels()) {
      const auto candidate = value.toMap();
      if (candidate.value("id").toString() == agent_edit_->selectedModelId()) model = candidate;
    }
    return ok_response(QJsonObject::fromVariantMap(search_edit_->start(args.value("instruction").toString(), model)));
  }
  if (command == QStringLiteral("photo_search_edit_status") || command == QStringLiteral("photo_search_edit_cancel")) {
    if (!args.isEmpty()) return error_response(QStringLiteral("This search command accepts no arguments."));
    if (search_edit_ == nullptr) return error_response(QStringLiteral("Search-and-edit is unavailable."));
    if (command == QStringLiteral("photo_search_edit_cancel")) search_edit_->cancel();
    return ok_response(QJsonObject::fromVariantMap(search_edit_->snapshot()));
  }
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

  if (command == QStringLiteral("photo_read_adjustments")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_read_adjustments does not accept args."));
    }
    return ok_response(QJsonObject{
        {QStringLiteral("assetId"), catalog_model_->selectedAssetId()},
        {QStringLiteral("adjustmentPatchJson"),
         catalog_model_->selectedAdjustmentPatchJson()},
    });
  }

  if (command == QStringLiteral("photo_read_recipe")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_read_recipe does not accept args."));
    }
    const auto adjustments =
        parse_adjustment_patch_json(
            catalog_model_
                ->selectedAdjustmentPatchJson());
    return ok_response(QJsonObject{
        {QStringLiteral("assetId"), catalog_model_->selectedAssetId()},
        {QStringLiteral("recipeJson"),
         adjustments.ok()
             ? serialize_edit_recipe_json(
                   *adjustments.state)
             : QString{}},
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

  if (command == QStringLiteral("photo_resolve_agent_review_issue")) {
    if (!has_only_keys(
            args,
            {QStringLiteral("assetId"), QStringLiteral("action"),
             QStringLiteral("confirmed")}) ||
        !args.value(QStringLiteral("assetId")).isString() ||
        !args.value(QStringLiteral("action")).isString() ||
        !args.value(QStringLiteral("confirmed")).isBool()) {
      return error_response(QStringLiteral(
          "photo_resolve_agent_review_issue requires assetId, action, and confirmed."));
    }
    if (!args.value(QStringLiteral("confirmed")).toBool()) {
      return error_response(QStringLiteral(
          "photo_resolve_agent_review_issue requires confirmed: true."));
    }
    const auto asset_id =
        args.value(QStringLiteral("assetId")).toString().trimmed();
    const auto action =
        args.value(QStringLiteral("action")).toString().trimmed().toLower();
    if (asset_id.isEmpty()) {
      return error_response(QStringLiteral(
          "photo_resolve_agent_review_issue requires a non-empty assetId."));
    }
    if (action == QStringLiteral("fix")) {
      return ok_response(
          variant_map_to_json_object(agent_edit_->fixReviewIssue(asset_id)));
    }
    if (action == QStringLiteral("accept_anyway")) {
      return ok_response(
          variant_map_to_json_object(agent_edit_->acceptReviewIssue(asset_id)));
    }
    if (action == QStringLiteral("rollback_photo")) {
      return ok_response(
          variant_map_to_json_object(agent_edit_->rollbackReviewIssue(asset_id)));
    }
    return error_response(QStringLiteral(
        "photo_resolve_agent_review_issue action must be fix, accept_anyway, or rollback_photo."));
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
      {QStringLiteral("mcpTransport"),
       mcp_http_server_ != nullptr ? QStringLiteral("streamable-http") : QStringLiteral("disabled")},
      {QStringLiteral("mcpCapabilities"), QJsonArray::fromStringList(mcp_capabilities_)},
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
  if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
    error_message_ = QStringLiteral("Unable to restrict automation session file permissions.");
    return false;
  }
  file.write(QJsonDocument(payload).toJson(QJsonDocument::Indented));
  if (!file.commit()) {
    error_message_ = QStringLiteral("Unable to commit automation session file.");
    return false;
  }
  if (!QFile::setPermissions(
          session_file_path_, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
    error_message_ = QStringLiteral("Unable to restrict committed automation session file permissions.");
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

#include "photo_automation_bridge.h"

#include "agent_edit_controller.h"
#include "agent_mcp_client.h"
#include "app_bootstrap.h"
#include "catalog_asset_model.h"
#include "edit_recipe_state.h"
#include "export_job_controller.h"
#include "photo_mcp_server.h"

#include <cstdlib>
#include <iostream>

#include <QColor>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTime>
#include <QUrl>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

photo_editor::desktop::CatalogAssetItem make_asset(
    const QString& asset_id,
    const QString& file_path,
    const QString& recipe_json) {
  photo_editor::desktop::CatalogAssetItem item;
  item.asset_id = asset_id;
  item.camera_label = QStringLiteral("Test Camera");
  item.file_path = file_path;
  item.capture_date = QStringLiteral("Jun 29, 2026");
  item.capture_date_value = QDate(2026, 6, 29);
  item.lens_model = QStringLiteral("RF 50mm F1.2L USM");
  item.iso_label = QStringLiteral("ISO 200");
  item.shutter_speed = QStringLiteral("1/500 s");
  item.aperture_label = QStringLiteral("f/2");
  item.imported_at = QStringLiteral("Jun 29, 2026  1:00 PM");
  item.imported_at_value = QDateTime(QDate(2026, 6, 29), QTime(13, 0));
  item.media_type = QStringLiteral("photo");
  item.recipe_json = recipe_json;
  item.online = true;
  return item;
}

bool write_fixture_image(const QString& file_path) {
  QImage image(20, 20, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixelColor(x, y, QColor(88 + x, 104 + y, 118));
    }
  }
  const QFileInfo info(file_path);
  if (!QDir().mkpath(info.dir().absolutePath())) {
    return false;
  }
  return image.save(file_path, "PNG");
}

QJsonObject read_json_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QJsonDocument::fromJson(file.readAll()).object();
}

QJsonObject send_local_request(const QString& endpoint, const QJsonObject& request) {
  QLocalSocket socket;
  socket.connectToServer(endpoint);
  if (!socket.waitForConnected(1000)) {
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), socket.errorString()}};
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
  socket.write("\n");
  socket.flush();
  socket.waitForBytesWritten(1000);

  QByteArray response;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 3000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (socket.waitForReadyRead(10)) {
      response.append(socket.readAll());
      if (response.contains('\n')) {
        break;
      }
    }
  }
  return QJsonDocument::fromJson(response).object();
}

QJsonObject send_tcp_request(const QString& endpoint, const QJsonObject& request) {
  const auto parts = endpoint.split(QStringLiteral(":"));
  if (parts.size() != 2) {
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("invalid endpoint")}};
  }
  auto ok = false;
  const auto port = parts.at(1).toUShort(&ok);
  if (!ok) {
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("invalid port")}};
  }

  QTcpSocket socket;
  socket.connectToHost(parts.at(0), port);
  if (!socket.waitForConnected(1000)) {
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), socket.errorString()}};
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
  socket.write("\n");
  socket.flush();
  socket.waitForBytesWritten(1000);

  QByteArray response;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 3000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (socket.waitForReadyRead(10)) {
      response.append(socket.readAll());
      if (response.contains('\n')) {
        break;
      }
    }
  }
  return QJsonDocument::fromJson(response).object();
}

QJsonObject send_mcp_http_request(const QString& endpoint, const QJsonObject& request, int* status_code) {
  QUrl url(endpoint);
  if (!url.isValid() || url.host().isEmpty() || url.port() <= 0) {
    return QJsonObject{{QStringLiteral("error"), QStringLiteral("invalid mcp endpoint")}};
  }

  QTcpSocket socket;
  socket.connectToHost(url.host(), static_cast<quint16>(url.port()));
  if (!socket.waitForConnected(1000)) {
    return QJsonObject{{QStringLiteral("error"), socket.errorString()}};
  }

  const auto body = QJsonDocument(request).toJson(QJsonDocument::Compact);
  QByteArray http_request;
  http_request.append("POST ");
  http_request.append(url.path().isEmpty() ? QByteArrayLiteral("/mcp") : url.path().toUtf8());
  http_request.append(" HTTP/1.1\r\nHost: ");
  http_request.append(url.host().toUtf8());
  http_request.append(':');
  http_request.append(QByteArray::number(url.port()));
  http_request.append("\r\nOrigin: http://127.0.0.1\r\nAccept: application/json, text/event-stream\r\nContent-Type: application/json\r\nContent-Length: ");
  http_request.append(QByteArray::number(body.size()));
  http_request.append("\r\n\r\n");
  http_request.append(body);
  socket.write(http_request);
  socket.flush();
  socket.waitForBytesWritten(1000);

  QByteArray response;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 3000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (socket.waitForReadyRead(10)) {
      response.append(socket.readAll());
      if (response.contains("\r\n\r\n")) {
        const auto header_end = response.indexOf("\r\n\r\n");
        const auto header = response.left(header_end);
        auto content_length = -1;
        for (const auto& line_bytes : header.split('\n')) {
          const auto line = QString::fromLatin1(line_bytes).trimmed();
          if (line.toLower().startsWith(QStringLiteral("content-length:"))) {
            auto ok = false;
            content_length = line.mid(QStringLiteral("content-length:").size()).trimmed().toInt(&ok);
            if (!ok) {
              content_length = -1;
            }
          }
        }
        if (content_length >= 0 && response.size() >= header_end + 4 + content_length) {
          break;
        }
      }
    }
  }
  const auto header_end = response.indexOf("\r\n\r\n");
  if (header_end < 0) {
    return QJsonObject{{QStringLiteral("error"), QStringLiteral("missing http response headers")}};
  }
  const auto status_line = QString::fromLatin1(response.left(response.indexOf('\n'))).trimmed();
  const auto status_parts = status_line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (status_code != nullptr && status_parts.size() >= 2) {
    *status_code = status_parts.at(1).toInt();
  }
  return QJsonDocument::fromJson(response.mid(header_end + 4)).object();
}

struct McpSseResponse final {
  int status_code = 0;
  QString content_type;
  QByteArray body;
};

McpSseResponse send_mcp_http_get(const QString& endpoint) {
  McpSseResponse result;
  QUrl url(endpoint);
  if (!url.isValid() || url.host().isEmpty() || url.port() <= 0) {
    result.body = QByteArrayLiteral("invalid mcp endpoint");
    return result;
  }

  QTcpSocket socket;
  socket.connectToHost(url.host(), static_cast<quint16>(url.port()));
  if (!socket.waitForConnected(1000)) {
    result.body = socket.errorString().toUtf8();
    return result;
  }

  QByteArray http_request;
  http_request.append("GET ");
  http_request.append(url.path().isEmpty() ? QByteArrayLiteral("/mcp") : url.path().toUtf8());
  http_request.append(" HTTP/1.1\r\nHost: ");
  http_request.append(url.host().toUtf8());
  http_request.append(':');
  http_request.append(QByteArray::number(url.port()));
  http_request.append("\r\nOrigin: http://127.0.0.1\r\nAccept: text/event-stream\r\n\r\n");
  socket.write(http_request);
  socket.flush();
  socket.waitForBytesWritten(1000);

  QByteArray response;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 3000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (socket.waitForReadyRead(10)) {
      response.append(socket.readAll());
      if (response.contains("\r\n\r\n")) {
        const auto header_end = response.indexOf("\r\n\r\n");
        const auto header = response.left(header_end);
        auto content_length = -1;
        for (const auto& line_bytes : header.split('\n')) {
          const auto line = QString::fromLatin1(line_bytes).trimmed();
          if (line.toLower().startsWith(QStringLiteral("content-length:"))) {
            auto ok = false;
            content_length = line.mid(QStringLiteral("content-length:").size()).trimmed().toInt(&ok);
            if (!ok) {
              content_length = -1;
            }
          }
        }
        if (content_length >= 0 && response.size() >= header_end + 4 + content_length) {
          break;
        }
      }
    }
  }

  const auto header_end = response.indexOf("\r\n\r\n");
  if (header_end < 0) {
    result.body = response;
    return result;
  }
  const auto header = response.left(header_end);
  const auto status_line = QString::fromLatin1(header.left(header.indexOf('\n'))).trimmed();
  const auto status_parts = status_line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (status_parts.size() >= 2) {
    result.status_code = status_parts.at(1).toInt();
  }
  for (const auto& line_bytes : header.split('\n')) {
    const auto line = QString::fromLatin1(line_bytes).trimmed();
    if (line.toLower().startsWith(QStringLiteral("content-type:"))) {
      result.content_type = line.mid(QStringLiteral("content-type:").size()).trimmed();
    }
  }
  result.body = response.mid(header_end + 4);
  return result;
}

QJsonObject send_request(
    const photo_editor::desktop::PhotoAutomationBridge& bridge,
    const QJsonObject& session,
    const QJsonObject& request) {
  if (session.value(QStringLiteral("endpointKind")).toString() == QStringLiteral("in-memory")) {
    return bridge.handleJsonRequest(request);
  }
  const auto kind = session.value(QStringLiteral("endpointKind")).toString();
  const auto endpoint = session.value(QStringLiteral("endpoint")).toString();
  if (kind == QStringLiteral("tcp-localhost")) {
    const auto response = send_tcp_request(endpoint, request);
    return response.isEmpty() ? bridge.handleJsonRequest(request) : response;
  }
  const auto response = send_local_request(endpoint, request);
  return response.isEmpty() ? bridge.handleJsonRequest(request) : response;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("PhotoAutomationBridgeTest"));

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("temporary runtime directory should be created");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  const auto model_manifest_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("agent-models/models.json"));
  if (!QDir().mkpath(QFileInfo(model_manifest_path).dir().absolutePath())) {
    return fail("agent model manifest directory should be created");
  }
  QFile model_manifest_file(model_manifest_path);
  if (!model_manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return fail("agent model manifest should be writable");
  }
  model_manifest_file.write(QJsonDocument(QJsonObject{
      {QStringLiteral("models"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("id"), QStringLiteral("ollama-qwen-tool-plan-test")},
               {QStringLiteral("name"), QStringLiteral("Qwen tool-plan test via Ollama")},
               {QStringLiteral("backend"), QStringLiteral("ollama")},
               {QStringLiteral("endpoint"), QStringLiteral("http://127.0.0.1:11434")},
               {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
               {QStringLiteral("vision"), true},
               {QStringLiteral("available"), true},
           },
       }},
  }).toJson(QJsonDocument::Compact));
  model_manifest_file.close();

  const auto source_path = QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/source.png"));
  if (!write_fixture_image(source_path)) {
    return fail("fixture image should be written");
  }
  const auto second_source_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/source-2.png"));
  if (!write_fixture_image(second_source_path)) {
    return fail("second fixture image should be written");
  }

  photo_editor::desktop::EditRecipeState original_state;
  original_state.output_color_space = QStringLiteral("Display P3");
  const auto original_recipe = photo_editor::desktop::serialize_edit_recipe_json(original_state);

  photo_editor::desktop::CatalogAssetModel catalog_model;
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);

  photo_editor::desktop::AgentEditController agent_edit;
  photo_editor::desktop::ExportJobController export_jobs;
  agent_edit.setCatalogModel(&catalog_model);
  agent_edit.setExportJobController(&export_jobs);
  agent_edit.setSelectedModelId(QStringLiteral("rulebook-deterministic"));

  photo_editor::desktop::PhotoAutomationBridge bridge(&agent_edit, &catalog_model);
  photo_editor::desktop::PhotoMcpServer mcp_server(&bridge);
  QJsonObject session;
  if (bridge.start()) {
    session = read_json_file(bridge.sessionFilePath());
    if (session.value(QStringLiteral("endpoint")).toString() != bridge.endpointName() ||
        session.value(QStringLiteral("endpointKind")).toString() != bridge.endpointKind() ||
        session.value(QStringLiteral("mcpEndpoint")).toString() != bridge.mcpEndpoint() ||
        session.value(QStringLiteral("mcpEndpointKind")).toString() != bridge.mcpEndpointKind() ||
        session.value(QStringLiteral("mcpTransport")).toString() != QStringLiteral("streamable-http") ||
        session.value(QStringLiteral("mcpConnectedClientCount")).toInt() != 0 ||
        session.value(QStringLiteral("mcpTotalClientCount")).toInt() != 0 ||
        session.value(QStringLiteral("token")).toString() != bridge.token()) {
      return fail("session file should expose endpoint and token");
    }
    session.insert(QStringLiteral("endpointKind"), QStringLiteral("in-memory"));
    session.insert(QStringLiteral("endpoint"), QStringLiteral("in-memory"));
  } else {
    const auto error = bridge.errorMessage();
    if (!error.contains(QStringLiteral("listen"), Qt::CaseInsensitive)) {
      std::cerr << error.toStdString() << '\n';
      return fail("automation bridge should start or be blocked only by listener restrictions");
    }
    session = QJsonObject{
        {QStringLiteral("endpointKind"), QStringLiteral("in-memory")},
        {QStringLiteral("endpoint"), QStringLiteral("in-memory")},
        {QStringLiteral("token"), bridge.token()},
    };
  }

  const auto invalid_token = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), QStringLiteral("wrong")},
          {QStringLiteral("command"), QStringLiteral("photo_live_status")},
      });
  if (invalid_token.value(QStringLiteral("ok")).toBool()) {
    return fail("bridge should reject invalid tokens");
  }

  if (!bridge.mcpEndpoint().isEmpty()) {
    const auto mcp_sse = send_mcp_http_get(bridge.mcpEndpoint());
    const auto mcp_sse_body = QString::fromUtf8(mcp_sse.body);
    if (mcp_sse.status_code != 200 ||
        mcp_sse.content_type != QStringLiteral("text/event-stream") ||
        !mcp_sse_body.contains(QStringLiteral("event: endpoint")) ||
        !mcp_sse_body.contains(QStringLiteral("\"transport\":\"streamable-http\"")) ||
        !mcp_sse_body.contains(QStringLiteral("event: tools")) ||
        !mcp_sse_body.contains(QStringLiteral("photo.live_status"))) {
      return fail("MCP HTTP endpoint should expose GET /mcp as an SSE discovery stream");
    }

    auto mcp_http_status_code = 0;
    const auto mcp_http_tools = send_mcp_http_request(
        bridge.mcpEndpoint(),
        QJsonObject{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), 101},
            {QStringLiteral("method"), QStringLiteral("tools/list")},
        },
        &mcp_http_status_code);
    if (mcp_http_status_code != 200 ||
        mcp_http_tools.value(QStringLiteral("id")).toInt() != 101 ||
        mcp_http_tools.value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray().isEmpty()) {
      return fail("MCP HTTP endpoint should serve tools/list over localhost streamable HTTP");
    }

    auto mcp_http_call_status_code = 0;
    const auto mcp_http_status = send_mcp_http_request(
        bridge.mcpEndpoint(),
        QJsonObject{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), 102},
            {QStringLiteral("method"), QStringLiteral("tools/call")},
            {QStringLiteral("params"),
             QJsonObject{
                 {QStringLiteral("name"), QStringLiteral("photo.live_status")},
                 {QStringLiteral("arguments"), QJsonObject{}},
             }},
        },
        &mcp_http_call_status_code);
    if (mcp_http_call_status_code != 200 ||
        mcp_http_status.value(QStringLiteral("result"))
                .toObject()
                .value(QStringLiteral("structuredContent"))
                .toObject()
                .value(QStringLiteral("selectedAssetId"))
                .toString() != QStringLiteral("asset-1")) {
      return fail("MCP HTTP endpoint should serve tools/call with structured tool results");
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (bridge.mcpTotalClientCount() < 2 ||
        bridge.mcpLastToolCallName() != QStringLiteral("photo.live_status") ||
        bridge.mcpLastToolCallStatus() != QStringLiteral("ok") ||
        bridge.mcpLastToolCallAt().isEmpty()) {
      return fail("MCP HTTP endpoint should track client count and last tool call telemetry");
    }

    photo_editor::desktop::AgentMcpClient mcp_client;
    mcp_client.setEndpoint(bridge.mcpEndpoint());
    mcp_client.setTimeoutMs(3000);
    const auto client_tools = mcp_client.refreshToolCache(QStringLiteral("bridge-test-run"));
    if (!client_tools.value(QStringLiteral("ok")).toBool() ||
        client_tools.value(QStringLiteral("runId")).toString() != QStringLiteral("bridge-test-run") ||
        client_tools.value(QStringLiteral("toolCount")).toInt() < 12 ||
        !mcp_client.hasCachedTool(QStringLiteral("photo.live_status"))) {
      return fail("AgentMcpClient should cache MCP tool schemas from the running app endpoint");
    }
    mcp_client.setAllowedTools({QStringLiteral("photo.live_status")});
    const auto client_status = mcp_client.callTool(QStringLiteral("photo.live_status"));
    if (!client_status.value(QStringLiteral("ok")).toBool() ||
        client_status.value(QStringLiteral("structuredContent"))
                .toMap()
                .value(QStringLiteral("selectedAssetId"))
                .toString() != QStringLiteral("asset-1")) {
      return fail("AgentMcpClient should call allowed MCP tools and expose structured results");
    }
    const auto client_status_telemetry = client_status.value(QStringLiteral("structuredContent")).toMap();
    if (client_status_telemetry.value(QStringLiteral("mcpTotalClientCount")).toInt() < 3 ||
        client_status_telemetry.value(QStringLiteral("mcpLastToolCallName")).toString() !=
            QStringLiteral("photo.live_status")) {
      return fail("AgentMcpClient live status should expose MCP telemetry from the running app");
    }
    const auto client_disallowed = mcp_client.callTool(QStringLiteral("photo.commit_agent_draft"));
    if (client_disallowed.value(QStringLiteral("ok")).toBool() ||
        !client_disallowed.value(QStringLiteral("blockedByAllowlist")).toBool()) {
      return fail("AgentMcpClient should block tools outside the run allowlist before calling MCP");
    }
    mcp_client.clearAllowedTools();
    const auto client_missing = mcp_client.callTool(QStringLiteral("photo.not_in_cache"));
    if (client_missing.value(QStringLiteral("ok")).toBool() ||
        !client_missing.value(QStringLiteral("missingFromToolCache")).toBool()) {
      return fail("AgentMcpClient should reject tools missing from the cached schema");
    }
    photo_editor::desktop::AgentMcpClient canceling_mcp_client;
    canceling_mcp_client.setEndpoint(bridge.mcpEndpoint());
    canceling_mcp_client.setTimeoutMs(3000);
    auto cancellation_poll_count = 0;
    canceling_mcp_client.setCancellationPredicate([&cancellation_poll_count]() {
      ++cancellation_poll_count;
      return cancellation_poll_count > 1;
    });
    const auto canceled_refresh =
        canceling_mcp_client.refreshToolCache(QStringLiteral("canceled-mcp-run"));
    if (canceled_refresh.value(QStringLiteral("ok")).toBool() ||
        !canceled_refresh.value(QStringLiteral("canceled")).toBool() ||
        !canceled_refresh.value(QStringLiteral("error")).toString().contains(QStringLiteral("canceled")) ||
        cancellation_poll_count <= 1) {
      return fail("AgentMcpClient should cancel active MCP HTTP requests through the run cancellation predicate");
    }

    agent_edit.setMcpEndpoint(bridge.mcpEndpoint(), bridge.mcpEndpointKind());
    const auto controller_mcp_status = agent_edit.refreshMcpTools();
    if (!controller_mcp_status.value(QStringLiteral("ok")).toBool() ||
        !agent_edit.mcpAvailable() ||
        agent_edit.mcpStatus() != QStringLiteral("Ready") ||
        agent_edit.mcpToolCount() < 12 ||
        !agent_edit.mcpToolNames().contains(QStringLiteral("photo.live_status")) ||
        !agent_edit.mcpLastError().isEmpty() ||
        agent_edit.mcpTotalClientCount() < 4 ||
        agent_edit.mcpLastToolCallName() != QStringLiteral("photo.live_status") ||
        agent_edit.mcpLastToolCallStatus() != QStringLiteral("ok") ||
        controller_mcp_status.value(QStringLiteral("lastToolCallName")).toString() !=
            QStringLiteral("photo.live_status") ||
        agent_edit.mcpEndpoint() != bridge.mcpEndpoint() ||
        agent_edit.mcpEndpointKind() != bridge.mcpEndpointKind()) {
      return fail("AgentEditController should expose refreshed MCP endpoint and tool status");
    }
    const auto controller_schema_cache =
        controller_mcp_status.value(QStringLiteral("schemaCache")).toMap();
    const auto first_schema_revision =
        controller_schema_cache.value(QStringLiteral("revision")).toInt();
    const auto first_schema_fingerprint =
        controller_schema_cache.value(QStringLiteral("fingerprint")).toString();
    if (!controller_schema_cache.value(QStringLiteral("enabled")).toBool() ||
        controller_schema_cache.value(QStringLiteral("primedBeforeRefresh")).toBool() ||
        controller_schema_cache.value(QStringLiteral("changed")).toBool() ||
        first_schema_revision != 1 ||
        first_schema_fingerprint.isEmpty() ||
        controller_schema_cache.value(QStringLiteral("cachedToolCount")).toInt() !=
            agent_edit.mcpToolCount() ||
        !controller_schema_cache.value(QStringLiteral("addedToolNames"))
             .toStringList()
             .contains(QStringLiteral("photo.live_status"))) {
      return fail("AgentEditController should initialize a fingerprinted MCP schema cache on refresh");
    }
    const auto second_controller_mcp_status = agent_edit.refreshMcpTools();
    const auto second_schema_cache =
        second_controller_mcp_status.value(QStringLiteral("schemaCache")).toMap();
    if (!second_controller_mcp_status.value(QStringLiteral("ok")).toBool() ||
        !second_schema_cache.value(QStringLiteral("enabled")).toBool() ||
        !second_schema_cache.value(QStringLiteral("primedBeforeRefresh")).toBool() ||
        second_schema_cache.value(QStringLiteral("changed")).toBool() ||
        second_schema_cache.value(QStringLiteral("revision")).toInt() != first_schema_revision ||
        second_schema_cache.value(QStringLiteral("fingerprint")).toString() !=
            first_schema_fingerprint ||
        !second_schema_cache.value(QStringLiteral("addedToolNames")).toStringList().isEmpty() ||
        !second_schema_cache.value(QStringLiteral("removedToolNames")).toStringList().isEmpty()) {
      return fail("AgentEditController should re-check MCP schemas without bumping the cache revision when unchanged");
    }
    const auto sample_tool_policy = agent_edit.mcpToolPolicyForPhase(QStringLiteral("sample"));
    const auto batch_tool_policy = agent_edit.mcpToolPolicyForPhase(QStringLiteral("batch"));
    const auto commit_tool_policy = agent_edit.mcpToolPolicyForPhase(QStringLiteral("commit-export"));
    const auto sample_tool_names = sample_tool_policy.value(QStringLiteral("toolNames")).toStringList();
    const auto batch_tool_names = batch_tool_policy.value(QStringLiteral("toolNames")).toStringList();
    const auto commit_tool_names = commit_tool_policy.value(QStringLiteral("toolNames")).toStringList();
    const auto sample_policy_schema_cache =
        sample_tool_policy.value(QStringLiteral("schemaCache")).toMap();
    const auto sample_tool_budget =
        sample_tool_policy.value(QStringLiteral("toolBudget")).toMap();
    if (!sample_tool_policy.value(QStringLiteral("ok")).toBool() ||
        sample_tool_policy.value(QStringLiteral("phase")).toString() != QStringLiteral("sample") ||
        !sample_tool_policy.value(QStringLiteral("leastPrivilege")).toBool() ||
        !sample_tool_budget.value(QStringLiteral("hardLimit")).toBool() ||
        sample_tool_budget.value(QStringLiteral("maxModelToolCallsPerTurn")).toInt() != 3 ||
        sample_tool_budget.value(QStringLiteral("phase")).toString() != QStringLiteral("sample") ||
        sample_policy_schema_cache.value(QStringLiteral("revision")).toInt() !=
            first_schema_revision ||
        sample_policy_schema_cache.value(QStringLiteral("schemaSource")).toString() !=
            QStringLiteral("cached-after-refresh") ||
        sample_tool_policy.value(QStringLiteral("availableToolCount")).toInt() <= 0 ||
        !sample_tool_names.contains(QStringLiteral("photo.render_current_proof")) ||
        sample_tool_names.contains(QStringLiteral("photo.commit_agent_draft")) ||
        sample_tool_names.contains(QStringLiteral("photo.export_batch")) ||
        !batch_tool_names.contains(QStringLiteral("photo.plan_bulk_edit")) ||
        !batch_tool_names.contains(QStringLiteral("photo.validate_batch")) ||
        batch_tool_names.contains(QStringLiteral("photo.commit_agent_draft")) ||
        !commit_tool_names.contains(QStringLiteral("photo.commit_agent_draft")) ||
        !commit_tool_names.contains(QStringLiteral("photo.export_batch")) ||
        commit_tool_policy.value(QStringLiteral("availableToolCount")).toInt() <= 0) {
      return fail("AgentEditController should expose phase-scoped MCP tool policies with filtered schemas");
    }

    const QJsonObject tool_plan_content{
        {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
        {QStringLiteral("summary"), QStringLiteral("Read live status before final patch.")},
        {QStringLiteral("toolCalls"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("name"), QStringLiteral("photo.live_status")},
                 {QStringLiteral("arguments"), QJsonObject{}},
                 {QStringLiteral("reason"), QStringLiteral("Confirm current selection and draft state.")},
             },
         }},
    };
    const QJsonObject patch_content{
        {QStringLiteral("decision"), QStringLiteral("recipe_patch")},
        {QStringLiteral("summary"), QStringLiteral("Warm balanced tool-informed draft.")},
        {QStringLiteral("styleConstraintCitations"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("field"), QStringLiteral("constraints.max_exposure_shift")},
                 {QStringLiteral("reason"), QStringLiteral("Bounds exposure changes.")},
             },
             QJsonObject{
                 {QStringLiteral("field"), QStringLiteral("allowedTools.color")},
                 {QStringLiteral("reason"), QStringLiteral("Color edits are allowed.")},
             },
         }},
        {QStringLiteral("recipePatch"),
         QJsonObject{
             {QStringLiteral("globalTone"),
              QJsonObject{
                  {QStringLiteral("exposure"), 0.08},
                  {QStringLiteral("highlights"), -8.0},
                  {QStringLiteral("shadows"), 6.0},
              }},
             {QStringLiteral("color"),
              QJsonObject{
                  {QStringLiteral("temperature"), 3.0},
                  {QStringLiteral("vibrance"), 4.0},
              }},
         }},
    };
    const QJsonArray fake_ollama_sequence{
        QJsonObject{
            {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
            {QStringLiteral("done"), true},
            {QStringLiteral("message"),
             QJsonObject{
                 {QStringLiteral("role"), QStringLiteral("assistant")},
                 {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(tool_plan_content).toJson(QJsonDocument::Compact))},
             }},
        },
        QJsonObject{
            {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
            {QStringLiteral("done"), true},
            {QStringLiteral("message"),
             QJsonObject{
                 {QStringLiteral("role"), QStringLiteral("assistant")},
                 {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(patch_content).toJson(QJsonDocument::Compact))},
             }},
        },
    };
    qputenv(
        "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON",
        QJsonDocument(fake_ollama_sequence).toJson(QJsonDocument::Compact));
    agent_edit.setSelectedModelId(QStringLiteral("ollama-qwen-tool-plan-test"));
    const auto mcp_draft_report =
        agent_edit.runDraftOnSelected(QStringLiteral("Use the active style and keep the proof natural."));
    qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON");
    auto saw_mcp_discovery_tool_call = false;
    auto saw_mcp_live_status_call = false;
    auto saw_mcp_style_rulebook_call = false;
    auto saw_mcp_recipe_schema_call = false;
    auto saw_model_live_status_call = false;
    for (const auto& tool_call_value : mcp_draft_report.value(QStringLiteral("toolCalls")).toList()) {
      const auto tool_call_name = tool_call_value.toMap().value(QStringLiteral("name")).toString();
      if (tool_call_name == QStringLiteral("mcp.tools/list")) {
        saw_mcp_discovery_tool_call = true;
      } else if (tool_call_name == QStringLiteral("mcp.tools/call photo.live_status")) {
        saw_mcp_live_status_call = true;
      } else if (tool_call_name == QStringLiteral("mcp.tools/call photo.get_style_rulebook")) {
        saw_mcp_style_rulebook_call = true;
      } else if (tool_call_name == QStringLiteral("mcp.tools/call photo.get_recipe_patch_schema")) {
        saw_mcp_recipe_schema_call = true;
      } else if (tool_call_name == QStringLiteral("mcp.model_tool_call photo.live_status")) {
        saw_model_live_status_call = true;
      }
    }
    const auto mcp_observation = mcp_draft_report.value(QStringLiteral("mcpObservation")).toMap();
    const auto mcp_discovery = mcp_draft_report.value(QStringLiteral("mcpDiscovery")).toMap();
    const auto model_tool_plan = mcp_draft_report.value(QStringLiteral("modelToolPlan")).toMap();
    const auto draft_mcp_tool_budget =
        mcp_draft_report.value(QStringLiteral("mcpToolBudget")).toMap();
    const auto model_tool_plan_budget =
        model_tool_plan.value(QStringLiteral("toolBudget")).toMap();
    if (!mcp_draft_report.value(QStringLiteral("ok")).toBool() ||
        !mcp_draft_report.value(QStringLiteral("mcpAttached")).toBool() ||
        mcp_discovery.value(QStringLiteral("mode")).toString() != QStringLiteral("mcp-readiness") ||
        !mcp_discovery.value(QStringLiteral("ready")).toBool() ||
        mcp_discovery.value(QStringLiteral("attemptCount")).toInt() < 1 ||
        mcp_discovery.value(QStringLiteral("recoveryPolicy")).toString() !=
            QStringLiteral("retry-discovery-before-agent-planning") ||
        !mcp_observation.value(QStringLiteral("ok")).toBool() ||
        !model_tool_plan.value(QStringLiteral("ok")).toBool() ||
        draft_mcp_tool_budget.value(QStringLiteral("maxModelToolCallsPerTurn")).toInt() != 3 ||
        !draft_mcp_tool_budget.value(QStringLiteral("hardLimit")).toBool() ||
        model_tool_plan_budget.value(QStringLiteral("maxModelToolCallsPerTurn")).toInt() != 3 ||
        model_tool_plan_budget.value(QStringLiteral("requestedToolCallCount")).toInt() != 1 ||
        model_tool_plan.value(QStringLiteral("successCount")).toInt() != 1 ||
        model_tool_plan.value(QStringLiteral("allowedTools")).toStringList().contains(QStringLiteral("photo.commit_agent_draft")) ||
        mcp_observation.value(QStringLiteral("successCount")).toInt() != 3 ||
        mcp_observation.value(QStringLiteral("toolReports")).toList().size() != 3 ||
        mcp_draft_report.value(QStringLiteral("mcpToolPhase")).toString() != QStringLiteral("sample") ||
        mcp_draft_report.value(QStringLiteral("mcpAllowedToolNames"))
            .toStringList()
            .contains(QStringLiteral("photo.commit_agent_draft")) ||
        mcp_draft_report.value(QStringLiteral("mcpAvailableToolCount")).toInt() <= 0 ||
        mcp_draft_report.value(QStringLiteral("mcpToolCount")).toInt() < 12 ||
        !mcp_draft_report.value(QStringLiteral("mcpToolNames"))
             .toStringList()
             .contains(QStringLiteral("photo.live_status")) ||
        !saw_mcp_discovery_tool_call ||
        !saw_mcp_live_status_call ||
        !saw_mcp_style_rulebook_call ||
        !saw_mcp_recipe_schema_call ||
        !saw_model_live_status_call) {
      return fail("Agent draft runs should execute read-only model-selected MCP tool plans before final local model planning");
    }
    const auto user_visible_tool_events =
        mcp_draft_report.value(QStringLiteral("userVisibleToolEvents")).toList();
    const auto tool_event_policy =
        mcp_draft_report.value(QStringLiteral("toolEventDisplayPolicy")).toMap();
    auto saw_visible_internal_mcp_event = false;
    auto saw_visible_plan_event = false;
    auto saw_visible_apply_event = false;
    for (const auto& event_value : user_visible_tool_events) {
      const auto event = event_value.toMap();
      const auto source_tool_name = event.value(QStringLiteral("sourceToolName")).toString();
      if (source_tool_name.startsWith(QStringLiteral("mcp."))) {
        saw_visible_internal_mcp_event = true;
      }
      if (source_tool_name == QStringLiteral("plan_agent_recipe_patch")) {
        saw_visible_plan_event = true;
      }
      if (source_tool_name == QStringLiteral("apply_recipe_patch_to_selected")) {
        saw_visible_apply_event = true;
      }
    }
    const auto manifest = read_json_file(mcp_draft_report.value(QStringLiteral("manifestPath")).toString());
    const auto manifest_tool_event_policy =
        manifest.value(QStringLiteral("toolEventDisplayPolicy")).toObject();
    if (user_visible_tool_events.isEmpty() ||
        user_visible_tool_events.size() >= mcp_draft_report.value(QStringLiteral("toolCalls")).toList().size() ||
        saw_visible_internal_mcp_event ||
        !saw_visible_plan_event ||
        !saw_visible_apply_event ||
        tool_event_policy.value(QStringLiteral("fullAuditField")).toString() !=
            QStringLiteral("toolCalls") ||
        tool_event_policy.value(QStringLiteral("hiddenToolCallCount")).toInt() <= 0 ||
        !tool_event_policy.value(QStringLiteral("defaultHidesInternalMcpEvents")).toBool() ||
        manifest.value(QStringLiteral("userVisibleToolEvents")).toArray().isEmpty() ||
        manifest_tool_event_policy.value(QStringLiteral("fullAuditField")).toString() !=
            QStringLiteral("toolCalls")) {
      return fail("Agent run reports should default to user-relevant tool events while retaining the full MCP audit log");
    }
    const auto mcp_draft_rollback = agent_edit.rollbackDraft();
    if (!mcp_draft_rollback.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
      return fail("MCP-audited draft run should remain rollbackable");
    }
    agent_edit.setSelectedModelId(QStringLiteral("rulebook-deterministic"));
  }

  const auto mcp_tools = mcp_server.handleJsonRpc(QJsonObject{
      {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
      {QStringLiteral("id"), 1},
      {QStringLiteral("method"), QStringLiteral("tools/list")},
  });
  const auto mcp_tool_values =
      mcp_tools.value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray();
  auto saw_live_status_tool = false;
  auto saw_recipe_schema_tool = false;
  auto saw_commit_confirmation = false;
  for (const auto& tool_value : mcp_tool_values) {
    const auto tool = tool_value.toObject();
    const auto name = tool.value(QStringLiteral("name")).toString();
    if (name == QStringLiteral("photo.live_status") &&
        tool.value(QStringLiteral("inputSchema")).toObject().value(QStringLiteral("type")).toString() ==
            QStringLiteral("object") &&
        tool.value(QStringLiteral("annotations")).toObject().value(QStringLiteral("readOnlyHint")).toBool()) {
      saw_live_status_tool = true;
    }
    if (name == QStringLiteral("photo.get_recipe_patch_schema") &&
        tool.value(QStringLiteral("annotations")).toObject().value(QStringLiteral("readOnlyHint")).toBool()) {
      saw_recipe_schema_tool = true;
    }
    if (name == QStringLiteral("photo.commit_agent_draft") &&
        tool.value(QStringLiteral("annotations")).toObject().value(QStringLiteral("requiresConfirmation")).toBool() &&
        tool.value(QStringLiteral("annotations")).toObject().value(QStringLiteral("destructiveHint")).toBool()) {
      saw_commit_confirmation = true;
    }
  }
  if (mcp_tools.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0") ||
      mcp_tools.value(QStringLiteral("id")).toInt() != 1 ||
      mcp_tool_values.size() < 12 ||
      !saw_live_status_tool ||
      !saw_recipe_schema_tool ||
      !saw_commit_confirmation) {
    return fail("MCP tools/list should expose photo tools with safety annotations");
  }

  const auto mcp_status = mcp_server.handleJsonRpc(QJsonObject{
      {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
      {QStringLiteral("id"), 2},
      {QStringLiteral("method"), QStringLiteral("tools/call")},
      {QStringLiteral("params"),
       QJsonObject{
           {QStringLiteral("name"), QStringLiteral("photo.live_status")},
           {QStringLiteral("arguments"), QJsonObject{}},
       }},
  });
  const auto mcp_status_result = mcp_status.value(QStringLiteral("result")).toObject();
  if (mcp_status.value(QStringLiteral("id")).toInt() != 2 ||
      mcp_status_result.value(QStringLiteral("isError")).toBool() ||
      mcp_status_result.value(QStringLiteral("structuredContent"))
              .toObject()
              .value(QStringLiteral("selectedAssetCount"))
              .toInt() != 1 ||
      mcp_status_result.value(QStringLiteral("content")).toArray().isEmpty()) {
    return fail("MCP tools/call should return structured live status content");
  }

  const auto mcp_schema = mcp_server.handleJsonRpc(QJsonObject{
      {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
      {QStringLiteral("id"), 3},
      {QStringLiteral("method"), QStringLiteral("tools/call")},
      {QStringLiteral("params"),
       QJsonObject{
           {QStringLiteral("name"), QStringLiteral("photo.get_recipe_patch_schema")},
           {QStringLiteral("arguments"), QJsonObject{}},
       }},
  });
  const auto mcp_schema_content =
      mcp_schema.value(QStringLiteral("result")).toObject().value(QStringLiteral("structuredContent")).toObject();
  if (!mcp_schema_content.value(QStringLiteral("typedSchema")).toBool() ||
      !mcp_schema_content.value(QStringLiteral("validationBacked")).toBool() ||
      !mcp_schema_content.value(QStringLiteral("groups")).isObject()) {
    return fail("MCP recipe schema tool should expose the validation-backed patch schema");
  }

  const auto mcp_unknown = mcp_server.handleJsonRpc(QJsonObject{
      {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
      {QStringLiteral("id"), 4},
      {QStringLiteral("method"), QStringLiteral("tools/call")},
      {QStringLiteral("params"),
       QJsonObject{
           {QStringLiteral("name"), QStringLiteral("photo.unknown_tool")},
           {QStringLiteral("arguments"), QJsonObject{}},
       }},
  });
  if (!mcp_unknown.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool()) {
    return fail("MCP unknown tool calls should return a tool execution error");
  }

  const auto mcp_invalid_method = mcp_server.handleJsonRpc(QJsonObject{
      {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
      {QStringLiteral("id"), 5},
      {QStringLiteral("method"), QStringLiteral("resources/list")},
  });
  if (mcp_invalid_method.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt() !=
      -32601) {
    return fail("MCP unsupported methods should return a JSON-RPC method error");
  }

  const auto unknown_field = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_live_status")},
          {QStringLiteral("unexpected"), true},
      });
  if (unknown_field.value(QStringLiteral("ok")).toBool()) {
    return fail("bridge should reject unknown top-level fields");
  }

  const auto status = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_live_status")},
      });
  if (!status.value(QStringLiteral("ok")).toBool() ||
      status.value(QStringLiteral("result")).toObject().value(QStringLiteral("selectedAssetCount")).toInt() != 1) {
    return fail("photo_live_status should report active selection");
  }

  const auto selected_asset = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_read_asset")},
      });
  const auto selected_asset_result = selected_asset.value(QStringLiteral("result")).toObject();
  if (!selected_asset.value(QStringLiteral("ok")).toBool() ||
      selected_asset_result.value(QStringLiteral("assetId")).toString() != QStringLiteral("asset-1") ||
      selected_asset_result.value(QStringLiteral("asset")).toObject().value(QStringLiteral("filePath")).toString() !=
          source_path) {
    return fail("photo_read_asset should return selected asset metadata");
  }

  const auto proof = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_render_proof")},
      });
  const auto proof_result = proof.value(QStringLiteral("result")).toObject();
  const auto proof_path = proof_result.value(QStringLiteral("currentProofPath")).toString();
  if (!proof.value(QStringLiteral("ok")).toBool() ||
      !proof_result.value(QStringLiteral("ok")).toBool() ||
      proof_result.value(QStringLiteral("mode")).toString() != QStringLiteral("render-proof") ||
      !QFileInfo::exists(proof_path) ||
      agent_edit.draftActive()) {
    return fail("photo_render_proof should write a read-only proof artifact for the selected asset");
  }

  const auto validate_edit = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_validate_edit")},
      });
  const auto validate_edit_result = validate_edit.value(QStringLiteral("result")).toObject();
  if (!validate_edit.value(QStringLiteral("ok")).toBool() ||
      !validate_edit_result.value(QStringLiteral("ok")).toBool() ||
      validate_edit_result.value(QStringLiteral("mode")).toString() != QStringLiteral("validate-edit") ||
      !validate_edit_result.value(QStringLiteral("validationReport")).toObject().value(QStringLiteral("ok")).toBool() ||
      !QFileInfo::exists(validate_edit_result.value(QStringLiteral("currentProofPath")).toString()) ||
      agent_edit.draftActive()) {
    return fail("photo_validate_edit should validate the selected edit without creating a draft");
  }

  const auto export_dir = QDir(runtime_root.path()).filePath(QStringLiteral("exports"));
  if (!QDir().mkpath(export_dir)) {
    return fail("export destination should be created for bridge preflight testing");
  }
  const auto export_preflight = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_export_preflight")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("preset"), QStringLiteral("Web Proof")},
               {QStringLiteral("destinationDirectory"), export_dir},
               {QStringLiteral("histogramSampleCount"), 100},
               {QStringLiteral("highlightClipCount"), 2},
           }},
      });
  const auto export_preflight_result = export_preflight.value(QStringLiteral("result")).toObject();
  if (!export_preflight.value(QStringLiteral("ok")).toBool() ||
      !export_preflight_result.value(QStringLiteral("ok")).toBool() ||
      export_preflight_result.value(QStringLiteral("mode")).toString() != QStringLiteral("export-preflight") ||
      export_preflight_result.value(QStringLiteral("checkedCount")).toInt() != 1 ||
      export_preflight_result.value(QStringLiteral("warningCount")).toInt() < 2 ||
      !QFileInfo::exists(export_preflight_result.value(QStringLiteral("preflightReportPath")).toString()) ||
      agent_edit.draftActive()) {
    return fail("photo_export_preflight should return and persist selected asset export readiness without creating a draft");
  }
  const auto malformed_preflight = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_export_preflight")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("histogramSampleCount"), QStringLiteral("many")}}},
      });
  if (malformed_preflight.value(QStringLiteral("ok")).toBool()) {
    return fail("photo_export_preflight should reject malformed metric args");
  }

  const auto unconfirmed_export = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_export_batch")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("confirmed"), false},
               {QStringLiteral("preset"), QStringLiteral("Web Proof")},
               {QStringLiteral("destinationDirectory"), export_dir},
           }},
      });
  if (!unconfirmed_export.value(QStringLiteral("ok")).toBool() ||
      unconfirmed_export.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool() ||
      export_jobs.jobCount() != 0) {
    return fail("photo_export_batch should require explicit confirmation before queueing jobs");
  }
  const auto confirmed_export = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_export_batch")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("confirmed"), true},
               {QStringLiteral("preset"), QStringLiteral("Web Proof")},
               {QStringLiteral("destinationDirectory"), export_dir},
               {QStringLiteral("histogramSampleCount"), 100},
               {QStringLiteral("highlightClipCount"), 2},
           }},
      });
  const auto confirmed_export_result = confirmed_export.value(QStringLiteral("result")).toObject();
  if (!confirmed_export.value(QStringLiteral("ok")).toBool() ||
      !confirmed_export_result.value(QStringLiteral("ok")).toBool() ||
      confirmed_export_result.value(QStringLiteral("queuedCount")).toInt() != 1 ||
      export_jobs.jobCount() != 1 ||
      agent_edit.draftActive()) {
    return fail("photo_export_batch should queue confirmed selected-asset exports");
  }

  const auto malformed_patch = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_validate_recipe_patch")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("patch"), QJsonObject{}},
               {QStringLiteral("extra"), true},
           }},
      });
  if (malformed_patch.value(QStringLiteral("ok")).toBool()) {
    return fail("bridge should reject unknown command args");
  }

  const auto linear_mask = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_create_mask")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("type"), QStringLiteral("linear")},
               {QStringLiteral("position"), 0.42},
               {QStringLiteral("rotation"), -12.0},
               {QStringLiteral("exposure"), 0.3},
               {QStringLiteral("feather"), 0.35},
           }},
      });
  const auto linear_mask_result = linear_mask.value(QStringLiteral("result")).toObject();
  const auto linear_mask_patch = linear_mask_result.value(QStringLiteral("maskPatch")).toObject();
  const auto linear_mask_values = linear_mask_patch.value(QStringLiteral("masks")).toObject();
  const auto linear_state = photo_editor::desktop::parse_edit_recipe_json(catalog_model.selectedRecipeJson());
  if (!linear_mask.value(QStringLiteral("ok")).toBool() ||
      !linear_mask_result.value(QStringLiteral("ok")).toBool() ||
      linear_mask_result.value(QStringLiteral("mode")).toString() != QStringLiteral("photo_create_mask") ||
      linear_mask_result.value(QStringLiteral("maskType")).toString() != QStringLiteral("linear") ||
      linear_mask_values.value(QStringLiteral("linearMaskExposure")).toDouble() != 0.3 ||
      linear_state.mask_stack.empty() ||
      !agent_edit.draftActive()) {
    return fail("photo_create_mask should create an active linear mask draft");
  }
  const auto rollback_linear_mask = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_rollback_agent_draft")},
      });
  if (!rollback_linear_mask.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should roll back a mask creation draft");
  }

  const auto radial_mask = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_apply_mask_adjustment")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("type"), QStringLiteral("radial")},
               {QStringLiteral("centerX"), 0.52},
               {QStringLiteral("centerY"), 0.48},
               {QStringLiteral("radiusX"), 0.3},
               {QStringLiteral("radiusY"), 0.28},
               {QStringLiteral("exposure"), -0.2},
               {QStringLiteral("temperature"), 8.0},
           }},
      });
  const auto radial_mask_result = radial_mask.value(QStringLiteral("result")).toObject();
  const auto radial_mask_patch = radial_mask_result.value(QStringLiteral("maskPatch")).toObject();
  const auto radial_mask_values = radial_mask_patch.value(QStringLiteral("masks")).toObject();
  const auto radial_state = photo_editor::desktop::parse_edit_recipe_json(catalog_model.selectedRecipeJson());
  if (!radial_mask.value(QStringLiteral("ok")).toBool() ||
      !radial_mask_result.value(QStringLiteral("ok")).toBool() ||
      radial_mask_result.value(QStringLiteral("mode")).toString() != QStringLiteral("photo_apply_mask_adjustment") ||
      radial_mask_result.value(QStringLiteral("maskType")).toString() != QStringLiteral("radial") ||
      radial_mask_values.value(QStringLiteral("radialMaskExposure")).toDouble() != -0.2 ||
      radial_mask_values.value(QStringLiteral("radialMaskTemperature")).toDouble() != 8.0 ||
      radial_state.mask_stack.empty() ||
      !agent_edit.draftActive()) {
    return fail("photo_apply_mask_adjustment should create an active radial mask adjustment draft");
  }
  const auto rollback_radial_mask = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_rollback_agent_draft")},
      });
  if (!rollback_radial_mask.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should roll back a mask adjustment draft");
  }

  const auto malformed_mask = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_apply_mask_adjustment")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("type"), QStringLiteral("radial")},
               {QStringLiteral("radiusX"), 2.0},
           }},
      });
  if (malformed_mask.value(QStringLiteral("ok")).toBool()) {
    return fail("photo_apply_mask_adjustment should reject out-of-range mask fields");
  }

  const auto single_asset_bulk = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_plan_bulk_edit")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("instructions"), QStringLiteral("consistent editorial batch")}}},
      });
  if (single_asset_bulk.value(QStringLiteral("ok")).toBool()) {
    return fail("photo_plan_bulk_edit should require a multi-asset selection");
  }

  catalog_model.toggleSelectionIndex(1);
  const auto bulk_plan = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_plan_bulk_edit")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("instructions"), QStringLiteral("consistent editorial batch")}}},
      });
  const auto bulk_plan_result = bulk_plan.value(QStringLiteral("result")).toObject();
  if (!bulk_plan.value(QStringLiteral("ok")).toBool() ||
      !bulk_plan_result.value(QStringLiteral("ok")).toBool() ||
      bulk_plan_result.value(QStringLiteral("automationCommand")).toString() !=
          QStringLiteral("photo_plan_bulk_edit") ||
      bulk_plan_result.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      bulk_plan_result.value(QStringLiteral("assetIds")).toArray().size() != 2 ||
      bulk_plan_result.value(QStringLiteral("recipePatch")).toObject().isEmpty() ||
      agent_edit.draftActive()) {
    return fail("photo_plan_bulk_edit should plan a selected-set draft without mutating recipes");
  }

  const auto recipe_before_batch_validation = catalog_model.selectedRecipeJson();
  const auto batch_validation = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_validate_batch")},
      });
  const auto batch_validation_result = batch_validation.value(QStringLiteral("result")).toObject();
  if (!batch_validation.value(QStringLiteral("ok")).toBool() ||
      !batch_validation_result.value(QStringLiteral("ok")).toBool() ||
      batch_validation_result.value(QStringLiteral("mode")).toString() != QStringLiteral("validate-batch") ||
      batch_validation_result.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      batch_validation_result.value(QStringLiteral("checkedCount")).toInt() != 2 ||
      batch_validation_result.value(QStringLiteral("assetReports")).toArray().size() != 2 ||
      !QFileInfo::exists(batch_validation_result.value(QStringLiteral("batchValidationReportPath")).toString()) ||
      agent_edit.draftActive() ||
      catalog_model.selectedRecipeJson() != recipe_before_batch_validation) {
    return fail("photo_validate_batch should validate selected assets without mutating recipes");
  }

  const auto bulk_draft = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_apply_bulk_draft")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("instructions"), QStringLiteral("consistent editorial batch")}}},
      });
  const auto bulk_draft_result = bulk_draft.value(QStringLiteral("result")).toObject();
  if (!bulk_draft.value(QStringLiteral("ok")).toBool() ||
      !bulk_draft_result.value(QStringLiteral("ok")).toBool() ||
      bulk_draft_result.value(QStringLiteral("automationCommand")).toString() !=
          QStringLiteral("photo_apply_bulk_draft") ||
      bulk_draft_result.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      bulk_draft_result.value(QStringLiteral("draftedCount")).toInt() != 2 ||
      bulk_draft_result.value(QStringLiteral("assetReports")).toArray().size() != 2 ||
      !agent_edit.draftActive()) {
    return fail("photo_apply_bulk_draft should create a reversible selected-set draft");
  }
  const auto rollback_bulk = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_rollback_agent_draft")},
      });
  if (!rollback_bulk.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should roll back a bulk draft");
  }

  const auto draft = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_run_draft_on_selected")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("instructions"), QStringLiteral("warm bright editorial draft")}}},
      });
  if (!draft.value(QStringLiteral("ok")).toBool() ||
      !draft.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool() ||
      !agent_edit.draftActive()) {
    return fail("bridge should run an authenticated agent draft");
  }

  const auto unconfirmed_commit = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_commit_agent_draft")},
          {QStringLiteral("args"), QJsonObject{{QStringLiteral("confirmed"), false}}},
      });
  if (unconfirmed_commit.value(QStringLiteral("ok")).toBool() || !agent_edit.draftActive()) {
    return fail("bridge should reject unconfirmed agent draft commits without clearing the draft");
  }

  const auto rollback = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_rollback_agent_draft")},
      });
  if (!rollback.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should roll back an authenticated agent draft");
  }

  const QJsonObject retouch_patch{
      {QStringLiteral("retouch"),
       QJsonObject{
           {QStringLiteral("retouchStack"),
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("bridge-heal-retouch")},
                    {QStringLiteral("name"), QStringLiteral("Bridge Heal Retouch")},
                    {QStringLiteral("kind"), QStringLiteral("brush")},
                    {QStringLiteral("retouchMode"), QStringLiteral("heal")},
                    {QStringLiteral("brushPoints"),
                     QJsonArray{
                         QJsonObject{
                             {QStringLiteral("x"), 0.42},
                             {QStringLiteral("y"), 0.48},
                             {QStringLiteral("size"), 0.08},
                             {QStringLiteral("flow"), 0.72},
                         },
                     }},
                },
            }},
       }},
  };
  const auto unconfirmed_retouch = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_apply_recipe_patch_draft")},
          {QStringLiteral("args"), QJsonObject{{QStringLiteral("patch"), retouch_patch}}},
      });
  if (unconfirmed_retouch.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should require explicit confirmation before applying retouch patches");
  }
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  const auto confirmed_retouch = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_apply_recipe_patch_draft")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("patch"), retouch_patch},
               {QStringLiteral("confirmed"), true},
           }},
      });
  if (!confirmed_retouch.value(QStringLiteral("ok")).toBool() ||
      !confirmed_retouch.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool() ||
      !agent_edit.draftActive()) {
    return fail("bridge should apply confirmed retouch patches as reversible drafts");
  }
  const auto rollback_retouch = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_rollback_agent_draft")},
      });
  if (!rollback_retouch.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should roll back confirmed retouch drafts");
  }
  catalog_model.toggleSelectionIndex(1);

  const auto commit_draft = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_run_draft_on_selected")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("instructions"), QStringLiteral("warm bright editorial draft")}}},
      });
  if (!commit_draft.value(QStringLiteral("ok")).toBool() || !agent_edit.draftActive()) {
    return fail("bridge should create a draft for confirmed commit testing");
  }
  const auto confirmed_commit = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_commit_agent_draft")},
          {QStringLiteral("args"), QJsonObject{{QStringLiteral("confirmed"), true}}},
      });
  const auto confirmed_commit_result = confirmed_commit.value(QStringLiteral("result")).toObject();
  if (!confirmed_commit.value(QStringLiteral("ok")).toBool() ||
      !confirmed_commit_result.value(QStringLiteral("ok")).toBool() ||
      confirmed_commit_result.value(QStringLiteral("mode")).toString() != QStringLiteral("commit") ||
      confirmed_commit_result.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      confirmed_commit_result.value(QStringLiteral("committedCount")).toInt() != 2 ||
      confirmed_commit_result.value(QStringLiteral("assetIds")).toArray().size() != 2 ||
      agent_edit.draftActive()) {
    return fail("bridge should commit a selected-set agent draft only after explicit confirmation");
  }

  const auto empty_commit = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_commit_agent_draft")},
          {QStringLiteral("args"), QJsonObject{{QStringLiteral("confirmed"), true}}},
      });
  if (!empty_commit.value(QStringLiteral("ok")).toBool() ||
      empty_commit.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool()) {
    return fail("bridge should not commit when no agent draft is active");
  }

  const auto idle_cancel = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_cancel_agent_run")},
          {QStringLiteral("args"), QJsonObject{{QStringLiteral("reason"), QStringLiteral("test_idle")}}},
      });
  if (!idle_cancel.value(QStringLiteral("ok")).toBool() ||
      idle_cancel.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool()) {
    return fail("bridge cancel command should report no active run while idle");
  }

  bridge.stop();
  if (QFileInfo::exists(bridge.sessionFilePath())) {
    return fail("stopping bridge should remove session file");
  }

  return EXIT_SUCCESS;
}

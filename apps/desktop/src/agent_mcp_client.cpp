#include "agent_mcp_client.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace photo_editor::desktop {
namespace {

QVariantMap result_map(bool ok, const QString& error = {}) {
  QVariantMap result{{QStringLiteral("ok"), ok}};
  if (!ok) {
    result.insert(QStringLiteral("error"), error);
  }
  return result;
}

QString normalized_tool_name(const QString& name) {
  return name.trimmed();
}

QVariantMap http_error(const QString& error) {
  auto result = result_map(false, error);
  result.insert(QStringLiteral("transport"), QStringLiteral("streamable-http"));
  return result;
}

QVariantMap cancellation_error(const QString& error) {
  auto result = http_error(error);
  result.insert(QStringLiteral("canceled"), true);
  return result;
}

int content_length_from_header(const QByteArray& header) {
  for (const auto& line_bytes : header.split('\n')) {
    const auto line = QString::fromLatin1(line_bytes).trimmed();
    if (!line.toLower().startsWith(QStringLiteral("content-length:"))) {
      continue;
    }
    auto ok = false;
    const auto length = line.mid(QStringLiteral("content-length:").size()).trimmed().toInt(&ok);
    return ok ? length : -1;
  }
  return -1;
}

int status_code_from_header(const QByteArray& header) {
  const auto first_newline = header.indexOf('\n');
  const auto status_line = QString::fromLatin1(
      first_newline >= 0 ? header.left(first_newline) : header).trimmed();
  const auto parts = status_line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (parts.size() < 2) {
    return 0;
  }
  return parts.at(1).toInt();
}

}  // namespace

AgentMcpClient::AgentMcpClient(QObject* parent) : QObject(parent) {}

QString AgentMcpClient::endpoint() const {
  return endpoint_;
}

void AgentMcpClient::setEndpoint(const QString& endpoint) {
  endpoint_ = endpoint.trimmed();
}

void AgentMcpClient::setBearerToken(const QString& token) {
  bearer_token_ = token.trimmed();
}

int AgentMcpClient::timeoutMs() const {
  return timeout_ms_;
}

void AgentMcpClient::setTimeoutMs(int timeout_ms) {
  timeout_ms_ = std::clamp(timeout_ms, 250, 300000);
}

QStringList AgentMcpClient::allowedTools() const {
  return allowed_tools_;
}

void AgentMcpClient::setAllowedTools(const QStringList& allowed_tools) {
  allowed_tools_.clear();
  for (const auto& tool : allowed_tools) {
    const auto normalized = normalized_tool_name(tool);
    if (!normalized.isEmpty() && !allowed_tools_.contains(normalized)) {
      allowed_tools_.append(normalized);
    }
  }
}

void AgentMcpClient::clearAllowedTools() {
  allowed_tools_.clear();
}

void AgentMcpClient::setCancellationPredicate(std::function<bool()> predicate) {
  cancellation_predicate_ = std::move(predicate);
}

void AgentMcpClient::clearCancellationPredicate() {
  cancellation_predicate_ = {};
}

QVariantList AgentMcpClient::cachedTools() const {
  return cached_tools_;
}

QStringList AgentMcpClient::cachedToolNames() const {
  QStringList names;
  for (const auto& tool_value : cached_tools_) {
    const auto name = tool_value.toMap().value(QStringLiteral("name")).toString();
    if (!name.isEmpty()) {
      names.append(name);
    }
  }
  return names;
}

bool AgentMcpClient::hasCachedTool(const QString& name) const {
  return cachedToolNames().contains(normalized_tool_name(name));
}

QVariantMap AgentMcpClient::refreshToolCache(const QString& run_id) {
  QVariantMap request{
      {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
      {QStringLiteral("id"), next_request_id_++},
      {QStringLiteral("method"), QStringLiteral("tools/list")},
      {QStringLiteral("params"), QVariantMap{}},
  };
  auto response = postJsonRpc(request);
  if (!response.value(QStringLiteral("ok")).toBool()) {
    return response;
  }
  const auto result = response.value(QStringLiteral("result")).toMap();
  const auto tools = result.value(QStringLiteral("tools")).toList();
  cached_tools_ = tools;
  cached_run_id_ = run_id.trimmed();
  QVariantMap report = result_map(true);
  report.insert(QStringLiteral("mode"), QStringLiteral("mcp-tools-list"));
  report.insert(QStringLiteral("runId"), cached_run_id_);
  report.insert(QStringLiteral("endpoint"), endpoint_);
  report.insert(QStringLiteral("toolCount"), tools.size());
  report.insert(QStringLiteral("tools"), tools);
  report.insert(QStringLiteral("toolNames"), cachedToolNames());
  return report;
}

QVariantMap AgentMcpClient::callTool(const QString& name, const QVariantMap& arguments) {
  const auto tool_name = normalized_tool_name(name);
  if (tool_name.isEmpty()) {
    return result_map(false, QStringLiteral("MCP tool name is required."));
  }
  if (!allowed_tools_.isEmpty() && !allowed_tools_.contains(tool_name)) {
    auto result = result_map(false, QStringLiteral("MCP tool is not allowed for this agent run: %1").arg(tool_name));
    result.insert(QStringLiteral("mode"), QStringLiteral("mcp-tool-call"));
    result.insert(QStringLiteral("toolName"), tool_name);
    result.insert(QStringLiteral("blockedByAllowlist"), true);
    return result;
  }
  if (!cached_tools_.isEmpty() && !hasCachedTool(tool_name)) {
    auto result = result_map(false, QStringLiteral("MCP tool is not present in the cached tool schema: %1").arg(tool_name));
    result.insert(QStringLiteral("mode"), QStringLiteral("mcp-tool-call"));
    result.insert(QStringLiteral("toolName"), tool_name);
    result.insert(QStringLiteral("missingFromToolCache"), true);
    return result;
  }

  QVariantMap request{
      {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
      {QStringLiteral("id"), next_request_id_++},
      {QStringLiteral("method"), QStringLiteral("tools/call")},
      {QStringLiteral("params"),
       QVariantMap{
           {QStringLiteral("name"), tool_name},
           {QStringLiteral("arguments"), arguments},
       }},
  };
  auto response = postJsonRpc(request);
  if (!response.value(QStringLiteral("ok")).toBool()) {
    response.insert(QStringLiteral("mode"), QStringLiteral("mcp-tool-call"));
    response.insert(QStringLiteral("toolName"), tool_name);
    return response;
  }
  const auto tool_result = response.value(QStringLiteral("result")).toMap();
  const auto is_error = tool_result.value(QStringLiteral("isError")).toBool();
  QVariantMap report = result_map(!is_error, is_error ? QStringLiteral("MCP tool returned an execution error.") : QString());
  report.insert(QStringLiteral("mode"), QStringLiteral("mcp-tool-call"));
  report.insert(QStringLiteral("toolName"), tool_name);
  report.insert(QStringLiteral("endpoint"), endpoint_);
  report.insert(QStringLiteral("toolResult"), tool_result);
  report.insert(QStringLiteral("structuredContent"), tool_result.value(QStringLiteral("structuredContent")));
  report.insert(QStringLiteral("content"), tool_result.value(QStringLiteral("content")));
  if (is_error) {
    report.insert(QStringLiteral("toolExecutionError"), true);
  }
  return report;
}

void AgentMcpClient::cancelActiveRequest() {
  cancellation_requested_ = true;
}

void AgentMcpClient::resetCancellation() {
  cancellation_requested_ = false;
}

bool AgentMcpClient::cancellationRequested() const {
  return cancellation_requested_ ||
         (cancellation_predicate_ && cancellation_predicate_());
}

QVariantMap AgentMcpClient::postJsonRpc(const QVariantMap& request) {
  if (cancellationRequested()) {
    return cancellation_error(QStringLiteral("MCP request was canceled before it started."));
  }

  QUrl url(endpoint_);
  if (!url.isValid() || url.host().isEmpty() || url.port() <= 0) {
    return http_error(QStringLiteral("MCP endpoint is not a valid localhost HTTP URL."));
  }
  if (url.scheme() != QStringLiteral("http") ||
      (url.host() != QStringLiteral("127.0.0.1") && url.host() != QStringLiteral("localhost"))) {
    return http_error(QStringLiteral("MCP client only connects to local http endpoints."));
  }
  if (bearer_token_.isEmpty()) {
    return http_error(QStringLiteral("MCP capability token is required."));
  }

  QTcpSocket socket;
  socket.connectToHost(url.host(), static_cast<quint16>(url.port()));
  if (!socket.waitForConnected(timeout_ms_)) {
    return http_error(QStringLiteral("Unable to connect to MCP endpoint: %1").arg(socket.errorString()));
  }
  if (QCoreApplication::instance() != nullptr) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }

  const auto body = QJsonDocument(QJsonObject::fromVariantMap(request)).toJson(QJsonDocument::Compact);
  QByteArray http_request;
  http_request.append("POST ");
  http_request.append(url.path().isEmpty() ? QByteArrayLiteral("/mcp") : url.path().toUtf8());
  http_request.append(" HTTP/1.1\r\nHost: ");
  http_request.append(url.host().toUtf8());
  http_request.append(':');
  http_request.append(QByteArray::number(url.port()));
  http_request.append("\r\nAuthorization: Bearer ");
  http_request.append(bearer_token_.toUtf8());
  http_request.append("\r\n");
  http_request.append("Origin: http://127.0.0.1\r\nAccept: application/json, text/event-stream\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ");
  http_request.append(QByteArray::number(body.size()));
  http_request.append("\r\n\r\n");
  http_request.append(body);
  const auto queued_bytes = socket.write(http_request);
  if (queued_bytes != http_request.size()) {
    return http_error(QStringLiteral("Unable to queue the full MCP request for writing."));
  }
  auto flushed = socket.flush();
  if (QCoreApplication::instance() != nullptr) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  QElapsedTimer write_timer;
  write_timer.start();
  while (socket.bytesToWrite() > 0 && write_timer.elapsed() < timeout_ms_) {
    if (cancellationRequested()) {
      socket.abort();
      return cancellation_error(QStringLiteral("MCP request was canceled while writing."));
    }
    flushed = socket.flush() || flushed;
    if (QCoreApplication::instance() != nullptr) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    if (socket.bytesToWrite() > 0) {
      socket.waitForBytesWritten(10);
    }
  }
  if (socket.bytesToWrite() > 0) {
    return http_error(
        QStringLiteral("Timed out writing MCP request: %1 bytes still queued.")
            .arg(socket.bytesToWrite()));
  }
  if (!flushed && socket.state() != QAbstractSocket::ConnectedState) {
    return http_error(QStringLiteral("MCP socket disconnected before the request was written."));
  }

  QByteArray response;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms_) {
    if (cancellationRequested()) {
      socket.abort();
      return cancellation_error(QStringLiteral("MCP request was canceled."));
    }
    if (QCoreApplication::instance() != nullptr) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    if (socket.bytesAvailable() > 0) {
      response.append(socket.readAll());
    }
    const auto header_end = response.indexOf("\r\n\r\n");
    if (header_end >= 0) {
      const auto header = response.left(header_end);
      const auto content_length = content_length_from_header(header);
      if (content_length >= 0 && response.size() >= header_end + 4 + content_length) {
        break;
      }
    }
    if (socket.state() == QAbstractSocket::UnconnectedState) {
      response.append(socket.readAll());
      break;
    }
    socket.waitForReadyRead(25);
    if (socket.bytesAvailable() > 0) {
      response.append(socket.readAll());
    }
  }

  const auto header_end = response.indexOf("\r\n\r\n");
  if (header_end < 0) {
    return http_error(QStringLiteral("MCP endpoint returned an incomplete HTTP response."));
  }
  const auto header = response.left(header_end);
  const auto status_code = status_code_from_header(header);
  const auto content_length = content_length_from_header(header);
  const auto body_bytes =
      content_length >= 0 ? response.mid(header_end + 4, content_length) : response.mid(header_end + 4);
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(body_bytes, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    return http_error(QStringLiteral("MCP endpoint returned invalid JSON."));
  }
  const auto response_object = document.object().toVariantMap();
  if (status_code < 200 || status_code >= 300) {
    auto result = http_error(QStringLiteral("MCP endpoint returned HTTP %1.").arg(status_code));
    result.insert(QStringLiteral("httpStatus"), status_code);
    result.insert(QStringLiteral("response"), response_object);
    return result;
  }
  if (response_object.contains(QStringLiteral("error"))) {
    auto result = http_error(response_object.value(QStringLiteral("error")).toMap().value(QStringLiteral("message")).toString());
    result.insert(QStringLiteral("protocolError"), response_object.value(QStringLiteral("error")));
    result.insert(QStringLiteral("response"), response_object);
    return result;
  }
  auto result = result_map(true);
  result.insert(QStringLiteral("httpStatus"), status_code);
  result.insert(QStringLiteral("response"), response_object);
  result.insert(QStringLiteral("result"), response_object.value(QStringLiteral("result")));
  return result;
}

}  // namespace photo_editor::desktop

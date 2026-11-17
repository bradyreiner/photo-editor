#pragma once

#include "photo_mcp_server.h"
#include "agent_mcp_proof_content.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>

inline bool agent_mcp_discovery_contract() {
  QTemporaryDir artifacts;
  if (!artifacts.isValid()) return false;
  const auto image_path = artifacts.filePath("proof.png");
  QImage rendered(2000, 1000, QImage::Format_RGB32);
  rendered.fill(Qt::darkGreen);
  if (!rendered.save(image_path)) return false;
  QJsonObject image_receipt;
  const auto image = photo_editor::desktop::agent_mcp_proof_image(image_path, &image_receipt);
  const auto decoded = QImage::fromData(QByteArray::fromBase64(image.value("data").toString().toLatin1()));
  if (image.value("type") != "image" || image.value("mimeType") != "image/png" ||
      !image_receipt.value("available").toBool() || decoded.size() != QSize(1024, 512) ||
      image_receipt.value("sourceSha256").toString().size() != 64 ||
      decoded.pixelColor(512, 256) != QColor(Qt::darkGreen)) return false;
  rendered = QImage(4097, 1, QImage::Format_RGB32);
  rendered.fill(Qt::black);
  if (!rendered.save(image_path) ||
      !photo_editor::desktop::agent_mcp_proof_image(image_path, &image_receipt).isEmpty() ||
      image_receipt.value("available").toBool() ||
      !photo_editor::desktop::agent_mcp_proof_image(artifacts.filePath("missing.png"), &image_receipt).isEmpty()) return false;
  using photo_editor::desktop::PhotoMcpServer;
  PhotoMcpServer server(nullptr);
  const QStringList read{"read"};
  const auto rpc = [&](const QString& method, const QJsonObject& params = QJsonObject{}) {
    return server.handleJsonRpc({{"jsonrpc", "2.0"}, {"id", 7}, {"method", method}, {"params", params}}, read);
  };
  const auto initialized = rpc("initialize", {{"protocolVersion", "2025-11-25"},
      {"clientInfo", QJsonObject{{"name", "independent-client"}, {"version", "1"}}}, {"capabilities", QJsonObject{}}})
      .value("result").toObject();
  if (initialized.value("protocolVersion") != "2025-11-25" ||
      !initialized.value("capabilities").toObject().contains("resources") ||
      !initialized.value("capabilities").toObject().contains("prompts") ||
      !initialized.value("instructions").toString().contains("app.agent_guide")) return false;
  const auto fallback = rpc("initialize", {{"protocolVersion", "future-version"},
      {"clientInfo", QJsonObject{}}, {"capabilities", QJsonObject{}}}).value("result").toObject();
  if (fallback.value("protocolVersion") != "2025-06-18" ||
      rpc("initialize").value("error").toObject().value("code").toInt() != -32602 ||
      !rpc("ping").value("result").isObject()) return false;
  for (const auto* method : {"notifications/initialized", "notifications/cancelled", "notifications/custom"}) {
    if (!server.handleJsonRpc({{"jsonrpc", "2.0"}, {"method", method}}, read).isEmpty()) return false;
  }
  QSet<QString> names;
  for (const auto& value : server.toolsListResult(read).value("tools").toArray())
    names.insert(value.toObject().value("name").toString());
  if (!names.contains("app.agent_guide") || names.contains("app.invoke_command")) return false;
  const auto resources = rpc("resources/list").value("result").toObject().value("resources").toArray();
  if (resources.size() != 5) return false;
  for (const auto& value : resources) {
    const auto uri = value.toObject().value("uri").toString();
    const auto response = rpc("resources/read", {{"uri", uri}}).value("result").toObject();
    const auto body = response.value("contents").toArray().first().toObject();
    const auto guide = QJsonDocument::fromJson(body.value("text").toString().toUtf8()).object();
    if (body.value("uri") != uri || guide.value("contract") != "looksy-agent-guide-v1" ||
        guide.value("capabilities").toArray() != QJsonArray{"read"} ||
        !guide.value("guidance").toString().contains("Creative workflow")) return false;
    for (const auto& route : guide.value("tools").toArray()) {
      if (!names.contains(route.toObject().value("name").toString()) ||
          route.toObject().value("purpose").toString().isEmpty()) return false;
    }
    const auto workflow = guide.value("workflow").toString();
    for (const auto& option : guide.value("decisionOptions").toArray()) {
      for (const auto& name : option.toObject().value("toolNames").toArray())
        if (!names.contains(name.toString())) return false;
    }
    if (guide.value("decisionOptions").toArray().isEmpty()) return false;
    if (server.callTool("app.agent_guide", {{"workflow", workflow}}, read).value("structuredContent").toObject() != guide)
      return false;
  }
  const auto prompts = rpc("prompts/list").value("result").toObject().value("prompts").toArray();
  if (prompts.size() != 1 || prompts.first().toObject().value("name") != "create-with-looksy") return false;
  const auto prompt = rpc("prompts/get", {{"name", "create-with-looksy"},
      {"arguments", QJsonObject{{"brief", "Create an editable hiking poster"}, {"workflow", "design"}}}})
      .value("result").toObject().value("messages").toArray();
  if (prompt.size() != 1 || !prompt.first().toObject().value("content").toObject().value("text")
      .toString().contains("Create an editable hiking poster")) return false;
  for (const auto& arguments : {QJsonObject{{"workflow", "unknown"}}, QJsonObject{{"workflow", 1}},
      QJsonObject{{"workflow", "photo"}, {"unexpected", true}}}) {
    if (!server.callTool("app.agent_guide", arguments, read).value("isError").toBool()) return false;
  }
  for (const auto* uri : {"file:///etc/passwd", "looksy://agent/workflows/../photo", "looksy://agent/workflows/unknown"}) {
    if (rpc("resources/read", {{"uri", uri}}).value("error").toObject().value("code").toInt() != -32002) return false;
  }
  if (!server.callTool("app.agent_guide", {}, {}).value("isError").toBool() ||
      !server.handleJsonRpc({{"jsonrpc", "2.0"}, {"id", 7}, {"method", "resources/list"}}, {})
          .value("result").toObject().value("resources").toArray().isEmpty() ||
      rpc("prompts/get", {{"name", "create-with-looksy"}, {"arguments", QJsonObject{{"brief", " "}}}})
          .value("error").toObject().value("code").toInt() != -32602) return false;
  const auto all = server.callTool("app.agent_guide", {{"workflow", "design"}}).value("structuredContent").toObject();
  bool mutation_route = false;
  for (const auto& route : all.value("tools").toArray())
    mutation_route |= route.toObject().value("name") == "app.invoke_command";
  return mutation_route;
}

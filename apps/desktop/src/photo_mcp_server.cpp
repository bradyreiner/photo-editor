#include "photo_mcp_server.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <algorithm>

#include "photo_automation_bridge.h"

namespace photo_editor::desktop {
namespace {

struct PhotoMcpTool final {
  const char* name;
  const char* title;
  const char* description;
  const char* automation_command;
  QJsonObject input_schema;
  bool read_only = false;
  bool destructive = false;
  bool requires_confirmation = false;
};

QJsonObject json_rpc_error(const QJsonValue& id, int code, const QString& message) {
  QJsonObject error;
  error.insert(QStringLiteral("code"), code);
  error.insert(QStringLiteral("message"), message);
  QJsonObject response;
  response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
  if (!id.isUndefined()) {
    response.insert(QStringLiteral("id"), id);
  }
  response.insert(QStringLiteral("error"), error);
  return response;
}

QJsonObject json_rpc_result(const QJsonValue& id, const QJsonObject& result) {
  QJsonObject response;
  response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
  if (!id.isUndefined()) {
    response.insert(QStringLiteral("id"), id);
  }
  response.insert(QStringLiteral("result"), result);
  return response;
}

QJsonObject empty_object_schema() {
  return QJsonObject{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"), QJsonObject{}},
      {QStringLiteral("additionalProperties"), false},
  };
}

QJsonObject string_property(const QString& description = {}) {
  QJsonObject property{{QStringLiteral("type"), QStringLiteral("string")}};
  if (!description.isEmpty()) {
    property.insert(QStringLiteral("description"), description);
  }
  return property;
}

QJsonObject boolean_property(const QString& description = {}) {
  QJsonObject property{{QStringLiteral("type"), QStringLiteral("boolean")}};
  if (!description.isEmpty()) {
    property.insert(QStringLiteral("description"), description);
  }
  return property;
}

QJsonObject number_property(const QString& description = {}) {
  QJsonObject property{{QStringLiteral("type"), QStringLiteral("number")}};
  if (!description.isEmpty()) {
    property.insert(QStringLiteral("description"), description);
  }
  return property;
}

QJsonObject generic_object_property(const QString& description = {}) {
  QJsonObject property{{QStringLiteral("type"), QStringLiteral("object")}};
  if (!description.isEmpty()) {
    property.insert(QStringLiteral("description"), description);
  }
  return property;
}

QJsonObject object_schema(
    const QJsonObject& properties,
    const QJsonArray& required = {},
    bool additional_properties = false) {
  QJsonObject schema{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"), properties},
      {QStringLiteral("additionalProperties"), additional_properties},
  };
  if (!required.isEmpty()) {
    schema.insert(QStringLiteral("required"), required);
  }
  return schema;
}

QJsonObject metrics_properties(bool include_confirmed) {
  QJsonObject properties{
      {QStringLiteral("preset"), string_property(QStringLiteral("Export preset name."))},
      {QStringLiteral("destinationDirectory"), string_property(QStringLiteral("Export destination directory."))},
      {QStringLiteral("histogramSampleCount"), number_property()},
      {QStringLiteral("shadowClipCount"), number_property()},
      {QStringLiteral("highlightClipCount"), number_property()},
      {QStringLiteral("rawClipSampleCount"), number_property()},
      {QStringLiteral("rawShadowClipCount"), number_property()},
      {QStringLiteral("rawHighlightClipCount"), number_property()},
  };
  if (include_confirmed) {
    properties.insert(
        QStringLiteral("confirmed"),
        boolean_property(QStringLiteral("Must be true before queueing export jobs.")));
  }
  return properties;
}

QVector<PhotoMcpTool> photo_mcp_tools() {
  return {
      {
          "photo.live_status",
          "Photo Live Status",
          "Read the current app, selection, agent, and draft status.",
          "photo_live_status",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.list_models",
          "List Agent Models",
          "Read available local agent models and the selected model id.",
          "photo_list_models",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.selected_assets",
          "Selected Assets",
          "Read selected asset ids and selected asset count.",
          "photo_selected_assets",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.read_asset",
          "Read Asset",
          "Read metadata for the selected asset or a requested asset id.",
          "photo_read_asset",
          object_schema(QJsonObject{{QStringLiteral("assetId"), string_property(QStringLiteral("Optional asset id."))}}),
          true,
          false,
          false,
      },
      {
          "photo.read_recipe",
          "Read Recipe",
          "Read the selected asset recipe JSON.",
          "photo_read_recipe",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.get_recipe_patch_schema",
          "Recipe Patch Schema",
          "Read the validation-backed schema for agent recipe patch tools.",
          "photo_get_recipe_patch_schema",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.get_style_rulebook",
          "Style Rulebook",
          "Read the selected style rulebook, constraints, allowed tools, and batch policy.",
          "photo_get_style_rulebook",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.validate_recipe_patch",
          "Validate Recipe Patch",
          "Validate a recipe patch before applying it.",
          "photo_validate_recipe_patch",
          object_schema(
              QJsonObject{{QStringLiteral("patch"), generic_object_property(QStringLiteral("Recipe patch object."))}},
              QJsonArray{QStringLiteral("patch")}),
          true,
          false,
          false,
      },
      {
          "photo.render_current_proof",
          "Render Current Proof",
          "Render a proof image for the current selected asset.",
          "photo_render_proof",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.validate_edit",
          "Validate Current Edit",
          "Render and validate the current selected edit state.",
          "photo_validate_edit",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.validate_batch",
          "Validate Batch",
          "Validate selected assets without mutating recipes.",
          "photo_validate_batch",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.export_preflight",
          "Export Preflight",
          "Check export readiness before queueing jobs.",
          "photo_export_preflight",
          object_schema(metrics_properties(false)),
          true,
          false,
          false,
      },
      {
          "photo.export_batch",
          "Export Batch",
          "Queue export jobs for selected assets after explicit confirmation.",
          "photo_export_batch",
          object_schema(metrics_properties(true), QJsonArray{QStringLiteral("confirmed")}),
          false,
          true,
          true,
      },
      {
          "photo.apply_recipe_patch_draft",
          "Apply Recipe Patch Draft",
          "Apply a validated recipe patch as a reversible draft.",
          "photo_apply_recipe_patch_draft",
          object_schema(
              QJsonObject{
                  {QStringLiteral("patch"), generic_object_property(QStringLiteral("Recipe patch object."))},
                  {QStringLiteral("confirmed"), boolean_property(QStringLiteral("Required for retouch patches."))},
              },
              QJsonArray{QStringLiteral("patch")}),
          false,
          true,
          false,
      },
      {
          "photo.plan_bulk_edit",
          "Plan Bulk Edit",
          "Plan a selected-assets edit without mutating recipes.",
          "photo_plan_bulk_edit",
          object_schema(QJsonObject{{QStringLiteral("instructions"), string_property()}}),
          true,
          false,
          false,
      },
      {
          "photo.apply_bulk_draft",
          "Apply Bulk Draft",
          "Apply a reversible draft to the selected asset set.",
          "photo_apply_bulk_draft",
          object_schema(QJsonObject{{QStringLiteral("instructions"), string_property()}}),
          false,
          true,
          false,
      },
      {
          "photo.create_mask",
          "Create Mask",
          "Create a supported mask and apply initial local adjustments as a draft.",
          "photo_create_mask",
          object_schema(
              QJsonObject{
                  {QStringLiteral("type"), string_property(QStringLiteral("linear or radial"))},
                  {QStringLiteral("exposure"), number_property()},
                  {QStringLiteral("temperature"), number_property()},
                  {QStringLiteral("feather"), number_property()},
                  {QStringLiteral("position"), number_property()},
                  {QStringLiteral("rotation"), number_property()},
                  {QStringLiteral("centerX"), number_property()},
                  {QStringLiteral("centerY"), number_property()},
                  {QStringLiteral("radiusX"), number_property()},
                  {QStringLiteral("radiusY"), number_property()},
              },
              QJsonArray{QStringLiteral("type")}),
          false,
          true,
          false,
      },
      {
          "photo.apply_mask_adjustment",
          "Apply Mask Adjustment",
          "Apply a local mask adjustment as a reversible draft.",
          "photo_apply_mask_adjustment",
          object_schema(
              QJsonObject{
                  {QStringLiteral("type"), string_property(QStringLiteral("linear or radial"))},
                  {QStringLiteral("exposure"), number_property()},
                  {QStringLiteral("temperature"), number_property()},
                  {QStringLiteral("feather"), number_property()},
                  {QStringLiteral("position"), number_property()},
                  {QStringLiteral("rotation"), number_property()},
                  {QStringLiteral("centerX"), number_property()},
                  {QStringLiteral("centerY"), number_property()},
                  {QStringLiteral("radiusX"), number_property()},
                  {QStringLiteral("radiusY"), number_property()},
              },
              QJsonArray{QStringLiteral("type")}),
          false,
          true,
          false,
      },
      {
          "photo.run_draft_on_selected",
          "Run Draft On Selected",
          "Plan and apply an agent draft for the selected assets.",
          "photo_run_draft_on_selected",
          object_schema(QJsonObject{{QStringLiteral("instructions"), string_property()}}),
          false,
          true,
          false,
      },
      {
          "photo.commit_agent_draft",
          "Commit Agent Draft",
          "Commit the active reversible draft after explicit confirmation.",
          "photo_commit_agent_draft",
          object_schema(
              QJsonObject{{QStringLiteral("confirmed"), boolean_property(QStringLiteral("Must be true."))}},
              QJsonArray{QStringLiteral("confirmed")}),
          false,
          true,
          true,
      },
      {
          "photo.cancel_agent_run",
          "Cancel Agent Run",
          "Cancel an active agent run.",
          "photo_cancel_agent_run",
          object_schema(QJsonObject{{QStringLiteral("reason"), string_property()}}),
          false,
          false,
          false,
      },
      {
          "photo.rollback_agent_draft",
          "Rollback Agent Draft",
          "Rollback the active reversible agent draft.",
          "photo_rollback_agent_draft",
          empty_object_schema(),
          false,
          true,
          false,
      },
  };
}

const PhotoMcpTool* find_tool(const QString& name) {
  static const auto tools = photo_mcp_tools();
  const auto iterator = std::find_if(
      tools.cbegin(),
      tools.cend(),
      [&](const PhotoMcpTool& tool) {
        return name == QString::fromLatin1(tool.name);
      });
  return iterator == tools.cend() ? nullptr : &(*iterator);
}

QJsonObject tool_annotations(const PhotoMcpTool& tool) {
  return QJsonObject{
      {QStringLiteral("readOnlyHint"), tool.read_only},
      {QStringLiteral("destructiveHint"), tool.destructive},
      {QStringLiteral("requiresConfirmation"), tool.requires_confirmation},
      {QStringLiteral("localOnly"), true},
  };
}

QJsonObject mcp_tool_object(const PhotoMcpTool& tool) {
  return QJsonObject{
      {QStringLiteral("name"), QString::fromLatin1(tool.name)},
      {QStringLiteral("title"), QString::fromLatin1(tool.title)},
      {QStringLiteral("description"), QString::fromLatin1(tool.description)},
      {QStringLiteral("inputSchema"), tool.input_schema},
      {QStringLiteral("annotations"), tool_annotations(tool)},
  };
}

QString compact_json(const QJsonObject& object) {
  return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

}  // namespace

PhotoMcpServer::PhotoMcpServer(PhotoAutomationBridge* automation_bridge, QObject* parent)
    : QObject(parent), automation_bridge_(automation_bridge) {}

QJsonObject PhotoMcpServer::toolsListResult() const {
  QJsonArray tools;
  for (const auto& tool : photo_mcp_tools()) {
    tools.append(mcp_tool_object(tool));
  }
  return QJsonObject{{QStringLiteral("tools"), tools}};
}

QJsonObject PhotoMcpServer::callTool(const QString& name, const QJsonObject& arguments) const {
  const auto* tool = find_tool(name.trimmed());
  if (tool == nullptr) {
    return QJsonObject{
        {QStringLiteral("content"),
         QJsonArray{QJsonObject{
             {QStringLiteral("type"), QStringLiteral("text")},
             {QStringLiteral("text"), QStringLiteral("Unknown photo tool: %1").arg(name)},
         }}},
        {QStringLiteral("isError"), true},
    };
  }
  if (automation_bridge_ == nullptr) {
    return QJsonObject{
        {QStringLiteral("content"),
         QJsonArray{QJsonObject{
             {QStringLiteral("type"), QStringLiteral("text")},
             {QStringLiteral("text"), QStringLiteral("Photo automation bridge is not connected.")},
         }}},
        {QStringLiteral("isError"), true},
    };
  }

  const auto automation_response = automation_bridge_->handleJsonRequest(QJsonObject{
      {QStringLiteral("token"), automation_bridge_->token()},
      {QStringLiteral("command"), QString::fromLatin1(tool->automation_command)},
      {QStringLiteral("args"), arguments},
  });
  const auto ok = automation_response.value(QStringLiteral("ok")).toBool(false);
  const auto structured_content = ok
                                      ? automation_response.value(QStringLiteral("result")).toObject()
                                      : QJsonObject{
                                            {QStringLiteral("error"),
                                             automation_response.value(QStringLiteral("error")).toString()}};
  const auto text = ok
                        ? compact_json(structured_content)
                        : automation_response.value(QStringLiteral("error")).toString();
  QJsonObject result{
      {QStringLiteral("content"),
       QJsonArray{QJsonObject{
           {QStringLiteral("type"), QStringLiteral("text")},
           {QStringLiteral("text"), text},
       }}},
      {QStringLiteral("structuredContent"), structured_content},
  };
  if (!ok) {
    result.insert(QStringLiteral("isError"), true);
  }
  return result;
}

QJsonObject PhotoMcpServer::handleJsonRpc(const QJsonObject& request) const {
  const auto id = request.value(QStringLiteral("id"));
  if (request.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0")) {
    return json_rpc_error(id, -32600, QStringLiteral("MCP requests must use JSON-RPC 2.0."));
  }
  const auto method = request.value(QStringLiteral("method")).toString();
  if (method == QStringLiteral("tools/list")) {
    const auto params = request.value(QStringLiteral("params"));
    if (!params.isUndefined() && !params.isObject()) {
      return json_rpc_error(id, -32602, QStringLiteral("tools/list params must be an object when provided."));
    }
    return json_rpc_result(id, toolsListResult());
  }
  if (method == QStringLiteral("tools/call")) {
    const auto params = request.value(QStringLiteral("params")).toObject();
    const auto name = params.value(QStringLiteral("name")).toString().trimmed();
    const auto arguments_value = params.value(QStringLiteral("arguments"));
    if (name.isEmpty() || (!arguments_value.isUndefined() && !arguments_value.isObject())) {
      return json_rpc_error(
          id,
          -32602,
          QStringLiteral("tools/call requires params.name and optional object params.arguments."));
    }
    return json_rpc_result(id, callTool(name, arguments_value.toObject()));
  }
  return json_rpc_error(id, -32601, QStringLiteral("Unsupported MCP method: %1").arg(method));
}

}  // namespace photo_editor::desktop

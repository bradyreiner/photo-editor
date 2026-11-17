#include "agent_decision_shadow.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

using namespace photo_editor::desktop;

namespace {
bool check(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  // Export exactly the production frame format for an offline, label-blind
  // model comparison. Labels stay outside the frame and are never model input.
  if (app.arguments().size() == 4 && app.arguments()[1] == "--export-cases") {
    QFile source(app.arguments()[2]);
    if (!source.open(QIODevice::ReadOnly)) return 1;
    const auto cases = QJsonDocument::fromJson(source.readAll()).array();
    QJsonArray output;
    for (const auto& value : cases) {
      auto row = value.toObject();
      const auto tools = row.take("tools").toArray();
      const auto state = row.take("state").toObject();
      const auto binding = QString::fromLatin1(QCryptographicHash::hash(
          QJsonDocument(QJsonObject{{"state", state}, {"tools", tools}})
              .toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
      const auto frame = agent_decision_frame(state, agent_decision_candidates(tools,
          !state.value("toolPlanRequired").toBool()), binding);
      if (frame.isEmpty()) return 2;
      row.insert("frame", frame);
      row.insert("responseSchema", agent_decision_response_schema(frame));
      output.append(row);
    }
    QFile destination(app.arguments()[3]);
    if (!destination.open(QIODevice::WriteOnly)) return 3;
    return destination.write(QJsonDocument(output).toJson(QJsonDocument::Indented)) > 0 ? 0 : 4;
  }
  AgentLocalModelPlanRequest request;
  request.user_instructions = "Repair the existing title without changing the photo.";
  request.asset = {{"assetId", "asset-a"}, {"revision", "r1"}, {"sourcePath", "/private/photo.jpg"}};
  request.allow_tool_call_plan = true;
  request.require_tool_call_plan = true;
  request.available_tool_schemas = {QVariantMap{{"name", "photo.inspect_composition"}},
      QVariantMap{{"name", "photo.edit_composition"}}};
  const auto frame = agent_plan_decision_frame(request, {});
  const QJsonObject response{{"requestDigest", frame.value("requestDigest")}, {"choice", "inspect_state"}};
  if (!check(!frame.isEmpty() && agent_decision_response_valid(frame, response),
          "A candidate response bound to the current frame should validate.")) return 1;
  if (!check(!QJsonDocument(frame).toJson().contains("/private/photo.jpg"),
          "Named path fields must be omitted from decision state.")) return 1;
  const auto schema = agent_decision_response_schema(frame);
  const auto ids = schema.value("properties").toObject().value("choice").toObject().value("enum").toArray();
  if (!check(!ids.contains("finish_plan") && !ids.contains("deliver_output") &&
          ids.contains("insufficient_evidence"), "Candidates must reflect supplied tools and required tool work.")) return 1;
  for (const auto* field : {"asset", "brief", "tools", "history", "recipe", "style", "memory"}) {
    auto changed = request;
    if (QString(field) == "asset") changed.asset.insert("revision", "r2");
    if (QString(field) == "brief") changed.user_instructions += " Keep the title size.";
    if (QString(field) == "tools") changed.available_tool_schemas[0] = QVariantMap{{"name", "photo.read_asset"}};
    if (QString(field) == "history") changed.prior_tool_results.append(QVariantMap{{"completed", true}});
    if (QString(field) == "recipe") changed.current_adjustment_patch_json = "changed";
    if (QString(field) == "style") changed.style.insert("constraint", "preserve framing");
    if (QString(field) == "memory") changed.memory_packet.insert("projectNote", "changed");
    if (!check(!agent_decision_response_valid(agent_plan_decision_frame(changed, {}), response),
            "Changed inputs must invalidate an older decision, even when projected state looks identical.")) return 1;
  }
  auto invalid = response;
  invalid.insert("choice", "invented_action");
  if (!check(!agent_decision_response_valid(frame, invalid), "Invented candidates must be rejected.")) return 1;
  invalid = response; invalid.insert("arguments", QJsonObject{{"deleteAll", true}});
  if (!check(!agent_decision_response_valid(frame, invalid), "The choice channel must reject executable arguments.")) return 1;
  auto tampered = frame; tampered.insert("state", QJsonObject{{"changed", true}});
  if (!check(!agent_decision_response_valid(tampered, response), "Tampered frames must fail digest validation.")) return 1;
  auto duplicates = frame.value("candidates").toArray(); duplicates.append(duplicates.first());
  if (!check(agent_decision_frame({{"brief", "x"}}, duplicates, QString(64, 'a')).isEmpty(),
          "Duplicate option IDs must fail frame construction.")) return 1;
  if (!check(agent_decision_tool_family("photo.get_export_job") == "inspect_state" &&
          agent_decision_tool_family("photo.read_asset") == "inspect_state" &&
          agent_decision_tool_family("photo.export_preflight") == "review_proof" &&
          agent_decision_tool_family("photo.export_batch") == "deliver_output",
          "Read-only export status, preflight and export submission have distinct purposes.")) return 1;
  QJsonArray observations;
  for (int i = 0; i < 25; ++i) observations.append(QJsonObject{{"sequence", i}, {"notes", QString(3000, 'x')}});
  const auto bounded = agent_plan_decision_frame(request, observations);
  const auto bounded_state = bounded.value("state").toObject();
  if (!check(!bounded.isEmpty() && bounded_state.value("projectionOmittedFields").toBool() &&
          bounded_state.value("observations").toArray().last().toObject().value("sequence").toInt() == 24,
          "Bounded state must disclose omissions and retain the most recent receipt.")) return 1;
  QTemporaryDir dir;
  AgentLocalModelPlanResult result;
  result.ok = true; result.decision = "tool_call_plan"; result.model_id = "test";
  result.tool_call_plan = {QVariantMap{{"name", "photo.inspect_composition"}}};
  record_agent_decision_shadow(request, &result, {}, dir.path());
  const auto audit = result.prompt_audit.value("decisionShadow").toMap();
  if (!check(audit.value("captured").toBool() && audit.value("extraInferenceCalls").toInt() == 0 &&
          result.ok && result.tool_call_plan.size() == 1 && QDir(dir.path()).entryList({"*.json"}, QDir::Files).size() == 1,
          "Shadow capture must record the original plan without modifying it or adding inference.")) return 1;
  QFile saved(dir.filePath(QDir(dir.path()).entryList({"*.json"}, QDir::Files).first()));
  if (!check(saved.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(saved.readAll()).object()
          .value("observation").toObject().value("labelStatus").toString().contains("not ground truth"),
          "Unreviewed planner choices must never be written as successful training labels.")) return 1;
  if (!check((saved.permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther)) == 0,
          "Local captures must not grant group or other read/write access.")) return 1;
  AgentLocalModelPlanResult disabled_result;
  record_agent_decision_shadow(request, &disabled_result, {}, {});
  if (!check(disabled_result.prompt_audit.isEmpty(), "Disabled capture must have no output or audit side effect.")) return 1;
  QFile blocked(dir.filePath("not-a-directory"));
  if (!blocked.open(QIODevice::WriteOnly)) return 1;
  blocked.close();
  record_agent_decision_shadow(request, &result, {}, blocked.fileName());
  if (!check(result.ok && !result.prompt_audit.value("decisionShadow").toMap().value("captured").toBool(),
          "Capture failure must not fail or change the editing plan.")) return 1;
  std::cout << "Decision frame, stale binding, bounded state and advisory capture contracts passed.\n";
  return 0;
}

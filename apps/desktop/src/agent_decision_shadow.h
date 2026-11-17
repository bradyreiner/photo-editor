#pragma once

#include "agent_decision_frame.h"
#include "agent_local_model.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>

namespace photo_editor::desktop {

inline QJsonValue agent_decision_state_projection(const QJsonValue& value, bool* omitted, int depth = 0) {
  if (depth > 7) { *omitted = true; return QJsonValue(QJsonValue::Null); }
  if (value.isString()) {
    const auto text = value.toString();
    if (text.size() > 1536) *omitted = true;
    return text.left(1536);
  }
  if (value.isArray()) {
    const auto source = value.toArray();
    if (source.size() > 12) *omitted = true;
    QJsonArray result;
    for (qsizetype i = 0; i < std::min<qsizetype>(source.size(), 12); ++i)
      result.append(agent_decision_state_projection(source[i], omitted, depth + 1));
    return result;
  }
  if (value.isObject()) {
    QJsonObject result;
    const auto object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
      const auto key = it.key().toLower();
      if (key.contains(QStringLiteral("path")) || key.contains(QStringLiteral("base64")) ||
          key.contains(QStringLiteral("token")) || key.contains(QStringLiteral("endpoint")) ||
          key.contains(QStringLiteral("url")) || key.contains(QStringLiteral("imagedata")) ||
          key.contains(QStringLiteral("imagebytes")) || key == QStringLiteral("data") ||
          key == QStringLiteral("pixelvalues") || result.size() >= 32) {
        *omitted = true;
        continue;
      }
      result.insert(it.key(), agent_decision_state_projection(it.value(), omitted, depth + 1));
    }
    return result;
  }
  return value;
}

inline QJsonObject agent_plan_decision_frame(
    const AgentLocalModelPlanRequest& request, const QJsonArray& compact_observations) {
  const QJsonObject binding{
      {"asset", QJsonObject::fromVariantMap(request.asset)},
      {"style", QJsonObject::fromVariantMap(request.style)},
      {"recipe", request.current_adjustment_patch_json},
      {"brief", request.user_instructions}, {"phase", request.strategy_phase},
      {"tools", QJsonArray::fromVariantList(request.available_tool_schemas)},
      {"history", QJsonArray::fromVariantList(request.prior_tool_results)},
      {"memory", QJsonObject::fromVariantMap(request.memory_packet)},
      {"toolsAllowed", request.allow_tool_call_plan},
      {"toolPlanRequired", request.require_tool_call_plan},
  };
  const auto binding_hash = QString::fromLatin1(QCryptographicHash::hash(
      QJsonDocument(binding).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
  bool omitted = compact_observations.size() > 12;
  QJsonArray recent;
  for (qsizetype i = std::max<qsizetype>(0, compact_observations.size() - 12);
       i < compact_observations.size(); ++i) recent.append(compact_observations[i]);
  auto observations = agent_decision_state_projection(recent, &omitted).toArray();
  const auto asset = agent_decision_state_projection(
      QJsonObject::fromVariantMap(request.asset), &omitted);
  if (request.user_instructions.size() > 4096) omitted = true;
  QJsonObject state{
      {"brief", request.user_instructions.left(4096)},
      {"phase", request.strategy_phase}, {"asset", asset},
      {"toolsAllowed", request.allow_tool_call_plan},
      {"toolPlanRequired", request.require_tool_call_plan},
      {"evidenceScope", "Text state only. No rendered pixels, full style, memory or recipe are included. Tool groups describe supplied schemas, not live execution admission. Missing fields are unknown."},
  };
  // Preserve the most recent evidence when the bounded projection is too large.
  while (QJsonDocument(observations).toJson(QJsonDocument::Compact).size() > 12000 &&
      observations.size() > 1) { observations.removeFirst(); omitted = true; }
  if (QJsonDocument(observations).toJson(QJsonDocument::Compact).size() > 12000) {
    observations = {}; omitted = true;
  }
  state.insert(QStringLiteral("observations"), observations);
  state.insert(QStringLiteral("projectionOmittedFields"), omitted);
  return agent_decision_frame(state, agent_decision_candidates(
      request.allow_tool_call_plan ? QJsonArray::fromVariantList(request.available_tool_schemas) : QJsonArray{},
      !request.require_tool_call_plan), binding_hash);
}

// Explicitly enabled local capture. It adds no inference, filters no tools and
// cannot change the planner's result or native execution authority.
inline void record_agent_decision_shadow(
    const AgentLocalModelPlanRequest& request, AgentLocalModelPlanResult* result,
    const QJsonArray& observations, const QString& directory) {
  if (directory.isEmpty()) return;
  const auto frame = agent_plan_decision_frame(request, observations);
  QVariantMap audit{{"contract", "looksy-agent-decision-shadow-v1"},
      {"advisoryOnly", true}, {"extraInferenceCalls", 0}, {"captured", false}};
  if (frame.isEmpty()) {
    audit.insert(QStringLiteral("errorCode"), QStringLiteral("frame_exceeds_contract"));
    result->prompt_audit.insert(QStringLiteral("decisionShadow"), audit);
    return;
  }
  QStringList observed;
  if (result->ok) {
    if (result->decision == QStringLiteral("tool_call_plan")) {
      for (const auto& value : result->tool_call_plan) {
        const auto family = agent_decision_tool_family(value.toMap().value(QStringLiteral("name")).toString());
        if (!observed.contains(family)) observed.append(family);
      }
    } else if (result->decision == QStringLiteral("needs_user_input")) observed.append(QStringLiteral("ask_user"));
    else if (result->decision == QStringLiteral("cannot_complete_safely")) observed.append(QStringLiteral("stop_safely"));
    else if (result->decision == QStringLiteral("edit_intent")) observed.append(QStringLiteral("finish_plan"));
  }
  const QJsonObject record{{"frame", frame}, {"observation", QJsonObject{
      {"plannerModelId", result->model_id}, {"plannerOk", result->ok},
      {"decision", result->decision}, {"chosenFamilies", QJsonArray::fromStringList(observed)},
      {"labelStatus", "unreviewed-model-behavior; not ground truth"}}}};
  QDir dir(directory);
  const auto filename = QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".json");
  QSaveFile file(dir.filePath(filename));
  const auto bytes = QJsonDocument(record).toJson(QJsonDocument::Indented);
  const auto saved = dir.mkpath(QStringLiteral(".")) && file.open(QIODevice::WriteOnly) &&
      file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) && file.write(bytes) == bytes.size() && file.commit();
  audit.insert(QStringLiteral("captured"), saved);
  audit.insert(QStringLiteral("requestDigest"), frame.value("requestDigest").toString());
  audit.insert(QStringLiteral("observedFamilies"), observed);
  if (!saved) audit.insert(QStringLiteral("errorCode"), QStringLiteral("capture_write_failed"));
  result->prompt_audit.insert(QStringLiteral("decisionShadow"), audit);
}

}  // namespace photo_editor::desktop

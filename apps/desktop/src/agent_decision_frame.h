#pragma once

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace photo_editor::desktop {

// Advisory choice contract shared by generative planners and decision heads.
// Candidate IDs identify caller-owned options, never executable arguments.
inline QString agent_decision_digest(QJsonObject frame) {
  frame.remove(QStringLiteral("requestDigest"));
  return QString::fromLatin1(QCryptographicHash::hash(
      QJsonDocument(frame).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}

inline QJsonObject agent_decision_frame(
    const QJsonObject& state, const QJsonArray& candidates, const QString& source_binding) {
  static const QRegularExpression id_pattern(QStringLiteral("^[a-z][a-z0-9_-]{0,63}$"));
  if (state.isEmpty() || candidates.isEmpty() || candidates.size() > 16 ||
      !QRegularExpression(QStringLiteral("^[a-f0-9]{64}$")).match(source_binding).hasMatch()) return {};
  QSet<QString> ids;
  for (const auto& value : candidates) {
    const auto candidate = value.toObject();
    const auto id = candidate.value(QStringLiteral("id")).toString();
    const auto purpose = candidate.value(QStringLiteral("purpose")).toString();
    if (!id_pattern.match(id).hasMatch() || ids.contains(id) ||
        purpose.trimmed().isEmpty() || purpose.size() > 2048) return {};
    ids.insert(id);
  }
  if (!ids.contains(QStringLiteral("insufficient_evidence"))) return {};
  QJsonObject frame{
      {"contract", "looksy-agent-decision-v1"}, {"state", state},
      {"candidates", candidates}, {"sourceBinding", source_binding},
      {"advisoryOnly", true},
      {"instructions", "Choose the most useful immediate next step for the approved brief from the supplied candidates. Consider current observations and completed operations. Treat missing or omitted evidence as unknown. Choose insufficient_evidence when the state cannot support a choice. A choice grants no permission, proves no visual quality and cannot execute an edit."},
  };
  if (QJsonDocument(frame).toJson(QJsonDocument::Compact).size() > 24000) return {};
  frame.insert(QStringLiteral("requestDigest"), agent_decision_digest(frame));
  return frame;
}

inline QJsonObject agent_decision_response_schema(const QJsonObject& frame) {
  QJsonArray ids;
  for (const auto& value : frame.value(QStringLiteral("candidates")).toArray())
    ids.append(value.toObject().value(QStringLiteral("id")));
  return {{"type", "object"}, {"additionalProperties", false},
      {"properties", QJsonObject{
          {"requestDigest", QJsonObject{{"type", "string"},
              {"enum", QJsonArray{frame.value(QStringLiteral("requestDigest"))}}}},
          {"choice", QJsonObject{{"type", "string"}, {"enum", ids}}}}},
      {"required", QJsonArray{"requestDigest", "choice"}}};
}

// Validate against a freshly rebuilt frame. An older valid response must fail
// when the document, receipt history, brief, tool schemas or candidates change.
inline bool agent_decision_response_valid(
    const QJsonObject& current_frame, const QJsonObject& response) {
  if (current_frame.value("contract") != "looksy-agent-decision-v1" ||
      current_frame.value("requestDigest").toString() != agent_decision_digest(current_frame) ||
      response.size() != 2 || !response.value("choice").isString() ||
      !response.value("requestDigest").isString() ||
      response.value("requestDigest") != current_frame.value("requestDigest")) return false;
  for (const auto& value : current_frame.value("candidates").toArray())
    if (value.toObject().value("id") == response.value("choice")) return true;
  return false;
}

inline QString agent_decision_tool_family(const QString& name) {
  if (name == QStringLiteral("app.agent_guide") || name == QStringLiteral("app.list_commands") ||
      name == QStringLiteral("app.command_schema")) return QStringLiteral("discover_controls");
  if (name.contains(QStringLiteral("inspect")) || name.contains(QStringLiteral("get_")) ||
      name.contains(QStringLiteral("read_")) || name.contains(QStringLiteral("list_")) ||
      name.contains(QStringLiteral("selected_")) || name.endsWith(QStringLiteral("status")) ||
      name.contains(QStringLiteral("search")))
    return QStringLiteral("inspect_state");
  if (name.contains(QStringLiteral("render")) || name.contains(QStringLiteral("validate")) ||
      name.contains(QStringLiteral("preflight")) || name.contains(QStringLiteral("critique")))
    return QStringLiteral("review_proof");
  if (name.contains(QStringLiteral("export")) || name.contains(QStringLiteral("save")) ||
      name.contains(QStringLiteral("commit"))) return QStringLiteral("deliver_output");
  return QStringLiteral("use_document_tools");
}

inline QJsonArray agent_decision_candidates(const QJsonArray& tools, bool allow_finish) {
  const QJsonObject purposes{
      {"discover_controls", "Discover native controls or read a missing authoritative command schema."},
      {"inspect_state", "Read current document state or obtain missing stable resource IDs. Reuse still-current receipts."},
      {"use_document_tools", "Continue approved document work using supplied typed tools. This category does not establish availability, ownership or permission for an individual operation."},
      {"review_proof", "Obtain or validate a current rendered proof after editing; inspect pixels before judging visual quality."},
      {"deliver_output", "Save, export or commit only when approved scope, verification and native admission permit it."},
  };
  QJsonObject families;
  for (const auto& value : tools) {
    const auto name = value.toObject().value("name").toString();
    if (name.isEmpty()) continue;
    const auto family = agent_decision_tool_family(name);
    auto names = families.value(family).toArray();
    if (!names.contains(name)) names.append(name);
    families.insert(family, names);
  }
  QJsonArray result;
  for (auto it = families.begin(); it != families.end(); ++it)
    result.append(QJsonObject{{"id", it.key()}, {"purpose", purposes.value(it.key())},
        {"toolNames", it.value()}});
  if (allow_finish) result.append(QJsonObject{{"id", "finish_plan"},
      {"purpose", "Return a semantic photo edit intent or conclude planning when the current phase and evidence permit it. This does not certify completed edits."}});
  result.append(QJsonObject{{"id", "ask_user"},
      {"purpose", "Ask for essential missing user intent or required input that the app cannot discover."}});
  result.append(QJsonObject{{"id", "stop_safely"},
      {"purpose", "Stop because scope, permissions or a supported execution path do not permit the remaining work."}});
  result.append(QJsonObject{{"id", "insufficient_evidence"},
      {"purpose", "Abstain because these observations do not establish a useful next step; obtain fuller context."}});
  return result;
}

}  // namespace photo_editor::desktop

#include "agent_session_controller.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <iostream>

// An opt-in real-runtime conversation probe. This does not dispatch edits or
// stand in for native app/design acceptance. No elapsed-time cutoff is imposed.
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto arguments = app.arguments();
  if (arguments.size() != 3) {
    std::cerr << "Usage: agent_session_live_planning_driver input.json output.json\n";
    return 1;
  }
  QFile input(arguments.at(1));
  if (!input.open(QIODevice::ReadOnly) || QFile::exists(arguments.at(2))) return 1;
  QJsonParseError parse;
  const auto document = QJsonDocument::fromJson(input.readAll(), &parse);
  if (parse.error != QJsonParseError::NoError || !document.isObject()) return 1;
  const auto request = document.object();
  const auto model_id = request.value("modelId").toString();
  const auto prompt = request.value("prompt").toString();
  if (model_id.isEmpty() || prompt.trimmed().isEmpty() || !request.value("context").isObject()) return 1;

  const auto probe = photo_editor::desktop::probe_agent_local_model(model_id);
  // Keep relevant runtime provenance, rather than duplicating /api/show's
  // entire license, model template and thousands of tensor descriptions.
  QJsonObject probe_receipt;
  for (const auto& key : {"ok", "available", "configured", "status", "statusReason",
          "backend", "endpoint", "modelId", "ollamaModel", "modelFamily",
          "parameterSize", "quantizationLevel", "transport"}) {
    if (probe.contains(key)) probe_receipt.insert(key, QJsonValue::fromVariant(probe.value(key)));
  }
  const auto model_details = probe.value("probeResponse").toMap();
  probe_receipt.insert("capabilities", QJsonValue::fromVariant(model_details.value("capabilities")));
  probe_receipt.insert("details", QJsonValue::fromVariant(model_details.value("details")));
  QVariantMap planning_audit;
  photo_editor::desktop::AgentSessionController session(
      [&planning_audit](const photo_editor::desktop::AgentLocalModelTaskSessionRequest& planning_request) {
    auto result = photo_editor::desktop::plan_agent_task_session(planning_request);
    planning_audit = result.prompt_audit;
    return result;
  });
  int execution_requests = 0;
  qsizetype answer_round = 0;
  const auto answer_rounds = request.value("answerRounds").toArray();
  QVariantList asked_questions;
  bool defaults_chosen = false;
  QElapsedTimer elapsed;
  elapsed.start();
  QVariantList stages;
  QObject::connect(&session, &photo_editor::desktop::AgentSessionController::executionRequested,
      &app, [&](const QVariantMap&) { ++execution_requests; });
  session.setContextSnapshotHandler([model_id, probe](const QVariantMap& supplied) {
    auto context = supplied;
    context.insert("selectedModelId", model_id);
    context.insert("contextReady", true);
    context.insert("selectedModelReady", probe.value("ok").toBool() && probe.value("available").toBool());
    context.insert("selectedModelReadinessReason", probe.value("statusReason"));
    return context;
  });
  QObject::connect(&session, &photo_editor::desktop::AgentSessionController::stateChanged, &app, [&] {
    if (stages.isEmpty() || stages.last().toString() != session.stage()) stages.append(session.stage());
    if (session.stage() == "idle" || session.stage() == "planning") return;
    if (session.stage() == "clarifying" && request.value("useDefaults").toBool() && !defaults_chosen) {
      defaults_chosen = true;
      asked_questions.append(QVariantMap{{"questions", session.questions()}, {"choice", "Use your judgment"}});
      QTimer::singleShot(0, &session, [&session] { session.planWithDefaults(); });
      return;
    }
    if (session.stage() == "clarifying" && answer_round < answer_rounds.size()) {
      const auto answers = answer_rounds.at(answer_round++).toObject().toVariantMap();
      asked_questions.append(QVariantMap{{"questions", session.questions()}, {"answers", answers},
          {"elapsedMs", elapsed.elapsed()}});
      QTimer::singleShot(0, &session, [&session, answers] { session.answerQuestions(answers); });
      return;
    }
    QJsonObject receipt{{"contract", "agent-real-model-readonly-planning-v1"},
        {"modelId", model_id}, {"probe", probe_receipt},
        {"stage", session.stage()}, {"status", session.status()},
        {"brief", QJsonObject::fromVariantMap(session.brief())},
        {"plan", QJsonObject::fromVariantMap(session.plan())},
        {"questions", QJsonArray::fromVariantList(session.questions())},
        {"answeredQuestionRounds", QJsonArray::fromVariantList(asked_questions)},
        {"elapsedMs", elapsed.elapsed()},
        {"planningAudit", QJsonObject::fromVariantMap(planning_audit)},
        {"stages", QJsonArray::fromVariantList(stages)},
        {"executionRequests", execution_requests}, {"readOnly", execution_requests == 0},
        {"canStart", session.canStart()}, {"nativeAppExecutionQualified", false}};
    const auto ok = (session.stage() == "plan-ready" || session.stage() == "clarifying") && execution_requests == 0;
    receipt.insert("ok", ok);
    QSaveFile output(arguments.at(2));
    const auto bytes = QJsonDocument(receipt).toJson(QJsonDocument::Indented);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
      app.exit(1);
      return;
    }
    std::cout << session.stage().toStdString() << ": " << session.status().toStdString() << '\n';
    app.exit(ok ? 0 : 1);
  });
  QTimer::singleShot(0, &session, [&] { session.submit(prompt, request.value("context").toObject().toVariantMap()); });
  return app.exec();
}

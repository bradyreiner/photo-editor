#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "agent_local_model.h"

// Replay captured, bounded prompt evidence through the production reviewer.
// No catalog is opened, no edits are applied, and the original is never used
// as a substitute for the rendered after proof.
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto args = app.arguments();
  if (args.size() != 4) {
    std::cerr << "usage: agent_proof_review_replay REPORT APP_DATA_ROOT RECEIPT\n";
    return EXIT_FAILURE;
  }
  QFile source(args[1]);
  if (!source.open(QIODevice::ReadOnly)) return EXIT_FAILURE;
  QJsonParseError parse_error;
  const auto report = QJsonDocument::fromJson(source.readAll(), &parse_error).object();
  if (parse_error.error != QJsonParseError::NoError) return EXIT_FAILURE;
  const auto captured = report.value("validationReport").toObject()
      .value("modelProofCritique").toObject().value("promptAudit").toObject();
  const auto prompt = captured.value("userPrompt").toString();
  const QStringList labels{
      QStringLiteral("User instructions:\n"),
      QStringLiteral("\nStyle rulebook JSON:\n"),
      QStringLiteral("\nValidation report JSON:\n"),
      QStringLiteral("\nPrevious attempts JSON:\n"),
      QStringLiteral("\nPhotographer calibration JSON:\n"),
      QStringLiteral("\nProject North Star JSON:\n")};
  QStringList sections;
  qsizetype cursor = 0;
  for (qsizetype i = 0; i < labels.size(); ++i) {
    const auto start = prompt.indexOf(labels[i], cursor);
    if (start < 0) return EXIT_FAILURE;
    const auto content_start = start + labels[i].size();
    const auto end = i + 1 < labels.size()
        ? prompt.indexOf(labels[i + 1], content_start) : prompt.size();
    if (end < content_start) return EXIT_FAILURE;
    sections.append(prompt.mid(content_start, end - content_start));
    cursor = end;
  }
  QVariantList evidence;
  for (qsizetype i = 1; i < sections.size(); ++i) {
    const auto doc = QJsonDocument::fromJson(sections[i].toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError) return EXIT_FAILURE;
    evidence.append(doc.toVariant());
  }
  const auto proof_path = captured.value("imageSourcePath").toString();
  QFile proof(proof_path);
  if (!proof.open(QIODevice::ReadOnly)) return EXIT_FAILURE;
  const auto proof_hash = QString::fromLatin1(QCryptographicHash::hash(
      proof.readAll(), QCryptographicHash::Sha256).toHex());
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", args[2].toUtf8());
  photo_editor::desktop::AgentLocalModelProofCritiqueRequest request;
  request.model_id = report.value("modelId").toString();
  request.user_instructions = sections[0];
  request.style = evidence[0].toMap();
  request.validation_report = evidence[1].toMap();
  request.previous_attempts = evidence[2].toList();
  request.photographer_calibration = evidence[3].toMap();
  request.project_north_star = evidence[4].toMap();
  request.asset = {{QStringLiteral("proofImagePath"), proof_path}};
  QElapsedTimer timer;
  timer.start();
  const auto result = photo_editor::desktop::critique_agent_rendered_proof(request);
  const auto elapsed = timer.elapsed();
  const auto actual_prompt = result.prompt_audit.value("userPrompt").toString();
  const QVariantMap receipt{
      {QStringLiteral("contract"), QStringLiteral("agent-rendered-proof-replay-v1")},
      {QStringLiteral("ok"), result.ok},
      {QStringLiteral("elapsedMs"), elapsed},
      {QStringLiteral("error"), result.error_message},
      {QStringLiteral("pass"), result.pass},
      {QStringLiteral("needsUserReview"), result.needs_user_review},
      {QStringLiteral("summary"), result.summary},
      {QStringLiteral("tasteConfidence"), result.taste_confidence},
      {QStringLiteral("issues"), result.issues},
      {QStringLiteral("repairHints"), result.repair_hints},
      {QStringLiteral("capturedPromptMatches"), actual_prompt == prompt},
      {QStringLiteral("proofSha256"), proof_hash},
      {QStringLiteral("promptAudit"), result.prompt_audit},
      {QStringLiteral("editsApplied"), false},
      {QStringLiteral("automaticCommitAllowed"), false}};
  QSaveFile output(args[3]);
  if (!output.open(QIODevice::WriteOnly)) return EXIT_FAILURE;
  const auto bytes = QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Indented);
  if (output.write(bytes) != bytes.size() || !output.commit()) return EXIT_FAILURE;
  std::cout << "reviewOk=" << result.ok << " elapsedMs=" << elapsed
            << " capturedPromptMatches=" << (actual_prompt == prompt)
            << " imageIncluded=" << result.prompt_audit.value("imageIncluded").toBool()
            << '\n';
  return result.ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

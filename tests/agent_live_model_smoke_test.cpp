#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include "agent_edit_style.h"
#include "agent_local_model.h"

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto model_id = photo_editor::desktop::default_agent_local_model_id();
  if (model_id == QStringLiteral("rulebook-deterministic")) {
    std::cerr << "No configured Ollama model is available.\n";
    return EXIT_FAILURE;
  }

  const auto styles = photo_editor::desktop::built_in_agent_edit_styles();
  if (styles.isEmpty()) {
    std::cerr << "No built-in agent style is available.\n";
    return EXIT_FAILURE;
  }

  QTemporaryDir fixture_root;
  const auto fixture_path = fixture_root.filePath(QStringLiteral("live-smoke.jpg"));
  QImage fixture(1600, 1200, QImage::Format_RGB32);
  for (int y = 0; y < fixture.height(); ++y) {
    const auto value = static_cast<int>(70.0 + 130.0 * static_cast<double>(y) /
                                                static_cast<double>(fixture.height()));
    for (int x = 0; x < fixture.width(); ++x) {
      fixture.setPixelColor(x, y, QColor(value + (x % 24), value, value - 12));
    }
  }
  {
    QPainter painter(&fixture);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(QRect(1130, 90, 390, 470), QColor(252, 246, 220));
    painter.fillRect(QRect(1170, 130, 310, 390), QColor(255, 252, 238));
    painter.setBrush(QColor(122, 82, 62));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QRectF(650.0, 245.0, 260.0, 260.0));
    painter.setBrush(QColor(64, 58, 56));
    painter.drawRoundedRect(QRectF(520.0, 475.0, 520.0, 650.0), 90.0, 90.0);
  }
  if (!fixture.save(fixture_path, "JPEG", 90)) {
    std::cerr << "Unable to create the live vision fixture.\n";
    return EXIT_FAILURE;
  }

  photo_editor::desktop::AgentLocalModelPlanRequest request;
  request.model_id = model_id;
  request.style = styles.first().toMap();
  request.asset = QVariantMap{
      {QStringLiteral("assetId"), QStringLiteral("live-smoke")},
      {QStringLiteral("filePath"), fixture_path},
  };
  request.current_adjustment_patch_json =
      QStringLiteral("{\"kind\":\"photo-editor-adjustment-patch\",\"version\":1,\"scope\":\"asset-bound\",\"groups\":{}}");
  request.user_instructions =
      QStringLiteral(
          "Keep the image in realistic color and preserve the framing. Subdue only the bright upper-right window with a feathered localized adjustment so the central subject remains unchanged.");
  request.strategy_phase = QStringLiteral("sample");
  request.allow_tool_call_plan = false;
  QElapsedTimer fast_plan_timer;
  fast_plan_timer.start();
  const auto fast_result =
      photo_editor::desktop::plan_agent_recipe_patch(request);
  const auto fast_duration_ms = fast_plan_timer.elapsed();
  const auto fast_findings =
      fast_result.edit_intent.value(QStringLiteral("visualFindings")).toList();
  const auto fast_actions =
      fast_result.edit_intent.value(QStringLiteral("localizedActions")).toList();
  const auto fast_masks = fast_result.recipe_patch
                              .value(QStringLiteral("masks"))
                              .toMap()
                              .value(QStringLiteral("appendMaskStack"))
                              .toList();
  const auto fast_finding =
      fast_findings.isEmpty() ? QVariantMap{} : fast_findings.first().toMap();
  const auto fast_action =
      fast_actions.isEmpty() ? QVariantMap{} : fast_actions.first().toMap();
  const auto fast_mask =
      fast_masks.isEmpty() ? QVariantMap{} : fast_masks.first().toMap();
  const auto fast_mask_id = fast_action.value(QStringLiteral("maskId")).toString();
  if (!fast_result.ok ||
      fast_result.model_id != QStringLiteral("fast-local-vision") ||
      fast_result.prompt_audit.value(QStringLiteral("plannerMode")).toString() !=
          QStringLiteral("fast-local-highlight") ||
      fast_result.prompt_audit.value(QStringLiteral("modelInvoked")).toBool() ||
      fast_result.edit_intent.value(QStringLiteral("globalChangePolicy")).toString() !=
          QStringLiteral("preserve") ||
      fast_result.edit_intent.value(QStringLiteral("visualFindings")).toList().isEmpty() ||
      fast_result.edit_intent.value(QStringLiteral("localizedActions")).toList().isEmpty() ||
      fast_mask_id.isEmpty() ||
      fast_finding.value(QStringLiteral("maskId")).toString() != fast_mask_id ||
      fast_mask.value(QStringLiteral("id")).toString() != fast_mask_id ||
      fast_result.recipe_patch.value(QStringLiteral("masks"))
          .toMap()
          .value(QStringLiteral("appendMaskStack"))
          .toList()
          .isEmpty() ||
      fast_result.recipe_patch.contains(QStringLiteral("globalTone")) ||
      fast_duration_ms >= 1000) {
    std::cerr << "Fast local vision did not produce an immediate localized-only plan.\n";
    return EXIT_FAILURE;
  }
  request.force_model_planning = true;

  const auto result = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (!result.ok) {
    std::cerr << result.error_message.toStdString() << '\n';
    return EXIT_FAILURE;
  }
  const auto fallback_used =
      result.prompt_audit.value(QStringLiteral("fallbackUsed")).toBool();
  const auto fallback_reason =
      result.prompt_audit.value(QStringLiteral("fallbackReason")).toString();
  const auto response_source =
      result.prompt_audit.value(QStringLiteral("ollamaResponseSource")).toString();
  const auto image_byte_count =
      result.prompt_audit.value(QStringLiteral("imageByteCount")).toInt();
  const auto image_width =
      result.prompt_audit.value(QStringLiteral("imageWidth")).toInt();
  const auto image_height =
      result.prompt_audit.value(QStringLiteral("imageHeight")).toInt();
  const auto response_schema =
      result.prompt_audit.value(QStringLiteral("responseSchema")).toMap();
  const auto edit_intent_schema =
      response_schema.value(QStringLiteral("properties"))
          .toMap()
          .value(QStringLiteral("editIntent"))
          .toMap();
  const auto edit_intent_required =
      edit_intent_schema.value(QStringLiteral("required")).toList();
  if (fallback_used || result.model_id != model_id || response_source.trimmed().isEmpty() ||
      image_byte_count <= 0 || image_width <= 0 || image_height <= 0 ||
      result.prompt_audit.value(QStringLiteral("intentCompiler")).toString() !=
          QStringLiteral("rulebook-deterministic-v2") ||
      !edit_intent_required.contains(QStringLiteral("visualFindings")) ||
      !edit_intent_required.contains(QStringLiteral("localizedActions")) ||
      !result.edit_intent.contains(QStringLiteral("visualFindings")) ||
      !result.edit_intent.contains(QStringLiteral("localizedActions"))) {
    const auto ollama_attempt =
        result.prompt_audit.value(QStringLiteral("ollamaAttempt")).toMap();
    std::cerr << "Live vision proof used fallback or omitted model/image evidence: requestedModel="
              << model_id.toStdString()
              << " resultModel=" << result.model_id.toStdString()
              << " fallbackUsed=" << (fallback_used ? "true" : "false")
              << " fallbackReason=" << fallback_reason.toStdString()
              << " responseSource=" << response_source.toStdString()
              << " responseChars="
              << ollama_attempt.value(QStringLiteral("responseCharCount")).toInt()
              << " responseError="
              << ollama_attempt.value(QStringLiteral("responseError")).toString().toStdString()
              << " responsePreview="
              << ollama_attempt.value(QStringLiteral("responsePreview")).toString().toStdString()
              << " imageBytes=" << image_byte_count
              << " imageSize=" << image_width << 'x' << image_height << '\n';
    return EXIT_FAILURE;
  }
  if (result.decision != QStringLiteral("recipe_patch") || result.recipe_patch.isEmpty() ||
      result.style_constraint_citations.isEmpty() ||
      result.edit_intent.value(QStringLiteral("colorMode")).toString() ==
          QStringLiteral("monochrome") ||
      result.edit_intent.value(QStringLiteral("crop")).toString() !=
          QStringLiteral("preserve") ||
      result.edit_intent.value(QStringLiteral("globalChangePolicy")).toString() !=
          QStringLiteral("preserve") ||
      result.edit_intent.value(QStringLiteral("visualFindings")).toList().isEmpty() ||
      result.edit_intent.value(QStringLiteral("localizedActions")).toList().isEmpty() ||
      result.prompt_audit.value(QStringLiteral("localizedMaskCount")).toInt() < 1 ||
      result.recipe_patch.value(QStringLiteral("masks"))
          .toMap()
          .value(QStringLiteral("appendMaskStack"))
          .toList()
          .isEmpty() ||
      result.recipe_patch.contains(QStringLiteral("globalTone")) ||
      result.recipe_patch.contains(QStringLiteral("color")) ||
      result.recipe_patch.contains(QStringLiteral("crop"))) {
    std::cerr << "Unexpected live model decision: " << result.decision.toStdString() << '\n';
    return EXIT_FAILURE;
  }
  const auto findings =
      result.edit_intent.value(QStringLiteral("visualFindings")).toList();
  const auto actions =
      result.edit_intent.value(QStringLiteral("localizedActions")).toList();
  const auto masks = result.recipe_patch
                         .value(QStringLiteral("masks"))
                         .toMap()
                         .value(QStringLiteral("appendMaskStack"))
                         .toList();
  const auto finding = findings.first().toMap();
  const auto action = actions.first().toMap();
  const auto mask = masks.first().toMap();
  const auto mask_id = action.value(QStringLiteral("maskId")).toString();
  if (mask_id.isEmpty() ||
      finding.value(QStringLiteral("maskId")).toString() != mask_id ||
      mask.value(QStringLiteral("id")).toString() != mask_id) {
    std::cerr << "Live model finding, localized action, and editable mask were not spatially linked.\n";
    return EXIT_FAILURE;
  }
  std::cout << "model=" << result.model_id.toStdString()
            << " responseSource="
            << result.prompt_audit.value(QStringLiteral("ollamaResponseSource")).toString().toStdString()
            << " decision=" << result.decision.toStdString()
            << " summary=" << result.plan_summary.toStdString()
            << " patchGroups=" << result.recipe_patch.size()
            << " citations=" << result.style_constraint_citations.size()
            << " findings="
            << result.edit_intent.value(QStringLiteral("visualFindings")).toList().size()
            << " localizedActions="
            << result.edit_intent.value(QStringLiteral("localizedActions")).toList().size()
            << " localizedMasks="
            << result.prompt_audit.value(QStringLiteral("localizedMaskCount")).toInt()
            << " durationMs="
            << result.prompt_audit.value(QStringLiteral("transport"))
                   .toMap()
                   .value(QStringLiteral("durationMs"))
                   .toInt()
            << " promptChars="
            << result.prompt_audit.value(QStringLiteral("userPromptCharCount")).toInt()
            << " serverPromptMs="
            << result.prompt_audit.value(QStringLiteral("ollamaServer"))
                   .toMap()
                   .value(QStringLiteral("promptEvalDurationMs"))
                   .toLongLong()
            << " serverEvalMs="
            << result.prompt_audit.value(QStringLiteral("ollamaServer"))
                   .toMap()
                   .value(QStringLiteral("evalDurationMs"))
                   .toLongLong()
            << " serverEvalTokens="
            << result.prompt_audit.value(QStringLiteral("ollamaServer"))
                   .toMap()
                   .value(QStringLiteral("evalCount"))
                   .toInt()
            << " fastDurationMs=" << fast_duration_ms
            << " imageBytes="
            << result.prompt_audit.value(QStringLiteral("imageByteCount")).toInt()
            << " originalImageBytes="
            << result.prompt_audit.value(QStringLiteral("imageOriginalByteCount")).toInt()
            << " imageSize="
            << result.prompt_audit.value(QStringLiteral("imageWidth")).toInt()
            << 'x'
            << result.prompt_audit.value(QStringLiteral("imageHeight")).toInt()
            << '\n';
  return EXIT_SUCCESS;
}

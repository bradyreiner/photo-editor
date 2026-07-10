#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QColor>
#include <QImage>
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
  request.current_recipe_json = QStringLiteral("{\"version\":1}");
  request.user_instructions =
      QStringLiteral("Create a subtle natural draft with protected highlights and realistic color.");
  request.strategy_phase = QStringLiteral("sample");
  request.allow_tool_call_plan = false;

  const auto result = photo_editor::desktop::plan_agent_recipe_patch(request);
  if (!result.ok) {
    std::cerr << result.error_message.toStdString() << '\n';
    return EXIT_FAILURE;
  }
  if (result.decision != QStringLiteral("recipe_patch") || result.recipe_patch.isEmpty() ||
      result.style_constraint_citations.isEmpty()) {
    std::cerr << "Unexpected live model decision: " << result.decision.toStdString() << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "model=" << result.model_id.toStdString()
            << " responseSource="
            << result.prompt_audit.value(QStringLiteral("ollamaResponseSource")).toString().toStdString()
            << " decision=" << result.decision.toStdString()
            << " summary=" << result.plan_summary.toStdString()
            << " patchGroups=" << result.recipe_patch.size()
            << " citations=" << result.style_constraint_citations.size()
            << " durationMs="
            << result.prompt_audit.value(QStringLiteral("transport"))
                   .toMap()
                   .value(QStringLiteral("durationMs"))
                   .toInt()
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

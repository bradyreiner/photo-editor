#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include "editor_preview_controller.h"

namespace photo_editor::desktop {

struct EditorPreviewControllerTestAccess {
  static QString adjusted_working_cache_key(const RenderRequest& request) {
    return EditorPreviewController::adjustedWorkingCacheKeyForTesting(request);
  }

  static QString rendered_preview_cache_key(const RenderRequest& request) {
    return EditorPreviewController::renderedPreviewCacheKeyForTesting(request);
  }
};

}  // namespace photo_editor::desktop

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

photo_editor::desktop::RenderRequest color_cache_request(const QString& file_path) {
  photo_editor::desktop::RenderRequest request;
  request.file_path = file_path;
  request.full_resolution = true;
  request.state.input_color_profile = QStringLiteral("Embedded");
  request.state.working_color_profile = QStringLiteral("Linear sRGB");
  request.state.preview_color_space = QStringLiteral("Adobe RGB");
  request.state.output_color_space = QStringLiteral("Adobe RGB");
  request.state.render_intent = QStringLiteral("Perceptual");
  request.state.exposure = 0.25;
  return request;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("runtime root should be valid");
  }

  const auto source_path = runtime_root.filePath(QStringLiteral("source.bin"));
  QFile source(source_path);
  if (!source.open(QIODevice::WriteOnly)) {
    return fail("source fixture should be writable");
  }
  source.write("color-cache-fixture");
  source.close();

  const auto baseline_request = color_cache_request(source_path);
  const auto baseline_adjusted_key =
      photo_editor::desktop::EditorPreviewControllerTestAccess::adjusted_working_cache_key(
          baseline_request);
  const auto baseline_rendered_key =
      photo_editor::desktop::EditorPreviewControllerTestAccess::rendered_preview_cache_key(
          baseline_request);
  if (baseline_adjusted_key.isEmpty() || baseline_rendered_key.isEmpty()) {
    return fail("baseline color cache keys should be available");
  }

  auto output_profile_request = baseline_request;
  output_profile_request.state.preview_color_space = QStringLiteral("Display P3");
  output_profile_request.state.output_color_space = QStringLiteral("Display P3");
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::adjusted_working_cache_key(
          output_profile_request) != baseline_adjusted_key ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::rendered_preview_cache_key(
          output_profile_request) == baseline_rendered_key) {
    return fail("output profile changes should reuse adjusted working pixels but invalidate rendered previews");
  }

  auto output_policy_request = baseline_request;
  output_policy_request.state.output_headroom = 2.0;
  output_policy_request.state.output_gamut_mapping_enabled = false;
  output_policy_request.state.output_gamut_mapping_policy = QStringLiteral("Clip");
  output_policy_request.state.black_point_compensation_enabled = true;
  output_policy_request.state.source_black_point = 0.02;
  output_policy_request.state.output_black_point = 0.05;
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::adjusted_working_cache_key(
          output_policy_request) != baseline_adjusted_key ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::rendered_preview_cache_key(
          output_policy_request) == baseline_rendered_key) {
    return fail("final output policy changes should reuse adjusted working pixels but invalidate rendered previews");
  }

  auto render_intent_request = baseline_request;
  render_intent_request.state.render_intent = QStringLiteral("Relative Colorimetric");
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::adjusted_working_cache_key(
          render_intent_request) != baseline_adjusted_key ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::rendered_preview_cache_key(
          render_intent_request) == baseline_rendered_key) {
    return fail("render intent changes should reuse adjusted working pixels but invalidate rendered previews");
  }

  auto working_profile_request = baseline_request;
  working_profile_request.state.working_color_profile = QStringLiteral("ACEScg");
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::adjusted_working_cache_key(
          working_profile_request) == baseline_adjusted_key ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::rendered_preview_cache_key(
          working_profile_request) == baseline_rendered_key) {
    return fail("working profile changes should invalidate adjusted and rendered preview caches");
  }

  auto input_profile_request = baseline_request;
  input_profile_request.state.input_color_profile =
      QStringLiteral("ICC: Manufacturer Camera Profile");
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::adjusted_working_cache_key(
          input_profile_request) == baseline_adjusted_key ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::rendered_preview_cache_key(
          input_profile_request) == baseline_rendered_key) {
    return fail("input profile changes should invalidate adjusted and rendered preview caches");
  }

  return EXIT_SUCCESS;
}

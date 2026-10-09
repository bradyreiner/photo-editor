#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QColorSpace>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include "editor_preview_controller.h"

namespace photo_editor::desktop {

RenderResult render_preview_request_for_test(const RenderRequest& request);

struct EditorPreviewControllerTestAccess {
  static void clear_caches() {
    EditorPreviewController::clearPreviewWorkingCachesForTesting();
  }
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

bool comparison_cache_contracts(const QTemporaryDir& runtime) {
  using namespace photo_editor::desktop;
  const auto reject = [](int line) {
    std::cerr << "comparison cache contract failed at line " << line << '\n';
    return false;
  };
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QImage fixture(96, 72, QImage::Format_ARGB32);
  fixture.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
  for (int y = 0; y < fixture.height(); ++y) {
    for (int x = 0; x < fixture.width(); ++x) {
      fixture.setPixelColor(x, y, QColor((x * 3) % 256, (y * 4) % 256,
          (x * 7 + y * 11) % 256, 64 + (x + y) % 192));
    }
  }
  const auto source = runtime.filePath("compare.png");
  const auto external = runtime.filePath("reference.png");
  if (!fixture.save(source) || !fixture.flipped().save(external)) return reject(__LINE__);
  const auto equal_image = [](const QImage& a, const QImage& b) {
    return !a.isNull() && a == b && a.colorSpace() == b.colorSpace();
  };
  int cases = 0;
  for (const auto& model : {QStringLiteral("legacy-oklch-v1"), QStringLiteral("aces2-ocio-v1")}) {
    for (const auto& monitor : {QStringLiteral("sRGB"), QStringLiteral("Display P3")}) {
      for (int mode = 0; mode < 4; ++mode) {
        EditorPreviewControllerTestAccess::clear_caches();
        RenderRequest request;
        request.file_path = source;
        request.source_asset_id = QStringLiteral("comparison-cache-fixture");
        request.full_resolution = true;
        request.use_in_memory_preview = true;
        request.require_cpu_preview_image = true;
        request.require_canonical_render_plan = model == QStringLiteral("aces2-ocio-v1");
        request.state.photographic_rendering_model = model;
        request.state.monitor_color_profile = monitor;
        request.state.exposure = 0.37;
        request.state.crop_rect_x = 0.1;
        request.state.crop_rect_y = 0.1;
        request.state.crop_rect_width = 0.8;
        request.state.crop_rect_height = 0.8;
        request.compare_mode = true;
        request.has_comparison_reference_state = mode == 1;
        request.comparison_reference_state = request.state;
        request.comparison_reference_state.exposure = -0.2;
        request.comparison_reference_state.crop_rect_width = 0.6;
        if (mode == 2) request.reference_file_path = external;
        if (mode == 3) request.compare_adjustment_groups = {QStringLiteral("tone")};
        const auto cold = render_preview_request_for_test(request);
        auto edited_request = request;
        edited_request.compare_mode = false;
        const auto seed = render_preview_request_for_test(edited_request);
        const auto warm = render_preview_request_for_test(request);
        if (!cold.success || cold.rendered_cache_hit || !seed.success || !warm.success ||
            !warm.rendered_cache_hit || !equal_image(cold.preview_image, warm.preview_image) ||
            !equal_image(seed.preview_image, warm.preview_image) ||
            !equal_image(cold.reference_preview_image, warm.reference_preview_image)) {
          std::cerr << "comparison cache parity failed: " << model.toStdString() << ' '
                    << monitor.toStdString() << " mode=" << mode << " cold=" << cold.success
                    << " seed=" << seed.success << " warm=" << warm.success
                    << " hit=" << warm.rendered_cache_hit
                    << " status=" << cold.status.toStdString()
                    << " detail=" << cold.failure_detail.toStdString()
                    << " plan=" << cold.canonical_plan_error.toStdString() << '\n';
          return reject(__LINE__);
        }
        auto changed = request;
        changed.state.exposure += 0.19;
        const auto changed_result = render_preview_request_for_test(changed);
        if (!changed_result.success || changed_result.rendered_cache_hit ||
            equal_image(changed_result.preview_image, warm.preview_image)) return reject(__LINE__);
        if (mode == 1) {
          changed = request;
          changed.comparison_reference_state.exposure += 0.5;
          const auto changed_reference = render_preview_request_for_test(changed);
          if (!changed_reference.success || !changed_reference.rendered_cache_hit ||
              !equal_image(changed_reference.preview_image, warm.preview_image) ||
              equal_image(changed_reference.reference_preview_image, warm.reference_preview_image)) return reject(__LINE__);
        }
        if (mode == 2) {
          changed = request;
          changed.reference_file_path = runtime.filePath("missing-reference.png");
          const auto missing = render_preview_request_for_test(changed);
          if (missing.success || !missing.reference_preview_image.isNull() ||
              (missing.failure_stage != QStringLiteral("reference_decode") &&
               missing.failure_stage != QStringLiteral("reference_render"))) return reject(__LINE__);
        }
        for (int guard = 0; guard < 4; ++guard) {
          auto guarded = request;
          if (guard == 0) guarded.full_invalidation = false;
          if (guard == 1) guarded.scopes_visible = true;
          if (guard == 2) guarded.generate_proof_gamut_warning_mask = true;
          if (guard == 3) guarded.allow_texture_patch_only = true;
          const auto result = render_preview_request_for_test(guarded);
          if (result.rendered_cache_hit) return reject(__LINE__);
        }
        auto canceled = request;
        canceled.generation = 1;
        canceled.latest_generation = std::make_shared<std::atomic<int>>(2);
        const auto cancellation = render_preview_request_for_test(canceled);
        if (!cancellation.cancelled || cancellation.success) return reject(__LINE__);
        ++cases;
      }
    }
  }
  std::cout << "comparison cache: " << cases << " exact cold/warm cases plus invalidation guards passed\n";
  return true;
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

  if (!comparison_cache_contracts(runtime_root)) return fail("comparison image cache contracts failed");
  return EXIT_SUCCESS;
}

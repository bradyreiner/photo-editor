#include "decoded_image_cache.h"
#include "editor_preview_controller.h"
#include "linked_image_source.h"
#include "photo_layer_source.h"
#include "photo_render_utils.h"
#include "render_plan_store.h"
#include "thumbnail_shared_preview_adapter.h"

#include <QGuiApplication>
#include <QSaveFile>
#include <QTemporaryDir>
#include <iostream>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static QString adjusted(const RenderRequest& request) {
    return EditorPreviewController::adjustedWorkingCacheKeyForTesting(request);
  }
  static QString rendered(const RenderRequest& request) {
    return EditorPreviewController::renderedPreviewCacheKeyForTesting(request);
  }
};
}  // namespace photo_editor::desktop

using namespace photo_editor::desktop;
using namespace photo_editor::pipeline;

int main(int argc, char** argv) {
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("runtime").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  QGuiApplication application(argc, argv);
  auto check = [](bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
  };
  const auto base_path = root.filePath("base.bmp");
  const auto layer_path = root.filePath("linked.bmp");
  QImage base(32, 24, QImage::Format_RGB32);
  base.setColorSpace(QColorSpace(QColorSpace::SRgb));
  base.fill(Qt::black);
  if (!base.save(base_path)) return 1;
  auto write_layer = [&](const QColor& color, bool atomic = false) {
    auto image = base;
    image.fill(color);
    if (!atomic) return image.save(layer_path);
    QSaveFile file(layer_path);
    return file.open(QIODevice::WriteOnly) && image.save(&file, "BMP") && file.commit();
  };
  if (!write_layer(Qt::red)) return 1;
  RenderRequest request;
  request.source_asset_id = "linked-layer-test-base";
  request.file_path = base_path;
  request.full_resolution = true;
  request.max_edge = 0;
  request.generation = 1;
  request.use_in_memory_preview = true;
  request.require_cpu_preview_image = true;
  request.state.layers = {QVariantMap{{"id", "linked"}, {"name", "Linked portrait"},
      {"kind", "source"}, {"sourcePath", layer_path}, {"pixelWidth", 32}, {"pixelHeight", 24}}};
  // Explicit recipe identities must not hide external source revisions.
  request.recipe_cache_identity = "persisted-recipe-unchanged";
  request.adjusted_recipe_cache_identity = "persisted-adjustments-unchanged";
  auto admission = [&]() {
    return RenderPlanStore::shared().admit(request.source_asset_id, base_path,
        request.state, base.size(), RenderPurpose::SettledPreview,
        DisplayRenderingTarget::SrgbSimulation, {}, nullptr, "same-recipe-digest");
  };
  const auto plan_before = admission();
  const auto plan_cached = admission();
  if (!check(plan_before.admitted && plan_cached.cache_hit, "canonical plan must cache before replacement")) return 1;
  const auto adjusted_before = EditorPreviewControllerTestAccess::adjusted(request);
  const auto rendered_before = EditorPreviewControllerTestAccess::rendered(request);
  const auto decode_before = decoded_image_cache_key(QFileInfo(layer_path), 0);
  const auto modified = QFileInfo(layer_path).lastModified();
  const auto bytes = QFileInfo(layer_path).size();
  const auto decoded_before = decode_photo_layer_source_image(layer_path);
  const auto preview_before = execute_preview_render_request(request);
  if (!check(preview_before.success && !preview_before.preview_image.isNull(), "initial real preview failed")) {
    std::cerr << preview_before.failure_detail.toStdString() << '\n';
    return 1;
  }
  if (!write_layer(Qt::green)) return 1;
  QFile source_file(layer_path);
  if (!source_file.open(QIODevice::ReadWrite) ||
      !source_file.setFileTime(modified, QFileDevice::FileModificationTime)) return 1;
  source_file.close();
  if (!check(QFileInfo(layer_path).size() == bytes && QFileInfo(layer_path).lastModified() == modified,
      "fixture must preserve size and millisecond mtime")) return 1;
  if (!check(adjusted_before != EditorPreviewControllerTestAccess::adjusted(request) &&
      rendered_before != EditorPreviewControllerTestAccess::rendered(request) &&
      decode_before != decoded_image_cache_key(QFileInfo(layer_path), 0),
      "same-size restored-mtime rewrite must invalidate all pixel caches")) return 1;
  const auto plan_after = admission();
  if (!check(plan_after.admitted && !plan_after.cache_hit &&
      plan_after.plan_cache_key != plan_before.plan_cache_key,
      "linked revision must change canonical content and plan identity")) return 1;
  const auto decoded_after = decode_photo_layer_source_image(layer_path);
  const auto preview_after = execute_preview_render_request(request);
  if (!check(!decoded_before.isNull() && !decoded_after.isNull() && decoded_before != decoded_after &&
      preview_after.success && !preview_after.preview_image.isNull() &&
      preview_before.preview_image != preview_after.preview_image,
      "source decode and published preview must actually change after rewrite")) return 1;
  const auto atomic_before = linked_image_file_revision(layer_path);
  if (!write_layer(Qt::blue, true) || !check(atomic_before != linked_image_file_revision(layer_path),
      "atomic file replacement must invalidate identity")) return 1;

  // A warmed disk preview must never turn missing media into a successful render.
  request.use_in_memory_preview = false;
  const auto disk = execute_preview_render_request(request);
  if (!check(disk.success, "disk preview warm-up failed")) return 1;
  if (!write_layer(Qt::green)) return 1;
  const auto changed_disk = execute_preview_render_request(request);
  if (!check(changed_disk.success && !changed_disk.disk_cache_hit &&
      changed_disk.preview_source != disk.preview_source &&
      QImage(QUrl(changed_disk.preview_source).toLocalFile()) != QImage(QUrl(disk.preview_source).toLocalFile()),
      "changed linked file must invalidate disk preview path and decoded output pixels")) return 1;
  if (!QFile::rename(layer_path, root.filePath("linked-moved.bmp"))) return 1;
  const auto missing = execute_preview_render_request(request);
  if (!check(!missing.success && missing.failure_stage == "layer_source" &&
      missing.failure_detail.contains(layer_path) && missing.failure_layer_ids == QStringList{"linked"} &&
      !admission().admitted,
      "missing linked media must reject preview and canonical admission before cache reuse")) return 1;
  for (const auto& model : {QString("legacy-oklch-v1"), QString("aces2-ocio-v1")}) {
    request.state.photographic_rendering_model = model;
    RenderBackendTelemetry telemetry;
    const auto output = render_output_image_with_backend(base, request.state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
        RenderBackendPreference::CpuOnly, nullptr, &telemetry);
    if (!check(output.isNull() && telemetry.fallback_reason.contains(layer_path),
        "missing linked media must fail output with an actionable reason")) return 1;
  }
  // Existing but corrupt media must also fail, beyond the cheap file preflight.
  QFile corrupt(layer_path);
  if (!corrupt.open(QIODevice::WriteOnly) || corrupt.write("not an image") <= 0) return 1;
  corrupt.close();
  request.use_in_memory_preview = true;
  const auto corrupt_preview = execute_preview_render_request(request);
  RenderBackendTelemetry corrupt_telemetry;
  const auto corrupt_output = render_output_image_with_backend(base, request.state,
      QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly, nullptr, &corrupt_telemetry);
  if (!check(!corrupt_preview.success && corrupt_preview.failure_stage == "layer_source" &&
      corrupt_preview.failure_detail.contains("Linked portrait") && corrupt_output.isNull() &&
      corrupt_telemetry.fallback_reason.contains("Linked portrait"),
      "corrupt linked media must fail actual preview and output decoding")) {
    std::cerr << "preview success=" << corrupt_preview.success
              << " preview detail=" << corrupt_preview.failure_detail.toStdString()
              << " output null=" << corrupt_output.isNull()
              << " output reason=" << corrupt_telemetry.fallback_reason.toStdString() << '\n';
    return 1;
  }
  for (const auto* property : {"enabled", "opacity"}) {
    auto layer = request.state.layers[0].toMap();
    layer.insert(property, 0);
    auto disabled = request.state;
    disabled.layers = {layer};
    if (!check(linked_photo_layer_source_file_error(disabled.layers).isEmpty() &&
        !render_output_image_with_backend(base, disabled, QColorSpace(QColorSpace::SRgb),
            QImage::Format_ARGB32, RenderBackendPreference::CpuOnly).isNull(),
        "disabled or transparent linked layers must not block output")) return 1;
  }
  auto hidden_by_solo = request.state;
  hidden_by_solo.layers.append(QVariantMap{{"id", "solo-fill"}, {"kind", "fill"},
      {"solo", true}, {"fillColor", "#0000ff"}});
  if (!check(!render_output_image_with_backend(base, hidden_by_solo, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly).isNull(),
      "non-solo missing layer must not block solo output")) return 1;
  if (!write_layer(Qt::red)) return 1;
  const auto restored = execute_preview_render_request(request);
  if (!check(restored.success && !restored.preview_image.isNull(), "restoring linked file must recover rendering")) return 1;
  std::cout << "Linked source rewrite, replacement, decoded pixels, canonical plan, preview, disk cache, missing/corrupt output, visibility and recovery gates passed\n";
  return 0;
}

#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "flattened_output_renderer.h"
#include "photo_render_utils.h"
#include "thumbnail_shared_preview_adapter.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <iostream>

namespace photo_editor::desktop {
class CropPreviewSink final : public EditorPreviewImageSink {
 public:
  void setImage(const QString& key, const QImage& image) override { images.insert(key, image); }
  void clearImage(const QString& key) override { images.remove(key); }
  QHash<QString, QImage> images;
};
struct EditorPreviewControllerTestAccess {
  static EditRecipeState state(const EditorPreviewController& c) { return c.currentState(); }
  static void refresh(EditorPreviewController& c) {
    auto request = c.currentRenderRequest(true);
    request.use_in_memory_preview = true; // Preserve pixels instead of the full-preview JPEG carrier.
    request.draft_interactive = false;
    request.render_reason = "crop-parity-proof";
    c.startRenderStage(true, request);
  }
  static bool idle(const EditorPreviewController& c) {
    return !c.processing() && !c.interactive_render_watcher_.isRunning() &&
        !c.full_render_watcher_.isRunning() && !c.interactive_request_pending_ &&
        !c.full_request_pending_ && !c.render_scheduler_.interactiveActive() &&
        !c.render_scheduler_.fullActive();
  }
};
}
using namespace photo_editor::desktop;

int main(int argc, char** argv) {
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("runtime").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication app(argc, argv);
  QString path = argc > 1 ? QString::fromLocal8Bit(argv[1]) : root.filePath("gradient.jpg");
  if (argc == 1) {
    QImage image(320, 240, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
      for (int x = 0; x < image.width(); ++x)
        image.setPixelColor(x, y, QColor(x * 255 / 319, y * 255 / 239, (x + y) % 256));
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    if (!image.save(path)) return 1;
  }
  CropPreviewSink sink;
  EditorPreviewController controller;
  controller.setImageProvider(&sink);
  controller.setPreviewTargetEdge(6144);
  controller.loadAsset("crop-zoom-proof", path, {}, {}, {}, {}, {});
  const auto wait = [&] {
    QElapsedTimer timer; timer.start();
    do {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      QThread::msleep(5);
    } while (!EditorPreviewControllerTestAccess::idle(controller) && timer.elapsed() < 30000);
    return EditorPreviewControllerTestAccess::idle(controller) && controller.previewFailureStage().isEmpty();
  };
  if (!wait()) return 1;
  EditorPreviewControllerTestAccess::refresh(controller);
  if (!wait()) return 1;
  const auto original = EditorPreviewControllerTestAccess::state(controller);
  const QImage original_preview = sink.images.value("edited");
  controller.setCropZoom(2.0);
  const auto zoomed = EditorPreviewControllerTestAccess::state(controller);
  const auto close = [](double a, double b) { return std::abs(a - b) < 1e-9; };
  if (!close(zoomed.crop_rect_width, 0.5) || !close(zoomed.crop_rect_height, 0.5) ||
      !close(zoomed.crop_rect_x, 0.25) || !close(zoomed.crop_rect_y, 0.25)) {
    std::cerr << "First 2x crop zoom must select the central half, got " << zoomed.crop_rect_width
              << " x " << zoomed.crop_rect_height << '\n';
    return 1;
  }
  if (!wait()) return 1;
  EditorPreviewControllerTestAccess::refresh(controller);
  if (!wait()) return 1;
  const QImage cropped_preview = sink.images.value("edited");
  if (original_preview.isNull() || cropped_preview.isNull() || original_preview == cropped_preview) {
    std::cerr << "Crop must publish a changed actual editor preview\n";
    return 1;
  }
  {
    const QRect expected(qRound(original_preview.width() * 0.25), qRound(original_preview.height() * 0.25),
        qRound(original_preview.width() * 0.5), qRound(original_preview.height() * 0.5));
    const auto central = original_preview.copy(expected).convertToFormat(QImage::Format_RGBA8888);
    const auto actual = cropped_preview.convertToFormat(QImage::Format_RGBA8888);
    qint64 changed = 0;
    int maximum = 0;
    if (central.size() == actual.size()) {
      for (int y = 0; y < actual.height(); ++y) for (int x = 0; x < actual.width(); ++x) {
        const auto a = central.pixelColor(x, y), b = actual.pixelColor(x, y);
        const auto delta = std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()), std::abs(a.blue() - b.blue())});
        changed += delta != 0; maximum = std::max(maximum, delta);
      }
    }
    std::cout << "Actual preview full=" << original_preview.width() << 'x' << original_preview.height()
              << " crop=" << actual.width() << 'x' << actual.height() << " changed=" << changed << " max=" << maximum << '\n';
    if (actual.size() != central.size() || changed != 0) {
      std::cerr << "Actual preview must preserve cropped source colors\n"; return 1;
    }
    for (const int edge : {512, 1600}) {
      const auto thumbnail = [&](const EditRecipeState& state) {
        return render_thumbnail_shared_preview_request(ThumbnailAdjustmentSharedRenderRequest{
            .asset_id = "crop-thumbnail-proof", .file_path = path, .state = state,
            .render_reason = "photo_layer_thumbnail", .interaction_mode = "thumbnail",
            .render_mode = "layer_thumbnail", .max_edge = edge});
      };
      const auto full_thumb = thumbnail(original), crop_thumb = thumbnail(zoomed);
      const QRect thumb_rect(qRound(full_thumb.image.width() * 0.25), qRound(full_thumb.image.height() * 0.25),
          qRound(full_thumb.image.width() * 0.5), qRound(full_thumb.image.height() * 0.5));
      if (!full_thumb.success || !crop_thumb.success || full_thumb.image.isNull() || crop_thumb.image.isNull() ||
          full_thumb.image.copy(thumb_rect).convertToFormat(QImage::Format_RGBA8888) !=
              crop_thumb.image.convertToFormat(QImage::Format_RGBA8888)) {
        std::cerr << "Shared thumbnail crop color mismatch at edge " << edge << '\n'; return 1;
      }
      std::cout << "Shared thumbnail exact crop pixels passed at edge " << edge << '\n';
    }
    auto tiled = execute_preview_render_request(RenderRequest{
        .source_asset_id = "crop-tiled-proof", .recipe_cache_identity = "crop-tiled-proof:v1",
        .adjusted_recipe_cache_identity = "crop-tiled-adjusted-proof:v1", .file_path = path,
        .render_reason = "crop_tile_proof", .tile_size = 512, .state = zoomed, .full_resolution = true,
        .max_edge = 0, .generation = 1, .use_in_memory_preview = true});
    std::cout << "Tiled result=" << tiled.success << " image=" << tiled.preview_image.width()
              << 'x' << tiled.preview_image.height() << " executed=" << tiled.executed_tile_count << '\n';
    if (!tiled.success || tiled.executed_tile_count <= 0 ||
        tiled.preview_image.convertToFormat(QImage::Format_RGBA8888) != central) {
      qint64 different = 0; int largest = 0;
      if (tiled.preview_image.size() == central.size()) {
        for (int y = 0; y < central.height(); ++y) for (int x = 0; x < central.width(); ++x) {
          const auto a = central.pixelColor(x, y), b = tiled.preview_image.pixelColor(x, y);
          const auto delta = std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()), std::abs(a.blue() - b.blue()), std::abs(a.alpha() - b.alpha())});
          different += delta != 0; largest = std::max(largest, delta);
        }
      }
      std::cerr << "Tile changed=" << different << " max=" << largest << '\n';
      std::cerr << "Tiled crop color mismatch\n"; return 1;
    }
  }
  controller.undo();
  if (!wait() || serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(controller)) !=
      serialize_edit_recipe_json(original)) return 1;
  controller.redo();
  if (!wait() || serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(controller)) !=
      serialize_edit_recipe_json(zoomed)) return 1;
  controller.setCropZoom(1.0);
  if (!wait()) return 1;
  const auto restored = EditorPreviewControllerTestAccess::state(controller);
  if (!close(restored.crop_rect_width, 1.0) || !close(restored.crop_rect_height, 1.0) ||
      !close(restored.crop_rect_x, 0.0) || !close(restored.crop_rect_y, 0.0)) return 1;
  controller.setCropZoom(2.0); // An already explicit rectangle must still scale once.
  if (!wait() || !close(controller.cropRectWidth(), 0.5) || !close(controller.cropRectHeight(), 0.5)) return 1;
  const auto render = [&](const EditRecipeState& state) {
    return render_flattened_output_file("crop-zoom-proof", path, state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32);
  };
  const auto full = render(original);
  const auto cropped = render(zoomed);
  const auto returned = render(restored);
  if (original_preview.convertToFormat(QImage::Format_RGBA8888) != full.convertToFormat(QImage::Format_RGBA8888) ||
      cropped_preview.convertToFormat(QImage::Format_RGBA8888) != cropped.convertToFormat(QImage::Format_RGBA8888)) {
    std::cerr << "Lossless full-resolution preview and flattened output disagree\n"; return 1;
  }
  std::cout << "Full-resolution preview and flattened output are pixel-identical before and after crop\n";
  // Independent pixel oracle: a 2x neutral crop is the centered half of the
  // rendered full frame. Do not derive the expected rectangle from crop helpers.
  const QRect central(qRound(full.width() * 0.25), qRound(full.height() * 0.25),
      qRound(full.width() * 0.5), qRound(full.height() * 0.5));
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_TEST_CROP_OUTPUT");
  if (!output.isEmpty() && (!full.save(output + "-full.png") || !cropped.save(output + "-crop.png") ||
      !returned.save(output + "-returned.png") || !cropped_preview.save(output + "-preview.png"))) return 1;
  if (full.isNull() || cropped.isNull() || cropped != full.copy(central) || returned != full) {
    if (cropped.size() == central.size()) {
      int maximum = 0;
      qint64 changed = 0;
      for (int y = 0; y < cropped.height(); ++y) {
        for (int x = 0; x < cropped.width(); ++x) {
          const auto a = full.pixelColor(x + central.x(), y + central.y());
          const auto b = cropped.pixelColor(x, y);
          const auto delta = std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()), std::abs(a.blue() - b.blue())});
          maximum = std::max(maximum, delta);
          changed += delta != 0;
        }
      }
      std::cerr << "Changed RGB pixels=" << changed << " max channel difference=" << maximum << '\n';
    }
    std::cerr << "Full=" << full.width() << 'x' << full.height() << " cropped=" << cropped.width() << 'x' << cropped.height()
              << " returned=" << returned.width() << 'x' << returned.height() << " crop_equal=" << (cropped == full.copy(central))
              << " return_equal=" << (returned == full) << '\n';
    std::cerr << "Full-resolution crop pixels or return-to-full-frame pixels differ\n";
    return 1;
  }
  std::cout << "Full image " << full.width() << 'x' << full.height() << "; cropped "
            << cropped.width() << 'x' << cropped.height() << "; exact central pixels and restored frame passed\n";
  controller.updateCropRect(0.1, 0.2, 0.4, 0.3);
  controller.setCropZoom(3.0);
  if (!wait() || !close(controller.cropRectWidth(), 0.4 * 2.0 / 3.0) ||
      !close(controller.cropRectHeight(), 0.2) || !close(controller.cropRectX(), 1.0 / 6.0) ||
      !close(controller.cropRectY(), 0.25)) {
    std::cerr << "Explicit off-center crop must preserve its center when zooming\n";
    return 1;
  }
  const auto clamped = serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(controller));
  controller.setCropZoom(4.0);
  if (serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(controller)) != clamped) return 1;
  controller.setCropZoom(0.0);
  if (!wait() || !close(controller.cropZoom(), 1.0) || !close(controller.cropRectWidth(), 0.8) ||
      !close(controller.cropRectHeight(), 0.6) || !close(controller.cropRectX(), 0.0) ||
      !close(controller.cropRectY(), 0.05)) return 1;
  std::cout << "Off-center zoom preserves the center; zoom limits and image-edge constraints passed\n";
  std::cout << "First and repeated crop zoom, return to full frame and exact undo/redo passed\n";
  return 0;
}

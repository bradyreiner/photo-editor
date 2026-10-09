#include "app_bootstrap.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"
#include "photo_text_document_style.h"
#include "photo_text_rasterizer.h"
#include "photo_render_utils.h"
#include "creative_fonts.h"
#include <QGuiApplication>
#include <QFontDatabase>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QFuture>
#include <QPromise>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QPainter>
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <functional>
#include <iterator>
#include <limits>
#include <thread>
#include <vector>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static EditState state(const EditorPreviewController& controller) { return controller.currentState(); }
  static RenderRequest current_render_request(EditorPreviewController& controller, bool full_resolution) {
    return controller.currentRenderRequest(full_resolution);
  }
  static void perform_render_preview(
      EditorPreviewController& controller,
      bool schedule_full_resolution,
      int coalesced_request_count,
      const QString& reason) {
    controller.performRenderPreview(
        schedule_full_resolution, coalesced_request_count, reason);
  }
  static bool full_request_pending(const EditorPreviewController& controller) {
    return controller.full_request_pending_;
  }
  static RenderRequest pending_full_request(const EditorPreviewController& controller) {
    return controller.pending_full_request_;
  }
  static void flush_transaction_interactive_preview(
      EditorPreviewController& controller) {
    controller.flushEditTransactionInteractivePreview();
  }
  static QFuture<RenderResult> install_completed_interactive_result_before_delivery(
      EditorPreviewController& controller,
      RenderResult result) {
    QPromise<RenderResult> promise;
    promise.start();
    const auto future = promise.future();
    controller.interactive_render_result_pending_ = true;
    controller.interactive_render_watcher_.setFuture(future);
    std::thread producer(
        [promise = std::move(promise), result = std::move(result)]() mutable {
          promise.addResult(std::move(result));
          promise.finish();
        });
    producer.join();
    return future;
  }
  static bool interactive_render_result_pending(
      const EditorPreviewController& controller) {
    return controller.interactive_render_result_pending_;
  }
  static bool interactive_render_watcher_running(
      const EditorPreviewController& controller) {
    return controller.interactive_render_watcher_.isRunning();
  }
  static QFuture<RenderResult> interactive_render_future(
      const EditorPreviewController& controller) {
    return controller.interactive_render_watcher_.future();
  }
  static void stop_transaction_preview_timer(EditorPreviewController& controller) {
    controller.edit_transaction_interactive_preview_timer_.stop();
  }
  static quint64 edit_transaction_generation(const EditorPreviewController& controller) {
    return controller.edit_transaction_generation_;
  }
  static int preview_generation(const EditorPreviewController& controller) {
    return controller.preview_session_.generation();
  }
  static int preview_publication_revision(const EditorPreviewController& controller) {
    return controller.preview_session_.publicationRevision();
  }
  static QImage cached_published_preview_image(const EditorPreviewController& controller) {
    return controller.thumbnail_cache_session_.publishedPreviewImage();
  }
  static QString cached_published_preview_state_signature(const EditorPreviewController& controller) {
    return controller.thumbnail_cache_session_.publishedPreviewStateSignature();
  }
  static void advance_preview_generation(EditorPreviewController& controller) {
    static_cast<void>(controller.preview_session_.advanceGeneration());
  }
  static void advance_mask_work_revision(EditorPreviewController& controller) {
    static_cast<void>(controller.mask_session_.advanceWorkRevision());
  }
  static quint64 layer_source_content_revision(const EditorPreviewController& controller) {
    return controller.layer_source_content_revision_;
  }
  static void advance_layer_source_content_revision(EditorPreviewController& controller) {
    ++controller.layer_source_content_revision_;
  }
  static void handle_render_result(
      EditorPreviewController& controller,
      bool full_resolution,
      const RenderResult& result) {
    controller.handleRenderResult(full_resolution, result);
  }
  static bool native_preview_commit_allowed(
      const EditorPreviewController& controller,
      const RenderResult& result,
      bool accepted_transaction_feedback,
      bool full_resolution_path = false) {
    const EditorPreviewController::NativePreviewCommitContext context{
        .generation = result.generation,
        .full_resolution = full_resolution_path || result.full_resolution,
        .overlay_only = result.overlay_only,
        .success = result.success,
        .cancelled = result.cancelled,
        .accepted_transaction_feedback = accepted_transaction_feedback,
        .edit_transaction_generation = result.edit_transaction_generation,
        .source_asset_id = result.async_job.asset_id,
        .source_file_path = result.async_job.file_path,
        .layer_source_content_revision = result.layer_source_content_revision,
        .mask_work_revision = result.async_job.state_revision,
    };
    return controller.nativePreviewCommitAllowed(context);
  }
  static bool update_active_mask_definition(EditorPreviewController& controller, const QVariantMap& mask) {
    return controller.updateActiveMaskDefinition(mask);
  }
  static bool outline_worker_running(const EditorPreviewController& controller) {
    return controller.mask_worker_coordinator_.activeMaskOutlineWatcher().isRunning();
  }
  static int completed_outline_workers(const EditorPreviewController& controller) {
    return controller.mask_worker_coordinator_.activeMaskOutlineWorkerCompletedCount();
  }
};
}
using namespace photo_editor::desktop;
#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)
static RenderResult feedback_frame_for_request(const RenderRequest& request, const QString& marker) {
  RenderResult result;
  result.file_path = request.file_path;
  result.preview_source = QStringLiteral("test-feedback://%1").arg(marker);
  result.display_preview_source = result.preview_source;
  result.status = QStringLiteral("Synthetic feedback frame");
  result.render_reason = request.render_reason;
  result.interaction_mode = request.interaction_mode;
  result.render_mode = request.render_mode;
  result.dirty_domain = request.dirty_domain;
  result.dirty_stages = request.dirty_stages;
  result.full_resolution = request.full_resolution;
  result.generation = request.generation;
  result.edit_transaction_generation = request.edit_transaction_generation;
  result.layer_source_content_revision = request.layer_source_content_revision;
  result.max_edge = request.max_edge;
  result.source_width = 640;
  result.source_height = 512;
  result.output_width = request.max_edge;
  result.output_height = request.max_edge;
  result.async_job = request.async_job;
  result.preview_image = QImage(4, 4, QImage::Format_ARGB32);
  result.preview_image.fill(QColor(QStringLiteral("#ff%1").arg(
      QString::number(static_cast<quint32>(qHash(marker) & 0x00ffffffU), 16)
          .rightJustified(6, QLatin1Char('0')))));
  result.success = true;
  return result;
}
static bool wait_until(const std::function<bool()>& predicate) {
  QElapsedTimer timer; timer.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents,10);
    if (predicate()) return true;
    QThread::msleep(5);
  } while(timer.elapsed()<20000);
  return false;
}

static int run_metal_showcase_only(const QString& evidence_dir, const QString& temp_dir) {
  CHECK(!evidence_dir.trimmed().isEmpty() && QDir().mkpath(evidence_dir),
      "Create the metal typography showcase evidence directory");
  static const std::array<QString, 10> material_ids{{
      QStringLiteral("polished_chrome"), QStringLiteral("brushed_silver"),
      QStringLiteral("warm_gold"), QStringLiteral("copper"),
      QStringLiteral("dark_steel"), QStringLiteral("champagne_gold"),
      QStringLiteral("rose_gold"), QStringLiteral("antique_brass"),
      QStringLiteral("titanium"), QStringLiteral("black_chrome")}};
  static const std::array<QString, 10> material_labels{{
      QStringLiteral("Polished Chrome"), QStringLiteral("Brushed Silver"),
      QStringLiteral("Warm Gold"), QStringLiteral("Copper"),
      QStringLiteral("Dark Steel"), QStringLiteral("Champagne Gold"),
      QStringLiteral("Rose Gold"), QStringLiteral("Antique Brass"),
      QStringLiteral("Satin Titanium"), QStringLiteral("Black Chrome")}};
  static const std::array<QString, 10> material_tints{{
      QStringLiteral("#ffe8edf2"), QStringLiteral("#ffc5cbd1"),
      QStringLiteral("#ffdba33b"), QStringLiteral("#ffc1784e"),
      QStringLiteral("#ff49525c"), QStringLiteral("#ffcfb47a"),
      QStringLiteral("#ffcf9286"), QStringLiteral("#ffa88445"),
      QStringLiteral("#ff88989f"), QStringLiteral("#ff343c49")}};

  static_cast<void>(register_creative_fonts());
  const auto available_families = QFontDatabase::families();
  QString showcase_family = QStringLiteral("Helvetica");
  for (const auto& candidate : {QStringLiteral("Outfit"), QStringLiteral("Avenir Next"),
           QStringLiteral("Arial"), QStringLiteral("Helvetica")}) {
    const auto match = std::find_if(available_families.cbegin(), available_families.cend(),
        [&](const auto& family) { return family.compare(candidate, Qt::CaseInsensitive) == 0; });
    if (match != available_families.cend()) {
      showcase_family = *match;
      break;
    }
  }

  const QSize canvas_size(1600, 900);
  QImage dark_background(canvas_size, QImage::Format_RGB32);
  dark_background.fill(QColor(QStringLiteral("#141922")));
  dark_background.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto source_path = QDir(evidence_dir).filePath(QStringLiteral("metal-showcase-background.png"));
  CHECK(dark_background.save(source_path), "Save the flat dark metal showcase background");

  auto provider = std::make_unique<EditorPreviewImageProvider>();
  auto controller = std::make_unique<EditorPreviewController>();
  controller->setImageProvider(provider.get());
  controller->loadAsset(QStringLiteral("looksy-metal-showcase"), source_path, {}, {}, {}, {}, {});
  CHECK(wait_until([&] { return !controller->displayPreviewSource().isEmpty() &&
      !controller->processing() && !controller->fullRenderRunning(); }),
      "Prepare the production controller for metal showcase rendering");
  CHECK(controller->addPointTextLayer(QStringLiteral("LOOKSY"), 190, 285,
      canvas_size.width(), canvas_size.height()),
      "Create a large editable LOOKSY title for metal material evidence");
  int title_layer_index = -1;
  for (qsizetype index = 0; index < controller->photoLayers().size(); ++index) {
    if (controller->photoLayers().at(index).toMap().value(QStringLiteral("text")).toString() ==
        QStringLiteral("LOOKSY")) {
      title_layer_index = static_cast<int>(index);
      break;
    }
  }
  CHECK(title_layer_index >= 0, "Find the editable LOOKSY showcase layer");

  constexpr int tile_width = 960;
  constexpr int tile_height = 540;
  constexpr int label_height = 42;
  QImage contact_sheet(tile_width * 2, (tile_height + label_height) * 5,
      QImage::Format_RGB32);
  contact_sheet.fill(QColor(QStringLiteral("#141922")));
  QPainter sheet_painter(&contact_sheet);
  sheet_painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  sheet_painter.setFont(QFont(showcase_family, 17, QFont::DemiBold));
  const auto metal_depth = 14.0;
  const auto metal_edge_softness = 1.6;
  int successful_exports = 0;
  for (std::size_t finish_index = 0; finish_index < material_ids.size(); ++finish_index) {
    const auto& material_id = material_ids[finish_index];
    const auto style_applied = controller->setPhotoLayerTypography(title_layer_index, {
        {QStringLiteral("family"), showcase_family},
        {QStringLiteral("size"), 280.0},
        {QStringLiteral("weight"), 800},
        {QStringLiteral("tracking"), 3.0},
        {QStringLiteral("color"), QStringLiteral("#ffffffff")},
        {QStringLiteral("material"), material_id},
        {QStringLiteral("materialTint"), material_tints.at(finish_index)},
        {QStringLiteral("materialBlur"), 0.0},
        {QStringLiteral("materialTransmission"), 0.9},
        {QStringLiteral("materialEdgeSoftness"), metal_edge_softness},
        {QStringLiteral("materialDepth"), metal_depth},
        {QStringLiteral("materialLightAngle"), -45.0}});
    CHECK(style_applied,
        "Apply the editable metal showcase style: " << material_id.toStdString());
    const auto styled_state = EditorPreviewControllerTestAccess::state(*controller);
    const auto styled_recipe = serialize_edit_recipe_json(styled_state);
    const auto decoded_recipe = parse_edit_recipe_json(styled_recipe);
    const auto decoded_style = decoded_recipe.layers.at(title_layer_index).toMap()
        .value(QStringLiteral("textTypography")).toMap();
    CHECK(validate_edit_recipe_json(styled_recipe).valid &&
        decoded_style.value(QStringLiteral("version")).toInt() == 9 &&
        decoded_style.value(QStringLiteral("material")).toString() == material_id &&
        decoded_style.value(QStringLiteral("family")).toString() == showcase_family &&
        decoded_style.value(QStringLiteral("size")).toDouble() == 280.0 &&
        decoded_style.value(QStringLiteral("materialDepth")).toDouble() == metal_depth &&
        decoded_style.value(QStringLiteral("materialLightAngle")).toDouble() == -45.0,
        "Metal showcase recipe retains editable font, depth and light settings: " << material_id.toStdString());

    const auto slug = QString(material_id).replace(QLatin1Char('_'), QLatin1Char('-'));
    const auto png_path = QDir(evidence_dir).filePath(QStringLiteral("looksy-%1.png").arg(slug));
    CHECK(controller->exportFlattenedPhoto(png_path),
        "Production flattened PNG export renders the showcase material: " << material_id.toStdString());
    const QImage rendered_png(png_path);
    CHECK(!rendered_png.isNull() && rendered_png.size() == canvas_size,
        "Showcase PNG has the full source dimensions: " << material_id.toStdString());

    const auto json_path = QDir(evidence_dir).filePath(QStringLiteral("looksy-%1.json").arg(slug));
    QFile recipe_file(json_path);
    CHECK(recipe_file.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "Open editable metal showcase recipe: " << material_id.toStdString());
    const auto recipe_bytes = styled_recipe.toUtf8();
    CHECK(recipe_file.write(recipe_bytes) == recipe_bytes.size(),
        "Write editable metal showcase recipe: " << material_id.toStdString());
    recipe_file.close();

    auto reopened_provider = std::make_unique<EditorPreviewImageProvider>();
    auto reopened_controller = std::make_unique<EditorPreviewController>();
    reopened_controller->setImageProvider(reopened_provider.get());
    reopened_controller->loadAsset(QStringLiteral("looksy-metal-showcase-%1").arg(material_id),
        source_path, {}, {}, {}, {}, styled_recipe);
    CHECK(wait_until([&] { return !reopened_controller->displayPreviewSource().isEmpty() &&
        !reopened_controller->processing() && !reopened_controller->fullRenderRunning(); }),
        "Restore the exact showcase recipe in a fresh production controller: " << material_id.toStdString());
    CHECK(EditorPreviewControllerTestAccess::state(*reopened_controller).layers == styled_state.layers,
        "Fresh controller restores the exact editable metal layer: " << material_id.toStdString());
    const auto reopened_png_path = QDir(temp_dir).filePath(QStringLiteral("metal-reopened-%1.png").arg(slug));
    CHECK(reopened_controller->exportFlattenedPhoto(reopened_png_path),
        "Reopened production controller exports the metal showcase: " << material_id.toStdString());
    const QImage reopened_png(reopened_png_path);
    CHECK(!reopened_png.isNull() && reopened_png == rendered_png,
        "Production PNG remains pixel-identical after exact controller reopen: " << material_id.toStdString());

    const auto tile_x = static_cast<int>(finish_index % 2) * tile_width;
    const auto tile_y = static_cast<int>(finish_index / 2) * (tile_height + label_height);
    sheet_painter.fillRect(QRect(tile_x, tile_y, tile_width, tile_height + label_height),
        QColor(QStringLiteral("#202733")));
    sheet_painter.drawImage(QRect(tile_x, tile_y, tile_width, tile_height), rendered_png);
    sheet_painter.setPen(QColor(QStringLiteral("#f0eee8")));
    sheet_painter.drawText(QRect(tile_x + 20, tile_y + tile_height, tile_width - 40, label_height),
        Qt::AlignVCenter | Qt::AlignLeft, material_labels.at(finish_index));
    ++successful_exports;
  }
  sheet_painter.end();
  CHECK(successful_exports == 10, "Render every editable metal finish for the showcase");
  CHECK(contact_sheet.save(QDir(evidence_dir).filePath(QStringLiteral("looksy-metal-showcase-contact-sheet.png"))),
      "Save the ten-finish LOOKSY metal showcase contact sheet");
  std::cout << "Production controller recipe/export evidence generated for all ten metal finishes in "
      << evidence_dir.toStdString() << '\n';
  return 0;
}

static int run_render_feedback_guard_tests(const QString& source_path) {
  auto provider = std::make_unique<EditorPreviewImageProvider>();
  auto controller = std::make_unique<EditorPreviewController>();
  controller->setImageProvider(provider.get());
  controller->loadAsset("typography-render-guard", source_path, {}, {}, {}, {}, {});
  CHECK(wait_until([&] { return !controller->displayPreviewSource().isEmpty() &&
      !controller->processing() && !controller->fullRenderRunning(); }), "Render-guard source preview");
  CHECK(controller->addPointTextLayer(QStringLiteral("Render feedback"), 80, 72, 640, 512),
      "Create text for render-publication guard tests");
  CHECK(wait_until([&] { return !controller->processing() && !controller->fullRenderRunning(); }),
      "Render-guard text preview settles");
  CHECK(controller->addPaintLayer(),
      "Create a valid paint layer for transform-draft paint-base coverage");
  CHECK(wait_until([&] { return !controller->processing() && !controller->fullRenderRunning(); }),
      "Render-guard paint layer settles");
  const auto layer_index = [&] {
    const auto layers = controller->photoLayers();
    for (qsizetype index = 0; index < layers.size(); ++index)
      if (layers.at(index).toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("text"))
        return static_cast<int>(index);
    return -1;
  };
  const auto prepare_stale_feedback = [&](RenderRequest* request, RenderResult* result,
                                           const QString& marker) {
    const auto index = layer_index();
    if (index < 0) return false;
    const auto layer = controller->photoLayers().at(index).toMap();
    const auto x = layer.value(QStringLiteral("x")).toInt();
    const auto y = layer.value(QStringLiteral("y")).toInt();
    const auto scale = layer.value(QStringLiteral("scale")).toDouble();
    const auto rotation = layer.value(QStringLiteral("rotation")).toDouble();
    controller->beginCoalescedEdit();
    if (!controller->setPhotoLayerTransform(index, x + 3, y + 2, scale, rotation)) return false;
    *request = EditorPreviewControllerTestAccess::current_render_request(*controller, false);
    if (request->async_job.job_kind != QStringLiteral("layer_composite") ||
        request->edit_transaction_generation == 0 ||
        request->edit_transaction_generation !=
            EditorPreviewControllerTestAccess::edit_transaction_generation(*controller))
      return false;
    if (!controller->setPhotoLayerTransform(index, x + 18, y + 11, scale, rotation)) return false;
    EditorPreviewControllerTestAccess::stop_transaction_preview_timer(*controller);
    *result = feedback_frame_for_request(*request, marker);
    return true;
  };
  const auto settled = [&] {
    return wait_until([&] { return !controller->processing() && !controller->fullRenderRunning(); });
  };

  const auto transform_layer_index = layer_index();
  CHECK(transform_layer_index >= 0,
        "Render-guard fixture keeps its text layer available for transform-draft coverage");
  const auto transform_layer =
      controller->photoLayers().at(transform_layer_index).toMap();
  controller->beginCoalescedEdit();
  CHECK(controller->setPhotoLayerTransform(
            transform_layer_index,
            transform_layer.value(QStringLiteral("x")).toInt() + 3,
            transform_layer.value(QStringLiteral("y")).toInt() + 2,
            transform_layer.value(QStringLiteral("scale"), 1.0).toDouble(),
            transform_layer.value(QStringLiteral("rotation")).toDouble()),
        "Start a real coalesced transform before drafting its first render");
  EditorPreviewControllerTestAccess::stop_transaction_preview_timer(*controller);
  EditorPreviewControllerTestAccess::perform_render_preview(
      *controller, false, 1, QStringLiteral("photo_layer_transform"));
  const auto transform_draft_request =
      EditorPreviewControllerTestAccess::current_render_request(*controller, false);
  CHECK(transform_draft_request.draft_interactive &&
            transform_draft_request.max_edge > 0 &&
            transform_draft_request.max_edge <= 1024 &&
            transform_draft_request.reuse_layer_transform_prefix &&
            transform_draft_request.require_cpu_display_image,
        "The first transform draft reuses its stable prefix while preserving the CPU paint base");
  controller->endCoalescedEdit();
  CHECK(EditorPreviewControllerTestAccess::full_request_pending(*controller),
        "Releasing a layer transform queues its settled full-resolution request");
  const auto released_transform_request =
      EditorPreviewControllerTestAccess::pending_full_request(*controller);
  CHECK(released_transform_request.full_resolution &&
            released_transform_request.max_edge == 0 &&
            !released_transform_request.draft_interactive &&
            !released_transform_request.reuse_layer_transform_prefix &&
            released_transform_request.require_cpu_display_image,
        "The release request returns to settled full resolution without draft-prefix reuse");
  CHECK(settled(), "Transform draft and release renders settle before stale-result guard cases");

  RenderRequest delivery_request;
  RenderResult delivery_result;
  CHECK(prepare_stale_feedback(
            &delivery_request, &delivery_result,
            QStringLiteral("finished-before-delivery")),
        "Capture a valid intermediate frame before exercising watcher delivery ordering");
  const auto delivery_publication_before =
      EditorPreviewControllerTestAccess::preview_publication_revision(*controller);
  const auto delivery_generation_before =
      EditorPreviewControllerTestAccess::preview_generation(*controller);
  const auto completed_interactive_future =
      EditorPreviewControllerTestAccess::install_completed_interactive_result_before_delivery(
          *controller, delivery_result);
  CHECK(completed_interactive_future.isFinished() &&
            !completed_interactive_future.isRunning() &&
            !EditorPreviewControllerTestAccess::interactive_render_watcher_running(*controller) &&
            EditorPreviewControllerTestAccess::interactive_render_result_pending(*controller) &&
            completed_interactive_future.result().preview_image == delivery_result.preview_image,
        "The synthetic watcher future is complete while its GUI result callback remains pending");
  EditorPreviewControllerTestAccess::flush_transaction_interactive_preview(*controller);
  const auto watched_future_after_flush =
      EditorPreviewControllerTestAccess::interactive_render_future(*controller);
  CHECK(EditorPreviewControllerTestAccess::preview_generation(*controller) ==
            delivery_generation_before &&
            EditorPreviewControllerTestAccess::interactive_render_result_pending(*controller) &&
            !EditorPreviewControllerTestAccess::interactive_render_watcher_running(*controller) &&
            watched_future_after_flush.isFinished() &&
            watched_future_after_flush.result().preview_image == delivery_result.preview_image,
        "A transaction flush waits for completed-but-unhandled delivery without advancing generation or replacing its result");
  EditorPreviewControllerTestAccess::stop_transaction_preview_timer(*controller);
  CHECK(wait_until([&] {
          return EditorPreviewControllerTestAccess::preview_publication_revision(*controller) ==
                     delivery_publication_before + 1 &&
                 !EditorPreviewControllerTestAccess::interactive_render_result_pending(*controller);
        }),
        "The queued watcher callback publishes its valid frame and clears the delivery guard");
  CHECK(controller->cancelCoalescedEdit(),
        "Cancel the pending-delivery fixture transaction after publication");
  CHECK(settled(), "Pending-delivery cancellation restores a settled controller");

  RenderRequest request;
  RenderResult result;
  CHECK(prepare_stale_feedback(&request, &result, QStringLiteral("active-frame")),
      "Capture an intermediate layer-composite request within an active transaction");
  const auto active_publication_before =
      EditorPreviewControllerTestAccess::preview_publication_revision(*controller);
  EditorPreviewControllerTestAccess::handle_render_result(*controller, false, result);
  CHECK(EditorPreviewControllerTestAccess::preview_publication_revision(*controller) ==
            active_publication_before + 1 &&
        controller->displayPreviewSource().contains(QStringLiteral("active-frame")) &&
        provider->image(QStringLiteral("edited")) == result.preview_image &&
        EditorPreviewControllerTestAccess::cached_published_preview_image(*controller).isNull() &&
        EditorPreviewControllerTestAccess::cached_published_preview_state_signature(*controller).isEmpty(),
      "A stale-signature frame publishes visibly without being cached as the newer current recipe");
  CHECK(EditorPreviewControllerTestAccess::native_preview_commit_allowed(
            *controller, result, true),
        "An accepted interactive frame may commit while its request is current");
  EditorPreviewControllerTestAccess::advance_preview_generation(*controller);
  CHECK(EditorPreviewControllerTestAccess::native_preview_commit_allowed(
            *controller, result, true),
        "An accepted intermediate native frame survives a newer render request in the same transaction");
  auto invalid_native_context = result;
  invalid_native_context.full_resolution = true;
  CHECK(!EditorPreviewControllerTestAccess::native_preview_commit_allowed(
            *controller, invalid_native_context, true),
        "A stale full-resolution result cannot use the interactive transaction exception");
  invalid_native_context = result;
  invalid_native_context.overlay_only = true;
  CHECK(!EditorPreviewControllerTestAccess::native_preview_commit_allowed(
            *controller, invalid_native_context, true),
        "A stale overlay result cannot use the layer-composite exception");
  invalid_native_context = result;
  ++invalid_native_context.edit_transaction_generation;
  CHECK(!EditorPreviewControllerTestAccess::native_preview_commit_allowed(
            *controller, invalid_native_context, true),
        "A stale native frame from another transaction token is rejected");
  invalid_native_context = result;
  invalid_native_context.async_job.asset_id = QStringLiteral("different-asset");
  CHECK(!EditorPreviewControllerTestAccess::native_preview_commit_allowed(
            *controller, invalid_native_context, true),
        "A stale native frame for another asset is rejected");
  invalid_native_context = result;
  invalid_native_context.async_job.file_path = QStringLiteral("/different/source.png");
  CHECK(!EditorPreviewControllerTestAccess::native_preview_commit_allowed(
            *controller, invalid_native_context, true),
        "A stale native frame for another source path is rejected");
  invalid_native_context = result;
  ++invalid_native_context.async_job.state_revision;
  CHECK(!EditorPreviewControllerTestAccess::native_preview_commit_allowed(
            *controller, invalid_native_context, true),
        "A stale native frame after mask work changes is rejected");
  CHECK(controller->cancelCoalescedEdit(), "Cancel the accepted-frame fixture transaction");
  CHECK(!EditorPreviewControllerTestAccess::native_preview_commit_allowed(
            *controller, result, true),
        "A queued intermediate native frame is rejected after cancellation");
  CHECK(settled(), "Accepted-frame fixture restores cleanly");

  CHECK(prepare_stale_feedback(&request, &result, QStringLiteral("released-frame")),
      "Capture a frame for release rejection");
  controller->endCoalescedEdit();
  result.generation = EditorPreviewControllerTestAccess::preview_generation(*controller);
  result.async_job.generation = result.generation;
  CHECK(!EditorPreviewControllerTestAccess::native_preview_commit_allowed(
            *controller, result, true),
        "An accepted transaction frame is rejected after release even if its generation is still current");
  const auto released_publication_before =
      EditorPreviewControllerTestAccess::preview_publication_revision(*controller);
  EditorPreviewControllerTestAccess::handle_render_result(*controller, false, result);
  CHECK(EditorPreviewControllerTestAccess::preview_publication_revision(*controller) ==
            released_publication_before, "A transaction frame is rejected after release");
  CHECK(settled(), "Released-frame final render settles");

  CHECK(prepare_stale_feedback(&request, &result, QStringLiteral("cancelled-frame")),
      "Capture a frame for cancellation rejection");
  CHECK(controller->cancelCoalescedEdit(), "Cancel the frame transaction");
  result.generation = EditorPreviewControllerTestAccess::preview_generation(*controller);
  result.async_job.generation = result.generation;
  CHECK(!EditorPreviewControllerTestAccess::native_preview_commit_allowed(
            *controller, result, true),
        "An accepted transaction frame is rejected after cancellation even if its generation is current");
  const auto cancelled_publication_before =
      EditorPreviewControllerTestAccess::preview_publication_revision(*controller);
  EditorPreviewControllerTestAccess::handle_render_result(*controller, false, result);
  CHECK(EditorPreviewControllerTestAccess::preview_publication_revision(*controller) ==
            cancelled_publication_before, "A transaction frame is rejected after cancellation");
  CHECK(settled(), "Cancelled-frame fixture restores cleanly");

  CHECK(prepare_stale_feedback(&request, &result, QStringLiteral("new-transaction-frame")),
      "Capture a frame before a replacement transaction");
  controller->endCoalescedEdit();
  controller->beginCoalescedEdit();
  CHECK(controller->setPhotoLayerTransform(layer_index(), 102, 91, 1.0, 0.0),
      "Start the replacement transaction");
  CHECK(!EditorPreviewControllerTestAccess::native_preview_commit_allowed(
            *controller, result, true),
        "A queued intermediate native frame is rejected by a replacement transaction token");
  result.generation = EditorPreviewControllerTestAccess::preview_generation(*controller);
  result.async_job.generation = result.generation;
  const auto replacement_publication_before =
      EditorPreviewControllerTestAccess::preview_publication_revision(*controller);
  EditorPreviewControllerTestAccess::handle_render_result(*controller, false, result);
  CHECK(EditorPreviewControllerTestAccess::preview_publication_revision(*controller) ==
            replacement_publication_before,
      "A frame from an earlier transaction is rejected inside the replacement transaction");
  CHECK(controller->cancelCoalescedEdit(), "Cancel the replacement transaction");
  CHECK(settled(), "Replacement-transaction fixture restores cleanly");

  CHECK(prepare_stale_feedback(&request, &result, QStringLiteral("mask-revision-frame")),
      "Capture a frame for mask-revision rejection");
  EditorPreviewControllerTestAccess::advance_mask_work_revision(*controller);
  const auto mask_publication_before =
      EditorPreviewControllerTestAccess::preview_publication_revision(*controller);
  EditorPreviewControllerTestAccess::handle_render_result(*controller, false, result);
  CHECK(EditorPreviewControllerTestAccess::preview_publication_revision(*controller) ==
            mask_publication_before,
      "A transaction frame is rejected after the mask work revision changes");
  CHECK(controller->cancelCoalescedEdit(), "Cancel the mask-revision fixture transaction");
  CHECK(settled(), "Mask-revision fixture restores cleanly");

  CHECK(prepare_stale_feedback(&request, &result, QStringLiteral("linked-source-stale-frame")),
      "Capture an intermediate frame before a linked-source content revision");
  const auto stale_source_revision = result.layer_source_content_revision;
  EditorPreviewControllerTestAccess::advance_layer_source_content_revision(*controller);
  CHECK(EditorPreviewControllerTestAccess::layer_source_content_revision(*controller) > stale_source_revision,
      "Linked-source fixture advances the controller source-content epoch");
  CHECK(result.generation == EditorPreviewControllerTestAccess::preview_generation(*controller),
      "The linked-source stale-frame case keeps its render generation current");
  CHECK(!EditorPreviewControllerTestAccess::native_preview_commit_allowed(*controller, result, true),
      "A queued native frame from before a linked-source update is rejected inside the same transaction");
  const auto stale_source_publication_before =
      EditorPreviewControllerTestAccess::preview_publication_revision(*controller);
  EditorPreviewControllerTestAccess::handle_render_result(*controller, false, result);
  CHECK(EditorPreviewControllerTestAccess::preview_publication_revision(*controller) ==
            stale_source_publication_before,
      "A CPU preview from before a linked-source update is rejected");

  const auto current_source_revision =
      EditorPreviewControllerTestAccess::layer_source_content_revision(*controller);
  const auto current_source_request =
      EditorPreviewControllerTestAccess::current_render_request(*controller, false);
  CHECK(current_source_request.async_job.job_kind == QStringLiteral("layer_composite") &&
            current_source_request.layer_source_content_revision == current_source_revision,
      "A newly captured layer-composite request carries the current linked-source epoch");
  const auto current_source_result = feedback_frame_for_request(
      current_source_request, QStringLiteral("linked-source-current-frame"));
  CHECK(EditorPreviewControllerTestAccess::native_preview_commit_allowed(
            *controller, current_source_result, true),
      "A current-epoch native frame remains eligible during the active transform");
  const auto current_source_publication_before =
      EditorPreviewControllerTestAccess::preview_publication_revision(*controller);
  EditorPreviewControllerTestAccess::handle_render_result(*controller, false, current_source_result);
  CHECK(EditorPreviewControllerTestAccess::preview_publication_revision(*controller) ==
            current_source_publication_before + 1 &&
            controller->displayPreviewSource().contains(QStringLiteral("linked-source-current-frame")),
      "A freshly captured current-epoch CPU preview still publishes inside the active transform");
  CHECK(controller->cancelCoalescedEdit(), "Cancel the linked-source epoch fixture transaction");
  CHECK(settled(), "Linked-source epoch fixture settles");

  CHECK(prepare_stale_feedback(&request, &result, QStringLiteral("asset-frame")),
      "Capture a frame for asset identity rejection");
  result.async_job.asset_id = QStringLiteral("different-asset");
  const auto asset_publication_before =
      EditorPreviewControllerTestAccess::preview_publication_revision(*controller);
  EditorPreviewControllerTestAccess::handle_render_result(*controller, false, result);
  CHECK(EditorPreviewControllerTestAccess::preview_publication_revision(*controller) ==
            asset_publication_before, "A transaction frame is rejected for a different source asset");
  CHECK(controller->cancelCoalescedEdit(), "Cancel the source-identity fixture transaction");
  CHECK(settled(), "Source-identity fixture restores cleanly");

  CHECK(prepare_stale_feedback(&request, &result, QStringLiteral("path-frame")),
      "Capture a frame for source-path rejection");
  result.async_job.file_path = QStringLiteral("/different/source.png");
  const auto path_publication_before =
      EditorPreviewControllerTestAccess::preview_publication_revision(*controller);
  EditorPreviewControllerTestAccess::handle_render_result(*controller, false, result);
  CHECK(EditorPreviewControllerTestAccess::preview_publication_revision(*controller) ==
            path_publication_before, "A transaction frame is rejected for a different source path");
  CHECK(controller->cancelCoalescedEdit(), "Cancel the source-path fixture transaction");
  CHECK(settled(), "Source-path fixture restores cleanly");

  CHECK(prepare_stale_feedback(&request, &result, QStringLiteral("generation-frame")),
      "Capture a frame for preview-generation rejection");
  EditorPreviewControllerTestAccess::advance_preview_generation(*controller);
  const auto generation_publication_before =
      EditorPreviewControllerTestAccess::preview_publication_revision(*controller);
  EditorPreviewControllerTestAccess::handle_render_result(*controller, false, result);
  CHECK(EditorPreviewControllerTestAccess::preview_publication_revision(*controller) ==
            generation_publication_before,
      "A transaction frame is rejected after the current preview generation changes");
  CHECK(controller->cancelCoalescedEdit(), "Cancel the generation fixture transaction");
  CHECK(settled(), "Generation fixture restores cleanly");

  CHECK(prepare_stale_feedback(&request, &result, QStringLiteral("full-resolution-frame")),
      "Capture an interactive frame for full-resolution rejection");
  result.full_resolution = true;
  result.render_mode = QStringLiteral("max_preview");
  const auto full_resolution_publication_before =
      EditorPreviewControllerTestAccess::preview_publication_revision(*controller);
  EditorPreviewControllerTestAccess::handle_render_result(*controller, true, result);
  CHECK(EditorPreviewControllerTestAccess::preview_publication_revision(*controller) ==
            full_resolution_publication_before,
      "An interactive transaction frame cannot publish through the full-resolution path");
  CHECK(controller->cancelCoalescedEdit(), "Cancel the full-resolution fixture transaction");
  CHECK(settled(), "Full-resolution fixture restores cleanly");
  std::cout << "Coalesced layer-composite intermediate publication guards passed\n";
  return 0;
}

int main(int argc, char** argv) {
  qputenv("QT_QUICK_CONTROLS_STYLE","Basic");
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE","1");
  bool typography_only = false;
  bool metal_showcase_only = false;
  for (int index = 1; index < argc; ++index) {
    const auto argument = QString::fromLocal8Bit(argv[index]);
    if (argument == QStringLiteral("--typography-only")) {
      typography_only = true;
    } else if (argument == QStringLiteral("--metal-showcase-only")) {
      metal_showcase_only = true;
    }
  }
  QGuiApplication app(argc,argv);
  qmlRegisterType<PhotoTextDocumentStyle>("PhotoEditor",1,0,"PhotoTextDocumentStyle");
  QTemporaryDir temp;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR",temp.path().toUtf8());
  CHECK(AppBootstrap::ensureRuntimeDirectories(),"Runtime directories");
  if (metal_showcase_only) {
    auto evidence_dir = qEnvironmentVariable("PHOTO_TEXT_EVIDENCE_DIR");
    if (evidence_dir.isEmpty())
      evidence_dir = QDir(temp.path()).filePath(QStringLiteral("metal-showcase-evidence"));
    return run_metal_showcase_only(evidence_dir, temp.path());
  }
  const auto path=qEnvironmentVariable("PHOTO_TEXT_TEST_SOURCE");
  QImage source=path.isEmpty()?QImage{}:QImage(path);
  const bool photographic=!source.isNull();
  CHECK(path.isEmpty() || photographic,"Photograph must load");
  if (photographic) {
    const auto scaled=source.scaled(640,512,Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation);
    source=scaled.copy((scaled.width()-640)/2, (scaled.height()-512)/2, 640, 512);
  }
  else {source=QImage(640,512,QImage::Format_RGB32);source.fill(QColor("#304050"));}
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto source_path=temp.filePath("source.png");CHECK(source.save(source_path),"Source fixture");
  if (!typography_only) {
    CHECK(run_render_feedback_guard_tests(source_path) == 0,
        "Render feedback transaction guard tests");
  }
  QQmlEngine engine;
  auto* provider=new EditorPreviewImageProvider;
  engine.addImageProvider("editor-preview",provider);
  auto controller=std::make_unique<EditorPreviewController>();
  controller->setImageProvider(provider);
  controller->loadAsset("typography-proof",source_path,{},{},{},{},{});
  CHECK(wait_until([&]{return !controller->displayPreviewSource().isEmpty() && !controller->processing() && !controller->fullRenderRunning();}),"Source preview");
  const auto state=[&]{return EditorPreviewControllerTestAccess::state(*controller);};
  QString selected_text_layer_id;
  const auto text_layer_index=[&] {
    const auto layers = controller->photoLayers();
    if (!selected_text_layer_id.isEmpty()) {
      for (qsizetype index = 0; index < layers.size(); ++index) {
        const auto layer = layers.at(index).toMap();
        if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("text") &&
            layer.value(QStringLiteral("id")).toString() == selected_text_layer_id)
          return static_cast<int>(index);
      }
    }
    for (qsizetype index = 0; index < layers.size(); ++index)
      if (layers.at(index).toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("text"))
        return static_cast<int>(index);
    return -1;
  };
  engine.rootContext()->setContextProperty("testController",controller.get());
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import QtQuick.Controls
import "../apps/desktop/qml/components"
Rectangle {
    id: harness
    width: 1024; height: 650; color: "#20242b"
    property bool creating: true
    property string selectedTextLayerId: ""
    property string secondarySelectedTextLayerId: ""
    property bool multiTargetSelection: false
    property int baselineEditRequestCount: 0
    property bool lastBaselineEditRequestValue: false
    readonly property int selectedTextLayerIndex: {
        let firstTextIndex = -1
        for (let i = 0; i < testController.photoLayers.length; ++i) {
            const layer = testController.photoLayers[i]
            if (layer.kind !== "text") continue
            if (firstTextIndex < 0) firstTextIndex = i
            if (harness.selectedTextLayerId.length && String(layer.id || "") === harness.selectedTextLayerId)
                return i
        }
        return firstTextIndex
    }
    readonly property var selectedTextLayerIndices: {
        const indices = []
        if (harness.multiTargetSelection) {
            for (let i = 0; i < testController.photoLayers.length; ++i)
                if (testController.photoLayers[i].kind === "text" &&
                    (String(testController.photoLayers[i].id || "") === harness.selectedTextLayerId ||
                     String(testController.photoLayers[i].id || "") === harness.secondarySelectedTextLayerId))
                    indices.push(i)
            return indices
        }
        return harness.selectedTextLayerIndex >= 0 ? [harness.selectedTextLayerIndex] : []
    }
    readonly property var selectedLayer: selectedTextLayerIndex >= 0 && testController.photoLayers.length
        ? testController.photoLayers[selectedTextLayerIndex] : ({})
    Image { x: 340; y: 20; width: 640; height: 512; source: testController.displayPreviewSource; cache: false }
    ScrollView {
        objectName: "typographyProofScroll"
        x: 12; y: 12; width: 305; height: 620; clip: true
        PhotoTypographyPanel {
            objectName: "typographyPanel"
            width: 285
            controller: testController; layerIndex: harness.selectedTextLayerIndex; layerData: harness.selectedLayer
            selectedLayerIndices: harness.selectedTextLayerIndices
            visible: testController.photoLayerCount > 0
            expanded: true
            onBaselineEditingRequested: function(active) {
                harness.baselineEditRequestCount += 1
                harness.lastBaselineEditRequestValue = active
                editor.pathHandlesActive = active
            }
        }
    }
    Item { id: creatorControls; x: 12; y: 12; width: 305; height: 620 }
    PhotoTextCreateOverlay {
        id: creator
        controlsParent: creatorControls
        anchors.fill: parent
        active: harness.creating && !editor.editing
        imageX: 340; imageY: 20; imageWidth: 640; imageHeight: 512
        canvasWidth: 640; canvasHeight: 512
        onPointRequested: function(x,y) {
            editor.beginCreation({x:x,y:y,width:512,height:128,pointText:true,canvasWidth:640,canvasHeight:512},creator)
        }
        onBoxRequested: function(x,y,width,height) {
            editor.beginCreation({x:x,y:y,width:width,height:height,canvasWidth:640,canvasHeight:512},creator)
        }
    }
    PhotoTextOverlay {
        id: editor
        objectName: "textEditor"
        anchors.fill: parent
        controller: testController; layerIndex: harness.selectedTextLayerIndex; layerData: harness.selectedLayer
        documentId: "typography-proof"; active: true
        transformFrame: ({frameX: 340+Number(harness.selectedLayer.x||0),frameY:20+Number(harness.selectedLayer.y||0),
            frameWidth:Number(harness.selectedLayer.pixelWidth||512),frameHeight:Number(harness.selectedLayer.pixelHeight||128),layerRotation:0})
        onEditFinished: function(committed,created) { harness.creating=false }
    }
}
)QML",QUrl::fromLocalFile(QStringLiteral(PHOTO_TEXT_TEST_ROOT "/tests/typography-workflow.qml")));
  std::unique_ptr<QObject> root(component.create());CHECK(root,component.errorString().toStdString());
  QQuickWindow window;window.resize(1024,650);
  qobject_cast<QQuickItem*>(root.get())->setParentItem(window.contentItem());window.show();window.requestActivate();
  CHECK(wait_until([&]{return window.isExposed();}),"Native window exposed");
  const auto settle=[] {QElapsedTimer timer;timer.start();while(timer.elapsed()<150)QCoreApplication::processEvents(QEventLoop::AllEvents,10);};
  const auto mouse=[&](QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons buttons) {
    QMouseEvent event(type,p,window.mapToGlobal(p.toPoint()),button,buttons,Qt::NoModifier);
    QCoreApplication::sendEvent(&window,&event);QCoreApplication::processEvents();
  };
  const auto click=[&](QPointF p) {mouse(QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton);settle();};
  const auto drag=[&](QPointF from,QPointF to) {
    mouse(QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
    mouse(QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton);
    settle();
  };
  const auto key=[&](int code,Qt::KeyboardModifiers modifiers,const QString& text=QString()) {
    QKeyEvent event(QEvent::KeyPress,code,modifiers,text);QCoreApplication::sendEvent(&window,&event);settle();
  };
  const auto find=[&](const QString& name) {
    QSet<QObject*> visited;
    std::function<QObject*(QObject*)> search=[&](QObject* object)->QObject* {
      if(!object || visited.contains(object))return nullptr;visited.insert(object);
      if(object->objectName()==name)return object;
      for(auto* child:object->children())if(auto* found=search(child))return found;
      if(auto* item=qobject_cast<QQuickItem*>(object))for(auto* child:item->childItems())if(auto* found=search(child))return found;
      return nullptr;
    };
    return search(root.get());
  };
  const auto reveal_control=[&](QQuickItem* item) {
    if (!item) return;
    settle();
    for (auto* ancestor=item->parentItem(); ancestor; ancestor=ancestor->parentItem()) {
      if (ancestor->metaObject()->indexOfProperty("contentY") < 0) continue;
      const auto position=item->mapToItem(ancestor, QPointF(item->width()/2, item->height()/2));
      const auto maximum=std::max(0.0, ancestor->property("contentHeight").toDouble()-ancestor->height());
      ancestor->setProperty("contentY", std::clamp(ancestor->property("contentY").toDouble()+
          position.y()-ancestor->height()/2, 0.0, maximum));
      settle();
    }
  };
  const auto click_control=[&](QQuickItem* item) {
    reveal_control(item);
    click(item->mapToScene(QPointF(item->width()/2, item->height()/2)));
  };
  const auto set_disclosure=[&](const QString& name, bool expanded) {
    settle();
    auto* item=qobject_cast<QQuickItem*>(find(name));
    if(!item || !item->isVisible())return false;
    if(item->property("active").toBool()!=expanded)
      click_control(item);
    return item->property("active").toBool()==expanded;
  };
  auto* editor=find("textEditor");auto* input=find("photoCanvasTextInput");CHECK(editor && input,"Text controls");
  const auto evidence=qEnvironmentVariable("PHOTO_TEXT_EVIDENCE_DIR");
  if(!evidence.isEmpty())CHECK(QDir().mkpath(evidence),"Evidence directory");
  if (!evidence.isEmpty()) CHECK(source.save(evidence + "/material-source.png"), "Save material source fixture");
  QString variable_family, variable_face, variable_tag;
  QVariantMap variable_axis;
  struct VariableFontProof { QString family, face, tag; QVariantMap axis; QImage base, varied; };
  std::vector<VariableFontProof> variable_font_proofs;
  for (const auto& family : QFontDatabase::families()) {
    if (std::any_of(variable_font_proofs.cbegin(), variable_font_proofs.cend(), [&](const auto& proof) {
          return proof.family.compare(family, Qt::CaseInsensitive) == 0;
        })) continue;
    const auto faces = QStringList{QString()} + QFontDatabase::styles(family);
    for (const auto& face : faces) {
      const auto axes = controller->availablePhotoTextAxes(family, face);
      const auto found = std::find_if(axes.cbegin(), axes.cend(), [](const auto& item) {
        const auto axis = item.toMap();
        return axis.value("maximum").toDouble() > axis.value("minimum").toDouble();
      });
      if (found == axes.cend()) continue;
      const auto axis=found->toMap();
      PhotoTextStyle default_axis_style;
      default_axis_style.family=family;
      default_axis_style.face=face;
      const auto baseline=rasterize_photo_text(QStringLiteral("0123456789"), default_axis_style,
          QSizeF(400,96), QSize(800,192));
      auto varied_axis_style=default_axis_style;
      const auto tag=axis.value("tag").toString();
      varied_axis_style.axes.insert(tag,axis.value("maximum").toDouble());
      const auto varied=rasterize_photo_text(QStringLiteral("0123456789"),varied_axis_style,
          QSizeF(400,96),QSize(800,192));
      if (!baseline.error.isEmpty() || !varied.error.isEmpty() || baseline.image==varied.image) continue;
      variable_font_proofs.push_back({family,face,tag,axis,baseline.image,varied.image});
      break;
    }
    if (variable_font_proofs.size() == 3) break;
  }
  if (!variable_font_proofs.empty()) {
    variable_family=variable_font_proofs.front().family;
    variable_face=variable_font_proofs.front().face;
    variable_axis=variable_font_proofs.front().axis;
    variable_tag=variable_font_proofs.front().tag;
    for (std::size_t index=0; index<variable_font_proofs.size(); ++index) {
      const auto& proof=variable_font_proofs[index];
      if (!evidence.isEmpty()) {
        const auto suffix=QString::number(static_cast<qulonglong>(index+1));
        CHECK(proof.base.save(evidence + "/variable-axis-" + suffix + "-default.png"), "Save default variable-font raster");
        CHECK(proof.varied.save(evidence + "/variable-axis-" + suffix + "-varied.png"), "Save varied variable-font raster");
      }
      std::cout << "Variable font proof: " << proof.family.toStdString() << " " << proof.tag.toStdString() << "\n";
    }
  } else {
    std::cout << "Variable font proof skipped: host has no axis metadata exposed by Qt\n";
  }
  click({390,110});CHECK(editor->property("editing").toBool(),"Native click begins point text");
  key(Qt::Key_unknown,Qt::NoModifier,"City light");
  const auto first_width=editor->property("logicalWidth").toDouble();
  key(Qt::Key_unknown,Qt::NoModifier," / evening");
  CHECK(editor->property("logicalWidth").toDouble()>first_width,"Point draft expands while typing");
  if(!evidence.isEmpty())CHECK(window.grabWindow().save(evidence+"/point-draft-native.png"),"Point draft screenshot");
  key(Qt::Key_Return,Qt::ControlModifier);
  CHECK(!editor->property("editing").toBool() && controller->photoLayerCount()==1,"Native point Done creates one layer");
  settle();
  const auto created_text_targets = root->property("selectedTextLayerIndices").toList();
  CHECK(root->property("selectedTextLayerIndex").toInt() == 0 &&
      created_text_targets.size() == 1 && created_text_targets.front().toInt() == 0,
      "A newly created text layer becomes the sole inspector target");
  auto point=state().layers[0].toMap();
  CHECK(point.value("text")=="City light / evening" && point.value("textTypography").toMap().value("mode")=="point",
      "Point text stores unwrapped mode");
  const auto fresh_text_id = point.value("id").toString();
  const auto fresh_text_material = controller->photoCompositionCommand(QVariantMap{
      {QStringLiteral("assetId"), QStringLiteral("typography-proof")},
      {QStringLiteral("operation"), QStringLiteral("typography")},
      {QStringLiteral("layerId"), fresh_text_id},
      {QStringLiteral("values"), QVariantMap{
          {QStringLiteral("family"), QStringLiteral("Georgia")}, {QStringLiteral("size"), 42.0},
          {QStringLiteral("material"), QStringLiteral("frosted_glass")},
          {QStringLiteral("materialTint"), QStringLiteral("#ffdce4e6")},
          {QStringLiteral("materialBlur"), 2.0}, {QStringLiteral("materialTransmission"), 0.6},
          {QStringLiteral("materialEdgeSoftness"), 0.6}, {QStringLiteral("materialDepth"), 4.0},
          {QStringLiteral("materialLightAngle"), -45.0}}}});
  CHECK(fresh_text_material.value("ok").toBool(),
      "Production composition typography upgrades a fresh point-text style to version-8 glass controls: "
          << fresh_text_material.value("error").toString().toStdString() << " status="
          << controller->status().toStdString());
  const auto fresh_glass_style = controller->photoLayerTypography(text_layer_index());
  CHECK(fresh_glass_style.value("version").toInt() == 8 &&
      fresh_glass_style.value("mode").toString() == QStringLiteral("point") &&
      fresh_glass_style.value("material").toString() == QStringLiteral("frosted_glass") &&
      fresh_glass_style.value("axes").toMap().isEmpty(),
      "Glass-style upgrade supplies the empty variable-axis object required by version-8 text recipes");
  const auto advanced_point_state = state();
  const auto advanced_point_layer = advanced_point_state.layers.at(0).toMap();
  auto expected_advanced_paragraph_style = fresh_glass_style;
  expected_advanced_paragraph_style.insert(QStringLiteral("mode"), QStringLiteral("paragraph"));
  const auto advanced_box_x = advanced_point_layer.value(QStringLiteral("x")).toInt() + 12;
  const auto advanced_box_y = advanced_point_layer.value(QStringLiteral("y")).toInt() + 8;
  CHECK(controller->resizePhotoLayerTextBox(0, advanced_box_x, advanced_box_y, 240, 124),
      "Resize advanced version-8 point text into a paragraph box");
  const auto advanced_paragraph_state = state();
  const auto advanced_paragraph_layer = advanced_paragraph_state.layers.at(0).toMap();
  CHECK(controller->photoLayerTypography(0) == expected_advanced_paragraph_style &&
      advanced_paragraph_layer.value(QStringLiteral("textTypography")).toMap() == expected_advanced_paragraph_style &&
      advanced_paragraph_layer.value(QStringLiteral("text")).toString() == advanced_point_layer.value(QStringLiteral("text")).toString() &&
      advanced_paragraph_layer.value(QStringLiteral("fontSize")).toDouble() == advanced_point_layer.value(QStringLiteral("fontSize")).toDouble() &&
      advanced_paragraph_layer.value(QStringLiteral("pixelWidth")).toInt() == 240 &&
      advanced_paragraph_layer.value(QStringLiteral("pixelHeight")).toInt() == 124 &&
      advanced_paragraph_layer.value(QStringLiteral("x")).toInt() == advanced_box_x &&
      advanced_paragraph_layer.value(QStringLiteral("y")).toInt() == advanced_box_y &&
      advanced_paragraph_layer.value(QStringLiteral("scale")).toDouble() == advanced_point_layer.value(QStringLiteral("scale")).toDouble() &&
      advanced_paragraph_layer.value(QStringLiteral("rotation")).toDouble() == advanced_point_layer.value(QStringLiteral("rotation")).toDouble() &&
      validate_edit_recipe_json(serialize_edit_recipe_json(advanced_paragraph_state)).valid,
      "Advanced box resize preserves version-8 material, axes, font, and transform data while changing only geometry and mode");
  controller->undo();
  CHECK(state().layers == advanced_point_state.layers,
      "Advanced point-to-paragraph resize is one exact undo step");
  controller->undo();
  CHECK(state().layers[0].toMap() == point, "Undo restores the fresh editable point text exactly");
  if (!variable_family.isEmpty()) {
    CHECK(set_disclosure("photoTypographySpacingToggle", true),
        "Open the user facing spacing section to reach variable font controls");
    CHECK(set_disclosure("photoTypographyVariableAxesToggle", true),
        "Open the variable font controls before checking the selected axis");
    CHECK(!controller->setPhotoLayerTypography(text_layer_index(), {{"axes", QStringLiteral("not-a-map")}}) &&
        !controller->setPhotoLayerTypography(text_layer_index(), {{"axes", QVariantMap{{"zzzz", 42.0}}}}),
        "Reject malformed and unsupported font axes without changing the text layer");
    CHECK(controller->setPhotoLayerTypography(text_layer_index(), {{"family", variable_family}, {"face", variable_face},
        {"axes", QVariantMap{{variable_tag, variable_axis.value("maximum")}}}}),
        "Apply a font-supported variable axis through the native typography model");
    CHECK(wait_until([&] {
      auto* axis_field = find("photoTypographyAxis_" + variable_tag);
      return axis_field && axis_field->property("visible").toBool();
    }), "Variable font axis control appears for the selected face");
    CHECK(controller->photoLayerTypography(text_layer_index()).value("axes").toMap().value(variable_tag).toDouble() ==
        variable_axis.value("maximum").toDouble(), "Variable axis is persisted on the editable text layer");
    CHECK(wait_until([&] { return !controller->processing() && !controller->fullRenderRunning(); }),
        "Variable font preview settles");
    reveal_control(qobject_cast<QQuickItem*>(find("photoTypographyAxis_" + variable_tag)));
    settle();
    if (!evidence.isEmpty())
      CHECK(window.grabWindow().save(evidence + "/variable-axes-native.png"),
          "Capture the active variable font controls");
    controller->undo();
    CHECK(state().layers[0].toMap() == point, "Variable font axis edit is a single undo step");
    CHECK(set_disclosure("photoTypographyVariableAxesToggle", false) &&
        set_disclosure("photoTypographySpacingToggle", false),
        "Close advanced typography sections before the normal editing screenshot");
  }
  CHECK(controller->photoLayerTextLayoutInfo(text_layer_index()).value("lineCount").toInt()==1,"Point text stays on one line");
  auto contour_style = PhotoTextStyle{};
  contour_style.family = QStringLiteral("Helvetica");
  contour_style.size = 54;
  contour_style.point_text = true;
  contour_style.path_points = {QPointF(0.06, 0.72), QPointF(0.28, 0.38),
      QPointF(0.54, 0.46), QPointF(0.77, 0.24), QPointF(0.94, 0.34)};
  contour_style.path_start = 0.08;
  contour_style.path_end = 0.92;
  for (const auto& radii : {QSizeF(215, 215), QSizeF(250, 160)}) {
    auto analytic_style = contour_style;
    analytic_style.path_closed = true;
    analytic_style.path_points.clear();
    constexpr int control_count = 128;
    constexpr double tau = 6.28318530717958647692;
    const QPointF center(320, 256);
    for (int i = 0; i < control_count; ++i) {
      const auto angle = tau * i / control_count;
      analytic_style.path_points.push_back({(center.x() + radii.width() * std::cos(angle)) / 640.0,
          (center.y() + radii.height() * std::sin(angle)) / 512.0});
    }
    const auto rendered_path = build_photo_text_baseline(analytic_style, QSizeF(640, 512));
    QVector<QPointF> analytic_samples;
    constexpr int reference_samples = 8192;
    analytic_samples.reserve(reference_samples);
    for (int i = 0; i < reference_samples; ++i) {
      const auto angle = tau * i / reference_samples;
      analytic_samples.push_back({center.x() + radii.width() * std::cos(angle),
          center.y() + radii.height() * std::sin(angle)});
    }
    double maximum_deviation = 0;
    constexpr int rendered_sample_count = 2048;
    QVector<QPointF> rendered_samples;
    rendered_samples.reserve(rendered_sample_count);
    const auto rendered_length = rendered_path.length();
    for (int i = 0; i < rendered_sample_count; ++i) {
      const auto point = rendered_path.pointAtPercent(rendered_path.percentAtLength(
          rendered_length * i / rendered_sample_count));
      rendered_samples.push_back(point);
      double nearest_squared = std::numeric_limits<double>::max();
      for (const auto& reference : analytic_samples) {
        const auto distance = QLineF(point, reference).length();
        nearest_squared = std::min(nearest_squared, distance * distance);
      }
      maximum_deviation = std::max(maximum_deviation, std::sqrt(nearest_squared));
    }
    for (const auto& reference : analytic_samples) {
      double nearest_squared = std::numeric_limits<double>::max();
      for (const auto& point : rendered_samples) {
        const auto distance = QLineF(point, reference).length();
        nearest_squared = std::min(nearest_squared, distance * distance);
      }
      maximum_deviation = std::max(maximum_deviation, std::sqrt(nearest_squared));
    }
    CHECK(maximum_deviation <= 0.5,
        "Rendered Catmull-Rom baseline stays within 0.5 source pixel of analytic curve: deviation="
        << maximum_deviation << " radii=" << radii.width() << "x" << radii.height());
    std::cout << "analytic_contour_max_deviation_px=" << maximum_deviation
        << " radii=" << radii.width() << "x" << radii.height() << '\n';
  }
  const auto contour_render = rasterize_photo_text(QStringLiteral("FORM FOLLOWS"),
      contour_style, QSizeF(640, 512), QSize(640, 512));
  CHECK(contour_render.error.isEmpty() && !contour_render.image.isNull() && !contour_render.overflow,
      "A shaped text run fits along a smooth editable contour: error=" << contour_render.error.toStdString()
      << " overflow=" << contour_render.overflow << " width=" << contour_render.content_width);
  const auto contour_metrics = rasterize_photo_text(QStringLiteral("FORM FOLLOWS"), contour_style,
      QSizeF(640, 512), QSize(1, 1), Qt::AlignLeft, 0, {}, true);
  CHECK(contour_metrics.error == contour_render.error && contour_metrics.line_count == contour_render.line_count &&
      contour_metrics.content_width == contour_render.content_width &&
      contour_metrics.content_height == contour_render.content_height &&
      contour_metrics.overflow == contour_render.overflow && contour_metrics.ink_bounds.isEmpty(),
      "Layout-only contour inspection preserves all inspector metrics");
  auto offset_contour_style = contour_style;
  offset_contour_style.path_offset = 18;
  offset_contour_style.path_side = QStringLiteral("right");
  offset_contour_style.path_spacing = 1.5;
  const auto offset_contour = rasterize_photo_text(QStringLiteral("FORM FOLLOWS"),
      offset_contour_style, QSizeF(640, 512), QSize(640, 512));
  CHECK(offset_contour.error.isEmpty() && offset_contour.image != contour_render.image,
      "Contour side, normal offset, and per-glyph spacing change the rasterized result");
  auto underlined_contour_style = contour_style;
  underlined_contour_style.underline = true;
  const auto underlined_contour = rasterize_photo_text(QStringLiteral("FORM FOLLOWS"),
      underlined_contour_style, QSizeF(640, 512), QSize(640, 512));
  CHECK(underlined_contour.error.isEmpty() && underlined_contour.image != contour_render.image,
      "Underline follows the contour instead of remaining horizontal");
  auto closed_contour_style = contour_style;
  closed_contour_style.path_points = {QPointF(0.2,0.2),QPointF(0.8,0.2),QPointF(0.8,0.8),QPointF(0.2,0.8)};
  closed_contour_style.path_closed = true;
  const auto closed_contour = rasterize_photo_text(QStringLiteral("EDITION 04"),
      closed_contour_style, QSizeF(640, 512), QSize(640, 512));
  CHECK(closed_contour.error.isEmpty() && !closed_contour.ink_bounds.isEmpty(),
      "Closed mask contours remain usable as text baselines");
  auto overflowing_contour_style = contour_style;
  overflowing_contour_style.path_start = 0.82;
  overflowing_contour_style.path_end = 0.98;
  const auto overflowing_contour = rasterize_photo_text(QStringLiteral("A LONG CONTINUOUS TITLE"),
      overflowing_contour_style, QSizeF(640, 512), QSize(640, 512));
  CHECK(overflowing_contour.error.isEmpty() && overflowing_contour.overflow,
      "Contour text reports when the selected path interval is too short");
  auto exclusion_style = contour_style;
  exclusion_style.path_points = {QPointF(0.36,0.22), QPointF(0.64,0.22),
      QPointF(0.64,0.78), QPointF(0.36,0.78)};
  exclusion_style.path_closed = true;
  exclusion_style.path_exclusion = true;
  exclusion_style.point_text = false;
  exclusion_style.size = 34;
  const auto exclusion_render = rasterize_photo_text(
      QStringLiteral("EDITORIAL WORDS FLOW\nAROUND THE SUBJECT\nWITH ROOM TO BREATHE\nA SECOND COLUMN EMERGES"),
      exclusion_style, QSizeF(640, 512), QSize(640, 512));
  CHECK(exclusion_render.error.isEmpty() && exclusion_render.line_count >= 4 && !exclusion_render.overflow,
      "Editable paragraph layout wraps through the widest open intervals around the silhouette: "
      << exclusion_render.error.toStdString() << " lines=" << exclusion_render.line_count);
  const auto exclusion_metrics = rasterize_photo_text(
      QStringLiteral("EDITORIAL WORDS FLOW\nAROUND THE SUBJECT\nWITH ROOM TO BREATHE\nA SECOND COLUMN EMERGES"),
      exclusion_style, QSizeF(640, 512), QSize(1, 1), Qt::AlignLeft, 0, {}, true);
  CHECK(exclusion_metrics.error == exclusion_render.error &&
      exclusion_metrics.line_count == exclusion_render.line_count &&
      exclusion_metrics.content_width == exclusion_render.content_width &&
      exclusion_metrics.content_height == exclusion_render.content_height &&
      exclusion_metrics.overflow == exclusion_render.overflow && exclusion_metrics.ink_bounds.isEmpty(),
      "Layout-only contour exclusion inspection preserves all inspector metrics");
  bool text_inside_excluded_core = false;
  for (int y = 150; y < 360 && !text_inside_excluded_core; y += 3)
    for (int x = 270; x < 370; x += 3)
      if (exclusion_render.image.pixelColor(x, y).alpha() > 0) { text_inside_excluded_core = true; break; }
  CHECK(!text_inside_excluded_core, "Paragraph ink stays outside the contour's protected interior");
  auto hole_exclusion_style = exclusion_style;
  hole_exclusion_style.path_points = {QPointF(0.1,0.1), QPointF(0.9,0.1),
      QPointF(0.9,0.9), QPointF(0.1,0.9)};
  hole_exclusion_style.path_contours = {hole_exclusion_style.path_points,
      {QPointF(0.25,0.25), QPointF(0.75,0.25), QPointF(0.75,0.75), QPointF(0.25,0.75)}};
  const auto hole_exclusion = rasterize_photo_text(QStringLiteral("\n\n\n\n\nTEXT THROUGH HOLE"),
      hole_exclusion_style, QSizeF(640, 512), QSize(640, 512));
  bool text_inside_hole = false;
  for (int y = 210; y < 300 && !text_inside_hole; y += 2)
    for (int x = 260; x < 440; x += 2)
      if (hole_exclusion.image.pixelColor(x, y).alpha() > 0) { text_inside_hole = true; break; }
  CHECK(hole_exclusion.error.isEmpty() && text_inside_hole,
      "An inner mask contour remains a usable paragraph interval for text");
  auto moved_hole_baseline_style = hole_exclusion_style;
  moved_hole_baseline_style.path_contours[1] = {QPointF(0.45,0.4), QPointF(0.55,0.4),
      QPointF(0.55,0.6), QPointF(0.45,0.6)};
  moved_hole_baseline_style.path_points[0] = QPointF(0.34, 0.14);
  const auto moved_hole_baseline = rasterize_photo_text(QStringLiteral("\n\n\n\n\nTEXT THROUGH HOLE"),
      moved_hole_baseline_style, QSizeF(640, 512), QSize(640, 512));
  CHECK(moved_hole_baseline.error.isEmpty() && moved_hole_baseline.image != hole_exclusion.image,
      "Moving the primary handle also moves the flow boundary while retaining inner contours: "
      << moved_hole_baseline.error.toStdString() << " changed="
      << (moved_hole_baseline.image != hole_exclusion.image));
  const auto point_state=state().layers;
  controller->undo();CHECK(controller->photoLayerCount()==0,"Point creation is one undo");
  settle();
  CHECK(root->property("selectedTextLayerIndex").toInt() == -1 &&
      root->property("selectedTextLayerIndices").toList().isEmpty(),
      "Undoing the only text layer clears the inspector target");
  controller->redo();CHECK(state().layers==point_state,"Redo restores point bounds");
  settle();
  const auto redone_text_targets = root->property("selectedTextLayerIndices").toList();
  CHECK(root->property("selectedTextLayerIndex").toInt() == 0 &&
      redone_text_targets.size() == 1 && redone_text_targets.front().toInt() == 0,
      "Redo restores a valid single text inspector target");
  QMetaObject::invokeMethod(editor,"beginEditing");key(Qt::Key_unknown,Qt::NoModifier,"Discard this");key(Qt::Key_Escape,Qt::NoModifier);
  CHECK(state().layers==point_state,"Escape discards draft and its resized bounds");
  QMetaObject::invokeMethod(editor,"beginEditing");
  QInputMethodEvent commit;commit.setCommitString(QString::fromUtf8("Café 日本語"));QCoreApplication::sendEvent(input,&commit);
  key(Qt::Key_Return,Qt::ControlModifier);
  CHECK(state().layers[0].toMap().value("text").toString()==QString::fromUtf8("Café 日本語"),"Composed text commits through the controller");
  controller->undo();CHECK(state().layers==point_state,"Text content and fitted bounds undo together");
  CHECK(controller->setPhotoLayerTypography(text_layer_index(),{{"size",34.5},{"color","#7feaa251"},{"italic",true},{"strokeWidth",1.5},{"strokeColor","#ff18202a"},{"shadowX",2.0},{"shadowY",3.0}}),"Fractional point typography");
  const auto style_before_alignment = controller->photoLayerTypography(text_layer_index());
  CHECK(controller->setPhotoLayerTypography(text_layer_index(), {{"alignment", "center"}}),
      "Alignment uses the typography command without rebuilding its style");
  CHECK(controller->photoLayerTypography(text_layer_index()) == style_before_alignment &&
      state().layers.at(text_layer_index()).toMap().value("textAlignment").toString() == QStringLiteral("center"),
      "Alignment preserves typography recipe bytes, including text alpha");
  const auto aligned_state = state().layers;
  CHECK(!controller->setPhotoLayerTypography(text_layer_index(), {{"alignment", "invalid"}}) &&
      state().layers == aligned_state, "Reject an unknown text alignment without changing the layer");
  controller->undo();
  CHECK(state().layers.at(text_layer_index()).toMap().value("textAlignment").toString() != QStringLiteral("center") &&
      controller->photoLayerTypography(text_layer_index()) == style_before_alignment,
      "Undo restores alignment while preserving typography color alpha");
  controller->redo();
  CHECK(state().layers == aligned_state, "Redo restores alignment without changing the style recipe");
  CHECK(controller->setPhotoLayerTextStyle(text_layer_index(), QStringLiteral("Georgia"), 36.5,
      QStringLiteral("#eaa251"), QStringLiteral("regular"), QStringLiteral("center")) &&
      controller->photoLayerTypography(text_layer_index()).value("color").toString() == QStringLiteral("#7feaa251"),
      "Combined family and size updates preserve the current typography alpha when RGB is unchanged");
  controller->undo();
  CHECK(state().layers == aligned_state, "Undo restores the prior family and point-text geometry");
  CHECK(controller->photoLayerTextLayoutInfo(text_layer_index()).value("lineCount").toInt()==1,"Point remains unwrapped after typography change");
  CHECK(wait_until([&]{return !controller->processing()&&!controller->fullRenderRunning();}),"Point preview settles");
  if(!evidence.isEmpty()){settle();CHECK(window.grabWindow().save(evidence+"/point-native.png"),"Point committed screenshot");}
  QMetaObject::invokeMethod(editor,"beginEditing");
  CHECK(editor->property("styledText").toBool(), "Outlined text keeps styled editing geometry");
  key(Qt::Key_Escape,Qt::NoModifier);
  const auto auto_sized=state().layers;
  const auto point_box_layer=auto_sized.at(0).toMap();
  CHECK(controller->setPhotoLayerTransform(0, point_box_layer.value("x").toInt(),
      point_box_layer.value("y").toInt(), 1.7, 20), "Prepare scaled, rotated text for box-only resizing");
  const auto transformed_point=state().layers;
  const auto point_box_style=controller->photoLayerTypography(0);
  controller->beginCoalescedEdit();
  CHECK(controller->resizePhotoLayerTextBox(0, 72, 81, 200, 180) &&
      controller->resizePhotoLayerTextBox(0, 68, 75, 230, 190), "Resize text box geometry during one drag");
  controller->endCoalescedEdit();
  const auto resized_box_layer=state().layers.at(0).toMap();
  auto expected_box_style=point_box_style;
  expected_box_style.insert("mode", "paragraph");
  CHECK(controller->photoLayerTypography(0)==expected_box_style &&
      resized_box_layer.value("scale").toDouble()==1.7 && resized_box_layer.value("rotation").toDouble()==20 &&
      resized_box_layer.value("pixelWidth").toInt()==230 && resized_box_layer.value("pixelHeight").toInt()==190 &&
      resized_box_layer.value("x").toInt()==68 && resized_box_layer.value("y").toInt()==75,
      "Box resize preserves font size, styling, scale and rotation while enabling paragraph wrapping");
  controller->undo();
  CHECK(state().layers==transformed_point, "One undo restores the complete box drag and point-text mode");
  controller->undo();
  CHECK(state().layers==auto_sized, "Restore the original point transform after the box regression");
  CHECK(controller->setPhotoLayerTextBox(0,120,200),"Resize point text into a paragraph box");
  CHECK(controller->photoLayerTypography(text_layer_index()).value("mode").toString()!="point" &&
      controller->photoLayerTextLayoutInfo(text_layer_index()).value("lineCount").toInt()>1,"Explicit box size enables paragraph wrapping");
  controller->undo();CHECK(state().layers==auto_sized,"Undo box conversion restores point mode and bounds");
  root->setProperty("creating",true);settle();
  mouse(QEvent::MouseButtonPress,{390,270},Qt::LeftButton,Qt::LeftButton);
  mouse(QEvent::MouseMove,{900,490},Qt::NoButton,Qt::LeftButton);
  mouse(QEvent::MouseButtonRelease,{900,490},Qt::LeftButton,Qt::NoButton);settle();
  CHECK(editor->property("editing").toBool(),"Native drag begins paragraph text");
  key(Qt::Key_unknown,Qt::NoModifier,"A city in motion\nA quiet moment between the buildings.");key(Qt::Key_Return,Qt::ControlModifier);
  CHECK(controller->photoLayerCount()==2,"Paragraph creation adds a separate editable layer");
  CHECK(controller->setPhotoLayerTypography(text_layer_index(),{{"size",25.5},{"lineHeight",1.4},{"color","#ffffffff"}}),"Paragraph typography");
  settle();
  const auto before_spacing=state().layers;
  CHECK(set_disclosure("photoTypographySpacingToggle", true),
      "Open the spacing and layout section before editing paragraph spacing");
  auto* spacing=qobject_cast<QQuickItem*>(find("photoTypography_paragraphSpacing"));CHECK(spacing && spacing->isVisible(),"Paragraph spacing control visible");
  click_control(spacing);
  CHECK(spacing->hasActiveFocus(),"Paragraph spacing field receives native focus");
  QMetaObject::invokeMethod(spacing,"selectAll");key(Qt::Key_unknown,Qt::NoModifier,"18.25");key(Qt::Key_Return,Qt::NoModifier);
  CHECK(controller->photoLayerTypography(text_layer_index()).value("paragraphSpacing").toDouble()==18.25,"Native fractional paragraph spacing");
  const auto spaced=state().layers;controller->undo();CHECK(state().layers==before_spacing,"Paragraph spacing undo");controller->redo();CHECK(state().layers==spaced,"Paragraph spacing redo");
  CHECK(controller->setPhotoLayerTypography(text_layer_index(),{{"family","Photo Editor Missing Font Fixture"}}),"Retain unavailable saved font");
  const auto missing=controller->photoLayerTextLayoutInfo(text_layer_index());
  CHECK(missing.value("missingFont").toBool() && !missing.value("resolvedFamily").toString().isEmpty(),"Report missing font and resolved fallback");
  settle();CHECK(find("photoMissingFontWarning")->property("visible").toBool(),"Visible missing font warning");
  controller->undo();CHECK(state().layers==spaced,"Undo font substitution restores exact typography");
  controller->addMask(QStringLiteral("rectangle"));
  controller->setActiveMaskCenterX(0.5); controller->setActiveMaskCenterY(0.52);
  controller->setActiveMaskRadiusX(0.38); controller->setActiveMaskRadiusY(0.26);
  CHECK(controller->addPointTextLayer(QStringLiteral("Contour study"), 30, 30, 640, 512),
      "Add a point-text fixture for contour capture");
  const auto contour_layers = controller->photoLayers();
  const auto contour_layer = std::find_if(contour_layers.cbegin(), contour_layers.cend(), [](const auto& value) {
    const auto layer = value.toMap();
    return layer.value(QStringLiteral("kind")).toString() == QStringLiteral("text") &&
        layer.value(QStringLiteral("text")).toString() == QStringLiteral("Contour study");
  });
  CHECK(contour_layer != contour_layers.cend(), "Find the newly created contour text by its content");
  const auto contour_index = static_cast<int>(std::distance(contour_layers.cbegin(), contour_layer));
  selected_text_layer_id = contour_layer->toMap().value(QStringLiteral("id")).toString();
  CHECK(!selected_text_layer_id.isEmpty(), "Contour text exposes its stable layer ID");
  root->setProperty("selectedTextLayerId", selected_text_layer_id);
  settle();
  const auto contour_text_targets = root->property("selectedTextLayerIndices").toList();
  CHECK(root->property("selectedTextLayerIndex").toInt() == contour_index &&
      text_layer_index() == contour_index &&
      contour_text_targets.size() == 1 && contour_text_targets.front().toInt() == contour_index,
      "The contour inspector and controller helper target the same stable text layer");
  CHECK(set_disclosure("photoTypographyPathToggle", true),
      "Open the text path section before attaching a selection contour");
  auto* contour_button = qobject_cast<QQuickItem*>(find("photoTextPathFromSelection"));
  CHECK(contour_button && contour_button->isVisible(), "Typography panel exposes selection-contour text");
  click_control(contour_button);
  CHECK(controller->photoLayerTypography(text_layer_index()).value("pathPoints").toList().size() >= 3,
      "Native typography control turns the traced selection into a detached baseline");
  controller->addMask(QStringLiteral("subject"));
  constexpr int donut_width = 640, donut_height = 512;
  QByteArray donut_alpha(donut_width * donut_height, char(0));
  for (int y = 0; y < donut_height; ++y) {
    for (int x = 0; x < donut_width; ++x) {
      const auto dx = (x - donut_width * 0.5) / (donut_width * 0.38);
      const auto dy = (y - donut_height * 0.5) / (donut_height * 0.40);
      const auto inner_x = (x - donut_width * 0.5) / (donut_width * 0.14);
      const auto inner_y = (y - donut_height * 0.5) / (donut_height * 0.16);
      if (dx * dx + dy * dy <= 1.0 && inner_x * inner_x + inner_y * inner_y >= 1.0)
        donut_alpha[y * donut_width + x] = char(0xff);
    }
  }
  auto subject_mask = controller->maskStack().at(controller->activeMaskIndex()).toMap();
  subject_mask.insert(QStringLiteral("kind"), QStringLiteral("subject"));
  subject_mask.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
  subject_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("donut-subject-selection"));
  subject_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Donut subject"));
  subject_mask.insert(QStringLiteral("segmentationData"), donut_alpha);
  subject_mask.insert(QStringLiteral("segmentationWidth"), donut_width);
  subject_mask.insert(QStringLiteral("segmentationHeight"), donut_height);
  subject_mask.insert(QStringLiteral("pixelSelection"), true);
  CHECK(EditorPreviewControllerTestAccess::update_active_mask_definition(*controller, subject_mask),
      "Install a deterministic subject selection with an inner hole");
  const auto outline_workers_before = EditorPreviewControllerTestAccess::completed_outline_workers(*controller);
  (void)controller->activeMaskOutlinePaths();
  if (controller->activeMaskOutlineTrace().value("outline_trace_source").toString() == QStringLiteral("worker-pending")) {
    CHECK(wait_until([&] {
      return EditorPreviewControllerTestAccess::completed_outline_workers(*controller) > outline_workers_before &&
          !EditorPreviewControllerTestAccess::outline_worker_running(*controller);
    }), "Subject contour trace settles");
  }
  const auto traced_donut_paths = controller->activeMaskOutlinePaths();
  CHECK(traced_donut_paths.size() >= 2,
      "Trace both the exterior and hole of the subject alpha mask: paths=" << traced_donut_paths.size()
      << " source=" << controller->activeMaskOutlineTrace().value("outline_trace_source").toString().toStdString());
  const auto captured_subject_baseline = controller->setPhotoLayerTextPathFromActiveSelection(text_layer_index(), 0);
  CHECK(captured_subject_baseline,
      "Replace the editable baseline from a real alpha-mask contour: "
      << controller->status().toStdString() << " role=" << controller->activeMaskToolRole().toStdString()
      << " layer=" << controller->photoLayers().value(0).toMap().value("kind").toString().toStdString()
      << " locked=" << controller->photoLayers().value(0).toMap().value("locked").toBool());
  settle();
  CHECK(controller->photoLayerTypography(text_layer_index()).value("pathContours").toList().size() == 2,
      "Detached text retains the subject boundary and its inner hole contour");
  const auto before_handle_drag = state().layers;
  CHECK(set_disclosure("photoTypographyPathToggle", true), "Reopen path controls for the selected contour text");
  auto* handle_edit_button = qobject_cast<QQuickItem*>(find("photoTextPathHandleEditButton"));
  auto* baseline_proof_scroll = qobject_cast<QQuickItem*>(find("typographyProofScroll"));
  auto* baseline_panel = qobject_cast<QQuickItem*>(find("typographyPanel"));
  CHECK(handle_edit_button && handle_edit_button->isVisible() && baseline_proof_scroll && baseline_panel,
      "Typography panel exposes direct baseline editing in its scroll view");
  QPointF handle_edit_center;
  QPointF previous_handle_edit_center;
  QRectF baseline_proof_viewport;
  bool handle_geometry_stable = false;
  for (int layout_pass = 0; layout_pass < 6; ++layout_pass) {
    reveal_control(handle_edit_button);
    settle();
    handle_edit_center = handle_edit_button->mapToScene(
        QPointF(handle_edit_button->width() / 2, handle_edit_button->height() / 2));
    baseline_proof_viewport = QRectF(baseline_proof_scroll->mapToScene(QPointF(0, 0)),
        QSizeF(baseline_proof_scroll->width(), baseline_proof_scroll->height()));
    const auto delta = handle_edit_center - previous_handle_edit_center;
    if (layout_pass > 0 && std::hypot(delta.x(), delta.y()) < 0.5 &&
        baseline_proof_viewport.contains(handle_edit_center)) {
      handle_geometry_stable = true;
      break;
    }
    previous_handle_edit_center = handle_edit_center;
  }
  CHECK(handle_geometry_stable && handle_edit_button->isVisible() && handle_edit_button->isEnabled() &&
      handle_edit_button->contains(handle_edit_button->mapFromScene(handle_edit_center)) &&
      baseline_proof_viewport.contains(handle_edit_center),
      "Baseline edit button layout settles enabled and centered inside the visible inspector viewport: center="
          << handle_edit_center.x() << ',' << handle_edit_center.y()
          << " viewport=" << baseline_proof_viewport.x() << ',' << baseline_proof_viewport.y() << ' '
          << baseline_proof_viewport.width() << 'x' << baseline_proof_viewport.height());
  const auto baseline_request_count_before = root->property("baselineEditRequestCount").toInt();
  mouse(QEvent::MouseButtonPress, handle_edit_center, Qt::LeftButton, Qt::LeftButton);
  auto* baseline_pointer_grabber = window.mouseGrabberItem();
  mouse(QEvent::MouseButtonRelease, handle_edit_center, Qt::LeftButton, Qt::NoButton);
  settle();
  bool pointer_grab_belongs_to_button = false;
  for (auto* item = baseline_pointer_grabber; item; item = item->parentItem())
    pointer_grab_belongs_to_button = pointer_grab_belongs_to_button || item == handle_edit_button;
  const auto baseline_pointer_grabber_name = baseline_pointer_grabber
      ? QString::fromLatin1(baseline_pointer_grabber->metaObject()->className()) + QLatin1Char('/') +
          baseline_pointer_grabber->objectName()
      : QStringLiteral("none");
  auto* path_handle_frame = qobject_cast<QQuickItem*>(find("photoTextPathHandleFrame"));
  CHECK(root->property("baselineEditRequestCount").toInt() == baseline_request_count_before + 1 &&
      root->property("lastBaselineEditRequestValue").toBool() &&
      baseline_panel->property("baselineEditing").toBool() &&
      editor->property("pathHandlesActive").toBool() && editor->property("pathHandlesVisible").toBool() &&
      path_handle_frame && path_handle_frame->isVisible(),
      "Baseline editing shows draggable curve handles on the photo: buttonActive="
          << handle_edit_button->property("active").toBool()
          << " panelEditing=" << baseline_panel->property("baselineEditing").toBool()
          << " signalCount=" << root->property("baselineEditRequestCount").toInt()
          << " signalValue=" << root->property("lastBaselineEditRequestValue").toBool()
          << " overlayActive=" << editor->property("pathHandlesActive").toBool()
          << " overlayVisible=" << editor->property("pathHandlesVisible").toBool()
          << " frameVisible=" << (path_handle_frame && path_handle_frame->isVisible())
          << " enabled=" << handle_edit_button->isEnabled()
          << " buttonCenter=" << handle_edit_center.x() << ',' << handle_edit_center.y()
          << " viewportContains=" << baseline_proof_viewport.contains(handle_edit_center)
          << " layoutStable=" << handle_geometry_stable
          << " mouseGrabber=" << baseline_pointer_grabber_name.toStdString()
          << " grabBelongsToButton=" << pointer_grab_belongs_to_button
          << " pathPoints=" << controller->photoLayerTypography(text_layer_index()).value("pathPoints").toList().size()
          << " selectedLayer=" << text_layer_index());
  auto* contour_canvas = qobject_cast<QQuickItem*>(find("photoTextPathBaseline"));
  CHECK(contour_canvas && contour_canvas->property("paintedContourCount").toInt() == 2,
      "Baseline editing draws the primary boundary and retained inner contour: painted="
      << (contour_canvas ? contour_canvas->property("paintedContourCount").toInt() : -1)
      << " styleContours=" << controller->photoLayerTypography(text_layer_index()).value("pathContours").toList().size());
  auto* contour_legend = qobject_cast<QQuickItem*>(find("photoTextPathContourLegend"));
  CHECK(contour_legend && contour_legend->isVisible(),
      "Typography panel explains the distinct styling for additional contours and holes");
  auto* first_path_handle = qobject_cast<QQuickItem*>(find("photoTextPathHandle_0"));
  CHECK(first_path_handle && first_path_handle->isVisible(), "First contour point is directly addressable");
  const auto original_handle_points = controller->photoLayerTypography(text_layer_index()).value("pathPoints").toList();
  const auto handle_center = first_path_handle->mapToScene(
      QPointF(first_path_handle->width()/2, first_path_handle->height()/2));
  drag(handle_center, handle_center + QPointF(18, 12));
  const auto moved_handle_points = controller->photoLayerTypography(text_layer_index()).value("pathPoints").toList();
  CHECK(moved_handle_points != original_handle_points,
      "Dragging a baseline point changes the editable contour geometry");
  controller->undo();
  CHECK(state().layers == before_handle_drag,
      "A complete baseline-point drag is one undoable edit");
  controller->redo();
  CHECK(controller->photoLayerTypography(text_layer_index()).value("pathPoints").toList() == moved_handle_points,
      "Redo restores the moved baseline point exactly");
  click_control(handle_edit_button);
  CHECK(!editor->property("pathHandlesActive").toBool(), "Done baseline returns to ordinary canvas interaction");
  auto* exclusion_check = qobject_cast<QQuickItem*>(find("photoTextContourExclusionCheckBox"));
  CHECK(exclusion_check && exclusion_check->isVisible(), "Closed selection exposes paragraph exclusion mode");
  click_control(exclusion_check);
  auto exclusion_typography = controller->photoLayerTypography(text_layer_index());
  CHECK(exclusion_typography.value("pathExclusion").toBool() &&
      exclusion_typography.value("mode").toString() == "paragraph" &&
      exclusion_typography.value("version").toInt() == 7,
      "Native control stores paragraph exclusion in the versioned text style");
  CHECK(controller->setPhotoLayerText(text_layer_index(), QStringLiteral("FLOW AROUND THE SUBJECT\nIN AN EDITABLE PARAGRAPH")),
      "Paragraph exclusion remains ordinary editable text");
  const auto exclusion_layout = controller->photoLayerTextLayoutInfo(text_layer_index());
  CHECK(exclusion_layout.value("error").toString().isEmpty() && exclusion_layout.value("lineCount").toInt() >= 2,
      "Typography inspection reports the exclusion paragraph layout");
  const auto exclusion_recipe = serialize_edit_recipe_json(state());
  const auto reopened_exclusion = parse_edit_recipe_json(exclusion_recipe);
  CHECK(validate_edit_recipe_json(exclusion_recipe).valid &&
      reopened_exclusion.layers.at(text_layer_index()).toMap().value("textTypography").toMap().value("pathExclusion").toBool(),
      "Version-7 paragraph exclusion with multiple contours survives recipe validation and reopen");
  CHECK(controller->setPhotoLayerText(text_layer_index(), QStringLiteral("Contour study")),
      "Restore the point-text fixture before testing baseline-only persistence");
  click_control(exclusion_check);
  const auto contour_saved_state = state().layers;
  const auto contour_text_layer = contour_saved_state.at(text_layer_index()).toMap();
  const auto contour_typography = contour_text_layer.value("textTypography").toMap();
  CHECK(contour_typography.value("version").toInt() == 7 &&
      contour_typography.value("pathPoints").toList().size() >= 3 &&
      contour_typography.value("pathContours").toList().size() == 2 &&
      contour_text_layer.value("pixelWidth").toInt() == 640 && contour_text_layer.value("pixelHeight").toInt() == 512,
      "Selection contour geometry is stored in the editable text recipe");
  CHECK(controller->clearPhotoLayerTextPath(text_layer_index()), "Contour baseline can be detached from text");
  controller->undo(); CHECK(state().layers == contour_saved_state, "Undo restores the contour baseline exactly");
  controller->setActiveMaskRadiusX(0.31);
  CHECK(controller->photoLayerTypography(text_layer_index()).value("pathPoints").toList() == contour_typography.value("pathPoints").toList(),
      "Changing the source selection does not mutate the detached text baseline");
  CHECK(controller->setPhotoLayerLocked(text_layer_index(),true),"Lock typography");
  CHECK(!controller->setPhotoLayerTypography(text_layer_index(),{{"paragraphSpacing",3.0}}) &&
      !controller->setPhotoLayerText(text_layer_index(),"Blocked") &&
      !controller->resizePhotoLayerTextBox(text_layer_index(),20,30,200,100),"Locked text refuses edits");controller->undo();
  const auto final_state=state();
  const auto recipe=serialize_edit_recipe_json(final_state);
  CHECK(validate_edit_recipe_json(recipe).valid,"Versioned typography and text effects validate in desktop recipe");
  RenderBackendTelemetry contour_telemetry;
  const auto contour_flatten = render_output_image_with_backend(source, final_state,
      QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64, RenderBackendPreference::CpuOnly,
      nullptr, &contour_telemetry);
  CHECK(!contour_flatten.isNull(), "A saved contour-text stack renders through the flattened compositor: "
      << contour_telemetry.fallback_reason.toStdString() << " " << contour_telemetry.canonical_plan_error.toStdString());
  CHECK(set_disclosure("photoTypographyEffectsToggle", true), "Open material controls for the selected text");
  auto* material_control = qobject_cast<QQuickItem*>(find("photoTypographyMaterial"));
  CHECK(material_control && material_control->isVisible(),
      "Typography panel exposes the user-selectable material-text modes");
  reveal_control(material_control);
  material_control->forceActiveFocus(Qt::TabFocusReason);
  key(Qt::Key_Space, Qt::NoModifier);
  key(Qt::Key_Down, Qt::NoModifier);
  key(Qt::Key_Return, Qt::NoModifier);
  CHECK(controller->photoLayerTypography(text_layer_index()).value("material").toString() == "frosted_glass",
      "Keyboard selection applies frosted glass through the native typography control");
  const auto glass_mode_state = state();
  auto* material_blur_slider = qobject_cast<QQuickItem*>(find("photoTypography_materialBlur"));
  auto* material_blur_field = qobject_cast<QQuickItem*>(find("photoTypography_materialBlurValueField"));
  auto* material_blur_reset = qobject_cast<QQuickItem*>(find("photoTypography_materialBlurResetButton"));
  CHECK(material_blur_slider && material_blur_slider->isVisible() && material_blur_field &&
      material_blur_reset && material_blur_slider->property("valueInputEnabled").toBool(),
      "Material blur exposes a labeled slider, numeric entry, and reset control");
  // A visible QQuickItem can still be clipped below the ScrollView viewport.
  // Scroll to the real field before sending native pointer/key input.
  auto* proof_scroll = find("typographyProofScroll");
  auto* proof_flickable = proof_scroll
      ? qobject_cast<QQuickItem*>(proof_scroll->property("contentItem").value<QObject*>()) : nullptr;
  CHECK(proof_flickable, "Typography proof exposes its actual scroll viewport");
  const auto field_viewport_y = material_blur_field->mapToItem(proof_flickable, QPointF(0, 0)).y();
  const auto maximum_content_y = std::max(0.0, proof_flickable->property("contentHeight").toDouble() - proof_flickable->height());
  proof_flickable->setProperty("contentY", std::clamp(
      proof_flickable->property("contentY").toDouble() + field_viewport_y - 24.0, 0.0, maximum_content_y));
  settle();
  click_control(material_blur_field);
  CHECK(material_blur_field->hasActiveFocus(), "Material blur field receives native focus inside its scroll viewport");
  QMetaObject::invokeMethod(material_blur_field, "selectAll");
  key(Qt::Key_unknown, Qt::NoModifier, "8");
  CHECK(material_blur_field->property("text").toString() == QStringLiteral("8"),
      "Material blur field receives the actual numeric keystroke");
  key(Qt::Key_Return, Qt::NoModifier);
  CHECK(std::abs(controller->photoLayerTypography(text_layer_index()).value("materialBlur").toDouble() - 8.0) < 1e-6,
      "Numeric material entry commits through the editable typography controller");
  const auto blur_value_state = state();
  click_control(material_blur_reset);
  CHECK(std::abs(controller->photoLayerTypography(text_layer_index()).value("materialBlur").toDouble() - 6.0) < 1e-6,
      "Material blur reset restores its declared default");
  const auto material_reset_state = state();
  CHECK(controller->setPhotoLayerTypography(text_layer_index(), {{"materialTransmission", 0.74},
      {"materialTint", "#ffb5cbd8"}}), "Apply tint and transmission through the shared typography controller");
  settle();
  const auto material_style = controller->photoLayerTypography(text_layer_index());
  CHECK(material_style.value("version").toInt() == 8 &&
      material_style.value("material").toString() == "frosted_glass" &&
      material_control->property("currentIndex").toInt() == 1,
      "Material controls use version-8 editable text style and update the native panel: style="
      << material_style.value("version").toInt() << '/' << material_style.value("material").toString().toStdString()
      << " index=" << material_control->property("currentIndex").toInt());
  if (!evidence.isEmpty())
    CHECK(window.grabWindow().save(evidence + "/material-native.png"),
        "Capture native material typography controls");
  const auto material_state = state();
  const auto material_flatten = render_output_image_with_backend(source, material_state,
      QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64, RenderBackendPreference::CpuOnly);
  CHECK(!material_flatten.isNull() && material_flatten != contour_flatten,
      "Frosted text samples and changes only the composed lower stack while remaining renderable");
  if (!evidence.isEmpty())
    CHECK(material_flatten.save(evidence + "/material-export.png"),
        "Capture the material-text CPU export fixture");
  CHECK(controller->setPhotoLayerTypography(text_layer_index(), {
      {QStringLiteral("material"), QStringLiteral("polished_chrome")},
      {QStringLiteral("materialTint"), QStringLiteral("#ffe8edf2")},
      {QStringLiteral("materialTransmission"), 0.94},
      {QStringLiteral("materialEdgeSoftness"), 1.1},
      {QStringLiteral("materialDepth"), 8.0},
      {QStringLiteral("materialLightAngle"), -45.0}}),
      "Controller accepts a metallic finish with editable light and bevel values");
  settle();
  const auto controller_metal_style = controller->photoLayerTypography(text_layer_index());
  CHECK(controller_metal_style.value(QStringLiteral("version")).toInt() == 9 &&
      controller_metal_style.value(QStringLiteral("material")).toString() == QStringLiteral("polished_chrome"),
      "Controller publishes metallic typography as version-9 editable style data");
  const auto controller_metal_state = state();
  const auto controller_metal_recipe = serialize_edit_recipe_json(controller_metal_state);
  const auto reopened_controller_metal = parse_edit_recipe_json(controller_metal_recipe);
  const auto reopened_controller_style = reopened_controller_metal.layers.at(text_layer_index()).toMap()
      .value(QStringLiteral("textTypography")).toMap();
  CHECK(validate_edit_recipe_json(controller_metal_recipe).valid &&
      reopened_controller_style.value(QStringLiteral("version")).toInt() == 9 &&
      reopened_controller_style.value(QStringLiteral("material")).toString() == QStringLiteral("polished_chrome") &&
      reopened_controller_style.value(QStringLiteral("materialTint")).toString() == QStringLiteral("#ffe8edf2") &&
      reopened_controller_style.value(QStringLiteral("materialDepth")).toDouble() == 8.0 &&
      reopened_controller_style.value(QStringLiteral("materialLightAngle")).toDouble() == -45.0,
      "Controller-created metal typography survives recipe validation and reopen");
  CHECK(material_control->property("currentIndex").toInt() == 4,
      "Native typography panel selects Chrome for the controller-created metal style");
  if (!evidence.isEmpty())
    CHECK(window.grabWindow().save(evidence + "/metal-chrome-native.png"),
        "Capture the native typography panel with Chrome selected");
  CHECK(render_output_image_with_backend(source, reopened_controller_metal,
      QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64, RenderBackendPreference::CpuOnly) ==
      render_output_image_with_backend(source, controller_metal_state,
          QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64, RenderBackendPreference::CpuOnly),
      "Controller-created metal text renders identically after recipe reopen");
  const auto controller_metal_export_path = temp.filePath("metal-chrome-controller.png");
  CHECK(controller->exportFlattenedPhoto(controller_metal_export_path),
      "Production flattened PNG export accepts the live version-9 Chrome recipe");
  const QImage controller_metal_export(controller_metal_export_path);
  CHECK(!controller_metal_export.isNull(),
      "Production flattened Chrome PNG export is readable");
  auto reopened_metal_controller = std::make_unique<EditorPreviewController>();
  reopened_metal_controller->loadAsset("typography-metal-reopened", source_path,
      {}, {}, {}, {}, controller_metal_recipe);
  const auto reopened_live_metal_state = EditorPreviewControllerTestAccess::state(*reopened_metal_controller);
  CHECK(reopened_live_metal_state.layers == controller_metal_state.layers,
      "A fresh controller restores the exact serialized Chrome typography state");
  const auto reopened_metal_export_path = temp.filePath("metal-chrome-reopened.png");
  CHECK(reopened_metal_controller->exportFlattenedPhoto(reopened_metal_export_path),
      "Production flattened PNG export accepts the controller-reopened version-9 Chrome recipe");
  const QImage reopened_metal_export(reopened_metal_export_path);
  CHECK(!reopened_metal_export.isNull() && reopened_metal_export == controller_metal_export,
      "Production Chrome PNG output is pixel-identical after exact controller recipe reopen");
  if (!evidence.isEmpty())
    CHECK(controller_metal_export.save(evidence + "/metal-chrome-export.png"),
        "Save the production flattened Chrome PNG export as inspectable evidence");
  if (!evidence.isEmpty())
    CHECK(reopened_metal_export.save(evidence + "/metal-chrome-reopened-export.png"),
        "Save the controller-reopened production Chrome PNG as inspectable evidence");
  controller->undo();
  CHECK(state().layers == material_state.layers,
      "Undo restores the prior glass material after a metallic typography edit");
  struct MetalFinish final { QString id; QString tint; };
  const std::array<MetalFinish, 10> metal_finishes{{
      {QStringLiteral("polished_chrome"), QStringLiteral("#ffe8edf2")},
      {QStringLiteral("brushed_silver"), QStringLiteral("#ffc5cbd1")},
      {QStringLiteral("warm_gold"), QStringLiteral("#ffdba33b")},
      {QStringLiteral("copper"), QStringLiteral("#ffc1784e")},
      {QStringLiteral("dark_steel"), QStringLiteral("#ff49525c")},
      {QStringLiteral("champagne_gold"), QStringLiteral("#ffcfb47a")},
      {QStringLiteral("rose_gold"), QStringLiteral("#ffcf9286")},
      {QStringLiteral("antique_brass"), QStringLiteral("#ffa88445")},
      {QStringLiteral("titanium"), QStringLiteral("#ff88989f")},
      {QStringLiteral("black_chrome"), QStringLiteral("#ff343c49")}}};
  constexpr int metal_tile_width = 320;
  constexpr int metal_tile_height = 252;
  constexpr int metal_label_height = 26;
  QImage metal_contact_sheet(metal_tile_width * 5, metal_tile_height * 2, QImage::Format_RGB32);
  metal_contact_sheet.fill(QColor(QStringLiteral("#171b22")));
  QPainter metal_painter(&metal_contact_sheet);
  metal_painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  metal_painter.setFont(QFont(QStringLiteral("Arial"), 10, QFont::DemiBold));
  for (std::size_t metal_index = 0; metal_index < metal_finishes.size(); ++metal_index) {
    const auto& finish = metal_finishes[metal_index];
    auto metal_state = material_state;
    auto metal_layers = metal_state.layers;
    auto metal_layer = metal_layers.at(text_layer_index()).toMap();
    auto metal_style = metal_layer.value(QStringLiteral("textTypography")).toMap();
    metal_style.insert(QStringLiteral("version"), 9);
    metal_style.insert(QStringLiteral("material"), finish.id);
    metal_style.insert(QStringLiteral("materialTint"), finish.tint);
    metal_style.insert(QStringLiteral("materialBlur"), 0.0);
    metal_style.insert(QStringLiteral("materialTransmission"), metal_index == 0 || metal_index == 9 ? 0.94 : 0.82);
    metal_style.insert(QStringLiteral("materialEdgeSoftness"), 1.2);
    metal_style.insert(QStringLiteral("materialDepth"), 8.0);
    metal_style.insert(QStringLiteral("materialLightAngle"), -45.0);
    metal_layer.insert(QStringLiteral("textTypography"), metal_style);
    metal_layers[text_layer_index()] = metal_layer;
    metal_state.layers = metal_layers;
    CHECK(validate_edit_recipe_json(serialize_edit_recipe_json(metal_state)).valid,
        "Metal material document remains recipe-valid: " << finish.id.toStdString());
    const auto metal_flatten = render_output_image_with_backend(source, metal_state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64, RenderBackendPreference::CpuOnly);
    CHECK(!metal_flatten.isNull() && metal_flatten != material_flatten,
        "Metal finish produces a rendered result distinct from glass: " << finish.id.toStdString());
    if (metal_index == 0) {
      const auto metal_recipe = serialize_edit_recipe_json(metal_state);
      const auto reopened_metal = parse_edit_recipe_json(metal_recipe);
      const auto reopened_style = reopened_metal.layers.at(text_layer_index()).toMap()
          .value(QStringLiteral("textTypography")).toMap();
      CHECK(validate_edit_recipe_json(metal_recipe).valid &&
          reopened_style.value(QStringLiteral("version")).toInt() == 9 &&
          reopened_style.value(QStringLiteral("material")).toString() == finish.id &&
          reopened_style.value(QStringLiteral("materialTint")).toString() == finish.tint &&
          reopened_style.value(QStringLiteral("materialDepth")).toDouble() == 8.0 &&
          reopened_style.value(QStringLiteral("materialLightAngle")).toDouble() == -45.0,
          "Metal tint, reflection, bevel and light survive recipe reopen");
      CHECK(render_output_image_with_backend(source, reopened_metal,
          QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64, RenderBackendPreference::CpuOnly) == metal_flatten,
          "Reopened chrome typography renders pixel-identically on CPU");
    }
    const auto tile_x = static_cast<int>(metal_index % 5) * metal_tile_width;
    const auto tile_y = static_cast<int>(metal_index / 5) * metal_tile_height;
    metal_painter.fillRect(QRect(tile_x, tile_y, metal_tile_width, metal_label_height),
        QColor(QStringLiteral("#242b35")));
    metal_painter.setPen(QColor(QStringLiteral("#f3f0e8")));
    metal_painter.drawText(QRect(tile_x + 10, tile_y, metal_tile_width - 20, metal_label_height),
        Qt::AlignLeft | Qt::AlignVCenter, QString(finish.id).replace(QLatin1Char('_'), QLatin1Char(' ')));
    const auto thumb = metal_flatten.scaled(metal_tile_width - 12, metal_tile_height - metal_label_height - 8,
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QRect image_rect(tile_x + (metal_tile_width - thumb.width()) / 2,
        tile_y + metal_label_height + (metal_tile_height - metal_label_height - thumb.height()) / 2,
        thumb.width(), thumb.height());
    metal_painter.drawImage(image_rect, thumb);
    if (!evidence.isEmpty())
      CHECK(metal_flatten.save(evidence + "/metal-" + finish.id + ".png"),
          "Capture rendered metal finish " << finish.id.toStdString());
  }
  metal_painter.end();
  if (!evidence.isEmpty())
    CHECK(metal_contact_sheet.save(evidence + "/typography-metal-contact-sheet.png"),
        "Capture the rendered ten-finish metal typography contact sheet");
  const auto material_recipe = serialize_edit_recipe_json(material_state);
  const auto reopened_material_state = parse_edit_recipe_json(material_recipe);
  const auto reopened_material_layer = reopened_material_state.layers.at(text_layer_index()).toMap();
  CHECK(validate_edit_recipe_json(material_recipe).valid &&
      reopened_material_layer.value("textTypography").toMap().value("material").toString() == "frosted_glass",
      "Version-8 material typography survives recipe validation and reopen");
  const auto reopened_material_flatten = render_output_image_with_backend(source, reopened_material_state,
      QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64, RenderBackendPreference::CpuOnly);
  CHECK(reopened_material_flatten == material_flatten,
      "Reopened material text exports pixel-identically on the deterministic CPU path");
  CHECK(controller->setPhotoLayerTextPathFromActiveSelection(text_layer_index(), 0) &&
      controller->photoLayerTypography(text_layer_index()).value("version").toInt() == 8 &&
      controller->photoLayerTypography(text_layer_index()).value("material").toString() == "frosted_glass",
      "Replacing the contour baseline preserves the version-8 material style");
  controller->undo();
  CHECK(state().layers == material_state.layers, "Contour replacement undo restores material text exactly");
  const auto material_path_state = state();
  const auto material_path_style = controller->photoLayerTypography(text_layer_index());
  CHECK(material_path_style.value(QStringLiteral("version")).toInt() == 8 &&
      material_path_style.value(QStringLiteral("material")).toString() == QStringLiteral("frosted_glass") &&
      !material_path_style.value(QStringLiteral("pathContours")).toList().isEmpty(),
      "Font-change fixture retains version-8 material styling and path contours");
  CHECK(!variable_font_proofs.empty(),
      "Font-change fixture has a measured variable-font proof; proofCount=" << variable_font_proofs.size());
  const auto& path_material_font_proof = variable_font_proofs.front();
  CHECK(!path_material_font_proof.family.isEmpty() && !path_material_font_proof.tag.isEmpty(),
      "Font-change fixture has an installed variable font axis; family="
          << path_material_font_proof.family.toStdString() << " tag="
          << path_material_font_proof.tag.toStdString() << " face="
          << path_material_font_proof.face.toStdString());
  const QVariantMap path_material_axis{{path_material_font_proof.tag,
      path_material_font_proof.axis.value(QStringLiteral("maximum"))}};
  CHECK(controller->setPhotoLayerTypography(text_layer_index(), {
      {QStringLiteral("family"), path_material_font_proof.family},
      {QStringLiteral("face"), path_material_font_proof.face},
      {QStringLiteral("axes"), path_material_axis}}),
      "Apply a supported variable axis to version-8 path/material typography");
  settle();
  const auto variable_material_path_state = state();
  const auto variable_material_path_style = controller->photoLayerTypography(text_layer_index());
  CHECK(variable_material_path_style.value(QStringLiteral("version")).toInt() == 8 &&
      variable_material_path_style.value(QStringLiteral("pathContours")).toList() ==
          material_path_style.value(QStringLiteral("pathContours")).toList() &&
      variable_material_path_style.value(QStringLiteral("material")).toString() == QStringLiteral("frosted_glass") &&
      variable_material_path_style.value(QStringLiteral("axes")).toMap() == path_material_axis,
      "Variable axes coexist with the preserved path/material style");
  QString family_after_axis_reset;
  for (const auto& family : QFontDatabase::families()) {
    if (family.compare(path_material_font_proof.family, Qt::CaseInsensitive) != 0) {
      family_after_axis_reset = family;
      break;
    }
  }
  CHECK(!family_after_axis_reset.isEmpty(), "Find an installed alternate family for the face reset");
  CHECK(controller->setPhotoLayerTypography(text_layer_index(), {
      {QStringLiteral("family"), family_after_axis_reset}, {QStringLiteral("face"), QString()}}),
      "Change family and clear its selected face on version-8 path/material typography");
  settle();
  const auto reset_axis_style = controller->photoLayerTypography(text_layer_index());
  CHECK(reset_axis_style.value(QStringLiteral("version")).toInt() == 8 &&
      reset_axis_style.value(QStringLiteral("family")).toString() == family_after_axis_reset &&
      reset_axis_style.value(QStringLiteral("face")).toString().isEmpty() &&
      reset_axis_style.value(QStringLiteral("axes")).toMap().isEmpty() &&
      reset_axis_style.value(QStringLiteral("pathContours")).toList() ==
          variable_material_path_style.value(QStringLiteral("pathContours")).toList() &&
      reset_axis_style.value(QStringLiteral("material")).toString() == QStringLiteral("frosted_glass"),
      "Family/face changes clear axes without downgrading path or material typography");
  const auto reset_axis_recipe = serialize_edit_recipe_json(state());
  CHECK(validate_edit_recipe_json(reset_axis_recipe).valid &&
      !render_output_image_with_backend(source, state(), QColorSpace(QColorSpace::SRgb),
          QImage::Format_RGBA64, RenderBackendPreference::CpuOnly).isNull(),
      "Updated version-8 path/material text remains valid and renderable");
  controller->undo();
  CHECK(state().layers == variable_material_path_state.layers,
      "Family/face change undo restores the variable-axis path/material style");
  controller->undo();
  CHECK(state().layers == material_path_state.layers,
      "Variable-axis path/material style edit is one undo step");
  controller->undo();
  CHECK(state().layers == material_reset_state.layers, "Undo restores material tint and transmission");
  controller->undo();
  CHECK(state().layers == blur_value_state.layers, "Undo restores the user-entered blur value");
  controller->undo();
  CHECK(state().layers == glass_mode_state.layers, "Undo restores the declared material blur default");
  controller->undo();
  CHECK(state().layers == final_state.layers, "Undo removes the material selection exactly");
  auto variable_axis_state = final_state;
  auto variable_axis_layer = variable_axis_state.layers.at(text_layer_index()).toMap();
  auto variable_axis_typography = variable_axis_layer.value("textTypography").toMap();
  for (const auto* key : {"pathPoints", "pathContours", "pathStart", "pathEnd", "pathOffset",
           "pathSpacing", "pathSide", "pathClosed", "pathExclusion"})
    variable_axis_typography.remove(QLatin1String(key));
  variable_axis_typography.insert("version", 4);
  variable_axis_typography.insert("axes", QVariantMap{{"wght", 625.0}});
  variable_axis_layer.insert("textTypography", variable_axis_typography);
  variable_axis_state.layers[text_layer_index()] = variable_axis_layer;
  const auto variable_axis_recipe = serialize_edit_recipe_json(variable_axis_state);
  const auto parsed_variable_axis_state = parse_edit_recipe_json(variable_axis_recipe);
  CHECK(validate_edit_recipe_json(variable_axis_recipe).valid &&
      parsed_variable_axis_state.layers.at(text_layer_index()).toMap().value("textTypography").toMap() == variable_axis_typography,
      "Version-4 variable-font axis styles survive desktop recipe validation and reopen");
  QFile file(temp.filePath("typography.json"));CHECK(file.open(QIODevice::WriteOnly)&&file.write(recipe.toUtf8())>0,"Save document file");file.close();
  CHECK(controller->exportFlattenedPhoto(temp.filePath("export.png")),"Start actual text export: "
      << controller->photoExportError().toStdString() << " status=" << controller->status().toStdString());
  CHECK(wait_until([&]{return !controller->photoExportBusy();})&&controller->photoExportError().isEmpty(),"Text export completes");
  const QImage exported(temp.filePath("export.png"));CHECK(!exported.isNull(),"Text export loads");
  CHECK(file.open(QIODevice::ReadOnly),"Read saved document");
  auto reopened=std::make_unique<EditorPreviewController>();
  reopened->loadAsset("typography-reopened",source_path,{},{},{},{},QString::fromUtf8(file.readAll()));
  CHECK(EditorPreviewControllerTestAccess::state(*reopened).layers==final_state.layers,"New controller restores typography and point geometry");
  CHECK(reopened->exportFlattenedPhoto(temp.filePath("reopened.png")),"Reopened text export");
  CHECK(wait_until([&]{return !reopened->photoExportBusy();})&&reopened->photoExportError().isEmpty(),"Reopened export completes");
  CHECK(exported==QImage(temp.filePath("reopened.png")),"Text export must remain pixel-identical after disk reopen");

  if (controller->photoLayerTypography(text_layer_index()).value(QStringLiteral("weight")).toInt() >= 600) {
    CHECK(controller->setPhotoLayerTypography(text_layer_index(), {{QStringLiteral("weight"), 400}}),
        "Set the primary contour layer to regular before the shared-bold toggle proof");
  }
  settle();
  CHECK(controller->photoLayerTypography(text_layer_index()).value(QStringLiteral("weight")).toInt() == 400,
      "Shared-bold fixture starts from a regular primary text layer");
  CHECK(controller->addParagraphTextLayer(QStringLiteral("Second selected title"), 128, 144, 260, 96, 640, 512),
      "Create a second independently editable text layer for multi-selection typography proof");
  CHECK(wait_until([&] { return !controller->processing() && !controller->fullRenderRunning(); }),
      "Second selected text layer settles before shared typography edit");
  QString secondary_text_layer_id;
  for (const auto& layer_value : controller->photoLayers()) {
    const auto layer = layer_value.toMap();
    if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("text") &&
        layer.value(QStringLiteral("text")).toString() == QStringLiteral("Second selected title")) {
      secondary_text_layer_id = layer.value(QStringLiteral("id")).toString();
      break;
    }
  }
  CHECK(!secondary_text_layer_id.isEmpty(), "Find the second text layer by its stable layer id");
  root->setProperty("secondarySelectedTextLayerId", secondary_text_layer_id);
  root->setProperty("multiTargetSelection", true);
  settle();
  const auto before_multi_style = state();
  auto* multi_panel = qobject_cast<QQuickItem*>(find("typographyPanel"));
  auto* multi_notice = qobject_cast<QQuickItem*>(find("photoTypographyMultiTargetNotice"));
  auto* multi_bold = qobject_cast<QQuickItem*>(find("photoLayerTextBoldButton"));
  const auto selected_text_indices = multi_panel
      ? multi_panel->property("selectedLayerIndices").toList() : QVariantList{};
  const auto expected_text_target_count = 2;
  CHECK(multi_panel && multi_panel->property("multiTarget").toBool() && multi_notice &&
      multi_notice->isVisible() && selected_text_indices.size() == expected_text_target_count &&
      selected_text_indices.size() > 1 && multi_bold,
      "Typography panel exposes the selected text-layer count and shared style action");
  bool shared_targets_are_unlocked_text = true;
  for (const auto& target : selected_text_indices) {
    const auto layer = before_multi_style.layers.at(target.toInt()).toMap();
    shared_targets_are_unlocked_text = shared_targets_are_unlocked_text &&
        layer.value(QStringLiteral("kind")).toString() == QStringLiteral("text") &&
        !layer.value(QStringLiteral("locked")).toBool();
  }
  CHECK(shared_targets_are_unlocked_text,
      "The shared-style fixture selects exactly two unlocked text layers");
  const QVariantMap shared_bold_changes{{QStringLiteral("weight"), 700},
      {QStringLiteral("face"), QString()}};
  controller->beginCoalescedEdit();
  bool all_single_target_setters_apply = true;
  QStringList single_target_results;
  for (const auto& target : selected_text_indices) {
    const auto index = target.toInt();
    const bool applied = controller->setPhotoLayerTypography(index, shared_bold_changes);
    const auto style = controller->photoLayerTypography(index);
    all_single_target_setters_apply = all_single_target_setters_apply &&
        (applied || style.value(QStringLiteral("weight")).toInt() == 700);
    single_target_results.append(QStringLiteral("%1:%2:%3:%4")
        .arg(index).arg(applied).arg(style.value(QStringLiteral("weight")).toInt())
        .arg(controller->status()));
  }
  const auto after_single_target_probe = state().layers;
  controller->endCoalescedEdit();
  if (after_single_target_probe != before_multi_style.layers)
    controller->undo();
  CHECK(state().layers == before_multi_style.layers,
      "Single-target setter probe is rolled back before the batch test");
  CHECK(all_single_target_setters_apply,
      "Each selected text style can be applied through the single-target API: "
      << single_target_results.join(QStringLiteral(" | ")).toStdString());
  CHECK(!controller->setPhotoLayersTypography(selected_text_indices,
          {{QStringLiteral("text"), QStringLiteral("must remain layer-owned")}}) &&
      !controller->setPhotoLayersTypography(selected_text_indices,
          {{QStringLiteral("face"), QStringLiteral("Looksy nonexistent face fixture")},
           {QStringLiteral("weight"), 600}}) && state().layers == before_multi_style.layers,
      "Multi-target styling rejects content edits and invalid faces atomically");
  CHECK(controller->setPhotoLayersTypography(selected_text_indices,
          shared_bold_changes),
      "Batch typography API applies a valid shared style to multiple text targets");
  bool direct_batch_applied_to_each = true;
  for (const auto& target : selected_text_indices)
    direct_batch_applied_to_each = direct_batch_applied_to_each &&
        controller->photoLayerTypography(target.toInt()).value(QStringLiteral("weight")).toInt() == 700;
  CHECK(direct_batch_applied_to_each,
      "Direct shared-style application updates every selected text target");
  controller->undo();
  CHECK(state().layers == before_multi_style.layers,
      "Direct batch API application remains one undo step");
  settle();
  CHECK(!multi_bold->property("active").toBool(),
      "Shared-bold toggle starts in its regular state for the selected primary layer");
  auto* typography_scroll = qobject_cast<QQuickItem*>(find("typographyProofScroll"));
  CHECK(typography_scroll, "Typography proof scroll view exists");
  reveal_control(multi_bold);
  const auto bold_center = multi_bold->mapToScene(QPointF(multi_bold->width() / 2, multi_bold->height() / 2));
  const QRectF typography_viewport(typography_scroll->mapToScene(QPointF(0, 0)),
      QSizeF(typography_scroll->width(), typography_scroll->height()));
  CHECK(typography_viewport.contains(bold_center),
      "Shared-bold control is scrolled into the visible viewport before pointer activation");
  click(bold_center);
  CHECK(multi_bold->property("active").toBool(),
      "Native pointer input activates the shared-bold control");
  const auto after_multi_style = state();
  CHECK(after_multi_style.layers.size() == before_multi_style.layers.size(),
      "Shared typography leaves the layer stack shape unchanged");
  for (int selection_index = 0; selection_index < selected_text_indices.size(); ++selection_index) {
    const auto index = selected_text_indices.at(selection_index).toInt();
    const auto before_layer = before_multi_style.layers.at(index).toMap();
    const auto after_layer = after_multi_style.layers.at(index).toMap();
    const auto after_style = controller->photoLayerTypography(index);
    CHECK(before_layer.value("kind").toString() == QStringLiteral("text") &&
        after_layer.value("text") == before_layer.value("text") && after_style.value("weight").toInt() == 700,
        "Shared bold applies to each selected layer without replacing its text");
    for (const auto* key : {"x", "y", "pixelWidth", "pixelHeight", "canvasWidth", "canvasHeight",
             "scale", "rotation", "textOriginX", "textOriginY"})
      CHECK(after_layer.value(QLatin1String(key)) == before_layer.value(QLatin1String(key)),
          "Shared typography preserves selected-layer geometry: " << key);
  }
  controller->undo();
  CHECK(state().layers == before_multi_style.layers,
      "Shared typography on multiple selected text layers is one undo step");
  root->setProperty("multiTargetSelection", false);
  settle();
  CHECK(!multi_panel->property("multiTarget").toBool() &&
      multi_panel->property("selectedLayerIndices").toList().size() == 1 &&
      multi_panel->property("layerIndex").toInt() == text_layer_index(),
      "Leaving shared styling restores the selected contour layer as the single inspector target");

  CHECK(wait_until([&]{return !reopened->processing()&&!reopened->fullRenderRunning();}),
      "Reopened preview settles before controller teardown");
  CHECK(wait_until([&]{return !controller->processing()&&!controller->fullRenderRunning();}),
      "Final preview settles before controller teardown");
  if(!evidence.isEmpty()) {
    CHECK(exported.save(evidence+"/typography-export.png"),"Typography export evidence");
    settle();CHECK(window.grabWindow().save(evidence+"/paragraph-native.png"),"Paragraph screenshot");
  }
  std::cout << "Native point/paragraph creation, growing bounds, composition, Cancel, typography, font fallback, undo, disk reopen and identical PNG exports passed; photographic=" << photographic << '\n';
  return 0;
}

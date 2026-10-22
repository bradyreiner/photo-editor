#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <utility>

#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>

#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "edit_adjustment_patch.h"
#include "canonical_composite_mask_alpha.h"
#include "photo_render_utils.h"
#include "thumbnail_refresh_queue_service.h"
#include "photo_editor/pipeline/color_adjustments.h"

namespace photo_editor::desktop {

struct EditorPreviewControllerTestAccess {
  static bool stable_mask_reselection() {
    {
      EditorPreviewController deferred;
      deferred.asset_id_ = QStringLiteral("deferred-scope-fixture");
      deferred.beginCoalescedEdit();
      deferred.pending_brush_commit_scheduled_ = true;
      deferred.endCoalescedEdit();
      deferred.beginCoalescedEdit();
      deferred.pending_brush_commit_scheduled_ = false;
      QElapsedTimer callback_window;
      callback_window.start();
      while (callback_window.elapsed() < 40) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
      }
      if (deferred.history_service_.transactionDepth() != 2) {
        std::cerr << "deferred completion must not close an active nested control edit\n";
        return false;
      }
      deferred.endCoalescedEdit();
      if (deferred.history_service_.transactionActive() || deferred.edit_transaction_end_pending_) {
        std::cerr << "finishing the nested control must drain pending outer completion\n";
        return false;
      }
    }
    {
      QTemporaryDir fixture;
      const auto path = fixture.filePath(QStringLiteral("strokes.png"));
      QImage image(600, 400, QImage::Format_RGB32);
      image.fill(QColor(120, 90, 65));
      if (!fixture.isValid() || !image.save(path)) { std::cerr << "multistroke checkpoint 1 failed\n"; return false; }
      EditorPreviewController strokes;
      strokes.loadAsset(QStringLiteral("multistroke-fixture"), path, {}, {}, {}, {}, {});
      const auto settle = [&]() {
        QElapsedTimer timer;
        timer.start();
        do {
          QCoreApplication::processEvents();
          if (!strokes.processing() && !strokes.pending_brush_commit_scheduled_ &&
              strokes.pending_brush_commits_.isEmpty() && !strokes.brush_raster_commit_active_ &&
              !strokes.mask_worker_coordinator_.brushRasterCommitWatcher().isRunning()) return true;
          QThread::msleep(2);
        } while (timer.elapsed() < 10000);
        { std::cerr << "multistroke checkpoint 2 failed\n"; return false; }
      };
      if (!settle()) { std::cerr << "multistroke checkpoint 3 failed\n"; return false; }
      strokes.addMask(QStringLiteral("radial"));
      const auto original_masks = strokes.maskStack();
      const auto original_history = strokes.editHistoryEntries();
      strokes.beginCoalescedEdit();
      strokes.startSelectionEdgeBrush(false);
      for (int stroke = 0; stroke < 2; ++stroke) {
        const auto before_stroke = strokes.maskStack();
        strokes.beginActiveBrushStroke();
        strokes.addBrushPoint(0.2 + 0.3 * stroke, 0.3);
        strokes.addBrushPoint(0.3 + 0.3 * stroke, 0.5);
        strokes.endActiveBrushStroke();
        if (!settle() || strokes.editHistoryEntries().size() != original_history.size() ||
            strokes.maskStack() == before_stroke || !strokes.activeMaskCommitted()) { std::cerr << "multistroke checkpoint 4 failed stroke=" << stroke << " history=" << strokes.editHistoryEntries().size() << "/" << original_history.size() << " changed=" << (strokes.maskStack() != before_stroke) << " committed=" << strokes.activeMaskCommitted() << " status=" << strokes.status().toStdString() << "\n"; return false; }
      }
      strokes.endCoalescedEdit();
      if (!settle() || strokes.editHistoryEntries().size() != original_history.size() + 1) { std::cerr << "multistroke checkpoint 5 failed\n"; return false; }
      const auto finished_masks = strokes.maskStack();
      if (finished_masks == original_masks) { std::cerr << "multistroke checkpoint 6 failed\n"; return false; }
      strokes.undo();
      if (strokes.maskStack() != original_masks) { std::cerr << "multistroke checkpoint 7 failed\n"; return false; }
      strokes.redo();
      if (strokes.maskStack() != finished_masks) { std::cerr << "multistroke checkpoint 8 failed\n"; return false; }
    }
    {
      EditorPreviewController rollback;
      rollback.asset_id_ = QStringLiteral("rollback-fixture");
      rollback.addMask(QStringLiteral("radial"));
      if (!rollback.hasActiveMask() || rollback.maskStack().size() != 1) return false;
      const auto original = rollback.currentState();
      const auto history = rollback.editHistoryEntries();
      rollback.beginCoalescedEdit();
      rollback.setExposure(1.25);
      rollback.beginCoalescedEdit();
      rollback.setContrast(18.0);
      rollback.startSelectionEdgeBrush(false);
      if (rollback.maskStack().size() != 2 || rollback.activeMaskKind() != QStringLiteral("brush")) return false;
      rollback.setActiveMaskFeather(0.73);
      if (rollback.currentState().exposure == original.exposure ||
          rollback.currentState().contrast == original.contrast) return false;
      if (!rollback.cancelCoalescedEdit() || rollback.history_service_.transactionActive() ||
          rollback.currentState().exposure != original.exposure ||
          rollback.currentState().contrast != original.contrast || rollback.editHistoryEntries() != history ||
          rollback.maskStack() != original.mask_stack || rollback.activeMaskIndex() != original.active_mask_index ||
          rollback.cancelCoalescedEdit()) return false;
    }
    QTemporaryDir storage;
    if (!storage.isValid()) return false;
    EditorPreviewController controller;
    const auto selection_path = storage.filePath(QStringLiteral("selection"));
    controller.mask_session_.setSelectionStorageDirectory(selection_path);
    controller.mask_session_.setAssetScope(QStringLiteral("close-photo"));
    const QVariantMap first{{QStringLiteral("id"), QStringLiteral("component-a")},
                            {QStringLiteral("maskSetId"), QStringLiteral("set-a")}};
    const QVariantMap second{{QStringLiteral("id"), QStringLiteral("component-b")},
                             {QStringLiteral("maskSetId"), QStringLiteral("set-a")}};
    controller.mask_session_.replaceStack({first, second}, 1);
    const auto stack = controller.maskStack();
    for (int cycle = 0; cycle < 20; ++cycle) {
      controller.deselectActiveMask();
      if (!controller.selectMaskSet(QStringLiteral("set-a")) ||
          controller.activeMaskIndex() != 1 || controller.maskStack() != stack)
        return false;
    }
    controller.mask_session_.replaceStack({second, first}, -1);
    QFile obstruction(selection_path);
    if (!obstruction.open(QIODevice::WriteOnly)) return false;
    obstruction.close();
    QString close_error;
    if (controller.prepareRecipeClose(&close_error) || close_error.isEmpty()) return false;
    if (!obstruction.remove() || !controller.prepareRecipeClose(&close_error)) return false;
    MaskSession restored;
    restored.setSelectionStorageDirectory(selection_path);
    restored.setAssetScope(QStringLiteral("close-photo"));
    restored.replaceStack({second, first}, -1);
    if (restored.indexForMaskSet(QStringLiteral("set-a")) != 0) return false;
    if (!controller.selectMaskSet(QStringLiteral("set-a"))) return false;
    controller.setActiveMaskLocked(true);
    const auto locked_stack = controller.maskStack();
    const auto locked_history = controller.editHistoryEntries();
    controller.startSelectionEdgeBrush(false);
    controller.startSelectionEdgeBrush(true);
    if (controller.refineActiveMaskWithSmartSelect() || controller.smartSelectPreviewActive() ||
        controller.status() != controller.activeMaskEditDisabledReason()) return false;
    if (controller.resetActiveMaskRefinement()) return false;
    if (controller.maskStack() != locked_stack || controller.editHistoryEntries() != locked_history ||
        controller.status() != controller.activeMaskEditDisabledReason()) return false;
    return controller.selectMaskSet(QStringLiteral("set-a")) &&
           controller.activeMaskIndex() == 0 &&
           !controller.selectMaskSet(QStringLiteral("missing")) &&
           controller.activeMaskIndex() == 0;
  }

  static int photo_layer_thumbnail_cache_hit_count(
      const EditorPreviewController& controller) {
    return controller.thumbnail_cache_session_.photoLayerHitCount();
  }

  static int photo_layer_thumbnail_refresh_count(
      const EditorPreviewController& controller) {
    return controller.thumbnail_cache_session_.photoLayerRefreshCount();
  }

  static int thumbnail_entry_cache_hit_count(const EditorPreviewController& controller) {
    return controller.thumbnail_cache_session_.entryHitCount();
  }

  static int thumbnail_entry_cache_store_count(const EditorPreviewController& controller) {
    return controller.thumbnail_cache_session_.entryStoreCount();
  }

  static bool thumbnail_refresh_deferred(const EditorPreviewController& controller) {
    return controller.thumbnailRefreshDeferred();
  }

  static bool thumbnail_refresh_queue_pending(const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_coordinator_.pending();
  }

  static int thumbnail_refresh_queue_pending_generation(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_coordinator_.pendingGeneration();
  }

  static int thumbnail_refresh_queue_dispatched_count(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_coordinator_.dispatchedCount();
  }

  static bool thumbnail_refresh_queue_worker_running(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_coordinator_.workerRunning();
  }

  static int thumbnail_refresh_queue_worker_started_count(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_coordinator_.workerStartedCount();
  }

  static int thumbnail_refresh_queue_worker_completed_count(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_coordinator_.workerCompletedCount();
  }

  static int thumbnail_refresh_queue_stale_result_count(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_coordinator_.staleResultCount();
  }

  static QString thumbnail_refresh_queue_active_job_id(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_coordinator_.activeJobId();
  }

  static int pending_brush_commit_count(const EditorPreviewController& controller) {
    return static_cast<int>(controller.pending_brush_commits_.size());
  }

  static void forceThumbnailEntryCacheLookup(EditorPreviewController& controller) {
    controller.thumbnail_cache_session_.maskDirty() = true;
    controller.thumbnail_cache_session_.photoLayerDirty() = true;
    controller.thumbnail_cache_session_.maskSetsWithThumbnails().clear();
    controller.thumbnail_cache_session_.photoLayersWithThumbnails().clear();
    controller.thumbnail_cache_session_.photoLayerAssetKey().clear();
  }

  static bool full_render_timer_active(const EditorPreviewController& controller) {
    return controller.render_scheduler_.fullActive();
  }

  static bool full_render_pending(const EditorPreviewController& controller) {
    return controller.full_request_pending_;
  }

  static bool full_render_running(const EditorPreviewController& controller) {
    return controller.full_render_watcher_.isRunning();
  }

  static bool photo_layer_adjustment_render_timer_active(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_timer_.isActive();
  }

  static int render_generation(const EditorPreviewController& controller) {
    return controller.preview_session_.generation();
  }

  static int edit_intent_count(const EditorPreviewController& controller) {
    return controller.edit_intent_count_;
  }

  static bool photo_layer_thumbnail_cache_dirty(const EditorPreviewController& controller) {
    return controller.thumbnail_cache_session_.photoLayerDirty();
  }

  static bool mask_thumbnail_cache_dirty(const EditorPreviewController& controller) {
    return controller.thumbnail_cache_session_.maskDirty();
  }
};

class TestImageSink final : public EditorPreviewImageSink {
 public:
  void setImage(const QString& key, const QImage& image) override { images_.insert(key, image); }
  void clearImage(const QString& key) override { images_.remove(key); }

  [[nodiscard]] QImage image(const QString& key) const { return images_.value(key); }

 private:
  QHash<QString, QImage> images_;
};

}  // namespace photo_editor::desktop

namespace {

bool almost_equal(double left, double right, double tolerance = 1e-6) {
  return std::abs(left - right) <= tolerance;
}

QVariantMap curve_point(double x, double y) {
  QVariantMap point;
  point.insert(QStringLiteral("x"), x);
  point.insert(QStringLiteral("y"), y);
  return point;
}

QVariantList curve_points(std::initializer_list<std::pair<double, double>> points) {
  QVariantList values;
  for (const auto& point : points) {
    values.append(curve_point(point.first, point.second));
  }
  return values;
}

bool curve_point_matches(const QVariantList& points, int index, double x, double y) {
  if (index < 0 || index >= points.size()) {
    return false;
  }
  const auto point = points.at(index).toMap();
  return almost_equal(point.value(QStringLiteral("x")).toDouble(), x) &&
         almost_equal(point.value(QStringLiteral("y")).toDouble(), y);
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool group_list_contains(const QVariantList& groups, const QString& key, const QString& label) {
  for (const auto& value : groups) {
    const auto group = value.toMap();
    if (group.value(QStringLiteral("key")).toString() == key &&
        group.value(QStringLiteral("label")).toString() == label) {
      return true;
    }
  }
  return false;
}

void wait_for_thumbnail_debounce() {
  for (auto index = 0; index < 20; ++index) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(50);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

bool wait_for_preview_idle(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms = 4000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!controller.processing() &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
            controller) &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_running(
            controller) &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
            controller) &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
            photo_layer_adjustment_render_timer_active(controller)) {
      return true;
    }
    QThread::msleep(10);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  return !controller.processing() &&
         !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
             controller) &&
         !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_running(
             controller) &&
         !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
             controller) &&
         !photo_editor::desktop::EditorPreviewControllerTestAccess::
             photo_layer_adjustment_render_timer_active(controller);
}

bool wait_for_brush_commit(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms = 2000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (photo_editor::desktop::EditorPreviewControllerTestAccess::pending_brush_commit_count(
            controller) == 0) {
      return true;
    }
    QThread::msleep(25);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return photo_editor::desktop::EditorPreviewControllerTestAccess::pending_brush_commit_count(
             controller) == 0;
}

bool finish_brush_stroke(photo_editor::desktop::EditorPreviewController& controller) {
  controller.endActiveBrushStroke();
  return wait_for_brush_commit(controller);
}

bool wait_for_thumbnail_queue_dispatch(
    photo_editor::desktop::EditorPreviewController& controller,
    int minimum_dispatch_count,
    int minimum_completed_count = -1,
    int timeout_ms = 3000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (photo_editor::desktop::EditorPreviewControllerTestAccess::
                thumbnail_refresh_queue_dispatched_count(controller) >=
            minimum_dispatch_count &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
            thumbnail_refresh_queue_pending(controller) &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
            thumbnail_refresh_deferred(controller) &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
            thumbnail_refresh_queue_worker_running(controller) &&
        (minimum_completed_count < 0 ||
         photo_editor::desktop::EditorPreviewControllerTestAccess::
                 thumbnail_refresh_queue_worker_completed_count(controller) >=
             minimum_completed_count)) {
      return true;
    }
    QThread::msleep(25);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return photo_editor::desktop::EditorPreviewControllerTestAccess::
             thumbnail_refresh_queue_dispatched_count(controller) >=
             minimum_dispatch_count &&
         !photo_editor::desktop::EditorPreviewControllerTestAccess::
             thumbnail_refresh_queue_pending(controller) &&
         !photo_editor::desktop::EditorPreviewControllerTestAccess::
             thumbnail_refresh_deferred(controller) &&
         !photo_editor::desktop::EditorPreviewControllerTestAccess::
             thumbnail_refresh_queue_worker_running(controller) &&
         (minimum_completed_count < 0 ||
          photo_editor::desktop::EditorPreviewControllerTestAccess::
                  thumbnail_refresh_queue_worker_completed_count(controller) >=
              minimum_completed_count);
}

QVariantMap wait_for_fresh_photo_layer_thumbnail(
    photo_editor::desktop::EditorPreviewController& controller,
    int index = 0,
    int timeout_ms = 6000) {
  QElapsedTimer timer;
  timer.start();
  QVariantMap layer;
  while (timer.elapsed() < timeout_ms) {
    const auto layers = controller.photoLayers();
    if (index >= 0 && index < layers.size()) {
      layer = layers.at(index).toMap();
    }
    if (!layer.value(QStringLiteral("thumbnailSource")).toString().isEmpty() &&
        layer.value(QStringLiteral("thumbnailReady")).toBool() &&
        !layer.value(QStringLiteral("thumbnailStale")).toBool() &&
        !layer.value(QStringLiteral("thumbnailProgressive")).toBool() &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
            photo_layer_thumbnail_cache_dirty(controller)) {
      return layer;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(25);
  }
  return layer;
}

QVariantList wait_for_fresh_mask_thumbnails(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms = 6000) {
  QElapsedTimer timer;
  timer.start();
  QVariantList sets;
  while (timer.elapsed() < timeout_ms) {
    sets = controller.maskSets();
    const auto all_ready = !sets.isEmpty() && std::all_of(
        sets.cbegin(), sets.cend(), [](const QVariant& value) {
          const auto set = value.toMap();
          return !set.value(QStringLiteral("thumbnailSource")).toString().isEmpty() &&
                 set.value(QStringLiteral("thumbnailReady")).toBool() &&
                 !set.value(QStringLiteral("thumbnailStale")).toBool() &&
                 !set.value(QStringLiteral("thumbnailProgressive")).toBool();
        });
    if (all_ready &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
            mask_thumbnail_cache_dirty(controller)) {
      return sets;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(25);
  }
  return sets;
}

QImage scaled_square_reference_image(QImage image, int edge) {
  if (image.isNull() || edge <= 0) {
    return {};
  }
  if (image.format() != QImage::Format_ARGB32_Premultiplied) {
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  }
  const auto scaled = image.scaled(
      edge,
      edge,
      Qt::KeepAspectRatioByExpanding,
      Qt::SmoothTransformation);
  if (scaled.width() < edge || scaled.height() < edge) {
    return {};
  }
  return scaled.copy((scaled.width() - edge) / 2, (scaled.height() - edge) / 2, edge, edge);
}

QVariantMap required_map(const QVariantMap& parent, const QString& key) {
  return parent.value(key).toMap();
}

bool write_thumbnail_fixture(const QString& file_path) {
  QImage image(160, 96, QImage::Format_RGB32);
  for (int y = 0; y < image.height(); ++y) {
    const auto fy = static_cast<double>(y) / static_cast<double>(image.height() - 1);
    for (int x = 0; x < image.width(); ++x) {
      const auto fx = static_cast<double>(x) / static_cast<double>(image.width() - 1);
      image.setPixelColor(
          x,
          y,
          QColor(
              qBound(0, static_cast<int>(30 + 180 * fx), 255),
              qBound(0, static_cast<int>(46 + 150 * fy), 255),
              qBound(0, static_cast<int>(92 + 90 * (1.0 - fx)), 255)));
    }
  }
  return image.save(file_path);
}

bool has_evaluated_alpha_thumbnail_metadata(const QVariantMap& value) {
  const auto width = value.value(QStringLiteral("thumbnailAlphaWidth")).toInt();
  const auto height = value.value(QStringLiteral("thumbnailAlphaHeight")).toInt();
  const auto bytes = value.value(QStringLiteral("thumbnailAlphaBytes")).toInt();
  const auto hash = value.value(QStringLiteral("thumbnailAlphaHash")).toString();
  const auto signature = value.value(QStringLiteral("thumbnailFreshnessSignature")).toString();
  const auto provider_cache_key =
      value.value(QStringLiteral("maskAlphaProviderCacheKey")).toString();
  return value.value(QStringLiteral("thumbnailAlphaSource")).toString() ==
             QStringLiteral("evaluated-mask-alpha") &&
         width > 0 && height > 0 && bytes == width * height &&
          hash.startsWith(QStringLiteral("sha256:")) &&
          hash.size() == QStringLiteral("sha256:").size() + 64 &&
          provider_cache_key.startsWith(QStringLiteral("sha1:")) &&
          value.contains(QStringLiteral("maskAlphaProviderCacheHit")) &&
           signature.contains(QStringLiteral("alpha:")) && signature.contains(hash);
}

bool has_thumbnail_pixel_metrics(
    const QVariantMap& value,
    int expected_display_edge,
    int minimum_backing_edge) {
  const auto backing_width = value.value(QStringLiteral("thumbnailBackingWidth")).toInt();
  const auto backing_height = value.value(QStringLiteral("thumbnailBackingHeight")).toInt();
  const auto display_width = value.value(QStringLiteral("thumbnailDisplayWidth")).toInt();
  const auto display_height = value.value(QStringLiteral("thumbnailDisplayHeight")).toInt();
  const auto device_pixel_ratio =
      value.value(QStringLiteral("thumbnailDevicePixelRatio")).toDouble();
  const auto signature =
      value.value(QStringLiteral("thumbnailPixelRatioSignature")).toString();
  return backing_width >= minimum_backing_edge && backing_height >= minimum_backing_edge &&
         display_width == expected_display_edge && display_height == expected_display_edge &&
         device_pixel_ratio >= 2.0 &&
         signature.contains(QStringLiteral("backing:%1x%2").arg(backing_width).arg(backing_height)) &&
         signature.contains(QStringLiteral("display:%1x%1").arg(expected_display_edge)) &&
          signature.contains(QStringLiteral("dpr:"));
}

QImage decoded_thumbnail_source(const QString& source) {
  const auto prefix = QStringLiteral("data:image/png;base64,");
  if (!source.startsWith(prefix)) {
    return {};
  }
  return QImage::fromData(QByteArray::fromBase64(source.mid(prefix.size()).toLatin1()), "PNG");
}

bool thumbnail_pixels_match_metadata(const QVariantMap& value) {
  const auto image = decoded_thumbnail_source(value.value(QStringLiteral("thumbnailSource")).toString());
  return !image.isNull() &&
         image.width() == value.value(QStringLiteral("thumbnailBackingWidth")).toInt() &&
         image.height() == value.value(QStringLiteral("thumbnailBackingHeight")).toInt();
}

bool thumbnail_alpha_matches_metadata(const QVariantMap& value) {
  const auto image = decoded_thumbnail_source(value.value(QStringLiteral("thumbnailSource")).toString());
  if (image.isNull() ||
      image.width() != value.value(QStringLiteral("thumbnailAlphaWidth")).toInt() ||
      image.height() != value.value(QStringLiteral("thumbnailAlphaHeight")).toInt()) {
    return false;
  }

  QByteArray alpha;
  alpha.reserve(image.width() * image.height());
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      alpha.append(static_cast<char>(image.pixelColor(x, y).alpha()));
    }
  }

  const auto alpha_hash = QStringLiteral("sha256:%1").arg(QString::fromLatin1(
      QCryptographicHash::hash(alpha, QCryptographicHash::Sha256).toHex()));
  return alpha.size() == value.value(QStringLiteral("thumbnailAlphaBytes")).toInt() &&
         alpha_hash == value.value(QStringLiteral("thumbnailAlphaHash")).toString();
}

bool thumbnail_is_clean_grayscale_alpha_matte(const QVariantMap& value) {
  if (value.value(QStringLiteral("thumbnailVisualMode")).toString() !=
          QStringLiteral("grayscale-alpha") ||
      value.value(QStringLiteral("thumbnailRgbPolicy")).toString() !=
          QStringLiteral("rgb-equals-alpha") ||
      !value.value(QStringLiteral("thumbnailDebugColorFree")).toBool()) {
    return false;
  }

  const auto image = decoded_thumbnail_source(value.value(QStringLiteral("thumbnailSource")).toString());
  if (image.isNull()) {
    return false;
  }
  const auto argb = image.convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < argb.height(); ++y) {
    for (int x = 0; x < argb.width(); ++x) {
      const auto pixel = argb.pixelColor(x, y);
      if (pixel.red() != pixel.green() || pixel.red() != pixel.blue() ||
          pixel.red() != pixel.alpha()) {
        return false;
      }
    }
  }
  return true;
}

bool thumbnail_has_clean_solid_color_overlay(const QVariantMap& value) {
  if (value.value(QStringLiteral("thumbnailOverlayVisualMode")).toString() !=
          QStringLiteral("solid-color-alpha") ||
      value.value(QStringLiteral("thumbnailOverlayRgbPolicy")).toString() !=
          QStringLiteral("solid-color-preserves-alpha") ||
      value.value(QStringLiteral("thumbnailOverlayContract")).toString() !=
          QStringLiteral("mask-thumbnail-overlay:v1|source:final-alpha|color:solid|debug-rgb:false") ||
      !value.value(QStringLiteral("thumbnailOverlayDebugColorFree")).toBool()) {
    return false;
  }

  const auto expected_color =
      QColor(value.value(QStringLiteral("thumbnailOverlayColor")).toString());
  if (!expected_color.isValid() || expected_color != QColor(0, 230, 195)) {
    return false;
  }

  const auto matte =
      decoded_thumbnail_source(value.value(QStringLiteral("thumbnailSource")).toString())
          .convertToFormat(QImage::Format_ARGB32);
  const auto overlay =
      decoded_thumbnail_source(value.value(QStringLiteral("thumbnailOverlaySource")).toString())
          .convertToFormat(QImage::Format_ARGB32);
  if (matte.isNull() || overlay.isNull() || matte.size() != overlay.size()) {
    return false;
  }

  auto saw_visible_alpha = false;
  for (int y = 0; y < overlay.height(); ++y) {
    for (int x = 0; x < overlay.width(); ++x) {
      const auto matte_pixel = matte.pixelColor(x, y);
      const auto overlay_pixel = overlay.pixelColor(x, y);
      if (overlay_pixel.alpha() != matte_pixel.alpha()) {
        return false;
      }
      if (overlay_pixel.alpha() > 0) {
        saw_visible_alpha = true;
        if (overlay_pixel.red() != expected_color.red() ||
            overlay_pixel.green() != expected_color.green() ||
            overlay_pixel.blue() != expected_color.blue()) {
          return false;
        }
      }
    }
  }
  return saw_visible_alpha;
}

int thumbnail_max_rgba_delta(const QImage& left, const QImage& right) {
  if (left.size() != right.size() || left.isNull() || right.isNull()) {
    return 255;
  }
  const auto left_argb = left.convertToFormat(QImage::Format_ARGB32);
  const auto right_argb = right.convertToFormat(QImage::Format_ARGB32);
  auto max_delta = 0;
  for (int y = 0; y < left_argb.height(); ++y) {
    for (int x = 0; x < left_argb.width(); ++x) {
      const auto lhs = left_argb.pixelColor(x, y);
      const auto rhs = right_argb.pixelColor(x, y);
      max_delta = std::max(max_delta, std::abs(lhs.red() - rhs.red()));
      max_delta = std::max(max_delta, std::abs(lhs.green() - rhs.green()));
      max_delta = std::max(max_delta, std::abs(lhs.blue() - rhs.blue()));
      max_delta = std::max(max_delta, std::abs(lhs.alpha() - rhs.alpha()));
    }
  }
  return max_delta;
}

int thumbnail_max_channel_delta(const QImage& left, const QImage& right) {
  if (left.size() != right.size() || left.isNull() || right.isNull()) {
    return 255;
  }
  const auto left_argb = left.convertToFormat(QImage::Format_ARGB32);
  const auto right_argb = right.convertToFormat(QImage::Format_ARGB32);
  auto max_delta = 0;
  for (int y = 0; y < left_argb.height(); ++y) {
    for (int x = 0; x < left_argb.width(); ++x) {
      const auto lhs = left_argb.pixelColor(x, y);
      const auto rhs = right_argb.pixelColor(x, y);
      max_delta = std::max(max_delta, std::abs(lhs.red() - rhs.red()));
      max_delta = std::max(max_delta, std::abs(lhs.green() - rhs.green()));
      max_delta = std::max(max_delta, std::abs(lhs.blue() - rhs.blue()));
    }
  }
  return max_delta;
}

int thumbnail_max_alpha_delta(const QImage& left, const QImage& right) {
  if (left.size() != right.size() || left.isNull() || right.isNull()) {
    return 255;
  }
  const auto left_argb = left.convertToFormat(QImage::Format_ARGB32);
  const auto right_argb = right.convertToFormat(QImage::Format_ARGB32);
  auto max_delta = 0;
  for (int y = 0; y < left_argb.height(); ++y) {
    for (int x = 0; x < left_argb.width(); ++x) {
      max_delta = std::max(
          max_delta,
          std::abs(left_argb.pixelColor(x, y).alpha() - right_argb.pixelColor(x, y).alpha()));
    }
  }
  return max_delta;
}

QImage expected_two_source_mask_thumbnail_alpha(
    const QImage& add_source,
    const QImage& second_source,
    const QString& operation) {
  if (add_source.size() != second_source.size() || add_source.isNull() ||
      second_source.isNull()) {
    return {};
  }
  const auto add_argb = add_source.convertToFormat(QImage::Format_ARGB32);
  const auto second_argb = second_source.convertToFormat(QImage::Format_ARGB32);
  QImage expected(add_argb.size(), QImage::Format_ARGB32);
  expected.fill(Qt::transparent);
  const auto normalized_operation = operation.trimmed().toLower();
  for (int y = 0; y < expected.height(); ++y) {
    for (int x = 0; x < expected.width(); ++x) {
      const auto first_alpha = add_argb.pixelColor(x, y).alpha() / 255.0;
      const auto second_alpha = second_argb.pixelColor(x, y).alpha() / 255.0;
      auto composite_alpha = first_alpha;
      if (normalized_operation == QStringLiteral("subtract")) {
        composite_alpha = std::max(0.0, first_alpha - second_alpha);
      } else if (normalized_operation == QStringLiteral("intersect")) {
        composite_alpha = first_alpha * second_alpha;
      } else if (normalized_operation == QStringLiteral("exclude")) {
        composite_alpha =
            std::clamp(first_alpha + second_alpha - (2.0 * first_alpha * second_alpha), 0.0, 1.0);
      } else if (normalized_operation == QStringLiteral("replace")) {
        composite_alpha = second_alpha;
      } else {
        composite_alpha = 1.0 - ((1.0 - first_alpha) * (1.0 - second_alpha));
      }
      const auto alpha =
          qBound(0, static_cast<int>(std::lround(composite_alpha * 255.0)), 255);
      expected.setPixelColor(x, y, QColor(alpha, alpha, alpha, alpha));
    }
  }
  return expected;
}

bool has_thumbnail_job_identity(
    const QVariantMap& value,
    const QString& expected_kind,
    const QString& expected_stage) {
  const auto async_job_id = value.value(QStringLiteral("async_job_id")).toString();
  const auto thumbnail_id = value.value(QStringLiteral("thumbnailId")).toString();
  const auto thumbnail_snake_id = value.value(QStringLiteral("thumbnail_id")).toString();
  const auto observability_thumbnail_id =
      value.value(QStringLiteral("observabilityThumbnailId")).toString();
  const auto thumbnail_job_id = value.value(QStringLiteral("thumbnailJobId")).toString();
  const auto async_generation = value.value(QStringLiteral("async_job_generation")).toInt();
  const auto thumbnail_generation = value.value(QStringLiteral("thumbnailJobGeneration")).toInt();
  const auto async_revision = value.value(QStringLiteral("async_job_state_revision")).toInt();
  const auto thumbnail_revision = value.value(QStringLiteral("thumbnailJobStateRevision")).toInt();
  const auto signature = value.value(QStringLiteral("thumbnailJobStateSignature")).toString();
  return !async_job_id.isEmpty() && async_job_id == thumbnail_job_id &&
         thumbnail_id == async_job_id &&
         thumbnail_snake_id == async_job_id &&
         observability_thumbnail_id == async_job_id &&
         async_job_id.startsWith(expected_kind + QStringLiteral("-")) &&
         value.value(QStringLiteral("async_job_kind")).toString() == expected_kind &&
         value.value(QStringLiteral("thumbnailJobKind")).toString() == expected_kind &&
         value.value(QStringLiteral("async_job_stage")).toString() == expected_stage &&
         value.value(QStringLiteral("thumbnailJobStage")).toString() == expected_stage &&
         async_generation > 0 && thumbnail_generation == async_generation &&
         thumbnail_revision == async_revision &&
         signature.startsWith(QStringLiteral("sha1:")) &&
         value.value(QStringLiteral("async_job_state_signature")).toString() == signature;
}

bool has_thumbnail_invalidation_fields(
    const QVariantMap& value,
    const QString& expected_asset_id,
    const QString& expected_layer_id,
    const QString& expected_mask_id,
    const QString& expected_blend_mode,
    const QString& expected_opacity,
    bool expected_visible) {
  const auto recipe_hash =
      value.value(QStringLiteral("thumbnailInvalidationRecipeHash")).toString();
  const auto mask_alpha_hash =
      value.value(QStringLiteral("thumbnailInvalidationMaskAlphaHash")).toString();
  const auto key = value.value(QStringLiteral("thumbnailInvalidationKey")).toString();
  const auto signature =
      value.value(QStringLiteral("thumbnailInvalidationSignature")).toString();
  return value.value(QStringLiteral("thumbnailInvalidationAssetId")).toString() ==
             expected_asset_id &&
         value.value(QStringLiteral("thumbnailInvalidationLayerId")).toString() ==
             expected_layer_id &&
         value.value(QStringLiteral("thumbnailInvalidationMaskId")).toString() ==
             expected_mask_id &&
         recipe_hash.startsWith(QStringLiteral("sha1:")) &&
         mask_alpha_hash.startsWith(QStringLiteral("sha")) &&
         value.value(QStringLiteral("thumbnailInvalidationBlendMode")).toString() ==
             expected_blend_mode &&
         value.value(QStringLiteral("thumbnailInvalidationOpacity")).toString() ==
             expected_opacity &&
         value.value(QStringLiteral("thumbnailInvalidationVisible")).toBool() ==
             expected_visible &&
         value.value(QStringLiteral("thumbnailInvalidationColorPipelineVersion")).toString() ==
             QStringLiteral("thumbnail-color-pipeline-v9") &&
         key.startsWith(QStringLiteral("sha1:")) &&
         value.value(QStringLiteral("thumbnailInvalidationComparator")).toString() ==
             QStringLiteral("thumbnailInvalidationKey") &&
         value.value(QStringLiteral("thumbnailInvalidationComparatorContract")).toString() ==
             QStringLiteral("thumbnail-invalidation-key-comparator-v1") &&
         signature.contains(QStringLiteral("asset:%1").arg(expected_asset_id)) &&
         signature.contains(QStringLiteral("layer:%1").arg(expected_layer_id)) &&
         signature.contains(QStringLiteral("mask:%1").arg(expected_mask_id)) &&
         signature.contains(QStringLiteral("recipe:%1").arg(recipe_hash)) &&
         signature.contains(QStringLiteral("mask-alpha:%1").arg(mask_alpha_hash)) &&
         signature.contains(QStringLiteral("blend:%1").arg(expected_blend_mode)) &&
         signature.contains(QStringLiteral("opacity:%1").arg(expected_opacity)) &&
         signature.contains(QStringLiteral("visible:%1")
                                .arg(expected_visible ? QStringLiteral("true")
                                                      : QStringLiteral("false"))) &&
         signature.contains(QStringLiteral("color:thumbnail-color-pipeline-v9")) &&
         signature.contains(QStringLiteral("key:%1").arg(key)) &&
         value.value(QStringLiteral("thumbnailSignature")).toString().contains(key);
}

bool has_thumbnail_entry_cache_fields(
    const QVariantMap& value,
    const QString& expected_asset_id,
    const QString& expected_layer_id,
    const QString& expected_mask_id,
    int expected_backing_width,
    int expected_backing_height,
    const QString& expected_source,
    bool expected_hit) {
  const auto key = value.value(QStringLiteral("thumbnailCacheKey")).toString();
  const auto recipe_hash = value.value(QStringLiteral("thumbnailCacheRecipeHash")).toString();
  const auto scale = QStringLiteral("%1x%2@")
                         .arg(expected_backing_width)
                         .arg(expected_backing_height);
  const auto signature = value.value(QStringLiteral("thumbnailCacheSignature")).toString();
  return value.value(QStringLiteral("thumbnailCacheContract")).toString() ==
             QStringLiteral("thumbnail-entry-cache-v1") &&
         key.startsWith(QStringLiteral("sha1:")) &&
         value.value(QStringLiteral("thumbnailCacheAssetId")).toString() ==
             expected_asset_id &&
         value.value(QStringLiteral("thumbnailCacheRecipeHash")).toString().startsWith(
             QStringLiteral("sha1:")) &&
         value.value(QStringLiteral("thumbnailCacheLayerId")).toString() ==
             expected_layer_id &&
         value.value(QStringLiteral("thumbnailCacheMaskId")).toString() ==
             expected_mask_id &&
         value.value(QStringLiteral("thumbnailCacheBackingWidth")).toInt() ==
             expected_backing_width &&
         value.value(QStringLiteral("thumbnailCacheBackingHeight")).toInt() ==
             expected_backing_height &&
         value.value(QStringLiteral("thumbnailCacheOutputScale")).toString().startsWith(scale) &&
         value.value(QStringLiteral("thumbnailCacheColorPipelineVersion")).toString() ==
             QStringLiteral("thumbnail-color-pipeline-v9") &&
         value.value(QStringLiteral("thumbnailCacheHit")).toBool() == expected_hit &&
         value.value(QStringLiteral("thumbnailCacheSource")).toString() == expected_source &&
         signature.contains(QStringLiteral("asset:%1").arg(expected_asset_id)) &&
         signature.contains(QStringLiteral("recipe:%1").arg(recipe_hash)) &&
         signature.contains(QStringLiteral("layer:%1").arg(expected_layer_id)) &&
         signature.contains(QStringLiteral("mask:%1").arg(expected_mask_id)) &&
         signature.contains(QStringLiteral("color:thumbnail-color-pipeline-v9")) &&
         signature.contains(QStringLiteral("hit:%1").arg(expected_hit ? QStringLiteral("true")
                                                                       : QStringLiteral("false"))) &&
         signature.contains(QStringLiteral("source:%1").arg(expected_source)) &&
         signature.contains(QStringLiteral("key:%1").arg(key));
}

bool has_mask_alpha_trace_metadata(const QVariantMap& value) {
  const auto source_hash = value.value(QStringLiteral("mask_source_hash")).toString();
  const auto trace_hash = value.value(QStringLiteral("mask_alpha_trace_hash")).toString();
  const auto signature = value.value(QStringLiteral("mask_alpha_trace_signature")).toString();
  const auto provider = value.value(QStringLiteral("mask_alpha_provider")).toString();
  const auto provider_signature =
      value.value(QStringLiteral("mask_alpha_provider_signature")).toString();
  return value.value(QStringLiteral("mask_source_id")).toString().simplified().size() > 0 &&
         value.value(QStringLiteral("mask_set_id")).toString().simplified().size() > 0 &&
         source_hash.startsWith(QStringLiteral("sha1:")) &&
         source_hash.size() == QStringLiteral("sha1:").size() + 40 &&
         trace_hash.startsWith(QStringLiteral("sha1:")) &&
         trace_hash.size() == QStringLiteral("sha1:").size() + 40 &&
         signature.contains(source_hash) &&
         !provider.isEmpty() &&
         value.value(QStringLiteral("mask_alpha_full_resolution_available")).toBool() &&
         value.value(QStringLiteral("mask_alpha_deterministic")).toBool() &&
         provider_signature.contains(QStringLiteral("provider:%1").arg(provider));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("EditorPreviewLayerAdjustmentTest"));

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("temporary runtime directory should be created");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::stable_mask_reselection())
    return fail("mask-set reselection must preserve component identity without changing masks");

  photo_editor::desktop::EditorPreviewController controller;
  photo_editor::desktop::TestImageSink image_sink;
  controller.setImageProvider(&image_sink);
  const auto thumbnail_fixture_path = runtime_root.filePath(QStringLiteral("thumbnail-proof.png"));
  if (!write_thumbnail_fixture(thumbnail_fixture_path)) {
    return fail("thumbnail fixture should be writable");
  }
  controller.loadAsset(
      QStringLiteral("layer-local-advanced-color-asset"),
      thumbnail_fixture_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(controller) ||
      image_sink.image(QStringLiteral("edited")).isNull()) {
    return fail("thumbnail parity controller should publish an initial edited preview");
  }

  {
    photo_editor::desktop::EditorPreviewController metadata_controller;
    photo_editor::desktop::TestImageSink metadata_image_sink;
    metadata_controller.setImageProvider(&metadata_image_sink);
    metadata_controller.loadAsset(
        QStringLiteral("layer-metadata-performance-asset"),
        thumbnail_fixture_path,
        QString(),
        QString(),
        QString(),
        QString(),
        QString());
    if (!wait_for_preview_idle(metadata_controller) ||
        !metadata_controller.addFillLayer(
            QStringLiteral("#456789"),
            QStringLiteral("Metadata Proof")) ||
        !wait_for_preview_idle(metadata_controller)) {
      return fail("layer metadata performance proof should create a settled fill layer");
    }
    const auto metadata_dispatch_before_warm =
        photo_editor::desktop::EditorPreviewControllerTestAccess::
            thumbnail_refresh_queue_dispatched_count(metadata_controller);
    const auto metadata_completed_before_warm =
        photo_editor::desktop::EditorPreviewControllerTestAccess::
            thumbnail_refresh_queue_worker_completed_count(metadata_controller);
    (void)metadata_controller.photoLayers();
    if (!wait_for_thumbnail_queue_dispatch(
            metadata_controller,
            metadata_dispatch_before_warm + 1,
            metadata_completed_before_warm + 1,
            6000)) {
      return fail("layer metadata performance proof should settle its initial async thumbnail");
    }
    (void)metadata_controller.photoLayers();
    if (photo_editor::desktop::EditorPreviewControllerTestAccess::
            photo_layer_thumbnail_cache_dirty(metadata_controller)) {
      return fail("layer metadata performance proof should warm a clean thumbnail cache");
    }
    const auto generation_before =
        photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
            metadata_controller);
    const auto intent_count_before =
        photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
            metadata_controller);
    const auto metadata_dispatch_before =
        photo_editor::desktop::EditorPreviewControllerTestAccess::
            thumbnail_refresh_queue_dispatched_count(metadata_controller);
    const auto metadata_worker_started_before =
        photo_editor::desktop::EditorPreviewControllerTestAccess::
            thumbnail_refresh_queue_worker_started_count(metadata_controller);
    const auto renamed = metadata_controller.renamePhotoLayer(
        0, QStringLiteral("Renamed Metadata Proof"));
    const auto locked = metadata_controller.setPhotoLayerLocked(0, true);
    const auto unlocked = metadata_controller.setPhotoLayerLocked(0, false);
    const auto metadata_layers = metadata_controller.photoLayers();
    const auto display_name = metadata_layers.at(0)
                                  .toMap()
                                  .value(QStringLiteral("displayName"))
                                  .toString();
    const auto generation_after =
        photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
            metadata_controller);
    const auto intent_count_after =
        photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
            metadata_controller);
    const auto thumbnail_dirty =
        photo_editor::desktop::EditorPreviewControllerTestAccess::
            photo_layer_thumbnail_cache_dirty(metadata_controller);
    const auto metadata_dispatch_after =
        photo_editor::desktop::EditorPreviewControllerTestAccess::
            thumbnail_refresh_queue_dispatched_count(metadata_controller);
    const auto metadata_worker_started_after =
        photo_editor::desktop::EditorPreviewControllerTestAccess::
            thumbnail_refresh_queue_worker_started_count(metadata_controller);
    if (!renamed || !locked || !unlocked ||
        display_name != QStringLiteral("Renamed Metadata Proof") ||
        generation_after != generation_before ||
        intent_count_after != intent_count_before || thumbnail_dirty) {
      std::cerr << "layer metadata diagnostics: renamed=" << renamed
                << " locked=" << locked
                << " unlocked=" << unlocked
                << " displayName=" << display_name.toStdString()
                << " generationBefore=" << generation_before
                << " generationAfter=" << generation_after
                << " intentBefore=" << intent_count_before
                << " intentAfter=" << intent_count_after
                << " dispatchBefore=" << metadata_dispatch_before
                << " dispatchAfter=" << metadata_dispatch_after
                << " workerStartedBefore=" << metadata_worker_started_before
                << " workerStartedAfter=" << metadata_worker_started_after
                << " thumbnailDirty=" << thumbnail_dirty << '\n';
      return fail("rename and lock metadata should not submit pixel or thumbnail renders");
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    if (photo_editor::desktop::EditorPreviewControllerTestAccess::
                thumbnail_refresh_queue_dispatched_count(metadata_controller) !=
            metadata_dispatch_before ||
        photo_editor::desktop::EditorPreviewControllerTestAccess::
                thumbnail_refresh_queue_worker_started_count(metadata_controller) !=
            metadata_worker_started_before) {
      return fail("rename and lock metadata must not enqueue delayed thumbnail work");
    }
  }

  controller.addMask(QStringLiteral("linear"));
  if (controller.photoLayerCount() != 1) {
    return fail("creating a mask should create one adjustment layer");
  }
  auto thumbnail_mask_sets = controller.maskSets();
  QElapsedTimer initial_mask_thumbnail_timer;
  initial_mask_thumbnail_timer.start();
  while (initial_mask_thumbnail_timer.elapsed() < 6000 &&
         (thumbnail_mask_sets.isEmpty() ||
          !has_thumbnail_job_identity(
              thumbnail_mask_sets.constFirst().toMap(),
              QStringLiteral("mask_thumbnail"),
              QStringLiteral("thumbnail")))) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(25);
    thumbnail_mask_sets = controller.maskSets();
  }
  if (thumbnail_mask_sets.isEmpty()) {
    return fail("mask thumbnail proof should have at least one mask set");
  }
  for (const auto& set_value : thumbnail_mask_sets) {
    const auto set = set_value.toMap();
    const auto set_thumbnail_source = set.value(QStringLiteral("thumbnailSource")).toString();
    if (!set_thumbnail_source.startsWith(QStringLiteral("data:image/png;base64,"))) {
      std::cerr << "initial mask thumbnail diagnostics: dirty="
                << photo_editor::desktop::EditorPreviewControllerTestAccess::
                       mask_thumbnail_cache_dirty(controller)
                << " pending="
                << photo_editor::desktop::EditorPreviewControllerTestAccess::
                       thumbnail_refresh_queue_pending(controller)
                << " running="
                << photo_editor::desktop::EditorPreviewControllerTestAccess::
                       thumbnail_refresh_queue_worker_running(controller)
                << " dispatched="
                << photo_editor::desktop::EditorPreviewControllerTestAccess::
                       thumbnail_refresh_queue_dispatched_count(controller)
                << " started="
                << photo_editor::desktop::EditorPreviewControllerTestAccess::
                       thumbnail_refresh_queue_worker_started_count(controller)
                << " completed="
                << photo_editor::desktop::EditorPreviewControllerTestAccess::
                       thumbnail_refresh_queue_worker_completed_count(controller)
                << " staleResults="
                << photo_editor::desktop::EditorPreviewControllerTestAccess::
                       thumbnail_refresh_queue_stale_result_count(controller)
                << " cacheStores="
                << photo_editor::desktop::EditorPreviewControllerTestAccess::
                       thumbnail_entry_cache_store_count(controller)
                << " progressive="
                << set.value(QStringLiteral("thumbnailProgressive")).toBool()
                << " failureStage="
                << set.value(QStringLiteral("thumbnailFailureStage"))
                       .toString()
                       .toStdString()
                << '\n';
      return fail("every mask set should expose a renderable thumbnail source");
    }
    if (!thumbnail_pixels_match_metadata(set)) {
      return fail("every mask set thumbnail PNG should match its backing-pixel metadata");
    }
    if (!has_thumbnail_job_identity(
            set,
            QStringLiteral("mask_thumbnail"),
            QStringLiteral("thumbnail"))) {
      std::cerr << "mask thumbnail identity diagnostics: async_job_id="
                << set.value(QStringLiteral("async_job_id")).toString().toStdString()
                << " thumbnailJobId="
                << set.value(QStringLiteral("thumbnailJobId")).toString().toStdString()
                << " thumbnailId="
                << set.value(QStringLiteral("thumbnailId")).toString().toStdString()
                << " thumbnail_id="
                << set.value(QStringLiteral("thumbnail_id")).toString().toStdString()
                << " observabilityId="
                << set.value(QStringLiteral("observabilityThumbnailId"))
                       .toString()
                       .toStdString()
                << " asyncKind="
                << set.value(QStringLiteral("async_job_kind")).toString().toStdString()
                << " thumbnailKind="
                << set.value(QStringLiteral("thumbnailJobKind")).toString().toStdString()
                << " asyncStage="
                << set.value(QStringLiteral("async_job_stage")).toString().toStdString()
                << " thumbnailStage="
                << set.value(QStringLiteral("thumbnailJobStage")).toString().toStdString()
                << " asyncGeneration="
                << set.value(QStringLiteral("async_job_generation")).toInt()
                << " thumbnailGeneration="
                << set.value(QStringLiteral("thumbnailJobGeneration")).toInt()
                << " asyncRevision="
                << set.value(QStringLiteral("async_job_state_revision")).toInt()
                << " thumbnailRevision="
                << set.value(QStringLiteral("thumbnailJobStateRevision")).toInt()
                << " asyncSignature="
                << set.value(QStringLiteral("async_job_state_signature"))
                       .toString()
                       .toStdString()
                << " thumbnailSignature="
                << set.value(QStringLiteral("thumbnailJobStateSignature"))
                       .toString()
                       .toStdString()
                << " thumbnailSourceEmpty=" << set_thumbnail_source.isEmpty()
                << " ready=" << set.value(QStringLiteral("thumbnailReady")).toBool()
                << " stale=" << set.value(QStringLiteral("thumbnailStale")).toBool()
                << " cacheHit=" << set.value(QStringLiteral("thumbnailCacheHit")).toBool()
                << " queuePending="
                << set.value(QStringLiteral("thumbnailQueuePending")).toBool()
                << " queueWorkerJobId="
                << set.value(QStringLiteral("thumbnailRenderQueuedWorkerJobId"))
                       .toString()
                       .toStdString()
                << '\n';
      return fail("every mask set thumbnail should expose a stable async job generation identity");
    }
    if (!has_evaluated_alpha_thumbnail_metadata(set)) {
      return fail("every mask set thumbnail should expose evaluated alpha freshness metadata");
    }
    if (!thumbnail_alpha_matches_metadata(set)) {
      return fail("every mask set thumbnail PNG alpha should match its evaluated alpha hash");
    }
    if (!thumbnail_is_clean_grayscale_alpha_matte(set)) {
      return fail("every mask set thumbnail should be a clean grayscale-alpha matte without debug RGB color");
    }
    if (!thumbnail_has_clean_solid_color_overlay(set)) {
      return fail("every mask set thumbnail should expose a clean optional solid-color overlay using the same alpha");
    }
    if (!has_thumbnail_pixel_metrics(set, 34, 96)) {
      return fail("every mask set thumbnail should expose Retina backing-pixel metrics");
    }
    if (!has_thumbnail_invalidation_fields(
            set,
            QStringLiteral("layer-local-advanced-color-asset"),
            QString(),
            set.value(QStringLiteral("mask_set_id"), set.value(QStringLiteral("id"))).toString(),
            QStringLiteral("normal"),
            QStringLiteral("1.000"),
            true)) {
      return fail("every mask set thumbnail should expose explicit invalidation identity fields");
    }
    if (!has_thumbnail_entry_cache_fields(
            set,
            QStringLiteral("layer-local-advanced-color-asset"),
            QString(),
            set.value(QStringLiteral("mask_set_id"), set.value(QStringLiteral("id"))).toString(),
            96,
            96,
            QStringLiteral("entry-cache"),
            true)) {
      return fail("every mask set thumbnail should publish its entry-cache key identity");
    }
    if (!has_mask_alpha_trace_metadata(set) ||
        set.value(QStringLiteral("mask_source_kind")).toString() != QStringLiteral("composite") ||
        set.value(QStringLiteral("mask_source_count")).toInt() <= 0 ||
        !set.value(QStringLiteral("mask_alpha_trace_signature"))
             .toString()
             .contains(QStringLiteral("mask-thumbnail-composite"))) {
      return fail("every mask set thumbnail should expose exact composite mask alpha trace metadata");
    }
    const auto sources = set.value(QStringLiteral("sources")).toList();
    if (sources.isEmpty()) {
      return fail("mask thumbnail proof should inspect component thumbnails");
    }
    if (sources.size() == 1) {
      const auto set_thumbnail_image = decoded_thumbnail_source(set_thumbnail_source);
      const auto source_thumbnail_image = decoded_thumbnail_source(
          sources.constFirst().toMap().value(QStringLiteral("thumbnailSource")).toString());
      if (thumbnail_max_rgba_delta(set_thumbnail_image, source_thumbnail_image) > 1) {
        return fail("single-source mask set thumbnail should match its component reference thumbnail");
      }
    }
    for (const auto& source_value : sources) {
      const auto source = source_value.toMap();
      const auto source_thumbnail_source =
          source.value(QStringLiteral("thumbnailSource")).toString();
      if (!source_thumbnail_source.startsWith(QStringLiteral("data:image/png;base64,"))) {
        return fail("every mask source component should expose a renderable thumbnail source");
      }
      if (!thumbnail_pixels_match_metadata(source)) {
        return fail("every mask source thumbnail PNG should match its backing-pixel metadata");
      }
      if (!has_thumbnail_job_identity(
              source,
              QStringLiteral("mask_thumbnail"),
              QStringLiteral("thumbnail"))) {
        return fail("every mask source thumbnail should expose a stable async job generation identity");
      }
      if (!has_evaluated_alpha_thumbnail_metadata(source)) {
        return fail("every mask source thumbnail should expose evaluated alpha freshness metadata");
      }
      if (!thumbnail_alpha_matches_metadata(source)) {
        return fail("every mask source thumbnail PNG alpha should match its evaluated alpha hash");
      }
      if (!thumbnail_is_clean_grayscale_alpha_matte(source)) {
        return fail("every mask source thumbnail should be a clean grayscale-alpha matte without debug RGB color");
      }
      if (!thumbnail_has_clean_solid_color_overlay(source)) {
        return fail("every mask source thumbnail should expose a clean optional solid-color overlay using the same alpha");
      }
      if (!has_thumbnail_pixel_metrics(source, 34, 96)) {
        return fail("every mask source thumbnail should expose Retina backing-pixel metrics");
      }
      if (!has_thumbnail_invalidation_fields(
              source,
              QStringLiteral("layer-local-advanced-color-asset"),
              QString(),
              source.value(QStringLiteral("mask_source_id"), source.value(QStringLiteral("id")))
                  .toString(),
              QStringLiteral("normal"),
              QStringLiteral("1.000"),
              true)) {
        return fail("every mask source thumbnail should expose explicit invalidation identity fields");
      }
      if (!has_thumbnail_entry_cache_fields(
              source,
              QStringLiteral("layer-local-advanced-color-asset"),
              QString(),
              source.value(QStringLiteral("mask_source_id"), source.value(QStringLiteral("id")))
                  .toString(),
              96,
              96,
              QStringLiteral("entry-cache"),
              true)) {
        return fail("every mask source thumbnail should publish its entry-cache key identity");
      }
      if (!has_mask_alpha_trace_metadata(source) ||
          source.value(QStringLiteral("mask_source_kind")).toString() ==
              QStringLiteral("composite") ||
          source.value(QStringLiteral("mask_alpha_raster_width")).toInt() <= 0 ||
          source.value(QStringLiteral("mask_alpha_raster_height")).toInt() <= 0) {
        return fail("every mask source thumbnail should expose exact source mask alpha trace metadata");
      }
    }
  }
  struct ThumbnailCombineCase {
    QString label;
    QString operation;
    bool invert_second_source;
  };
  const ThumbnailCombineCase thumbnail_combine_cases[] = {
      {QStringLiteral("subtract"), QStringLiteral("subtract"), false},
      {QStringLiteral("intersect"), QStringLiteral("intersect"), false},
      {QStringLiteral("exclude"), QStringLiteral("exclude"), false},
      {QStringLiteral("replace"), QStringLiteral("replace"), false},
      {QStringLiteral("add inverted"), QStringLiteral("add"), true},
  };
  for (const auto& thumbnail_combine_case : thumbnail_combine_cases) {
    photo_editor::desktop::EditorPreviewController thumbnail_combine_controller;
    thumbnail_combine_controller.loadAsset(
        QStringLiteral("mask-thumbnail-combine-%1-asset").arg(thumbnail_combine_case.operation),
        thumbnail_fixture_path,
        QString(),
        QString(),
        QString(),
        QString(),
        QString());
    thumbnail_combine_controller.addMask(QStringLiteral("linear"));
    thumbnail_combine_controller.addMask(QStringLiteral("radial"));
    thumbnail_combine_controller.setActiveMaskOperation(thumbnail_combine_case.operation);
    if (thumbnail_combine_case.invert_second_source) {
      thumbnail_combine_controller.setActiveMaskSelectionInverted(true);
    }
    wait_for_thumbnail_debounce();
    auto combine_mask_sets = thumbnail_combine_controller.maskSets();
    QElapsedTimer combine_thumbnail_timer;
    combine_thumbnail_timer.start();
    while (combine_thumbnail_timer.elapsed() < 6000 &&
           (combine_mask_sets.isEmpty() ||
            !combine_mask_sets.constFirst()
                 .toMap()
                 .value(QStringLiteral("mask_alpha_trace_signature"))
                 .toString()
                 .contains(QStringLiteral("mask-thumbnail-composite")))) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
      QThread::msleep(25);
      combine_mask_sets = thumbnail_combine_controller.maskSets();
    }
    if (combine_mask_sets.size() != 1) {
      return fail(
          QStringLiteral("mask thumbnail combine proof should keep %1 sources in one set")
              .arg(thumbnail_combine_case.label)
              .toStdString()
              .c_str());
    }
    const auto combine_set = combine_mask_sets.constFirst().toMap();
    const auto combine_sources = combine_set.value(QStringLiteral("sources")).toList();
    const auto combine_modes = combine_set.value(QStringLiteral("combineModes")).toList();
    const auto combine_contract =
        combine_set.value(QStringLiteral("combineModeContract")).toString();
    if (combine_sources.size() != 2 ||
        !combine_modes.contains(thumbnail_combine_case.operation) ||
        !combine_set.value(QStringLiteral("mask_alpha_trace_signature"))
             .toString()
             .contains(QStringLiteral("mask-thumbnail-composite")) ||
        (thumbnail_combine_case.invert_second_source &&
         !combine_contract.contains(QStringLiteral("inverted:true")))) {
      return fail(
          QStringLiteral("mask thumbnail combine proof should expose composite %1 provenance")
              .arg(thumbnail_combine_case.label)
              .toStdString()
              .c_str());
    }
    // Combine at the photo-aspect evaluation size, then resize the finished
    // matte. Combining already-resized source thumbnails is not equivalent for
    // subtract/intersect/exclude, and used to conceal coordinate mismatches.
    QImage mask_input;
    if (!photo_editor::desktop::decode_source_image(
            thumbnail_fixture_path, &mask_input,
            photo_editor::desktop::kMaskThumbnailBackingEdge)) {
      return fail("mask combine reference should decode the thumbnail-size source");
    }
    const auto native_source_alpha = [&mask_input](QVariantMap source) {
      source.insert(QStringLiteral("operation"), QStringLiteral("add"));
      return photo_editor::desktop::grayscale_mask_alpha_image(
          photo_editor::desktop::evaluate_canonical_composite_mask_alpha(
              mask_input, QVariantList{source}, 1.0, {1.0, 1.0, 1.0}));
    };
    const auto add_source_thumbnail = native_source_alpha(combine_sources.constFirst().toMap());
    const auto second_source_thumbnail = native_source_alpha(combine_sources.constLast().toMap());
    const auto combined_thumbnail = decoded_thumbnail_source(
        combine_set.value(QStringLiteral("thumbnailSource")).toString());
    const auto expected_native = expected_two_source_mask_thumbnail_alpha(
        add_source_thumbnail,
        second_source_thumbnail,
        thumbnail_combine_case.operation);
    const auto expected_thumbnail = photo_editor::desktop::scaled_square_thumbnail_image(
        expected_native, photo_editor::desktop::kMaskThumbnailBackingEdge);
    if (!thumbnail_alpha_matches_metadata(combine_set) ||
        !thumbnail_is_clean_grayscale_alpha_matte(combine_set) ||
        thumbnail_max_alpha_delta(combined_thumbnail, expected_thumbnail) > 1) {
      return fail(
          QStringLiteral("multi-source mask set thumbnail alpha should exactly follow %1 combine logic")
              .arg(thumbnail_combine_case.label)
              .toStdString()
              .c_str());
    }
  }
  const auto initial_photo_layer = controller.photoLayers().at(0).toMap();
  if (!initial_photo_layer.value(QStringLiteral("thumbnailReady")).toBool() ||
      !initial_photo_layer.value(QStringLiteral("thumbnailSource"))
           .toString()
           .startsWith(QStringLiteral("data:image/png;base64,")) ||
      !initial_photo_layer.value(QStringLiteral("thumbnailSignature"))
           .toString()
            .contains(QStringLiteral("ready:true"))) {
    return fail("selection adjustment layer should expose a renderable layer thumbnail");
  }
  if (initial_photo_layer.value(QStringLiteral("thumbnailKind")).toString() !=
          QStringLiteral("masked-adjustment-layer") ||
      initial_photo_layer.value(QStringLiteral("thumbnailRenderSource")).toString() !=
          QStringLiteral("shared-preview-render-request") ||
      !initial_photo_layer.value(QStringLiteral("thumbnailSharedRenderPath")).toBool() ||
      initial_photo_layer.value(QStringLiteral("thumbnailSharedRenderPathContract")).toString() !=
          QStringLiteral("layer-thumbnail-shared-preview-render-v1") ||
      initial_photo_layer.value(QStringLiteral("thumbnailPreviewRenderMode")).toString() !=
          QStringLiteral("layer_thumbnail") ||
      initial_photo_layer.value(QStringLiteral("thumbnailEffectPreviewContract")).toString() !=
          QStringLiteral("adjustment-layer-before-after-thumbnail-v1") ||
      initial_photo_layer.value(QStringLiteral("thumbnailEffectPreviewKind")).toString() !=
          QStringLiteral("before-after-split") ||
      !initial_photo_layer.value(QStringLiteral("thumbnailEffectPreviewSource"))
           .toString()
           .startsWith(QStringLiteral("data:image/png;base64,")) ||
      initial_photo_layer.value(QStringLiteral("thumbnailDisplaySource")).toString() !=
          initial_photo_layer.value(QStringLiteral("thumbnailEffectPreviewSource")).toString() ||
      initial_photo_layer.value(QStringLiteral("async_job_kind")).toString() !=
          QStringLiteral("photo_layer_thumbnail") ||
      initial_photo_layer.value(QStringLiteral("async_job_stage")).toString() !=
          QStringLiteral("thumbnail") ||
      !initial_photo_layer.value(QStringLiteral("async_job_id"))
           .toString()
           .startsWith(QStringLiteral("photo_layer_thumbnail-")) ||
      !initial_photo_layer.value(QStringLiteral("thumbnailJobStateSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !has_thumbnail_job_identity(
          initial_photo_layer,
          QStringLiteral("photo_layer_thumbnail"),
          QStringLiteral("thumbnail")) ||
      initial_photo_layer.value(QStringLiteral("thumbnail_render_budget_ms")).toLongLong() != 100 ||
      initial_photo_layer.value(QStringLiteral("thumbnail_render_elapsed_ms")).toLongLong() < 0 ||
       initial_photo_layer.value(QStringLiteral("thumbnailMasked")).toBool() != true ||
        initial_photo_layer.value(QStringLiteral("thumbnailMaskSetId")).toString().isEmpty() ||
        !has_thumbnail_pixel_metrics(initial_photo_layer, 32, 64) ||
        !thumbnail_pixels_match_metadata(initial_photo_layer) ||
         !has_thumbnail_invalidation_fields(
             initial_photo_layer,
             QStringLiteral("layer-local-advanced-color-asset"),
             initial_photo_layer.value(QStringLiteral("id")).toString(),
             initial_photo_layer.value(QStringLiteral("selectionMaskSetSignature")).toString(),
             QStringLiteral("normal"),
             QStringLiteral("1.000"),
             true) ||
         !has_thumbnail_entry_cache_fields(
             initial_photo_layer,
             QStringLiteral("layer-local-advanced-color-asset"),
             initial_photo_layer.value(QStringLiteral("id")).toString(),
             initial_photo_layer.value(QStringLiteral("selectionMaskSetSignature")).toString(),
             64,
             64,
             QStringLiteral("entry-cache"),
             true) ||
         !has_mask_alpha_trace_metadata(initial_photo_layer) ||
      initial_photo_layer.value(QStringLiteral("mask_source_kind")).toString() !=
          QStringLiteral("composite") ||
      !initial_photo_layer.value(QStringLiteral("mask_alpha_trace_signature"))
         .toString()
         .contains(QStringLiteral("layer-thumbnail-composite")) ||
      !has_evaluated_alpha_thumbnail_metadata(initial_photo_layer)) {
    std::cerr << "initial layer diagnostics: kind="
              << initial_photo_layer.value(QStringLiteral("thumbnailKind"))
                     .toString()
                     .toStdString()
              << " renderSource="
              << initial_photo_layer.value(QStringLiteral("thumbnailRenderSource"))
                     .toString()
                     .toStdString()
              << " shared="
              << initial_photo_layer.value(QStringLiteral("thumbnailSharedRenderPath")).toBool()
              << " previewMode="
              << initial_photo_layer.value(QStringLiteral("thumbnailPreviewRenderMode"))
                     .toString()
                     .toStdString()
              << " jobIdentity="
              << has_thumbnail_job_identity(
                     initial_photo_layer,
                     QStringLiteral("photo_layer_thumbnail"),
                     QStringLiteral("thumbnail"))
              << " elapsed="
              << initial_photo_layer.value(QStringLiteral("thumbnail_render_elapsed_ms"))
                     .toLongLong()
              << " budget="
              << initial_photo_layer.value(QStringLiteral("thumbnail_render_budget_ms"))
                     .toLongLong()
              << " within="
              << initial_photo_layer.value(
                     QStringLiteral("thumbnail_render_within_latency_budget"))
                     .toBool()
              << " masked="
              << initial_photo_layer.value(QStringLiteral("thumbnailMasked")).toBool()
              << " pixelMetrics="
              << has_thumbnail_pixel_metrics(initial_photo_layer, 32, 64)
              << " invalidation="
              << has_thumbnail_invalidation_fields(
                     initial_photo_layer,
                     QStringLiteral("layer-local-advanced-color-asset"),
                     initial_photo_layer.value(QStringLiteral("id")).toString(),
                     initial_photo_layer.value(QStringLiteral("selectionMaskSetSignature"))
                         .toString(),
                     QStringLiteral("normal"),
                     QStringLiteral("1.000"),
                     true)
              << " cache="
              << has_thumbnail_entry_cache_fields(
                     initial_photo_layer,
                     QStringLiteral("layer-local-advanced-color-asset"),
                     initial_photo_layer.value(QStringLiteral("id")).toString(),
                     initial_photo_layer.value(QStringLiteral("selectionMaskSetSignature"))
                         .toString(),
                     64,
                     64,
                     QStringLiteral("entry-cache"),
                     true)
              << " trace=" << has_mask_alpha_trace_metadata(initial_photo_layer)
              << " alpha="
              << has_evaluated_alpha_thumbnail_metadata(initial_photo_layer)
              << '\n';
    return fail("selection adjustment layer thumbnail should be rendered from its linked mask with current thumbnail job metadata");
  }
  const auto initial_layer_thumbnail_source =
      initial_photo_layer.value(QStringLiteral("thumbnailSource")).toString();
  const auto initial_layer_thumbnail_image =
      decoded_thumbnail_source(initial_layer_thumbnail_source);
  const auto initial_layer_thumbnail_invalidation_key =
      initial_photo_layer.value(QStringLiteral("thumbnailInvalidationKey")).toString();
  const auto initial_layer_thumbnail_refresh_count =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_thumbnail_refresh_count(controller);
  const auto initial_layer_thumbnail_cache_hit_count =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_thumbnail_cache_hit_count(controller);
  const auto cached_initial_photo_layer = controller.photoLayers().at(0).toMap();
  if (cached_initial_photo_layer.value(QStringLiteral("thumbnailSource")).toString() !=
          initial_layer_thumbnail_source ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_count(controller) !=
          initial_layer_thumbnail_refresh_count ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_cache_hit_count(controller) !=
           initial_layer_thumbnail_cache_hit_count + 1) {
    return fail("selection adjustment layer thumbnails should be served from the cached composited layer list on repeated reads");
  }
  const auto entry_cache_store_count_before_forced_lookup =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_entry_cache_store_count(controller);
  const auto entry_cache_hit_count_before_forced_lookup =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_entry_cache_hit_count(controller);
  photo_editor::desktop::EditorPreviewControllerTestAccess::forceThumbnailEntryCacheLookup(controller);
  const auto entry_cached_mask_sets = controller.maskSets();
  const auto entry_cached_mask_set =
      entry_cached_mask_sets.isEmpty() ? QVariantMap{} : entry_cached_mask_sets.constFirst().toMap();
  const auto entry_cached_mask_sources =
      entry_cached_mask_set.value(QStringLiteral("sources")).toList();
  const auto entry_cached_mask_source =
      entry_cached_mask_sources.isEmpty() ? QVariantMap{} : entry_cached_mask_sources.constFirst().toMap();
  const auto entry_cached_photo_layer = controller.photoLayers().at(0).toMap();
  if (entry_cache_store_count_before_forced_lookup < 3 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_entry_cache_hit_count(controller) <
          entry_cache_hit_count_before_forced_lookup + 3 ||
      entry_cached_mask_set.value(QStringLiteral("thumbnailCacheHit")).toBool() != true ||
      entry_cached_mask_set.value(QStringLiteral("thumbnailCacheSource")).toString() !=
          QStringLiteral("entry-cache") ||
      entry_cached_mask_source.value(QStringLiteral("thumbnailCacheHit")).toBool() != true ||
      entry_cached_mask_source.value(QStringLiteral("thumbnailCacheSource")).toString() !=
          QStringLiteral("entry-cache") ||
      entry_cached_photo_layer.value(QStringLiteral("thumbnailCacheHit")).toBool() != true ||
      entry_cached_photo_layer.value(QStringLiteral("thumbnailCacheSource")).toString() !=
          QStringLiteral("entry-cache") ||
      entry_cached_photo_layer.value(QStringLiteral("thumbnailCacheKey")).toString() !=
          initial_photo_layer.value(QStringLiteral("thumbnailCacheKey")).toString() ||
      entry_cached_photo_layer.value(QStringLiteral("thumbnailSource")).toString() !=
          initial_layer_thumbnail_source) {
    return fail("thumbnail entry cache should reuse mask source, mask set, and adjustment layer thumbnails by stable identity key");
  }
  photo_editor::desktop::EditorPreviewController mask_queue_controller;
  mask_queue_controller.loadAsset(
      QStringLiteral("mask-thumbnail-queue-worker-proof"),
      thumbnail_fixture_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(mask_queue_controller)) {
    return fail("mask thumbnail queue proof should start from an idle preview controller");
  }
  mask_queue_controller.startSelectionBrush(false);
  const auto mask_queue_initial_dispatch =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_dispatched_count(mask_queue_controller);
  const auto mask_queue_initial_completed =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_worker_completed_count(mask_queue_controller);
  static_cast<void>(mask_queue_controller.maskSets());
  if (!wait_for_thumbnail_queue_dispatch(
          mask_queue_controller,
          mask_queue_initial_dispatch + 1,
          mask_queue_initial_completed + 1,
          6000)) {
    return fail("mask thumbnail queue proof should settle its initial async thumbnail");
  }
  const auto mask_queue_sets_before = mask_queue_controller.maskSets();
  if (mask_queue_sets_before.isEmpty() ||
      mask_queue_sets_before.constFirst()
          .toMap()
          .value(QStringLiteral("thumbnailSource"))
          .toString()
          .isEmpty()) {
    return fail("mask thumbnail queue proof should warm an initial mask thumbnail before the live stroke");
  }
  const auto mask_queue_store_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_entry_cache_store_count(mask_queue_controller);
  const auto mask_queue_hit_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_entry_cache_hit_count(mask_queue_controller);
  const auto mask_queue_dispatch_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_dispatched_count(mask_queue_controller);
  const auto mask_queue_worker_started_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_worker_started_count(mask_queue_controller);
  const auto mask_queue_worker_completed_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_worker_completed_count(mask_queue_controller);
  const auto mask_queue_stale_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_stale_result_count(mask_queue_controller);
  mask_queue_controller.beginActiveBrushStroke();
  mask_queue_controller.addBrushPoint(0.24, 0.34);
  mask_queue_controller.addBrushPoint(0.72, 0.58);
  mask_queue_controller.setActiveMaskDensity(0.73);
  const auto mask_queue_sets_deferred = mask_queue_controller.maskSets();
  const auto mask_queue_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_pending_generation(mask_queue_controller);
  const auto mask_queue_set_deferred =
      mask_queue_sets_deferred.isEmpty() ? QVariantMap{} : mask_queue_sets_deferred.constFirst().toMap();
  if (mask_queue_sets_deferred.isEmpty() ||
      mask_queue_generation <= 0 ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_deferred(mask_queue_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_pending(mask_queue_controller) ||
      !mask_queue_set_deferred.value(QStringLiteral("thumbnailQueuePending")).toBool() ||
      mask_queue_set_deferred.value(QStringLiteral("thumbnailQueueJobId")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-%1").arg(mask_queue_generation) ||
      mask_queue_set_deferred.value(QStringLiteral("thumbnailQueueReason")).toString() !=
          QStringLiteral("mask_thumbnail_refresh_deferred") ||
      mask_queue_set_deferred.value(QStringLiteral("thumbnailProgressive")).toBool() != true ||
      mask_queue_set_deferred.value(QStringLiteral("thumbnailStale")).toBool() != true) {
    std::cerr << "mask deferred diagnostics: empty=" << mask_queue_sets_deferred.isEmpty()
              << " generation=" << mask_queue_generation
              << " deferred="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     thumbnail_refresh_deferred(mask_queue_controller)
              << " pending="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     thumbnail_refresh_queue_pending(mask_queue_controller)
              << " fieldPending="
              << mask_queue_set_deferred.value(QStringLiteral("thumbnailQueuePending")).toBool()
              << " fieldJob="
              << mask_queue_set_deferred.value(QStringLiteral("thumbnailQueueJobId"))
                     .toString()
                     .toStdString()
              << " fieldReason="
              << mask_queue_set_deferred.value(QStringLiteral("thumbnailQueueReason"))
                     .toString()
                     .toStdString()
              << " progressive="
              << mask_queue_set_deferred.value(QStringLiteral("thumbnailProgressive")).toBool()
              << " stale="
              << mask_queue_set_deferred.value(QStringLiteral("thumbnailStale")).toBool()
              << '\n';
    return fail("mask thumbnail queue proof should defer changed mask thumbnails and expose the queued worker job on stale rows");
  }
  if (!finish_brush_stroke(mask_queue_controller)) {
    return fail("mask thumbnail queue proof should commit the brush stroke");
  }
  if (!wait_for_thumbnail_queue_dispatch(
          mask_queue_controller,
          mask_queue_dispatch_count_before + 1,
          mask_queue_worker_completed_before + 1,
          6000)) {
    return fail("mask thumbnail queue proof should dispatch queued thumbnail work after the live stroke settles");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_refresh_queue_worker_started_count(mask_queue_controller) !=
          mask_queue_worker_started_before + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_refresh_queue_worker_completed_count(mask_queue_controller) !=
          mask_queue_worker_completed_before + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_worker_running(mask_queue_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
           thumbnail_refresh_queue_active_job_id(mask_queue_controller)
           .isEmpty() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_refresh_queue_stale_result_count(mask_queue_controller) !=
          mask_queue_stale_before ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_entry_cache_store_count(mask_queue_controller) <=
          mask_queue_store_count_before) {
    std::cerr << "mask thumbnail queue worker diagnostics:"
              << " started="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     thumbnail_refresh_queue_worker_started_count(mask_queue_controller)
              << " started_before=" << mask_queue_worker_started_before
              << " completed="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     thumbnail_refresh_queue_worker_completed_count(mask_queue_controller)
              << " completed_before=" << mask_queue_worker_completed_before
              << " running="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     thumbnail_refresh_queue_worker_running(mask_queue_controller)
              << " active_job="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     thumbnail_refresh_queue_active_job_id(mask_queue_controller)
                     .toStdString()
              << " stale="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     thumbnail_refresh_queue_stale_result_count(mask_queue_controller)
              << " stale_before=" << mask_queue_stale_before
              << " stores="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     thumbnail_entry_cache_store_count(mask_queue_controller)
              << " stores_before=" << mask_queue_store_count_before
              << '\n';
    return fail("mask thumbnail queue proof should complete through a current background worker and store rendered entries");
  }
  const auto mask_queue_sets_after = mask_queue_controller.maskSets();
  const auto mask_queue_set_after =
      mask_queue_sets_after.isEmpty() ? QVariantMap{} : mask_queue_sets_after.constFirst().toMap();
  const auto mask_queue_sources_after =
      mask_queue_set_after.value(QStringLiteral("sources")).toList();
  const auto mask_queue_source_after =
      mask_queue_sources_after.isEmpty() ? QVariantMap{} : mask_queue_sources_after.constFirst().toMap();
  if (mask_queue_sets_after.isEmpty() ||
      mask_queue_sources_after.isEmpty() ||
      mask_queue_set_after.value(QStringLiteral("thumbnailSource")).toString().isEmpty() ||
      mask_queue_source_after.value(QStringLiteral("thumbnailSource")).toString().isEmpty() ||
      mask_queue_set_after.value(QStringLiteral("thumbnailProgressive")).toBool() ||
      mask_queue_set_after.value(QStringLiteral("thumbnailStale")).toBool() ||
      mask_queue_set_after.value(QStringLiteral("thumbnailRenderQueuedWorkerJobId")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-%1").arg(mask_queue_generation) ||
      mask_queue_set_after.value(QStringLiteral("thumbnailRenderQueuedWorkerContract")).toString() !=
          QStringLiteral("thumbnail-render-consumed-queue-worker:v1") ||
      mask_queue_set_after.value(QStringLiteral("thumbnailRenderQueuedWorkerMaskEntryCount")).toInt() <= 0 ||
      mask_queue_set_after.value(QStringLiteral("thumbnailCacheHit")).toBool() != true ||
      mask_queue_set_after.value(QStringLiteral("thumbnailCacheSource")).toString() !=
          QStringLiteral("entry-cache") ||
      mask_queue_set_after.value(QStringLiteral("thumbnailQueueWorkerRendered")).toBool() != true ||
      mask_queue_set_after.value(QStringLiteral("thumbnailQueueWorkerRenderJobId")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-%1").arg(mask_queue_generation) ||
      mask_queue_set_after.value(QStringLiteral("thumbnailQueueWorkerRenderSource")).toString() !=
          QStringLiteral("mask-set") ||
      mask_queue_set_after.value(QStringLiteral("thumbnailQueueWorkerRenderMaskEntryCount")).toInt() <= 0 ||
      mask_queue_set_after.value(QStringLiteral("thumbnailQueueWorkerRenderContract")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-worker-entry:v1") ||
      mask_queue_set_after.value(QStringLiteral("thumbnailQueueWorkerServiceContract")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-worker-service|cancellable|entry-cache:v1") ||
      mask_queue_set_after.value(QStringLiteral("thumbnailRenderQueuedWorkerServiceContract")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-worker-service|cancellable|entry-cache:v1") ||
      mask_queue_source_after.value(QStringLiteral("thumbnailRenderQueuedWorkerJobId")).toString() !=
          mask_queue_set_after.value(QStringLiteral("thumbnailRenderQueuedWorkerJobId")).toString() ||
      mask_queue_source_after.value(QStringLiteral("thumbnailCacheHit")).toBool() != true ||
      mask_queue_source_after.value(QStringLiteral("thumbnailCacheSource")).toString() !=
          QStringLiteral("entry-cache") ||
      mask_queue_source_after.value(QStringLiteral("thumbnailQueueWorkerRendered")).toBool() != true ||
      mask_queue_source_after.value(QStringLiteral("thumbnailQueueWorkerRenderSource")).toString() !=
          QStringLiteral("mask-source") ||
      mask_queue_source_after.value(QStringLiteral("thumbnailQueueWorkerRenderMaskEntryCount")).toInt() <= 0 ||
      mask_queue_source_after.value(QStringLiteral("thumbnailQueueWorkerServiceContract")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-worker-service|cancellable|entry-cache:v1") ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_entry_cache_hit_count(mask_queue_controller) <=
          mask_queue_hit_count_before) {
    return fail("mask set and source thumbnails should be rendered by the thumbnail queue worker and then consumed from the entry cache");
  }
  photo_editor::desktop::EditorPreviewController adjustment_queue_controller;
  adjustment_queue_controller.loadAsset(
      QStringLiteral("adjustment-thumbnail-queue-worker-proof"),
      thumbnail_fixture_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(adjustment_queue_controller)) {
    return fail("adjustment thumbnail queue proof should start from an idle preview controller");
  }
  adjustment_queue_controller.startSelectionBrush(false);
  const auto adjustment_queue_initial_dispatch =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_dispatched_count(adjustment_queue_controller);
  const auto adjustment_queue_initial_completed =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_worker_completed_count(adjustment_queue_controller);
  static_cast<void>(adjustment_queue_controller.photoLayers());
  if (!wait_for_thumbnail_queue_dispatch(
          adjustment_queue_controller,
          adjustment_queue_initial_dispatch + 1,
          adjustment_queue_initial_completed + 1,
          6000)) {
    return fail("adjustment thumbnail queue proof should settle its initial async thumbnail");
  }
  const auto adjustment_queue_layers_before = adjustment_queue_controller.photoLayers();
  if (adjustment_queue_layers_before.isEmpty() ||
      adjustment_queue_layers_before.constFirst()
          .toMap()
          .value(QStringLiteral("thumbnailSource"))
          .toString()
          .isEmpty()) {
    return fail("adjustment thumbnail queue proof should warm an initial masked adjustment thumbnail");
  }
  const auto adjustment_queue_store_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_entry_cache_store_count(adjustment_queue_controller);
  const auto adjustment_queue_hit_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_entry_cache_hit_count(adjustment_queue_controller);
  const auto adjustment_queue_dispatch_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_dispatched_count(adjustment_queue_controller);
  const auto adjustment_queue_worker_completed_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_worker_completed_count(adjustment_queue_controller);
  adjustment_queue_controller.beginActiveBrushStroke();
  if (!adjustment_queue_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("exposure"),
          0.44)) {
    return fail("adjustment thumbnail queue proof should dirty the masked adjustment layer during a live stroke");
  }
  const auto adjustment_queue_layers_deferred = adjustment_queue_controller.photoLayers();
  const auto adjustment_queue_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_pending_generation(adjustment_queue_controller);
  const auto adjustment_queue_layer_deferred =
      adjustment_queue_layers_deferred.isEmpty()
          ? QVariantMap{}
          : adjustment_queue_layers_deferred.constFirst().toMap();
  if (adjustment_queue_layers_deferred.isEmpty() ||
      adjustment_queue_generation <= 0 ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_deferred(adjustment_queue_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_pending(adjustment_queue_controller) ||
      !adjustment_queue_layer_deferred.value(QStringLiteral("thumbnailQueuePending")).toBool() ||
      adjustment_queue_layer_deferred.value(QStringLiteral("thumbnailQueueJobId")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-%1").arg(adjustment_queue_generation) ||
      adjustment_queue_layer_deferred.value(QStringLiteral("thumbnailQueueReason")).toString() !=
          QStringLiteral("photo_layer_thumbnail_refresh_deferred") ||
      adjustment_queue_layer_deferred.value(QStringLiteral("thumbnailProgressive")).toBool() != true ||
      adjustment_queue_layer_deferred.value(QStringLiteral("thumbnailStale")).toBool() != true) {
    return fail("adjustment thumbnail queue proof should expose the queued worker job on a stale masked adjustment row");
  }
  if (!finish_brush_stroke(adjustment_queue_controller)) {
    return fail("adjustment thumbnail queue proof should end the live stroke");
  }
  if (!wait_for_thumbnail_queue_dispatch(
          adjustment_queue_controller,
          adjustment_queue_dispatch_count_before + 1,
          adjustment_queue_worker_completed_before + 1,
          6000)) {
    return fail("adjustment thumbnail queue proof should dispatch after live stroke blockers settle");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_entry_cache_store_count(adjustment_queue_controller) <=
          adjustment_queue_store_count_before) {
    return fail("adjustment thumbnail queue worker should store a rendered masked adjustment layer entry");
  }
  const auto adjustment_queue_layers_after = adjustment_queue_controller.photoLayers();
  const auto adjustment_queue_layer_after =
      adjustment_queue_layers_after.isEmpty()
          ? QVariantMap{}
          : adjustment_queue_layers_after.constFirst().toMap();
  if (adjustment_queue_layers_after.isEmpty() ||
      adjustment_queue_layer_after.value(QStringLiteral("thumbnailSource")).toString().isEmpty() ||
      adjustment_queue_layer_after.value(QStringLiteral("thumbnailProgressive")).toBool() ||
      adjustment_queue_layer_after.value(QStringLiteral("thumbnailStale")).toBool() ||
      adjustment_queue_layer_after.value(QStringLiteral("thumbnailKind")).toString() !=
          QStringLiteral("masked-adjustment-layer") ||
      adjustment_queue_layer_after.value(QStringLiteral("thumbnailSharedRenderPath")).toBool() != true ||
      adjustment_queue_layer_after.value(QStringLiteral("thumbnailCacheHit")).toBool() != true ||
      adjustment_queue_layer_after.value(QStringLiteral("thumbnailCacheSource")).toString() !=
          QStringLiteral("entry-cache") ||
      adjustment_queue_layer_after.value(QStringLiteral("thumbnailQueueWorkerRendered")).toBool() != true ||
      adjustment_queue_layer_after.value(QStringLiteral("thumbnailQueueWorkerRenderJobId")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-%1").arg(adjustment_queue_generation) ||
      adjustment_queue_layer_after.value(QStringLiteral("thumbnailQueueWorkerRenderSource")).toString() !=
          QStringLiteral("masked-adjustment-photo-layer") ||
      adjustment_queue_layer_after.value(QStringLiteral("thumbnailQueueWorkerRenderEntryCount")).toInt() <= 0 ||
      adjustment_queue_layer_after.value(QStringLiteral("thumbnailQueueWorkerRenderContract")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-worker-entry:v1") ||
      adjustment_queue_layer_after.value(QStringLiteral("thumbnailQueueWorkerServiceContract")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-worker-service|cancellable|entry-cache:v1") ||
      adjustment_queue_layer_after.value(QStringLiteral("thumbnailRenderQueuedWorkerJobId")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-%1").arg(adjustment_queue_generation) ||
      adjustment_queue_layer_after.value(QStringLiteral("thumbnailRenderQueuedWorkerEntryCount")).toInt() <= 0 ||
      adjustment_queue_layer_after.value(QStringLiteral("thumbnailRenderQueuedWorkerServiceContract")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-worker-service|cancellable|entry-cache:v1") ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_entry_cache_hit_count(adjustment_queue_controller) <=
          adjustment_queue_hit_count_before) {
    std::cerr << "thumbnail queue diagnostics: "
              << "sourceEmpty="
              << adjustment_queue_layer_after.value(QStringLiteral("thumbnailSource")).toString().isEmpty()
              << " progressive="
              << adjustment_queue_layer_after.value(QStringLiteral("thumbnailProgressive")).toBool()
              << " stale="
              << adjustment_queue_layer_after.value(QStringLiteral("thumbnailStale")).toBool()
              << " kind="
              << adjustment_queue_layer_after.value(QStringLiteral("thumbnailKind")).toString().toStdString()
              << " shared="
              << adjustment_queue_layer_after.value(QStringLiteral("thumbnailSharedRenderPath")).toBool()
              << " cacheHit="
              << adjustment_queue_layer_after.value(QStringLiteral("thumbnailCacheHit")).toBool()
              << " cacheSource="
              << adjustment_queue_layer_after.value(QStringLiteral("thumbnailCacheSource")).toString().toStdString()
              << " workerRendered="
              << adjustment_queue_layer_after.value(QStringLiteral("thumbnailQueueWorkerRendered")).toBool()
              << " workerJob="
              << adjustment_queue_layer_after.value(QStringLiteral("thumbnailQueueWorkerRenderJobId")).toString().toStdString()
              << " expectedJob="
              << QStringLiteral("thumbnail-refresh-queue-%1").arg(adjustment_queue_generation).toStdString()
              << " renderSource="
              << adjustment_queue_layer_after.value(QStringLiteral("thumbnailQueueWorkerRenderSource")).toString().toStdString()
              << " workerEntries="
              << adjustment_queue_layer_after.value(QStringLiteral("thumbnailQueueWorkerRenderEntryCount")).toInt()
              << " hitBefore=" << adjustment_queue_hit_count_before
              << " hitAfter="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     thumbnail_entry_cache_hit_count(adjustment_queue_controller)
              << '\n';
    return fail("masked adjustment thumbnails should be rendered by the thumbnail queue worker and consumed from the entry cache");
  }
  const auto post_entry_layer_thumbnail_refresh_count =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_thumbnail_refresh_count(controller);
  const auto post_entry_layer_thumbnail_cache_hit_count =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_thumbnail_cache_hit_count(controller);

  photo_editor::desktop::EditorPreviewController thumbnail_parity_controller;
  photo_editor::desktop::TestImageSink thumbnail_parity_sink;
  thumbnail_parity_controller.setImageProvider(&thumbnail_parity_sink);
  thumbnail_parity_controller.loadAsset(
      QStringLiteral("thumbnail-preview-parity-asset"),
      thumbnail_fixture_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(thumbnail_parity_controller) ||
      thumbnail_parity_sink.image(QStringLiteral("edited")).isNull()) {
    return fail("thumbnail preview parity proof should publish an initial edited preview");
  }
  thumbnail_parity_controller.addMask(QStringLiteral("linear"));
  if (!thumbnail_parity_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("exposure"),
          0.35)) {
    return fail("thumbnail preview parity proof should apply a representative layer edit");
  }
  wait_for_thumbnail_debounce();
  if (!wait_for_preview_idle(thumbnail_parity_controller) ||
      thumbnail_parity_sink.image(QStringLiteral("edited")).isNull()) {
    return fail("thumbnail preview parity proof should publish an edited preview");
  }
  const auto parity_layer =
      wait_for_fresh_photo_layer_thumbnail(thumbnail_parity_controller);
  const auto parity_layer_thumbnail_image =
      decoded_thumbnail_source(parity_layer.value(QStringLiteral("thumbnailSource")).toString());
  const auto parity_preview_reference = scaled_square_reference_image(
      thumbnail_parity_sink.image(QStringLiteral("edited")),
      parity_layer_thumbnail_image.width());
  const auto parity_thumbnail_preview_delta =
      thumbnail_max_channel_delta(parity_layer_thumbnail_image, parity_preview_reference);
  if (parity_preview_reference.isNull() || parity_thumbnail_preview_delta > 42) {
    std::cerr << "thumbnail preview parity delta=" << parity_thumbnail_preview_delta
              << " thumbnail=" << parity_layer_thumbnail_image.width() << "x"
              << parity_layer_thumbnail_image.height() << " preview="
              << thumbnail_parity_sink.image(QStringLiteral("edited")).width() << "x"
              << thumbnail_parity_sink.image(QStringLiteral("edited")).height()
              << " reference=" << parity_preview_reference.width() << "x"
              << parity_preview_reference.height() << '\n';
    return fail("representative masked exposure layer thumbnail should match the published canvas preview within thumbnail tolerance");
  }

  photo_editor::desktop::EditorPreviewController curve_thumbnail_parity_controller;
  photo_editor::desktop::TestImageSink curve_thumbnail_parity_sink;
  curve_thumbnail_parity_controller.setImageProvider(&curve_thumbnail_parity_sink);
  curve_thumbnail_parity_controller.loadAsset(
      QStringLiteral("curve-thumbnail-preview-parity-asset"),
      thumbnail_fixture_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(curve_thumbnail_parity_controller) ||
      curve_thumbnail_parity_sink.image(QStringLiteral("edited")).isNull()) {
    return fail("curve thumbnail parity proof should publish an initial edited preview");
  }
  curve_thumbnail_parity_controller.addMask(QStringLiteral("linear"));
  const auto initial_curve_parity_layer =
      wait_for_fresh_photo_layer_thumbnail(curve_thumbnail_parity_controller);
  const auto initial_curve_parity_thumbnail_source =
      initial_curve_parity_layer.value(QStringLiteral("thumbnailSource")).toString();
  const auto initial_curve_parity_thumbnail_image =
      decoded_thumbnail_source(initial_curve_parity_thumbnail_source);
  const auto initial_curve_parity_invalidation_key =
      initial_curve_parity_layer.value(QStringLiteral("thumbnailInvalidationKey")).toString();
  if (!curve_thumbnail_parity_controller.setPhotoLayerCurvePoints(
          0,
          QStringLiteral("RGB"),
          curve_points({{0.0, 0.02}, {0.32, 0.16}, {0.68, 0.93}, {1.0, 1.0}})) ||
      !curve_thumbnail_parity_controller.setPhotoLayerCurvePoints(
          0,
          QStringLiteral("Red"),
          curve_points({{0.0, 0.0}, {0.52, 0.68}, {1.0, 1.0}}))) {
    return fail("curve thumbnail parity proof should apply layer-local point curves");
  }
  wait_for_thumbnail_debounce();
  if (!wait_for_preview_idle(curve_thumbnail_parity_controller) ||
      curve_thumbnail_parity_sink.image(QStringLiteral("edited")).isNull()) {
    return fail("curve thumbnail parity proof should publish an edited preview");
  }
  const auto curve_parity_layer =
      wait_for_fresh_photo_layer_thumbnail(curve_thumbnail_parity_controller);
  const auto curve_parity_thumbnail_source =
      curve_parity_layer.value(QStringLiteral("thumbnailSource")).toString();
  if (curve_parity_thumbnail_source == initial_curve_parity_thumbnail_source ||
      curve_parity_layer.value(QStringLiteral("thumbnailInvalidationKey")).toString() ==
          initial_curve_parity_invalidation_key ||
      !thumbnail_pixels_match_metadata(curve_parity_layer)) {
    return fail("layer-local point curves should invalidate and rebuild the masked layer thumbnail");
  }
  const auto curve_parity_layer_thumbnail_image =
      decoded_thumbnail_source(curve_parity_thumbnail_source);
  if (thumbnail_max_channel_delta(
          initial_curve_parity_thumbnail_image, curve_parity_layer_thumbnail_image) < 4) {
    return fail("masked point-curve layer thumbnail should visibly rebuild from the layer recipe");
  }

  photo_editor::desktop::EditorPreviewController raster_layer_thumbnail_controller;
  photo_editor::desktop::TestImageSink raster_layer_thumbnail_sink;
  raster_layer_thumbnail_controller.setImageProvider(&raster_layer_thumbnail_sink);
  raster_layer_thumbnail_controller.loadAsset(
      QStringLiteral("raster-layer-thumbnail-lifecycle-asset"),
      thumbnail_fixture_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(raster_layer_thumbnail_controller) ||
      raster_layer_thumbnail_sink.image(QStringLiteral("edited")).isNull()) {
    return fail("raster layer thumbnail proof should publish an initial edited preview");
  }
  if (!raster_layer_thumbnail_controller.addFillLayer(
          QStringLiteral("#204060"),
          QStringLiteral("Fill Thumb")) ||
      !raster_layer_thumbnail_controller.addTextLayer(
          QStringLiteral("Mask"),
          QStringLiteral("Text Thumb"))) {
    return fail("raster layer thumbnail proof should create fill and text layers");
  }
  wait_for_thumbnail_debounce();
  static_cast<void>(wait_for_fresh_photo_layer_thumbnail(
      raster_layer_thumbnail_controller));
  const auto raster_layers = raster_layer_thumbnail_controller.photoLayers();
  if (raster_layers.size() < 2) {
    return fail("raster layer thumbnail proof should expose fill and text layer rows");
  }
  const auto text_thumbnail_layer = raster_layers.at(0).toMap();
  const auto fill_thumbnail_layer = raster_layers.at(1).toMap();
  if (text_thumbnail_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("text") ||
      fill_thumbnail_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("fill")) {
    return fail("raster layer thumbnail proof should keep newest text layer above fill layer");
  }
  const auto raster_thumbnail_layers =
      QList<QVariantMap>{text_thumbnail_layer, fill_thumbnail_layer};
  for (const auto& raster_layer : raster_thumbnail_layers) {
    const auto expected_kind =
        raster_layer.value(QStringLiteral("kind")).toString() == QStringLiteral("text")
            ? QStringLiteral("text-layer")
            : QStringLiteral("fill-layer");
    if (!raster_layer.value(QStringLiteral("thumbnailReady")).toBool() ||
        !raster_layer.value(QStringLiteral("thumbnailSource"))
             .toString()
             .startsWith(QStringLiteral("data:image/png;base64,")) ||
        raster_layer.value(QStringLiteral("thumbnailKind")).toString() != expected_kind ||
        raster_layer.value(QStringLiteral("thumbnailRenderSource")).toString() !=
            QStringLiteral("layer-raster-thumbnail") ||
        raster_layer.value(QStringLiteral("thumbnailSharedRenderPath")).toBool() ||
        raster_layer.value(QStringLiteral("thumbnailSharedRenderPathContract")).toString() !=
            QStringLiteral("layer-thumbnail-raster-fallback-v1") ||
        raster_layer.value(QStringLiteral("thumbnailPreviewRenderMode")).toString() !=
            QStringLiteral("layer_thumbnail_raster") ||
        raster_layer.value(QStringLiteral("thumbnailMasked")).toBool() ||
        !raster_layer.value(QStringLiteral("thumbnailMaskSetId")).toString().isEmpty() ||
        !raster_layer.value(QStringLiteral("thumbnailSignature"))
             .toString()
             .contains(QStringLiteral("invalidation:")) ||
        !has_thumbnail_job_identity(
            raster_layer,
            QStringLiteral("photo_layer_thumbnail"),
            QStringLiteral("thumbnail")) ||
        !has_thumbnail_pixel_metrics(raster_layer, 32, 64) ||
        !thumbnail_pixels_match_metadata(raster_layer) ||
        !has_thumbnail_invalidation_fields(
            raster_layer,
            QStringLiteral("raster-layer-thumbnail-lifecycle-asset"),
            raster_layer.value(QStringLiteral("id")).toString(),
            QString(),
            QStringLiteral("normal"),
            QStringLiteral("1.000"),
            true) ||
        !has_thumbnail_entry_cache_fields(
            raster_layer,
            QStringLiteral("raster-layer-thumbnail-lifecycle-asset"),
            raster_layer.value(QStringLiteral("id")).toString(),
            QString(),
            64,
            64,
            QStringLiteral("entry-cache"),
            true)) {
      return fail("fill and text layer thumbnails should publish job, cache, invalidation, and raster fallback metadata");
    }
  }
  const auto text_thumbnail_invalidation_key =
      text_thumbnail_layer.value(QStringLiteral("thumbnailInvalidationKey")).toString();
  if (!raster_layer_thumbnail_controller.setPhotoLayerText(
          0,
          QStringLiteral("Updated Mask"))) {
    return fail("raster layer thumbnail proof should edit the text layer");
  }
  wait_for_thumbnail_debounce();
  const auto edited_text_thumbnail_layer =
      wait_for_fresh_photo_layer_thumbnail(raster_layer_thumbnail_controller);
  if (edited_text_thumbnail_layer.value(QStringLiteral("thumbnailInvalidationKey")).toString() ==
          text_thumbnail_invalidation_key ||
      !edited_text_thumbnail_layer.value(QStringLiteral("thumbnailSource"))
           .toString()
           .startsWith(QStringLiteral("data:image/png;base64,")) ||
      edited_text_thumbnail_layer.value(QStringLiteral("thumbnailKind")).toString() !=
          QStringLiteral("text-layer") ||
      !has_thumbnail_entry_cache_fields(
          edited_text_thumbnail_layer,
          QStringLiteral("raster-layer-thumbnail-lifecycle-asset"),
          edited_text_thumbnail_layer.value(QStringLiteral("id")).toString(),
          QString(),
          64,
          64,
          QStringLiteral("entry-cache"),
          true)) {
    return fail("editing text layer content should invalidate and rebuild the text layer thumbnail");
  }

  const auto thumbnail_failure_fixture_path =
      runtime_root.filePath(QStringLiteral("thumbnail-failure-proof.png"));
  if (!write_thumbnail_fixture(thumbnail_failure_fixture_path)) {
    return fail("thumbnail failure fixture should be writable");
  }
  photo_editor::desktop::EditorPreviewController thumbnail_failure_controller;
  thumbnail_failure_controller.loadAsset(
      QStringLiteral("thumbnail-failure-containment-asset"),
      thumbnail_failure_fixture_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  thumbnail_failure_controller.addMask(QStringLiteral("linear"));
  const auto failure_initial_mask_set =
      wait_for_fresh_mask_thumbnails(thumbnail_failure_controller).constFirst().toMap();
  const auto failure_initial_mask_thumbnail =
      failure_initial_mask_set.value(QStringLiteral("thumbnailSource")).toString();
  const auto failure_initial_layer =
      wait_for_fresh_photo_layer_thumbnail(thumbnail_failure_controller);
  const auto failure_initial_layer_thumbnail =
      failure_initial_layer.value(QStringLiteral("thumbnailSource")).toString();
  if (!failure_initial_mask_thumbnail.startsWith(QStringLiteral("data:image/png;base64,")) ||
      !failure_initial_layer_thumbnail.startsWith(QStringLiteral("data:image/png;base64,"))) {
    return fail("thumbnail failure proof should start with valid cached thumbnails");
  }
  if (!QFile::remove(thumbnail_failure_fixture_path)) {
    return fail("thumbnail failure fixture should be removable after cache warmup");
  }
  thumbnail_failure_controller.setActiveMaskDensity(0.72);
  if (!thumbnail_failure_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("exposure"),
          0.35)) {
    return fail("thumbnail failure proof should dirty the linked adjustment layer");
  }
  wait_for_thumbnail_debounce();
  const auto failure_dispatch_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_dispatched_count(thumbnail_failure_controller);
  const auto failure_completed_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_worker_completed_count(thumbnail_failure_controller);
  static_cast<void>(thumbnail_failure_controller.maskSets());
  static_cast<void>(thumbnail_failure_controller.photoLayers());
  if (!wait_for_thumbnail_queue_dispatch(
          thumbnail_failure_controller,
          failure_dispatch_before + 1,
          failure_completed_before + 1,
          6000)) {
    return fail("thumbnail failure proof should settle its contained worker failure");
  }
  const auto failed_mask_set =
      thumbnail_failure_controller.maskSets().constFirst().toMap();
  const auto failed_mask_sources = failed_mask_set.value(QStringLiteral("sources")).toList();
  const auto failed_mask_source =
      failed_mask_sources.isEmpty() ? QVariantMap{} : failed_mask_sources.constFirst().toMap();
  const auto failed_layer =
      thumbnail_failure_controller.photoLayers().constFirst().toMap();
  if (thumbnail_failure_controller.previewFailureStage().startsWith(
          QStringLiteral("thumbnail")) ||
      failed_mask_set.value(QStringLiteral("thumbnailSource")).toString() !=
          failure_initial_mask_thumbnail ||
      failed_mask_set.value(QStringLiteral("thumbnailReady")).toBool() != true ||
      failed_mask_set.value(QStringLiteral("thumbnailStale")).toBool() != true ||
      failed_mask_set.value(QStringLiteral("thumbnailFailureContained")).toBool() != true ||
      failed_mask_set.value(QStringLiteral("thumbnailFailureStage")).toString() !=
          QStringLiteral("thumbnail_source_decode") ||
      failed_mask_source.value(QStringLiteral("thumbnailFailureStage")).toString() !=
          QStringLiteral("thumbnail_source_decode") ||
      failed_layer.value(QStringLiteral("thumbnailSource")).toString() !=
          failure_initial_layer_thumbnail ||
      failed_layer.value(QStringLiteral("thumbnailReady")).toBool() != true ||
      failed_layer.value(QStringLiteral("thumbnailStale")).toBool() != true ||
      failed_layer.value(QStringLiteral("thumbnailFailureContained")).toBool() != true ||
      failed_layer.value(QStringLiteral("thumbnailFailureStage")).toString() !=
          QStringLiteral("thumbnail_source_decode") ||
      !failed_layer.value(QStringLiteral("thumbnailFailureSignature"))
           .toString()
           .contains(QStringLiteral("reused:true"))) {
    std::cerr << "thumbnail failure diagnostics: previewStage="
              << thumbnail_failure_controller.previewFailureStage().toStdString()
              << " maskSourceSame="
              << (failed_mask_set.value(QStringLiteral("thumbnailSource")).toString() ==
                  failure_initial_mask_thumbnail)
              << " maskReady="
              << failed_mask_set.value(QStringLiteral("thumbnailReady")).toBool()
              << " maskStale="
              << failed_mask_set.value(QStringLiteral("thumbnailStale")).toBool()
              << " maskContained="
              << failed_mask_set.value(QStringLiteral("thumbnailFailureContained")).toBool()
              << " maskStage="
              << failed_mask_set.value(QStringLiteral("thumbnailFailureStage"))
                     .toString()
                     .toStdString()
              << " sourceStage="
              << failed_mask_source.value(QStringLiteral("thumbnailFailureStage"))
                     .toString()
                     .toStdString()
              << " layerSourceSame="
              << (failed_layer.value(QStringLiteral("thumbnailSource")).toString() ==
                  failure_initial_layer_thumbnail)
              << " layerReady="
              << failed_layer.value(QStringLiteral("thumbnailReady")).toBool()
              << " layerStale="
              << failed_layer.value(QStringLiteral("thumbnailStale")).toBool()
              << " layerContained="
              << failed_layer.value(QStringLiteral("thumbnailFailureContained")).toBool()
              << " layerStage="
              << failed_layer.value(QStringLiteral("thumbnailFailureStage"))
                     .toString()
                     .toStdString()
              << " layerSignature="
              << failed_layer.value(QStringLiteral("thumbnailFailureSignature"))
                     .toString()
                     .toStdString()
              << '\n';
    return fail("thumbnail decode failures should reuse last good row thumbnails and stay contained away from the main preview failure state");
  }
  if (!write_thumbnail_fixture(thumbnail_failure_fixture_path)) {
    return fail("thumbnail failure fixture should be restorable after contained failure");
  }
  thumbnail_failure_controller.setActiveMaskDensity(0.61);
  if (!thumbnail_failure_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("exposure"),
          0.12)) {
    return fail("thumbnail failure proof should dirty thumbnails after fixture recovery");
  }
  wait_for_thumbnail_debounce();
  static_cast<void>(thumbnail_failure_controller.maskSets());
  static_cast<void>(thumbnail_failure_controller.photoLayers());
  const auto recovered_mask_set =
      wait_for_fresh_mask_thumbnails(thumbnail_failure_controller).constFirst().toMap();
  const auto recovered_mask_sources =
      recovered_mask_set.value(QStringLiteral("sources")).toList();
  const auto recovered_mask_source =
      recovered_mask_sources.isEmpty() ? QVariantMap{} : recovered_mask_sources.constFirst().toMap();
  const auto recovered_layer =
      wait_for_fresh_photo_layer_thumbnail(thumbnail_failure_controller);
  if (recovered_mask_set.value(QStringLiteral("thumbnailReady")).toBool() != true ||
      recovered_mask_set.value(QStringLiteral("thumbnailStale")).toBool() ||
      recovered_mask_set.value(QStringLiteral("thumbnailFailureContained")).toBool() ||
      !recovered_mask_set.value(QStringLiteral("thumbnailFailureStage")).toString().isEmpty() ||
      recovered_mask_source.value(QStringLiteral("thumbnailFailureContained")).toBool() ||
      !recovered_mask_source.value(QStringLiteral("thumbnailFailureStage")).toString().isEmpty() ||
      recovered_layer.value(QStringLiteral("thumbnailReady")).toBool() != true ||
      recovered_layer.value(QStringLiteral("thumbnailStale")).toBool() ||
      recovered_layer.value(QStringLiteral("thumbnailFailureContained")).toBool() ||
      !recovered_layer.value(QStringLiteral("thumbnailFailureStage")).toString().isEmpty()) {
    return fail("successful thumbnail regeneration should clear contained failure state on mask and layer rows");
  }

  photo_editor::desktop::EditorPreviewController copied_mask_set_controller;
  copied_mask_set_controller.addMask(QStringLiteral("linear"));
  copied_mask_set_controller.addMask(QStringLiteral("radial"));
  if (copied_mask_set_controller.maskCount() != 2 ||
      copied_mask_set_controller.photoLayerCount() != 1 ||
      !copied_mask_set_controller.duplicateActiveMaskSet() ||
      copied_mask_set_controller.maskCount() != 4 ||
      copied_mask_set_controller.photoLayerCount() != 2) {
    return fail("duplicating a selection mask set should create one linked adjustment layer for the copied set");
  }
  const auto copied_mask_set_id =
      copied_mask_set_controller.maskStack()
          .at(copied_mask_set_controller.activeMaskIndex())
          .toMap()
          .value(QStringLiteral("maskSetId"))
          .toString();
  const auto copied_mask_set_layer = copied_mask_set_controller.photoLayers().at(0).toMap();
  if (copied_mask_set_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("adjustment") ||
      !copied_mask_set_layer.value(QStringLiteral("selectionScoped")).toBool() ||
      copied_mask_set_layer.value(QStringLiteral("selectionMaskSetId")).toString() !=
          copied_mask_set_id ||
      !almost_equal(copied_mask_set_layer.value(QStringLiteral("exposure")).toDouble(), 0.0) ||
      !almost_equal(copied_mask_set_layer.value(QStringLiteral("temperature")).toDouble(), 0.0)) {
    return fail("duplicated mask set adjustment layers should be neutral and linked to the copied set id");
  }

  if (!controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), 0.72) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("contrast"), -11.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("highlights"), 27.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("shadows"), -31.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("whites"), 44.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("blacks"), -52.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("brightness"), 18.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("filmicShoulder"), -23.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("filmicToe"), 19.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("adaptiveContrast"), 37.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("levelsBlackPoint"), 7.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("levelsGamma"), -16.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("levelsWhitePoint"), 92.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("zoneDeepShadows"), -21.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("zoneShadows"), 14.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("zoneMidtones"), -9.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("zoneLights"), 12.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("zoneHighlights"), -18.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("curveShadows"), 24.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("curveDarks"), -17.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("curveLights"), 31.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("curveHighlights"), -28.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("temperature"), 16.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("tint"), -8.0) ||
      !controller.setPhotoLayerAdjustmentString(0, QStringLiteral("colorMixerModel"), QStringLiteral("HSV")) ||
      !controller.setPhotoLayerAdjustmentString(0, QStringLiteral("hslChannel"), QStringLiteral("Blues")) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("vibrance"), 33.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("saturation"), -14.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("hueAdjustment"), 220.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("selectiveCyan"), -42.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("channelMixerRedFromGreen"), 240.0) ||
      !controller.setPhotoLayerAdjustmentBool(0, QStringLiteral("blackAndWhiteEnabled"), true) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("blackAndWhiteReds"), 58.0) ||
      !controller.setPhotoLayerAdjustmentBool(0, QStringLiteral("colorReplacementEnabled"), true) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("colorReplacementSourceHue"), 1.25) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("colorReplacementRange"), -4.0) ||
      !controller.setPhotoLayerAdjustmentBool(0, QStringLiteral("skinToneProtectionEnabled"), true) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("skinToneLineHue"), -0.25) ||
      !controller.setPhotoLayerAdjustmentBool(0, QStringLiteral("colorHarmonyEnabled"), true) ||
      !controller.setPhotoLayerAdjustmentString(0, QStringLiteral("colorHarmonyMode"), QStringLiteral("Triadic")) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("gradingHighlightsHue"), -0.25) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("gradingAmount"), 125.0)) {
    return fail("selection adjustment layers should accept the advanced layer-local color recipe");
  }

  wait_for_thumbnail_debounce();
  if (!wait_for_preview_idle(controller) ||
      image_sink.image(QStringLiteral("edited")).isNull()) {
    return fail("layer-local color edit should publish an edited preview for thumbnail parity");
  }
  const auto layer = wait_for_fresh_photo_layer_thumbnail(controller);
  if (layer.value(QStringLiteral("thumbnailKind")).toString() !=
          QStringLiteral("masked-adjustment-layer") ||
      layer.value(QStringLiteral("thumbnailRenderSource")).toString() !=
          QStringLiteral("shared-preview-render-request") ||
      !layer.value(QStringLiteral("thumbnailSharedRenderPath")).toBool() ||
      layer.value(QStringLiteral("thumbnailSharedRenderPathContract")).toString() !=
          QStringLiteral("layer-thumbnail-shared-preview-render-v1") ||
      layer.value(QStringLiteral("thumbnailPreviewRenderMode")).toString() !=
          QStringLiteral("layer_thumbnail") ||
      layer.value(QStringLiteral("thumbnailEffectPreviewContract")).toString() !=
          QStringLiteral("adjustment-layer-before-after-thumbnail-v1") ||
      layer.value(QStringLiteral("thumbnailEffectPreviewKind")).toString() !=
          QStringLiteral("before-after-split") ||
      !layer.value(QStringLiteral("thumbnailEffectPreviewSource"))
           .toString()
           .startsWith(QStringLiteral("data:image/png;base64,")) ||
      layer.value(QStringLiteral("thumbnailDisplaySource")).toString() !=
          layer.value(QStringLiteral("thumbnailEffectPreviewSource")).toString() ||
      layer.value(QStringLiteral("async_job_kind")).toString() !=
          QStringLiteral("photo_layer_thumbnail") ||
      layer.value(QStringLiteral("async_job_stage")).toString() != QStringLiteral("thumbnail") ||
      layer.value(QStringLiteral("async_job_id")).toString() ==
          initial_photo_layer.value(QStringLiteral("async_job_id")).toString() ||
      !layer.value(QStringLiteral("thumbnailJobStateSignature")).toString().startsWith(
          QStringLiteral("sha1:")) ||
      !has_thumbnail_job_identity(
          layer,
          QStringLiteral("photo_layer_thumbnail"),
          QStringLiteral("thumbnail")) ||
      layer.value(QStringLiteral("thumbnail_render_budget_ms")).toLongLong() != 100 ||
      layer.value(QStringLiteral("thumbnail_render_elapsed_ms")).toLongLong() < 0 ||
      layer.value(QStringLiteral("thumbnail_render_elapsed_ms")).toLongLong() >
          layer.value(QStringLiteral("thumbnail_render_budget_ms")).toLongLong() ||
      !layer.value(QStringLiteral("thumbnail_render_within_latency_budget")).toBool() ||
      layer.value(QStringLiteral("thumbnailInvalidationKey")).toString() ==
          initial_layer_thumbnail_invalidation_key ||
      !has_thumbnail_invalidation_fields(
          layer,
          QStringLiteral("layer-local-advanced-color-asset"),
          layer.value(QStringLiteral("id")).toString(),
          layer.value(QStringLiteral("selectionMaskSetSignature")).toString(),
          QStringLiteral("normal"),
          QStringLiteral("1.000"),
          true) ||
      !thumbnail_pixels_match_metadata(layer) ||
      layer.value(QStringLiteral("thumbnailSource")).toString() == initial_layer_thumbnail_source) {
    std::cerr << "edited layer diagnostics: kind="
              << layer.value(QStringLiteral("thumbnailKind")).toString().toStdString()
              << " renderSource="
              << layer.value(QStringLiteral("thumbnailRenderSource")).toString().toStdString()
              << " jobIdentity="
              << has_thumbnail_job_identity(
                     layer,
                     QStringLiteral("photo_layer_thumbnail"),
                     QStringLiteral("thumbnail"))
              << " jobChanged="
              << (layer.value(QStringLiteral("async_job_id")).toString() !=
                  initial_photo_layer.value(QStringLiteral("async_job_id")).toString())
              << " elapsed="
              << layer.value(QStringLiteral("thumbnail_render_elapsed_ms")).toLongLong()
              << " budget="
              << layer.value(QStringLiteral("thumbnail_render_budget_ms")).toLongLong()
              << " within="
              << layer.value(QStringLiteral("thumbnail_render_within_latency_budget")).toBool()
              << " invalidationChanged="
              << (layer.value(QStringLiteral("thumbnailInvalidationKey")).toString() !=
                  initial_layer_thumbnail_invalidation_key)
              << " invalidation="
              << has_thumbnail_invalidation_fields(
                     layer,
                     QStringLiteral("layer-local-advanced-color-asset"),
                     layer.value(QStringLiteral("id")).toString(),
                     layer.value(QStringLiteral("selectionMaskSetSignature")).toString(),
                     QStringLiteral("normal"),
                     QStringLiteral("1.000"),
                     true)
              << " pixels=" << thumbnail_pixels_match_metadata(layer)
              << " sourceChanged="
              << (layer.value(QStringLiteral("thumbnailSource")).toString() !=
                  initial_layer_thumbnail_source)
              << '\n';
    return fail("masked adjustment layer thumbnail should update with a fresh thumbnail job after layer-local color edits");
  }
  const auto edited_layer_thumbnail_image =
      decoded_thumbnail_source(layer.value(QStringLiteral("thumbnailSource")).toString());
  if (thumbnail_max_channel_delta(initial_layer_thumbnail_image, edited_layer_thumbnail_image) < 4) {
    return fail("layer-local color edits should visibly change the decoded masked adjustment thumbnail pixels");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_count(controller) !=
          post_entry_layer_thumbnail_refresh_count + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_cache_hit_count(controller) !=
          post_entry_layer_thumbnail_cache_hit_count) {
    return fail("layer-local edits should invalidate and rebuild the cached composited layer thumbnail exactly once");
  }
  const auto edited_layer_thumbnail_source =
      layer.value(QStringLiteral("thumbnailSource")).toString();
  const auto edited_layer_thumbnail_signature =
      layer.value(QStringLiteral("thumbnailSignature")).toString();
  const auto edited_layer_thumbnail_invalidation_key =
      layer.value(QStringLiteral("thumbnailInvalidationKey")).toString();
  const auto edited_layer_thumbnail_refresh_count =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_thumbnail_refresh_count(controller);
  const auto edited_layer_thumbnail_cache_hit_count =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_thumbnail_cache_hit_count(controller);
  const auto cached_edited_layer = controller.photoLayers().at(0).toMap();
  if (cached_edited_layer.value(QStringLiteral("thumbnailSource")).toString() !=
          edited_layer_thumbnail_source ||
      cached_edited_layer.value(QStringLiteral("thumbnailSignature")).toString() !=
          edited_layer_thumbnail_signature ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_count(controller) !=
          edited_layer_thumbnail_refresh_count ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_cache_hit_count(controller) !=
          edited_layer_thumbnail_cache_hit_count + 1) {
    return fail("rebuilt masked adjustment layer thumbnails should be cached until a layer presentation field changes");
  }
  if (!controller.setPhotoLayerOpacity(0, 0.43)) {
    return fail("test should be able to change masked adjustment layer opacity");
  }
  wait_for_thumbnail_debounce();
  const auto opacity_layer = wait_for_fresh_photo_layer_thumbnail(controller);
  const auto opacity_layer_thumbnail_source =
      opacity_layer.value(QStringLiteral("thumbnailSource")).toString();
  const auto opacity_layer_thumbnail_signature =
      opacity_layer.value(QStringLiteral("thumbnailSignature")).toString();
  if (opacity_layer_thumbnail_source == edited_layer_thumbnail_source ||
      opacity_layer_thumbnail_signature == edited_layer_thumbnail_signature ||
      opacity_layer.value(QStringLiteral("thumbnailInvalidationKey")).toString() ==
          edited_layer_thumbnail_invalidation_key ||
      !has_thumbnail_invalidation_fields(
          opacity_layer,
          QStringLiteral("layer-local-advanced-color-asset"),
          opacity_layer.value(QStringLiteral("id")).toString(),
          opacity_layer.value(QStringLiteral("selectionMaskSetSignature")).toString(),
          QStringLiteral("normal"),
          QStringLiteral("0.430"),
          true) ||
      !thumbnail_pixels_match_metadata(opacity_layer) ||
      !opacity_layer_thumbnail_signature.contains(QStringLiteral("opacity:0.430"))) {
    return fail("masked adjustment layer thumbnail identity and pixels should change after opacity edits");
  }
  const auto opacity_layer_thumbnail_image =
      decoded_thumbnail_source(opacity_layer_thumbnail_source);
  if (thumbnail_max_channel_delta(edited_layer_thumbnail_image, opacity_layer_thumbnail_image) < 2) {
    return fail("opacity edits should visibly change the decoded masked adjustment thumbnail pixels");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_count(controller) !=
          edited_layer_thumbnail_refresh_count + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_cache_hit_count(controller) !=
          edited_layer_thumbnail_cache_hit_count + 1) {
    return fail("opacity edits should invalidate and rebuild the cached masked adjustment thumbnail once without consuming a stale hit");
  }
  if (!controller.setPhotoLayerBlendMode(0, QStringLiteral("screen"))) {
    return fail("test should be able to change masked adjustment layer blend mode");
  }
  const auto blend_layer = wait_for_fresh_photo_layer_thumbnail(controller);
  const auto blend_layer_thumbnail_source =
      blend_layer.value(QStringLiteral("thumbnailSource")).toString();
  const auto blend_layer_thumbnail_signature =
      blend_layer.value(QStringLiteral("thumbnailSignature")).toString();
  if (blend_layer_thumbnail_source == opacity_layer_thumbnail_source ||
      blend_layer_thumbnail_signature == opacity_layer_thumbnail_signature ||
      blend_layer.value(QStringLiteral("thumbnailInvalidationKey")).toString() ==
          opacity_layer.value(QStringLiteral("thumbnailInvalidationKey")).toString() ||
      !has_thumbnail_invalidation_fields(
          blend_layer,
          QStringLiteral("layer-local-advanced-color-asset"),
          blend_layer.value(QStringLiteral("id")).toString(),
          blend_layer.value(QStringLiteral("selectionMaskSetSignature")).toString(),
          QStringLiteral("screen"),
          QStringLiteral("0.430"),
          true) ||
      !thumbnail_pixels_match_metadata(blend_layer) ||
      !blend_layer_thumbnail_signature.contains(QStringLiteral("blend:screen"))) {
    return fail("masked adjustment layer thumbnail identity and pixels should change after blend-mode edits");
  }
  const auto blend_layer_thumbnail_image =
      decoded_thumbnail_source(blend_layer_thumbnail_source);
  if (thumbnail_max_channel_delta(opacity_layer_thumbnail_image, blend_layer_thumbnail_image) < 2) {
    return fail("blend-mode edits should visibly change the decoded masked adjustment thumbnail pixels");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_count(controller) !=
          edited_layer_thumbnail_refresh_count + 2 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_cache_hit_count(controller) !=
          edited_layer_thumbnail_cache_hit_count + 1) {
    return fail("blend-mode edits should invalidate and rebuild the cached masked adjustment thumbnail once without consuming a stale hit");
  }
  const auto blend_layer_thumbnail_invalidation_key =
      blend_layer.value(QStringLiteral("thumbnailInvalidationKey")).toString();
  if (!controller.setPhotoLayerEnabled(0, false)) {
    return fail("test should be able to hide a masked adjustment layer");
  }
  wait_for_thumbnail_debounce();
  const auto hidden_layer = wait_for_fresh_photo_layer_thumbnail(controller);
  const auto hidden_layer_thumbnail_invalidation_key =
      hidden_layer.value(QStringLiteral("thumbnailInvalidationKey")).toString();
  if (hidden_layer_thumbnail_invalidation_key == blend_layer_thumbnail_invalidation_key ||
      !has_thumbnail_invalidation_fields(
          hidden_layer,
          QStringLiteral("layer-local-advanced-color-asset"),
          hidden_layer.value(QStringLiteral("id")).toString(),
          hidden_layer.value(QStringLiteral("selectionMaskSetSignature")).toString(),
          QStringLiteral("screen"),
          QStringLiteral("0.430"),
          false) ||
      !hidden_layer.value(QStringLiteral("thumbnailInvalidationSignature"))
           .toString()
           .contains(QStringLiteral("visible:false"))) {
    return fail("layer visibility changes should invalidate the masked adjustment thumbnail identity exactly");
  }
  if (!controller.setPhotoLayerEnabled(0, true)) {
    return fail("test should be able to show a masked adjustment layer again");
  }
  wait_for_thumbnail_debounce();
  const auto visible_layer = wait_for_fresh_photo_layer_thumbnail(controller);
  const auto visible_layer_thumbnail_invalidation_key =
      visible_layer.value(QStringLiteral("thumbnailInvalidationKey")).toString();
  if (visible_layer_thumbnail_invalidation_key == hidden_layer_thumbnail_invalidation_key ||
      !has_thumbnail_invalidation_fields(
          visible_layer,
          QStringLiteral("layer-local-advanced-color-asset"),
          visible_layer.value(QStringLiteral("id")).toString(),
          visible_layer.value(QStringLiteral("selectionMaskSetSignature")).toString(),
          QStringLiteral("screen"),
          QStringLiteral("0.430"),
          true)) {
    return fail("restoring layer visibility should publish a fresh visible thumbnail invalidation identity");
  }
  const auto visible_layer_mask_alpha_hash =
      visible_layer.value(QStringLiteral("thumbnailInvalidationMaskAlphaHash")).toString();
  const auto visible_mask_set =
      wait_for_fresh_mask_thumbnails(controller).constFirst().toMap();
  const auto visible_mask_set_invalidation_key =
      visible_mask_set.value(QStringLiteral("thumbnailInvalidationKey")).toString();
  const auto visible_mask_set_alpha_hash =
      visible_mask_set.value(QStringLiteral("thumbnailInvalidationMaskAlphaHash")).toString();
  controller.setActiveMaskPosition(0.28);
  wait_for_thumbnail_debounce();
  if (!wait_for_preview_idle(controller)) {
    return fail("mask shape edit should settle before thumbnail invalidation is inspected");
  }
  const auto mask_edited_set =
      wait_for_fresh_mask_thumbnails(controller).constFirst().toMap();
  const auto mask_edited_layer = wait_for_fresh_photo_layer_thumbnail(controller);
  if (mask_edited_set.value(QStringLiteral("thumbnailInvalidationKey")).toString() ==
          visible_mask_set_invalidation_key ||
      mask_edited_set.value(QStringLiteral("thumbnailInvalidationMaskAlphaHash")).toString() ==
          visible_mask_set_alpha_hash ||
      !has_thumbnail_invalidation_fields(
          mask_edited_set,
          QStringLiteral("layer-local-advanced-color-asset"),
          QString(),
          mask_edited_set.value(QStringLiteral("mask_set_id"), mask_edited_set.value(QStringLiteral("id")))
              .toString(),
          QStringLiteral("normal"),
          QStringLiteral("1.000"),
          true) ||
      mask_edited_layer.value(QStringLiteral("thumbnailInvalidationKey")).toString() ==
          visible_layer_thumbnail_invalidation_key ||
      mask_edited_layer.value(QStringLiteral("thumbnailInvalidationMaskAlphaHash")).toString() ==
          visible_layer_mask_alpha_hash ||
      !has_thumbnail_invalidation_fields(
          mask_edited_layer,
          QStringLiteral("layer-local-advanced-color-asset"),
          mask_edited_layer.value(QStringLiteral("id")).toString(),
          mask_edited_layer.value(QStringLiteral("selectionMaskSetSignature")).toString(),
          QStringLiteral("screen"),
          QStringLiteral("0.430"),
          true)) {
    return fail("mask shape edits should update exact alpha and invalidation identity for mask and linked layer thumbnails");
  }
  const auto mask_shape_thumbnail_source =
      mask_edited_set.value(QStringLiteral("thumbnailSource")).toString();
  const auto mask_shape_thumbnail_signature =
      mask_edited_set.value(QStringLiteral("thumbnailSignature")).toString();
  const auto mask_shape_alpha_hash =
      mask_edited_set.value(QStringLiteral("thumbnailInvalidationMaskAlphaHash")).toString();
  const auto layer_after_shape_thumbnail_source =
      mask_edited_layer.value(QStringLiteral("thumbnailSource")).toString();
  const auto layer_after_shape_thumbnail_signature =
      mask_edited_layer.value(QStringLiteral("thumbnailSignature")).toString();
  const auto layer_after_shape_alpha_hash =
      mask_edited_layer.value(QStringLiteral("thumbnailInvalidationMaskAlphaHash")).toString();
  controller.setActiveMaskDensity(0.58);
  wait_for_thumbnail_debounce();
  if (!wait_for_preview_idle(controller)) {
    return fail("mask density edit should settle before thumbnail freshness is inspected");
  }
  const auto density_edited_set =
      wait_for_fresh_mask_thumbnails(controller).constFirst().toMap();
  const auto density_edited_layer = wait_for_fresh_photo_layer_thumbnail(controller);
  if (density_edited_set.value(QStringLiteral("thumbnailSource")).toString() ==
          mask_shape_thumbnail_source ||
      density_edited_set.value(QStringLiteral("thumbnailSignature")).toString() ==
          mask_shape_thumbnail_signature ||
      density_edited_set.value(QStringLiteral("thumbnailInvalidationMaskAlphaHash")).toString() ==
          mask_shape_alpha_hash ||
      !has_thumbnail_invalidation_fields(
          density_edited_set,
          QStringLiteral("layer-local-advanced-color-asset"),
          QString(),
          density_edited_set.value(QStringLiteral("mask_set_id"), density_edited_set.value(QStringLiteral("id")))
              .toString(),
          QStringLiteral("normal"),
          QStringLiteral("1.000"),
          true) ||
      !has_evaluated_alpha_thumbnail_metadata(density_edited_set) ||
      !thumbnail_alpha_matches_metadata(density_edited_set) ||
      density_edited_layer.value(QStringLiteral("thumbnailSource")).toString() ==
          layer_after_shape_thumbnail_source ||
      density_edited_layer.value(QStringLiteral("thumbnailSignature")).toString() ==
          layer_after_shape_thumbnail_signature ||
      density_edited_layer.value(QStringLiteral("thumbnailInvalidationMaskAlphaHash")).toString() ==
          layer_after_shape_alpha_hash ||
      !has_thumbnail_invalidation_fields(
          density_edited_layer,
          QStringLiteral("layer-local-advanced-color-asset"),
          density_edited_layer.value(QStringLiteral("id")).toString(),
          density_edited_layer.value(QStringLiteral("selectionMaskSetSignature")).toString(),
          QStringLiteral("screen"),
          QStringLiteral("0.430"),
          true) ||
      !thumbnail_pixels_match_metadata(density_edited_layer) ||
      !has_evaluated_alpha_thumbnail_metadata(density_edited_layer)) {
    std::cerr << "density thumbnail diagnostics: maskSourceChanged="
              << (density_edited_set.value(QStringLiteral("thumbnailSource")).toString() !=
                  mask_shape_thumbnail_source)
              << " maskSignatureChanged="
              << (density_edited_set.value(QStringLiteral("thumbnailSignature")).toString() !=
                  mask_shape_thumbnail_signature)
              << " maskAlphaChanged="
              << (density_edited_set
                      .value(QStringLiteral("thumbnailInvalidationMaskAlphaHash"))
                      .toString() != mask_shape_alpha_hash)
              << " maskInvalidation="
              << has_thumbnail_invalidation_fields(
                     density_edited_set,
                     QStringLiteral("layer-local-advanced-color-asset"),
                     QString(),
                     density_edited_set
                         .value(QStringLiteral("mask_set_id"),
                                density_edited_set.value(QStringLiteral("id")))
                         .toString(),
                     QStringLiteral("normal"),
                     QStringLiteral("1.000"),
                     true)
              << " maskAlpha="
              << has_evaluated_alpha_thumbnail_metadata(density_edited_set)
              << " layerSourceChanged="
              << (density_edited_layer.value(QStringLiteral("thumbnailSource")).toString() !=
                  layer_after_shape_thumbnail_source)
              << " layerSignatureChanged="
              << (density_edited_layer.value(QStringLiteral("thumbnailSignature")).toString() !=
                  layer_after_shape_thumbnail_signature)
              << " layerAlphaChanged="
              << (density_edited_layer
                      .value(QStringLiteral("thumbnailInvalidationMaskAlphaHash"))
                      .toString() != layer_after_shape_alpha_hash)
              << " layerInvalidation="
              << has_thumbnail_invalidation_fields(
                     density_edited_layer,
                     QStringLiteral("layer-local-advanced-color-asset"),
                     density_edited_layer.value(QStringLiteral("id")).toString(),
                     density_edited_layer
                         .value(QStringLiteral("selectionMaskSetSignature"))
                         .toString(),
                     QStringLiteral("screen"),
                     QStringLiteral("0.430"),
                     true)
              << " layerPixels=" << thumbnail_pixels_match_metadata(density_edited_layer)
              << " layerAlpha="
              << has_evaluated_alpha_thumbnail_metadata(density_edited_layer)
              << '\n';
    return fail("mask density edits should refresh mask and linked layer thumbnails from the new evaluated alpha");
  }
  const auto adjustments = required_map(layer, QStringLiteral("adjustments"));
  const auto color_mixer = required_map(adjustments, QStringLiteral("colorMixer"));
  const auto color_channels = required_map(color_mixer, QStringLiteral("channels"));
  const auto blue_channel = required_map(color_channels, QStringLiteral("Blues"));
  const auto rgb_channel_mixer = required_map(color_mixer, QStringLiteral("rgbChannelMixer"));
  const auto black_and_white = required_map(color_mixer, QStringLiteral("blackAndWhite"));
  const auto black_and_white_channels = required_map(black_and_white, QStringLiteral("channels"));
  const auto color_replacement = required_map(color_mixer, QStringLiteral("colorReplacement"));
  const auto skin_protection = required_map(color_mixer, QStringLiteral("skinToneProtection"));
  const auto harmony = required_map(color_mixer, QStringLiteral("colorHarmony"));
  const auto grading = required_map(adjustments, QStringLiteral("colorGrading"));
  const auto grading_highlights = required_map(grading, QStringLiteral("highlights"));

  if (!almost_equal(layer.value(QStringLiteral("exposure")).toDouble(), 0.72) ||
      !almost_equal(layer.value(QStringLiteral("contrast")).toDouble(), -11.0) ||
      !almost_equal(layer.value(QStringLiteral("highlights")).toDouble(), 27.0) ||
      !almost_equal(layer.value(QStringLiteral("shadows")).toDouble(), -31.0) ||
      !almost_equal(layer.value(QStringLiteral("whites")).toDouble(), 44.0) ||
      !almost_equal(layer.value(QStringLiteral("blacks")).toDouble(), -52.0) ||
      !almost_equal(layer.value(QStringLiteral("brightness")).toDouble(), 18.0) ||
      !almost_equal(layer.value(QStringLiteral("filmicShoulder")).toDouble(), -23.0) ||
      !almost_equal(layer.value(QStringLiteral("filmicToe")).toDouble(), 19.0) ||
      !almost_equal(layer.value(QStringLiteral("adaptiveContrast")).toDouble(), 37.0) ||
      !almost_equal(layer.value(QStringLiteral("levelsBlackPoint")).toDouble(), 7.0) ||
      !almost_equal(layer.value(QStringLiteral("levelsGamma")).toDouble(), -16.0) ||
      !almost_equal(layer.value(QStringLiteral("levelsWhitePoint")).toDouble(), 92.0) ||
      !almost_equal(layer.value(QStringLiteral("zoneDeepShadows")).toDouble(), -21.0) ||
      !almost_equal(layer.value(QStringLiteral("zoneShadows")).toDouble(), 14.0) ||
      !almost_equal(layer.value(QStringLiteral("zoneMidtones")).toDouble(), -9.0) ||
      !almost_equal(layer.value(QStringLiteral("zoneLights")).toDouble(), 12.0) ||
      !almost_equal(layer.value(QStringLiteral("zoneHighlights")).toDouble(), -18.0) ||
      !almost_equal(layer.value(QStringLiteral("curveShadows")).toDouble(), 24.0) ||
      !almost_equal(layer.value(QStringLiteral("curveDarks")).toDouble(), -17.0) ||
      !almost_equal(layer.value(QStringLiteral("curveLights")).toDouble(), 31.0) ||
      !almost_equal(layer.value(QStringLiteral("curveHighlights")).toDouble(), -28.0) ||
      !almost_equal(layer.value(QStringLiteral("temperature")).toDouble(), 16.0) ||
      !almost_equal(layer.value(QStringLiteral("tint")).toDouble(), -8.0) ||
      !almost_equal(layer.value(QStringLiteral("vibrance")).toDouble(), 33.0) ||
      !almost_equal(layer.value(QStringLiteral("saturation")).toDouble(), -14.0) ||
      color_mixer.value(QStringLiteral("model")).toString() != QStringLiteral("HSV") ||
      color_mixer.value(QStringLiteral("channel")).toString() != QStringLiteral("Blues") ||
      !almost_equal(blue_channel.value(QStringLiteral("hue")).toDouble(), 100.0) ||
      !almost_equal(blue_channel.value(QStringLiteral("selectiveCyan")).toDouble(), -42.0) ||
      !almost_equal(rgb_channel_mixer.value(QStringLiteral("redFromGreen")).toDouble(), 200.0) ||
      !black_and_white.value(QStringLiteral("enabled")).toBool() ||
      !almost_equal(black_and_white_channels.value(QStringLiteral("Reds")).toDouble(), 58.0) ||
      !color_replacement.value(QStringLiteral("enabled")).toBool() ||
      !almost_equal(color_replacement.value(QStringLiteral("sourceHue")).toDouble(), 0.25) ||
      !almost_equal(color_replacement.value(QStringLiteral("range")).toDouble(), 1.0) ||
      !almost_equal(color_replacement.value(QStringLiteral("strength")).toDouble(), 75.0) ||
      !skin_protection.value(QStringLiteral("enabled")).toBool() ||
      !almost_equal(skin_protection.value(QStringLiteral("lineHue")).toDouble(), 0.75) ||
      !almost_equal(skin_protection.value(QStringLiteral("strength")).toDouble(), 65.0) ||
      !harmony.value(QStringLiteral("enabled")).toBool() ||
      harmony.value(QStringLiteral("mode")).toString() != QStringLiteral("Triadic") ||
      !almost_equal(harmony.value(QStringLiteral("strength")).toDouble(), 45.0) ||
      !almost_equal(grading_highlights.value(QStringLiteral("hue")).toDouble(), 0.75) ||
      !almost_equal(grading.value(QStringLiteral("amount")).toDouble(), 100.0)) {
    return fail("advanced layer-local color recipe should persist normalized nested values on the layer");
  }

  if (!almost_equal(controller.exposure(), 0.0) ||
      !almost_equal(controller.contrast(), 0.0) ||
      !almost_equal(controller.highlights(), 0.0) ||
      !almost_equal(controller.shadows(), 0.0) ||
      !almost_equal(controller.whites(), 0.0) ||
      !almost_equal(controller.blacks(), 0.0) ||
      !almost_equal(controller.brightness(), 0.0) ||
      !almost_equal(controller.filmicShoulder(), 0.0) ||
      !almost_equal(controller.filmicToe(), 0.0) ||
      !almost_equal(controller.adaptiveContrast(), 0.0) ||
      !almost_equal(controller.levelsBlackPoint(), 0.0) ||
      !almost_equal(controller.levelsGamma(), 0.0) ||
      !almost_equal(controller.levelsWhitePoint(), 0.0) ||
      !almost_equal(controller.zoneDeepShadows(), 0.0) ||
      !almost_equal(controller.zoneShadows(), 0.0) ||
      !almost_equal(controller.zoneMidtones(), 0.0) ||
      !almost_equal(controller.zoneLights(), 0.0) ||
      !almost_equal(controller.zoneHighlights(), 0.0) ||
      !almost_equal(controller.curveShadows(), 0.0) ||
      !almost_equal(controller.curveDarks(), 0.0) ||
      !almost_equal(controller.curveLights(), 0.0) ||
      !almost_equal(controller.curveHighlights(), 0.0) ||
      !almost_equal(controller.temperature(), 0.0) ||
      !almost_equal(controller.tint(), 0.0) ||
      !almost_equal(controller.vibrance(), 0.0) ||
      !almost_equal(controller.saturation(), 0.0) ||
      controller.colorMixerModel() == QStringLiteral("HSV") ||
      controller.hslChannel() == QStringLiteral("Blues") ||
      controller.blackAndWhiteEnabled() ||
      controller.colorReplacementEnabled() ||
      controller.skinToneProtectionEnabled() ||
      controller.colorHarmonyEnabled()) {
    return fail("layer-local advanced color edits should not mutate the base/global recipe");
  }

  if (!controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("whiteBalance"))) {
    return fail("layer-local white balance reset should clear only layer temperature and tint");
  }

  photo_editor::desktop::EditorPreviewController parametric_curve_edge_controller;
  parametric_curve_edge_controller.addMask(QStringLiteral("linear"));
  if (parametric_curve_edge_controller.photoLayerCount() != 1 ||
      !parametric_curve_edge_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("curveShadows"), 140.0) ||
      !parametric_curve_edge_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("curveDarks"), -140.0) ||
      !parametric_curve_edge_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("curveLights"), 140.0) ||
      !parametric_curve_edge_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("curveHighlights"), -140.0)) {
    return fail("parametric layer-local curve edge proof should create and edit a selection layer");
  }
  const auto parametric_curve_edge_layer =
      parametric_curve_edge_controller.photoLayers().at(0).toMap();
  if (!almost_equal(parametric_curve_edge_layer.value(QStringLiteral("curveShadows")).toDouble(), 100.0) ||
      !almost_equal(parametric_curve_edge_layer.value(QStringLiteral("curveDarks")).toDouble(), -100.0) ||
      !almost_equal(parametric_curve_edge_layer.value(QStringLiteral("curveLights")).toDouble(), 100.0) ||
      !almost_equal(parametric_curve_edge_layer.value(QStringLiteral("curveHighlights")).toDouble(), -100.0)) {
    return fail("parametric layer-local curve values should clamp to safe layer ranges");
  }
  if (!parametric_curve_edge_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("toneCurve"))) {
    return fail("parametric layer-local curve reset should remove edited curve controls");
  }
  const auto parametric_curve_reset_layer =
      parametric_curve_edge_controller.photoLayers().at(0).toMap();
  if (parametric_curve_reset_layer.contains(QStringLiteral("curveShadows")) ||
      parametric_curve_reset_layer.contains(QStringLiteral("curveDarks")) ||
      parametric_curve_reset_layer.contains(QStringLiteral("curveLights")) ||
      parametric_curve_reset_layer.contains(QStringLiteral("curveHighlights")) ||
      parametric_curve_edge_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("toneCurve"))) {
    return fail("parametric layer-local curve reset should be exact and idempotent");
  }

  photo_editor::desktop::EditorPreviewController point_curve_controller;
  point_curve_controller.addMask(QStringLiteral("linear"));
  const auto master_points = curve_points({{1.0, 0.96}, {0.70, 0.82}, {0.30, 0.25}, {0.0, 0.05}});
  const auto luma_points = curve_points({{0.0, 0.0}, {0.50, 0.44}, {1.0, 1.0}});
  const auto red_points = curve_points({{0.0, 0.0}, {0.40, 0.58}, {1.0, 1.0}});
  const auto green_points = curve_points({{0.0, 0.0}, {0.55, 0.48}, {1.0, 1.0}});
  const auto blue_points = curve_points({{0.0, 0.0}, {0.70, 0.60}, {1.0, 1.0}});
  if (point_curve_controller.photoLayerCount() != 1 ||
      !point_curve_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("curveShadows"), 21.0) ||
      !point_curve_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("curveDarks"), -13.0) ||
      !point_curve_controller.setPhotoLayerCurvePoints(0, QStringLiteral("RGB"), master_points) ||
      !point_curve_controller.setPhotoLayerCurvePoints(0, QStringLiteral("Luma"), luma_points) ||
      !point_curve_controller.setPhotoLayerCurvePoints(0, QStringLiteral("Red"), red_points) ||
      !point_curve_controller.setPhotoLayerCurvePoints(0, QStringLiteral("Green"), green_points) ||
      !point_curve_controller.setPhotoLayerCurvePoints(0, QStringLiteral("Blue"), blue_points)) {
    return fail("point curve proof should create and edit a selection adjustment layer");
  }
  const auto point_curve_layer = point_curve_controller.photoLayers().at(0).toMap();
  const auto stored_master_points = point_curve_layer.value(QStringLiteral("masterCurvePoints")).toList();
  const auto stored_luma_points = point_curve_layer.value(QStringLiteral("lumaCurvePoints")).toList();
  const auto stored_red_points = point_curve_layer.value(QStringLiteral("redCurvePoints")).toList();
  const auto stored_green_points = point_curve_layer.value(QStringLiteral("greenCurvePoints")).toList();
  const auto stored_blue_points = point_curve_layer.value(QStringLiteral("blueCurvePoints")).toList();
  if (point_curve_layer.contains(QStringLiteral("curveShadows")) ||
      point_curve_layer.contains(QStringLiteral("curveDarks")) ||
      stored_master_points.size() != 4 ||
      !curve_point_matches(stored_master_points, 0, 0.0, 0.05) ||
      !curve_point_matches(stored_master_points, 1, 0.30, 0.25) ||
      !curve_point_matches(stored_master_points, 2, 0.70, 0.82) ||
      !curve_point_matches(stored_master_points, 3, 1.0, 0.96) ||
      stored_luma_points.size() != 3 ||
      !curve_point_matches(stored_luma_points, 1, 0.50, 0.44) ||
      stored_red_points.size() != 3 ||
      !curve_point_matches(stored_red_points, 1, 0.40, 0.58) ||
      stored_green_points.size() != 3 ||
      !curve_point_matches(stored_green_points, 1, 0.55, 0.48) ||
      stored_blue_points.size() != 3 ||
      !curve_point_matches(stored_blue_points, 1, 0.70, 0.60) ||
      !almost_equal(point_curve_controller.curveShadows(), 0.0) ||
      !almost_equal(point_curve_controller.curveDarks(), 0.0) ||
      !almost_equal(point_curve_controller.curveLights(), 0.0) ||
      !almost_equal(point_curve_controller.curveHighlights(), 0.0)) {
    return fail("layer-local point curves should persist per channel without mutating global curve controls");
  }
  if (point_curve_controller.setPhotoLayerCurvePoints(0, QStringLiteral("Red"), red_points)) {
    return fail("setting an identical layer-local point curve should report no-op");
  }
  if (!point_curve_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("toneCurve"))) {
    return fail("layer-local point curve reset should remove all curve channels");
  }
  const auto point_curve_reset_layer = point_curve_controller.photoLayers().at(0).toMap();
  if (point_curve_reset_layer.contains(QStringLiteral("masterCurvePoints")) ||
      point_curve_reset_layer.contains(QStringLiteral("lumaCurvePoints")) ||
      point_curve_reset_layer.contains(QStringLiteral("redCurvePoints")) ||
      point_curve_reset_layer.contains(QStringLiteral("greenCurvePoints")) ||
      point_curve_reset_layer.contains(QStringLiteral("blueCurvePoints")) ||
      point_curve_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("toneCurve"))) {
    return fail("layer-local point curve reset should be exact and idempotent");
  }

  photo_editor::desktop::EditorPreviewController hsl_mixer_controller;
  hsl_mixer_controller.addMask(QStringLiteral("linear"));
  const QStringList mixer_channels = {
      QStringLiteral("Reds"),
      QStringLiteral("Oranges"),
      QStringLiteral("Yellows"),
      QStringLiteral("Greens"),
      QStringLiteral("Aquas"),
      QStringLiteral("Blues"),
      QStringLiteral("Purples"),
      QStringLiteral("Magentas"),
  };
  if (hsl_mixer_controller.photoLayerCount() != 1) {
    return fail("HSL mixer proof should create and prepare a selection adjustment layer");
  }
  const auto initial_global_color_mixer_model = hsl_mixer_controller.colorMixerModel();
  const auto initial_global_hsl_channel = hsl_mixer_controller.hslChannel();
  if (!hsl_mixer_controller.setPhotoLayerAdjustmentString(
          0, QStringLiteral("colorMixerModel"), QStringLiteral("HSL"))) {
    return fail("HSL mixer proof should prepare a layer-local mixer model");
  }
  for (int channel_index = 0; channel_index < mixer_channels.size(); ++channel_index) {
    const auto channel = mixer_channels.at(channel_index);
    if (!hsl_mixer_controller.setPhotoLayerAdjustmentString(
            0, QStringLiteral("hslChannel"), channel) ||
        !hsl_mixer_controller.setPhotoLayerAdjustmentValue(
            0, QStringLiteral("hueAdjustment"), -80.0 + channel_index * 20.0) ||
        !hsl_mixer_controller.setPhotoLayerAdjustmentValue(
            0, QStringLiteral("saturationAdjustment"), 70.0 - channel_index * 10.0) ||
        !hsl_mixer_controller.setPhotoLayerAdjustmentValue(
            0, QStringLiteral("luminanceAdjustment"), -40.0 + channel_index * 8.0) ||
        !hsl_mixer_controller.setPhotoLayerAdjustmentValue(
            0, QStringLiteral("selectiveCyan"), -60.0 + channel_index * 5.0) ||
        !hsl_mixer_controller.setPhotoLayerAdjustmentValue(
            0, QStringLiteral("selectiveMagenta"), 55.0 - channel_index * 4.0) ||
        !hsl_mixer_controller.setPhotoLayerAdjustmentValue(
            0, QStringLiteral("selectiveYellow"), -45.0 + channel_index * 6.0) ||
        !hsl_mixer_controller.setPhotoLayerAdjustmentValue(
            0, QStringLiteral("selectiveBlack"), 35.0 - channel_index * 3.0)) {
      return fail("HSL mixer proof should edit every color channel on the layer");
    }
  }
  const auto hsl_mixer_layer = hsl_mixer_controller.photoLayers().at(0).toMap();
  const auto hsl_mixer_adjustments = required_map(hsl_mixer_layer, QStringLiteral("adjustments"));
  const auto hsl_color_mixer = required_map(hsl_mixer_adjustments, QStringLiteral("colorMixer"));
  const auto hsl_channels = required_map(hsl_color_mixer, QStringLiteral("channels"));
  if (hsl_color_mixer.value(QStringLiteral("model")).toString() != QStringLiteral("HSL") ||
      hsl_color_mixer.value(QStringLiteral("channel")).toString() != QStringLiteral("Magentas")) {
    return fail("HSL mixer should persist layer-local model and active channel");
  }
  for (int channel_index = 0; channel_index < mixer_channels.size(); ++channel_index) {
    const auto channel = required_map(hsl_channels, mixer_channels.at(channel_index));
    if (!almost_equal(channel.value(QStringLiteral("hue")).toDouble(), -80.0 + channel_index * 20.0) ||
        !almost_equal(channel.value(QStringLiteral("saturation")).toDouble(), 70.0 - channel_index * 10.0) ||
        !almost_equal(channel.value(QStringLiteral("luminance")).toDouble(), -40.0 + channel_index * 8.0) ||
        !almost_equal(channel.value(QStringLiteral("selectiveCyan")).toDouble(), -60.0 + channel_index * 5.0) ||
        !almost_equal(channel.value(QStringLiteral("selectiveMagenta")).toDouble(), 55.0 - channel_index * 4.0) ||
        !almost_equal(channel.value(QStringLiteral("selectiveYellow")).toDouble(), -45.0 + channel_index * 6.0) ||
        !almost_equal(channel.value(QStringLiteral("selectiveBlack")).toDouble(), 35.0 - channel_index * 3.0)) {
      return fail("HSL mixer should persist every layer-local channel field");
    }
  }
  if (hsl_mixer_controller.colorMixerModel() != initial_global_color_mixer_model ||
      hsl_mixer_controller.hslChannel() != initial_global_hsl_channel ||
      !almost_equal(hsl_mixer_controller.hueAdjustment(), 0.0) ||
      !almost_equal(hsl_mixer_controller.saturationAdjustment(), 0.0) ||
      !almost_equal(hsl_mixer_controller.luminanceAdjustment(), 0.0) ||
      !almost_equal(hsl_mixer_controller.selectiveCyan(), 0.0) ||
      !almost_equal(hsl_mixer_controller.selectiveMagenta(), 0.0) ||
      !almost_equal(hsl_mixer_controller.selectiveYellow(), 0.0) ||
      !almost_equal(hsl_mixer_controller.selectiveBlack(), 0.0)) {
    return fail("layer-local HSL mixer edits should not mutate global mixer controls");
  }
  if (!hsl_mixer_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("hsl"))) {
    return fail("layer-local HSL mixer reset should remove the color mixer object");
  }
  const auto hsl_mixer_reset_layer = hsl_mixer_controller.photoLayers().at(0).toMap();
  if (hsl_mixer_reset_layer.value(QStringLiteral("adjustments")).toMap().contains(QStringLiteral("colorMixer")) ||
      hsl_mixer_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("hsl"))) {
    return fail("layer-local HSL mixer reset should be exact and idempotent");
  }

  photo_editor::desktop::EditorPreviewController rgb_mixer_controller;
  rgb_mixer_controller.addMask(QStringLiteral("linear"));
  if (rgb_mixer_controller.photoLayerCount() != 1 ||
      !rgb_mixer_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("channelMixerRedFromRed"), 260.0) ||
      !rgb_mixer_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("channelMixerRedFromGreen"), -240.0) ||
      !rgb_mixer_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("channelMixerRedFromBlue"), 35.0) ||
      !rgb_mixer_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("channelMixerGreenFromRed"), 44.0) ||
      !rgb_mixer_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("channelMixerGreenFromGreen"), -250.0) ||
      !rgb_mixer_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("channelMixerGreenFromBlue"), 88.0) ||
      !rgb_mixer_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("channelMixerBlueFromRed"), -91.0) ||
      !rgb_mixer_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("channelMixerBlueFromGreen"), 120.0) ||
      !rgb_mixer_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("channelMixerBlueFromBlue"), 240.0)) {
    return fail("RGB channel mixer proof should create and edit all matrix terms");
  }
  const auto rgb_mixer_layer = rgb_mixer_controller.photoLayers().at(0).toMap();
  const auto rgb_mixer_adjustments = required_map(rgb_mixer_layer, QStringLiteral("adjustments"));
  const auto rgb_color_mixer = required_map(rgb_mixer_adjustments, QStringLiteral("colorMixer"));
  const auto rgb_channel_mixer_proof =
      required_map(rgb_color_mixer, QStringLiteral("rgbChannelMixer"));
  if (!almost_equal(rgb_channel_mixer_proof.value(QStringLiteral("redFromRed")).toDouble(), 200.0) ||
      !almost_equal(rgb_channel_mixer_proof.value(QStringLiteral("redFromGreen")).toDouble(), -200.0) ||
      !almost_equal(rgb_channel_mixer_proof.value(QStringLiteral("redFromBlue")).toDouble(), 35.0) ||
      !almost_equal(rgb_channel_mixer_proof.value(QStringLiteral("greenFromRed")).toDouble(), 44.0) ||
      !almost_equal(rgb_channel_mixer_proof.value(QStringLiteral("greenFromGreen")).toDouble(), -200.0) ||
      !almost_equal(rgb_channel_mixer_proof.value(QStringLiteral("greenFromBlue")).toDouble(), 88.0) ||
      !almost_equal(rgb_channel_mixer_proof.value(QStringLiteral("blueFromRed")).toDouble(), -91.0) ||
      !almost_equal(rgb_channel_mixer_proof.value(QStringLiteral("blueFromGreen")).toDouble(), 120.0) ||
      !almost_equal(rgb_channel_mixer_proof.value(QStringLiteral("blueFromBlue")).toDouble(), 200.0) ||
      !almost_equal(rgb_mixer_controller.channelMixerRedFromRed(), 100.0) ||
      !almost_equal(rgb_mixer_controller.channelMixerRedFromGreen(), 0.0) ||
      !almost_equal(rgb_mixer_controller.channelMixerRedFromBlue(), 0.0) ||
      !almost_equal(rgb_mixer_controller.channelMixerGreenFromRed(), 0.0) ||
      !almost_equal(rgb_mixer_controller.channelMixerGreenFromGreen(), 100.0) ||
      !almost_equal(rgb_mixer_controller.channelMixerGreenFromBlue(), 0.0) ||
      !almost_equal(rgb_mixer_controller.channelMixerBlueFromRed(), 0.0) ||
      !almost_equal(rgb_mixer_controller.channelMixerBlueFromGreen(), 0.0) ||
      !almost_equal(rgb_mixer_controller.channelMixerBlueFromBlue(), 100.0)) {
    return fail("layer-local RGB channel mixer should clamp, persist, and not mutate global matrix controls");
  }
  if (!rgb_mixer_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("rgbMixer"))) {
    return fail("layer-local RGB channel mixer reset should remove the matrix object");
  }
  const auto rgb_mixer_reset_layer = rgb_mixer_controller.photoLayers().at(0).toMap();
  const auto rgb_mixer_reset_color =
      rgb_mixer_reset_layer.value(QStringLiteral("adjustments")).toMap()
          .value(QStringLiteral("colorMixer")).toMap();
  if (rgb_mixer_reset_color.contains(QStringLiteral("rgbChannelMixer")) ||
      rgb_mixer_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("rgbMixer"))) {
    return fail("layer-local RGB channel mixer reset should be exact and idempotent");
  }

  photo_editor::desktop::EditorPreviewController black_and_white_controller;
  black_and_white_controller.addMask(QStringLiteral("linear"));
  const QStringList black_and_white_channel_names = mixer_channels;
  if (black_and_white_controller.photoLayerCount() != 1 ||
      !black_and_white_controller.setPhotoLayerAdjustmentBool(
          0, QStringLiteral("blackAndWhiteEnabled"), true) ||
      !black_and_white_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("blackAndWhiteReds"), 140.0) ||
      !black_and_white_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("blackAndWhiteOranges"), -135.0) ||
      !black_and_white_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("blackAndWhiteYellows"), 72.0) ||
      !black_and_white_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("blackAndWhiteGreens"), -61.0) ||
      !black_and_white_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("blackAndWhiteAquas"), 48.0) ||
      !black_and_white_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("blackAndWhiteBlues"), -37.0) ||
      !black_and_white_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("blackAndWhitePurples"), 26.0) ||
      !black_and_white_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("blackAndWhiteMagentas"), -15.0)) {
    return fail("black and white mixer proof should create and edit all channel weights");
  }
  const auto black_and_white_layer = black_and_white_controller.photoLayers().at(0).toMap();
  const auto black_and_white_adjustments =
      required_map(black_and_white_layer, QStringLiteral("adjustments"));
  const auto black_and_white_color_mixer =
      required_map(black_and_white_adjustments, QStringLiteral("colorMixer"));
  const auto black_and_white_proof =
      required_map(black_and_white_color_mixer, QStringLiteral("blackAndWhite"));
  const auto black_and_white_proof_channels =
      required_map(black_and_white_proof, QStringLiteral("channels"));
  const std::array<double, 8> black_and_white_expected = {
      100.0, -100.0, 72.0, -61.0, 48.0, -37.0, 26.0, -15.0,
  };
  if (!black_and_white_proof.value(QStringLiteral("enabled")).toBool()) {
    return fail("layer-local black and white mixer should persist enabled state");
  }
  for (int channel_index = 0; channel_index < black_and_white_channel_names.size(); ++channel_index) {
    if (!almost_equal(
            black_and_white_proof_channels.value(black_and_white_channel_names.at(channel_index)).toDouble(),
            black_and_white_expected.at(static_cast<std::size_t>(channel_index)))) {
      return fail("layer-local black and white mixer should persist every clamped channel weight");
    }
  }
  if (black_and_white_controller.blackAndWhiteEnabled() ||
      !almost_equal(black_and_white_controller.blackAndWhiteReds(), 0.0) ||
      !almost_equal(black_and_white_controller.blackAndWhiteOranges(), 0.0) ||
      !almost_equal(black_and_white_controller.blackAndWhiteYellows(), 0.0) ||
      !almost_equal(black_and_white_controller.blackAndWhiteGreens(), 0.0) ||
      !almost_equal(black_and_white_controller.blackAndWhiteAquas(), 0.0) ||
      !almost_equal(black_and_white_controller.blackAndWhiteBlues(), 0.0) ||
      !almost_equal(black_and_white_controller.blackAndWhitePurples(), 0.0) ||
      !almost_equal(black_and_white_controller.blackAndWhiteMagentas(), 0.0)) {
    return fail("layer-local black and white mixer edits should not mutate global mixer controls");
  }
  if (!black_and_white_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("blackAndWhite"))) {
    return fail("layer-local black and white mixer reset should remove the mixer object");
  }
  const auto black_and_white_reset_layer = black_and_white_controller.photoLayers().at(0).toMap();
  const auto black_and_white_reset_color =
      black_and_white_reset_layer.value(QStringLiteral("adjustments")).toMap()
          .value(QStringLiteral("colorMixer")).toMap();
  if (black_and_white_reset_color.contains(QStringLiteral("blackAndWhite")) ||
      black_and_white_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("blackAndWhite"))) {
    return fail("layer-local black and white mixer reset should be exact and idempotent");
  }

  photo_editor::desktop::EditorPreviewController color_grading_controller;
  color_grading_controller.addMask(QStringLiteral("linear"));
  if (color_grading_controller.photoLayerCount() != 1 ||
      !color_grading_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("gradingShadowsHue"), -0.20) ||
      !color_grading_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("gradingShadowsSaturation"), 140.0) ||
      !color_grading_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("gradingShadowsLuminance"), -140.0) ||
      !color_grading_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("gradingMidtonesHue"), 1.35) ||
      !color_grading_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("gradingMidtonesSaturation"), -64.0) ||
      !color_grading_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("gradingMidtonesLuminance"), 52.0) ||
      !color_grading_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("gradingHighlightsHue"), 0.62) ||
      !color_grading_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("gradingHighlightsSaturation"), 75.0) ||
      !color_grading_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("gradingHighlightsLuminance"), -43.0) ||
      !color_grading_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("gradingAmount"), 128.0) ||
      !color_grading_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("gradingBalance"), -155.0)) {
    return fail("color grading proof should create and edit all layer-local grading controls");
  }
  const auto color_grading_layer = color_grading_controller.photoLayers().at(0).toMap();
  const auto color_grading_adjustments =
      required_map(color_grading_layer, QStringLiteral("adjustments"));
  const auto color_grading = required_map(color_grading_adjustments, QStringLiteral("colorGrading"));
  const auto grading_shadows = required_map(color_grading, QStringLiteral("shadows"));
  const auto grading_midtones = required_map(color_grading, QStringLiteral("midtones"));
  const auto color_grading_highlights = required_map(color_grading, QStringLiteral("highlights"));
  if (!almost_equal(grading_shadows.value(QStringLiteral("hue")).toDouble(), 0.80) ||
      !almost_equal(grading_shadows.value(QStringLiteral("saturation")).toDouble(), 100.0) ||
      !almost_equal(grading_shadows.value(QStringLiteral("luminance")).toDouble(), -100.0) ||
      !almost_equal(grading_midtones.value(QStringLiteral("hue")).toDouble(), 0.35) ||
      !almost_equal(grading_midtones.value(QStringLiteral("saturation")).toDouble(), -64.0) ||
      !almost_equal(grading_midtones.value(QStringLiteral("luminance")).toDouble(), 52.0) ||
      !almost_equal(color_grading_highlights.value(QStringLiteral("hue")).toDouble(), 0.62) ||
      !almost_equal(color_grading_highlights.value(QStringLiteral("saturation")).toDouble(), 75.0) ||
      !almost_equal(color_grading_highlights.value(QStringLiteral("luminance")).toDouble(), -43.0) ||
      !almost_equal(color_grading.value(QStringLiteral("amount")).toDouble(), 100.0) ||
      !almost_equal(color_grading.value(QStringLiteral("balance")).toDouble(), -100.0) ||
      !almost_equal(color_grading_controller.gradingShadowsHue(), 0.0) ||
      !almost_equal(color_grading_controller.gradingShadowsSaturation(), 0.0) ||
      !almost_equal(color_grading_controller.gradingShadowsLuminance(), 0.0) ||
      !almost_equal(color_grading_controller.gradingMidtonesHue(), 0.0) ||
      !almost_equal(color_grading_controller.gradingMidtonesSaturation(), 0.0) ||
      !almost_equal(color_grading_controller.gradingMidtonesLuminance(), 0.0) ||
      !almost_equal(color_grading_controller.gradingHighlightsHue(), 0.0) ||
      !almost_equal(color_grading_controller.gradingHighlightsSaturation(), 0.0) ||
      !almost_equal(color_grading_controller.gradingHighlightsLuminance(), 0.0) ||
      !almost_equal(color_grading_controller.gradingAmount(), 100.0) ||
      !almost_equal(color_grading_controller.gradingBalance(), 0.0)) {
    return fail("layer-local color grading should clamp, wrap, persist, and not mutate global grading controls");
  }
  if (!color_grading_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("colorGrading"))) {
    return fail("layer-local color grading reset should remove the grading object");
  }
  const auto color_grading_reset_layer = color_grading_controller.photoLayers().at(0).toMap();
  if (color_grading_reset_layer.value(QStringLiteral("adjustments")).toMap().contains(QStringLiteral("colorGrading")) ||
      color_grading_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("colorGrading"))) {
    return fail("layer-local color grading reset should be exact and idempotent");
  }

  photo_editor::desktop::EditorPreviewController replacement_skin_harmony_controller;
  replacement_skin_harmony_controller.addMask(QStringLiteral("linear"));
  if (replacement_skin_harmony_controller.photoLayerCount() != 1 ||
      !replacement_skin_harmony_controller.setPhotoLayerAdjustmentBool(
          0, QStringLiteral("colorReplacementEnabled"), true) ||
      !replacement_skin_harmony_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("colorReplacementSourceHue"), -0.20) ||
      !replacement_skin_harmony_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("colorReplacementTargetHue"), 1.34) ||
      !replacement_skin_harmony_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("colorReplacementRange"), 140.0) ||
      !replacement_skin_harmony_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("colorReplacementSoftness"), -12.0) ||
      !replacement_skin_harmony_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("colorReplacementStrength"), 128.0) ||
      !replacement_skin_harmony_controller.setPhotoLayerAdjustmentBool(
          0, QStringLiteral("colorReplacementPreserveLuminance"), false) ||
      !replacement_skin_harmony_controller.setPhotoLayerAdjustmentBool(
          0, QStringLiteral("colorReplacementPreserveSaturation"), false) ||
      !replacement_skin_harmony_controller.setPhotoLayerAdjustmentBool(
          0, QStringLiteral("skinToneProtectionEnabled"), true) ||
      !replacement_skin_harmony_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("skinToneLineHue"), 1.27) ||
      !replacement_skin_harmony_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("skinToneProtectionRange"), -10.0) ||
      !replacement_skin_harmony_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("skinToneProtectionStrength"), 130.0) ||
      !replacement_skin_harmony_controller.setPhotoLayerAdjustmentBool(
          0, QStringLiteral("colorHarmonyEnabled"), true) ||
      !replacement_skin_harmony_controller.setPhotoLayerAdjustmentString(
          0, QStringLiteral("colorHarmonyMode"), QStringLiteral("Analogous")) ||
      !replacement_skin_harmony_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("colorHarmonyBaseHue"), -0.40) ||
      !replacement_skin_harmony_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("colorHarmonyStrength"), 130.0)) {
    return fail("replacement, skin protection, and harmony proof should edit every layer-local control");
  }
  const auto replacement_skin_harmony_layer =
      replacement_skin_harmony_controller.photoLayers().at(0).toMap();
  const auto replacement_skin_harmony_color =
      required_map(required_map(replacement_skin_harmony_layer, QStringLiteral("adjustments")),
                   QStringLiteral("colorMixer"));
  const auto replacement_proof =
      required_map(replacement_skin_harmony_color, QStringLiteral("colorReplacement"));
  const auto skin_proof =
      required_map(replacement_skin_harmony_color, QStringLiteral("skinToneProtection"));
  const auto harmony_proof =
      required_map(replacement_skin_harmony_color, QStringLiteral("colorHarmony"));
  if (!replacement_proof.value(QStringLiteral("enabled")).toBool() ||
      !almost_equal(replacement_proof.value(QStringLiteral("sourceHue")).toDouble(), 0.80) ||
      !almost_equal(replacement_proof.value(QStringLiteral("targetHue")).toDouble(), 0.34) ||
      !almost_equal(replacement_proof.value(QStringLiteral("range")).toDouble(), 100.0) ||
      !almost_equal(replacement_proof.value(QStringLiteral("softness")).toDouble(), 0.0) ||
      !almost_equal(replacement_proof.value(QStringLiteral("strength")).toDouble(), 100.0) ||
      replacement_proof.value(QStringLiteral("preserveLuminance")).toBool() ||
      replacement_proof.value(QStringLiteral("preserveSaturation")).toBool() ||
      !skin_proof.value(QStringLiteral("enabled")).toBool() ||
      !almost_equal(skin_proof.value(QStringLiteral("lineHue")).toDouble(), 0.27) ||
      !almost_equal(skin_proof.value(QStringLiteral("range")).toDouble(), 1.0) ||
      !almost_equal(skin_proof.value(QStringLiteral("strength")).toDouble(), 100.0) ||
      !harmony_proof.value(QStringLiteral("enabled")).toBool() ||
      harmony_proof.value(QStringLiteral("mode")).toString() != QStringLiteral("Analogous") ||
      !almost_equal(harmony_proof.value(QStringLiteral("baseHue")).toDouble(), 0.60) ||
      !almost_equal(harmony_proof.value(QStringLiteral("strength")).toDouble(), 100.0) ||
      replacement_skin_harmony_controller.colorReplacementEnabled() ||
      !almost_equal(replacement_skin_harmony_controller.colorReplacementSourceHue(), 0.0) ||
      !almost_equal(replacement_skin_harmony_controller.colorReplacementTargetHue(), 0.33) ||
      !almost_equal(replacement_skin_harmony_controller.colorReplacementRange(), 35.0) ||
      !almost_equal(replacement_skin_harmony_controller.colorReplacementSoftness(), 100.0) ||
      !almost_equal(replacement_skin_harmony_controller.colorReplacementStrength(), 0.0) ||
      !replacement_skin_harmony_controller.colorReplacementPreserveLuminance() ||
      !replacement_skin_harmony_controller.colorReplacementPreserveSaturation() ||
      replacement_skin_harmony_controller.skinToneProtectionEnabled() ||
      !almost_equal(replacement_skin_harmony_controller.skinToneLineHue(), 0.08) ||
      !almost_equal(replacement_skin_harmony_controller.skinToneProtectionRange(), 30.0) ||
      !almost_equal(replacement_skin_harmony_controller.skinToneProtectionStrength(), 0.0) ||
      replacement_skin_harmony_controller.colorHarmonyEnabled() ||
      replacement_skin_harmony_controller.colorHarmonyMode() != QStringLiteral("Complementary") ||
      !almost_equal(replacement_skin_harmony_controller.colorHarmonyBaseHue(), 0.0) ||
      !almost_equal(replacement_skin_harmony_controller.colorHarmonyStrength(), 0.0)) {
    return fail("replacement, skin protection, and harmony should clamp, wrap, persist, and not mutate global controls");
  }
  if (!replacement_skin_harmony_controller.resetPhotoLayerAdjustmentGroup(
          0, QStringLiteral("colorReplacement")) ||
      !replacement_skin_harmony_controller.resetPhotoLayerAdjustmentGroup(
          0, QStringLiteral("skinToneProtection")) ||
      !replacement_skin_harmony_controller.resetPhotoLayerAdjustmentGroup(
          0, QStringLiteral("colorHarmony"))) {
    return fail("replacement, skin protection, and harmony resets should remove nested layer-local objects");
  }
  const auto replacement_skin_harmony_reset_layer =
      replacement_skin_harmony_controller.photoLayers().at(0).toMap();
  const auto replacement_skin_harmony_reset_color =
      replacement_skin_harmony_reset_layer.value(QStringLiteral("adjustments")).toMap()
          .value(QStringLiteral("colorMixer")).toMap();
  if (replacement_skin_harmony_reset_color.contains(QStringLiteral("colorReplacement")) ||
      replacement_skin_harmony_reset_color.contains(QStringLiteral("skinToneProtection")) ||
      replacement_skin_harmony_reset_color.contains(QStringLiteral("colorHarmony")) ||
      replacement_skin_harmony_controller.resetPhotoLayerAdjustmentGroup(
          0, QStringLiteral("colorReplacement")) ||
      replacement_skin_harmony_controller.resetPhotoLayerAdjustmentGroup(
          0, QStringLiteral("skinToneProtection")) ||
      replacement_skin_harmony_controller.resetPhotoLayerAdjustmentGroup(
          0, QStringLiteral("colorHarmony"))) {
    return fail("replacement, skin protection, and harmony resets should be exact and idempotent");
  }

  photo_editor::desktop::EditorPreviewController lut_stack_controller;
  lut_stack_controller.addMask(QStringLiteral("linear"));
  QVariantList lut_stack_table;
  for (int sample_index = 0; sample_index < 8; ++sample_index) {
    lut_stack_table.append(QVariant(QVariantList{1.25, -0.25, 0.50, 1.25}));
  }
  QVariantMap first_lut_stack_entry;
  first_lut_stack_entry.insert(QStringLiteral("enabled"), true);
  first_lut_stack_entry.insert(QStringLiteral("strength"), 150.0);
  first_lut_stack_entry.insert(QStringLiteral("name"), QStringLiteral("Signed HDR Layer LUT"));
  first_lut_stack_entry.insert(QStringLiteral("size"), 999);
  first_lut_stack_entry.insert(QStringLiteral("table"), lut_stack_table);
  first_lut_stack_entry.insert(QStringLiteral("domainMin"), QVariantList{0.0, 0.0, 0.0});
  first_lut_stack_entry.insert(QStringLiteral("domainMax"), QVariantList{1.0, 1.0, 1.0});
  first_lut_stack_entry.insert(QStringLiteral("inputColorSpace"), QStringLiteral("sRGB"));
  first_lut_stack_entry.insert(QStringLiteral("outputColorSpace"), QStringLiteral("sRGB"));
  QVariantMap second_lut_stack_entry = first_lut_stack_entry;
  second_lut_stack_entry.insert(QStringLiteral("enabled"), false);
  second_lut_stack_entry.insert(QStringLiteral("strength"), -25.0);
  second_lut_stack_entry.insert(QStringLiteral("name"), QStringLiteral("Disabled Layer LUT"));
  second_lut_stack_entry.insert(QStringLiteral("size"), 2);
  const QVariantList raw_lut_stack{first_lut_stack_entry, second_lut_stack_entry};
  if (lut_stack_controller.photoLayerCount() != 1 ||
      !lut_stack_controller.setPhotoLayerLutStack(0, raw_lut_stack)) {
    return fail("layer-local LUT stack proof should accept a stack on adjustment layers");
  }
  const auto lut_stack_layer = lut_stack_controller.photoLayers().at(0).toMap();
  const auto persisted_lut_stack =
      required_map(lut_stack_layer, QStringLiteral("adjustments"))
          .value(QStringLiteral("lutStack")).toList();
  if (persisted_lut_stack.size() != 2) {
    return fail("layer-local LUT stack should persist all stack entries under adjustments");
  }
  const auto persisted_first_lut = persisted_lut_stack.at(0).toMap();
  const auto persisted_second_lut = persisted_lut_stack.at(1).toMap();
  const auto persisted_first_lut_table = persisted_first_lut.value(QStringLiteral("table")).toList();
  const auto persisted_first_lut_sample = persisted_first_lut_table.value(0).toMap();
  if (!persisted_first_lut.value(QStringLiteral("enabled")).toBool()) {
    return fail("layer-local LUT stack should persist enabled state");
  }
  if (!almost_equal(persisted_first_lut.value(QStringLiteral("strength")).toDouble(), 100.0)) {
    return fail("layer-local LUT stack should clamp over-range strength");
  }
  if (persisted_first_lut.value(QStringLiteral("name")).toString() != QStringLiteral("Signed HDR Layer LUT")) {
    return fail("layer-local LUT stack should persist LUT names");
  }
  if (persisted_first_lut.value(QStringLiteral("size")).toInt() != 256) {
    return fail("layer-local LUT stack should clamp over-range LUT size");
  }
  if (persisted_first_lut_table.size() != 8) {
    return fail("layer-local LUT stack should persist normalized table rows");
  }
  // LUT strength and dimensions are bounded above; table channels carry
  // scene-linear values and must retain negative/HDR range in the recipe.
  if (!almost_equal(persisted_first_lut_sample.value(QStringLiteral("red")).toDouble(), 1.25) ||
      !almost_equal(persisted_first_lut_sample.value(QStringLiteral("green")).toDouble(), -0.25) ||
      !almost_equal(persisted_first_lut_sample.value(QStringLiteral("blue")).toDouble(), 0.50) ||
      !almost_equal(persisted_first_lut_sample.value(QStringLiteral("alpha")).toDouble(), 1.25)) {
    return fail("layer-local LUT stack should preserve signed/HDR RGB and table alpha values");
  }

  photo_editor::pipeline::ColorAdjustmentState extended_layer_lut_state;
  extended_layer_lut_state.color_pipeline.working_profile.profile_name = "Linear sRGB";
  extended_layer_lut_state.color_pipeline.working_profile.color_space =
      photo_editor::pipeline::make_linear_srgb_color_space();
  extended_layer_lut_state.color_pipeline.output_headroom = 4.0;
  photo_editor::pipeline::LutAdjustment extended_layer_lut;
  extended_layer_lut.enabled = true;
  extended_layer_lut.strength = 100.0;
  extended_layer_lut.name = "Signed HDR layer LUT";
  extended_layer_lut.size = 2;
  extended_layer_lut.input_color_space = "Linear sRGB";
  extended_layer_lut.output_color_space = "Linear sRGB";
  extended_layer_lut.table.assign(
      8U,
      photo_editor::pipeline::ColorPixel{
          .red = 1.25F,
          .green = -0.25F,
          .blue = 0.50F,
          .alpha = 1.25F,
      });
  extended_layer_lut_state.photo_layers = {
      photo_editor::pipeline::PhotoLayer{
          .id = "signed-hdr-layer-lut",
          .name = "Signed HDR layer LUT",
          .kind = "adjustment",
          .enabled = true,
          .opacity = 1.0,
          .lut_stack = {extended_layer_lut},
      },
  };
  std::vector<photo_editor::pipeline::ColorPixel> extended_layer_pixels{
      {0.23F, 0.34F, 0.45F, 0.37F},
  };
  if (!photo_editor::pipeline::apply_color_layers_stage(
          &extended_layer_pixels,
          1,
          1,
          extended_layer_lut_state,
          photo_editor::pipeline::RemovalQuality::Final) ||
      extended_layer_pixels.size() != 1U ||
      !almost_equal(extended_layer_pixels.front().red, 1.25, 1.0e-5) ||
      !almost_equal(extended_layer_pixels.front().green, -0.25, 1.0e-5) ||
      !almost_equal(extended_layer_pixels.front().blue, 0.50, 1.0e-5) ||
      !almost_equal(extended_layer_pixels.front().alpha, 0.37, 1.0e-6)) {
    return fail("native adjustment-layer LUT rendering should preserve signed/HDR RGB and source alpha");
  }
  if (persisted_second_lut.value(QStringLiteral("enabled")).toBool()) {
    return fail("layer-local LUT stack should persist disabled entries");
  }
  if (!almost_equal(persisted_second_lut.value(QStringLiteral("strength")).toDouble(), 0.0)) {
    return fail("layer-local LUT stack should clamp under-range strength");
  }
  if (persisted_second_lut.value(QStringLiteral("size")).toInt() != 2) {
    return fail("layer-local LUT stack should persist valid LUT size");
  }
  if (lut_stack_controller.setPhotoLayerLutStack(0, raw_lut_stack)) {
    return fail("layer-local LUT stack should report identical writes as no-op");
  }
  if (!lut_stack_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("lut"))) {
    return fail("layer-local LUT reset should remove LUT stack adjustments");
  }
  const auto lut_stack_reset_layer = lut_stack_controller.photoLayers().at(0).toMap();
  const auto lut_stack_reset_adjustments =
      lut_stack_reset_layer.value(QStringLiteral("adjustments")).toMap();
  if (lut_stack_reset_adjustments.contains(QStringLiteral("lutStack")) ||
      lut_stack_reset_adjustments.contains(QStringLiteral("lut")) ||
      lut_stack_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("lut"))) {
    return fail("layer-local LUT reset should be exact and idempotent");
  }

  photo_editor::desktop::EditorPreviewController advanced_tone_edge_controller;
  advanced_tone_edge_controller.addMask(QStringLiteral("linear"));
  if (advanced_tone_edge_controller.photoLayerCount() != 1 ||
      !advanced_tone_edge_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("brightness"), 140.0) ||
      !advanced_tone_edge_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("filmicShoulder"), -140.0) ||
      !advanced_tone_edge_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("filmicToe"), 140.0) ||
      !advanced_tone_edge_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("adaptiveContrast"), 240.0) ||
      !advanced_tone_edge_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("levelsBlackPoint"), 12.0) ||
      !advanced_tone_edge_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("levelsBlackPoint"), -12.0) ||
      !advanced_tone_edge_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("levelsGamma"), 160.0) ||
      !advanced_tone_edge_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("levelsWhitePoint"), 130.0) ||
      !advanced_tone_edge_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("zoneDeepShadows"), -180.0) ||
      !advanced_tone_edge_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("zoneShadows"), 180.0) ||
      !advanced_tone_edge_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("zoneMidtones"), -180.0) ||
      !advanced_tone_edge_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("zoneLights"), 180.0) ||
      !advanced_tone_edge_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("zoneHighlights"), -180.0)) {
    return fail("advanced layer-local tone edge proof should create and edit a selection layer");
  }
  const auto advanced_tone_edge_layer =
      advanced_tone_edge_controller.photoLayers().at(0).toMap();
  if (!almost_equal(advanced_tone_edge_layer.value(QStringLiteral("brightness")).toDouble(), 100.0) ||
      !almost_equal(advanced_tone_edge_layer.value(QStringLiteral("filmicShoulder")).toDouble(), -100.0) ||
      !almost_equal(advanced_tone_edge_layer.value(QStringLiteral("filmicToe")).toDouble(), 100.0) ||
      !almost_equal(advanced_tone_edge_layer.value(QStringLiteral("adaptiveContrast")).toDouble(), 100.0) ||
      !almost_equal(advanced_tone_edge_layer.value(QStringLiteral("levelsBlackPoint")).toDouble(), 0.0) ||
      !almost_equal(advanced_tone_edge_layer.value(QStringLiteral("levelsGamma")).toDouble(), 100.0) ||
      !almost_equal(advanced_tone_edge_layer.value(QStringLiteral("levelsWhitePoint")).toDouble(), 100.0) ||
      !almost_equal(advanced_tone_edge_layer.value(QStringLiteral("zoneDeepShadows")).toDouble(), -100.0) ||
      !almost_equal(advanced_tone_edge_layer.value(QStringLiteral("zoneShadows")).toDouble(), 100.0) ||
      !almost_equal(advanced_tone_edge_layer.value(QStringLiteral("zoneMidtones")).toDouble(), -100.0) ||
      !almost_equal(advanced_tone_edge_layer.value(QStringLiteral("zoneLights")).toDouble(), 100.0) ||
      !almost_equal(advanced_tone_edge_layer.value(QStringLiteral("zoneHighlights")).toDouble(), -100.0)) {
    return fail("advanced layer-local tone values should clamp to safe layer ranges");
  }
  if (!advanced_tone_edge_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("light"))) {
    return fail("advanced layer-local tone reset should remove edited light controls");
  }
  const auto advanced_tone_reset_layer =
      advanced_tone_edge_controller.photoLayers().at(0).toMap();
  if (advanced_tone_reset_layer.contains(QStringLiteral("brightness")) ||
      advanced_tone_reset_layer.contains(QStringLiteral("filmicShoulder")) ||
      advanced_tone_reset_layer.contains(QStringLiteral("filmicToe")) ||
      advanced_tone_reset_layer.contains(QStringLiteral("adaptiveContrast")) ||
      advanced_tone_reset_layer.contains(QStringLiteral("levelsBlackPoint")) ||
      advanced_tone_reset_layer.contains(QStringLiteral("levelsGamma")) ||
      advanced_tone_reset_layer.contains(QStringLiteral("levelsWhitePoint")) ||
      advanced_tone_reset_layer.contains(QStringLiteral("zoneDeepShadows")) ||
      advanced_tone_reset_layer.contains(QStringLiteral("zoneShadows")) ||
      advanced_tone_reset_layer.contains(QStringLiteral("zoneMidtones")) ||
      advanced_tone_reset_layer.contains(QStringLiteral("zoneLights")) ||
      advanced_tone_reset_layer.contains(QStringLiteral("zoneHighlights")) ||
      advanced_tone_edge_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("light"))) {
    return fail("advanced layer-local tone reset should be exact and idempotent");
  }

  if (!controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("colorGrading"))) {
    return fail("layer-local color grading reset should clear only the grading object");
  }
  const auto grading_reset_layer = controller.photoLayers().at(0).toMap();
  const auto grading_reset_adjustments =
      required_map(grading_reset_layer, QStringLiteral("adjustments"));
  if (grading_reset_adjustments.contains(QStringLiteral("colorGrading")) ||
      !grading_reset_adjustments.contains(QStringLiteral("colorMixer")) ||
      !almost_equal(grading_reset_layer.value(QStringLiteral("exposure")).toDouble(), 0.72) ||
      !almost_equal(grading_reset_layer.value(QStringLiteral("vibrance")).toDouble(), 33.0)) {
    return fail("layer-local color grading reset should preserve other layer adjustments");
  }

  if (!controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("color"))) {
    return fail("layer-local color reset should clear the full active layer color recipe");
  }
  const auto color_reset_layer = controller.photoLayers().at(0).toMap();
  const auto color_reset_adjustments = color_reset_layer.value(QStringLiteral("adjustments")).toMap();
  if (color_reset_layer.contains(QStringLiteral("vibrance")) ||
      color_reset_layer.contains(QStringLiteral("saturation")) ||
      color_reset_adjustments.contains(QStringLiteral("colorMixer")) ||
      color_reset_adjustments.contains(QStringLiteral("colorGrading")) ||
      !almost_equal(color_reset_layer.value(QStringLiteral("exposure")).toDouble(), 0.72)) {
    return fail("layer-local color reset should not leave nested color edits or touch light edits");
  }
  if (controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("color"))) {
    return fail("neutral layer-local color reset should report no-op");
  }
  if (!controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("texture"), 44.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("clarity"), -28.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("dehaze"), 37.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("sharpening"), 180.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("sharpenRadius"), 9.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("sharpenDetail"), -140.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("sharpenMasking"), 120.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("luminanceNr"), 62.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("colorNr"), 48.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("lensDistortion"), -37.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("lensVignette"), 29.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("chromaticAberration"), 130.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("perspectiveVertical"), 55.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("perspectiveHorizontal"), -66.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("transformRotation"), 60.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("transformScale"), 0.05) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("transformSkewX"), 70.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("transformSkewY"), -80.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("liquifyCenterX"), 1.4) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("liquifyCenterY"), -0.4) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("liquifyRadius"), 2.0) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("liquifyStrength"), -120.0) ||
      !controller.setPhotoLayerAdjustmentBool(0, QStringLiteral("contentAwareCropFill"), true)) {
    return fail("layer-local complete recipe should accept detail, effects, transform, and liquify fields");
  }
  const auto complete_recipe_layer = controller.photoLayers().at(0).toMap();
  if (!almost_equal(complete_recipe_layer.value(QStringLiteral("texture")).toDouble(), 44.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("clarity")).toDouble(), -28.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("dehaze")).toDouble(), 37.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("sharpening")).toDouble(), 150.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("sharpenRadius")).toDouble(), 5.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("sharpenDetail")).toDouble(), -100.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("sharpenMasking")).toDouble(), 100.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("luminanceNr")).toDouble(), 62.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("colorNr")).toDouble(), 48.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("lensDistortion")).toDouble(), -37.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("lensVignette")).toDouble(), 29.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("chromaticAberration")).toDouble(), 100.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("perspectiveVertical")).toDouble(), 55.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("perspectiveHorizontal")).toDouble(), -66.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("transformRotation")).toDouble(), 45.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("transformScale")).toDouble(), 0.25) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("transformSkewX")).toDouble(), 70.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("transformSkewY")).toDouble(), -80.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("liquifyCenterX")).toDouble(), 1.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("liquifyCenterY")).toDouble(), 0.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("liquifyRadius")).toDouble(), 1.0) ||
      !almost_equal(complete_recipe_layer.value(QStringLiteral("liquifyStrength")).toDouble(), -100.0) ||
      !complete_recipe_layer.value(QStringLiteral("contentAwareCropFill")).toBool()) {
    return fail("layer-local complete recipe fields should clamp and persist on the adjustment layer");
  }
  if (!controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("temperature"), 18.0) ||
      !controller.setPhotoLayerAdjustmentString(0, QStringLiteral("colorMixerModel"), QStringLiteral("HSL")) ||
      !controller.setPhotoLayerAdjustmentString(0, QStringLiteral("hslChannel"), QStringLiteral("Reds")) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("hueAdjustment"), -24.0) ||
      !controller.copyPhotoLayerAdjustments(0) ||
      !controller.hasCopiedPhotoLayerAdjustments()) {
    return fail("adjustment layers should copy their layer-local adjustment recipe");
  }
  const auto copied_source_layer = controller.photoLayers().at(0).toMap();
  const auto copied_source_id = copied_source_layer.value(QStringLiteral("id")).toString();
  if (!controller.addAdjustmentLayer(QStringLiteral("Paste Target")) || controller.photoLayerCount() != 2) {
    return fail("test should create a second adjustment layer paste target");
  }
  const auto paste_target_before = controller.photoLayers().at(0).toMap();
  const auto paste_target_id = paste_target_before.value(QStringLiteral("id")).toString();
  if (paste_target_id == copied_source_id ||
      !controller.setPhotoLayerOpacity(0, 0.37) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), -1.0) ||
      !controller.pastePhotoLayerAdjustments(0)) {
    return fail("adjustment layers should paste copied recipes onto another adjustment layer");
  }
  const auto pasted_layer = controller.photoLayers().at(0).toMap();
  const auto pasted_adjustments = required_map(pasted_layer, QStringLiteral("adjustments"));
  const auto pasted_color_mixer = required_map(pasted_adjustments, QStringLiteral("colorMixer"));
  const auto pasted_channels = required_map(pasted_color_mixer, QStringLiteral("channels"));
  const auto pasted_red_channel = required_map(pasted_channels, QStringLiteral("Reds"));
  if (pasted_layer.value(QStringLiteral("id")).toString() != paste_target_id ||
      !almost_equal(pasted_layer.value(QStringLiteral("opacity")).toDouble(), 0.37) ||
      !almost_equal(pasted_layer.value(QStringLiteral("exposure")).toDouble(), 0.72) ||
      !almost_equal(pasted_layer.value(QStringLiteral("temperature")).toDouble(), 18.0) ||
      !almost_equal(pasted_layer.value(QStringLiteral("texture")).toDouble(), 44.0) ||
      !almost_equal(pasted_layer.value(QStringLiteral("sharpening")).toDouble(), 150.0) ||
      !almost_equal(pasted_layer.value(QStringLiteral("lensDistortion")).toDouble(), -37.0) ||
      !almost_equal(pasted_layer.value(QStringLiteral("transformRotation")).toDouble(), 45.0) ||
      !almost_equal(pasted_layer.value(QStringLiteral("liquifyStrength")).toDouble(), -100.0) ||
      !pasted_layer.value(QStringLiteral("contentAwareCropFill")).toBool() ||
      pasted_color_mixer.value(QStringLiteral("model")).toString() != QStringLiteral("HSL") ||
      pasted_color_mixer.value(QStringLiteral("channel")).toString() != QStringLiteral("Reds") ||
      !almost_equal(pasted_red_channel.value(QStringLiteral("hue")).toDouble(), -24.0)) {
    return fail("pasted layer-local recipe should replace adjustments without replacing layer structure");
  }
  if (controller.pastePhotoLayerAdjustments(0)) {
    return fail("pasting an identical layer-local recipe should report no-op");
  }
  if (controller.copyPhotoLayerAdjustments(-1) ||
      controller.pastePhotoLayerAdjustments(-1) ||
      controller.addFillLayer(QStringLiteral("#204060"), QStringLiteral("Fill Target")) != true ||
      controller.copyPhotoLayerAdjustments(0) ||
      controller.pastePhotoLayerAdjustments(0) ||
      controller.pasteGlobalAdjustmentsToPhotoLayer(0)) {
    return fail("layer-local copy/paste should reject invalid or non-adjustment targets");
  }

  if (!controller.addAdjustmentLayer(QStringLiteral("Sync Target A")) ||
      !controller.addAdjustmentLayer(QStringLiteral("Sync Target B")) ||
      controller.photoLayerCount() != 5) {
    return fail("test should create adjustment layer sync targets");
  }
  const auto sync_target_b_before = controller.photoLayers().at(0).toMap();
  const auto sync_target_a_before = controller.photoLayers().at(1).toMap();
  const auto sync_source_before = controller.photoLayers().at(4).toMap();
  const auto sync_target_b_id = sync_target_b_before.value(QStringLiteral("id")).toString();
  const auto sync_target_a_id = sync_target_a_before.value(QStringLiteral("id")).toString();
  const auto sync_source_id = sync_source_before.value(QStringLiteral("id")).toString();
  if (sync_target_b_id == sync_source_id || sync_target_a_id == sync_source_id ||
      !controller.setPhotoLayerOpacity(0, 0.62) ||
      !controller.setPhotoLayerOpacity(1, 0.41) ||
      !controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), -0.5) ||
      !controller.setPhotoLayerAdjustmentValue(1, QStringLiteral("temperature"), -12.0) ||
      !controller.setPhotoLayerLocked(3, true)) {
    return fail("test should prepare distinct sync target structure and locked skip target");
  }

  QVariantList sync_targets;
  sync_targets << 0 << 1 << 2 << 3 << 4 << 1 << -1 << 999;
  if (!controller.syncPhotoLayerAdjustments(4, sync_targets)) {
    return fail("adjustment layer sync should copy source recipe to valid selected adjustment layers");
  }
  const auto synced_target_b = controller.photoLayers().at(0).toMap();
  const auto synced_target_a = controller.photoLayers().at(1).toMap();
  const auto skipped_fill = controller.photoLayers().at(2).toMap();
  const auto skipped_locked = controller.photoLayers().at(3).toMap();
  const auto sync_source_after = controller.photoLayers().at(4).toMap();
  const auto synced_a_adjustments = required_map(synced_target_a, QStringLiteral("adjustments"));
  const auto synced_a_color_mixer = required_map(synced_a_adjustments, QStringLiteral("colorMixer"));
  const auto synced_a_channels = required_map(synced_a_color_mixer, QStringLiteral("channels"));
  const auto synced_a_red_channel = required_map(synced_a_channels, QStringLiteral("Reds"));
  if (synced_target_b.value(QStringLiteral("id")).toString() != sync_target_b_id ||
      synced_target_a.value(QStringLiteral("id")).toString() != sync_target_a_id ||
      sync_source_after.value(QStringLiteral("id")).toString() != sync_source_id ||
      !almost_equal(synced_target_b.value(QStringLiteral("opacity")).toDouble(), 0.62) ||
      !almost_equal(synced_target_a.value(QStringLiteral("opacity")).toDouble(), 0.41) ||
      !almost_equal(synced_target_b.value(QStringLiteral("exposure")).toDouble(), 0.72) ||
      !almost_equal(synced_target_a.value(QStringLiteral("temperature")).toDouble(), 18.0) ||
      synced_a_color_mixer.value(QStringLiteral("model")).toString() != QStringLiteral("HSL") ||
      !almost_equal(synced_a_red_channel.value(QStringLiteral("hue")).toDouble(), -24.0) ||
      skipped_fill.value(QStringLiteral("kind")).toString() != QStringLiteral("fill") ||
      !almost_equal(skipped_locked.value(QStringLiteral("opacity")).toDouble(), 0.37) ||
      !skipped_locked.value(QStringLiteral("locked")).toBool()) {
    return fail("synced recipes should update selected adjustment layers while preserving structure and skipping invalid targets");
  }
  if (controller.syncPhotoLayerAdjustments(4, sync_targets) ||
      controller.syncPhotoLayerAdjustments(-1, sync_targets) ||
      controller.syncPhotoLayerAdjustments(2, sync_targets) ||
      controller.syncPhotoLayerAdjustments(4, QVariantList{})) {
    return fail("sync should report no-op or reject invalid sources and empty selections");
  }

  photo_editor::desktop::EditorPreviewController paste_global_controller;
  paste_global_controller.setExposure(0.58);
  paste_global_controller.setTemperature(-22.0);
  paste_global_controller.setTexture(31.0);
  paste_global_controller.setCurveShadows(19.0);
  paste_global_controller.setColorMixerModel(QStringLiteral("HSV"));
  paste_global_controller.setHslChannel(QStringLiteral("Blues"));
  paste_global_controller.setHueAdjustment(41.0);
  paste_global_controller.addMask(QStringLiteral("linear"));
  if (paste_global_controller.photoLayerCount() != 1 ||
      paste_global_controller.maskSets().size() != 1 ||
      !paste_global_controller.setPhotoLayerOpacity(0, 0.46) ||
      !paste_global_controller.setPhotoLayerBlendMode(0, QStringLiteral("screen")) ||
      !paste_global_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), -1.0) ||
      !paste_global_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("temperature"), 12.0) ||
      !paste_global_controller.setPhotoLayerAdjustmentString(
          0, QStringLiteral("colorMixerModel"), QStringLiteral("HSL")) ||
      !paste_global_controller.setPhotoLayerAdjustmentString(
          0, QStringLiteral("hslChannel"), QStringLiteral("Reds")) ||
      !paste_global_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("hueAdjustment"), -26.0)) {
    return fail("paste-from-global proof should prepare a masked target layer and global source recipe");
  }
  const auto paste_global_before = paste_global_controller.photoLayers().at(0).toMap();
  const auto paste_global_layer_id = paste_global_before.value(QStringLiteral("id")).toString();
  const auto paste_global_mask_set_id =
      paste_global_before.value(QStringLiteral("selectionMaskSetId")).toString();
  if (!paste_global_controller.pasteGlobalAdjustmentsToPhotoLayer(0)) {
    return fail("adjustment layers should paste the current Base Image/global recipe onto the active layer");
  }
  const auto paste_global_after = paste_global_controller.photoLayers().at(0).toMap();
  const auto paste_global_adjustments =
      required_map(paste_global_after, QStringLiteral("adjustments"));
  const auto paste_global_color_mixer =
      required_map(paste_global_adjustments, QStringLiteral("colorMixer"));
  if (paste_global_after.value(QStringLiteral("id")).toString() != paste_global_layer_id ||
      paste_global_after.value(QStringLiteral("selectionMaskSetId")).toString() !=
          paste_global_mask_set_id ||
      !paste_global_after.value(QStringLiteral("selectionScoped")).toBool() ||
      !almost_equal(paste_global_after.value(QStringLiteral("opacity")).toDouble(), 0.46) ||
      paste_global_after.value(QStringLiteral("blendMode")).toString() != QStringLiteral("screen") ||
      !almost_equal(paste_global_after.value(QStringLiteral("exposure")).toDouble(), 0.58) ||
      !almost_equal(paste_global_after.value(QStringLiteral("temperature")).toDouble(), -22.0) ||
      !almost_equal(paste_global_after.value(QStringLiteral("texture")).toDouble(), 31.0) ||
      !almost_equal(paste_global_after.value(QStringLiteral("curveShadows")).toDouble(), 19.0) ||
      paste_global_color_mixer.value(QStringLiteral("model")).toString() != QStringLiteral("HSV") ||
      paste_global_color_mixer.value(QStringLiteral("channel")).toString() != QStringLiteral("Blues") ||
      !almost_equal(paste_global_color_mixer.value(QStringLiteral("hue")).toDouble(), 41.0) ||
      !paste_global_controller.status().contains(QStringLiteral("Pasted Base Image adjustments"))) {
    return fail("paste-from-global should replace only layer recipe data while preserving layer/mask structure");
  }
  paste_global_controller.setExposure(0.0);
  paste_global_controller.setTemperature(0.0);
  paste_global_controller.setTexture(0.0);
  paste_global_controller.setCurveShadows(0.0);
  paste_global_controller.setColorMixerModel(QStringLiteral("HSL"));
  paste_global_controller.setHslChannel(QStringLiteral("Blues"));
  paste_global_controller.setHueAdjustment(0.0);
  paste_global_controller.setHslChannel(QStringLiteral("Reds"));
  if (!paste_global_controller.pasteGlobalAdjustmentsToPhotoLayer(0)) {
    return fail("pasting neutral Base Image settings should clear an edited layer recipe");
  }
  const auto paste_global_neutral_layer = paste_global_controller.photoLayers().at(0).toMap();
  if (paste_global_neutral_layer.value(QStringLiteral("id")).toString() != paste_global_layer_id ||
      paste_global_neutral_layer.value(QStringLiteral("selectionMaskSetId")).toString() !=
          paste_global_mask_set_id ||
      !almost_equal(paste_global_neutral_layer.value(QStringLiteral("opacity")).toDouble(), 0.46) ||
      !paste_global_neutral_layer.value(QStringLiteral("adjustments")).toMap().isEmpty() ||
      paste_global_neutral_layer.contains(QStringLiteral("exposure")) ||
      paste_global_neutral_layer.contains(QStringLiteral("temperature")) ||
      paste_global_neutral_layer.contains(QStringLiteral("texture")) ||
      paste_global_neutral_layer.contains(QStringLiteral("curveShadows")) ||
      paste_global_controller.pasteGlobalAdjustmentsToPhotoLayer(0)) {
    return fail("neutral paste-from-global should clear recipe data exactly and preserve the masked layer shell");
  }

  if (!controller.setPhotoLayerLocked(1, true) ||
      controller.resetPhotoLayerAdjustmentGroup(1, QStringLiteral("light"))) {
    return fail("locked adjustment layers should reject layer-local resets");
  }

  photo_editor::desktop::EditorPreviewController module_reset_controller;
  module_reset_controller.addMask(QStringLiteral("linear"));
  if (module_reset_controller.photoLayerCount() != 1 ||
      !module_reset_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("texture"), 30.0) ||
      !module_reset_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("sharpening"), 40.0) ||
      !module_reset_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("lensVignette"), -25.0) ||
      !module_reset_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("transformScale"), 1.7) ||
      !module_reset_controller.setPhotoLayerAdjustmentBool(0, QStringLiteral("contentAwareCropFill"), true)) {
    return fail("module reset proof should prepare layer-local detail/effects/transform edits");
  }
  if (!module_reset_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("detail")) ||
      !module_reset_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("effects")) ||
      !module_reset_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("transform"))) {
    return fail("layer-local detail, effects, and transform groups should reset independently");
  }
  const auto module_reset_layer = module_reset_controller.photoLayers().at(0).toMap();
  if (module_reset_layer.contains(QStringLiteral("texture")) ||
      module_reset_layer.contains(QStringLiteral("sharpening")) ||
      module_reset_layer.contains(QStringLiteral("lensVignette")) ||
      module_reset_layer.contains(QStringLiteral("transformScale")) ||
      module_reset_layer.contains(QStringLiteral("contentAwareCropFill")) ||
      module_reset_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("detail")) ||
      module_reset_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("effects")) ||
      module_reset_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("transform"))) {
    return fail("layer-local detail/effects/transform resets should be exact and idempotent");
  }

  photo_editor::desktop::EditorPreviewController full_layer_reset_controller;
  full_layer_reset_controller.addMask(QStringLiteral("linear"));
  QVariantList reset_lut_table;
  for (int i = 0; i < 8; ++i) {
    reset_lut_table.append(QVariant(QVariantList{1.0, 0.0, 0.0, 1.0}));
  }
  QVariantMap reset_lut_entry;
  reset_lut_entry.insert(QStringLiteral("enabled"), true);
  reset_lut_entry.insert(QStringLiteral("strength"), 70.0);
  reset_lut_entry.insert(QStringLiteral("name"), QStringLiteral("Reset Proof LUT"));
  reset_lut_entry.insert(QStringLiteral("size"), 2);
  reset_lut_entry.insert(QStringLiteral("table"), reset_lut_table);
  reset_lut_entry.insert(QStringLiteral("domainMin"), QVariantList{0.0, 0.0, 0.0});
  reset_lut_entry.insert(QStringLiteral("domainMax"), QVariantList{1.0, 1.0, 1.0});
  reset_lut_entry.insert(QStringLiteral("inputColorSpace"), QStringLiteral("sRGB"));
  reset_lut_entry.insert(QStringLiteral("outputColorSpace"), QStringLiteral("sRGB"));
  if (full_layer_reset_controller.photoLayerCount() != 1 ||
      full_layer_reset_controller.maskSets().size() != 1 ||
      !full_layer_reset_controller.setPhotoLayerOpacity(0, 0.61) ||
      !full_layer_reset_controller.setPhotoLayerBlendMode(0, QStringLiteral("screen")) ||
      !full_layer_reset_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), 0.85) ||
      !full_layer_reset_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("temperature"), -18.0) ||
      !full_layer_reset_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("texture"), 34.0) ||
      !full_layer_reset_controller.setPhotoLayerCurvePoints(
          0,
          QStringLiteral("RGB"),
          curve_points({{0.0, 0.02}, {0.42, 0.34}, {1.0, 0.98}})) ||
      !full_layer_reset_controller.setPhotoLayerAdjustmentString(
          0, QStringLiteral("colorMixerModel"), QStringLiteral("HSL")) ||
      !full_layer_reset_controller.setPhotoLayerAdjustmentString(
          0, QStringLiteral("hslChannel"), QStringLiteral("Blues")) ||
      !full_layer_reset_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("hueAdjustment"), -44.0) ||
      !full_layer_reset_controller.setPhotoLayerAdjustmentBool(
          0, QStringLiteral("colorReplacementEnabled"), true) ||
      !full_layer_reset_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("gradingShadowsSaturation"), 38.0) ||
      !full_layer_reset_controller.setPhotoLayerLutStack(0, QVariantList{reset_lut_entry})) {
    return fail("full layer reset proof should prepare a masked adjustment layer with broad edits");
  }
  const auto full_reset_before = full_layer_reset_controller.photoLayers().at(0).toMap();
  const auto full_reset_before_adjustments =
      full_reset_before.value(QStringLiteral("adjustments")).toMap();
  const auto full_reset_before_groups =
      full_reset_before.value(QStringLiteral("changedAdjustmentGroups")).toList();
  if (full_reset_before.value(QStringLiteral("kind")).toString() != QStringLiteral("adjustment") ||
      !full_reset_before.value(QStringLiteral("selectionScoped")).toBool() ||
      full_reset_before.value(QStringLiteral("selectionMaskSetId")).toString().isEmpty() ||
      full_reset_before_adjustments.isEmpty() ||
      !full_reset_before.contains(QStringLiteral("exposure")) ||
      !full_reset_before.contains(QStringLiteral("temperature")) ||
      !full_reset_before.contains(QStringLiteral("texture")) ||
      !full_reset_before.contains(QStringLiteral("masterCurvePoints")) ||
      !full_reset_before_adjustments.contains(QStringLiteral("colorMixer")) ||
      !full_reset_before_adjustments.contains(QStringLiteral("colorGrading")) ||
      !full_reset_before_adjustments.contains(QStringLiteral("lutStack"))) {
    return fail("full layer reset proof should include direct, curve, nested color, and LUT recipe data");
  }
  if (!full_reset_before.value(QStringLiteral("hasNonNeutralAdjustments")).toBool() ||
      !group_list_contains(full_reset_before_groups, QStringLiteral("white-balance"), QStringLiteral("WB")) ||
      !group_list_contains(full_reset_before_groups, QStringLiteral("light"), QStringLiteral("Light")) ||
      !group_list_contains(full_reset_before_groups, QStringLiteral("tone-curve"), QStringLiteral("Curve")) ||
      !group_list_contains(full_reset_before_groups, QStringLiteral("color"), QStringLiteral("Color")) ||
      !group_list_contains(full_reset_before_groups, QStringLiteral("detail"), QStringLiteral("Detail")) ||
      full_reset_before.value(QStringLiteral("changedAdjustmentGroupLabel"))
              .toString()
              .split(QStringLiteral(", "), Qt::SkipEmptyParts)
              .size() < 5 ||
      !full_reset_before.value(QStringLiteral("layerAuditLabel"))
           .toString()
           .contains(QStringLiteral("edited:"))) {
    return fail("edited adjustment layers should expose non-neutral changed groups for layer rows");
  }
  if (!full_layer_reset_controller.setPhotoLayerAdjustmentsPreviewBypassed(0, true)) {
    return fail("edited adjustment layers should support per-layer before/after preview bypass");
  }
  const auto full_reset_preview_before =
      full_layer_reset_controller.photoLayers().at(0).toMap();
  if (!full_reset_preview_before.value(QStringLiteral("adjustmentsPreviewBypassed")).toBool() ||
      !full_reset_preview_before.value(QStringLiteral("hasNonNeutralAdjustments")).toBool() ||
      !full_reset_preview_before.contains(QStringLiteral("exposure")) ||
      !full_reset_preview_before.value(QStringLiteral("adjustments")).toMap().contains(
          QStringLiteral("colorMixer")) ||
      !full_reset_preview_before.value(QStringLiteral("layerAuditLabel"))
           .toString()
           .contains(QStringLiteral("preview: before")) ||
      !full_reset_preview_before.value(QStringLiteral("selectionMaskSetId"))
           .toString()
           .contains(QStringLiteral("linear-set"))) {
    return fail("per-layer before preview should preserve recipe data and mask linkage while flagging only preview bypass");
  }
  if (!full_layer_reset_controller.setPhotoLayerAdjustmentsPreviewBypassed(0, false) ||
      full_layer_reset_controller.photoLayers()
          .at(0)
          .toMap()
          .value(QStringLiteral("adjustmentsPreviewBypassed"))
          .toBool()) {
    return fail("per-layer before/after preview should restore the adjusted layer view without clearing recipe data");
  }
  const auto full_reset_mask_set_id_for_preview =
      full_reset_before.value(QStringLiteral("selectionMaskSetId")).toString();
  const auto full_reset_initial_mask_stack = full_layer_reset_controller.maskStack();
  if (full_reset_mask_set_id_for_preview.isEmpty() ||
      !full_layer_reset_controller.setMaskSetPreviewBypassed(
          full_reset_mask_set_id_for_preview,
          true)) {
    return fail("mask attachments should support per-mask before/after preview bypass");
  }
  const auto full_reset_mask_preview_set =
      full_layer_reset_controller.maskSets().constFirst().toMap();
  const auto full_reset_mask_preview_source =
      full_reset_mask_preview_set.value(QStringLiteral("sources")).toList().constFirst().toMap();
  const auto full_reset_mask_preview_stack =
      full_layer_reset_controller.maskStack().constFirst().toMap();
  const auto full_reset_mask_preview_layer =
      full_layer_reset_controller.photoLayers().at(0).toMap();
  if (!full_reset_mask_preview_set.value(QStringLiteral("maskSetPreviewBypassed")).toBool() ||
      !full_reset_mask_preview_source.value(QStringLiteral("maskSetPreviewBypassed")).toBool() ||
      !full_reset_mask_preview_source.value(QStringLiteral("sourceAuditLabel"))
           .toString()
           .contains(QStringLiteral("preview: before")) ||
      !full_reset_mask_preview_stack.value(QStringLiteral("maskSetPreviewBypassed")).toBool() ||
      !full_reset_mask_preview_stack.value(QStringLiteral("enabled")).toBool() ||
      full_reset_mask_preview_layer.value(QStringLiteral("selectionMaskSetId")).toString() !=
          full_reset_mask_set_id_for_preview ||
      !full_reset_mask_preview_layer.value(QStringLiteral("selectionScoped")).toBool() ||
      !almost_equal(full_reset_mask_preview_layer.value(QStringLiteral("opacity")).toDouble(), 0.61) ||
      full_reset_mask_preview_layer.value(QStringLiteral("blendMode")).toString() !=
          QStringLiteral("screen")) {
    return fail("per-mask before preview should preserve mask sources, layer linkage, opacity, and blend mode");
  }
  if (!full_layer_reset_controller.setMaskSetPreviewBypassed(
          full_reset_mask_set_id_for_preview,
          false) ||
      full_layer_reset_controller.maskSets()
          .constFirst()
          .toMap()
          .value(QStringLiteral("maskSetPreviewBypassed"))
          .toBool() ||
      full_layer_reset_controller.maskStack() != full_reset_initial_mask_stack) {
    return fail("per-mask before/after preview should restore the attachment without losing stored mask data");
  }
  const auto full_reset_layer_id = full_reset_before.value(QStringLiteral("id")).toString();
  const auto full_reset_mask_set_id =
      full_reset_before.value(QStringLiteral("selectionMaskSetId")).toString();
  const auto full_reset_mask_set_count = full_layer_reset_controller.maskSets().size();
  if (!full_layer_reset_controller.resetPhotoLayerAdjustments(0)) {
    return fail("full adjustment layer reset should clear a non-neutral layer recipe");
  }
  const auto full_reset_after = full_layer_reset_controller.photoLayers().at(0).toMap();
  const auto full_reset_after_adjustments =
      full_reset_after.value(QStringLiteral("adjustments")).toMap();
  if (full_reset_after.value(QStringLiteral("id")).toString() != full_reset_layer_id ||
      full_reset_after.value(QStringLiteral("kind")).toString() != QStringLiteral("adjustment") ||
      !full_reset_after.value(QStringLiteral("selectionScoped")).toBool() ||
      full_reset_after.value(QStringLiteral("selectionMaskSetId")).toString() !=
          full_reset_mask_set_id ||
      full_layer_reset_controller.maskSets().size() != full_reset_mask_set_count ||
      full_reset_after.value(QStringLiteral("hasNonNeutralAdjustments")).toBool() ||
      !full_reset_after.value(QStringLiteral("changedAdjustmentGroups")).toList().isEmpty() ||
      !full_reset_after.value(QStringLiteral("changedAdjustmentGroupLabel")).toString().isEmpty() ||
      full_reset_after.value(QStringLiteral("layerAuditLabel"))
          .toString()
          .contains(QStringLiteral("edited:")) ||
      !almost_equal(full_reset_after.value(QStringLiteral("opacity")).toDouble(), 0.61) ||
      full_reset_after.value(QStringLiteral("blendMode")).toString() != QStringLiteral("screen") ||
      !full_reset_after_adjustments.isEmpty() ||
      full_reset_after.contains(QStringLiteral("exposure")) ||
      full_reset_after.contains(QStringLiteral("temperature")) ||
      full_reset_after.contains(QStringLiteral("texture")) ||
      full_reset_after.contains(QStringLiteral("masterCurvePoints")) ||
      full_reset_after.contains(QStringLiteral("lutStack")) ||
      full_layer_reset_controller.resetPhotoLayerAdjustments(0) ||
      full_layer_reset_controller.setPhotoLayerAdjustmentsPreviewBypassed(0, true)) {
    return fail("full adjustment layer reset should remove only recipe data and preserve mask, layer identity, opacity, and blend mode");
  }
  if (!full_layer_reset_controller.setPhotoLayerAdjustmentValue(
          0, QStringLiteral("exposure"), 0.35) ||
      !full_layer_reset_controller.setPhotoLayerLocked(0, true) ||
      full_layer_reset_controller.resetPhotoLayerAdjustments(0)) {
    return fail("locked adjustment layers should reject full layer resets");
  }

  photo_editor::desktop::EditorPreviewController layer_preset_controller;
  layer_preset_controller.loadAsset(
      QStringLiteral("layer-preset-target-asset"),
      thumbnail_fixture_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  layer_preset_controller.setExposure(0.44);
  layer_preset_controller.setTexture(44.0);
  layer_preset_controller.setVibrance(66.0);
  layer_preset_controller.addMask(QStringLiteral("linear"));
  if (layer_preset_controller.photoLayerCount() != 1) {
    return fail("test should prepare one linked adjustment layer for layer preset routing");
  }
  const auto layer_preset_preview = layer_preset_controller.photoLayerPresetPreviewRecipe(
      0,
      QStringLiteral("Landscape Pop"),
      {},
      0.5);
  const auto layer_before_preset = layer_preset_controller.photoLayers().at(0).toMap();
  const auto preview_adjustments = required_map(layer_preset_preview, QStringLiteral("adjustments"));
  const auto preview_color_mixer = required_map(preview_adjustments, QStringLiteral("colorMixer"));
  const auto preview_channels = required_map(preview_color_mixer, QStringLiteral("channels"));
  const auto preview_blue_channel = required_map(preview_channels, QStringLiteral("Blues"));
  if (layer_preset_preview.isEmpty() ||
      layer_before_preset.contains(QStringLiteral("texture")) ||
      !almost_equal(layer_preset_preview.value(QStringLiteral("exposure")).toDouble(), 0.04) ||
      !almost_equal(layer_preset_preview.value(QStringLiteral("contrast")).toDouble(), 9.0) ||
      !almost_equal(layer_preset_preview.value(QStringLiteral("texture")).toDouble(), 9.0) ||
      !almost_equal(layer_preset_preview.value(QStringLiteral("clarity")).toDouble(), 10.0) ||
      !almost_equal(layer_preset_preview.value(QStringLiteral("dehaze")).toDouble(), 7.0) ||
      !almost_equal(layer_preset_preview.value(QStringLiteral("vibrance")).toDouble(), 11.0) ||
      !almost_equal(preview_blue_channel.value(QStringLiteral("saturation")).toDouble(), 8.0) ||
      !almost_equal(preview_blue_channel.value(QStringLiteral("luminance")).toDouble(), -4.0)) {
    return fail("layer preset preview should produce a target-local recipe without mutating the layer");
  }
  if (!layer_preset_controller.applyPresetToPhotoLayer(
          0,
          QStringLiteral("Landscape Pop"),
          0.5,
          {}) ||
      !almost_equal(layer_preset_controller.exposure(), 0.44) ||
      !almost_equal(layer_preset_controller.texture(), 44.0) ||
      !almost_equal(layer_preset_controller.vibrance(), 66.0)) {
    return fail("applying a preset to a layer should not mutate Base Image adjustments");
  }
  if (!layer_preset_controller.status().contains(
          QStringLiteral("Applied preset 'Landscape Pop' to layer"))) {
    return fail("layer preset application should report the applied preset before a later action changes status");
  }
  const auto layer_authoring_result = layer_preset_controller.saveCustomPreset(
      QStringLiteral("Layer Only Look"),
      QStringLiteral("Layer Looks"),
      {QStringLiteral("tone")},
      0);
  const auto layer_authoring_state =
      photo_editor::desktop::parse_adjustment_payload_or_legacy_recipe(
          layer_preset_controller.presetAdjustmentPatchJson(QStringLiteral("Layer Only Look")))
          .state.value_or(photo_editor::desktop::EditState{});
  if (!layer_authoring_result.value(QStringLiteral("ok")).toBool() ||
      !almost_equal(layer_authoring_state.exposure, 0.04) ||
      !almost_equal(layer_authoring_state.vibrance, 0.0) ||
      !almost_equal(layer_preset_controller.exposure(), 0.44) ||
      !almost_equal(layer_preset_controller.vibrance(), 66.0)) {
    return fail("saving a layer preset should capture that layer's selected groups without changing Base Image or layer state");
  }
  const auto layer_after_preset = layer_preset_controller.photoLayers().at(0).toMap();
  const auto preset_adjustments = required_map(layer_after_preset, QStringLiteral("adjustments"));
  const auto preset_color_mixer = required_map(preset_adjustments, QStringLiteral("colorMixer"));
  const auto preset_channels = required_map(preset_color_mixer, QStringLiteral("channels"));
  const auto preset_blue_channel = required_map(preset_channels, QStringLiteral("Blues"));
  if (!almost_equal(layer_after_preset.value(QStringLiteral("exposure")).toDouble(), 0.04) ||
      !almost_equal(layer_after_preset.value(QStringLiteral("contrast")).toDouble(), 9.0) ||
      !almost_equal(layer_after_preset.value(QStringLiteral("texture")).toDouble(), 9.0) ||
      !almost_equal(layer_after_preset.value(QStringLiteral("clarity")).toDouble(), 10.0) ||
      !almost_equal(layer_after_preset.value(QStringLiteral("dehaze")).toDouble(), 7.0) ||
      !almost_equal(layer_after_preset.value(QStringLiteral("vibrance")).toDouble(), 11.0) ||
      !almost_equal(preset_blue_channel.value(QStringLiteral("saturation")).toDouble(), 8.0)) {
    return fail("layer preset application should write the preset recipe onto the adjustment layer");
  }
  if (!layer_preset_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("temperature"), 18.0) ||
      !layer_preset_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("texture"), -12.0) ||
      !layer_preset_controller.applyPresetToPhotoLayer(
          0,
          QStringLiteral("Landscape Pop"),
          0.5,
          {QStringLiteral("detail")})) {
    return fail("layer preset application should support partial preset groups");
  }
  const auto detail_only_layer = layer_preset_controller.photoLayers().at(0).toMap();
  if (!almost_equal(detail_only_layer.value(QStringLiteral("temperature")).toDouble(), 18.0) ||
      !almost_equal(detail_only_layer.value(QStringLiteral("texture")).toDouble(), 9.0) ||
      !almost_equal(detail_only_layer.value(QStringLiteral("contrast")).toDouble(), 9.0) ||
      !layer_preset_controller.photoLayerPresetPreviewRecipe(
          99,
          QStringLiteral("Landscape Pop"),
          {},
          0.5).isEmpty() ||
      layer_preset_controller.applyPresetToPhotoLayer(99, QStringLiteral("Landscape Pop"), 0.5, {})) {
    return fail("partial and invalid layer preset operations should be scoped and validated");
  }

  return EXIT_SUCCESS;
}

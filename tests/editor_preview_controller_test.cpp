#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QCoreApplication>
#include <QColor>
#include <QColorSpace>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QPointF>
#include <QRectF>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "photo_render_utils.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "raw_image_decoder.h"
#include "working_image.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool almost_equal(double left, double right, double tolerance = 1e-6) {
  return std::abs(left - right) <= tolerance;
}

int selected_pixel_count(const QByteArray& mask_data) {
  auto count = 0;
  for (const auto value : mask_data) {
    if (static_cast<unsigned char>(value) > 0) {
      ++count;
    }
  }
  return count;
}

std::uint64_t fnv1a_append(std::uint64_t hash, std::uint64_t value) {
  constexpr std::uint64_t prime = 1099511628211ULL;
  for (int i = 0; i < 8; ++i) {
    hash ^= (value >> (i * 8)) & 0xFFULL;
    hash *= prime;
  }
  return hash;
}

std::uint64_t image_hash(const QImage& image) {
  constexpr std::uint64_t offset = 14695981039346656037ULL;
  constexpr std::uint64_t prime = 1099511628211ULL;
  auto hash = offset;
  hash = fnv1a_append(hash, static_cast<std::uint64_t>(image.width()));
  hash = fnv1a_append(hash, static_cast<std::uint64_t>(image.height()));
  hash = fnv1a_append(hash, static_cast<std::uint64_t>(image.format()));
  for (int y = 0; y < image.height(); ++y) {
    const auto* scanline = image.constScanLine(y);
    for (qsizetype index = 0; index < image.bytesPerLine(); ++index) {
      hash ^= static_cast<std::uint64_t>(scanline[index]);
      hash *= prime;
    }
  }
  return hash;
}

QImage built_in_preset_portrait_source_image() {
  QImage image(72, 72, QImage::Format_ARGB32);
  image.setColorSpace(photo_editor::desktop::preview_color_space());
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      auto red = 34 + (x % 11);
      auto green = 42 + (y % 13);
      auto blue = 52 + ((x + y) % 17);
      const auto dx = static_cast<double>(x - 36) / 21.0;
      const auto dy = static_cast<double>(y - 34) / 27.0;
      if (dx * dx + dy * dy < 1.0) {
        red = 166 + ((x * 3 + y) % 24);
        green = 105 + ((x + y * 2) % 22);
        blue = 78 + ((x * 5 + y) % 18);
      }
      if (y > 17 && y < 26 && x > 22 && x < 50) {
        red = std::min(red, 72);
        green = std::min(green, 54);
        blue = std::min(blue, 44);
      }
      image.setPixelColor(x, y, QColor(red, green, blue, 255));
    }
  }
  return image;
}

QImage built_in_preset_landscape_source_image() {
  QImage image(88, 58, QImage::Format_ARGB32);
  image.setColorSpace(photo_editor::desktop::preview_color_space());
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      auto red = 48 + y;
      auto green = 86 + y / 2;
      auto blue = 158 + y;
      if (y > 22) {
        red = 36 + (x % 17);
        green = 92 + ((x * 5 + y * 3) % 78);
        blue = 42 + (y % 23);
      }
      if (y > 34 && x > 50) {
        red = 86 + ((x + y) % 34);
        green = 66 + ((x * 2) % 26);
        blue = 38 + (y % 18);
      }
      if (x > 8 && x < 32 && y > 27 && y < 45) {
        red = 18 + (x % 11);
        green = 68 + (y % 17);
        blue = 30 + ((x + y) % 9);
      }
      image.setPixelColor(x, y, QColor(red, green, blue, 255));
    }
  }
  return image;
}

QImage preset_thumbnail_fixture_source_image() {
  QImage image(96, 64, QImage::Format_ARGB32);
  image.setColorSpace(photo_editor::desktop::preview_color_space());
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const auto horizontal = static_cast<double>(x) / static_cast<double>(image.width() - 1);
      const auto vertical = static_cast<double>(y) / static_cast<double>(image.height() - 1);
      int red = 30;
      int green = 46;
      int blue = 64;
      if (vertical < 0.42) {
        red = static_cast<int>(44 + horizontal * 72 + vertical * 38);
        green = static_cast<int>(76 + horizontal * 42 + vertical * 56);
        blue = static_cast<int>(132 + horizontal * 54 + vertical * 40);
      } else {
        red = static_cast<int>(28 + horizontal * 44 + (1.0 - vertical) * 28);
        green = static_cast<int>(82 + horizontal * 86 + (1.0 - vertical) * 40);
        blue = static_cast<int>(38 + horizontal * 36 + (1.0 - vertical) * 28);
      }
      if (y > 22 && y < 36 && x > 26 && x < 70) {
        red = 188 + ((x + y) % 22);
        green = 182 + ((x * 3 + y) % 24);
        blue = 166 + ((x + y * 2) % 28);
      }
      if (y > 42 && x < 36) {
        red = 20 + (x % 12);
        green = 58 + (y % 18);
        blue = 32 + ((x + y) % 11);
      }
      image.setPixelColor(
          x,
          y,
          QColor(qBound(0, red, 255), qBound(0, green, 255), qBound(0, blue, 255), 255));
    }
  }
  return image;
}

QImage decode_png_data_url(const QString& source) {
  const auto prefix = QStringLiteral("data:image/png;base64,");
  if (!source.startsWith(prefix)) {
    return {};
  }
  return QImage::fromData(QByteArray::fromBase64(source.mid(prefix.size()).toLatin1()), "PNG");
}

double curve_point_coordinate(const QVariant& point, const char* key) {
  return point.toMap().value(QString::fromLatin1(key)).toDouble();
}

int max_channel_delta(const QImage& left, const QImage& right) {
  if (left.size() != right.size()) {
    return 255;
  }

  auto max_delta = 0;
  for (int y = 0; y < left.height(); ++y) {
    for (int x = 0; x < left.width(); ++x) {
      const auto lhs = left.pixelColor(x, y);
      const auto rhs = right.pixelColor(x, y);
      max_delta = std::max(max_delta, std::abs(lhs.red() - rhs.red()));
      max_delta = std::max(max_delta, std::abs(lhs.green() - rhs.green()));
      max_delta = std::max(max_delta, std::abs(lhs.blue() - rhs.blue()));
    }
  }
  return max_delta;
}

int max_channel_span(const QImage& image) {
  if (image.isNull()) {
    return 0;
  }

  auto min_red = 255;
  auto min_green = 255;
  auto min_blue = 255;
  auto max_red = 0;
  auto max_green = 0;
  auto max_blue = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const auto pixel = image.pixelColor(x, y);
      min_red = std::min(min_red, pixel.red());
      min_green = std::min(min_green, pixel.green());
      min_blue = std::min(min_blue, pixel.blue());
      max_red = std::max(max_red, pixel.red());
      max_green = std::max(max_green, pixel.green());
      max_blue = std::max(max_blue, pixel.blue());
    }
  }
  return std::max({max_red - min_red, max_green - min_green, max_blue - min_blue});
}

int max_alpha_channel(const QImage& image) {
  if (image.isNull()) {
    return 0;
  }

  auto max_alpha = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      max_alpha = std::max(max_alpha, image.pixelColor(x, y).alpha());
    }
  }
  return max_alpha;
}

unsigned char mask_value_at(
    const QByteArray& mask_data,
    int width,
    int height,
    double normalized_x,
    double normalized_y) {
  if (mask_data.isEmpty() || width <= 0 || height <= 0) {
    return 0;
  }

  const auto x = std::clamp(
      static_cast<int>(std::lround(normalized_x * static_cast<double>(std::max(width - 1, 0)))),
      0,
      std::max(width - 1, 0));
  const auto y = std::clamp(
      static_cast<int>(std::lround(normalized_y * static_cast<double>(std::max(height - 1, 0)))),
      0,
      std::max(height - 1, 0));
  return static_cast<unsigned char>(mask_data.at(y * width + x));
}

QRectF alpha_mask_bounds(
    const QByteArray& mask_data,
    int width,
    int height,
    unsigned char threshold = 96) {
  if (mask_data.isEmpty() || width <= 0 || height <= 0 || mask_data.size() < width * height) {
    return {};
  }

  auto min_x = width;
  auto min_y = height;
  auto max_x = -1;
  auto max_y = -1;
  for (auto y = 0; y < height; ++y) {
    for (auto x = 0; x < width; ++x) {
      if (static_cast<unsigned char>(mask_data.at(y * width + x)) < threshold) {
        continue;
      }
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
    }
  }
  if (max_x < min_x || max_y < min_y) {
    return {};
  }

  return QRectF(
      QPointF(
          static_cast<double>(min_x) / static_cast<double>(width),
          static_cast<double>(min_y) / static_cast<double>(height)),
      QPointF(
          static_cast<double>(max_x + 1) / static_cast<double>(width),
      static_cast<double>(max_y + 1) / static_cast<double>(height)));
}

QVariantMap normalized_point(double x, double y) {
  QVariantMap point;
  point.insert(QStringLiteral("x"), x);
  point.insert(QStringLiteral("y"), y);
  return point;
}

bool mask_source_has_full_resolution_alpha_contract(
    const QVariantMap& source,
    const QString& expected_provider) {
  const auto provider_signature =
      source.value(QStringLiteral("mask_alpha_provider_signature")).toString();
  const auto trace_signature =
      source.value(QStringLiteral("mask_alpha_trace_signature")).toString();
  return source.value(QStringLiteral("mask_alpha_provider_contract")).toString() ==
             QStringLiteral("shared-mask-alpha-provider-v1") &&
         source.value(QStringLiteral("mask_alpha_provider")).toString() == expected_provider &&
         source.value(QStringLiteral("mask_alpha_full_resolution_available")).toBool() &&
         source.value(QStringLiteral("mask_alpha_deterministic")).toBool() &&
         source.value(QStringLiteral("mask_source_hash")).toString().startsWith(QStringLiteral("sha1:")) &&
         source.value(QStringLiteral("mask_alpha_trace_hash")).toString().startsWith(
             QStringLiteral("sha1:")) &&
         trace_signature.contains(QStringLiteral("full:true")) &&
         provider_signature.contains(QStringLiteral("contract:shared-mask-alpha-provider-v1")) &&
         provider_signature.contains(QStringLiteral("provider:%1").arg(expected_provider)) &&
         provider_signature.contains(QStringLiteral("full:true")) &&
         provider_signature.contains(QStringLiteral("deterministic:true"));
}

QRectF outline_bounds(const QVariantList& outline_paths) {
  auto min_x = 1.0;
  auto min_y = 1.0;
  auto max_x = 0.0;
  auto max_y = 0.0;
  auto point_count = 0;

  const auto accumulate_point = [&](const QVariantMap& point) {
    if (!point.contains(QStringLiteral("x")) || !point.contains(QStringLiteral("y"))) {
      return;
    }
    const auto x = point.value(QStringLiteral("x")).toDouble();
    const auto y = point.value(QStringLiteral("y")).toDouble();
    min_x = std::min(min_x, x);
    min_y = std::min(min_y, y);
    max_x = std::max(max_x, x);
    max_y = std::max(max_y, y);
    ++point_count;
  };

  for (const auto& value : outline_paths) {
    const auto point = value.toMap();
    if (!point.isEmpty() && point.contains(QStringLiteral("x"))) {
      accumulate_point(point);
      continue;
    }
    for (const auto& nested_value : value.toList()) {
      accumulate_point(nested_value.toMap());
    }
  }

  if (point_count == 0) {
    return {};
  }
  return QRectF(QPointF(min_x, min_y), QPointF(max_x, max_y)).normalized();
}

bool outline_bounds_match(const QRectF& left, const QRectF& right, double tolerance) {
  return left.isValid() && right.isValid() &&
      std::abs(left.left() - right.left()) <= tolerance &&
      std::abs(left.top() - right.top()) <= tolerance &&
      std::abs(left.right() - right.right()) <= tolerance &&
      std::abs(left.bottom() - right.bottom()) <= tolerance;
}

double outline_tolerance_for_image_pixels(int width, int height, double pixels = 1.5) {
  return pixels / static_cast<double>(std::max(1, std::min(width, height)));
}

QString outline_bounds_detail(const QString& label, const QRectF& outline, const QRectF& expected) {
  return QStringLiteral("%1 outline bounds mismatch: outline=(%2,%3 %4x%5) expected=(%6,%7 %8x%9)")
      .arg(label)
      .arg(outline.x(), 0, 'f', 4)
      .arg(outline.y(), 0, 'f', 4)
      .arg(outline.width(), 0, 'f', 4)
      .arg(outline.height(), 0, 'f', 4)
      .arg(expected.x(), 0, 'f', 4)
      .arg(expected.y(), 0, 'f', 4)
      .arg(expected.width(), 0, 'f', 4)
      .arg(expected.height(), 0, 'f', 4);
}

QString read_repo_text_file(const QString& relative_path) {
  QDir dir(QCoreApplication::applicationDirPath());
  for (auto depth = 0; depth < 8; ++depth) {
    const auto candidate = dir.filePath(relative_path);
    if (QFileInfo::exists(candidate)) {
      QFile file(candidate);
      if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
      }
      return QString::fromUtf8(file.readAll());
    }
    if (!dir.cdUp()) {
      break;
    }
  }
  return {};
}

}  // namespace

namespace photo_editor::desktop {

RenderResult render_preview_request_for_test(const RenderRequest& request);

struct EditorPreviewControllerTestAccess {
  static QString cache_path_for_selection(
      const EditorPreviewController& controller,
      bool edited,
      bool full_resolution) {
    return controller.cachePathForSelection(edited, full_resolution);
  }
  static void set_raw_as_shot_white_balance(
      EditorPreviewController& controller,
      double temperature,
      double tint) {
    controller.setRawAsShotWhiteBalance(temperature, tint);
  }
  static void update_raw_clipping_warnings(EditorPreviewController& controller, const QImage& image) {
    controller.updateRawClippingWarnings(image);
  }
  static void compute_histogram(EditorPreviewController& controller, const QImage& image) {
    controller.computeHistogram(image);
  }
  static bool update_active_mask_definition(
      EditorPreviewController& controller,
      const QVariantMap& mask) {
    return controller.updateActiveMaskDefinition(mask);
  }
  static bool apply_photo_layer_stack_change(
      EditorPreviewController& controller,
      const QVariantList& layers) {
    return controller.applyPhotoLayerStackChange(
        layers,
        QStringLiteral("test_multi_mask_layer"),
        false,
        QStringLiteral("Test multi-mask layer"));
  }
  static QString mask_tile_cache_key(
      const QVariantMap& mask,
      const QString& source_revision,
      const QRectF& crop_rect,
      const QSize& raster_size,
      const QRect& tile_rect) {
    return EditorPreviewController::maskTileCacheKeyForTesting(
        mask,
        source_revision,
        crop_rect,
        raster_size,
        tile_rect);
  }
  static void clear_mask_tile_cache() {
    EditorPreviewController::clearMaskTileCacheForTesting();
  }
  static void clear_preview_working_caches() {
    EditorPreviewController::clearPreviewWorkingCachesForTesting();
  }
  static bool insert_mask_tile(
      const QString& key,
      const QByteArray& matte_data,
      const QSize& tile_size) {
    return EditorPreviewController::insertMaskTileForTesting(key, matte_data, tile_size);
  }
  static bool find_mask_tile(
      const QString& key,
      QByteArray* matte_data,
      QSize* tile_size) {
    return EditorPreviewController::findMaskTileForTesting(key, matte_data, tile_size);
  }
  static int mask_tile_cache_entries() {
    return EditorPreviewController::maskTileCacheEntryCountForTesting();
  }
  static qint64 mask_tile_cache_bytes() {
    return EditorPreviewController::maskTileCacheBytesForTesting();
  }
  static QVariantMap scaled_mask(
      const QVariantMap& mask,
      const QSize& target_size,
      const QString& purpose) {
    return EditorPreviewController::scaledMaskForTesting(mask, target_size, purpose);
  }
  static void clear_scaled_mask_matte_cache() {
    EditorPreviewController::clearScaledMaskMatteCacheForTesting();
  }
  static int scaled_mask_matte_cache_entries() {
    return EditorPreviewController::scaledMaskMatteCacheEntryCountForTesting();
  }
  static qint64 scaled_mask_matte_cache_bytes() {
    return EditorPreviewController::scaledMaskMatteCacheBytesForTesting();
  }
  static int scaled_mask_matte_cache_hits() {
    return EditorPreviewController::scaledMaskMatteCacheHitCountForTesting();
  }
  static int scaled_mask_matte_cache_misses() {
    return EditorPreviewController::scaledMaskMatteCacheMissCountForTesting();
  }
  static int scaled_mask_matte_cache_evictions() {
    return EditorPreviewController::scaledMaskMatteCacheEvictionCountForTesting();
  }
  static QSize active_brush_live_preview_size(const EditorPreviewController& controller) {
    return controller.activeBrushLivePreviewSize();
  }
  static QImage active_brush_live_preview_image(const EditorPreviewController& controller) {
    return controller.active_brush_live_preview_image_;
  }
  static bool ensure_smart_select_instance_map(EditorPreviewController& controller) {
    return controller.ensureSmartSelectInstanceMap();
  }
  static QString smart_select_instance_map_key(const EditorPreviewController& controller) {
    return controller.smart_select_instance_map_key_;
  }
  static QSize smart_select_instance_map_size(const EditorPreviewController& controller) {
    return QSize(controller.smart_select_instance_map_width_, controller.smart_select_instance_map_height_);
  }
  static bool smart_select_instance_map_shared_cache_hit(const EditorPreviewController& controller) {
    return controller.smart_select_instance_map_shared_cache_hit_;
  }
  static EditState interactive_render_state(const EditorPreviewController& controller) {
    return controller.currentRenderRequest(false).state;
  }
  static QString recipe_json_for_state(
      const EditorPreviewController& controller,
      const EditState& state) {
    return controller.recipeJsonForState(state);
  }
  static RenderRequest interactive_render_request(const EditorPreviewController& controller) {
    return controller.currentRenderRequest(false);
  }
  static RenderRequest full_render_request(const EditorPreviewController& controller) {
    return controller.currentRenderRequest(true);
  }
  static RenderResult render_preview_request_sync(const RenderRequest& request) {
    return render_preview_request_for_test(request);
  }
  static RenderRequest pending_full_render_request(const EditorPreviewController& controller) {
    return controller.pending_full_request_;
  }
  static QStringList interactive_compare_adjustment_groups(const EditorPreviewController& controller) {
    return controller.currentRenderRequest(false).compare_adjustment_groups;
  }
  static QStringList interactive_mask_compare_adjustment_groups(
      const EditorPreviewController& controller) {
    return controller.currentRenderRequest(false).mask_compare_adjustment_groups;
  }
  static bool full_render_timer_active(const EditorPreviewController& controller) {
    return controller.full_render_timer_.isActive();
  }
  static int full_render_timer_interval(const EditorPreviewController& controller) {
    return controller.full_render_timer_.interval();
  }
  static bool full_render_pending(const EditorPreviewController& controller) {
    return controller.full_request_pending_;
  }
  static bool full_render_running(const EditorPreviewController& controller) {
    return controller.full_render_watcher_.isRunning();
  }
  static bool interactive_render_timer_active(const EditorPreviewController& controller) {
    return controller.interactive_render_timer_.isActive();
  }
  static int interactive_render_timer_interval(const EditorPreviewController& controller) {
    return controller.interactive_render_timer_.interval();
  }
  static bool edit_transaction_interactive_preview_timer_active(
      const EditorPreviewController& controller) {
    return controller.edit_transaction_interactive_preview_timer_.isActive();
  }
  static int edit_transaction_interactive_preview_request_count(
      const EditorPreviewController& controller) {
    return controller.edit_transaction_interactive_preview_request_count_;
  }
  static bool interactive_render_pending(const EditorPreviewController& controller) {
    return controller.interactive_request_pending_;
  }
  static bool interactive_render_running(const EditorPreviewController& controller) {
    return controller.interactive_render_watcher_.isRunning();
  }
  static bool overlay_render_pending(const EditorPreviewController& controller) {
    return controller.overlay_request_pending_;
  }
  static bool overlay_render_running(const EditorPreviewController& controller) {
    return controller.overlay_render_watcher_.isRunning();
  }
  static RenderRequest pending_overlay_render_request(const EditorPreviewController& controller) {
    return controller.pending_overlay_request_;
  }
  static RenderRequest last_overlay_render_request(const EditorPreviewController& controller) {
    return controller.last_overlay_render_request_;
  }
  static bool photo_layer_adjustment_render_timer_active(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_timer_.isActive();
  }
  static int photo_layer_adjustment_render_timer_interval(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_timer_.interval();
  }
  static bool smart_select_preview_request_pending(const EditorPreviewController& controller) {
    return controller.smart_select_preview_request_pending_;
  }
  static bool smart_select_preview_timer_active(const EditorPreviewController& controller) {
    return controller.smart_select_preview_timer_.isActive();
  }
  static bool smart_select_preview_running(const EditorPreviewController& controller) {
    return controller.smart_select_preview_watcher_.isRunning();
  }
  static int photo_layer_adjustment_render_debounce_count(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_debounce_count_;
  }
  static int photo_layer_adjustment_render_flush_count(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_flush_count_;
  }
  static int photo_layer_adjustment_render_pending_write_count(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_pending_write_count_;
  }
  static int photo_layer_adjustment_render_last_coalesced_write_count(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_last_coalesced_write_count_;
  }
  static qint64 photo_layer_adjustment_render_last_flush_elapsed_ms(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_last_flush_elapsed_ms_;
  }
  static qint64 photo_layer_adjustment_render_budget_ms(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_budget_ms_;
  }
  static bool photo_layer_adjustment_render_within_latency_budget(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_within_latency_budget_;
  }
  static bool thumbnail_refresh_deferred(const EditorPreviewController& controller) {
    return controller.thumbnailRefreshDeferred();
  }
  static bool thumbnail_refresh_queue_pending(const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_queue_pending_;
  }
  static bool thumbnail_refresh_queue_timer_active(const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_queue_timer_.isActive();
  }
  static int thumbnail_refresh_queue_scheduled_count(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_queue_scheduled_count_;
  }
  static int thumbnail_refresh_queue_preempted_count(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_queue_preempted_count_;
  }
  static int thumbnail_refresh_queue_dispatched_count(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_queue_dispatched_count_;
  }
  static int thumbnail_refresh_queue_cancelled_count(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_queue_cancelled_count_;
  }
  static int thumbnail_refresh_queue_pending_generation(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_queue_pending_generation_;
  }
  static QString thumbnail_refresh_queue_cancelled_reason(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_queue_cancelled_reason_;
  }
  static bool thumbnail_refresh_queue_worker_running(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_queue_watcher_.isRunning();
  }
  static int thumbnail_refresh_queue_worker_started_count(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_queue_worker_started_count_;
  }
  static int thumbnail_refresh_queue_worker_completed_count(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_queue_worker_completed_count_;
  }
  static int thumbnail_refresh_queue_stale_result_count(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_queue_stale_result_count_;
  }
  static QString thumbnail_refresh_queue_active_job_id(
      const EditorPreviewController& controller) {
    return controller.thumbnail_refresh_queue_active_job_id_;
  }
  static EditState pending_full_render_state(const EditorPreviewController& controller) {
    return controller.pending_full_request_.state;
  }
  static int pending_full_render_generation(const EditorPreviewController& controller) {
    return controller.pending_full_request_.generation;
  }
  static bool interactive_render_draft(const EditorPreviewController& controller) {
    return controller.currentRenderRequest(false).draft_interactive;
  }
  static void begin_edit_transaction(EditorPreviewController& controller) {
    controller.beginEditTransaction();
  }
  static void end_edit_transaction(EditorPreviewController& controller) {
    controller.endEditTransaction();
  }
  static int render_generation(const EditorPreviewController& controller) {
    return controller.render_generation_;
  }
  static int edit_intent_count(const EditorPreviewController& controller) {
    return controller.edit_intent_count_;
  }
  static EditIntent last_edit_intent(const EditorPreviewController& controller) {
    return controller.last_edit_intent_;
  }
  static RenderGraphResolution last_render_graph_resolution(
      const EditorPreviewController& controller) {
    return controller.last_render_graph_resolution_;
  }
  static int draft_promoted_count(const EditorPreviewController& controller) {
    return controller.draft_promoted_count_;
  }
  static int adjusted_preview_cache_hit_count(const EditorPreviewController& controller) {
    return controller.adjusted_preview_cache_hit_count_;
  }
  static int adjusted_preview_cache_fast_schedule_count(const EditorPreviewController& controller) {
    return controller.adjusted_preview_cache_fast_schedule_count_;
  }
  static int rendered_preview_cache_hit_count(const EditorPreviewController& controller) {
    return controller.rendered_preview_cache_hit_count_;
  }
  static int rendered_preview_cache_fast_schedule_count(const EditorPreviewController& controller) {
    return controller.rendered_preview_cache_fast_schedule_count_;
  }
  static int disk_preview_cache_hit_count(const EditorPreviewController& controller) {
    return controller.disk_preview_cache_hit_count_;
  }
  static int disk_preview_cache_fast_schedule_count(const EditorPreviewController& controller) {
    return controller.disk_preview_cache_fast_schedule_count_;
  }
  static int disk_preview_cache_direct_schedule_count(const EditorPreviewController& controller) {
    return controller.disk_preview_cache_direct_schedule_count_;
  }
  static int visible_tile_prepass_schedule_count(const EditorPreviewController& controller) {
    return controller.visible_tile_prepass_schedule_count_;
  }
  static int viewport_max_preview_schedule_count(const EditorPreviewController& controller) {
    return controller.viewport_max_preview_schedule_count_;
  }
  static int visible_overlay_queue_wait_exceeded_count(
      const EditorPreviewController& controller) {
    return controller.visible_overlay_queue_wait_exceeded_count_;
  }
  static qint64 last_visible_overlay_queue_wait_ms(
      const EditorPreviewController& controller) {
    return controller.last_visible_overlay_queue_wait_ms_;
  }
  static QVariantMap last_mask_work_telemetry(const EditorPreviewController& controller) {
    return controller.last_mask_work_telemetry_;
  }
  static int pending_brush_commit_count(const EditorPreviewController& controller) {
    return static_cast<int>(controller.pending_brush_commits_.size()) +
           (controller.pending_brush_commit_scheduled_ ? 1 : 0) +
           (controller.brush_raster_commit_watcher_.isRunning() ? 1 : 0) +
           (controller.brush_raster_commit_active_ ? 1 : 0);
  }
  static bool brush_raster_commit_running(const EditorPreviewController& controller) {
    return controller.brush_raster_commit_watcher_.isRunning() ||
           controller.brush_raster_commit_active_;
  }
  static int brush_raster_cancelled_count(const EditorPreviewController& controller) {
    return controller.brush_raster_cancelled_count_;
  }
  static int mask_work_state_revision(const EditorPreviewController& controller) {
    return controller.mask_work_state_revision_;
  }
  static int active_mask_outline_cache_hit_count(const EditorPreviewController& controller) {
    return controller.active_mask_outline_cache_hit_count_;
  }
  static int active_mask_outline_cache_miss_count(const EditorPreviewController& controller) {
    return controller.active_mask_outline_cache_miss_count_;
  }
  static int active_mask_outline_fast_key_count(const EditorPreviewController& controller) {
    return controller.active_mask_outline_fast_key_count_;
  }
  static int active_mask_outline_full_key_count(const EditorPreviewController& controller) {
    return controller.active_mask_outline_full_key_count_;
  }
  static qint64 active_mask_outline_trace_elapsed_ms(const EditorPreviewController& controller) {
    return controller.active_mask_outline_trace_elapsed_ms_;
  }
  static qint64 active_mask_outline_trace_budget_ms(const EditorPreviewController& controller) {
    return controller.active_mask_outline_trace_budget_ms_;
  }
  static bool active_mask_outline_trace_within_latency_budget(
      const EditorPreviewController& controller) {
    return controller.active_mask_outline_trace_within_latency_budget_;
  }
  static QString active_mask_outline_trace_source(const EditorPreviewController& controller) {
    return controller.active_mask_outline_trace_source_;
  }
  static int active_mask_outline_trace_path_count(const EditorPreviewController& controller) {
    return controller.active_mask_outline_trace_path_count_;
  }
  static QVariantMap active_mask_outline_trace_fields(const EditorPreviewController& controller) {
    return controller.active_mask_outline_trace_fields_;
  }
  static int active_mask_preview_source_skip_count(const EditorPreviewController& controller) {
    return controller.active_mask_preview_source_skip_count_;
  }
  static int smart_select_preview_outline_cache_hit_count(
      const EditorPreviewController& controller) {
    return controller.smart_select_preview_outline_cache_hit_count_;
  }
  static int smart_select_preview_outline_cache_miss_count(
      const EditorPreviewController& controller) {
    return controller.smart_select_preview_outline_cache_miss_count_;
  }
  static int smart_select_preview_generation(const EditorPreviewController& controller) {
    return controller.smart_select_preview_generation_;
  }
  static int smart_select_preview_duplicate_skip_count(
      const EditorPreviewController& controller) {
    return controller.smart_select_preview_duplicate_skip_count_;
  }
  static QVariantMap smart_select_preview_mask(const EditorPreviewController& controller) {
    return controller.smart_select_preview_mask_;
  }
  static int active_mask_preview_lru_hit_count(const EditorPreviewController& controller) {
    return controller.active_mask_preview_lru_hit_count_;
  }
  static int active_mask_preview_lru_miss_count(const EditorPreviewController& controller) {
    return controller.active_mask_preview_lru_miss_count_;
  }
  static int active_mask_preview_lru_eviction_count(const EditorPreviewController& controller) {
    return controller.active_mask_preview_lru_eviction_count_;
  }
  static int active_mask_preview_lru_cache_size(const EditorPreviewController& controller) {
    return static_cast<int>(controller.active_mask_preview_lru_cache_.size());
  }
  static int mask_group_cache_rebuild_count(const EditorPreviewController& controller) {
    return controller.mask_group_cache_rebuild_count_;
  }
  static int mask_thumbnail_refresh_count(const EditorPreviewController& controller) {
    return controller.mask_thumbnail_refresh_count_;
  }
  static int mask_thumbnail_refresh_deferred_count(const EditorPreviewController& controller) {
    return controller.mask_thumbnail_refresh_deferred_count_;
  }
  static bool mask_thumbnail_cache_dirty(const EditorPreviewController& controller) {
    return controller.mask_thumbnail_cache_dirty_;
  }
  static bool photo_layer_thumbnail_cache_dirty(const EditorPreviewController& controller) {
    return controller.photo_layer_thumbnail_cache_dirty_;
  }
  static int thumbnail_entry_cache_hit_count(const EditorPreviewController& controller) {
    return controller.thumbnail_entry_cache_hit_count_;
  }
  static int thumbnail_entry_cache_miss_count(const EditorPreviewController& controller) {
    return controller.thumbnail_entry_cache_miss_count_;
  }
  static int thumbnail_entry_cache_store_count(const EditorPreviewController& controller) {
    return controller.thumbnail_entry_cache_store_count_;
  }
  static int photo_layer_thumbnail_refresh_count(const EditorPreviewController& controller) {
    return controller.photo_layer_thumbnail_refresh_count_;
  }
  static int photo_layer_thumbnail_refresh_deferred_count(
      const EditorPreviewController& controller) {
    return controller.photo_layer_thumbnail_refresh_deferred_count_;
  }
  static int smart_selection_job_generation(const EditorPreviewController& controller) {
    return controller.smart_selection_job_generation_;
  }
  static int smart_selection_cancelled_count(const EditorPreviewController& controller) {
    return controller.smart_selection_cancelled_count_;
  }
  static int inpaint_job_generation(const EditorPreviewController& controller) {
    return controller.inpaint_job_generation_;
  }
  static int inpaint_cancelled_count(const EditorPreviewController& controller) {
    return controller.inpaint_cancelled_count_;
  }
  static AsyncMaskJob latest_refine_edge_preview_job(
      const EditorPreviewController& controller) {
    return controller.latest_refine_edge_preview_job_;
  }
  static void set_display_preview_native_rendered_cache_key(
      EditorPreviewController& controller,
      const QString& key) {
    controller.display_preview_native_rendered_cache_key_ = key;
  }
  static int interactive_render_cancelled_count(const EditorPreviewController& controller) {
    return controller.interactive_render_cancelled_count_;
  }
  static int full_render_cancelled_count(const EditorPreviewController& controller) {
    return controller.full_render_cancelled_count_;
  }
  static void request_render(
      EditorPreviewController& controller,
      bool schedule_full_resolution,
      const QString& reason) {
    controller.renderPreview(schedule_full_resolution, reason);
  }
  static void submit_edit_intent(EditorPreviewController& controller, const EditIntent& intent) {
    controller.submitEditIntent(intent);
  }
  static void start_render_stage(
      EditorPreviewController& controller,
      bool full_resolution,
      const RenderRequest& request) {
    controller.startRenderStage(full_resolution, request);
  }
  static void handle_render_result(
      EditorPreviewController& controller,
      bool full_resolution,
      const RenderResult& result) {
    controller.handleRenderResult(full_resolution, result);
  }
  static int last_interactive_preview_generation(const EditorPreviewController& controller) {
    return controller.last_interactive_preview_generation_;
  }
  static int last_full_preview_generation(const EditorPreviewController& controller) {
    return controller.last_full_preview_generation_;
  }
  static void start_histogram_update(
      EditorPreviewController& controller,
      const QImage& image,
      int generation) {
    controller.startHistogramUpdate(
        image,
        generation,
        false,
        0,
        0,
        0,
        QStringLiteral("test_histogram_async"),
        QStringLiteral("test"));
  }
  static void update_diagnostic_overlay_images(EditorPreviewController& controller, const QImage& image) {
    controller.updateDiagnosticOverlayImages(image);
  }
  static void clear_diagnostic_overlay_images(EditorPreviewController& controller) {
    controller.clearDiagnosticOverlayImages();
  }
  static void set_display_preview_texture(
      EditorPreviewController& controller,
      std::unique_ptr<EditorPreviewTexture> texture) {
    controller.display_preview_texture_ = std::move(texture);
    controller.display_preview_native_revision_ = 0;
    controller.display_preview_native_rendered_cache_key_.clear();
  }
  static QString display_preview_native_rendered_cache_key(
      const EditorPreviewController& controller) {
    return controller.display_preview_native_rendered_cache_key_;
  }
};

class FakeEditorPreviewTexture final : public EditorPreviewTexture {
	 public:
	  [[nodiscard]] bool available() const override { return true; }
	  [[nodiscard]] const char* backendName() const override { return "fake"; }
	  bool ensureNativeTexture(const QSize& size, QImage::Format format) override {
	    if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
	      return false;
	    }
	    texture_size_ = size;
	    texture_format_name_ = native_format_name_for_format(format);
	    texture_bytes_per_pixel_ = native_bytes_per_pixel_for_format(format);
	    return true;
	  }
	  bool upload(const QImage& image) override {
	    if (image.isNull()) {
	      return false;
	    }
	    texture_size_ = image.size();
	    texture_format_name_ = native_format_name_for_format(image.format());
	    texture_bytes_per_pixel_ = native_bytes_per_pixel_for_format(image.format());
    upload_sizes_.push_back(texture_size_);
    ++upload_count_;
    return true;
  }
  bool uploadRegion(const QImage& image, const QRect& region) override {
    if (image.isNull() || region.isEmpty() || image.size() != texture_size_) {
      return false;
    }
    ++partial_upload_count_;
    last_partial_upload_rect_ = region;
    return true;
  }
  bool uploadPatch(const QImage& image, const QRect& destination_region) override {
    if (image.isNull() || destination_region.isEmpty() ||
        destination_region.size() != image.size() || !texture_size_.isValid() ||
        destination_region.right() >= texture_size_.width() ||
        destination_region.bottom() >= texture_size_.height()) {
      return false;
    }
    ++partial_upload_count_;
    ++patch_upload_count_;
    last_patch_upload_rect_ = destination_region;
    return true;
  }
  void clear() override { texture_size_ = {}; }
  [[nodiscard]] bool hasNativeTexture() const override { return texture_size_.isValid(); }
  [[nodiscard]] quint64 nativeTextureHandle() const override { return hasNativeTexture() ? 1 : 0; }
  [[nodiscard]] QSize nativeTextureSize() const override { return texture_size_; }
  [[nodiscard]] const char* nativeTextureFormatName() const override {
    return hasNativeTexture() ? texture_format_name_ : "none";
  }
  [[nodiscard]] int nativeTextureBytesPerPixel() const override {
    return hasNativeTexture() ? texture_bytes_per_pixel_ : 0;
  }
  [[nodiscard]] int uploadCount() const { return upload_count_; }
  [[nodiscard]] int partialUploadCount() const { return partial_upload_count_; }
  [[nodiscard]] int patchUploadCount() const { return patch_upload_count_; }
  [[nodiscard]] int uploadCountForSize(const QSize& size) const {
    return static_cast<int>(std::count(upload_sizes_.cbegin(), upload_sizes_.cend(), size));
  }
  [[nodiscard]] QRect lastPartialUploadRect() const { return last_partial_upload_rect_; }
  [[nodiscard]] QRect lastPatchUploadRect() const { return last_patch_upload_rect_; }

	 private:
	  [[nodiscard]] static const char* native_format_name_for_format(QImage::Format format) {
	    return format == QImage::Format_RGBA16FPx4 ? "rgba16float" : "bgra8unorm";
	  }
	  [[nodiscard]] static int native_bytes_per_pixel_for_format(QImage::Format format) {
	    return format == QImage::Format_RGBA16FPx4 ? 8 : 4;
	  }

  QSize texture_size_;
  const char* texture_format_name_ = "none";
  int texture_bytes_per_pixel_ = 0;
  std::vector<QSize> upload_sizes_;
  int upload_count_ = 0;
  int partial_upload_count_ = 0;
  int patch_upload_count_ = 0;
  QRect last_partial_upload_rect_;
  QRect last_patch_upload_rect_;
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

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("EditorPreviewControllerTest"));

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("temporary runtime directory should be created");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");

  const auto metal_brush_preview_source =
      read_repo_text_file(QStringLiteral("apps/desktop/src/brush_preview_compositor_metal.mm"));
  if (metal_brush_preview_source.isEmpty() ||
      !metal_brush_preview_source.contains(QStringLiteral("composite_brush_dab")) ||
      !metal_brush_preview_source.contains(QStringLiteral("dispatchThreads")) ||
      metal_brush_preview_source.contains(QStringLiteral("waitUntilCompleted"))) {
    return fail("Metal live brush preview should batch command dispatch without blocking on GPU completion");
  }

  auto native_texture = photo_editor::desktop::EditorPreviewTexture::create();
  if (native_texture != nullptr && native_texture->available()) {
    QImage texture_probe(64, 64, QImage::Format_ARGB32);
    texture_probe.fill(QColor(32, 64, 96));
    if (native_texture->uploadRegion(texture_probe, QRect(16, 16, 16, 16))) {
      return fail("partial native texture upload should require an existing same-size texture");
    }
    if (!native_texture->upload(texture_probe) ||
        native_texture->nativeTextureSize() != texture_probe.size()) {
      return fail("full native texture upload should initialize the preview texture");
    }
    texture_probe.fill(QColor(96, 64, 32));
    if (!native_texture->uploadRegion(texture_probe, QRect(16, 16, 16, 16)) ||
        native_texture->nativeTextureSize() != texture_probe.size()) {
      return fail("partial native texture upload should update an existing same-size texture");
    }
    QImage texture_patch(16, 16, QImage::Format_ARGB32);
    texture_patch.fill(QColor(16, 96, 64));
    if (!native_texture->uploadPatch(texture_patch, QRect(24, 24, 16, 16)) ||
        native_texture->nativeTextureSize() != texture_probe.size()) {
      return fail("native texture patch upload should update an existing destination rect");
    }
    if (std::string(native_texture->nativeTextureFormatName()) != "bgra8unorm" ||
        native_texture->nativeTextureBytesPerPixel() != 4) {
      return fail("native texture should upload 8-bit preview images through the BGRA8 path");
    }
    QImage texture_half_probe(64, 64, QImage::Format_RGBA16FPx4);
    texture_half_probe.fill(QColor(48, 96, 144));
    if (!native_texture->upload(texture_half_probe) ||
        native_texture->nativeTextureSize() != texture_half_probe.size() ||
        std::string(native_texture->nativeTextureFormatName()) != "rgba16float" ||
        native_texture->nativeTextureBytesPerPixel() != 8) {
      return fail("native texture should preserve half-float preview uploads");
    }
    QImage texture_half_patch(16, 16, QImage::Format_RGBA16FPx4);
    texture_half_patch.fill(QColor(144, 96, 48));
    if (!native_texture->uploadPatch(texture_half_patch, QRect(24, 24, 16, 16)) ||
        std::string(native_texture->nativeTextureFormatName()) != "rgba16float" ||
        native_texture->nativeTextureBytesPerPixel() != 8) {
      return fail("native texture patch uploads should keep the half-float texture format");
    }
  }

  photo_editor::desktop::EditorPreviewController controller;
  const auto expected_selection_runtime =
      controller.aiSmartSelectionAvailable()
          ? QStringLiteral("SAM Smart Select")
          : (controller.subjectMaskAvailable() ? QStringLiteral("Vision Object Select")
                                               : QStringLiteral("Local Smart Select"));
  const auto expected_inpaint_runtime =
      controller.aiInpaintAvailable() ? QStringLiteral("LaMa Model Inpaint")
                                      : QStringLiteral("Local CPU Fill");
  const auto model_diagnostics = controller.aiModelDiagnostics();
  if (controller.smartSelectionRuntimeLabel() != expected_selection_runtime ||
      controller.inpaintRuntimeLabel() != expected_inpaint_runtime ||
      model_diagnostics.isEmpty() ||
      !model_diagnostics.contains(QStringLiteral("root:")) ||
      !model_diagnostics.contains(QStringLiteral("|manifest:")) ||
      !model_diagnostics.contains(QStringLiteral("|sam:")) ||
      !model_diagnostics.contains(QStringLiteral("|lama:")) ||
      !model_diagnostics.contains(QStringLiteral("|onnx:")) ||
      !model_diagnostics.contains(QStringLiteral("|vision:"))) {
    return fail("model runtime trust labels and diagnostics should match the actual selection and inpaint runtimes");
  }
  photo_editor::desktop::EditorPreviewController advanced_color_defaults_controller;
  if (advanced_color_defaults_controller.colorMixerModel() != QStringLiteral("HSL")) {
    return fail("default color mix model should match the visible HSL controls");
  }
  advanced_color_defaults_controller.setColorReplacementEnabled(true);
  advanced_color_defaults_controller.setSkinToneProtectionEnabled(true);
  advanced_color_defaults_controller.setColorHarmonyEnabled(true);
  if (!advanced_color_defaults_controller.colorReplacementEnabled() ||
      advanced_color_defaults_controller.colorReplacementStrength() < 50.0 ||
      !advanced_color_defaults_controller.skinToneProtectionEnabled() ||
      advanced_color_defaults_controller.skinToneProtectionStrength() < 50.0 ||
      !advanced_color_defaults_controller.colorHarmonyEnabled() ||
      advanced_color_defaults_controller.colorHarmonyStrength() < 30.0) {
    return fail("advanced color enable toggles should seed visible default strengths");
  }
  const auto wait_for_preview_idle = [](photo_editor::desktop::EditorPreviewController& preview_controller,
                                        int timeout_ms = 2000) {
    auto elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
      const auto layer_adjustment_timer_active =
          photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_timer_active(preview_controller);
      if (!preview_controller.processing() &&
          !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
              preview_controller) &&
          !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_running(
              preview_controller) &&
          !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
              preview_controller) &&
          !layer_adjustment_timer_active) {
        return true;
      }
      QThread::msleep(25);
      elapsed_ms += 25;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    const auto layer_adjustment_timer_active =
        photo_editor::desktop::EditorPreviewControllerTestAccess::
            photo_layer_adjustment_render_timer_active(preview_controller);
    return !preview_controller.processing() &&
           !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
               preview_controller) &&
            !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_running(
                preview_controller) &&
            !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
                preview_controller) &&
            !layer_adjustment_timer_active;
  };
  const auto wait_for_brush_commit = [](photo_editor::desktop::EditorPreviewController& preview_controller,
                                        int timeout_ms = 2000) {
    auto elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
      if (photo_editor::desktop::EditorPreviewControllerTestAccess::pending_brush_commit_count(
              preview_controller) == 0) {
        return true;
      }
      QThread::msleep(25);
      elapsed_ms += 25;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return photo_editor::desktop::EditorPreviewControllerTestAccess::pending_brush_commit_count(
               preview_controller) == 0;
  };
  const auto finish_brush_stroke = [&wait_for_brush_commit](
                                       photo_editor::desktop::EditorPreviewController& preview_controller) {
    preview_controller.endActiveBrushStroke();
    return wait_for_brush_commit(preview_controller);
  };
  const auto wait_for_thumbnail_queue_dispatch = [](
                                                     photo_editor::desktop::EditorPreviewController& preview_controller,
                                                     int minimum_dispatch_count,
                                                     int timeout_ms = 2000) {
    auto elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
      if (photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_refresh_queue_dispatched_count(preview_controller) >=
              minimum_dispatch_count &&
          !photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_refresh_queue_pending(preview_controller)) {
        return true;
      }
      QThread::msleep(25);
      elapsed_ms += 25;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return photo_editor::desktop::EditorPreviewControllerTestAccess::
               thumbnail_refresh_queue_dispatched_count(preview_controller) >=
               minimum_dispatch_count &&
           !photo_editor::desktop::EditorPreviewControllerTestAccess::
               thumbnail_refresh_queue_pending(preview_controller);
  };

  auto wait_for_histogram_idle = [](photo_editor::desktop::EditorPreviewController& preview_controller,
                                     int timeout_ms = 2000) {
    auto elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
      if (!preview_controller.histogramUpdateActive()) {
        return true;
      }
      QThread::msleep(25);
      elapsed_ms += 25;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return !preview_controller.histogramUpdateActive();
  };
  auto wait_for_sink_image = [](
      const photo_editor::desktop::TestImageSink& sink,
      const QString& key,
      int timeout_ms = 2000) {
    auto elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
      if (!sink.image(key).isNull()) {
        return true;
      }
      QThread::msleep(25);
      elapsed_ms += 25;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return !sink.image(key).isNull();
  };

  photo_editor::desktop::EditorPreviewController native_publish_controller;
  photo_editor::desktop::TestImageSink native_publish_sink;
  native_publish_controller.setImageProvider(&native_publish_sink);
  photo_editor::desktop::EditorPreviewControllerTestAccess::set_display_preview_texture(
      native_publish_controller,
      std::make_unique<photo_editor::desktop::FakeEditorPreviewTexture>());
  QImage native_publish_source(320, 240, QImage::Format_ARGB32);
  native_publish_source.fill(QColor(40, 80, 120));
  const auto native_publish_path = runtime_root.path() + QStringLiteral("/native-publish.png");
  if (!native_publish_source.save(native_publish_path)) {
    return fail("native publish test source should be saved");
  }
  native_publish_controller.loadAsset(
      QStringLiteral("native-publish"),
      native_publish_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(native_publish_controller) ||
      !native_publish_controller.displayPreviewNativeAvailable() ||
      native_publish_controller.displayPreviewNativeRevision() <= 0 ||
      native_publish_sink.image(QStringLiteral("edited")).isNull()) {
    return fail("native preview upload should keep an edited image-provider publication for QML fallback paths");
  }
  const auto target_edge_generation_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          native_publish_controller);
  const auto target_edge_intent_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
          native_publish_controller);
  native_publish_controller.setPreviewTargetEdge(1800);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  if (native_publish_controller.previewTargetEdge() != 1800 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          native_publish_controller) != target_edge_generation_before ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
          native_publish_controller) != target_edge_intent_count_before ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_timer_active(
          native_publish_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_pending(
          native_publish_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_running(
          native_publish_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          native_publish_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          native_publish_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_running(
          native_publish_controller)) {
    return fail("layout-only preview target edge changes should not submit render work");
  }
  native_publish_controller.setExposure(0.25);
  const auto full_restore_active_before_viewport =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          native_publish_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          native_publish_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_running(
          native_publish_controller);
  if (!full_restore_active_before_viewport) {
    return fail("exposure change should schedule a full-resolution restore before viewport test");
  }
  native_publish_controller.setPreviewViewport(0.10, 0.10, 0.50, 0.50, 512, true);
  const auto full_restore_active_after_viewport =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          native_publish_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          native_publish_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_running(
          native_publish_controller);
  if (!full_restore_active_after_viewport) {
    return fail("layout-only viewport changes should not cancel full-resolution restore work");
  }
  if (!wait_for_preview_idle(native_publish_controller, 4000)) {
    return fail("native publish controller should settle after viewport restore guard test");
  }

  photo_editor::desktop::EditorPreviewController zoom_native_controller;
  photo_editor::desktop::TestImageSink zoom_native_sink;
  zoom_native_controller.setImageProvider(&zoom_native_sink);
  auto zoom_native_texture =
      std::make_unique<photo_editor::desktop::FakeEditorPreviewTexture>();
  auto* zoom_native_texture_ptr = zoom_native_texture.get();
  photo_editor::desktop::EditorPreviewControllerTestAccess::set_display_preview_texture(
      zoom_native_controller,
      std::move(zoom_native_texture));
  QImage full_zoom_preview(4000, 3000, QImage::Format_ARGB32);
  full_zoom_preview.fill(QColor(180, 64, 48));
  full_zoom_preview.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
  photo_editor::desktop::RenderResult full_zoom_result;
  full_zoom_result.success = true;
  full_zoom_result.full_resolution = true;
  full_zoom_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          zoom_native_controller);
  full_zoom_result.render_mode = QStringLiteral("max_preview");
  full_zoom_result.render_reason = QStringLiteral("edit_change");
  full_zoom_result.preview_image = full_zoom_preview;
  full_zoom_result.output_width = full_zoom_preview.width();
  full_zoom_result.output_height = full_zoom_preview.height();
  full_zoom_result.rendered_cache_key = QStringLiteral("full-zoom-rendered-cache");
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      zoom_native_controller,
      true,
      full_zoom_result);
  if (!zoom_native_controller.displayPreviewNativeAvailable() ||
      zoom_native_controller.displayPreviewNativeSize() != full_zoom_preview.size() ||
      zoom_native_texture_ptr->uploadCountForSize(full_zoom_preview.size()) != 1 ||
      zoom_native_sink.image(QStringLiteral("edited")).size() != full_zoom_preview.size() ||
      zoom_native_sink.image(QStringLiteral("edited")).colorSpace() !=
          QColorSpace(QColorSpace::DisplayP3)) {
    std::cerr << "full zoom native available="
              << zoom_native_controller.displayPreviewNativeAvailable()
              << " nativeSize="
              << zoom_native_controller.displayPreviewNativeSize().width() << "x"
              << zoom_native_controller.displayPreviewNativeSize().height()
              << " uploadFull="
              << zoom_native_texture_ptr->uploadCountForSize(full_zoom_preview.size())
              << " sink="
              << zoom_native_sink.image(QStringLiteral("edited")).width() << "x"
              << zoom_native_sink.image(QStringLiteral("edited")).height()
              << '\n';
    return fail("full-resolution max previews should publish the native zoom texture and preserve preview color space");
  }
  const auto full_zoom_revision = zoom_native_controller.displayPreviewNativeRevision();
  const auto full_zoom_upload_count = zoom_native_texture_ptr->uploadCount();
  zoom_native_controller.setPreviewViewport(0.25, 0.25, 0.25, 0.25, 900, true);
  QImage low_resolution_interactive(1200, 900, QImage::Format_ARGB32);
  low_resolution_interactive.fill(QColor(16, 32, 48));
  low_resolution_interactive.setColorSpace(QColorSpace(QColorSpace::SRgb));
  photo_editor::desktop::RenderResult low_resolution_result;
  low_resolution_result.success = true;
  low_resolution_result.full_resolution = false;
  low_resolution_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          zoom_native_controller);
  low_resolution_result.render_mode = QStringLiteral("settled_preview");
  low_resolution_result.render_reason = QStringLiteral("viewport_change");
  low_resolution_result.preview_image = low_resolution_interactive;
  low_resolution_result.output_width = low_resolution_interactive.width();
  low_resolution_result.output_height = low_resolution_interactive.height();
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      zoom_native_controller,
      false,
      low_resolution_result);
  if (zoom_native_controller.displayPreviewNativeSize() != full_zoom_preview.size() ||
      zoom_native_controller.displayPreviewNativeRevision() != full_zoom_revision ||
      zoom_native_texture_ptr->uploadCount() != full_zoom_upload_count) {
    std::cerr << "preserved native size="
              << zoom_native_controller.displayPreviewNativeSize().width() << "x"
              << zoom_native_controller.displayPreviewNativeSize().height()
              << " revisionBefore=" << full_zoom_revision
              << " revisionAfter=" << zoom_native_controller.displayPreviewNativeRevision()
              << " uploadsBefore=" << full_zoom_upload_count
              << " uploadsAfter=" << zoom_native_texture_ptr->uploadCount()
              << '\n';
    return fail("zoomed lower-resolution interactive previews should not replace an existing full-resolution native texture");
  }
  zoom_native_controller.setPreviewViewport(0, 0, 0, 0, 0, false);
  QImage low_resolution_tool_switch(1600, 1200, QImage::Format_ARGB32);
  low_resolution_tool_switch.fill(QColor(24, 48, 72));
  photo_editor::desktop::RenderResult low_resolution_tool_switch_result;
  low_resolution_tool_switch_result.success = true;
  low_resolution_tool_switch_result.full_resolution = false;
  low_resolution_tool_switch_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          zoom_native_controller);
  low_resolution_tool_switch_result.render_mode = QStringLiteral("settled_preview");
  low_resolution_tool_switch_result.render_reason = QStringLiteral("compare_toggle");
  low_resolution_tool_switch_result.preview_image = low_resolution_tool_switch;
  low_resolution_tool_switch_result.output_width = low_resolution_tool_switch.width();
  low_resolution_tool_switch_result.output_height = low_resolution_tool_switch.height();
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      zoom_native_controller,
      false,
      low_resolution_tool_switch_result);
  if (zoom_native_controller.displayPreviewNativeSize() != full_zoom_preview.size() ||
      zoom_native_controller.displayPreviewNativeRevision() != full_zoom_revision ||
      zoom_native_texture_ptr->uploadCount() != full_zoom_upload_count) {
    return fail("lower-resolution tool-switch previews should not downgrade an existing full-resolution native texture");
  }
  const auto layer_edit_revision_before = zoom_native_controller.displayPreviewNativeRevision();
  const auto layer_edit_upload_count_before = zoom_native_texture_ptr->uploadCount();
  QImage low_resolution_layer_edit(1000, 750, QImage::Format_ARGB32);
  low_resolution_layer_edit.fill(QColor(220, 96, 32));
  photo_editor::desktop::RenderResult low_resolution_layer_edit_result;
  low_resolution_layer_edit_result.success = true;
  low_resolution_layer_edit_result.full_resolution = false;
  low_resolution_layer_edit_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          zoom_native_controller);
  low_resolution_layer_edit_result.render_mode = QStringLiteral("settled_preview");
  low_resolution_layer_edit_result.render_reason = QStringLiteral("photo_layer_change");
  low_resolution_layer_edit_result.dirty_domain = QStringLiteral("layers_composite");
  low_resolution_layer_edit_result.dirty_stages =
      QStringList{QStringLiteral("layers_composite")};
  low_resolution_layer_edit_result.preview_image = low_resolution_layer_edit;
  low_resolution_layer_edit_result.output_width = low_resolution_layer_edit.width();
  low_resolution_layer_edit_result.output_height = low_resolution_layer_edit.height();
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      zoom_native_controller,
      false,
      low_resolution_layer_edit_result);
  if (zoom_native_controller.displayPreviewNativeSize() !=
          low_resolution_layer_edit.size() ||
      zoom_native_controller.displayPreviewNativeRevision() <=
          layer_edit_revision_before ||
      zoom_native_texture_ptr->uploadCount() != layer_edit_upload_count_before + 1) {
    std::cerr << "layer edit native size="
              << zoom_native_controller.displayPreviewNativeSize().width() << "x"
              << zoom_native_controller.displayPreviewNativeSize().height()
              << " revisionBefore=" << layer_edit_revision_before
              << " revisionAfter=" << zoom_native_controller.displayPreviewNativeRevision()
              << " uploadsBefore=" << layer_edit_upload_count_before
              << " uploadsAfter=" << zoom_native_texture_ptr->uploadCount()
              << '\n';
    return fail("layer-composite edits should publish their lower-resolution preview instead of preserving stale full-resolution texture");
  }

  photo_editor::desktop::EditorPreviewController raw_availability_controller;
  raw_availability_controller.loadAsset(
      QStringLiteral("jpeg-asset"),
      QStringLiteral("/tmp/IMG_0001.jpg"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (raw_availability_controller.rawControlsAvailable()) {
    return fail("JPEG assets should not report RAW control availability");
  }
  const auto raw_test_path = QStringLiteral("/tmp/IMG_0001.dng");
  if (photo_editor::desktop::raw_image_supported(raw_test_path)) {
    raw_availability_controller.loadAsset(
        QStringLiteral("raw-asset"),
        raw_test_path,
        QString(),
        QString(),
        QString(),
        QString(),
        QString());
    if (!raw_availability_controller.rawControlsAvailable()) {
      return fail("RAW assets should report RAW control availability when the decoder supports the format");
    }
  }

  photo_editor::desktop::EditorPreviewController histogram_async_controller;
  QImage histogram_async_source(320, 240, QImage::Format_ARGB32);
  for (int y = 0; y < histogram_async_source.height(); ++y) {
    for (int x = 0; x < histogram_async_source.width(); ++x) {
      histogram_async_source.setPixelColor(
          x,
          y,
          QColor::fromRgb(
              (x * 3) % 256,
              (y * 5) % 256,
              ((x + y) * 7) % 256));
    }
  }
  const auto histogram_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          histogram_async_controller);
  QElapsedTimer histogram_queue_timer;
  histogram_queue_timer.start();
  photo_editor::desktop::EditorPreviewControllerTestAccess::start_histogram_update(
      histogram_async_controller,
      histogram_async_source,
      histogram_generation);
  if (histogram_queue_timer.elapsed() > 100 || !histogram_async_controller.histogramUpdateActive() ||
      histogram_async_controller.processing()) {
    return fail("histogram and scope updates should queue asynchronously without blocking the UI thread");
  }
  if (histogram_async_controller.histogramSampleCount() <= 0 ||
      histogram_async_controller.histogramSampleCount() >=
          histogram_async_source.width() * histogram_async_source.height() ||
      histogram_async_controller.histogramRed().isEmpty()) {
    return fail("histogram updates should publish an immediate low-resolution preview before exact async settle");
  }
  if (!wait_for_histogram_idle(histogram_async_controller) ||
      histogram_async_controller.histogramSampleCount() <= 0 ||
      histogram_async_controller.histogramRed().isEmpty() ||
      histogram_async_controller.scopeWaveformLuminance().isEmpty() ||
      histogram_async_controller.scopeVectorscopePoints().isEmpty()) {
    return fail("asynchronous histogram and scope updates should publish completed scope data");
  }
  if (histogram_async_controller.histogramSampleCount() !=
      histogram_async_source.width() * histogram_async_source.height()) {
    return fail("asynchronous histogram settle should replace the immediate preview with exact full-image data");
  }
  const auto settled_red_counts = histogram_async_controller.histogramRedCounts();
  QImage stale_histogram_source(histogram_async_source.size(), QImage::Format_ARGB32);
  stale_histogram_source.fill(QColor::fromRgb(255, 0, 0));
  photo_editor::desktop::EditorPreviewControllerTestAccess::start_histogram_update(
      histogram_async_controller,
      stale_histogram_source,
      histogram_generation + 1);
  if (histogram_async_controller.histogramRedCounts() != settled_red_counts) {
    return fail("stale histogram generations should not overwrite the current immediate histogram preview");
  }
  if (!wait_for_histogram_idle(histogram_async_controller) ||
      histogram_async_controller.histogramRedCounts() != settled_red_counts) {
    return fail("stale histogram generations should not overwrite the settled current histogram");
  }
  auto histogram_count_sum = [](const QVariantList& bins) {
    qint64 sum = 0;
    for (const auto& bin : bins) {
      sum += bin.toLongLong();
    }
    return sum;
  };
  auto image_channel_counts = [](const QImage& source, char channel) {
    std::array<int, 256> bins{};
    const auto image = source.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
      const auto* scanline = reinterpret_cast<const QRgb*>(image.constScanLine(y));
      for (int x = 0; x < image.width(); ++x) {
        const auto pixel = scanline[x];
        const auto r = qRed(pixel);
        const auto g = qGreen(pixel);
        const auto b = qBlue(pixel);
        const auto value =
            channel == 'r'
                ? r
                : (channel == 'g'
                       ? g
                       : (channel == 'b'
                              ? b
                              : std::clamp(
                                    static_cast<int>(0.2126 * r + 0.7152 * g + 0.0722 * b + 0.5),
                                    0,
                                    255)));
        ++bins[static_cast<std::size_t>(value)];
      }
    }
    QVariantList counts;
    counts.reserve(static_cast<int>(bins.size()));
    for (const auto count : bins) {
      counts.append(count);
    }
    return counts;
  };
  auto scope_column_average = [](const QImage& source, int column, char channel) {
    const auto image = source.convertToFormat(QImage::Format_ARGB32);
    double sum = 0.0;
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
      const auto* scanline = reinterpret_cast<const QRgb*>(image.constScanLine(y));
      for (int x = 0; x < image.width(); ++x) {
        const auto scope_index = std::clamp(
            static_cast<int>((static_cast<double>(x) / static_cast<double>(std::max(1, image.width()))) *
                             128.0),
            0,
            127);
        if (scope_index != column) {
          continue;
        }
        const auto pixel = scanline[x];
        const auto r = qRed(pixel);
        const auto g = qGreen(pixel);
        const auto b = qBlue(pixel);
        const auto value =
            channel == 'r'
                ? r / 255.0
                : (channel == 'g'
                       ? g / 255.0
                       : (channel == 'b'
                              ? b / 255.0
                              : std::clamp(
                                    static_cast<int>(0.2126 * r + 0.7152 * g + 0.0722 * b + 0.5),
                                    0,
                                    255) /
                                    255.0));
        sum += value;
        ++count;
      }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
  };
  const auto histogram_sample_count = histogram_async_controller.histogramSampleCount();
  if (histogram_async_controller.histogramRedCounts().size() != 256 ||
      histogram_async_controller.histogramGreenCounts().size() != 256 ||
      histogram_async_controller.histogramBlueCounts().size() != 256 ||
      histogram_async_controller.histogramLuminanceCounts().size() != 256 ||
      histogram_count_sum(histogram_async_controller.histogramRedCounts()) != histogram_sample_count ||
      histogram_count_sum(histogram_async_controller.histogramGreenCounts()) != histogram_sample_count ||
      histogram_count_sum(histogram_async_controller.histogramBlueCounts()) != histogram_sample_count ||
      histogram_count_sum(histogram_async_controller.histogramLuminanceCounts()) != histogram_sample_count) {
    return fail("histogram raw bin counts should sum exactly to the sampled pixel count for every channel");
  }
  photo_editor::desktop::EditorPreviewController high_precision_scope_controller;
  QImage high_precision_scope_source(128, 1, QImage::Format_RGBA64);
  const auto sub_8bit_red = 1.0 / 4095.0;
  high_precision_scope_source.fill(QColor::fromRgbF(static_cast<float>(sub_8bit_red), 0.0F, 0.0F, 1.0F));
  photo_editor::desktop::EditorPreviewControllerTestAccess::compute_histogram(
      high_precision_scope_controller,
      high_precision_scope_source);
  const auto high_precision_waveform_red = high_precision_scope_controller.scopeWaveformRed();
  if (high_precision_waveform_red.size() != 128 ||
      !(high_precision_waveform_red[0].toMap().value(QStringLiteral("avg")).toDouble() > 0.0) ||
      !(high_precision_waveform_red[0].toMap().value(QStringLiteral("avg")).toDouble() < (1.0 / 255.0))) {
    return fail("histogram scopes should sample 16-bit image data before 8-bit display quantization");
  }

  photo_editor::desktop::EditorPreviewController diagnostic_overlay_controller;
  photo_editor::desktop::TestImageSink diagnostic_overlay_sink;
  diagnostic_overlay_controller.setImageProvider(&diagnostic_overlay_sink);
  QImage diagnostic_source(64, 64, QImage::Format_ARGB32);
  for (int y = 0; y < diagnostic_source.height(); ++y) {
    for (int x = 0; x < diagnostic_source.width(); ++x) {
      if (x < 16) {
        diagnostic_source.setPixelColor(x, y, QColor::fromRgb(0, 0, 0));
      } else if (x < 32) {
        diagnostic_source.setPixelColor(x, y, QColor::fromRgb(255, 255, 255));
      } else if (x < 48) {
        diagnostic_source.setPixelColor(x, y, QColor::fromRgb(250, 20, 20));
      } else {
        diagnostic_source.setPixelColor(x, y, QColor::fromRgb(96, 128, 160));
      }
    }
  }
  photo_editor::desktop::EditorPreviewControllerTestAccess::update_diagnostic_overlay_images(
      diagnostic_overlay_controller,
      diagnostic_source);
  const auto focus_overlay = diagnostic_overlay_sink.image(QStringLiteral("diagnostic-focus-check"));
  const auto peaking_overlay = diagnostic_overlay_sink.image(QStringLiteral("diagnostic-focus-peaking"));
  const auto clipping_overlay = diagnostic_overlay_sink.image(QStringLiteral("diagnostic-clipping"));
  const auto false_color_overlay = diagnostic_overlay_sink.image(QStringLiteral("diagnostic-false-color"));
  const auto gamut_overlay = diagnostic_overlay_sink.image(QStringLiteral("diagnostic-gamut"));
  const auto zone_overlay = diagnostic_overlay_sink.image(QStringLiteral("diagnostic-zone-map"));
  if (focus_overlay.size() != diagnostic_source.size() ||
      peaking_overlay.size() != diagnostic_source.size() ||
      clipping_overlay.size() != diagnostic_source.size() ||
      false_color_overlay.size() != diagnostic_source.size() ||
      gamut_overlay.size() != diagnostic_source.size() ||
      zone_overlay.size() != diagnostic_source.size() ||
      !diagnostic_overlay_controller.focusCheckOverlaySource().contains(QStringLiteral("diagnostic-focus-check")) ||
      !diagnostic_overlay_controller.clippingOverlaySource().contains(QStringLiteral("diagnostic-clipping")) ||
      qAlpha(focus_overlay.pixel(16, 32)) == 0 ||
      qAlpha(peaking_overlay.pixel(16, 32)) == 0 ||
      qBlue(clipping_overlay.pixel(4, 32)) <= qRed(clipping_overlay.pixel(4, 32)) ||
      qRed(clipping_overlay.pixel(20, 32)) <= qBlue(clipping_overlay.pixel(20, 32)) ||
      false_color_overlay.pixel(4, 32) == false_color_overlay.pixel(20, 32) ||
      qAlpha(gamut_overlay.pixel(40, 32)) == 0 ||
      zone_overlay.pixel(4, 32) == zone_overlay.pixel(20, 32)) {
    return fail("diagnostic overlays should be generated from rendered preview pixels, not static decorative patterns");
  }
  photo_editor::desktop::EditorPreviewControllerTestAccess::clear_diagnostic_overlay_images(
      diagnostic_overlay_controller);
  if (!diagnostic_overlay_controller.focusCheckOverlaySource().isEmpty() ||
      !diagnostic_overlay_sink.image(QStringLiteral("diagnostic-focus-check")).isNull()) {
    return fail("diagnostic overlays should clear their image-provider sources when preview pixels are unavailable");
  }

  photo_editor::desktop::EditorPreviewController missing_preview_controller;
  missing_preview_controller.loadAsset(
      QStringLiteral("missing-preview-source"),
      runtime_root.filePath(QStringLiteral("missing-preview-source.jpg")),
      QString(),
      QStringLiteral("Test Camera"),
      QStringLiteral("Apr 8, 2026"),
      QString(),
      QString());
  if (missing_preview_controller.status() != QStringLiteral("Source file unavailable.") ||
      missing_preview_controller.previewFailureStage() != QStringLiteral("source_decode") ||
      missing_preview_controller.previewFailureDetail() != QStringLiteral("Source file unavailable.") ||
      missing_preview_controller.processing()) {
    return fail("missing preview source should settle as a nonblocking retryable source-decode failure");
  }
  if (!missing_preview_controller.retryPreviewRender() ||
      missing_preview_controller.status() != QStringLiteral("Source file unavailable.") ||
      missing_preview_controller.previewFailureStage() != QStringLiteral("source_decode") ||
      missing_preview_controller.previewFailureDetail() != QStringLiteral("Source file unavailable.") ||
      missing_preview_controller.processing()) {
    return fail("preview render retry should preserve source-decode failure metadata without opening a blocking state");
  }

  photo_editor::desktop::EditorPreviewController rendered_scope_controller;
  photo_editor::desktop::TestImageSink rendered_scope_sink;
  rendered_scope_controller.setImageProvider(&rendered_scope_sink);
  QImage rendered_scope_source(960, 240, QImage::Format_ARGB32);
  for (int y = 0; y < rendered_scope_source.height(); ++y) {
    for (int x = 0; x < rendered_scope_source.width(); ++x) {
      rendered_scope_source.setPixelColor(
          x,
          y,
          QColor::fromRgb(
              24 + ((x * 96) / std::max(1, rendered_scope_source.width() - 1)),
              36 + ((y * 92) / std::max(1, rendered_scope_source.height() - 1)),
              72 + (((x + y) * 52) /
                    std::max(1, rendered_scope_source.width() + rendered_scope_source.height() - 2))));
    }
  }
  const auto rendered_scope_source_counts = image_channel_counts(rendered_scope_source, 'r');
  const auto rendered_scope_path = runtime_root.filePath(QStringLiteral("rendered-scope-source.png"));
  if (!rendered_scope_source.save(rendered_scope_path)) {
    return fail("rendered scope source image should be writable");
  }
  rendered_scope_controller.loadAsset(
      QStringLiteral("rendered-scope-source"),
      rendered_scope_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(rendered_scope_controller)) {
    return fail("rendered scope controller should finish its initial preview");
  }
  rendered_scope_controller.setExposure(1.20);
  if (!wait_for_preview_idle(rendered_scope_controller, 4000)) {
    return fail("rendered scope controller should finish the adjusted preview");
  }
  if (!wait_for_histogram_idle(rendered_scope_controller, 4000)) {
    return fail("rendered scope controller should finish the adjusted histogram update");
  }
  const auto rendered_scope_preview = rendered_scope_sink.image(QStringLiteral("edited"));
  if (rendered_scope_preview.isNull()) {
    return fail("rendered scope image sink should receive the adjusted preview");
  }
  const auto last_good_preview_source = rendered_scope_controller.displayPreviewSource();
  const auto last_good_preview_hash = image_hash(rendered_scope_preview);
  auto failed_layer_result = photo_editor::desktop::RenderResult{};
  failed_layer_result.file_path = rendered_scope_path;
  failed_layer_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          rendered_scope_controller);
  failed_layer_result.dirty_domain = QStringLiteral("layers_composite");
  failed_layer_result.dirty_stages = {QStringLiteral("layers_composite")};
  failed_layer_result.render_mode = QStringLiteral("settled_preview");
  failed_layer_result.mask_alpha_trace_fields.insert(
      QStringLiteral("layer_composite_layer_ids"),
      QVariantList{QStringLiteral("failed-layer-1")});
  failed_layer_result.mask_alpha_trace_fields.insert(
      QStringLiteral("layer_composite_mask_set_ids"),
      QVariantList{QStringLiteral("failed-mask-set-1")});
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      rendered_scope_controller,
      false,
      failed_layer_result);
  if (rendered_scope_controller.previewFailureStage() != QStringLiteral("layer_composite") ||
      !rendered_scope_controller.previewFailureDetail().contains(QStringLiteral("layer_composite")) ||
      rendered_scope_controller.previewFailureLayerIds() !=
          QStringList{QStringLiteral("failed-layer-1")} ||
      rendered_scope_controller.previewFailureMaskSetIds() !=
          QStringList{QStringLiteral("failed-mask-set-1")} ||
      rendered_scope_controller.status() != QStringLiteral("Preview failed: Layer composite.") ||
      rendered_scope_controller.displayPreviewSource() != last_good_preview_source ||
      image_hash(rendered_scope_sink.image(QStringLiteral("edited"))) != last_good_preview_hash) {
    return fail("failed layer-composite previews should keep the last good canvas and expose the layer stage");
  }
  auto failed_mask_result = photo_editor::desktop::RenderResult{};
  failed_mask_result.file_path = rendered_scope_path;
  failed_mask_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          rendered_scope_controller);
  failed_mask_result.dirty_domain = QStringLiteral("mask_rasterization");
  failed_mask_result.dirty_stages = {QStringLiteral("mask_rasterization")};
  failed_mask_result.render_mode = QStringLiteral("live_preview");
  failed_mask_result.mask_alpha_trace_fields.insert(
      QStringLiteral("mask_alpha_trace_hash"),
      QStringLiteral("sha1:test-mask-alpha"));
  failed_mask_result.mask_alpha_trace_fields.insert(
      QStringLiteral("mask_set_id"),
      QStringLiteral("failed-mask-set-2"));
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      rendered_scope_controller,
      false,
      failed_mask_result);
  if (rendered_scope_controller.previewFailureStage() != QStringLiteral("mask_alpha") ||
      !rendered_scope_controller.previewFailureDetail().contains(QStringLiteral("mask_alpha")) ||
      !rendered_scope_controller.previewFailureDetail().contains(QStringLiteral("sha1:test-mask-alpha")) ||
      !rendered_scope_controller.previewFailureLayerIds().isEmpty() ||
      rendered_scope_controller.previewFailureMaskSetIds() !=
          QStringList{QStringLiteral("failed-mask-set-2")} ||
      rendered_scope_controller.status() != QStringLiteral("Preview failed: Mask alpha.") ||
      rendered_scope_controller.displayPreviewSource() != last_good_preview_source ||
      image_hash(rendered_scope_sink.image(QStringLiteral("edited"))) != last_good_preview_hash) {
    std::cerr << "mask failure stage="
              << rendered_scope_controller.previewFailureStage().toStdString()
              << " detail="
              << rendered_scope_controller.previewFailureDetail().toStdString()
              << " layerIds="
              << rendered_scope_controller.previewFailureLayerIds().join(',').toStdString()
              << " maskSetIds="
              << rendered_scope_controller.previewFailureMaskSetIds().join(',').toStdString()
              << " sourceChanged="
              << (rendered_scope_controller.displayPreviewSource() != last_good_preview_source)
              << " hashChanged="
              << (image_hash(rendered_scope_sink.image(QStringLiteral("edited"))) !=
                  last_good_preview_hash)
              << '\n';
    return fail("failed mask-alpha previews should keep the canvas and expose the exact mask failure stage");
  }
  photo_editor::desktop::EditorPreviewControllerTestAccess::start_histogram_update(
      rendered_scope_controller,
      QImage(),
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          rendered_scope_controller));
  if (rendered_scope_controller.previewFailureStage() != QStringLiteral("mask_alpha") ||
      rendered_scope_controller.displayPreviewSource() != last_good_preview_source ||
      image_hash(rendered_scope_sink.image(QStringLiteral("edited"))) != last_good_preview_hash) {
    return fail("contained histogram failures should not replace the canvas preview failure state or pixels");
  }
  if (rendered_scope_controller.histogramSampleCount() !=
      rendered_scope_preview.width() * rendered_scope_preview.height()) {
    return fail("rendered preview scopes should report the adjusted preview pixel count");
  }
  if (rendered_scope_controller.histogramRedCounts() !=
          image_channel_counts(rendered_scope_preview, 'r') ||
      rendered_scope_controller.histogramGreenCounts() !=
          image_channel_counts(rendered_scope_preview, 'g') ||
      rendered_scope_controller.histogramBlueCounts() !=
          image_channel_counts(rendered_scope_preview, 'b') ||
      rendered_scope_controller.histogramLuminanceCounts() !=
          image_channel_counts(rendered_scope_preview, 'l')) {
    return fail("scope histograms should be computed from the rendered preview image");
  }
  if (rendered_scope_controller.histogramRedCounts() == rendered_scope_source_counts) {
    return fail("scope histograms should not be computed from the stale source image after edits");
  }
  const auto rendered_waveform_red = rendered_scope_controller.scopeWaveformRed();
  constexpr int kVerifiedScopeColumn = 64;
  if (rendered_waveform_red.size() != 128 ||
      !almost_equal(
          rendered_waveform_red[kVerifiedScopeColumn].toMap().value(QStringLiteral("avg")).toDouble(),
          scope_column_average(rendered_scope_preview, kVerifiedScopeColumn, 'r'))) {
    return fail("scope waveform averages should derive from the rendered preview image");
  }
  auto overlay_only_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          rendered_scope_controller);
  overlay_only_request.overlay_only = true;
  overlay_only_request.use_in_memory_preview = true;
  overlay_only_request.preview_overlay_rect = QRectF(0.125, 0.125, 0.50, 0.50);
  overlay_only_request.preview_overlay_max_edge = 128;
  overlay_only_request.render_reason = QStringLiteral("overlay_scope_isolation_test");
  overlay_only_request.state.output_color_space = QStringLiteral("Display P3");
  overlay_only_request.state.preview_color_space = QStringLiteral("Display P3");
  overlay_only_request.state.monitor_color_profile = QStringLiteral("Display P3");
  const auto overlay_only_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          overlay_only_request);
  if (!overlay_only_result.success || !overlay_only_result.overlay_only ||
      overlay_only_result.preview_overlay_image.isNull() ||
      overlay_only_result.display_preview_overlay_source.isEmpty()) {
    return fail("overlay-only preview requests should publish a standalone display overlay patch");
  }
  if (!overlay_only_result.histogram_image.isNull()) {
    return fail("overlay-only preview requests should not publish histogram input images");
  }
  if (!overlay_only_result.preview_overlay_image.colorSpace().isValid() ||
      overlay_only_result.preview_overlay_image.colorSpace() !=
          QColorSpace(QColorSpace::DisplayP3)) {
    return fail("overlay-only preview patches should be transformed into the active display color space");
  }
  auto queued_overlay_request = overlay_only_request;
  queued_overlay_request.render_mode = QStringLiteral("visible_overlay");
  queued_overlay_request.render_priority = 0;
  queued_overlay_request.requested_at_msecs =
      QDateTime::currentMSecsSinceEpoch() - 250;
  const auto visible_overlay_budget_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          visible_overlay_queue_wait_exceeded_count(rendered_scope_controller);
  photo_editor::desktop::EditorPreviewControllerTestAccess::start_render_stage(
      rendered_scope_controller,
      false,
      queued_overlay_request);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::
              visible_overlay_queue_wait_exceeded_count(rendered_scope_controller) !=
          visible_overlay_budget_count_before + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              last_visible_overlay_queue_wait_ms(rendered_scope_controller) < 100) {
    return fail("visible overlay renders should expose over-budget queue wait telemetry before running");
  }
  if (!wait_for_preview_idle(rendered_scope_controller, 4000)) {
    return fail("visible overlay queue budget probe should finish its overlay render");
  }
  const auto viewport_overlay_budget_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          visible_overlay_queue_wait_exceeded_count(rendered_scope_controller);
  rendered_scope_controller.setPreviewViewport(0.20, 0.20, 0.40, 0.40, 128, true);
  const auto viewport_overlay_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_overlay_render_request(
          rendered_scope_controller);
  if (!viewport_overlay_request.overlay_only ||
      viewport_overlay_request.render_reason != QStringLiteral("viewport_change") ||
      viewport_overlay_request.dirty_domain != QStringLiteral("publish_texture") ||
      viewport_overlay_request.dirty_stages.isEmpty() ||
      viewport_overlay_request.dirty_stages.first() != QStringLiteral("publish_texture") ||
      viewport_overlay_request.full_invalidation) {
    return fail("viewport overlay refreshes should not inherit the previous edit render reason or dirty graph");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_pending(
          rendered_scope_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_running(
          rendered_scope_controller)) {
    return fail("visible overlay viewport updates should use the overlay render lane, not the interactive preview lane");
  }
  if (!wait_for_preview_idle(rendered_scope_controller, 4000) ||
      rendered_scope_controller.displayPreviewOverlaySource().isEmpty() ||
      !rendered_scope_controller.displayPreviewOverlayRect().isValid()) {
    return fail("visible overlay viewport updates should publish through the dedicated overlay render lane");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::
              visible_overlay_queue_wait_exceeded_count(rendered_scope_controller) !=
      viewport_overlay_budget_count_before) {
    return fail("fresh visible overlay viewport updates should not inherit stale render queue timing");
  }

  photo_editor::desktop::EditorPreviewController retry_tool_controller;
  photo_editor::desktop::TestImageSink retry_tool_sink;
  retry_tool_controller.setImageProvider(&retry_tool_sink);
  retry_tool_controller.loadAsset(
      QStringLiteral("retry-tool-source"),
      rendered_scope_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  retry_tool_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  retry_tool_controller.startSelectionBrush(false);
  if (!wait_for_preview_idle(retry_tool_controller, 4000) ||
      retry_tool_controller.activeMaskKind() != QStringLiteral("brush") ||
      retry_tool_controller.activeMaskToolRole() != QStringLiteral("selection") ||
      retry_tool_controller.maskPreviewMode() != QStringLiteral("overlay")) {
    return fail("retry failure fixture should start with an active brush mask tool context");
  }
  const auto retry_tool_preview = retry_tool_sink.image(QStringLiteral("edited"));
  if (retry_tool_preview.isNull()) {
    return fail("retry failure fixture should publish a last-good preview before failure injection");
  }
  const auto retry_tool_preview_source = retry_tool_controller.displayPreviewSource();
  const auto retry_tool_preview_hash = image_hash(retry_tool_preview);
  const auto retry_tool_active_mask_index = retry_tool_controller.activeMaskIndex();
  auto retry_tool_failure = photo_editor::desktop::RenderResult{};
  retry_tool_failure.file_path = rendered_scope_path;
  retry_tool_failure.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          retry_tool_controller);
  retry_tool_failure.dirty_domain = QStringLiteral("mask_rasterization");
  retry_tool_failure.dirty_stages = {QStringLiteral("mask_rasterization")};
  retry_tool_failure.render_mode = QStringLiteral("live_preview");
  retry_tool_failure.mask_alpha_trace_fields.insert(
      QStringLiteral("mask_alpha_trace_hash"),
      QStringLiteral("sha1:retry-tool-mask-alpha"));
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      retry_tool_controller,
      false,
      retry_tool_failure);
  if (retry_tool_controller.status() != QStringLiteral("Preview failed: Mask alpha.") ||
      retry_tool_controller.previewFailureStage() != QStringLiteral("mask_alpha") ||
      retry_tool_controller.displayPreviewSource() != retry_tool_preview_source ||
      image_hash(retry_tool_sink.image(QStringLiteral("edited"))) != retry_tool_preview_hash ||
      retry_tool_controller.activeMaskKind() != QStringLiteral("brush") ||
      retry_tool_controller.activeMaskToolRole() != QStringLiteral("selection") ||
      retry_tool_controller.activeMaskIndex() != retry_tool_active_mask_index ||
      retry_tool_controller.maskPreviewMode() != QStringLiteral("overlay")) {
    return fail("stage-specific preview failure should keep last good canvas and active brush tool context");
  }
  if (!retry_tool_controller.retryPreviewRender() ||
      !wait_for_preview_idle(retry_tool_controller, 4000) ||
      retry_tool_controller.activeMaskKind() != QStringLiteral("brush") ||
      retry_tool_controller.activeMaskToolRole() != QStringLiteral("selection") ||
      retry_tool_controller.activeMaskIndex() != retry_tool_active_mask_index ||
      retry_tool_controller.maskPreviewMode() != QStringLiteral("overlay")) {
    return fail("preview retry should recover without losing the active brush mask tool context");
  }

  QImage rapid_slider_source(16, 16, QImage::Format_ARGB32);
  rapid_slider_source.fill(QColor::fromRgb(96, 104, 112));
  const auto rapid_slider_path = runtime_root.filePath(QStringLiteral("rapid-slider.png"));
  if (!rapid_slider_source.save(rapid_slider_path)) {
    return fail("rapid slider source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController working_profile_controller;
  working_profile_controller.loadAsset(
      QStringLiteral("working-profile"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(working_profile_controller)) {
    return fail("working-profile controller should finish its initial preview");
  }
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  working_profile_controller.setWorkingColorProfile(QStringLiteral("Linear sRGB"));
  const auto working_profile_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          working_profile_controller);
  const auto working_profile_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          working_profile_controller);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          working_profile_controller) ||
      working_profile_intent.operation != QStringLiteral("working_color_profile") ||
      working_profile_intent.dirty_domain != QStringLiteral("input_color_transform") ||
      working_profile_full_request.dirty_domain != QStringLiteral("input_color_transform") ||
      working_profile_full_request.dirty_stages.isEmpty() ||
      working_profile_full_request.dirty_stages.first() !=
          QStringLiteral("input_color_transform") ||
      working_profile_full_request.state.working_color_profile !=
          QStringLiteral("Linear sRGB")) {
    return fail(
        "working-profile changes should invalidate input transform and carry the normalized profile into render state");
  }
  const auto working_profile_intent_count =
      photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
          working_profile_controller);
  working_profile_controller.setWorkingColorProfile(QStringLiteral("linear srgb"));
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
          working_profile_controller) != working_profile_intent_count) {
    return fail("normalized working-profile no-op should not emit another edit intent");
  }
  if (!wait_for_preview_idle(working_profile_controller)) {
    return fail("working-profile controller should settle after profile render");
  }

  photo_editor::desktop::EditorPreviewController rapid_slider_controller;
  rapid_slider_controller.loadAsset(
      QStringLiteral("rapid-slider"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(rapid_slider_controller)) {
    return fail("rapid slider controller should finish its initial preview");
  }
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  rapid_slider_controller.setExposure(0.10);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          rapid_slider_controller)) {
    return fail("the first slider edit should schedule a settled full-resolution render");
  }
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_timer_active(
          rapid_slider_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_timer_interval(
          rapid_slider_controller) != 0) {
    return fail("global slider edits should post their visible preview render immediately");
  }
  const auto first_slider_interactive_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          rapid_slider_controller);
  const auto first_slider_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          rapid_slider_controller);
  if (first_slider_interactive_request.render_mode != QStringLiteral("settled_preview") ||
      first_slider_interactive_request.render_priority != 1 ||
      first_slider_interactive_request.dirty_domain != QStringLiteral("global_tone_color") ||
      first_slider_interactive_request.dirty_stages.isEmpty() ||
      first_slider_interactive_request.dirty_stages.first() != QStringLiteral("global_tone_color") ||
      first_slider_interactive_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      !first_slider_interactive_request.dirty_stages.contains(QStringLiteral("display_transform")) ||
      !first_slider_interactive_request.full_invalidation ||
      first_slider_interactive_request.full_resolution ||
      first_slider_full_request.render_mode != QStringLiteral("max_preview") ||
      first_slider_full_request.render_priority != 3 ||
      first_slider_full_request.dirty_domain != QStringLiteral("global_tone_color") ||
      first_slider_full_request.dirty_stages != first_slider_interactive_request.dirty_stages ||
      !first_slider_full_request.full_resolution ||
      first_slider_full_request.max_edge != 0 ||
      first_slider_full_request.async_job.job_kind != QStringLiteral("full_resolution_preview") ||
      first_slider_full_request.async_job.stage != QStringLiteral("render_request") ||
      !first_slider_full_request.async_job.job_id.startsWith(
          QStringLiteral("full_resolution_preview-")) ||
      first_slider_full_request.async_job.asset_id != QStringLiteral("rapid-slider") ||
      first_slider_full_request.async_job.file_path != rapid_slider_path ||
      first_slider_full_request.async_job.generation !=
          photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
              rapid_slider_controller) ||
      first_slider_full_request.async_job.state_signature.isEmpty() ||
      first_slider_interactive_request.render_latency_budget_ms != 1000 ||
      first_slider_full_request.render_latency_budget_ms != 6000 ||
      first_slider_full_request.tile_size != 512 ||
      first_slider_full_request.tile_padding_px != 64 ||
      first_slider_full_request.planned_tile_count != 1 ||
      first_slider_full_request.planned_background_tile_count != 1 ||
      first_slider_full_request.planned_visible_tile_count != 0 ||
      first_slider_full_request.planned_adjacent_tile_count != 0) {
    return fail("preview requests should classify render modes and global-color dirty stages explicitly");
  }
  const auto first_slider_full_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          first_slider_full_request);
  if (!first_slider_full_result.success ||
      first_slider_full_result.cancelled ||
      !first_slider_full_result.full_resolution ||
      first_slider_full_result.render_mode != QStringLiteral("max_preview") ||
      first_slider_full_result.async_job.job_id != first_slider_full_request.async_job.job_id ||
      first_slider_full_result.async_job.job_kind != QStringLiteral("full_resolution_preview") ||
      first_slider_full_result.async_job.state_signature !=
          first_slider_full_request.async_job.state_signature ||
      first_slider_full_result.render_latency_budget_ms !=
          first_slider_full_request.render_latency_budget_ms ||
      first_slider_full_result.render_within_latency_budget !=
          (first_slider_full_result.total_elapsed_ms <=
           first_slider_full_result.render_latency_budget_ms)) {
    return fail("full-resolution settled preview renders should carry budget telemetry from request to result");
  }
  const auto first_slider_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          rapid_slider_controller);
  const auto first_slider_intent_count =
      photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
          rapid_slider_controller);
  if (first_slider_intent_count != 1 ||
      first_slider_intent.operation != QStringLiteral("exposure") ||
      first_slider_intent.phase != QStringLiteral("commit") ||
      first_slider_intent.dirty_domain != QStringLiteral("global_tone_color") ||
      first_slider_intent.render_reason != QStringLiteral("edit_change") ||
      !first_slider_intent.schedule_full_resolution) {
    return fail("basic slider edits should emit a typed global color edit intent before rendering");
  }
  const auto first_slider_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          rapid_slider_controller);
  rapid_slider_controller.setExposure(0.10);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
          rapid_slider_controller) != first_slider_intent_count ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          rapid_slider_controller) != first_slider_generation) {
    return fail("no-op slider changes should not emit edit intents or schedule renders");
  }

  photo_editor::desktop::EditorPreviewController masked_global_debounce_controller;
  masked_global_debounce_controller.loadAsset(
      QStringLiteral("masked-global-debounce"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(masked_global_debounce_controller)) {
    return fail("masked global debounce controller should finish its initial preview");
  }
  masked_global_debounce_controller.addMask(QStringLiteral("linear"));
  masked_global_debounce_controller.setActiveMaskExposure(0.35);
  if (!wait_for_preview_idle(masked_global_debounce_controller, 4000)) {
    return fail("masked global debounce controller should settle its local mask preview");
  }
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  masked_global_debounce_controller.setExposure(0.18);
  const auto masked_global_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          masked_global_debounce_controller);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          masked_global_debounce_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_interval(
          masked_global_debounce_controller) != 700 ||
      masked_global_full_request.dirty_domain != QStringLiteral("global_tone_color") ||
      !masked_global_full_request.full_resolution) {
    return fail("global exposure edits over rendered masks should use the normal full-preview settle delay");
  }
  if (!wait_for_preview_idle(masked_global_debounce_controller, 4000)) {
    return fail("masked global debounce controller should settle after global exposure render");
  }

  photo_editor::desktop::EditorPreviewController immediate_mask_feedback_controller;
  immediate_mask_feedback_controller.loadAsset(
      QStringLiteral("immediate-mask-feedback"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(immediate_mask_feedback_controller)) {
    return fail("immediate mask feedback controller should finish its initial preview");
  }
  immediate_mask_feedback_controller.addMask(QStringLiteral("linear"));
  immediate_mask_feedback_controller.setActiveMaskExposure(0.27);
  const auto immediate_mask_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          immediate_mask_feedback_controller);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_timer_active(
          immediate_mask_feedback_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_timer_interval(
          immediate_mask_feedback_controller) != 0 ||
      immediate_mask_request.render_mode != QStringLiteral("live_preview") ||
      immediate_mask_request.dirty_domain != QStringLiteral("mask_rasterization") ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          immediate_mask_feedback_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          immediate_mask_feedback_controller)) {
    return fail("active mask edits should post visible preview work immediately without scheduling full-resolution work");
  }
  if (!wait_for_preview_idle(immediate_mask_feedback_controller, 4000)) {
    return fail("immediate mask feedback controller should settle after active mask preview");
  }

  photo_editor::desktop::EditorPreviewController immediate_layer_feedback_controller;
  immediate_layer_feedback_controller.loadAsset(
      QStringLiteral("immediate-layer-feedback"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(immediate_layer_feedback_controller)) {
    return fail("immediate layer feedback controller should finish its initial preview");
  }
  immediate_layer_feedback_controller.addMask(QStringLiteral("linear"));
  if (immediate_layer_feedback_controller.photoLayerCount() != 1 ||
      !immediate_layer_feedback_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("exposure"),
          0.42)) {
    return fail("immediate layer feedback controller should expose a layer-local exposure adjustment");
  }
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_adjustment_render_timer_active(immediate_layer_feedback_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_timer_interval(immediate_layer_feedback_controller) !=
          0 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          immediate_layer_feedback_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          immediate_layer_feedback_controller)) {
    return fail("layer-local adjustment edits should post visible preview work immediately without scheduling full-resolution work");
  }
  if (!wait_for_preview_idle(immediate_layer_feedback_controller, 4000)) {
    return fail("immediate layer feedback controller should settle after layer-local preview");
  }

  photo_editor::desktop::EditorPreviewController stale_full_preview_controller;
  stale_full_preview_controller.loadAsset(
      QStringLiteral("stale-full-preview"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(stale_full_preview_controller)) {
    return fail("stale full-preview controller should finish its initial preview");
  }
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  stale_full_preview_controller.setExposure(0.11);
  const auto stale_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          stale_full_preview_controller);
  const auto stale_full_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          stale_full_request);
  if (!stale_full_result.success ||
      stale_full_result.cancelled ||
      stale_full_result.async_job.job_kind != QStringLiteral("full_resolution_preview") ||
      stale_full_result.async_job.job_id != stale_full_request.async_job.job_id) {
    return fail("full-resolution preview results should preserve their async job identity");
  }
  const auto full_resolution_publish_generation_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_full_preview_generation(
          stale_full_preview_controller);
  stale_full_preview_controller.setExposure(0.15);
  auto forged_stale_full_result = stale_full_result;
  forged_stale_full_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          stale_full_preview_controller);
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      stale_full_preview_controller,
      true,
      forged_stale_full_result);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::last_full_preview_generation(
          stale_full_preview_controller) != full_resolution_publish_generation_before) {
    return fail("stale full-resolution preview async results should not publish after edit state changes");
  }

  rapid_slider_controller.setContrast(12.0);
  rapid_slider_controller.setHighlights(-18.0);
  rapid_slider_controller.setShadows(24.0);
  rapid_slider_controller.setWhites(8.0);
  rapid_slider_controller.setBlacks(-10.0);
  rapid_slider_controller.setBrightness(9.0);
  rapid_slider_controller.setVibrance(33.0);
  rapid_slider_controller.setSaturation(-22.0);
  rapid_slider_controller.setTexture(14.0);
  rapid_slider_controller.setClarity(-11.0);
  rapid_slider_controller.setDehaze(7.0);
  rapid_slider_controller.setTemperature(-30.0);
  rapid_slider_controller.setTint(15.0);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          rapid_slider_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_running(
          rapid_slider_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          rapid_slider_controller)) {
    return fail(
        "rapid slider edits should keep full-resolution work deferred behind interactive preview");
  }
  const auto rapid_slider_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          rapid_slider_controller);
  if (rapid_slider_generation != first_slider_generation + 13 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::pending_full_render_generation(
          rapid_slider_controller) != rapid_slider_generation ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_draft(
          rapid_slider_controller)) {
    return fail("rapid slider edits should coalesce into one latest draft/full render pair");
  }
  const auto rapid_interactive_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          rapid_slider_controller);
  const auto rapid_pending_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::pending_full_render_request(
          rapid_slider_controller);
  if (rapid_interactive_request.render_mode != QStringLiteral("live_preview") ||
      rapid_interactive_request.render_priority != 0 ||
      rapid_interactive_request.dirty_domain != QStringLiteral("global_tone_color") ||
      rapid_interactive_request.dirty_stages.isEmpty() ||
      rapid_interactive_request.dirty_stages.first() != QStringLiteral("global_tone_color") ||
      rapid_interactive_request.full_resolution ||
      rapid_pending_full_request.render_mode != QStringLiteral("max_preview") ||
      rapid_pending_full_request.render_priority != 3 ||
      rapid_pending_full_request.dirty_domain != QStringLiteral("global_tone_color") ||
      rapid_pending_full_request.dirty_stages == QStringList{} ||
      !rapid_pending_full_request.full_resolution ||
      rapid_pending_full_request.max_edge != 0 ||
      rapid_interactive_request.render_latency_budget_ms != 100 ||
      rapid_pending_full_request.render_latency_budget_ms != 6000) {
    return fail("rapid edits should classify live preview ahead of deferred max preview work with dirty stages");
  }
  const auto rapid_pending_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::pending_full_render_state(
          rapid_slider_controller);
  if (!almost_equal(rapid_pending_state.exposure, 0.10) ||
      !almost_equal(rapid_pending_state.contrast, 12.0) ||
      !almost_equal(rapid_pending_state.highlights, -18.0) ||
      !almost_equal(rapid_pending_state.shadows, 24.0) ||
      !almost_equal(rapid_pending_state.whites, 8.0) ||
      !almost_equal(rapid_pending_state.blacks, -10.0) ||
      !almost_equal(rapid_pending_state.brightness, 9.0) ||
      !almost_equal(rapid_pending_state.vibrance, 33.0) ||
      !almost_equal(rapid_pending_state.saturation, -22.0) ||
      !almost_equal(rapid_pending_state.texture, 14.0) ||
      !almost_equal(rapid_pending_state.clarity, -11.0) ||
      !almost_equal(rapid_pending_state.dehaze, 7.0) ||
      !almost_equal(rapid_pending_state.temperature, -30.0) ||
      !almost_equal(rapid_pending_state.tint, 15.0)) {
    return fail("rapid slider full-resolution render should use the latest adjustment state");
  }
  if (!wait_for_preview_idle(rapid_slider_controller)) {
    return fail("rapid slider controller should settle after deferred full render");
  }
  rapid_slider_controller.setExposure(0.20);
  rapid_slider_controller.setOutputColorSpace(QStringLiteral("Display P3"));
  const auto mixed_dirty_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          rapid_slider_controller);
  if (mixed_dirty_request.dirty_domain != QStringLiteral("global_tone_color") ||
      mixed_dirty_request.dirty_stages.isEmpty() ||
      mixed_dirty_request.dirty_stages.first() != QStringLiteral("global_tone_color") ||
      mixed_dirty_request.dirty_stages.contains(QStringLiteral("source_decode"))) {
    return fail("coalesced edits should keep the earliest dirty stage required by the burst");
  }
  if (!wait_for_preview_idle(rapid_slider_controller)) {
    return fail("mixed dirty-stage burst should settle after deferred full render");
  }

  photo_editor::desktop::EditorPreviewController advanced_tone_controller;
  advanced_tone_controller.loadAsset(
      QStringLiteral("advanced-tone-burst"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(advanced_tone_controller)) {
    return fail("advanced tone controller should finish its initial preview");
  }
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  advanced_tone_controller.setHdrLocalToneMapping(16.0);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          advanced_tone_controller)) {
    return fail("the first advanced tone edit should schedule a settled full-resolution render");
  }
  const auto first_advanced_tone_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          advanced_tone_controller);
  const auto first_advanced_tone_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          advanced_tone_controller);
  const auto first_advanced_tone_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          advanced_tone_controller);
  const auto first_advanced_tone_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          advanced_tone_controller);
  if (first_advanced_tone_request.render_mode != QStringLiteral("settled_preview") ||
      first_advanced_tone_request.render_priority != 1 ||
      first_advanced_tone_request.dirty_domain != QStringLiteral("global_tone_color") ||
      first_advanced_tone_request.dirty_stages.isEmpty() ||
      first_advanced_tone_request.dirty_stages.first() != QStringLiteral("global_tone_color") ||
      first_advanced_tone_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      first_advanced_tone_full_request.render_mode != QStringLiteral("max_preview") ||
      first_advanced_tone_full_request.render_priority != 3 ||
      first_advanced_tone_full_request.dirty_domain != QStringLiteral("global_tone_color") ||
      first_advanced_tone_full_request.dirty_stages != first_advanced_tone_request.dirty_stages ||
      first_advanced_tone_intent.operation != QStringLiteral("hdr_local_tone_mapping") ||
      first_advanced_tone_intent.dirty_domain != QStringLiteral("global_tone_color") ||
      !first_advanced_tone_intent.schedule_full_resolution) {
    return fail("advanced tone edits should emit typed global tone/color intents before rendering");
  }
  advanced_tone_controller.setFilmicShoulder(21.0);
  advanced_tone_controller.setFilmicToe(-18.0);
  advanced_tone_controller.setAdaptiveContrast(13.0);
  advanced_tone_controller.setLevelsBlackPoint(4.0);
  advanced_tone_controller.setLevelsGamma(9.0);
  advanced_tone_controller.setLevelsWhitePoint(96.0);
  advanced_tone_controller.setZoneDeepShadows(-7.0);
  advanced_tone_controller.setZoneShadows(8.0);
  advanced_tone_controller.setZoneMidtones(11.0);
  advanced_tone_controller.setZoneLights(-6.0);
  advanced_tone_controller.setZoneHighlights(5.0);
  const auto rapid_advanced_tone_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          advanced_tone_controller);
  const auto rapid_advanced_tone_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          advanced_tone_controller);
  const auto rapid_advanced_tone_pending_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::pending_full_render_request(
          advanced_tone_controller);
  const auto rapid_advanced_tone_pending_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::pending_full_render_state(
          advanced_tone_controller);
  const auto last_advanced_tone_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          advanced_tone_controller);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
          advanced_tone_controller) != 12 ||
      rapid_advanced_tone_generation != first_advanced_tone_generation + 11 ||
      rapid_advanced_tone_request.render_mode != QStringLiteral("live_preview") ||
      rapid_advanced_tone_request.render_priority != 0 ||
      rapid_advanced_tone_request.dirty_domain != QStringLiteral("global_tone_color") ||
      rapid_advanced_tone_request.dirty_stages.isEmpty() ||
      rapid_advanced_tone_request.dirty_stages.first() != QStringLiteral("global_tone_color") ||
      rapid_advanced_tone_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      rapid_advanced_tone_pending_full_request.render_mode != QStringLiteral("max_preview") ||
      rapid_advanced_tone_pending_full_request.render_priority != 3 ||
      rapid_advanced_tone_pending_full_request.dirty_domain != QStringLiteral("global_tone_color") ||
      rapid_advanced_tone_pending_full_request.dirty_stages != rapid_advanced_tone_request.dirty_stages ||
      !almost_equal(rapid_advanced_tone_pending_state.hdr_local_tone_mapping, 16.0) ||
      !almost_equal(rapid_advanced_tone_pending_state.filmic_shoulder, 21.0) ||
      !almost_equal(rapid_advanced_tone_pending_state.filmic_toe, -18.0) ||
      !almost_equal(rapid_advanced_tone_pending_state.adaptive_contrast, 13.0) ||
      !almost_equal(rapid_advanced_tone_pending_state.levels_black_point, 4.0) ||
      !almost_equal(rapid_advanced_tone_pending_state.levels_gamma, 9.0) ||
      !almost_equal(rapid_advanced_tone_pending_state.levels_white_point, 96.0) ||
      !almost_equal(rapid_advanced_tone_pending_state.zone_deep_shadows, -7.0) ||
      !almost_equal(rapid_advanced_tone_pending_state.zone_shadows, 8.0) ||
      !almost_equal(rapid_advanced_tone_pending_state.zone_midtones, 11.0) ||
      !almost_equal(rapid_advanced_tone_pending_state.zone_lights, -6.0) ||
      !almost_equal(rapid_advanced_tone_pending_state.zone_highlights, 5.0) ||
      last_advanced_tone_intent.operation != QStringLiteral("zone_highlights") ||
      last_advanced_tone_intent.dirty_domain != QStringLiteral("global_tone_color")) {
    return fail("rapid advanced tone edits should coalesce into one typed global tone/color render pair");
  }
  if (!wait_for_preview_idle(advanced_tone_controller)) {
    return fail("advanced tone controller should settle after deferred full render");
  }

  QImage localized_tile_source(2048, 2048, QImage::Format_ARGB32);
  localized_tile_source.fill(QColor::fromRgb(72, 86, 104));
  const auto localized_tile_path = runtime_root.filePath(QStringLiteral("localized-tile-plan.png"));
  if (!localized_tile_source.save(localized_tile_path)) {
    return fail("localized tile source image should be writable");
  }
  const auto final_rebuild_source_path =
      runtime_root.filePath(QStringLiteral("final-mask-rebuild-source.png"));
  QImage final_rebuild_source(64, 64, QImage::Format_ARGB32);
  final_rebuild_source.setColorSpace(photo_editor::desktop::preview_color_space());
  final_rebuild_source.fill(QColor::fromRgb(64, 64, 64));
  if (!final_rebuild_source.save(final_rebuild_source_path)) {
    return fail("final mask rebuild source image should be writable");
  }
  auto final_rebuild_mask = photo_editor::desktop::default_mask_definition(QStringLiteral("brush"), 1);
  final_rebuild_mask.insert(QStringLiteral("enabled"), true);
  final_rebuild_mask.insert(QStringLiteral("exposure"), 1.0);
  final_rebuild_mask.insert(QStringLiteral("brushSize"), 0.18);
  final_rebuild_mask.insert(QStringLiteral("brushFlow"), 1.0);
  final_rebuild_mask.insert(QStringLiteral("brushHardness"), 0.7);
  final_rebuild_mask.insert(QStringLiteral("segmentationData"), QByteArray(4, char(0)));
  final_rebuild_mask.insert(QStringLiteral("segmentationWidth"), 2);
  final_rebuild_mask.insert(QStringLiteral("segmentationHeight"), 2);
  QVariantMap final_rebuild_point;
  final_rebuild_point.insert(QStringLiteral("x"), 0.5);
  final_rebuild_point.insert(QStringLiteral("y"), 0.5);
  final_rebuild_point.insert(QStringLiteral("size"), 0.18);
  final_rebuild_point.insert(QStringLiteral("flow"), 1.0);
  final_rebuild_point.insert(QStringLiteral("pressure"), 1.0);
  final_rebuild_point.insert(QStringLiteral("tiltX"), 0.0);
  final_rebuild_point.insert(QStringLiteral("tiltY"), 0.0);
  final_rebuild_point.insert(QStringLiteral("erase"), false);
  final_rebuild_mask.insert(QStringLiteral("brushPoints"), QVariantList{final_rebuild_point});
  photo_editor::desktop::EditState final_rebuild_state;
  final_rebuild_state.mask_stack = QVariantList{final_rebuild_mask};
  final_rebuild_state.active_mask_index = 0;
  photo_editor::desktop::RenderRequest final_rebuild_request;
  final_rebuild_request.file_path = final_rebuild_source_path;
  final_rebuild_request.state = final_rebuild_state;
  final_rebuild_request.full_resolution = true;
  final_rebuild_request.use_in_memory_preview = true;
  final_rebuild_request.full_invalidation = true;
  final_rebuild_request.render_reason = QStringLiteral("final_mask_rebuild_test");
  final_rebuild_request.generation = 1;
  final_rebuild_request.latest_generation = std::make_shared<std::atomic<int>>(1);
  const auto final_rebuild_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          final_rebuild_request);
  if (!final_rebuild_result.success || final_rebuild_result.preview_image.isNull() ||
      final_rebuild_result.preview_image.pixelColor(32, 32).red() <=
          final_rebuild_result.preview_image.pixelColor(0, 0).red() + 16) {
    return fail("full-resolution render requests should rebuild brush masks from editable points");
  }
  photo_editor::desktop::EditorPreviewController localized_tile_controller;
  localized_tile_controller.loadAsset(
      QStringLiteral("localized-tile-plan"),
      localized_tile_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(localized_tile_controller)) {
    return fail("localized tile controller should finish its initial preview");
  }
  localized_tile_controller.setPreviewViewport(0.75, 0.75, 0.125, 0.125, 512, true);
  photo_editor::desktop::EditorPreviewControllerTestAccess::submit_edit_intent(
      localized_tile_controller,
      photo_editor::desktop::EditIntent{
          .operation = QStringLiteral("localized_tile_plan_probe"),
          .dirty_domain = QStringLiteral("mask_rasterization"),
          .normalized_region = QRectF(0.125, 0.125, 0.10, 0.10),
          .schedule_full_resolution = false,
      });
  const auto localized_tile_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          localized_tile_controller);
  if (localized_tile_request.full_invalidation ||
      localized_tile_request.tile_size != 512 ||
      localized_tile_request.tile_padding_px != 64 ||
      !localized_tile_request.tile_visible_region_active ||
      localized_tile_request.planned_tile_count != 1 ||
      localized_tile_request.planned_visible_tile_count != 0 ||
      localized_tile_request.planned_adjacent_tile_count != 0 ||
      localized_tile_request.planned_background_tile_count != 1) {
    return fail("localized dirty regions should plan only intersecting max-preview tiles");
  }
  if (!wait_for_preview_idle(localized_tile_controller)) {
    return fail("localized tile controller should settle after tile-plan probe");
  }
  auto localized_seed_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          localized_tile_controller);
  localized_seed_request.use_in_memory_preview = true;
  localized_seed_request.render_reason = QStringLiteral("edit_change");
  localized_seed_request.dirty_domain = QStringLiteral("source_decode");
  localized_seed_request.full_invalidation = false;
  localized_seed_request.dirty_region = QRectF(0.0, 0.0, 1.0, 1.0);
  const auto localized_seed_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          localized_seed_request);
  if (!localized_seed_result.success ||
      localized_seed_result.executed_tile_count != 16 ||
      localized_seed_result.executed_visible_tile_count != 1 ||
      localized_seed_result.executed_adjacent_tile_count != 3 ||
      localized_seed_result.executed_background_tile_count != 12 ||
      localized_seed_result.first_executed_tile_priority != QStringLiteral("visible") ||
      localized_seed_result.executed_tile_pixels != 2048 * 2048) {
    std::cerr << "large seed success=" << localized_seed_result.success
              << " backend=" << localized_seed_result.backend.toStdString()
              << " adjustedLookup=" << localized_seed_result.adjusted_cache_lookup
              << " adjustedHit=" << localized_seed_result.adjusted_cache_hit
              << " tiles=" << localized_seed_result.executed_tile_count
              << " visibleTiles=" << localized_seed_result.executed_visible_tile_count
              << " adjacentTiles=" << localized_seed_result.executed_adjacent_tile_count
              << " backgroundTiles=" << localized_seed_result.executed_background_tile_count
              << " firstTilePriority="
              << localized_seed_result.first_executed_tile_priority.toStdString()
              << " pixels=" << localized_seed_result.executed_tile_pixels
              << " output=" << localized_seed_result.output_width << "x"
              << localized_seed_result.output_height << '\n';
    return fail("large full max-preview render should execute visible tiles before background tiles");
  }
  auto brush_tile_reuse_request = localized_seed_request;
  brush_tile_reuse_request.full_invalidation = false;
  brush_tile_reuse_request.dirty_region = QRectF(0.0, 0.0, 0.10, 0.10);
  brush_tile_reuse_request.dirty_domain = QStringLiteral("mask_rasterization");
  brush_tile_reuse_request.dirty_stages = QStringList{
      QStringLiteral("mask_rasterization"),
      QStringLiteral("mask_composition"),
      QStringLiteral("local_adjustments"),
      QStringLiteral("retouch_heal"),
      QStringLiteral("retouch_patch_application"),
      QStringLiteral("detail_spatial"),
      QStringLiteral("layers_composite"),
      QStringLiteral("lut_post_color"),
      QStringLiteral("display_transform"),
      QStringLiteral("overlay_composite"),
      QStringLiteral("publish_texture"),
      QStringLiteral("encode_disk"),
      QStringLiteral("histogram_scope"),
  };
  brush_tile_reuse_request.tile_padding_px = 64;
  auto brush_mask = photo_editor::desktop::default_mask_definition(QStringLiteral("brush"), 1);
  brush_mask.insert(QStringLiteral("enabled"), true);
  brush_mask.insert(QStringLiteral("exposure"), 0.75);
  brush_mask.insert(QStringLiteral("brushSize"), 0.04);
  brush_mask.insert(QStringLiteral("brushFlow"), 1.0);
  brush_mask.insert(QStringLiteral("brushHardness"), 0.25);
  QVariantMap brush_point;
  brush_point.insert(QStringLiteral("x"), 0.08);
  brush_point.insert(QStringLiteral("y"), 0.08);
  brush_point.insert(QStringLiteral("size"), 0.04);
  brush_point.insert(QStringLiteral("flow"), 1.0);
  brush_point.insert(QStringLiteral("pressure"), 1.0);
  brush_point.insert(QStringLiteral("tiltX"), 0.0);
  brush_point.insert(QStringLiteral("tiltY"), 0.0);
  brush_point.insert(QStringLiteral("erase"), false);
  brush_mask.insert(QStringLiteral("brushPoints"), QVariantList{brush_point});
  brush_tile_reuse_request.state.mask_stack = QVariantList{brush_mask};
  brush_tile_reuse_request.state.active_mask_index = 0;
  const auto brush_tile_reuse_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          brush_tile_reuse_request);
  if (!brush_tile_reuse_result.success ||
      brush_tile_reuse_result.adjusted_cache_hit ||
      brush_tile_reuse_result.backend != QStringLiteral("adjusted_tile_cache") ||
      brush_tile_reuse_result.adjusted_stage_tile_count != 1 ||
      brush_tile_reuse_result.adjusted_stage_pixels != 512 * 512 ||
      brush_tile_reuse_result.executed_tile_count != 1 ||
      brush_tile_reuse_result.executed_tile_pixels != 512 * 512 ||
      brush_tile_reuse_result.texture_upload_pixels != 512 * 512 ||
      brush_tile_reuse_result.decode_elapsed_ms != 0 ||
      brush_tile_reuse_result.texture_upload_rect != QRect(0, 0, 512, 512) ||
      !brush_tile_reuse_result.partial_texture_upload_candidate ||
      !brush_tile_reuse_result.base_rendered_image_hit ||
      brush_tile_reuse_result.output_width != 2048 ||
      brush_tile_reuse_result.output_height != 2048 ||
      brush_tile_reuse_result.preview_image.isNull()) {
    std::cerr << "brush tile reuse success=" << brush_tile_reuse_result.success
              << " adjusted=" << brush_tile_reuse_result.adjusted_cache_hit
              << " backend=" << brush_tile_reuse_result.backend.toStdString()
              << " adjustedStageTiles=" << brush_tile_reuse_result.adjusted_stage_tile_count
              << " adjustedStagePixels=" << brush_tile_reuse_result.adjusted_stage_pixels
              << " tiles=" << brush_tile_reuse_result.executed_tile_count
              << " pixels=" << brush_tile_reuse_result.executed_tile_pixels
              << " uploadPixels=" << brush_tile_reuse_result.texture_upload_pixels
              << " decodeMs=" << brush_tile_reuse_result.decode_elapsed_ms
              << " baseRendered=" << brush_tile_reuse_result.base_rendered_image_hit
              << " output=" << brush_tile_reuse_result.output_width << "x"
              << brush_tile_reuse_result.output_height << '\n';
    return fail("brush-local max-preview requests should reuse unaffected rendered tiles across recipe changes");
  }
  auto brush_full_reference_request = brush_tile_reuse_request;
  brush_full_reference_request.full_invalidation = true;
  brush_full_reference_request.dirty_region = QRectF();
  brush_full_reference_request.compare_mode = true;
  const auto brush_full_reference_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          brush_full_reference_request);
  const auto brush_tile_delta = max_channel_delta(
      brush_tile_reuse_result.preview_image,
      brush_full_reference_result.preview_image);
  if (!brush_full_reference_result.success ||
      brush_full_reference_result.preview_image.isNull() ||
      brush_tile_delta > 10) {
    std::cerr << "brush tile parity full_success=" << brush_full_reference_result.success
              << " tile_image_null=" << brush_tile_reuse_result.preview_image.isNull()
              << " full_image_null=" << brush_full_reference_result.preview_image.isNull()
              << " delta=" << brush_tile_delta << '\n';
    return fail("brush-local adjusted tile render should match the full-frame brush render");
  }
  auto second_stroke_brush_request = brush_tile_reuse_request;
  second_stroke_brush_request.dirty_region = QRectF(0.25, 0.0, 0.10, 0.10);
  QVariantMap second_brush_point;
  second_brush_point.insert(QStringLiteral("x"), 0.32);
  second_brush_point.insert(QStringLiteral("y"), 0.08);
  second_brush_point.insert(QStringLiteral("size"), 0.04);
  second_brush_point.insert(QStringLiteral("flow"), 1.0);
  second_brush_point.insert(QStringLiteral("pressure"), 1.0);
  second_brush_point.insert(QStringLiteral("tiltX"), 0.0);
  second_brush_point.insert(QStringLiteral("tiltY"), 0.0);
  second_brush_point.insert(QStringLiteral("erase"), false);
  auto second_stroke_mask = brush_mask;
  second_stroke_mask.insert(QStringLiteral("brushPoints"), QVariantList{brush_point, second_brush_point});
  second_stroke_brush_request.state.mask_stack = QVariantList{second_stroke_mask};
  const auto second_stroke_brush_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          second_stroke_brush_request);
  if (!second_stroke_brush_result.success ||
      second_stroke_brush_result.backend != QStringLiteral("adjusted_tile_cache") ||
      second_stroke_brush_result.adjusted_stage_tile_count != 1 ||
      second_stroke_brush_result.adjusted_stage_pixels != 512 * 512 ||
      second_stroke_brush_result.executed_tile_count != 1 ||
      second_stroke_brush_result.executed_tile_pixels != 512 * 512 ||
      second_stroke_brush_result.texture_upload_pixels != 512 * 512 ||
      second_stroke_brush_result.texture_upload_rect != QRect(512, 0, 512, 512) ||
      !second_stroke_brush_result.partial_texture_upload_candidate ||
      !second_stroke_brush_result.base_rendered_image_hit ||
      second_stroke_brush_result.preview_image.isNull()) {
    std::cerr << "second-stroke brush success=" << second_stroke_brush_result.success
              << " backend=" << second_stroke_brush_result.backend.toStdString()
              << " adjustedStageTiles=" << second_stroke_brush_result.adjusted_stage_tile_count
              << " adjustedStagePixels=" << second_stroke_brush_result.adjusted_stage_pixels
              << " tiles=" << second_stroke_brush_result.executed_tile_count
              << " pixels=" << second_stroke_brush_result.executed_tile_pixels
              << " uploadPixels=" << second_stroke_brush_result.texture_upload_pixels
              << " baseRendered=" << second_stroke_brush_result.base_rendered_image_hit << '\n';
    return fail("second brush strokes should patch onto the previous rendered brush state");
  }
  auto second_stroke_reference_request = second_stroke_brush_request;
  second_stroke_reference_request.full_invalidation = true;
  second_stroke_reference_request.dirty_region = QRectF();
  second_stroke_reference_request.compare_mode = true;
  const auto second_stroke_reference_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          second_stroke_reference_request);
  const auto second_stroke_delta = max_channel_delta(
      second_stroke_brush_result.preview_image,
      second_stroke_reference_result.preview_image);
  if (!second_stroke_reference_result.success ||
      second_stroke_reference_result.preview_image.isNull() ||
      second_stroke_delta > 10) {
    std::cerr << "second-stroke brush parity full_success="
              << second_stroke_reference_result.success
              << " delta=" << second_stroke_delta << '\n';
    return fail("second brush stroke adjusted tile render should match the full-frame render");
  }
  auto texture_patch_brush_request = second_stroke_brush_request;
  texture_patch_brush_request.allow_texture_patch_only = true;
  texture_patch_brush_request.dirty_region = QRectF(0.5, 0.0, 0.10, 0.10);
  QVariantMap texture_patch_brush_point;
  texture_patch_brush_point.insert(QStringLiteral("x"), 0.56);
  texture_patch_brush_point.insert(QStringLiteral("y"), 0.08);
  texture_patch_brush_point.insert(QStringLiteral("size"), 0.04);
  texture_patch_brush_point.insert(QStringLiteral("flow"), 1.0);
  texture_patch_brush_point.insert(QStringLiteral("pressure"), 1.0);
  texture_patch_brush_point.insert(QStringLiteral("tiltX"), 0.0);
  texture_patch_brush_point.insert(QStringLiteral("tiltY"), 0.0);
  texture_patch_brush_point.insert(QStringLiteral("erase"), false);
  auto texture_patch_mask = brush_mask;
  texture_patch_mask.insert(
      QStringLiteral("brushPoints"),
      QVariantList{brush_point, second_brush_point, texture_patch_brush_point});
  texture_patch_brush_request.state.mask_stack = QVariantList{texture_patch_mask};
  const auto texture_patch_brush_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          texture_patch_brush_request);
  if (!texture_patch_brush_result.success ||
      texture_patch_brush_result.backend != QStringLiteral("adjusted_tile_cache") ||
      texture_patch_brush_result.adjusted_stage_tile_count != 1 ||
      texture_patch_brush_result.adjusted_stage_pixels != 169 * 169 ||
      texture_patch_brush_result.executed_tile_count != 1 ||
      texture_patch_brush_result.executed_tile_pixels != 512 * 512 ||
      texture_patch_brush_result.texture_upload_pixels >= 512 * 512 ||
      texture_patch_brush_result.texture_patch_base_copy_pixels != 0 ||
      texture_patch_brush_result.decode_elapsed_ms != 0 ||
      texture_patch_brush_result.texture_upload_rect != QRect(1062, 79, 169, 169) ||
      !texture_patch_brush_result.partial_texture_upload_candidate ||
      !texture_patch_brush_result.base_rendered_image_hit ||
      texture_patch_brush_result.output_width != 2048 ||
      texture_patch_brush_result.output_height != 2048 ||
      !texture_patch_brush_result.preview_image.isNull() ||
      texture_patch_brush_result.texture_upload_image.isNull() ||
      texture_patch_brush_result.texture_upload_image.size() != QSize(169, 169)) {
    std::cerr << "texture-patch brush success=" << texture_patch_brush_result.success
              << " backend=" << texture_patch_brush_result.backend.toStdString()
              << " adjustedStageTiles=" << texture_patch_brush_result.adjusted_stage_tile_count
              << " adjustedStagePixels=" << texture_patch_brush_result.adjusted_stage_pixels
              << " tiles=" << texture_patch_brush_result.executed_tile_count
               << " pixels=" << texture_patch_brush_result.executed_tile_pixels
               << " uploadPixels=" << texture_patch_brush_result.texture_upload_pixels
               << " baseCopyPixels=" << texture_patch_brush_result.texture_patch_base_copy_pixels
               << " decodeMs=" << texture_patch_brush_result.decode_elapsed_ms
               << " patchImageNull=" << texture_patch_brush_result.texture_upload_image.isNull()
              << " fullImageNull=" << texture_patch_brush_result.preview_image.isNull()
              << " baseRendered=" << texture_patch_brush_result.base_rendered_image_hit << '\n';
    return fail("texture patch brush render should return only the brush-effect upload image");
  }
  auto texture_patch_reference_request = texture_patch_brush_request;
  texture_patch_reference_request.allow_texture_patch_only = false;
  texture_patch_reference_request.full_invalidation = true;
  texture_patch_reference_request.dirty_region = QRectF();
  texture_patch_reference_request.compare_mode = true;
  const auto texture_patch_reference_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          texture_patch_reference_request);
  const auto texture_patch_delta = max_channel_delta(
      texture_patch_brush_result.texture_upload_image,
      texture_patch_reference_result.preview_image.copy(
          texture_patch_brush_result.texture_upload_rect));
  if (!texture_patch_reference_result.success ||
      texture_patch_reference_result.preview_image.isNull() ||
      texture_patch_delta > 1) {
    std::cerr << "texture-patch brush parity full_success="
              << texture_patch_reference_result.success
              << " delta=" << texture_patch_delta << '\n';
    return fail("texture patch brush upload image should match the full-frame render crop");
  }
  photo_editor::desktop::EditorPreviewController scheduled_patch_controller;
  photo_editor::desktop::TestImageSink scheduled_patch_sink;
  scheduled_patch_controller.setImageProvider(&scheduled_patch_sink);
  auto scheduled_patch_texture =
      std::make_unique<photo_editor::desktop::FakeEditorPreviewTexture>();
  auto* scheduled_patch_texture_ptr = scheduled_patch_texture.get();
  photo_editor::desktop::EditorPreviewControllerTestAccess::set_display_preview_texture(
      scheduled_patch_controller,
      std::move(scheduled_patch_texture));
  scheduled_patch_controller.loadAsset(
      QStringLiteral("scheduled-texture-patch"),
      localized_tile_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(scheduled_patch_controller)) {
    return fail("scheduled texture patch controller should finish its initial preview");
  }
  auto scheduled_seed_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          scheduled_patch_controller);
  scheduled_seed_request.use_in_memory_preview = true;
  scheduled_seed_request.render_reason = QStringLiteral("edit_change");
  scheduled_seed_request.dirty_domain = QStringLiteral("source_decode");
  scheduled_seed_request.full_invalidation = false;
  scheduled_seed_request.dirty_region = QRectF(0.0, 0.0, 1.0, 1.0);
  scheduled_seed_request.state.exposure = 0.20;
  scheduled_seed_request.async_job = {};
  const auto scheduled_seed_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          scheduled_seed_request);
  if (!scheduled_seed_result.success ||
      scheduled_seed_result.preview_image.size() != QSize(2048, 2048) ||
      scheduled_seed_result.rendered_cache_key.isEmpty() ||
      !scheduled_patch_texture_ptr->upload(scheduled_seed_result.preview_image)) {
    return fail("scheduled texture patch test should seed a full-size native texture");
  }
  scheduled_patch_controller.setExposure(0.20);
  if (!wait_for_preview_idle(scheduled_patch_controller)) {
    return fail("scheduled texture patch controller should settle after exposure seed");
  }
  const auto viewport_patch_count_before = scheduled_patch_texture_ptr->patchUploadCount();
  const auto viewport_schedule_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          viewport_max_preview_schedule_count(scheduled_patch_controller);
  const auto viewport_prepass_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          visible_tile_prepass_schedule_count(scheduled_patch_controller);
  scheduled_patch_controller.setPreviewViewport(0.75, 0.75, 0.125, 0.125, 512, true);
  if (!wait_for_preview_idle(scheduled_patch_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              viewport_max_preview_schedule_count(scheduled_patch_controller) !=
          viewport_schedule_count_before + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              visible_tile_prepass_schedule_count(scheduled_patch_controller) !=
          viewport_prepass_count_before + 1 ||
      scheduled_patch_texture_ptr->patchUploadCount() != viewport_patch_count_before + 1) {
    std::cerr << "viewport max-preview scheduleCount="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     viewport_max_preview_schedule_count(scheduled_patch_controller)
              << " prepassCount="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     visible_tile_prepass_schedule_count(scheduled_patch_controller)
              << " patchUploads=" << scheduled_patch_texture_ptr->patchUploadCount()
              << " lastPatch="
              << scheduled_patch_texture_ptr->lastPatchUploadRect().x() << ","
              << scheduled_patch_texture_ptr->lastPatchUploadRect().y() << ","
              << scheduled_patch_texture_ptr->lastPatchUploadRect().width() << ","
              << scheduled_patch_texture_ptr->lastPatchUploadRect().height()
              << '\n';
    return fail("viewport changes should publish full-resolution visible tile patches once the full native base is tracked");
  }
  auto scheduled_patch_request = scheduled_seed_request;
  scheduled_patch_request.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          scheduled_patch_controller);
  scheduled_patch_request.dirty_region = QRectF(0.75, 0.0, 0.10, 0.10);
  scheduled_patch_request.dirty_domain = QStringLiteral("mask_rasterization");
  scheduled_patch_request.dirty_stages = brush_tile_reuse_request.dirty_stages;
  scheduled_patch_request.full_invalidation = false;
  scheduled_patch_request.allow_texture_patch_only = false;
  auto scheduled_patch_mask =
      photo_editor::desktop::default_mask_definition(QStringLiteral("brush"), 77);
  scheduled_patch_mask.insert(QStringLiteral("enabled"), true);
  scheduled_patch_mask.insert(QStringLiteral("exposure"), 0.65);
  scheduled_patch_mask.insert(QStringLiteral("brushSize"), 0.04);
  scheduled_patch_mask.insert(QStringLiteral("brushFlow"), 1.0);
  scheduled_patch_mask.insert(QStringLiteral("brushHardness"), 0.25);
  QVariantMap scheduled_patch_point;
  scheduled_patch_point.insert(QStringLiteral("x"), 0.80);
  scheduled_patch_point.insert(QStringLiteral("y"), 0.08);
  scheduled_patch_point.insert(QStringLiteral("size"), 0.04);
  scheduled_patch_point.insert(QStringLiteral("flow"), 1.0);
  scheduled_patch_point.insert(QStringLiteral("pressure"), 1.0);
  scheduled_patch_point.insert(QStringLiteral("tiltX"), 0.0);
  scheduled_patch_point.insert(QStringLiteral("tiltY"), 0.0);
  scheduled_patch_point.insert(QStringLiteral("erase"), false);
  scheduled_patch_mask.insert(QStringLiteral("brushPoints"), QVariantList{scheduled_patch_point});
  scheduled_patch_request.state.mask_stack = QVariantList{scheduled_patch_mask};
  scheduled_patch_request.state.active_mask_index = 0;
  auto scheduled_patch_base_request = scheduled_patch_request;
  auto scheduled_patch_base_mask = scheduled_patch_mask;
  scheduled_patch_base_mask.insert(QStringLiteral("brushPoints"), QVariantList{});
  scheduled_patch_base_request.state.mask_stack = QVariantList{scheduled_patch_base_mask};
  scheduled_patch_base_request.full_invalidation = true;
  scheduled_patch_base_request.allow_texture_patch_only = false;
  const auto scheduled_patch_base_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          scheduled_patch_base_request);
  if (!scheduled_patch_base_result.success ||
      scheduled_patch_base_result.preview_image.size() != QSize(2048, 2048) ||
      scheduled_patch_base_result.rendered_cache_key.isEmpty() ||
      !scheduled_patch_texture_ptr->upload(scheduled_patch_base_result.preview_image)) {
    return fail("scheduled texture patch test should seed the brush base texture state");
  }
  photo_editor::desktop::EditorPreviewControllerTestAccess::
      set_display_preview_native_rendered_cache_key(
          scheduled_patch_controller,
          scheduled_patch_base_result.rendered_cache_key);
  auto visible_prepass_request = scheduled_patch_base_request;
  visible_prepass_request.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          scheduled_patch_controller);
  visible_prepass_request.render_reason = QStringLiteral("viewport_change");
  visible_prepass_request.dirty_domain = QStringLiteral("publish_texture");
  visible_prepass_request.dirty_stages = QStringList{QStringLiteral("publish_texture")};
  visible_prepass_request.full_invalidation = true;
  visible_prepass_request.dirty_region = QRectF(0.0, 0.0, 1.0, 1.0);
  visible_prepass_request.visible_tile_only = false;
  visible_prepass_request.async_job = {};
  visible_prepass_request.tile_visible_region_active = true;
  visible_prepass_request.tile_visible_region = QRectF(0.75, 0.75, 0.125, 0.125);
  const auto scheduled_visible_prepass_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          visible_tile_prepass_schedule_count(scheduled_patch_controller);
  const auto scheduled_visible_prepass_patch_upload_count_before =
      scheduled_patch_texture_ptr->patchUploadCount();
  photo_editor::desktop::EditorPreviewControllerTestAccess::start_render_stage(
      scheduled_patch_controller,
      true,
      visible_prepass_request);
  const auto scheduled_visible_background_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::pending_full_render_request(
          scheduled_patch_controller);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::
              visible_tile_prepass_schedule_count(scheduled_patch_controller) !=
          scheduled_visible_prepass_count_before + 1 ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          scheduled_patch_controller) ||
      scheduled_visible_background_request.render_mode != QStringLiteral("max_preview") ||
      scheduled_visible_background_request.tile_visible_region_active ||
      scheduled_visible_background_request.planned_tile_count != 16 ||
      scheduled_visible_background_request.planned_visible_tile_count != 0 ||
      scheduled_visible_background_request.planned_background_tile_count != 16 ||
      scheduled_visible_background_request.visible_tile_only) {
    std::cerr << "visible prepass count="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     visible_tile_prepass_schedule_count(scheduled_patch_controller)
              << " fullPending="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
                     scheduled_patch_controller)
              << " backgroundMode="
              << scheduled_visible_background_request.render_mode.toStdString()
              << " backgroundVisibleActive="
              << scheduled_visible_background_request.tile_visible_region_active
              << " backgroundTiles="
              << scheduled_visible_background_request.planned_tile_count
              << " visibleTiles="
              << scheduled_visible_background_request.planned_visible_tile_count
              << " backgroundOnlyTiles="
              << scheduled_visible_background_request.planned_background_tile_count
              << '\n';
    return fail("full max-preview renders should schedule a visible viewport prepass before background tiles");
  }
  if (!wait_for_preview_idle(scheduled_patch_controller, 6000) ||
      scheduled_patch_texture_ptr->patchUploadCount() !=
          scheduled_visible_prepass_patch_upload_count_before + 1) {
    std::cerr << "visible prepass patchUploadsBefore="
              << scheduled_visible_prepass_patch_upload_count_before
              << " patchUploadsAfter=" << scheduled_patch_texture_ptr->patchUploadCount()
              << " nativeSize="
              << scheduled_patch_texture_ptr->nativeTextureSize().width() << "x"
              << scheduled_patch_texture_ptr->nativeTextureSize().height() << '\n';
    return fail("visible viewport prepass should patch the native texture before the background render settles");
  }
  photo_editor::desktop::EditorPreviewControllerTestAccess::
      set_display_preview_native_rendered_cache_key(
          scheduled_patch_controller,
          scheduled_patch_base_result.rendered_cache_key);
  const auto scheduled_patch_upload_count_before =
      scheduled_patch_texture_ptr->patchUploadCount();
  const auto scheduled_patch_revision_before =
      scheduled_patch_controller.displayPreviewNativeRevision();
  const auto scheduled_patch_provider_before =
      scheduled_patch_sink.image(QStringLiteral("edited"));
  photo_editor::desktop::EditorPreviewControllerTestAccess::start_render_stage(
      scheduled_patch_controller,
      true,
      scheduled_patch_request);
  if (!wait_for_preview_idle(scheduled_patch_controller)) {
    return fail("scheduled full render should finish before validating texture patch provider fallback");
  }
  const auto scheduled_patch_provider_after =
      scheduled_patch_sink.image(QStringLiteral("edited"));
  if (scheduled_patch_texture_ptr->patchUploadCount() !=
          scheduled_patch_upload_count_before + 1 ||
      scheduled_patch_texture_ptr->lastPatchUploadRect() != QRect(1554, 79, 169, 169) ||
      scheduled_patch_texture_ptr->nativeTextureSize() != QSize(2048, 2048) ||
      scheduled_patch_controller.displayPreviewNativeRevision() <=
          scheduled_patch_revision_before ||
      scheduled_patch_provider_before.isNull() ||
      scheduled_patch_provider_after.size() != scheduled_patch_provider_before.size() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          display_preview_native_rendered_cache_key(scheduled_patch_controller).isEmpty()) {
    std::cerr << "scheduled texture patch patchUploads="
              << scheduled_patch_texture_ptr->patchUploadCount()
              << " lastPatch="
              << scheduled_patch_texture_ptr->lastPatchUploadRect().x() << ","
              << scheduled_patch_texture_ptr->lastPatchUploadRect().y() << ","
              << scheduled_patch_texture_ptr->lastPatchUploadRect().width() << ","
              << scheduled_patch_texture_ptr->lastPatchUploadRect().height()
              << " nativeRevision="
              << scheduled_patch_controller.displayPreviewNativeRevision()
              << " providerBefore=" << scheduled_patch_provider_before.width() << "x"
              << scheduled_patch_provider_before.height()
              << " providerAfter=" << scheduled_patch_provider_after.width() << "x"
              << scheduled_patch_provider_after.height()
              << '\n';
    return fail("scheduled full render should publish a brush-effect native texture patch upload");
  }
  auto chained_patch_request = scheduled_patch_request;
  chained_patch_request.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          scheduled_patch_controller);
  chained_patch_request.dirty_region = QRectF(0.5, 0.0, 0.10, 0.10);
  QVariantMap chained_patch_point;
  chained_patch_point.insert(QStringLiteral("x"), 0.56);
  chained_patch_point.insert(QStringLiteral("y"), 0.08);
  chained_patch_point.insert(QStringLiteral("size"), 0.04);
  chained_patch_point.insert(QStringLiteral("flow"), 1.0);
  chained_patch_point.insert(QStringLiteral("pressure"), 1.0);
  chained_patch_point.insert(QStringLiteral("tiltX"), 0.0);
  chained_patch_point.insert(QStringLiteral("tiltY"), 0.0);
  chained_patch_point.insert(QStringLiteral("erase"), false);
  scheduled_patch_mask.insert(
      QStringLiteral("brushPoints"),
      QVariantList{scheduled_patch_point, chained_patch_point});
  chained_patch_request.state.mask_stack = QVariantList{scheduled_patch_mask};
  const auto chained_patch_upload_count_before =
      scheduled_patch_texture_ptr->patchUploadCount();
  const auto chained_patch_revision_before =
      scheduled_patch_controller.displayPreviewNativeRevision();
  const auto chained_patch_provider_before =
      scheduled_patch_sink.image(QStringLiteral("edited"));
  photo_editor::desktop::EditorPreviewControllerTestAccess::start_render_stage(
      scheduled_patch_controller,
      true,
      chained_patch_request);
  if (!wait_for_preview_idle(scheduled_patch_controller)) {
    return fail("chained texture patch render should finish before validating provider fallback");
  }
  const auto chained_patch_provider_after =
      scheduled_patch_sink.image(QStringLiteral("edited"));
  if (scheduled_patch_texture_ptr->patchUploadCount() !=
          chained_patch_upload_count_before + 1 ||
      scheduled_patch_texture_ptr->lastPatchUploadRect() != QRect(1062, 79, 169, 169) ||
      scheduled_patch_controller.displayPreviewNativeRevision() <=
          chained_patch_revision_before ||
      chained_patch_provider_before.isNull() ||
      chained_patch_provider_after.size() != chained_patch_provider_before.size()) {
    std::cerr << "chained texture patch patchUploads="
              << scheduled_patch_texture_ptr->patchUploadCount()
              << " lastPatch="
              << scheduled_patch_texture_ptr->lastPatchUploadRect().x() << ","
              << scheduled_patch_texture_ptr->lastPatchUploadRect().y() << ","
              << scheduled_patch_texture_ptr->lastPatchUploadRect().width() << ","
              << scheduled_patch_texture_ptr->lastPatchUploadRect().height()
              << " nativeRevision="
              << scheduled_patch_controller.displayPreviewNativeRevision()
              << " providerBefore=" << chained_patch_provider_before.width() << "x"
              << chained_patch_provider_before.height()
              << " providerAfter=" << chained_patch_provider_after.width() << "x"
              << chained_patch_provider_after.height()
              << '\n';
    return fail("consecutive texture patch renders should not require a full CPU patch base image");
  }
  const auto low_resolution_uploads_before_edit =
      scheduled_patch_texture_ptr->uploadCountForSize(QSize(960, 960));
  scheduled_patch_controller.setContrast(11.0);
  if (!wait_for_preview_idle(scheduled_patch_controller, 5000) ||
      scheduled_patch_texture_ptr->nativeTextureSize() != QSize(2048, 2048) ||
      scheduled_patch_texture_ptr->uploadCountForSize(QSize(960, 960)) !=
          low_resolution_uploads_before_edit) {
    std::cerr << "active viewport texture size="
              << scheduled_patch_texture_ptr->nativeTextureSize().width() << "x"
              << scheduled_patch_texture_ptr->nativeTextureSize().height()
              << " lowResUploadsBefore=" << low_resolution_uploads_before_edit
              << " lowResUploadsAfter="
              << scheduled_patch_texture_ptr->uploadCountForSize(QSize(960, 960))
              << '\n';
    return fail(
        "active viewport should preserve the max-resolution native texture during low-resolution edit renders");
  }
  auto overbroad_brush_request = brush_tile_reuse_request;
  overbroad_brush_request.dirty_region = QRectF(0.0, 0.0, 0.5, 0.5);
  const auto overbroad_brush_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          overbroad_brush_request);
  if (!overbroad_brush_result.success ||
      overbroad_brush_result.backend != QStringLiteral("adjusted_tile_cache") ||
      overbroad_brush_result.adjusted_stage_tile_count != 1 ||
      overbroad_brush_result.adjusted_stage_pixels != 512 * 512 ||
      overbroad_brush_result.executed_tile_count != 1 ||
      overbroad_brush_result.executed_tile_pixels != 512 * 512 ||
      !overbroad_brush_result.base_rendered_image_hit ||
      overbroad_brush_result.preview_image.isNull()) {
    std::cerr << "overbroad brush success=" << overbroad_brush_result.success
              << " backend=" << overbroad_brush_result.backend.toStdString()
              << " adjustedStageTiles=" << overbroad_brush_result.adjusted_stage_tile_count
              << " adjustedStagePixels=" << overbroad_brush_result.adjusted_stage_pixels
              << " tiles=" << overbroad_brush_result.executed_tile_count
              << " pixels=" << overbroad_brush_result.executed_tile_pixels
              << " baseRendered=" << overbroad_brush_result.base_rendered_image_hit << '\n';
    return fail("overbroad brush dirty regions should adjust only tiles containing brush work");
  }
  auto overbroad_reference_request = overbroad_brush_request;
  overbroad_reference_request.full_invalidation = true;
  overbroad_reference_request.dirty_region = QRectF();
  overbroad_reference_request.compare_mode = true;
  const auto overbroad_reference_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          overbroad_reference_request);
  const auto overbroad_delta = max_channel_delta(
      overbroad_brush_result.preview_image,
      overbroad_reference_result.preview_image);
  if (!overbroad_reference_result.success ||
      overbroad_reference_result.preview_image.isNull() ||
      overbroad_delta > 1) {
    std::cerr << "overbroad brush parity full_success="
              << overbroad_reference_result.success
              << " delta=" << overbroad_delta << '\n';
    return fail("overbroad brush adjusted tile render should match the full-frame render");
  }
  auto four_tile_brush_request = brush_tile_reuse_request;
  four_tile_brush_request.dirty_region = QRectF(0.0, 0.0, 0.5, 0.5);
  QVariantList four_tile_points;
  for (const auto& point_xy : std::initializer_list<QPointF>{
           QPointF{0.08, 0.08},
           QPointF{0.32, 0.08},
           QPointF{0.08, 0.32},
           QPointF{0.32, 0.32},
       }) {
    QVariantMap point;
    point.insert(QStringLiteral("x"), point_xy.x());
    point.insert(QStringLiteral("y"), point_xy.y());
    point.insert(QStringLiteral("size"), 0.04);
    point.insert(QStringLiteral("flow"), 1.0);
    point.insert(QStringLiteral("pressure"), 1.0);
    point.insert(QStringLiteral("tiltX"), 0.0);
    point.insert(QStringLiteral("tiltY"), 0.0);
    point.insert(QStringLiteral("erase"), false);
    four_tile_points.append(point);
  }
  auto four_tile_mask = brush_mask;
  four_tile_mask.insert(QStringLiteral("brushPoints"), four_tile_points);
  four_tile_brush_request.state.mask_stack = QVariantList{four_tile_mask};
  const auto four_tile_brush_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          four_tile_brush_request);
  if (!four_tile_brush_result.success ||
      four_tile_brush_result.backend != QStringLiteral("adjusted_tile_cache") ||
      four_tile_brush_result.adjusted_stage_tile_count != 4 ||
      four_tile_brush_result.adjusted_stage_pixels != 4 * 512 * 512 ||
      four_tile_brush_result.executed_tile_count != 4 ||
      four_tile_brush_result.executed_tile_pixels != 4 * 512 * 512 ||
      !four_tile_brush_result.base_rendered_image_hit ||
      four_tile_brush_result.preview_image.isNull()) {
    std::cerr << "four-tile brush success=" << four_tile_brush_result.success
              << " backend=" << four_tile_brush_result.backend.toStdString()
              << " adjustedStageTiles=" << four_tile_brush_result.adjusted_stage_tile_count
              << " adjustedStagePixels=" << four_tile_brush_result.adjusted_stage_pixels
              << " tiles=" << four_tile_brush_result.executed_tile_count
              << " pixels=" << four_tile_brush_result.executed_tile_pixels
              << " baseRendered=" << four_tile_brush_result.base_rendered_image_hit << '\n';
    return fail("multi-tile brush dirty regions should scale with the affected tile count");
  }
  auto four_tile_reference_request = four_tile_brush_request;
  four_tile_reference_request.full_invalidation = true;
  four_tile_reference_request.dirty_region = QRectF();
  four_tile_reference_request.compare_mode = true;
  const auto four_tile_reference_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          four_tile_reference_request);
  const auto four_tile_delta = max_channel_delta(
      four_tile_brush_result.preview_image,
      four_tile_reference_result.preview_image);
  if (!four_tile_reference_result.success ||
      four_tile_reference_result.preview_image.isNull() ||
      four_tile_delta > 1) {
    std::cerr << "four-tile brush parity full_success="
              << four_tile_reference_result.success
              << " delta=" << four_tile_delta << '\n';
    return fail("multi-tile brush adjusted tile render should match the full-frame render");
  }
  auto four_tile_patch_request = four_tile_brush_request;
  four_tile_patch_request.allow_texture_patch_only = true;
  const auto four_tile_patch_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          four_tile_patch_request);
  if (!four_tile_patch_result.success ||
      four_tile_patch_result.backend != QStringLiteral("adjusted_tile_cache") ||
      four_tile_patch_result.adjusted_stage_tile_count != 4 ||
      four_tile_patch_result.adjusted_stage_pixels != 661 * 661 ||
      four_tile_patch_result.executed_tile_count != 4 ||
      four_tile_patch_result.executed_tile_pixels != 4 * 512 * 512 ||
      four_tile_patch_result.texture_upload_pixels >= 4 * 512 * 512 ||
      four_tile_patch_result.texture_patch_base_copy_pixels != 0 ||
      four_tile_patch_result.texture_upload_rect != QRect(79, 79, 661, 661) ||
      !four_tile_patch_result.partial_texture_upload_candidate ||
      !four_tile_patch_result.base_rendered_image_hit ||
      !four_tile_patch_result.preview_image.isNull() ||
      four_tile_patch_result.texture_upload_image.size() != QSize(661, 661)) {
    std::cerr << "four-tile patch success=" << four_tile_patch_result.success
              << " backend=" << four_tile_patch_result.backend.toStdString()
              << " adjustedStageTiles=" << four_tile_patch_result.adjusted_stage_tile_count
              << " adjustedStagePixels=" << four_tile_patch_result.adjusted_stage_pixels
              << " tiles=" << four_tile_patch_result.executed_tile_count
              << " pixels=" << four_tile_patch_result.executed_tile_pixels
              << " uploadPixels=" << four_tile_patch_result.texture_upload_pixels
              << " baseCopyPixels=" << four_tile_patch_result.texture_patch_base_copy_pixels
              << " uploadRect=" << four_tile_patch_result.texture_upload_rect.x() << ","
              << four_tile_patch_result.texture_upload_rect.y() << ","
              << four_tile_patch_result.texture_upload_rect.width() << ","
              << four_tile_patch_result.texture_upload_rect.height()
              << " patchImageNull=" << four_tile_patch_result.texture_upload_image.isNull()
              << " fullImageNull=" << four_tile_patch_result.preview_image.isNull()
              << " baseRendered=" << four_tile_patch_result.base_rendered_image_hit << '\n';
    return fail("multi-tile texture patch render should publish only the brush-effect patch bounds");
  }
  const auto four_tile_patch_delta = max_channel_delta(
      four_tile_patch_result.texture_upload_image,
      four_tile_reference_result.preview_image.copy(four_tile_patch_result.texture_upload_rect));
  if (four_tile_patch_delta > 1) {
    std::cerr << "four-tile patch parity delta=" << four_tile_patch_delta << '\n';
    return fail("multi-tile texture patch upload image should match the full-frame render crop");
  }
  auto global_exposure_seed_request = localized_seed_request;
  global_exposure_seed_request.state.exposure = 0.20;
  const auto global_exposure_seed_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          global_exposure_seed_request);
  if (!global_exposure_seed_result.success ||
      global_exposure_seed_result.executed_tile_count != 16 ||
      global_exposure_seed_result.executed_tile_pixels != 2048 * 2048) {
    std::cerr << "global exposure seed success=" << global_exposure_seed_result.success
              << " backend=" << global_exposure_seed_result.backend.toStdString()
              << " tiles=" << global_exposure_seed_result.executed_tile_count
              << " pixels=" << global_exposure_seed_result.executed_tile_pixels << '\n';
    return fail("global exposure max-preview render should seed adjusted and rendered tile caches");
  }
  auto global_exposure_brush_request = brush_tile_reuse_request;
  global_exposure_brush_request.state.exposure = 0.20;
  const auto global_exposure_brush_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          global_exposure_brush_request);
  if (!global_exposure_brush_result.success ||
      global_exposure_brush_result.backend != QStringLiteral("adjusted_tile_cache") ||
      global_exposure_brush_result.adjusted_stage_tile_count != 1 ||
      global_exposure_brush_result.adjusted_stage_pixels != 512 * 512 ||
      global_exposure_brush_result.executed_tile_count != 1 ||
      global_exposure_brush_result.executed_tile_pixels != 512 * 512 ||
      !global_exposure_brush_result.base_rendered_image_hit ||
      global_exposure_brush_result.preview_image.isNull()) {
    std::cerr << "global exposure brush success=" << global_exposure_brush_result.success
              << " backend=" << global_exposure_brush_result.backend.toStdString()
              << " adjustedStageTiles="
              << global_exposure_brush_result.adjusted_stage_tile_count
              << " adjustedStagePixels=" << global_exposure_brush_result.adjusted_stage_pixels
              << " tiles=" << global_exposure_brush_result.executed_tile_count
              << " pixels=" << global_exposure_brush_result.executed_tile_pixels
              << " baseRendered=" << global_exposure_brush_result.base_rendered_image_hit
              << '\n';
    return fail("brush-over-input-stage exposure should use the adjusted-tile shortcut");
  }
  auto global_exposure_reference_request = global_exposure_brush_request;
  global_exposure_reference_request.full_invalidation = true;
  global_exposure_reference_request.dirty_region = QRectF();
  global_exposure_reference_request.compare_mode = true;
  const auto global_exposure_reference_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          global_exposure_reference_request);
  const auto global_exposure_delta = max_channel_delta(
      global_exposure_brush_result.preview_image,
      global_exposure_reference_result.preview_image);
  if (!global_exposure_reference_result.success ||
      global_exposure_reference_result.preview_image.isNull() ||
      global_exposure_delta > 1) {
    std::cerr << "global exposure brush parity full_success="
              << global_exposure_reference_result.success
              << " delta=" << global_exposure_delta << '\n';
    return fail("brush-over-input-stage exposure tile render should match the full-frame render");
  }
  auto unsupported_post_color_brush_request = brush_tile_reuse_request;
  unsupported_post_color_brush_request.state.saturation = 18.0;
  const auto unsupported_post_color_brush_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          unsupported_post_color_brush_request);
  if (!unsupported_post_color_brush_result.success ||
      unsupported_post_color_brush_result.backend == QStringLiteral("adjusted_tile_cache") ||
      unsupported_post_color_brush_result.adjusted_stage_tile_count != 0 ||
      unsupported_post_color_brush_result.adjusted_stage_pixels != 0) {
    std::cerr << "unsupported post-color brush success="
              << unsupported_post_color_brush_result.success
              << " backend=" << unsupported_post_color_brush_result.backend.toStdString()
              << " adjustedStageTiles="
              << unsupported_post_color_brush_result.adjusted_stage_tile_count
              << " adjustedStagePixels="
              << unsupported_post_color_brush_result.adjusted_stage_pixels << '\n';
    return fail("brush-over-post-color edits should not use the adjusted-tile shortcut until stage composition is supported");
  }
  auto unsupported_global_brush_request = brush_tile_reuse_request;
  unsupported_global_brush_request.state.contrast = 18.0;
  const auto unsupported_global_brush_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          unsupported_global_brush_request);
  if (!unsupported_global_brush_result.success ||
      unsupported_global_brush_result.backend == QStringLiteral("adjusted_tile_cache") ||
      unsupported_global_brush_result.adjusted_stage_tile_count != 0 ||
      unsupported_global_brush_result.adjusted_stage_pixels != 0) {
    std::cerr << "unsupported global brush success=" << unsupported_global_brush_result.success
              << " backend=" << unsupported_global_brush_result.backend.toStdString()
              << " adjustedStageTiles="
              << unsupported_global_brush_result.adjusted_stage_tile_count
              << " adjustedStagePixels=" << unsupported_global_brush_result.adjusted_stage_pixels
              << '\n';
    return fail("brush-over-global tone edits should not use the adjusted-tile shortcut until stage composition is supported");
  }

  QImage non_square_tile_source(1024, 512, QImage::Format_ARGB32);
  non_square_tile_source.fill(QColor::fromRgb(66, 91, 112));
  const auto non_square_tile_path = runtime_root.filePath(QStringLiteral("non-square-tile-plan.png"));
  if (!non_square_tile_source.save(non_square_tile_path)) {
    return fail("non-square localized tile source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController non_square_tile_controller;
  non_square_tile_controller.loadAsset(
      QStringLiteral("non-square-tile-plan"),
      non_square_tile_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(non_square_tile_controller)) {
    return fail("non-square localized tile controller should finish its initial preview");
  }
  auto non_square_seed_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          non_square_tile_controller);
  non_square_seed_request.use_in_memory_preview = true;
  non_square_seed_request.render_reason = QStringLiteral("edit_change");
  non_square_seed_request.dirty_domain = QStringLiteral("source_decode");
  non_square_seed_request.full_invalidation = false;
  non_square_seed_request.dirty_region = QRectF(0.0, 0.0, 1.0, 1.0);
  const auto non_square_seed_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          non_square_seed_request);
  if (!non_square_seed_result.success ||
      non_square_seed_result.executed_tile_count != 2 ||
      non_square_seed_result.executed_tile_pixels != 1024 * 512) {
    std::cerr << "non-square seed success=" << non_square_seed_result.success
              << " backend=" << non_square_seed_result.backend.toStdString()
              << " tiles=" << non_square_seed_result.executed_tile_count
              << " pixels=" << non_square_seed_result.executed_tile_pixels
              << " output=" << non_square_seed_result.output_width << "x"
              << non_square_seed_result.output_height << '\n';
    return fail("non-square full max-preview render should seed rendered tile cache entries");
  }
  auto unsupported_geometry_brush_request = non_square_seed_request;
  unsupported_geometry_brush_request.full_invalidation = false;
  unsupported_geometry_brush_request.dirty_region = QRectF(0.0, 0.0, 0.10, 0.10);
  unsupported_geometry_brush_request.dirty_domain = QStringLiteral("mask_rasterization");
  unsupported_geometry_brush_request.tile_padding_px = 64;
  unsupported_geometry_brush_request.state.mask_stack = QVariantList{brush_mask};
  unsupported_geometry_brush_request.state.active_mask_index = 0;
  const auto unsupported_geometry_brush_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          unsupported_geometry_brush_request);
  if (!unsupported_geometry_brush_result.success ||
      unsupported_geometry_brush_result.backend == QStringLiteral("adjusted_tile_cache") ||
      unsupported_geometry_brush_result.adjusted_stage_tile_count != 0 ||
      unsupported_geometry_brush_result.adjusted_stage_pixels != 0) {
    std::cerr << "unsupported geometry brush success="
              << unsupported_geometry_brush_result.success
              << " backend=" << unsupported_geometry_brush_result.backend.toStdString()
              << " adjustedStageTiles="
              << unsupported_geometry_brush_result.adjusted_stage_tile_count
              << " adjustedStagePixels="
              << unsupported_geometry_brush_result.adjusted_stage_pixels << '\n';
    return fail("non-uniform tile-local brush geometry should fall back to the full adjusted path");
  }

  photo_editor::desktop::EditorPreviewController brush_tile_controller;
  brush_tile_controller.loadAsset(
      QStringLiteral("brush-tile-plan"),
      localized_tile_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(brush_tile_controller)) {
    return fail("brush tile controller should finish its initial preview");
  }
  brush_tile_controller.addMask(QStringLiteral("brush"));
  if (!wait_for_preview_idle(brush_tile_controller)) {
    return fail("brush tile controller should settle after adding a brush mask");
  }
  brush_tile_controller.setPreviewViewport(0.0, 0.0, 0.25, 0.25, 512, true);
  brush_tile_controller.setActiveMaskBrushSize(0.04);
  brush_tile_controller.setActiveMaskBrushSpacing(0.20);
  if (!wait_for_preview_idle(brush_tile_controller)) {
    return fail("brush tile controller should settle after brush setup");
  }
  const auto brush_tile_final_x = static_cast<int>(std::lround(
      brush_tile_controller.activeMaskBrushSizeNormalized() * 2.0 *
      static_cast<double>(std::max(1, localized_tile_source.width() - 1))));
  const auto brush_tile_final_y = static_cast<int>(std::lround(
      brush_tile_controller.activeMaskBrushSizeNormalized() * 2.0 *
      static_cast<double>(std::max(1, localized_tile_source.height() - 1))));
  const auto brush_tile_size_summary = brush_tile_controller.activeMaskBrushSizeSummary();
  if (!almost_equal(brush_tile_controller.activeMaskBrushSizeNormalized(), 0.04) ||
      brush_tile_controller.activeMaskBrushPreviewDiameterPx() <= 0 ||
      brush_tile_controller.activeMaskBrushFinalDiameterXPx() != std::max(1, brush_tile_final_x) ||
      brush_tile_controller.activeMaskBrushFinalDiameterYPx() != std::max(1, brush_tile_final_y) ||
      !brush_tile_size_summary.contains(QStringLiteral("Preview ")) ||
      !brush_tile_size_summary.contains(QStringLiteral("Stored 4% radius")) ||
      !brush_tile_size_summary.contains(
          QStringLiteral("Final %1 x %2 px")
              .arg(brush_tile_controller.activeMaskBrushFinalDiameterXPx())
              .arg(brush_tile_controller.activeMaskBrushFinalDiameterYPx()))) {
    return fail("image-backed brush masks should expose normalized storage plus preview and final pixel size units");
  }
  photo_editor::desktop::EditorPreviewControllerTestAccess::clear_mask_tile_cache();
  brush_tile_controller.beginActiveBrushStroke();
  brush_tile_controller.addBrushPoint(0.10, 0.12);
  brush_tile_controller.addBrushPoint(0.18, 0.16);
  (void)finish_brush_stroke(brush_tile_controller);
  const auto brush_raster_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          brush_tile_controller);
  const auto brush_tile_mask =
      brush_tile_controller.maskStack().at(brush_tile_controller.activeMaskIndex()).toMap();
  if (brush_raster_telemetry.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("brush_rasterization") ||
      brush_raster_telemetry.value(QStringLiteral("mask_raster_ms")).toLongLong() < 0 ||
      brush_raster_telemetry.value(QStringLiteral("cursor_budget_ms")).toLongLong() != 16 ||
      brush_raster_telemetry.value(QStringLiteral("live_stroke_budget_ms")).toLongLong() != 33 ||
      brush_raster_telemetry.value(QStringLiteral("preview_matte_budget_ms")).toLongLong() != 100 ||
      !brush_raster_telemetry.contains(QStringLiteral("cursor_within_latency_budget")) ||
      !brush_raster_telemetry.contains(QStringLiteral("live_stroke_within_latency_budget")) ||
      !brush_raster_telemetry.contains(QStringLiteral("preview_matte_within_latency_budget")) ||
      !brush_raster_telemetry.value(QStringLiteral("dirty_region_valid")).toBool() ||
      brush_raster_telemetry.value(QStringLiteral("dirty_region_width")).toDouble() <= 0.0 ||
      brush_raster_telemetry.value(QStringLiteral("dirty_region_area")).toDouble() <= 0.0 ||
      brush_raster_telemetry.value(QStringLiteral("raster_dirty_width")).toInt() <= 0 ||
      brush_raster_telemetry.value(QStringLiteral("brush_commit_final_raster_width")).toInt() !=
          brush_tile_mask.value(QStringLiteral("segmentationWidth")).toInt() ||
      brush_raster_telemetry.value(QStringLiteral("brush_commit_final_raster_height")).toInt() !=
          brush_tile_mask.value(QStringLiteral("segmentationHeight")).toInt() ||
      brush_raster_telemetry.value(QStringLiteral("brush_commit_dirty_rect_width")).toInt() <= 0 ||
      brush_raster_telemetry.value(QStringLiteral("brush_commit_dirty_rect_height")).toInt() <= 0 ||
      brush_raster_telemetry.value(QStringLiteral("brush_commit_dirty_rect_width")).toInt() !=
          brush_raster_telemetry.value(QStringLiteral("raster_dirty_width")).toInt() ||
      brush_raster_telemetry.value(QStringLiteral("brush_commit_dirty_rect_height")).toInt() !=
          brush_raster_telemetry.value(QStringLiteral("raster_dirty_height")).toInt() ||
      !brush_raster_telemetry.value(QStringLiteral("brush_commit_source_revision_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      brush_raster_telemetry.value(QStringLiteral("brush_commit_source_of_truth")).toString() !=
          QStringLiteral("brushStrokes") ||
      brush_raster_telemetry.value(QStringLiteral("mask_tile_cache_entries")).toInt() <= 0 ||
      !brush_raster_telemetry.contains(QStringLiteral("fallback_reason"))) {
    return fail("brush stroke commits should expose mask raster timing, final raster size, dirty rect, source revision, source-of-truth mode, cache, and fallback telemetry");
  }
  const auto brush_tile_full_matte_bytes =
      brush_tile_mask.value(QStringLiteral("segmentationData")).toByteArray().size();
  if (brush_tile_mask.value(QStringLiteral("brushAlphaSourceContract")).toString() !=
          QStringLiteral("brush-strokes-plus-alpha-tile-cache-v1") ||
      brush_tile_mask.value(QStringLiteral("brushAlphaSourceOfTruth")).toString() !=
          QStringLiteral("brushStrokes") ||
      brush_tile_mask.value(QStringLiteral("brushAlphaStrokeCount")).toInt() <= 0 ||
      brush_tile_mask.value(QStringLiteral("brushAlphaPointCount")).toInt() <= 0 ||
      brush_tile_mask.value(QStringLiteral("brushAlphaTileCacheKind")).toString() !=
          QStringLiteral("mask-alpha-tile-cache") ||
      !brush_tile_mask.value(QStringLiteral("brushAlphaTileCacheEnabled")).toBool() ||
      brush_tile_mask.value(QStringLiteral("brushAlphaTileCacheTileSize")).toInt() != 256 ||
      brush_tile_mask.value(QStringLiteral("brushAlphaTileCacheDirtyTileCount")).toInt() <= 0 ||
      !brush_tile_mask.value(QStringLiteral("brushAlphaSourceSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:"))) {
    return fail("committed brush masks should expose stroke source plus alpha tile cache metadata");
  }
  photo_editor::desktop::EditorPreviewControllerTestAccess::clear_scaled_mask_matte_cache();
  const auto scaled_thumbnail_mask_a =
      photo_editor::desktop::EditorPreviewControllerTestAccess::scaled_mask(
          brush_tile_mask,
          QSize(96, 64),
          QStringLiteral("thumbnail"));
  const auto scaled_thumbnail_mask_b =
      photo_editor::desktop::EditorPreviewControllerTestAccess::scaled_mask(
          brush_tile_mask,
          QSize(96, 64),
          QStringLiteral("thumbnail"));
  if (scaled_thumbnail_mask_a.value(QStringLiteral("segmentationWidth")).toInt() != 96 ||
      scaled_thumbnail_mask_a.value(QStringLiteral("segmentationHeight")).toInt() != 64 ||
      scaled_thumbnail_mask_b.value(QStringLiteral("segmentationWidth")).toInt() != 96 ||
      scaled_thumbnail_mask_b.value(QStringLiteral("segmentationHeight")).toInt() != 64 ||
      scaled_thumbnail_mask_a.value(QStringLiteral("segmentationData")).toByteArray() !=
          scaled_thumbnail_mask_b.value(QStringLiteral("segmentationData")).toByteArray() ||
      !scaled_thumbnail_mask_a.value(QStringLiteral("scaledMatteRebuiltFromBrushStrokes")).toBool() ||
      scaled_thumbnail_mask_a.value(QStringLiteral("scaledMatteBrushReplayPointCount")).toInt() <= 0 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              scaled_mask_matte_cache_entries() != 0 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              scaled_mask_matte_cache_bytes() != 0 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              scaled_mask_matte_cache_misses() != 0 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
               scaled_mask_matte_cache_hits() != 0) {
    return fail("brush scaled matte previews should replay stroke geometry at target size without using the low-resolution resample cache");
  }
  const auto scaled_preview_mask = photo_editor::desktop::EditorPreviewControllerTestAccess::scaled_mask(
      brush_tile_mask,
      QSize(192, 128),
      QStringLiteral("preview-zoom"));
  const auto scaled_zoom_mask = photo_editor::desktop::EditorPreviewControllerTestAccess::scaled_mask(
      brush_tile_mask,
      QSize(384, 256),
      QStringLiteral("preview-zoom"));
  const auto scaled_preview_bounds = alpha_mask_bounds(
      scaled_preview_mask.value(QStringLiteral("segmentationData")).toByteArray(),
      scaled_preview_mask.value(QStringLiteral("segmentationWidth")).toInt(),
      scaled_preview_mask.value(QStringLiteral("segmentationHeight")).toInt());
  const auto scaled_zoom_bounds = alpha_mask_bounds(
      scaled_zoom_mask.value(QStringLiteral("segmentationData")).toByteArray(),
      scaled_zoom_mask.value(QStringLiteral("segmentationWidth")).toInt(),
      scaled_zoom_mask.value(QStringLiteral("segmentationHeight")).toInt());
  if (!scaled_preview_mask.value(QStringLiteral("scaledMatteRebuiltFromBrushStrokes")).toBool() ||
      !scaled_zoom_mask.value(QStringLiteral("scaledMatteRebuiltFromBrushStrokes")).toBool() ||
      !outline_bounds_match(scaled_preview_bounds, scaled_zoom_bounds, 0.02)) {
    return fail("brush stroke replay should preserve normalized mask bounds across preview target sizes");
  }
  const auto brush_tile_mask_sets = brush_tile_controller.maskSets();
  if (brush_tile_mask_sets.size() != 1) {
    return fail("brush stroke thumbnails should expose one grouped mask set");
  }
  const auto brush_tile_mask_set = brush_tile_mask_sets.at(0).toMap();
  const auto brush_tile_sources = brush_tile_mask_set.value(QStringLiteral("sources")).toList();
  if (brush_tile_sources.size() != 1) {
    return fail("brush stroke thumbnails should expose one grouped source");
  }
  const auto brush_tile_thumbnail_source = brush_tile_sources.at(0).toMap();
  if (!brush_tile_mask_set.value(QStringLiteral("thumbnailSource")).toString().startsWith(
          QStringLiteral("data:image/png;base64,")) ||
      !brush_tile_thumbnail_source.value(QStringLiteral("thumbnailSource")).toString().startsWith(
          QStringLiteral("data:image/png;base64,")) ||
      brush_tile_thumbnail_source.value(QStringLiteral("scaledMattePurpose")).toString() !=
          QStringLiteral("thumbnail") ||
      brush_tile_thumbnail_source.value(QStringLiteral("scaledMatteSourceWidth")).toInt() <=
          brush_tile_thumbnail_source.value(QStringLiteral("scaledMatteWidth")).toInt() ||
      brush_tile_thumbnail_source.value(QStringLiteral("scaledMatteSourceHeight")).toInt() <=
          brush_tile_thumbnail_source.value(QStringLiteral("scaledMatteHeight")).toInt() ||
      brush_tile_thumbnail_source.value(QStringLiteral("scaledMatteWidth")).toInt() > 96 ||
      brush_tile_thumbnail_source.value(QStringLiteral("scaledMatteHeight")).toInt() > 96 ||
      brush_tile_thumbnail_source.value(QStringLiteral("brushAlphaSourceContract")).toString() !=
          QStringLiteral("brush-strokes-plus-alpha-tile-cache-v1") ||
      brush_tile_thumbnail_source.value(QStringLiteral("brushAlphaSourceOfTruth")).toString() !=
          QStringLiteral("brushStrokes") ||
      brush_tile_thumbnail_source.value(QStringLiteral("brushAlphaStrokeCount")).toInt() <= 0 ||
      brush_tile_thumbnail_source.value(QStringLiteral("brushAlphaPointCount")).toInt() <= 0 ||
      brush_tile_thumbnail_source.value(QStringLiteral("brushAlphaTileCacheKind")).toString() !=
          QStringLiteral("mask-alpha-tile-cache") ||
      !brush_tile_thumbnail_source.value(QStringLiteral("brushAlphaTileCacheEnabled")).toBool() ||
      brush_tile_thumbnail_source.value(QStringLiteral("brushAlphaTileCacheTileSize")).toInt() !=
          256 ||
      brush_tile_thumbnail_source.value(QStringLiteral("brushAlphaTileCacheDirtyTileCount")).toInt() <=
          0 ||
      brush_tile_thumbnail_source.value(QStringLiteral("brushAlphaSourceSignature")).toString() !=
          brush_tile_mask.value(QStringLiteral("brushAlphaSourceSignature")).toString() ||
      brush_tile_mask_set.value(QStringLiteral("scaledMattePurpose")).toString() !=
          QStringLiteral("thumbnail")) {
    return fail("brush stroke thumbnails should use scaled matte levels instead of full-resolution mattes");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::mask_tile_cache_entries() != 2 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_tile_cache_bytes() !=
          2 * 256 * 256 ||
      brush_tile_full_matte_bytes <=
          photo_editor::desktop::EditorPreviewControllerTestAccess::mask_tile_cache_bytes()) {
    return fail("brush stroke rasterization should cache strict and geometry-only aliases for the dirty matte tile");
  }
  const auto brush_tile_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          brush_tile_controller);
  const auto brush_tile_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          brush_tile_controller);
  auto cached_mask_tile_request = brush_tile_request;
  cached_mask_tile_request.use_in_memory_preview = true;
  cached_mask_tile_request.allow_texture_patch_only = false;
  cached_mask_tile_request.state.layers.clear();
  if (!cached_mask_tile_request.state.mask_stack.isEmpty()) {
    auto cached_mask_tile_mask = brush_tile_mask;
    cached_mask_tile_mask.insert(QStringLiteral("exposure"), 0.55);
    cached_mask_tile_request.state.mask_stack[0] = cached_mask_tile_mask;
  }
  auto cached_mask_tile_base_request = cached_mask_tile_request;
  cached_mask_tile_base_request.use_in_memory_preview = true;
  cached_mask_tile_base_request.state.mask_stack.clear();
  cached_mask_tile_base_request.state.active_mask_index = -1;
  const auto cached_mask_tile_base_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          cached_mask_tile_base_request);
  if (!cached_mask_tile_base_result.success ||
      !cached_mask_tile_base_result.adjusted_cache_lookup ||
      cached_mask_tile_base_result.adjusted_cache_entries <= 0) {
    std::cerr << "mask tile base seed success=" << cached_mask_tile_base_result.success
              << " adjustedLookup=" << cached_mask_tile_base_result.adjusted_cache_lookup
              << " adjustedEntries=" << cached_mask_tile_base_result.adjusted_cache_entries
              << '\n';
    return fail("dirty brush tile cache test should seed the no-mask base working image cache");
  }
  const auto cached_mask_tile_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          cached_mask_tile_request);
  if (!cached_mask_tile_result.success ||
      cached_mask_tile_result.backend != QStringLiteral("adjusted_tile_cache") ||
      cached_mask_tile_result.mask_tile_cache_hit_count <= 0) {
    std::cerr << "mask tile cache render success=" << cached_mask_tile_result.success
              << " backend=" << cached_mask_tile_result.backend.toStdString()
              << " maskTileHits=" << cached_mask_tile_result.mask_tile_cache_hit_count
              << " maskTileMisses=" << cached_mask_tile_result.mask_tile_cache_miss_count
              << '\n';
    return fail("dirty brush renders should consume cached per-mask alpha tiles for covered chunks");
  }
  if (brush_tile_intent.operation != QStringLiteral("brush_stroke") ||
      brush_tile_intent.dirty_domain != QStringLiteral("mask_rasterization") ||
      brush_tile_intent.render_reason != QStringLiteral("mask_preview_change") ||
      brush_tile_intent.schedule_full_resolution ||
      !brush_tile_intent.normalized_region.isValid() ||
      brush_tile_intent.normalized_region.x() > 0.10 ||
      brush_tile_intent.normalized_region.y() > 0.12 ||
      brush_tile_intent.normalized_region.right() < 0.18 ||
      brush_tile_intent.normalized_region.bottom() < 0.16 ||
      brush_tile_request.full_invalidation ||
      brush_tile_request.tile_size != 512 ||
      brush_tile_request.planned_tile_count != 1 ||
      brush_tile_request.planned_visible_tile_count != 1 ||
      brush_tile_request.planned_adjacent_tile_count != 0 ||
      brush_tile_request.planned_background_tile_count != 0) {
    return fail("real brush strokes should emit localized dirty regions and visible max-preview tile plans");
  }
  if (!wait_for_preview_idle(brush_tile_controller)) {
    return fail("brush tile controller should settle after localized brush tile-plan render");
  }
  brush_tile_controller.beginActiveBrushStroke();
  brush_tile_controller.addBrushPoint(0.22, 0.20);
  brush_tile_controller.addBrushPoint(0.24, 0.22);
  (void)finish_brush_stroke(brush_tile_controller);
  const auto source_free_brush_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          brush_tile_controller);
  const auto source_free_brush_mask =
      brush_tile_controller.maskStack().at(brush_tile_controller.activeMaskIndex()).toMap();
  const auto source_free_brush_width =
      source_free_brush_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto source_free_brush_height =
      source_free_brush_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (source_free_brush_telemetry.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("brush_rasterization") ||
      !source_free_brush_telemetry.value(QStringLiteral("incremental")).toBool() ||
      source_free_brush_telemetry.value(QStringLiteral("source_decoded")).toBool() ||
      !source_free_brush_telemetry.value(QStringLiteral("dirty_region_valid")).toBool() ||
      source_free_brush_telemetry.value(QStringLiteral("dirty_region_area")).toDouble() <= 0.0 ||
      source_free_brush_telemetry.value(QStringLiteral("dirty_region_area")).toDouble() >= 0.25 ||
      source_free_brush_telemetry.value(QStringLiteral("raster_dirty_width")).toInt() <= 0 ||
      source_free_brush_telemetry.value(QStringLiteral("raster_dirty_height")).toInt() <= 0 ||
      source_free_brush_telemetry.value(QStringLiteral("brush_commit_final_raster_width")).toInt() !=
          source_free_brush_width ||
      source_free_brush_telemetry.value(QStringLiteral("brush_commit_final_raster_height")).toInt() !=
          source_free_brush_height ||
      source_free_brush_telemetry.value(QStringLiteral("brush_commit_dirty_rect_width")).toInt() !=
          source_free_brush_telemetry.value(QStringLiteral("raster_dirty_width")).toInt() ||
      source_free_brush_telemetry.value(QStringLiteral("brush_commit_dirty_rect_height")).toInt() !=
          source_free_brush_telemetry.value(QStringLiteral("raster_dirty_height")).toInt() ||
      !source_free_brush_telemetry.value(QStringLiteral("brush_commit_source_revision_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      source_free_brush_telemetry.value(QStringLiteral("brush_commit_source_of_truth")).toString() !=
          QStringLiteral("brushStrokes") ||
      source_free_brush_telemetry.value(QStringLiteral("raster_dirty_width")).toInt() >=
          source_free_brush_width ||
      source_free_brush_telemetry.value(QStringLiteral("raster_dirty_height")).toInt() >=
          source_free_brush_height) {
    return fail("manual brush commits with an existing raster matte should report final raster metadata and update only the dirty raster region without source decode");
  }
  if (!wait_for_preview_idle(brush_tile_controller)) {
    return fail("brush tile controller should settle after source-free manual brush rasterization");
  }

  constexpr int kStressBrushDabCount = 10000;
  constexpr int kStressMaskComponentCount = 100;
  constexpr int kStressMatteWidth = 128;
  constexpr int kStressMatteHeight = 128;
  const auto make_stress_brush_points = []() {
    QVariantList points;
    points.reserve(kStressBrushDabCount);
    for (int index = 0; index < kStressBrushDabCount; ++index) {
      const auto column = index % 200;
      const auto row = (index / 200) % 50;
      QVariantMap point;
      point.insert(QStringLiteral("x"), 0.04 + static_cast<double>(column) / 220.0);
      point.insert(QStringLiteral("y"), 0.08 + static_cast<double>(row) / 62.0);
      point.insert(QStringLiteral("size"), 0.012 + 0.004 * static_cast<double>(index % 5) / 4.0);
      point.insert(QStringLiteral("flow"), 0.55 + 0.30 * static_cast<double>(index % 7) / 6.0);
      point.insert(QStringLiteral("pressure"), 0.45 + 0.50 * static_cast<double>(index % 11) / 10.0);
      point.insert(QStringLiteral("tiltX"), 0.0);
      point.insert(QStringLiteral("tiltY"), 0.0);
      point.insert(QStringLiteral("erase"), false);
      points.append(point);
    }
    return points;
  };
  const auto make_stress_matte = [](int ordinal) {
    QByteArray matte(kStressMatteWidth * kStressMatteHeight, char(0));
    for (int y = 0; y < kStressMatteHeight; ++y) {
      for (int x = 0; x < kStressMatteWidth; ++x) {
        if (((x + ordinal * 3) % 17) < 6 && ((y + ordinal * 5) % 19) < 7) {
          matte[y * kStressMatteWidth + x] = char(196);
        }
      }
    }
    return matte;
  };
  const auto make_stress_mask = [&](int ordinal, bool include_large_brush_history) {
    auto mask = photo_editor::desktop::default_mask_definition(QStringLiteral("brush"), ordinal + 1);
    mask.insert(QStringLiteral("name"), QStringLiteral("Stress Brush %1").arg(ordinal + 1));
    mask.insert(QStringLiteral("maskSetId"), QStringLiteral("stress-mask-set"));
    mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Stress Mask Set"));
    mask.insert(QStringLiteral("operation"), ordinal == 0 ? QStringLiteral("add") : QStringLiteral("add"));
    mask.insert(QStringLiteral("enabled"), true);
    mask.insert(QStringLiteral("brushSize"), 0.012);
    mask.insert(QStringLiteral("brushFlow"), 0.72);
    mask.insert(QStringLiteral("brushHardness"), 0.65);
    mask.insert(QStringLiteral("segmentationData"), make_stress_matte(ordinal));
    mask.insert(QStringLiteral("segmentationWidth"), kStressMatteWidth);
    mask.insert(QStringLiteral("segmentationHeight"), kStressMatteHeight);
    if (include_large_brush_history) {
      mask.insert(QStringLiteral("brushPoints"), make_stress_brush_points());
    }
    return mask;
  };

  QVariantList stress_mask_stack;
  stress_mask_stack.reserve(kStressMaskComponentCount);
  stress_mask_stack.append(make_stress_mask(0, true));
  for (int index = 1; index < kStressMaskComponentCount; ++index) {
    stress_mask_stack.append(make_stress_mask(index, false));
  }
  photo_editor::desktop::EditState stress_state;
  stress_state.mask_stack = stress_mask_stack;
  stress_state.active_mask_index = 0;
  stress_state.exposure = 0.05;
  const auto stress_recipe_json = photo_editor::desktop::serialize_edit_recipe_json(stress_state);
  const auto reparsed_stress_state = photo_editor::desktop::parse_edit_recipe_json(stress_recipe_json);
  if (reparsed_stress_state.mask_stack.size() != kStressMaskComponentCount ||
      reparsed_stress_state.mask_stack.at(0).toMap().value(QStringLiteral("brushPoints")).toList().size() !=
          kStressBrushDabCount ||
      reparsed_stress_state.mask_stack.at(99).toMap().value(QStringLiteral("segmentationData")).toByteArray().size() !=
          kStressMatteWidth * kStressMatteHeight) {
    return fail("stress recipe serialization should preserve 10,000 brush dabs and 100 mask components");
  }
  photo_editor::desktop::RenderRequest stress_request;
  stress_request.file_path = localized_tile_path;
  stress_request.state = reparsed_stress_state;
  stress_request.full_resolution = false;
  stress_request.max_edge = 512;
  stress_request.use_in_memory_preview = true;
  stress_request.full_invalidation = true;
  stress_request.render_reason = QStringLiteral("mask_stress_test");
  stress_request.generation = 1;
  stress_request.latest_generation = std::make_shared<std::atomic<int>>(1);
  const auto stress_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          stress_request);
  if (!stress_result.success || stress_result.preview_image.isNull() ||
      stress_result.preview_image.width() <= 0 || stress_result.preview_image.width() > 512) {
    return fail("stress render should handle 100 mask components over a high-resolution source without failing");
  }

  photo_editor::desktop::EditorPreviewController stress_undo_controller;
  stress_undo_controller.loadAsset(
      QStringLiteral("stress-undo"),
      final_rebuild_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(stress_undo_controller)) {
    return fail("stress undo controller should finish its initial preview");
  }
  stress_undo_controller.addMask(QStringLiteral("brush"));
  auto stress_active_mask = make_stress_mask(0, true);
  const auto existing_stress_active_mask =
      stress_undo_controller.maskStack().at(stress_undo_controller.activeMaskIndex()).toMap();
  stress_active_mask.insert(QStringLiteral("id"), existing_stress_active_mask.value(QStringLiteral("id")));
  stress_active_mask.insert(QStringLiteral("maskSetId"), existing_stress_active_mask.value(QStringLiteral("maskSetId")));
  stress_active_mask.insert(QStringLiteral("maskSetName"), stress_undo_controller.activeMaskSetName());
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          stress_undo_controller,
          stress_active_mask)) {
    return fail("stress undo controller should accept a 10,000-dab active brush mask");
  }
  if (stress_undo_controller.activeMaskBrushPointCount() != kStressBrushDabCount) {
    std::cerr << "stress active brush point count after update="
              << stress_undo_controller.activeMaskBrushPointCount() << '\n';
    return fail("stress undo controller should retain all generated brush dabs after update");
  }
  constexpr int kStressUndoRedoRounds = 3;
  for (int round = 0; round < kStressUndoRedoRounds; ++round) {
    stress_undo_controller.setActiveMaskExposure(static_cast<double>(round + 1));
  }
  if (!stress_undo_controller.canUndo()) {
    return fail("stress undo controller should retain undo history for repeated large-mask edits");
  }
  const auto stress_latest_exposure = stress_undo_controller.activeMaskExposure();
  auto stress_previous_exposure = 0.0;
  auto stress_previous_exposure_captured = false;
  for (int round = 0; round < kStressUndoRedoRounds; ++round) {
    stress_undo_controller.undo();
    if (!stress_previous_exposure_captured) {
      stress_previous_exposure = stress_undo_controller.activeMaskExposure();
      stress_previous_exposure_captured = true;
    }
    if (stress_undo_controller.activeMaskBrushPointCount() != kStressBrushDabCount ||
        !almost_equal(stress_undo_controller.activeMaskExposure(), stress_previous_exposure) ||
        !(stress_previous_exposure < stress_latest_exposure) ||
        !stress_undo_controller.canRedo()) {
      std::cerr << "stress undo cycle=" << round
                << " pointCount=" << stress_undo_controller.activeMaskBrushPointCount()
                << " exposure=" << stress_undo_controller.activeMaskExposure()
                << " canRedo=" << stress_undo_controller.canRedo()
                << " maskCount=" << stress_undo_controller.maskCount()
                << " activeIndex=" << stress_undo_controller.activeMaskIndex() << '\n';
      return fail("repeated undo should restore the previous large-mask edit without losing brush dabs");
    }
    stress_undo_controller.redo();
    if (stress_undo_controller.activeMaskBrushPointCount() != kStressBrushDabCount ||
        !almost_equal(stress_undo_controller.activeMaskExposure(), stress_latest_exposure)) {
      std::cerr << "stress redo cycle=" << round
                << " pointCount=" << stress_undo_controller.activeMaskBrushPointCount()
                << " exposure=" << stress_undo_controller.activeMaskExposure()
                << " maskCount=" << stress_undo_controller.maskCount()
                << " activeIndex=" << stress_undo_controller.activeMaskIndex() << '\n';
      return fail("repeated redo should restore the latest large-mask edit without corrupting brush history");
    }
  }
  if (!wait_for_preview_idle(stress_undo_controller, 12000)) {
    return fail("stress undo controller should settle after repeated undo/redo renders");
  }

  photo_editor::desktop::EditorPreviewController pure_global_burst_controller;
  pure_global_burst_controller.loadAsset(
      QStringLiteral("pure-global-burst"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(pure_global_burst_controller)) {
    return fail("pure global burst controller should finish its initial preview");
  }
  pure_global_burst_controller.setExposure(0.15);
  pure_global_burst_controller.setContrast(10.0);
  const auto pure_global_live_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          pure_global_burst_controller);
  if (pure_global_live_request.render_mode != QStringLiteral("live_preview") ||
      pure_global_live_request.dirty_domain != QStringLiteral("global_tone_color")) {
    return fail("pure global edit burst should schedule one live preview");
  }
  if (!wait_for_preview_idle(pure_global_burst_controller)) {
    return fail("pure global edit burst should settle after promoted live render");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::draft_promoted_count(
          pure_global_burst_controller) != 1) {
    return fail("exact live preview should be promoted instead of rendering a duplicate settled preview");
  }

  photo_editor::desktop::EditorPreviewController detail_slider_controller;
  detail_slider_controller.loadAsset(
      QStringLiteral("detail-slider-burst"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(detail_slider_controller)) {
    return fail("detail slider controller should finish its initial preview");
  }
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  detail_slider_controller.setSharpening(18.0);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          detail_slider_controller)) {
    return fail("the first detail slider edit should schedule a settled full-resolution render");
  }
  const auto first_detail_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          detail_slider_controller);
  const auto first_detail_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          detail_slider_controller);
  const auto first_detail_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          detail_slider_controller);
  const auto first_detail_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          detail_slider_controller);
  if (first_detail_request.render_mode != QStringLiteral("settled_preview") ||
      first_detail_request.render_priority != 1 ||
      first_detail_request.dirty_domain != QStringLiteral("detail_spatial") ||
      first_detail_request.dirty_stages.isEmpty() ||
      first_detail_request.dirty_stages.first() != QStringLiteral("detail_spatial") ||
      !first_detail_request.dirty_stages.contains(QStringLiteral("display_transform")) ||
      first_detail_request.dirty_stages.contains(QStringLiteral("global_tone_color")) ||
      first_detail_full_request.render_mode != QStringLiteral("max_preview") ||
      first_detail_full_request.render_priority != 3 ||
      first_detail_full_request.dirty_domain != QStringLiteral("detail_spatial") ||
      first_detail_full_request.dirty_stages != first_detail_request.dirty_stages ||
      first_detail_intent.operation != QStringLiteral("sharpening") ||
      first_detail_intent.dirty_domain != QStringLiteral("detail_spatial") ||
      !first_detail_intent.schedule_full_resolution) {
    return fail("detail slider edits should emit typed detail-spatial intents before rendering");
  }
  detail_slider_controller.setSharpenMasking(35.0);
  detail_slider_controller.setLuminanceNr(22.0);
  detail_slider_controller.setColorNr(14.0);
  const auto rapid_detail_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          detail_slider_controller);
  const auto rapid_detail_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          detail_slider_controller);
  const auto rapid_detail_pending_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::pending_full_render_request(
          detail_slider_controller);
  const auto rapid_detail_pending_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::pending_full_render_state(
          detail_slider_controller);
  const auto last_detail_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          detail_slider_controller);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
          detail_slider_controller) != 4 ||
      rapid_detail_generation != first_detail_generation + 3 ||
      rapid_detail_request.render_mode != QStringLiteral("live_preview") ||
      rapid_detail_request.render_priority != 0 ||
      rapid_detail_request.dirty_domain != QStringLiteral("detail_spatial") ||
      rapid_detail_request.dirty_stages.isEmpty() ||
      rapid_detail_request.dirty_stages.first() != QStringLiteral("detail_spatial") ||
      rapid_detail_request.dirty_stages.contains(QStringLiteral("global_tone_color")) ||
      rapid_detail_pending_full_request.render_mode != QStringLiteral("max_preview") ||
      rapid_detail_pending_full_request.render_priority != 3 ||
      rapid_detail_pending_full_request.dirty_domain != QStringLiteral("detail_spatial") ||
      rapid_detail_pending_full_request.dirty_stages != rapid_detail_request.dirty_stages ||
      !almost_equal(rapid_detail_pending_state.sharpening, 18.0) ||
      !almost_equal(rapid_detail_pending_state.sharpen_masking, 35.0) ||
      !almost_equal(rapid_detail_pending_state.luminance_nr, 22.0) ||
      !almost_equal(rapid_detail_pending_state.color_nr, 14.0) ||
      last_detail_intent.operation != QStringLiteral("color_nr") ||
      last_detail_intent.dirty_domain != QStringLiteral("detail_spatial")) {
    return fail("rapid detail slider edits should coalesce into one typed detail-spatial render pair");
  }
  if (!wait_for_preview_idle(detail_slider_controller)) {
    return fail("detail slider controller should settle after deferred full render");
  }

  photo_editor::desktop::EditorPreviewController geometry_slider_controller;
  geometry_slider_controller.loadAsset(
      QStringLiteral("geometry-slider-burst"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(geometry_slider_controller)) {
    return fail("geometry slider controller should finish its initial preview");
  }
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  geometry_slider_controller.setLensDistortion(-12.0);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          geometry_slider_controller)) {
    return fail("the first geometry slider edit should schedule a settled full-resolution render");
  }
  const auto first_geometry_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          geometry_slider_controller);
  const auto first_geometry_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          geometry_slider_controller);
  const auto first_geometry_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          geometry_slider_controller);
  const auto first_geometry_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          geometry_slider_controller);
  if (first_geometry_request.render_mode != QStringLiteral("settled_preview") ||
      first_geometry_request.render_priority != 1 ||
      first_geometry_request.dirty_domain != QStringLiteral("geometry_transform") ||
      first_geometry_request.dirty_stages.isEmpty() ||
      first_geometry_request.dirty_stages.first() != QStringLiteral("geometry_transform") ||
      first_geometry_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      !first_geometry_request.dirty_stages.contains(QStringLiteral("display_transform")) ||
      first_geometry_full_request.render_mode != QStringLiteral("max_preview") ||
      first_geometry_full_request.render_priority != 3 ||
      first_geometry_full_request.dirty_domain != QStringLiteral("geometry_transform") ||
      first_geometry_full_request.dirty_stages != first_geometry_request.dirty_stages ||
      first_geometry_intent.operation != QStringLiteral("lens_distortion") ||
      first_geometry_intent.dirty_domain != QStringLiteral("geometry_transform") ||
      !first_geometry_intent.schedule_full_resolution) {
    return fail("geometry slider edits should emit typed geometry-transform intents before rendering");
  }
  geometry_slider_controller.setLensVignette(18.0);
  geometry_slider_controller.setChromaticAberration(26.0);
  geometry_slider_controller.setPerspectiveVertical(-14.0);
  geometry_slider_controller.setPerspectiveHorizontal(11.0);
  geometry_slider_controller.setTransformRotation(3.5);
  geometry_slider_controller.setTransformScale(1.20);
  geometry_slider_controller.setTransformSkewX(-9.0);
  geometry_slider_controller.setTransformSkewY(7.0);
  geometry_slider_controller.setLiquifyCenterX(0.40);
  geometry_slider_controller.setLiquifyCenterY(0.60);
  geometry_slider_controller.setLiquifyRadius(0.32);
  geometry_slider_controller.setLiquifyStrength(28.0);
  geometry_slider_controller.setContentAwareCropFill(true);
  const auto rapid_geometry_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          geometry_slider_controller);
  const auto rapid_geometry_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          geometry_slider_controller);
  const auto rapid_geometry_pending_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::pending_full_render_request(
          geometry_slider_controller);
  const auto rapid_geometry_pending_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::pending_full_render_state(
          geometry_slider_controller);
  const auto last_geometry_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          geometry_slider_controller);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
          geometry_slider_controller) != 14 ||
      rapid_geometry_generation != first_geometry_generation + 13 ||
      rapid_geometry_request.render_mode != QStringLiteral("live_preview") ||
      rapid_geometry_request.render_priority != 0 ||
      rapid_geometry_request.dirty_domain != QStringLiteral("geometry_transform") ||
      rapid_geometry_request.dirty_stages.isEmpty() ||
      rapid_geometry_request.dirty_stages.first() != QStringLiteral("geometry_transform") ||
      rapid_geometry_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      rapid_geometry_pending_full_request.render_mode != QStringLiteral("max_preview") ||
      rapid_geometry_pending_full_request.render_priority != 3 ||
      rapid_geometry_pending_full_request.dirty_domain != QStringLiteral("geometry_transform") ||
      rapid_geometry_pending_full_request.dirty_stages != rapid_geometry_request.dirty_stages ||
      !almost_equal(rapid_geometry_pending_state.lens_distortion, -12.0) ||
      !almost_equal(rapid_geometry_pending_state.lens_vignette, 18.0) ||
      !almost_equal(rapid_geometry_pending_state.chromatic_aberration, 26.0) ||
      !almost_equal(rapid_geometry_pending_state.perspective_vertical, -14.0) ||
      !almost_equal(rapid_geometry_pending_state.perspective_horizontal, 11.0) ||
      !almost_equal(rapid_geometry_pending_state.transform_rotation, 3.5) ||
      !almost_equal(rapid_geometry_pending_state.transform_scale, 1.20) ||
      !almost_equal(rapid_geometry_pending_state.transform_skew_x, -9.0) ||
      !almost_equal(rapid_geometry_pending_state.transform_skew_y, 7.0) ||
      !almost_equal(rapid_geometry_pending_state.liquify_center_x, 0.40) ||
      !almost_equal(rapid_geometry_pending_state.liquify_center_y, 0.60) ||
      !almost_equal(rapid_geometry_pending_state.liquify_radius, 0.32) ||
      !almost_equal(rapid_geometry_pending_state.liquify_strength, 28.0) ||
      !rapid_geometry_pending_state.content_aware_crop_fill ||
      last_geometry_intent.operation != QStringLiteral("content_aware_crop_fill") ||
      last_geometry_intent.dirty_domain != QStringLiteral("geometry_transform")) {
    return fail("rapid geometry slider edits should coalesce into one typed geometry-transform render pair");
  }
  if (!wait_for_preview_idle(geometry_slider_controller)) {
    return fail("geometry slider controller should settle after deferred full render");
  }

  photo_editor::desktop::EditorPreviewController crop_intent_controller;
  crop_intent_controller.loadAsset(
      QStringLiteral("crop-intent-burst"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(crop_intent_controller)) {
    return fail("crop intent controller should finish its initial preview");
  }
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  crop_intent_controller.updateCropRect(0.10, 0.20, 0.60, 0.70);
  const auto first_crop_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          crop_intent_controller);
  const auto first_crop_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          crop_intent_controller);
  const auto first_crop_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          crop_intent_controller);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          crop_intent_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          crop_intent_controller) ||
      first_crop_request.render_mode != QStringLiteral("settled_preview") ||
      first_crop_request.render_priority != 1 ||
      first_crop_request.dirty_domain != QStringLiteral("geometry_transform") ||
      first_crop_request.dirty_stages.isEmpty() ||
      first_crop_request.dirty_stages.first() != QStringLiteral("geometry_transform") ||
      first_crop_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      first_crop_intent.operation != QStringLiteral("crop_rect") ||
      first_crop_intent.dirty_domain != QStringLiteral("geometry_transform") ||
      first_crop_intent.render_reason != QStringLiteral("crop_change") ||
      first_crop_intent.schedule_full_resolution) {
    return fail("crop rectangle edits should emit typed preview-only geometry intents");
  }
  crop_intent_controller.setCropZoom(1.5);
  crop_intent_controller.setCropPreset(QStringLiteral("1:1"));
  crop_intent_controller.resetCropRect();
  const auto rapid_crop_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          crop_intent_controller);
  const auto rapid_crop_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          crop_intent_controller);
  const auto rapid_crop_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          crop_intent_controller);
  const auto rapid_crop_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(
          crop_intent_controller);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
          crop_intent_controller) != 4 ||
      rapid_crop_generation != first_crop_generation + 3 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          crop_intent_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          crop_intent_controller) ||
      rapid_crop_request.render_mode != QStringLiteral("live_preview") ||
      rapid_crop_request.render_priority != 0 ||
      rapid_crop_request.dirty_domain != QStringLiteral("geometry_transform") ||
      rapid_crop_request.dirty_stages.isEmpty() ||
      rapid_crop_request.dirty_stages.first() != QStringLiteral("geometry_transform") ||
      rapid_crop_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      !almost_equal(rapid_crop_state.crop_rect_x, 0.0) ||
      !almost_equal(rapid_crop_state.crop_rect_y, 0.0) ||
      !almost_equal(rapid_crop_state.crop_rect_width, 0.0) ||
      !almost_equal(rapid_crop_state.crop_rect_height, 0.0) ||
      !almost_equal(rapid_crop_state.crop_zoom, 1.5) ||
      rapid_crop_state.crop_preset != QStringLiteral("1:1") ||
      rapid_crop_intent.operation != QStringLiteral("reset_crop_rect") ||
      rapid_crop_intent.dirty_domain != QStringLiteral("geometry_transform") ||
      rapid_crop_intent.render_reason != QStringLiteral("crop_change") ||
      rapid_crop_intent.schedule_full_resolution) {
    return fail("rapid crop edits should coalesce as preview-only typed geometry work");
  }
  if (!wait_for_preview_idle(crop_intent_controller)) {
    return fail("crop intent controller should settle after preview-only crop render");
  }

  photo_editor::desktop::EditorPreviewController color_intent_controller;
  color_intent_controller.loadAsset(
      QStringLiteral("color-intent-burst"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(color_intent_controller)) {
    return fail("color intent controller should finish its initial preview");
  }
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  color_intent_controller.setCurveShadows(18.0);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          color_intent_controller)) {
    return fail("the first color edit should schedule a settled full-resolution render");
  }
  const auto first_color_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          color_intent_controller);
  const auto first_color_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          color_intent_controller);
  const auto first_color_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          color_intent_controller);
  const auto first_color_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          color_intent_controller);
  if (first_color_request.render_mode != QStringLiteral("settled_preview") ||
      first_color_request.render_priority != 1 ||
      first_color_request.dirty_domain != QStringLiteral("global_tone_color") ||
      first_color_request.dirty_stages.isEmpty() ||
      first_color_request.dirty_stages.first() != QStringLiteral("global_tone_color") ||
      first_color_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      !first_color_request.dirty_stages.contains(QStringLiteral("display_transform")) ||
      first_color_full_request.render_mode != QStringLiteral("max_preview") ||
      first_color_full_request.render_priority != 3 ||
      first_color_full_request.dirty_domain != QStringLiteral("global_tone_color") ||
      first_color_full_request.dirty_stages != first_color_request.dirty_stages ||
      first_color_intent.operation != QStringLiteral("curve_shadows") ||
      first_color_intent.dirty_domain != QStringLiteral("global_tone_color") ||
      !first_color_intent.schedule_full_resolution) {
    return fail("curve edits should emit typed global tone/color intents before rendering");
  }
  color_intent_controller.setColorMixerModel(QStringLiteral("HSV"));
  color_intent_controller.setHslChannel(QStringLiteral("Blues"));
  color_intent_controller.setHueAdjustment(12.0);
  color_intent_controller.setSaturationAdjustment(-8.0);
  color_intent_controller.setLuminanceAdjustment(5.0);
  color_intent_controller.setSelectiveCyan(24.0);
  color_intent_controller.setChannelMixerRedFromGreen(34.0);
  color_intent_controller.setBlackAndWhiteEnabled(true);
  color_intent_controller.setBlackAndWhiteReds(28.0);
  color_intent_controller.setColorReplacementEnabled(true);
  color_intent_controller.setColorReplacementStrength(82.0);
  color_intent_controller.setSkinToneProtectionEnabled(true);
  color_intent_controller.setColorHarmonyMode(QStringLiteral("Triadic"));
  color_intent_controller.setGradingHighlightsLuminance(6.0);
  const auto rapid_color_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          color_intent_controller);
  const auto rapid_color_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          color_intent_controller);
  const auto rapid_color_pending_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::pending_full_render_request(
          color_intent_controller);
  const auto rapid_color_pending_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::pending_full_render_state(
          color_intent_controller);
  const auto last_color_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          color_intent_controller);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
          color_intent_controller) != 15 ||
      rapid_color_generation != first_color_generation + 14 ||
      rapid_color_request.render_mode != QStringLiteral("live_preview") ||
      rapid_color_request.render_priority != 0 ||
      rapid_color_request.dirty_domain != QStringLiteral("global_tone_color") ||
      rapid_color_request.dirty_stages.isEmpty() ||
      rapid_color_request.dirty_stages.first() != QStringLiteral("global_tone_color") ||
      rapid_color_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      rapid_color_pending_full_request.render_mode != QStringLiteral("max_preview") ||
      rapid_color_pending_full_request.render_priority != 3 ||
      rapid_color_pending_full_request.dirty_domain != QStringLiteral("global_tone_color") ||
      rapid_color_pending_full_request.dirty_stages != rapid_color_request.dirty_stages ||
      !almost_equal(rapid_color_pending_state.curve_shadows, 18.0) ||
      rapid_color_pending_state.color_mixer_model != QStringLiteral("HSV") ||
      rapid_color_pending_state.hsl_channel != QStringLiteral("Blues") ||
      !almost_equal(rapid_color_pending_state.hue_adjustment, 12.0) ||
      !almost_equal(rapid_color_pending_state.saturation_adjustment, -8.0) ||
      !almost_equal(rapid_color_pending_state.luminance_adjustment, 5.0) ||
      !almost_equal(rapid_color_pending_state.mixer_channels[5].selective_cyan, 24.0) ||
      !almost_equal(rapid_color_pending_state.rgb_channel_mixer.red_from_green, 34.0) ||
      !rapid_color_pending_state.black_and_white_enabled ||
      !almost_equal(rapid_color_pending_state.black_and_white_mixer[0], 28.0) ||
      !rapid_color_pending_state.color_replacement_enabled ||
      !almost_equal(rapid_color_pending_state.color_replacement_strength, 82.0) ||
      !rapid_color_pending_state.skin_tone_protection_enabled ||
      rapid_color_pending_state.color_harmony_mode != QStringLiteral("Triadic") ||
      !almost_equal(rapid_color_pending_state.grading_highlights_luminance, 6.0) ||
      last_color_intent.operation != QStringLiteral("grading_highlights_luminance") ||
      last_color_intent.dirty_domain != QStringLiteral("global_tone_color")) {
    return fail("rapid curve and mixer edits should coalesce into one typed global tone/color render pair");
  }
  if (!wait_for_preview_idle(color_intent_controller)) {
    return fail("color intent controller should settle after deferred full render");
  }

  photo_editor::desktop::EditorPreviewController input_profile_intent_controller;
  input_profile_intent_controller.loadAsset(
      QStringLiteral("input-profile-intent"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(input_profile_intent_controller)) {
    return fail("input profile intent controller should finish its initial preview");
  }
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  input_profile_intent_controller.setInputColorProfile(QStringLiteral("ICC: Manufacturer Camera Profile"));
  const auto input_profile_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          input_profile_intent_controller);
  const auto input_profile_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          input_profile_intent_controller);
  if (input_profile_request.render_mode != QStringLiteral("settled_preview") ||
      input_profile_request.render_priority != 1 ||
      input_profile_request.dirty_domain != QStringLiteral("input_color_transform") ||
      input_profile_request.dirty_stages.isEmpty() ||
      input_profile_request.dirty_stages.first() != QStringLiteral("input_color_transform") ||
      input_profile_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      input_profile_request.dirty_stages.contains(QStringLiteral("raw_demosaic")) ||
      input_profile_intent.operation != QStringLiteral("input_color_profile") ||
      input_profile_intent.dirty_domain != QStringLiteral("input_color_transform") ||
      !input_profile_intent.schedule_full_resolution) {
    return fail("input profile edits should emit typed input-color-transform intents before rendering");
  }
  if (!wait_for_preview_idle(input_profile_intent_controller)) {
    return fail("input profile intent controller should settle after input profile render");
  }

  photo_editor::desktop::EditorPreviewController raw_intent_controller;
  raw_intent_controller.loadAsset(
      QStringLiteral("raw-intent-burst"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(raw_intent_controller)) {
    return fail("raw intent controller should finish its initial preview");
  }
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  raw_intent_controller.setRawExposure(1.10);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          raw_intent_controller)) {
    return fail("the first RAW edit should schedule a settled full-resolution render");
  }
  const auto first_raw_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          raw_intent_controller);
  const auto first_raw_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          raw_intent_controller);
  const auto first_raw_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          raw_intent_controller);
  const auto first_raw_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          raw_intent_controller);
  if (first_raw_request.render_mode != QStringLiteral("settled_preview") ||
      first_raw_request.render_priority != 1 ||
      first_raw_request.dirty_domain != QStringLiteral("raw") ||
      first_raw_request.dirty_stages.isEmpty() ||
      first_raw_request.dirty_stages.first() != QStringLiteral("raw_demosaic") ||
      first_raw_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      !first_raw_request.dirty_stages.contains(QStringLiteral("input_color_transform")) ||
      first_raw_full_request.render_mode != QStringLiteral("max_preview") ||
      first_raw_full_request.render_priority != 3 ||
      first_raw_full_request.dirty_domain != QStringLiteral("raw") ||
      first_raw_full_request.dirty_stages != first_raw_request.dirty_stages ||
      first_raw_intent.operation != QStringLiteral("raw_exposure") ||
      first_raw_intent.dirty_domain != QStringLiteral("raw") ||
      !first_raw_intent.schedule_full_resolution) {
    return fail("RAW edits should emit typed raw-demosaic intents before rendering");
  }
  raw_intent_controller.setRawBoost(0.60);
  raw_intent_controller.setRawShadowBoost(1.35);
  raw_intent_controller.setRawDetail(0.75);
  raw_intent_controller.setRawChromaNoiseReduction(0.45);
  raw_intent_controller.setRawMoireReduction(0.30);
  raw_intent_controller.setRawHotDeadPixelCleanup(0.40);
  raw_intent_controller.setRawFalseColorSuppression(0.25);
  raw_intent_controller.setRawDemosaicQuality(QStringLiteral("Full"));
  raw_intent_controller.setRawGamutMapping(false);
  raw_intent_controller.setRawLensCorrection(false);
  raw_intent_controller.setRawWhiteBalanceOverride(true);
  raw_intent_controller.setRawWhiteBalanceTemperature(6400.0);
  raw_intent_controller.setRawWhiteBalanceTint(-12.0);
  raw_intent_controller.setRawBlackLevel(0.016);
  raw_intent_controller.setRawWhiteLevel(1.16);
  raw_intent_controller.setRawCalibrationHue(-11.0);
  raw_intent_controller.setRawCalibrationSaturation(21.0);
  raw_intent_controller.setRawRedPrimaryHue(7.0);
  raw_intent_controller.setRawBluePrimarySaturation(16.0);
  const auto rapid_raw_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          raw_intent_controller);
  const auto rapid_raw_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          raw_intent_controller);
  const auto rapid_raw_pending_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::pending_full_render_request(
          raw_intent_controller);
  const auto rapid_raw_pending_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::pending_full_render_state(
          raw_intent_controller);
  const auto last_raw_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          raw_intent_controller);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
          raw_intent_controller) != 20 ||
      rapid_raw_generation != first_raw_generation + 19 ||
      rapid_raw_request.render_mode != QStringLiteral("live_preview") ||
      rapid_raw_request.render_priority != 0 ||
      rapid_raw_request.dirty_domain != QStringLiteral("raw_demosaic") ||
      rapid_raw_request.dirty_stages.isEmpty() ||
      rapid_raw_request.dirty_stages.first() != QStringLiteral("raw_demosaic") ||
      rapid_raw_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      rapid_raw_pending_full_request.render_mode != QStringLiteral("max_preview") ||
      rapid_raw_pending_full_request.render_priority != 3 ||
      rapid_raw_pending_full_request.dirty_domain != QStringLiteral("raw_demosaic") ||
      rapid_raw_pending_full_request.dirty_stages != rapid_raw_request.dirty_stages ||
      !almost_equal(rapid_raw_pending_state.raw_exposure, 1.10) ||
      !almost_equal(rapid_raw_pending_state.raw_boost, 0.60) ||
      !almost_equal(rapid_raw_pending_state.raw_shadow_boost, 1.35) ||
      !almost_equal(rapid_raw_pending_state.raw_detail, 0.75) ||
      !almost_equal(rapid_raw_pending_state.raw_chroma_noise_reduction, 0.45) ||
      !almost_equal(rapid_raw_pending_state.raw_moire_reduction, 0.30) ||
      !almost_equal(rapid_raw_pending_state.raw_hot_dead_pixel_cleanup, 0.40) ||
      !almost_equal(rapid_raw_pending_state.raw_false_color_suppression, 0.25) ||
      rapid_raw_pending_state.raw_demosaic_quality != QStringLiteral("Full") ||
      rapid_raw_pending_state.raw_gamut_mapping ||
      rapid_raw_pending_state.raw_lens_correction ||
      !rapid_raw_pending_state.raw_white_balance_override ||
      !almost_equal(rapid_raw_pending_state.raw_white_balance_temperature, 6400.0) ||
      !almost_equal(rapid_raw_pending_state.raw_white_balance_tint, -12.0) ||
      !almost_equal(rapid_raw_pending_state.raw_black_level, 0.016) ||
      !almost_equal(rapid_raw_pending_state.raw_white_level, 1.16) ||
      !almost_equal(rapid_raw_pending_state.raw_calibration_hue, -11.0) ||
      !almost_equal(rapid_raw_pending_state.raw_calibration_saturation, 21.0) ||
      !almost_equal(rapid_raw_pending_state.raw_red_primary_hue, 7.0) ||
      !almost_equal(rapid_raw_pending_state.raw_blue_primary_saturation, 16.0) ||
      last_raw_intent.operation != QStringLiteral("raw_blue_primary_saturation") ||
      last_raw_intent.dirty_domain != QStringLiteral("raw")) {
    return fail("rapid RAW edits should coalesce into one typed raw-demosaic render pair");
  }
  if (!wait_for_preview_idle(raw_intent_controller)) {
    return fail("raw intent controller should settle after deferred full render");
  }

  photo_editor::desktop::EditorPreviewController curve_preset_intent_controller;
  curve_preset_intent_controller.loadAsset(
      QStringLiteral("curve-preset-intent"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(curve_preset_intent_controller)) {
    return fail("curve preset intent controller should finish its initial preview");
  }
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  curve_preset_intent_controller.setCurveChannel(QStringLiteral("RGB"));
  if (!curve_preset_intent_controller.applyCurvePreset(QStringLiteral("Matte Lift"))) {
    return fail("curve preset intent controller should apply a built-in preset");
  }
  const auto curve_preset_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          curve_preset_intent_controller);
  const auto curve_preset_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          curve_preset_intent_controller);
  if (curve_preset_request.render_mode != QStringLiteral("settled_preview") ||
      curve_preset_request.render_priority != 1 ||
      curve_preset_request.dirty_domain != QStringLiteral("global_tone_color") ||
      curve_preset_request.dirty_stages.isEmpty() ||
      curve_preset_request.dirty_stages.first() != QStringLiteral("global_tone_color") ||
      curve_preset_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      curve_preset_intent.operation != QStringLiteral("curve_preset") ||
      curve_preset_intent.dirty_domain != QStringLiteral("global_tone_color") ||
      !curve_preset_intent.schedule_full_resolution) {
    return fail("curve presets should emit typed global tone/color intents before rendering");
  }
  if (!wait_for_preview_idle(curve_preset_intent_controller)) {
    return fail("curve preset intent controller should settle after preset render");
  }

  photo_editor::desktop::EditorPreviewController layer_intent_controller;
  layer_intent_controller.loadAsset(
      QStringLiteral("layer-intent"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(layer_intent_controller)) {
    return fail("layer intent controller should finish its initial preview");
  }
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  if (!layer_intent_controller.addFillLayer(QStringLiteral("#445566"), QStringLiteral("Proof Fill"))) {
    return fail("layer intent controller should add a fill layer");
  }
  const auto add_layer_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          layer_intent_controller);
  const auto add_layer_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          layer_intent_controller);
  const auto add_layer_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          layer_intent_controller);
  if (add_layer_request.render_mode != QStringLiteral("settled_preview") ||
      add_layer_request.render_priority != 1 ||
      add_layer_request.dirty_domain != QStringLiteral("layers_composite") ||
      add_layer_request.dirty_stages.isEmpty() ||
      add_layer_request.dirty_stages.first() != QStringLiteral("layers_composite") ||
      add_layer_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      add_layer_full_request.render_mode != QStringLiteral("max_preview") ||
      add_layer_full_request.render_priority != 3 ||
      add_layer_full_request.dirty_domain != QStringLiteral("layers_composite") ||
      add_layer_intent.operation != QStringLiteral("add_fill_layer") ||
      add_layer_intent.dirty_domain != QStringLiteral("layers_composite") ||
      !add_layer_intent.schedule_full_resolution) {
    return fail("layer creation should emit typed layer-composite intents before rendering");
  }
  if (!wait_for_preview_idle(layer_intent_controller)) {
    return fail("layer intent controller should settle after fill-layer render");
  }
  const auto layer_stack_generation_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          layer_intent_controller);
  if (!layer_intent_controller.setPhotoLayerOpacity(0, 0.42)) {
    return fail("layer intent controller should edit layer opacity");
  }
  const auto layer_stack_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          layer_intent_controller);
  const auto layer_stack_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          layer_intent_controller);
  const auto layer_stack_is_preview_only_mode =
      (layer_stack_request.render_mode == QStringLiteral("settled_preview") &&
       layer_stack_request.render_priority == 1) ||
      (layer_stack_request.render_mode == QStringLiteral("live_preview") &&
       layer_stack_request.render_priority == 0);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          layer_intent_controller) != layer_stack_generation_before + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          layer_intent_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          layer_intent_controller) ||
      !layer_stack_is_preview_only_mode ||
      layer_stack_request.dirty_domain != QStringLiteral("layers_composite") ||
      layer_stack_request.dirty_stages.isEmpty() ||
      layer_stack_request.dirty_stages.first() != QStringLiteral("layers_composite") ||
      layer_stack_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      layer_stack_request.async_job.job_kind != QStringLiteral("layer_composite") ||
      layer_stack_request.async_job.stage != QStringLiteral("render_request") ||
      !layer_stack_request.async_job.job_id.startsWith(QStringLiteral("layer_composite-")) ||
      layer_stack_request.async_job.asset_id != QStringLiteral("layer-intent") ||
      layer_stack_request.async_job.file_path != rapid_slider_path ||
      layer_stack_request.async_job.generation !=
          photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
              layer_intent_controller) ||
      layer_stack_request.async_job.state_signature.isEmpty() ||
      layer_stack_intent.operation != QStringLiteral("photo_layer_stack") ||
      layer_stack_intent.dirty_domain != QStringLiteral("layers_composite") ||
      layer_stack_intent.render_reason != QStringLiteral("photo_layer_change") ||
      layer_stack_intent.schedule_full_resolution) {
    return fail("layer stack edits should emit typed preview-only layer-composite intents");
  }
  const auto stale_layer_composite_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          layer_stack_request);
  if (!stale_layer_composite_result.success ||
      stale_layer_composite_result.cancelled ||
      stale_layer_composite_result.async_job.job_id != layer_stack_request.async_job.job_id ||
      stale_layer_composite_result.async_job.job_kind != QStringLiteral("layer_composite") ||
      stale_layer_composite_result.async_job.state_signature !=
          layer_stack_request.async_job.state_signature) {
    return fail("layer-composite render results should preserve the request async job identity");
  }
  const auto layer_composite_publish_generation_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_interactive_preview_generation(
          layer_intent_controller);
  if (!layer_intent_controller.setPhotoLayerOpacity(0, 0.47)) {
    return fail("layer intent controller should advance layer opacity before stale publish");
  }
  auto forged_stale_layer_composite_result = stale_layer_composite_result;
  forged_stale_layer_composite_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          layer_intent_controller);
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      layer_intent_controller,
      false,
      forged_stale_layer_composite_result);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::last_interactive_preview_generation(
          layer_intent_controller) != layer_composite_publish_generation_before) {
    return fail("stale layer-composite async results should not publish after layer state changes");
  }
  if (!wait_for_preview_idle(layer_intent_controller)) {
    return fail("layer intent controller should settle after opacity render");
  }

  photo_editor::desktop::EditorPreviewController layer_trace_controller;
  layer_trace_controller.loadAsset(
      QStringLiteral("layer-composite-trace"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  layer_trace_controller.addMask(QStringLiteral("linear"));
  if (!wait_for_preview_idle(layer_trace_controller)) {
    return fail("layer composite trace controller should settle after initial mask creation");
  }
  if (layer_trace_controller.photoLayerCount() != 1 ||
      !layer_trace_controller.setPhotoLayerOpacity(0, 0.73)) {
    return fail("layer composite trace controller should create and edit a masked adjustment layer");
  }
  const auto layer_trace_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          layer_trace_controller);
  const auto layer_trace_fields = layer_trace_request.mask_alpha_trace_fields;
  if (layer_trace_request.dirty_domain != QStringLiteral("layers_composite") ||
      layer_trace_request.async_job.job_kind != QStringLiteral("layer_composite") ||
      layer_trace_fields.value(QStringLiteral("mask_source_kind")).toString() !=
          QStringLiteral("layer-composite") ||
      layer_trace_fields.value(QStringLiteral("mask_alpha_provider_contract")).toString() !=
          QStringLiteral("shared-mask-alpha-provider-v1") ||
      layer_trace_fields.value(QStringLiteral("mask_alpha_provider")).toString() !=
          QStringLiteral("layer-composite") ||
      !layer_trace_fields
           .value(QStringLiteral("mask_alpha_provider_signature"))
           .toString()
           .contains(QStringLiteral("provider:layer-composite")) ||
      layer_trace_fields.value(QStringLiteral("layer_composite_masked_layer_count")).toInt() != 1 ||
      !layer_trace_fields.value(QStringLiteral("mask_source_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !layer_trace_fields.value(QStringLiteral("mask_alpha_trace_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !layer_trace_fields.value(QStringLiteral("mask_alpha_trace_signature"))
           .toString()
           .contains(QStringLiteral("layer-composite:layers:1")) ||
      !layer_trace_fields.value(QStringLiteral("mask_alpha_trace_signature"))
           .toString()
           .contains(QStringLiteral("provider:layer-composite"))) {
    return fail("layer-composite render requests should expose exact masked-layer alpha trace metadata");
  }
  const auto layer_trace_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          layer_trace_request);
  if (!layer_trace_result.success ||
      layer_trace_result.mask_alpha_trace_fields.value(QStringLiteral("mask_alpha_trace_hash"))
              .toString() !=
          layer_trace_fields.value(QStringLiteral("mask_alpha_trace_hash")).toString() ||
      layer_trace_result.mask_alpha_trace_fields.value(QStringLiteral("mask_source_hash"))
              .toString() !=
          layer_trace_fields.value(QStringLiteral("mask_source_hash")).toString()) {
    return fail("layer-composite render results should preserve exact masked-layer alpha trace metadata");
  }

  photo_editor::desktop::EditorPreviewController control_intent_controller;
  control_intent_controller.loadAsset(
      QStringLiteral("control-intent"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(control_intent_controller)) {
    return fail("control intent controller should finish its initial preview");
  }
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  const auto compare_generation_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          control_intent_controller);
  control_intent_controller.setCompareMode(true);
  const auto compare_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          control_intent_controller);
  const auto compare_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          control_intent_controller);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          control_intent_controller) != compare_generation_before + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          control_intent_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          control_intent_controller) ||
      compare_request.render_mode != QStringLiteral("settled_preview") ||
      compare_request.render_priority != 1 ||
      compare_request.dirty_domain != QStringLiteral("full_pipeline") ||
      compare_request.dirty_stages.isEmpty() ||
      compare_request.dirty_stages.first() != QStringLiteral("source_decode") ||
      compare_intent.operation != QStringLiteral("compare_toggle") ||
      compare_intent.dirty_domain != QStringLiteral("full_pipeline") ||
      compare_intent.render_reason != QStringLiteral("compare_toggle") ||
      compare_intent.schedule_full_resolution) {
    return fail("preview-only compare controls should emit typed full-pipeline intents without scheduling max preview");
  }
  if (!wait_for_preview_idle(control_intent_controller)) {
    return fail("control intent controller should settle after compare toggle render");
  }
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  control_intent_controller.setRetouchBeforeAfterEnabled(true);
  const auto retouch_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          control_intent_controller);
  const auto retouch_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          control_intent_controller);
  const auto retouch_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          control_intent_controller);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          control_intent_controller) ||
      retouch_request.render_mode != QStringLiteral("settled_preview") ||
      retouch_request.render_priority != 1 ||
      retouch_request.dirty_domain != QStringLiteral("retouch_heal") ||
      retouch_request.dirty_stages.isEmpty() ||
      retouch_request.dirty_stages.first() != QStringLiteral("retouch_heal") ||
      retouch_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      retouch_request.dirty_stages.contains(QStringLiteral("raw_demosaic")) ||
      retouch_request.dirty_stages.contains(QStringLiteral("input_color_transform")) ||
      retouch_request.dirty_stages.contains(QStringLiteral("global_tone_color")) ||
      !retouch_request.dirty_stages.contains(QStringLiteral("retouch_patch_application")) ||
      !retouch_request.dirty_stages.contains(QStringLiteral("display_transform")) ||
      !retouch_request.dirty_stages.contains(QStringLiteral("overlay_composite")) ||
      retouch_full_request.render_mode != QStringLiteral("max_preview") ||
      retouch_full_request.render_priority != 3 ||
      retouch_full_request.dirty_domain != QStringLiteral("retouch_heal") ||
      retouch_full_request.dirty_stages != retouch_request.dirty_stages ||
      retouch_intent.operation != QStringLiteral("retouch_before_after") ||
      retouch_intent.dirty_domain != QStringLiteral("retouch_heal") ||
      retouch_intent.render_reason != QStringLiteral("edit_change") ||
      !retouch_intent.schedule_full_resolution) {
    return fail("retouch before/after should invalidate retouch and downstream display stages without redirtying unrelated source stages");
  }
  if (!wait_for_preview_idle(control_intent_controller)) {
    return fail("control intent controller should settle after retouch before/after render");
  }
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  const auto compare_off_generation_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          control_intent_controller);
  control_intent_controller.setCompareMode(false);
  const auto compare_off_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          control_intent_controller);
  const auto compare_off_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          control_intent_controller);
  const auto compare_off_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          control_intent_controller);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          control_intent_controller) != compare_off_generation_before + 1 ||
      (!photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
           control_intent_controller) &&
       !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
           control_intent_controller)) ||
      compare_off_request.compare_mode ||
      compare_off_request.render_mode != QStringLiteral("settled_preview") ||
      compare_off_full_request.compare_mode ||
      compare_off_full_request.render_mode != QStringLiteral("max_preview") ||
      compare_off_intent.operation != QStringLiteral("compare_toggle") ||
      compare_off_intent.dirty_domain != QStringLiteral("full_pipeline") ||
      compare_off_intent.render_reason != QStringLiteral("compare_toggle") ||
      !compare_off_intent.schedule_full_resolution) {
    return fail("leaving compare mode should schedule a full-resolution max preview restore");
  }
  if (!wait_for_preview_idle(control_intent_controller)) {
    return fail("control intent controller should settle after compare exit restore render");
  }

  photo_editor::desktop::EditorPreviewController base_cache_controller;
  photo_editor::desktop::TestImageSink base_cache_sink;
  base_cache_controller.setImageProvider(&base_cache_sink);
  base_cache_controller.setDiagnosticOverlaysRequested(true);
  QImage base_cache_source(20, 14, QImage::Format_ARGB32);
  base_cache_source.fill(QColor::fromRgb(112, 126, 138));
  const auto base_cache_path = runtime_root.filePath(QStringLiteral("base-cache-source.png"));
  if (!base_cache_source.save(base_cache_path)) {
    return fail("base cache source image should be writable");
  }
  base_cache_controller.loadAsset(
      QStringLiteral("base-cache-hit"),
      base_cache_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(base_cache_controller)) {
    return fail("base-cache controller should finish its initial preview");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::rendered_preview_cache_hit_count(
          base_cache_controller) < 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::rendered_preview_cache_fast_schedule_count(
          base_cache_controller) < 1) {
    return fail("full-fit base max preview should reuse the visible rendered preview cache without waiting for debounce");
  }
  if (!wait_for_sink_image(base_cache_sink, QStringLiteral("diagnostic-focus-check"))) {
    return fail("deferring diagnostic overlays for cached full preview should not drop visible overlay generation");
  }

  photo_editor::desktop::EditorPreviewController display_cache_controller;
  photo_editor::desktop::TestImageSink display_cache_sink;
  display_cache_controller.setImageProvider(&display_cache_sink);
  display_cache_controller.loadAsset(
      QStringLiteral("display-cache-hit"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(display_cache_controller)) {
    return fail("display-cache controller should finish its initial preview");
  }
  const auto rendered_cache_hits_before_global_change =
      photo_editor::desktop::EditorPreviewControllerTestAccess::rendered_preview_cache_hit_count(
          display_cache_controller);
  display_cache_controller.setExposure(0.25);
  if (!wait_for_preview_idle(display_cache_controller)) {
    return fail("display-cache controller should populate full-fit rendered preview caches");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::rendered_preview_cache_hit_count(
          display_cache_controller) <= rendered_cache_hits_before_global_change) {
    return fail("full-fit max preview should reuse the already full-resolution visible rendered cache");
  }
  const auto adjusted_cache_hits_before_display_change =
      photo_editor::desktop::EditorPreviewControllerTestAccess::adjusted_preview_cache_hit_count(
          display_cache_controller);
  const auto adjusted_cache_fast_schedules_before_display_change =
      photo_editor::desktop::EditorPreviewControllerTestAccess::adjusted_preview_cache_fast_schedule_count(
          display_cache_controller);
  const auto rendered_cache_hits_before_display_change =
      photo_editor::desktop::EditorPreviewControllerTestAccess::rendered_preview_cache_hit_count(
          display_cache_controller);
  auto adjusted_cache_tile_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          display_cache_controller);
  adjusted_cache_tile_request.state.output_color_space = QStringLiteral("Adobe RGB");
  adjusted_cache_tile_request.state.preview_color_space = QStringLiteral("Adobe RGB");
  adjusted_cache_tile_request.render_reason = QStringLiteral("output_profile_change");
  adjusted_cache_tile_request.dirty_domain = QStringLiteral("display_transform");
  const auto display_cache_full_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          adjusted_cache_tile_request);
  if (!display_cache_full_result.success ||
      !display_cache_full_result.adjusted_cache_hit ||
      display_cache_full_result.executed_tile_count != 1 ||
      display_cache_full_result.executed_tile_pixels !=
          static_cast<qint64>(display_cache_full_result.output_width) *
              display_cache_full_result.output_height ||
      display_cache_full_result.output_width != 16 ||
      display_cache_full_result.output_height != 16) {
    std::cerr << "tile result success=" << display_cache_full_result.success
              << " adjusted=" << display_cache_full_result.adjusted_cache_hit
              << " disk=" << display_cache_full_result.disk_cache_hit
              << " rendered=" << display_cache_full_result.rendered_cache_hit
              << " tiles=" << display_cache_full_result.executed_tile_count
              << " pixels=" << display_cache_full_result.executed_tile_pixels
              << " output=" << display_cache_full_result.output_width << "x"
              << display_cache_full_result.output_height << '\n';
    return fail("adjusted-cache max preview output conversion should execute through planned full-frame tiles");
  }
  if (display_cache_full_result.histogram_image.isNull() ||
      display_cache_full_result.histogram_image.format() != QImage::Format_RGBA16FPx4) {
    return fail("adjusted preview scopes should receive a high-precision histogram image before display quantization");
  }
  auto final_output_policy_request = adjusted_cache_tile_request;
  final_output_policy_request.state.output_headroom = 2.0;
  final_output_policy_request.state.output_gamut_mapping_enabled = false;
  final_output_policy_request.state.output_gamut_mapping_policy = QStringLiteral("Clip");
  final_output_policy_request.state.black_point_compensation_enabled = true;
  final_output_policy_request.state.source_black_point = 0.02;
  final_output_policy_request.state.output_black_point = 0.05;
  final_output_policy_request.render_reason = QStringLiteral("output_policy_change");
  final_output_policy_request.dirty_domain = QStringLiteral("display_transform");
  const auto final_output_policy_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          final_output_policy_request);
  if (!final_output_policy_result.success ||
      !final_output_policy_result.adjusted_cache_hit ||
      final_output_policy_result.rendered_cache_key.isEmpty() ||
      final_output_policy_result.rendered_cache_key ==
          display_cache_full_result.rendered_cache_key) {
    std::cerr << "final output policy result success=" << final_output_policy_result.success
              << " adjusted=" << final_output_policy_result.adjusted_cache_hit
              << " renderedKeyEmpty="
              << final_output_policy_result.rendered_cache_key.isEmpty()
              << " sameRenderedKey="
              << (final_output_policy_result.rendered_cache_key ==
                  display_cache_full_result.rendered_cache_key)
              << '\n';
    return fail("final output color policy should invalidate rendered previews while reusing adjusted working pixels");
  }
  auto superseded_rendered_cache_request = adjusted_cache_tile_request;
  superseded_rendered_cache_request.use_in_memory_preview = true;
  superseded_rendered_cache_request.render_reason = QStringLiteral("edit_change");
  superseded_rendered_cache_request.dirty_domain = QStringLiteral("display_transform");
  superseded_rendered_cache_request.generation = 7;
  superseded_rendered_cache_request.latest_generation =
      std::make_shared<std::atomic<int>>(8);
  const auto superseded_rendered_cache_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          superseded_rendered_cache_request);
  if (!superseded_rendered_cache_result.cancelled ||
      superseded_rendered_cache_result.success ||
      !superseded_rendered_cache_result.rendered_cache_hit ||
      superseded_rendered_cache_result.decode_elapsed_ms != 0 ||
      superseded_rendered_cache_result.edit_elapsed_ms != 0 ||
      superseded_rendered_cache_result.encode_elapsed_ms != 0 ||
      !superseded_rendered_cache_result.preview_image.isNull()) {
    std::cerr << "superseded rendered-cache result cancelled="
              << superseded_rendered_cache_result.cancelled
              << " success=" << superseded_rendered_cache_result.success
              << " rendered=" << superseded_rendered_cache_result.rendered_cache_hit
              << " decode=" << superseded_rendered_cache_result.decode_elapsed_ms
              << " edit=" << superseded_rendered_cache_result.edit_elapsed_ms
              << " encode=" << superseded_rendered_cache_result.encode_elapsed_ms
              << " previewImage=" << !superseded_rendered_cache_result.preview_image.isNull()
              << '\n';
    return fail("superseded rendered-cache max previews should cancel before publication");
  }
  auto dirty_tile_patch_request = adjusted_cache_tile_request;
  dirty_tile_patch_request.use_in_memory_preview = true;
  dirty_tile_patch_request.full_invalidation = false;
  dirty_tile_patch_request.dirty_region = QRectF(0.0, 0.0, 0.10, 0.10);
  dirty_tile_patch_request.dirty_domain = QStringLiteral("mask_rasterization");
  dirty_tile_patch_request.dirty_stages = QStringList{
      QStringLiteral("mask_rasterization"),
      QStringLiteral("mask_composition"),
      QStringLiteral("local_adjustments"),
      QStringLiteral("retouch_heal"),
      QStringLiteral("retouch_patch_application"),
      QStringLiteral("detail_spatial"),
      QStringLiteral("layers_composite"),
      QStringLiteral("lut_post_color"),
      QStringLiteral("display_transform"),
      QStringLiteral("overlay_composite"),
      QStringLiteral("publish_texture"),
      QStringLiteral("encode_disk"),
      QStringLiteral("histogram_scope"),
  };
  dirty_tile_patch_request.tile_padding_px = 64;
  const auto dirty_tile_patch_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          dirty_tile_patch_request);
  if (!dirty_tile_patch_result.success ||
      !dirty_tile_patch_result.adjusted_cache_hit ||
      dirty_tile_patch_result.executed_tile_count != 1 ||
      dirty_tile_patch_result.executed_tile_pixels !=
          static_cast<qint64>(dirty_tile_patch_result.output_width) *
              dirty_tile_patch_result.output_height ||
      dirty_tile_patch_result.output_width != display_cache_full_result.output_width ||
      dirty_tile_patch_result.output_height != display_cache_full_result.output_height ||
      dirty_tile_patch_result.histogram_image.isNull() ||
      display_cache_full_result.histogram_image.isNull() ||
      max_channel_delta(
          dirty_tile_patch_result.histogram_image,
          display_cache_full_result.histogram_image) > 1) {
    std::cerr << "dirty tile patch success=" << dirty_tile_patch_result.success
              << " adjusted=" << dirty_tile_patch_result.adjusted_cache_hit
              << " tiles=" << dirty_tile_patch_result.executed_tile_count
              << " pixels=" << dirty_tile_patch_result.executed_tile_pixels
              << " output=" << dirty_tile_patch_result.output_width << "x"
              << dirty_tile_patch_result.output_height
              << " delta="
              << (dirty_tile_patch_result.histogram_image.isNull() ||
                          display_cache_full_result.histogram_image.isNull()
                      ? -1
                      : max_channel_delta(
                            dirty_tile_patch_result.histogram_image,
                            display_cache_full_result.histogram_image))
              << '\n';
    return fail("localized max-preview dirty requests should render only dirty tiles and stitch cached clean tiles");
  }
  auto direct_cpu_reference_request = adjusted_cache_tile_request;
  direct_cpu_reference_request.compare_mode = true;
  direct_cpu_reference_request.use_in_memory_preview = true;
  direct_cpu_reference_request.tile_size = 0;
  auto tiled_cpu_request = adjusted_cache_tile_request;
  tiled_cpu_request.compare_mode = true;
  tiled_cpu_request.use_in_memory_preview = true;
  const auto direct_cpu_reference_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          direct_cpu_reference_request);
  const auto tiled_cpu_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          tiled_cpu_request);
  if (!direct_cpu_reference_result.success ||
      !tiled_cpu_result.success ||
      direct_cpu_reference_result.executed_tile_count != 0 ||
      tiled_cpu_result.executed_tile_count <= 0 ||
      tiled_cpu_result.executed_tile_pixels !=
          static_cast<qint64>(tiled_cpu_result.output_width) * tiled_cpu_result.output_height ||
      direct_cpu_reference_result.histogram_image.isNull() ||
      tiled_cpu_result.histogram_image.isNull() ||
      max_channel_delta(
          direct_cpu_reference_result.histogram_image,
          tiled_cpu_result.histogram_image) > 1) {
    std::cerr << "cache-miss tile parity direct_success=" << direct_cpu_reference_result.success
              << " tiled_success=" << tiled_cpu_result.success
              << " direct_tiles=" << direct_cpu_reference_result.executed_tile_count
              << " tiled_tiles=" << tiled_cpu_result.executed_tile_count
              << " tiled_pixels=" << tiled_cpu_result.executed_tile_pixels
              << " tiled_output=" << tiled_cpu_result.output_width << "x"
              << tiled_cpu_result.output_height
              << " direct_hist=" << direct_cpu_reference_result.histogram_image.width()
              << "x" << direct_cpu_reference_result.histogram_image.height()
              << " tiled_hist=" << tiled_cpu_result.histogram_image.width()
              << "x" << tiled_cpu_result.histogram_image.height()
              << " delta="
              << (direct_cpu_reference_result.histogram_image.isNull() ||
                          tiled_cpu_result.histogram_image.isNull()
                      ? -1
                      : max_channel_delta(
                            direct_cpu_reference_result.histogram_image,
                            tiled_cpu_result.histogram_image))
              << '\n';
    return fail("cache-miss max-preview tiled output conversion should match the non-tiled reference render");
  }
  display_cache_controller.setOutputColorSpace(QStringLiteral("Display P3"));
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::adjusted_preview_cache_fast_schedule_count(
          display_cache_controller) <= adjusted_cache_fast_schedules_before_display_change ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          display_cache_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          display_cache_controller)) {
    return fail("display-only max preview work should bypass the full-render debounce while visible preview work seeds the rendered cache");
  }
  if (!wait_for_preview_idle(display_cache_controller)) {
    return fail("display-cache controller should finish its display-transform preview");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::adjusted_preview_cache_hit_count(
          display_cache_controller) <= adjusted_cache_hits_before_display_change ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::rendered_preview_cache_hit_count(
          display_cache_controller) <= rendered_cache_hits_before_display_change) {
    return fail("display-only preview changes should reuse cached adjusted working pixels for visible preview and rendered pixels for max preview");
  }
  const auto display_cache_preview = display_cache_sink.image(QStringLiteral("edited"));
  if (display_cache_preview.isNull() ||
      display_cache_preview.colorSpace() != QColorSpace(QColorSpace::DisplayP3)) {
    return fail("adjusted working cache hits should still publish the selected preview color space");
  }
  const auto display_cache_full_path =
      photo_editor::desktop::EditorPreviewControllerTestAccess::cache_path_for_selection(
          display_cache_controller,
          true,
          true);
  if (QFileInfo::exists(display_cache_full_path)) {
    return fail("edit-driven rendered-cache full previews with an image provider should not write a hot-path disk JPEG");
  }

  photo_editor::desktop::EditorPreviewController full_cache_controller;
  full_cache_controller.loadAsset(
      QStringLiteral("full-cache-hit"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(full_cache_controller)) {
    return fail("full-cache controller should finish its initial max preview");
  }
  const auto full_cache_path =
      photo_editor::desktop::EditorPreviewControllerTestAccess::cache_path_for_selection(
          full_cache_controller,
          true,
          true);
  if (!QFileInfo::exists(full_cache_path)) {
    return fail("initial full-resolution preview should create a disk preview cache entry");
  }
  photo_editor::desktop::EditorPreviewController warm_asset_select_controller;
  warm_asset_select_controller.loadAsset(
      QStringLiteral("warm-cache-hit"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          warm_asset_select_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          warm_asset_select_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_timer_active(
          warm_asset_select_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_pending(
          warm_asset_select_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::disk_preview_cache_direct_schedule_count(
          warm_asset_select_controller) <= 0) {
    return fail("warm asset selection should publish the fresh disk full-preview cache immediately");
  }
  if (!wait_for_preview_idle(warm_asset_select_controller)) {
    return fail("warm asset selection disk cache publish should settle immediately");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::disk_preview_cache_hit_count(
          warm_asset_select_controller) <= 0) {
    return fail("warm asset selection should hit the fresh disk full-preview cache");
  }
  const auto full_cache_hits_before_repeat =
      photo_editor::desktop::EditorPreviewControllerTestAccess::disk_preview_cache_hit_count(
          full_cache_controller);
  const auto full_cache_fast_schedules_before_repeat =
      photo_editor::desktop::EditorPreviewControllerTestAccess::disk_preview_cache_fast_schedule_count(
          full_cache_controller);
  const auto full_cache_direct_schedules_before_repeat =
      photo_editor::desktop::EditorPreviewControllerTestAccess::disk_preview_cache_direct_schedule_count(
          full_cache_controller);
  QThread::msleep(200);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  photo_editor::desktop::EditorPreviewControllerTestAccess::request_render(
      full_cache_controller,
      true,
      QStringLiteral("preview_retry"));
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          full_cache_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          full_cache_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_draft(
          full_cache_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::disk_preview_cache_fast_schedule_count(
          full_cache_controller) != full_cache_fast_schedules_before_repeat ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::disk_preview_cache_direct_schedule_count(
          full_cache_controller) <= full_cache_direct_schedules_before_repeat) {
    return fail("fresh disk-cache max preview should bypass settled and full-preview debounce work");
  }
  if (!wait_for_preview_idle(full_cache_controller)) {
    return fail("repeat full-resolution preview should settle after disk cache lookup");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::disk_preview_cache_hit_count(
          full_cache_controller) <= full_cache_hits_before_repeat) {
    return fail("repeat full-resolution preview should publish from disk cache without recomputing");
  }
  const auto full_cache_direct_schedules_before_immediate_repeat =
      photo_editor::desktop::EditorPreviewControllerTestAccess::disk_preview_cache_direct_schedule_count(
          full_cache_controller);
  photo_editor::desktop::EditorPreviewControllerTestAccess::request_render(
      full_cache_controller,
      true,
      QStringLiteral("preview_retry"));
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          full_cache_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          full_cache_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_draft(
          full_cache_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::disk_preview_cache_direct_schedule_count(
          full_cache_controller) <= full_cache_direct_schedules_before_immediate_repeat) {
    return fail("immediate repeat preview retry should stay on the disk-cache direct path, not draft interactive work");
  }
  if (!wait_for_preview_idle(full_cache_controller)) {
    return fail("immediate repeat full-resolution preview should settle after direct disk cache lookup");
  }

  photo_editor::desktop::EditorPreviewController mask_preview_controller;
  mask_preview_controller.loadAsset(
      QStringLiteral("mask-preview-draft"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(mask_preview_controller)) {
    return fail("mask preview controller should finish its initial preview");
  }
  mask_preview_controller.addMask(QStringLiteral("linear"));
  const auto add_mask_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          mask_preview_controller);
  const auto add_mask_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          mask_preview_controller);
  const auto mask_creation_preview_empty =
      mask_preview_controller.activeMaskPreviewSource().isEmpty();
  if (!mask_creation_preview_empty ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_draft(
          mask_preview_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          mask_preview_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          mask_preview_controller) ||
      add_mask_request.render_mode != QStringLiteral("live_preview") ||
      add_mask_request.render_priority != 0 ||
      add_mask_request.dirty_domain != QStringLiteral("mask_rasterization") ||
      add_mask_request.dirty_stages.isEmpty() ||
      add_mask_request.dirty_stages.first() != QStringLiteral("mask_rasterization") ||
      add_mask_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      !add_mask_request.dirty_stages.contains(QStringLiteral("mask_composition")) ||
      !add_mask_request.dirty_stages.contains(QStringLiteral("local_adjustments")) ||
      !add_mask_request.dirty_stages.contains(QStringLiteral("overlay_composite")) ||
      add_mask_intent.operation != QStringLiteral("mask_stack") ||
      add_mask_intent.dirty_domain != QStringLiteral("mask_rasterization") ||
      add_mask_intent.render_reason != QStringLiteral("mask_preview_change") ||
      add_mask_intent.schedule_full_resolution) {
    const auto failure_detail =
        QStringLiteral(
            "mask creation should stay source-free in normal overlay mode without queueing a full-resolution preview render "
            "(previewEmpty=%1 draft=%2 fullTimer=%3 fullPending=%4 mode=%5 priority=%6 domain=%7 firstStage=%8 hasSourceDecode=%9 hasComposition=%10 hasLocal=%11 hasOverlay=%12 op=%13 intentDomain=%14 reason=%15 fullIntent=%16)")
            .arg(mask_creation_preview_empty)
            .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_draft(
                mask_preview_controller))
            .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
                mask_preview_controller))
            .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
                mask_preview_controller))
            .arg(add_mask_request.render_mode)
            .arg(add_mask_request.render_priority)
            .arg(add_mask_request.dirty_domain)
            .arg(add_mask_request.dirty_stages.isEmpty() ? QStringLiteral("<empty>")
                                                         : add_mask_request.dirty_stages.first())
            .arg(add_mask_request.dirty_stages.contains(QStringLiteral("source_decode")))
            .arg(add_mask_request.dirty_stages.contains(QStringLiteral("mask_composition")))
            .arg(add_mask_request.dirty_stages.contains(QStringLiteral("local_adjustments")))
            .arg(add_mask_request.dirty_stages.contains(QStringLiteral("overlay_composite")))
            .arg(add_mask_intent.operation)
            .arg(add_mask_intent.dirty_domain)
            .arg(add_mask_intent.render_reason)
            .arg(add_mask_intent.schedule_full_resolution)
            .toUtf8();
    return fail(failure_detail.constData());
  }
  if (!wait_for_preview_idle(mask_preview_controller)) {
    return fail("mask preview controller should settle after deferred mask render");
  }
  if (!wait_for_histogram_idle(mask_preview_controller)) {
    return fail("mask preview controller histogram should settle before overlay isolation checks");
  }
  const auto mask_overlay_red_counts = mask_preview_controller.histogramRedCounts();
  const auto mask_overlay_green_counts = mask_preview_controller.histogramGreenCounts();
  const auto mask_overlay_blue_counts = mask_preview_controller.histogramBlueCounts();
  const auto mask_overlay_luminance_counts = mask_preview_controller.histogramLuminanceCounts();
  const auto mask_overlay_waveform = mask_preview_controller.scopeWaveformLuminance();
  const auto mask_overlay_vectorscope = mask_preview_controller.scopeVectorscopePoints();
  const auto mask_overlay_sample_count = mask_preview_controller.histogramSampleCount();
  const auto mask_overlay_preview_source = mask_preview_controller.activeMaskPreviewSource();
  if (!mask_overlay_preview_source.isEmpty()) {
    return fail("normal overlay mode should not generate a raster matte preview source");
  }
  mask_preview_controller.setMaskPreviewMode(QStringLiteral("black-on-white"));
  const auto mask_overlay_inspection_source = mask_preview_controller.activeMaskPreviewSource();
  const auto mask_overlay_preview_image = decode_png_data_url(mask_overlay_inspection_source);
  if (mask_overlay_preview_image.isNull() ||
      !mask_overlay_preview_image.colorSpace().isValid() ||
      mask_overlay_preview_image.colorSpace() != QColorSpace(QColorSpace::SRgb)) {
    return fail("mask inspection preview PNGs should be tagged as sRGB display assets");
  }
  auto visible_overlay_pixels = 0;
  for (int y = 0; y < mask_overlay_preview_image.height(); ++y) {
    for (int x = 0; x < mask_overlay_preview_image.width(); ++x) {
      const auto alpha = mask_overlay_preview_image.pixelColor(x, y).alpha();
      if (alpha > 0) {
        ++visible_overlay_pixels;
      }
    }
  }
  if (visible_overlay_pixels <= 0) {
    return fail("mask inspection preview should contain visible matte pixels");
  }
  if (mask_preview_controller.histogramSampleCount() != mask_overlay_sample_count ||
      mask_preview_controller.histogramRedCounts() != mask_overlay_red_counts ||
      mask_preview_controller.histogramGreenCounts() != mask_overlay_green_counts ||
      mask_preview_controller.histogramBlueCounts() != mask_overlay_blue_counts ||
      mask_preview_controller.histogramLuminanceCounts() != mask_overlay_luminance_counts ||
      mask_preview_controller.scopeWaveformLuminance() != mask_overlay_waveform ||
      mask_preview_controller.scopeVectorscopePoints() != mask_overlay_vectorscope) {
    return fail("mask overlay preview mode changes should not contaminate histogram or scope data");
  }
  mask_preview_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  mask_preview_controller.setActiveMaskExposure(0.33);
  const auto active_mask_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          mask_preview_controller);
  const auto active_mask_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          mask_preview_controller);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_draft(
          mask_preview_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_timer_active(
          mask_preview_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_timer_interval(
          mask_preview_controller) != 0 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          mask_preview_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          mask_preview_controller) ||
      active_mask_request.render_mode != QStringLiteral("live_preview") ||
      active_mask_request.render_priority != 0 ||
      active_mask_request.dirty_domain != QStringLiteral("mask_rasterization") ||
      active_mask_request.dirty_stages.isEmpty() ||
      active_mask_request.dirty_stages.first() != QStringLiteral("mask_rasterization") ||
      active_mask_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      !active_mask_request.dirty_stages.contains(QStringLiteral("mask_composition")) ||
      !active_mask_request.dirty_stages.contains(QStringLiteral("local_adjustments")) ||
      !active_mask_request.dirty_stages.contains(QStringLiteral("overlay_composite")) ||
      active_mask_intent.operation != QStringLiteral("active_mask_exposure") ||
      active_mask_intent.dirty_domain != QStringLiteral("mask_rasterization") ||
      active_mask_intent.render_reason != QStringLiteral("mask_preview_change") ||
      active_mask_intent.schedule_full_resolution) {
    return fail("active mask controls should immediately emit preview-only typed mask-rasterization intents before rendering");
  }

  const auto rapid_mask_setting_intent_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
          mask_preview_controller);
  const auto rapid_mask_setting_generation_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          mask_preview_controller);
  mask_preview_controller.setActiveMaskTemperature(14.0);
  mask_preview_controller.setActiveMaskSelectionExpand(18.0);
  mask_preview_controller.setActiveMaskSelectionSmooth(24.0);
  mask_preview_controller.setActiveMaskSelectionFineEdge(32.0);
  const auto rapid_mask_setting_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(
          mask_preview_controller);
  const auto rapid_mask_setting_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          mask_preview_controller);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
          mask_preview_controller) != rapid_mask_setting_intent_count_before + 4 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          mask_preview_controller) <= rapid_mask_setting_generation_before ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_draft(
          mask_preview_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          mask_preview_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          mask_preview_controller) ||
      rapid_mask_setting_request.render_mode != QStringLiteral("live_preview") ||
      rapid_mask_setting_request.render_priority != 0 ||
      rapid_mask_setting_request.dirty_domain != QStringLiteral("mask_rasterization") ||
      rapid_mask_setting_request.dirty_stages.isEmpty() ||
      rapid_mask_setting_request.dirty_stages.first() != QStringLiteral("mask_rasterization") ||
      rapid_mask_setting_request.dirty_stages.contains(QStringLiteral("source_decode")) ||
      rapid_mask_setting_intent.operation != QStringLiteral("active_mask_selection_fine_edge") ||
      rapid_mask_setting_intent.dirty_domain != QStringLiteral("mask_rasterization") ||
      rapid_mask_setting_intent.render_reason != QStringLiteral("mask_preview_change") ||
      rapid_mask_setting_intent.schedule_full_resolution) {
    return fail("rapid active-mask setting changes should coalesce as preview-only mask rasterization work without scheduling full-resolution renders");
  }
  if (!wait_for_preview_idle(mask_preview_controller)) {
    return fail("mask preview controller should settle after active mask control render");
  }

  photo_editor::desktop::EditorPreviewController lens_profile_controller;
  lens_profile_controller.loadAsset(
      QStringLiteral("lens-profile-auto"),
      QString(),
      QString(),
      QStringLiteral("Canon EOS R5"),
      QString(),
      QStringLiteral("RF 28-70mm F2L USM"),
      QString());
  if (!almost_equal(lens_profile_controller.lensDistortion(), 12.0) ||
      !almost_equal(lens_profile_controller.lensVignette(), 18.0) ||
      !almost_equal(lens_profile_controller.chromaticAberration(), 22.0)) {
    return fail("new assets should automatically match camera and lens correction profiles");
  }

  const auto explicit_neutral_recipe =
      photo_editor::desktop::serialize_edit_recipe_json(photo_editor::desktop::EditRecipeState{});
  lens_profile_controller.loadAsset(
      QStringLiteral("lens-profile-explicit-recipe"),
      QString(),
      QString(),
      QStringLiteral("Canon EOS R5"),
      QString(),
      QStringLiteral("RF 28-70mm F2L USM"),
      explicit_neutral_recipe);
  if (!almost_equal(lens_profile_controller.lensDistortion(), 0.0) ||
      !almost_equal(lens_profile_controller.lensVignette(), 0.0) ||
      !almost_equal(lens_profile_controller.chromaticAberration(), 0.0)) {
    return fail("explicit recipes should not be overwritten by automatic lens profile matching");
  }

  QImage live_preview_source(3000, 1500, QImage::Format_ARGB32);
  live_preview_source.fill(QColor::fromRgb(96, 104, 112));
  const auto live_preview_path = runtime_root.filePath(QStringLiteral("brush-live-preview.png"));
  if (!live_preview_source.save(live_preview_path)) {
    return fail("live brush preview source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController live_preview_controller;
  live_preview_controller.loadAsset(
      QStringLiteral("brush-live-preview"),
      live_preview_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  live_preview_controller.setPreviewTargetEdge(960);
  const auto live_preview_size =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_brush_live_preview_size(
          live_preview_controller);
  if (live_preview_size.width() != 2048 || live_preview_size.height() != 1024) {
    return fail("live brush stroke previews should use a high-resolution overlay target");
  }
  live_preview_controller.startSelectionBrush(false);
  live_preview_controller.beginActiveBrushStroke();
  const auto live_brush_alpha_for_mode =
      [&live_preview_controller](const QString& mode, double x, double y) {
        live_preview_controller.setMaskPreviewMode(mode);
        live_preview_controller.addBrushPoint(x, y);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        return max_alpha_channel(
            photo_editor::desktop::EditorPreviewControllerTestAccess::active_brush_live_preview_image(
                live_preview_controller));
      };
  if (live_brush_alpha_for_mode(QStringLiteral("overlay"), 0.20, 0.30) <= 0 ||
      live_preview_controller.maskPreviewDisplayMode() != QStringLiteral("overlay")) {
    return fail("live brush previews should remain visible in public overlay mode");
  }
  if (live_brush_alpha_for_mode(QStringLiteral("outline"), 0.30, 0.40) <= 0 ||
      live_preview_controller.maskPreviewDisplayMode() != QStringLiteral("outline")) {
    return fail("live brush previews should remain visible in public outline mode");
  }
  if (live_brush_alpha_for_mode(QStringLiteral("grayscale-alpha"), 0.40, 0.50) <= 0 ||
      live_preview_controller.maskPreviewMode() != QStringLiteral("grayscale") ||
      live_preview_controller.maskPreviewDisplayMode() != QStringLiteral("grayscale-alpha")) {
    return fail("live brush previews should honor public grayscale-alpha mode through its internal grayscale alias");
  }
  if (live_brush_alpha_for_mode(QStringLiteral("isolated-mask"), 0.50, 0.60) <= 0 ||
      live_preview_controller.maskPreviewMode() != QStringLiteral("on-black") ||
      live_preview_controller.maskPreviewDisplayMode() != QStringLiteral("isolated-mask")) {
    return fail("live brush previews should honor public isolated-mask mode through its internal on-black alias");
  }
  if (live_brush_alpha_for_mode(QStringLiteral("hidden"), 0.60, 0.70) != 0 ||
      live_preview_controller.maskPreviewMode() != QStringLiteral("none") ||
      live_preview_controller.maskPreviewDisplayMode() != QStringLiteral("hidden")) {
    return fail("live brush previews should be fully transparent when the public hidden mode maps to the internal none alias");
  }
  (void)finish_brush_stroke(live_preview_controller);

  controller.loadAsset(
      QStringLiteral("asset-a"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());

  if (controller.canUndo() || controller.canRedo()) {
    return fail("freshly loaded assets should start with empty undo history");
  }

  photo_editor::desktop::EditorPreviewController liquify_controller;
  liquify_controller.loadAsset(
      QStringLiteral("liquify-controls"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  liquify_controller.setLiquifyCenterX(-0.5);
  liquify_controller.setLiquifyCenterY(1.5);
  liquify_controller.setLiquifyRadius(0.001);
  liquify_controller.setLiquifyStrength(140.0);
  if (!almost_equal(liquify_controller.liquifyCenterX(), 0.0) ||
      !almost_equal(liquify_controller.liquifyCenterY(), 1.0) ||
      !almost_equal(liquify_controller.liquifyRadius(), 0.02) ||
      !almost_equal(liquify_controller.liquifyStrength(), 100.0)) {
    return fail("liquify controls should clamp to supported center, radius, and strength ranges");
  }
  const auto liquify_render_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(liquify_controller);
  if (!almost_equal(liquify_render_state.liquify_center_x, 0.0) ||
      !almost_equal(liquify_render_state.liquify_center_y, 1.0) ||
      !almost_equal(liquify_render_state.liquify_radius, 0.02) ||
      !almost_equal(liquify_render_state.liquify_strength, 100.0)) {
    return fail("controller render requests should carry liquify geometry into the pipeline state");
  }

  QImage compare_source_image(24, 16, QImage::Format_ARGB32);
  compare_source_image.fill(QColor::fromRgb(24, 48, 96));
  const auto compare_source_path = runtime_root.filePath(QStringLiteral("compare-source.png"));
  if (!compare_source_image.save(compare_source_path)) {
    return fail("compare source image should be writable");
  }
  QImage reference_image(18, 12, QImage::Format_ARGB32);
  reference_image.fill(QColor::fromRgb(180, 140, 80));
  const auto reference_image_path = runtime_root.filePath(QStringLiteral("reference-image.png"));
  if (!reference_image.save(reference_image_path)) {
    return fail("reference image should be writable");
  }
  photo_editor::desktop::EditorPreviewController reference_controller;
  reference_controller.loadAsset(
      QStringLiteral("reference-compare"),
      compare_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!reference_controller.setReferenceImage(QUrl::fromLocalFile(reference_image_path).toString()) ||
      !reference_controller.hasReferenceImage() ||
      reference_controller.referenceImageName() != QStringLiteral("reference-image.png")) {
    return fail("external reference images should be accepted from local file URLs");
  }
  reference_controller.setCompareMode(true);
  if (!wait_for_preview_idle(reference_controller) ||
      reference_controller.referencePreviewSource().isEmpty() ||
      reference_controller.displayReferencePreviewSource().isEmpty()) {
    return fail("compare mode should render the selected external reference image");
  }
  reference_controller.updatePixelInspector(0.5, 0.5);
  const auto pixel_inspector = reference_controller.pixelInspector();
  const auto pixel_after = pixel_inspector.value(QStringLiteral("after")).toMap();
  const auto pixel_before = pixel_inspector.value(QStringLiteral("before")).toMap();
  if (!pixel_inspector.value(QStringLiteral("active")).toBool() ||
      !pixel_inspector.value(QStringLiteral("hasAfter")).toBool() ||
      !pixel_inspector.value(QStringLiteral("hasBefore")).toBool() ||
      pixel_after.value(QStringLiteral("red")).toInt() != 24 ||
      pixel_after.value(QStringLiteral("green")).toInt() != 48 ||
      pixel_after.value(QStringLiteral("blue")).toInt() != 96 ||
      pixel_before.value(QStringLiteral("red")).toInt() != 180 ||
      pixel_before.value(QStringLiteral("green")).toInt() != 140 ||
      pixel_before.value(QStringLiteral("blue")).toInt() != 80 ||
      pixel_inspector.value(QStringLiteral("deltaRed")).toInt() != -156) {
    return fail("pixel inspector should report before and after preview RGB values at the sampled point");
  }
  reference_controller.clearPixelInspector();
  if (!reference_controller.pixelInspector().isEmpty()) {
    return fail("clearing the pixel inspector should remove transient sample state");
  }
  reference_controller.clearReferenceImage();
  if (reference_controller.hasReferenceImage() || !reference_controller.referenceImagePath().isEmpty()) {
    return fail("clearing the reference image should restore before/after compare mode");
  }

  constexpr int kPixelMapWidth = 960;
  constexpr int kPixelMapHeight = 240;
  const auto pixel_map_color = [](int x, int y, int offset) {
    return QColor::fromRgb(
        (x + offset) % 256,
        (y * 3 + offset) % 256,
        (x * 2 + y + offset) % 256);
  };
  QImage pixel_map_source(kPixelMapWidth, kPixelMapHeight, QImage::Format_ARGB32);
  QImage pixel_map_reference(kPixelMapWidth, kPixelMapHeight, QImage::Format_ARGB32);
  for (int y = 0; y < kPixelMapHeight; ++y) {
    for (int x = 0; x < kPixelMapWidth; ++x) {
      pixel_map_source.setPixelColor(x, y, pixel_map_color(x, y, 0));
      pixel_map_reference.setPixelColor(x, y, pixel_map_color(x, y, 37));
    }
  }
  const auto pixel_map_source_path = runtime_root.filePath(QStringLiteral("pixel-map-source.png"));
  const auto pixel_map_reference_path = runtime_root.filePath(QStringLiteral("pixel-map-reference.png"));
  if (!pixel_map_source.save(pixel_map_source_path) ||
      !pixel_map_reference.save(pixel_map_reference_path)) {
    return fail("pixel inspector mapping source images should be writable");
  }
  photo_editor::desktop::EditorPreviewController pixel_map_controller;
  pixel_map_controller.loadAsset(
      QStringLiteral("pixel-map"),
      pixel_map_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!pixel_map_controller.setReferenceImage(QUrl::fromLocalFile(pixel_map_reference_path).toString())) {
    return fail("pixel inspector mapping reference image should load");
  }
  pixel_map_controller.setCompareMode(true);
  if (!wait_for_preview_idle(pixel_map_controller)) {
    return fail("pixel inspector mapping previews should render");
  }
  constexpr double kPixelSampleX = 0.3375;
  constexpr double kPixelSampleY = 0.625;
  const auto expected_pixel_x = static_cast<int>(
      std::lround(kPixelSampleX * static_cast<double>(kPixelMapWidth - 1)));
  const auto expected_pixel_y = static_cast<int>(
      std::lround(kPixelSampleY * static_cast<double>(kPixelMapHeight - 1)));
  pixel_map_controller.updatePixelInspector(kPixelSampleX, kPixelSampleY);
  const auto mapped_pixel_inspector = pixel_map_controller.pixelInspector();
  const auto mapped_after = mapped_pixel_inspector.value(QStringLiteral("after")).toMap();
  const auto mapped_before = mapped_pixel_inspector.value(QStringLiteral("before")).toMap();
  const auto expected_after_color = pixel_map_color(expected_pixel_x, expected_pixel_y, 0);
  const auto expected_before_color = pixel_map_color(expected_pixel_x, expected_pixel_y, 37);
  if (mapped_pixel_inspector.value(QStringLiteral("pixelX")).toInt() != expected_pixel_x ||
      mapped_pixel_inspector.value(QStringLiteral("pixelY")).toInt() != expected_pixel_y ||
      mapped_after.value(QStringLiteral("pixelX")).toInt() != expected_pixel_x ||
      mapped_after.value(QStringLiteral("pixelY")).toInt() != expected_pixel_y ||
      mapped_before.value(QStringLiteral("pixelX")).toInt() != expected_pixel_x ||
      mapped_before.value(QStringLiteral("pixelY")).toInt() != expected_pixel_y ||
      mapped_after.value(QStringLiteral("red")).toInt() != expected_after_color.red() ||
      mapped_after.value(QStringLiteral("green")).toInt() != expected_after_color.green() ||
      mapped_after.value(QStringLiteral("blue")).toInt() != expected_after_color.blue() ||
      mapped_before.value(QStringLiteral("red")).toInt() != expected_before_color.red() ||
      mapped_before.value(QStringLiteral("green")).toInt() != expected_before_color.green() ||
      mapped_before.value(QStringLiteral("blue")).toInt() != expected_before_color.blue()) {
    return fail("pixel inspector normalized coordinates should map to the same before/after preview pixel");
  }

  photo_editor::desktop::EditorPreviewController adjustment_layer_controller;
  adjustment_layer_controller.loadAsset(
      QStringLiteral("adjustment-layer-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  adjustment_layer_controller.setExposure(0.5);
  adjustment_layer_controller.setContrast(18.0);
  adjustment_layer_controller.setTemperature(65.0);
  adjustment_layer_controller.setVibrance(22.0);
  if (!adjustment_layer_controller.addAdjustmentLayer(QStringLiteral("Grade Pass")) ||
      adjustment_layer_controller.photoLayerCount() != 1) {
    return fail("current tone and color edits should be movable into a photo adjustment layer");
  }
  if (adjustment_layer_controller.undoLabel() !=
      QStringLiteral("Create adjustment layer from Base Image edits")) {
    return fail("creating an adjustment layer from global edits should expose a clear undo label");
  }
  const auto adjustment_layers = adjustment_layer_controller.photoLayers();
  const auto adjustment_layer = adjustment_layers.at(0).toMap();
  if (adjustment_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("adjustment") ||
      adjustment_layer.value(QStringLiteral("name")).toString() != QStringLiteral("Grade Pass") ||
      !almost_equal(adjustment_layer.value(QStringLiteral("exposure")).toDouble(), 0.5) ||
      !almost_equal(adjustment_layer.value(QStringLiteral("contrast")).toDouble(), 18.0) ||
      !almost_equal(adjustment_layer.value(QStringLiteral("temperature")).toDouble(), 65.0) ||
      !almost_equal(adjustment_layer.value(QStringLiteral("vibrance")).toDouble(), 22.0) ||
      !adjustment_layer.value(QStringLiteral("enabled")).toBool() ||
      !almost_equal(adjustment_layer.value(QStringLiteral("opacity")).toDouble(), 1.0)) {
    return fail("adjustment layer should preserve captured edit values and normalized layer metadata");
  }
  if (!almost_equal(adjustment_layer_controller.exposure(), 0.0) ||
      !almost_equal(adjustment_layer_controller.contrast(), 0.0) ||
      !almost_equal(adjustment_layer_controller.temperature(), 0.0) ||
      !almost_equal(adjustment_layer_controller.vibrance(), 0.0)) {
    return fail("captured base adjustments should reset after creating an adjustment layer");
  }
  const auto adjustment_layer_render_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(
          adjustment_layer_controller);
  if (adjustment_layer_render_state.layers.size() != 1 ||
      adjustment_layer_render_state.layers.at(0).toMap().value(QStringLiteral("kind")).toString() !=
          QStringLiteral("adjustment")) {
    return fail("controller render requests should carry photo adjustment layers into the pipeline state");
  }
  adjustment_layer_controller.undo();
  if (adjustment_layer_controller.photoLayerCount() != 0 ||
      !almost_equal(adjustment_layer_controller.exposure(), 0.5)) {
    return fail("undo should restore edits that were moved into an adjustment layer");
  }
  adjustment_layer_controller.redo();
  if (adjustment_layer_controller.photoLayerCount() != 1 ||
      !almost_equal(adjustment_layer_controller.exposure(), 0.0)) {
    return fail("redo should recreate the adjustment layer and keep captured edits off the base layer");
  }
  if (!adjustment_layer_controller.bakePhotoLayerAdjustmentsToGlobal(0) ||
      !almost_equal(adjustment_layer_controller.exposure(), 0.5) ||
      !almost_equal(adjustment_layer_controller.contrast(), 18.0) ||
      !almost_equal(adjustment_layer_controller.temperature(), 65.0) ||
      adjustment_layer_controller.undoLabel() !=
          QStringLiteral("Bake adjustment layer edits to Base Image")) {
    return fail("adjustment layer edits should bake back to Base Image with a clear undo label");
  }
  adjustment_layer_controller.undo();
  if (!almost_equal(adjustment_layer_controller.exposure(), 0.0) ||
      !adjustment_layer_controller.canRedo()) {
    return fail("undo should restore Base Image edits before the layer bake command");
  }
  adjustment_layer_controller.redo();
  if (!almost_equal(adjustment_layer_controller.exposure(), 0.5) ||
      !almost_equal(adjustment_layer_controller.temperature(), 65.0)) {
    return fail("redo should reapply baked layer edits to the Base Image");
  }

  photo_editor::desktop::EditorPreviewController fill_layer_controller;
  fill_layer_controller.loadAsset(
      QStringLiteral("fill-layer-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!fill_layer_controller.addFillLayer(QStringLiteral("#336699"), QStringLiteral("Slate Fill")) ||
      fill_layer_controller.photoLayerCount() != 1) {
    return fail("fill layers should be creatable through the editor preview controller");
  }
  const auto fill_layer = fill_layer_controller.photoLayers().at(0).toMap();
  if (fill_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("fill") ||
      fill_layer.value(QStringLiteral("name")).toString() != QStringLiteral("Slate Fill") ||
      fill_layer.value(QStringLiteral("fillColor")).toString() != QStringLiteral("#336699") ||
      !almost_equal(fill_layer.value(QStringLiteral("fillAlpha")).toDouble(), 1.0)) {
    return fail("fill layer controller state should store normalized solid color metadata");
  }
  const auto fill_layer_render_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(
          fill_layer_controller);
  if (fill_layer_render_state.layers.size() != 1 ||
      fill_layer_render_state.layers.at(0).toMap().value(QStringLiteral("kind")).toString() !=
          QStringLiteral("fill")) {
    return fail("controller render requests should carry fill layers into the pipeline state");
  }
  fill_layer_controller.undo();
  if (fill_layer_controller.photoLayerCount() != 0) {
    return fail("undo should remove a newly added fill layer");
  }

  photo_editor::desktop::EditorPreviewController text_layer_controller;
  text_layer_controller.loadAsset(
      QStringLiteral("text-layer-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!text_layer_controller.addTextLayer(QStringLiteral("Caption"), QStringLiteral("Caption Layer")) ||
      text_layer_controller.photoLayerCount() != 1) {
    return fail("text layers should be creatable through the editor preview controller");
  }
  const auto text_layer = text_layer_controller.photoLayers().at(0).toMap();
  if (text_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("text") ||
      text_layer.value(QStringLiteral("name")).toString() != QStringLiteral("Caption Layer") ||
      text_layer.value(QStringLiteral("text")).toString() != QStringLiteral("Caption") ||
      text_layer.value(QStringLiteral("textColor")).toString() != QStringLiteral("#ffffff") ||
      text_layer.value(QStringLiteral("fontFamily")).toString() != QStringLiteral("Helvetica") ||
      text_layer.value(QStringLiteral("fontWeight")).toString() != QStringLiteral("regular") ||
      text_layer.value(QStringLiteral("textAlignment")).toString() != QStringLiteral("left") ||
      text_layer.value(QStringLiteral("pixelWidth")).toInt() <= 0 ||
      text_layer.value(QStringLiteral("pixelHeight")).toInt() <= 0) {
    return fail("text layer controller state should store normalized raster text metadata");
  }
  if (!text_layer_controller.setPhotoLayerText(0, QStringLiteral("Updated Caption")) ||
      !text_layer_controller.setPhotoLayerTextStyle(
          0,
          QStringLiteral("Avenir"),
          64,
          QStringLiteral("#33CCAA"),
          QStringLiteral("bold"),
          QStringLiteral("center")) ||
      !text_layer_controller.setPhotoLayerTextBox(0, 640, 192)) {
    return fail("text layer controller should edit content, style, alignment, and text box");
  }
  const auto edited_text_layer = text_layer_controller.photoLayers().at(0).toMap();
  if (edited_text_layer.value(QStringLiteral("text")).toString() != QStringLiteral("Updated Caption") ||
      edited_text_layer.value(QStringLiteral("fontFamily")).toString() != QStringLiteral("Avenir") ||
      edited_text_layer.value(QStringLiteral("fontSize")).toInt() != 64 ||
      edited_text_layer.value(QStringLiteral("textColor")).toString() != QStringLiteral("#33ccaa") ||
      edited_text_layer.value(QStringLiteral("fontWeight")).toString() != QStringLiteral("bold") ||
      edited_text_layer.value(QStringLiteral("textAlignment")).toString() != QStringLiteral("center") ||
      edited_text_layer.value(QStringLiteral("pixelWidth")).toInt() != 640 ||
      edited_text_layer.value(QStringLiteral("pixelHeight")).toInt() != 192) {
    return fail("text layer controller should retain normalized edited text metadata");
  }
  const auto text_layer_render_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(
          text_layer_controller);
  if (text_layer_render_state.layers.size() != 1 ||
      text_layer_render_state.layers.at(0).toMap().value(QStringLiteral("kind")).toString() !=
          QStringLiteral("text") ||
      text_layer_render_state.layers.at(0).toMap().value(QStringLiteral("textAlignment")).toString() !=
          QStringLiteral("center") ||
      text_layer_render_state.layers.at(0).toMap().value(QStringLiteral("fontWeight")).toString() !=
          QStringLiteral("bold")) {
    return fail("controller render requests should carry edited text layers into the pipeline state");
  }
  if (!text_layer_controller.setPhotoLayerLocked(0, true) ||
      text_layer_controller.setPhotoLayerText(0, QStringLiteral("Locked Edit")) ||
      !text_layer_controller.setPhotoLayerLocked(0, false)) {
    return fail("text layer edits should respect layer locking");
  }
  for (int undo_index = 0; undo_index < 6; ++undo_index) {
    text_layer_controller.undo();
  }
  if (text_layer_controller.photoLayerCount() != 0) {
    return fail("undo should remove a newly added and edited text layer");
  }
  for (int redo_index = 0; redo_index < 6; ++redo_index) {
    text_layer_controller.redo();
  }
  if (text_layer_controller.photoLayerCount() != 1 ||
      text_layer_controller.photoLayers().at(0).toMap().value(QStringLiteral("text")).toString() !=
          QStringLiteral("Updated Caption") ||
      text_layer_controller.photoLayers().at(0).toMap().value(QStringLiteral("textAlignment")).toString() !=
          QStringLiteral("center")) {
    return fail("redo should restore a newly added and edited text layer");
  }

  QImage source_layer_image(3, 2, QImage::Format_ARGB32);
  source_layer_image.fill(QColor::fromRgb(32, 128, 220));
  const auto source_layer_path = runtime_root.filePath(QStringLiteral("source-layer.png"));
  if (!source_layer_image.save(source_layer_path)) {
    return fail("source layer fixture image should be writable");
  }
  photo_editor::desktop::EditorPreviewController source_layer_controller;
  source_layer_controller.loadAsset(
      QStringLiteral("source-layer-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!source_layer_controller.addSourceLayer(source_layer_path, QStringLiteral("Linked Source")) ||
      source_layer_controller.photoLayerCount() != 1) {
    return fail("source layers should be creatable through the editor preview controller");
  }
  const auto source_layer = source_layer_controller.photoLayers().at(0).toMap();
  if (source_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("source") ||
      source_layer.value(QStringLiteral("name")).toString() != QStringLiteral("Linked Source") ||
      source_layer.value(QStringLiteral("sourcePath")).toString() != source_layer_path ||
      source_layer.value(QStringLiteral("pixelWidth")).toInt() != 3 ||
      source_layer.value(QStringLiteral("pixelHeight")).toInt() != 2) {
    return fail("source layer controller state should retain linked image metadata and dimensions");
  }
  const auto source_layer_render_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(
          source_layer_controller);
  if (source_layer_render_state.layers.size() != 1 ||
      source_layer_render_state.layers.at(0).toMap().value(QStringLiteral("kind")).toString() !=
          QStringLiteral("source")) {
    return fail("controller render requests should carry source layers into the pipeline state");
  }
  source_layer_controller.undo();
  if (source_layer_controller.photoLayerCount() != 0) {
    return fail("undo should remove a newly added source layer");
  }

  photo_editor::desktop::EditorPreviewController group_layer_controller;
  group_layer_controller.loadAsset(
      QStringLiteral("group-layer-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!group_layer_controller.addLayerGroup(QStringLiteral("Retouch Group")) ||
      group_layer_controller.photoLayerCount() != 1) {
    return fail("layer groups should be creatable through the editor preview controller");
  }
  const auto group_layer = group_layer_controller.photoLayers().at(0).toMap();
  if (group_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("group") ||
      group_layer.value(QStringLiteral("name")).toString() != QStringLiteral("Retouch Group") ||
      group_layer.value(QStringLiteral("groupId")).toString().isEmpty() ||
      !group_layer.value(QStringLiteral("expanded")).toBool()) {
    return fail("layer group controller state should store normalized organizational metadata");
  }
  group_layer_controller.undo();
  if (group_layer_controller.photoLayerCount() != 0) {
    return fail("undo should remove a newly added layer group");
  }

  photo_editor::desktop::EditorPreviewController layer_ops_controller;
  layer_ops_controller.loadAsset(
      QStringLiteral("layer-ops-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  layer_ops_controller.addFillLayer(QStringLiteral("#808080"), QStringLiteral("Fill"));
  layer_ops_controller.addTextLayer(QStringLiteral("Title"), QStringLiteral("Title"));
  if (layer_ops_controller.photoLayerCount() != 2 ||
      !layer_ops_controller.renamePhotoLayer(0, QStringLiteral("Headline"))) {
    return fail("photo layers should support rename operations");
  }
  auto readable_layer = layer_ops_controller.photoLayers().at(0).toMap();
  if (readable_layer.value(QStringLiteral("flatIndex")).toInt() != 0 ||
      readable_layer.value(QStringLiteral("kindLabel")).toString() != QStringLiteral("Text") ||
      readable_layer.value(QStringLiteral("opacityPercent")).toInt() != 100 ||
      !readable_layer.value(QStringLiteral("layerAuditLabel")).toString().contains(QStringLiteral("visible")) ||
      !readable_layer.value(QStringLiteral("layerAuditLabel")).toString().contains(QStringLiteral("normal"))) {
    std::cerr << "Layer row metadata: flatIndex="
              << readable_layer.value(QStringLiteral("flatIndex")).toInt()
              << " kindLabel="
              << readable_layer.value(QStringLiteral("kindLabel")).toString().toStdString()
              << " opacityPercent="
              << readable_layer.value(QStringLiteral("opacityPercent")).toInt()
              << " audit="
              << readable_layer.value(QStringLiteral("layerAuditLabel")).toString().toStdString()
              << "\\n";
    return fail("photo layer stack rows should expose readable kind, opacity, state, and blend metadata");
  }
  if (!layer_ops_controller.setPhotoLayerEnabled(0, false) ||
      layer_ops_controller.photoLayers().at(0).toMap().value(QStringLiteral("enabled")).toBool()) {
    return fail("photo layers should support hide/show operations");
  }
  if (!layer_ops_controller.setPhotoLayerEnabled(0, true) ||
      !layer_ops_controller.setPhotoLayerOpacity(0, 0.42) ||
      !layer_ops_controller.setPhotoLayerBlendMode(0, QStringLiteral("multiply")) ||
      !layer_ops_controller.setPhotoLayerClippingMask(0, true) ||
      !layer_ops_controller.setPhotoLayerMaskEnabled(0, true)) {
    return fail("photo layers should support direct opacity, blend, clipping, and layer mask controls");
  }
  if (layer_ops_controller.undoLabel() != QStringLiteral("Layer stack change")) {
    return fail("generic layer stack edits should expose a readable fallback undo label");
  }
  if (!layer_ops_controller.setPhotoLayerMaskOpacity(0, 0.64) ||
      !layer_ops_controller.setPhotoLayerMaskDensity(0, 0.72) ||
      layer_ops_controller.setPhotoLayerMaskOpacity(1, 0.5)) {
    return fail("photo layer masks should expose independent opacity and density controls");
  }
  if (layer_ops_controller.undoLabel() != QStringLiteral("Layer mask density change")) {
    return fail("layer mask density edits should expose a clear undo label");
  }
  photo_editor::desktop::EditorPreviewController layer_undo_label_controller;
  layer_undo_label_controller.loadAsset(
      QStringLiteral("layer-undo-label-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!layer_undo_label_controller.addFillLayer(QStringLiteral("#808080"), QStringLiteral("Fill")) ||
      !layer_undo_label_controller.setPhotoLayerOpacity(0, 0.42) ||
      layer_undo_label_controller.undoLabel() != QStringLiteral("Layer opacity change")) {
    return fail("layer opacity edits should expose a specific undo label");
  }
  layer_undo_label_controller.undo();
  if (layer_undo_label_controller.redoLabel() != QStringLiteral("Layer opacity change")) {
    return fail("undoing layer opacity should move the same action label to redo");
  }
  layer_undo_label_controller.redo();
  if (layer_undo_label_controller.undoLabel() != QStringLiteral("Layer opacity change")) {
    return fail("redoing layer opacity should restore the same undo action label");
  }
  photo_editor::desktop::EditorPreviewController layer_adjustment_controller;
  layer_adjustment_controller.loadAsset(
      QStringLiteral("layer-adjustment-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  layer_adjustment_controller.addMask(QStringLiteral("linear"));
  const auto layer_adjustment_intent_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
          layer_adjustment_controller);
  const auto layer_adjustment_debounce_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_adjustment_render_debounce_count(layer_adjustment_controller);
  const auto layer_adjustment_flush_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_adjustment_render_flush_count(layer_adjustment_controller);
  if (layer_adjustment_controller.photoLayerCount() != 1 ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("exposure"),
          0.72) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("tint"), -18.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("brightness"), 21.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("filmicShoulder"), -17.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("filmicToe"), 14.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("adaptiveContrast"), 19.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("levelsBlackPoint"), 8.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("levelsGamma"), 11.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("levelsWhitePoint"), 6.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("zoneDeepShadows"), -9.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("zoneShadows"), -7.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("zoneMidtones"), 13.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("zoneLights"), 15.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("zoneHighlights"), -12.0)) {
    return fail("selection adjustment layers should accept layer-local tone and color edits");
  }
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_adjustment_render_timer_active(layer_adjustment_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_timer_interval(layer_adjustment_controller) !=
          0 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_debounce_count(layer_adjustment_controller) <=
          layer_adjustment_debounce_count_before ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_flush_count(layer_adjustment_controller) !=
          layer_adjustment_flush_count_before ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
          layer_adjustment_controller) != layer_adjustment_intent_count_before ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          layer_adjustment_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          layer_adjustment_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_pending_write_count(layer_adjustment_controller) !=
          14 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_budget_ms(layer_adjustment_controller) !=
          16) {
    return fail("rapid layer-local adjustment writes should update the model while posting preview render submission immediately");
  }
  const auto adjusted_selection_layer = layer_adjustment_controller.photoLayers().at(0).toMap();
  if (!almost_equal(adjusted_selection_layer.value(QStringLiteral("exposure")).toDouble(), 0.72) ||
      !almost_equal(adjusted_selection_layer.value(QStringLiteral("tint")).toDouble(), -18.0) ||
      !almost_equal(adjusted_selection_layer.value(QStringLiteral("brightness")).toDouble(), 21.0) ||
      !almost_equal(adjusted_selection_layer.value(QStringLiteral("filmicShoulder")).toDouble(), -17.0) ||
      !almost_equal(adjusted_selection_layer.value(QStringLiteral("filmicToe")).toDouble(), 14.0) ||
      !almost_equal(adjusted_selection_layer.value(QStringLiteral("adaptiveContrast")).toDouble(), 19.0) ||
      !almost_equal(adjusted_selection_layer.value(QStringLiteral("levelsBlackPoint")).toDouble(), 8.0) ||
      !almost_equal(adjusted_selection_layer.value(QStringLiteral("levelsGamma")).toDouble(), 11.0) ||
      !almost_equal(adjusted_selection_layer.value(QStringLiteral("levelsWhitePoint")).toDouble(), 6.0) ||
      !almost_equal(adjusted_selection_layer.value(QStringLiteral("zoneMidtones")).toDouble(), 13.0) ||
      !almost_equal(layer_adjustment_controller.exposure(), 0.0) ||
      !almost_equal(layer_adjustment_controller.brightness(), 0.0) ||
      !almost_equal(layer_adjustment_controller.levelsGamma(), 0.0) ||
      !almost_equal(layer_adjustment_controller.tint(), 0.0)) {
    return fail("layer-local adjustment edits should not mutate the base/global recipe");
  }
  if (!wait_for_preview_idle(layer_adjustment_controller)) {
    return fail("layer-local adjustment debounce should eventually submit and finish one preview render");
  }
  const auto layer_adjustment_debounced_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(
          layer_adjustment_controller);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_flush_count(layer_adjustment_controller) !=
          layer_adjustment_flush_count_before + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::edit_intent_count(
          layer_adjustment_controller) != layer_adjustment_intent_count_before + 1 ||
      layer_adjustment_debounced_intent.operation != QStringLiteral("photo_layer_adjustment") ||
      layer_adjustment_debounced_intent.dirty_domain != QStringLiteral("layers_composite") ||
      layer_adjustment_debounced_intent.render_reason != QStringLiteral("photo_layer_change") ||
      layer_adjustment_debounced_intent.schedule_full_resolution ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_pending_write_count(layer_adjustment_controller) !=
          0 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_last_coalesced_write_count(layer_adjustment_controller) !=
          14 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_budget_ms(layer_adjustment_controller) !=
          16 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_last_flush_elapsed_ms(layer_adjustment_controller) <
          0 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_within_latency_budget(layer_adjustment_controller) !=
          (photo_editor::desktop::EditorPreviewControllerTestAccess::
               photo_layer_adjustment_render_last_flush_elapsed_ms(layer_adjustment_controller) <=
           photo_editor::desktop::EditorPreviewControllerTestAccess::
               photo_layer_adjustment_render_budget_ms(layer_adjustment_controller))) {
    return fail("debounced layer-local adjustment edits should coalesce to one preview-only layer composite intent");
  }
  if (!layer_adjustment_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("curveHighlights"),
          -42.0) ||
      !layer_adjustment_controller.setPhotoLayerCurvePoints(
          0,
          QStringLiteral("Red"),
          QVariantList{
              QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
              QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 0.2}},
          })) {
    return fail("selection adjustment layers should accept layer-local tone curve edits");
  }
  const auto layer_curve_undo_label = layer_adjustment_controller.undoLabel();
  if (!layer_curve_undo_label.contains(QStringLiteral("Adjustment")) ||
      !layer_curve_undo_label.endsWith(QStringLiteral(" curve edit"))) {
    return fail("layer curve edits should expose the active adjustment layer curve undo label");
  }
  auto curve_selection_layer = layer_adjustment_controller.photoLayers().at(0).toMap();
  const auto red_layer_curve_points =
      curve_selection_layer.value(QStringLiteral("redCurvePoints")).toList();
  if (!almost_equal(curve_selection_layer.value(QStringLiteral("curveHighlights")).toDouble(), -42.0) ||
      red_layer_curve_points.size() != 2 ||
      !almost_equal(curve_point_coordinate(red_layer_curve_points[1], "y"), 0.2) ||
      !almost_equal(layer_adjustment_controller.curveHighlights(), 0.0) ||
      layer_adjustment_controller.activeCurveMode() != QStringLiteral("Neutral")) {
    return fail("layer-local curve edits should persist on the layer without mutating the base/global curve");
  }
  if (!layer_adjustment_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("toneCurve"))) {
    return fail("selection adjustment layers should support layer-local tone curve reset");
  }
  curve_selection_layer = layer_adjustment_controller.photoLayers().at(0).toMap();
  if (curve_selection_layer.contains(QStringLiteral("curveHighlights")) ||
      curve_selection_layer.contains(QStringLiteral("redCurvePoints")) ||
      !almost_equal(curve_selection_layer.value(QStringLiteral("exposure")).toDouble(), 0.72)) {
    return fail("layer-local tone curve reset should clear only layer curve fields");
  }
  if (!layer_adjustment_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("temperature"), 24.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("vibrance"), 33.0) ||
      !layer_adjustment_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("whiteBalance"))) {
    return fail("selection adjustment layers should support layer-local white balance reset");
  }
  auto reset_selection_layer = layer_adjustment_controller.photoLayers().at(0).toMap();
  if (reset_selection_layer.contains(QStringLiteral("temperature")) ||
      reset_selection_layer.contains(QStringLiteral("tint")) ||
      !almost_equal(reset_selection_layer.value(QStringLiteral("exposure")).toDouble(), 0.72) ||
      !almost_equal(reset_selection_layer.value(QStringLiteral("vibrance")).toDouble(), 33.0) ||
      !almost_equal(layer_adjustment_controller.temperature(), 0.0) ||
      !almost_equal(layer_adjustment_controller.tint(), 0.0) ||
      !almost_equal(layer_adjustment_controller.exposure(), 0.0)) {
    return fail("layer-local white balance reset should return WB/tint to neutral without mutating base or other layer values");
  }
  layer_adjustment_controller.undo();
  reset_selection_layer = layer_adjustment_controller.photoLayers().at(0).toMap();
  if (!almost_equal(reset_selection_layer.value(QStringLiteral("temperature")).toDouble(), 24.0) ||
      !almost_equal(reset_selection_layer.value(QStringLiteral("tint")).toDouble(), -18.0)) {
    return fail("layer-local reset should be undoable as one layer stack edit");
  }
  if (!layer_adjustment_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("light"))) {
    return fail("selection adjustment layers should support layer-local light reset");
  }
  reset_selection_layer = layer_adjustment_controller.photoLayers().at(0).toMap();
  if (reset_selection_layer.contains(QStringLiteral("exposure")) ||
      reset_selection_layer.contains(QStringLiteral("contrast")) ||
      reset_selection_layer.contains(QStringLiteral("brightness")) ||
      reset_selection_layer.contains(QStringLiteral("filmicShoulder")) ||
      reset_selection_layer.contains(QStringLiteral("filmicToe")) ||
      reset_selection_layer.contains(QStringLiteral("adaptiveContrast")) ||
      reset_selection_layer.contains(QStringLiteral("levelsBlackPoint")) ||
      reset_selection_layer.contains(QStringLiteral("levelsGamma")) ||
      reset_selection_layer.contains(QStringLiteral("levelsWhitePoint")) ||
      reset_selection_layer.contains(QStringLiteral("zoneDeepShadows")) ||
      reset_selection_layer.contains(QStringLiteral("zoneShadows")) ||
      reset_selection_layer.contains(QStringLiteral("zoneMidtones")) ||
      reset_selection_layer.contains(QStringLiteral("zoneLights")) ||
      reset_selection_layer.contains(QStringLiteral("zoneHighlights")) ||
      !almost_equal(reset_selection_layer.value(QStringLiteral("temperature")).toDouble(), 24.0) ||
      !almost_equal(reset_selection_layer.value(QStringLiteral("vibrance")).toDouble(), 33.0) ||
      !almost_equal(layer_adjustment_controller.exposure(), 0.0)) {
    return fail("layer-local light reset should reset only layer tone values and keep base/global neutral");
  }
  if (!layer_adjustment_controller.setPhotoLayerAdjustmentString(
          0,
          QStringLiteral("colorMixerModel"),
          QStringLiteral("HSV")) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentString(
          0,
          QStringLiteral("hslChannel"),
          QStringLiteral("Blues")) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("hueAdjustment"),
          220.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("selectiveCyan"),
          -42.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("channelMixerRedFromGreen"),
          240.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentBool(
          0,
          QStringLiteral("blackAndWhiteEnabled"),
          true) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("blackAndWhiteReds"),
          58.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentBool(
          0,
          QStringLiteral("colorReplacementEnabled"),
          true) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("colorReplacementSourceHue"),
          1.25) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("colorReplacementRange"),
          -4.0) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentBool(
          0,
          QStringLiteral("skinToneProtectionEnabled"),
          true) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("skinToneLineHue"),
          -0.25) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentBool(
          0,
          QStringLiteral("colorHarmonyEnabled"),
          true) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentString(
          0,
          QStringLiteral("colorHarmonyMode"),
          QStringLiteral("Triadic")) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("gradingHighlightsHue"),
          -0.25) ||
      !layer_adjustment_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("gradingAmount"),
          125.0)) {
    return fail("selection adjustment layers should accept the advanced layer-local color recipe");
  }
  const auto advanced_layer =
      layer_adjustment_controller.photoLayers().at(0).toMap();
  const auto advanced_adjustments =
      advanced_layer.value(QStringLiteral("adjustments")).toMap();
  const auto color_mixer =
      advanced_adjustments.value(QStringLiteral("colorMixer")).toMap();
  const auto color_channels =
      color_mixer.value(QStringLiteral("channels")).toMap();
  const auto blue_channel =
      color_channels.value(QStringLiteral("Blues")).toMap();
  const auto rgb_channel_mixer =
      color_mixer.value(QStringLiteral("rgbChannelMixer")).toMap();
  const auto black_and_white =
      color_mixer.value(QStringLiteral("blackAndWhite")).toMap();
  const auto black_and_white_channels =
      black_and_white.value(QStringLiteral("channels")).toMap();
  const auto color_replacement =
      color_mixer.value(QStringLiteral("colorReplacement")).toMap();
  const auto skin_protection =
      color_mixer.value(QStringLiteral("skinToneProtection")).toMap();
  const auto harmony =
      color_mixer.value(QStringLiteral("colorHarmony")).toMap();
  const auto grading =
      advanced_adjustments.value(QStringLiteral("colorGrading")).toMap();
  const auto grading_highlights =
      grading.value(QStringLiteral("highlights")).toMap();
  if (color_mixer.value(QStringLiteral("model")).toString() != QStringLiteral("HSV") ||
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
      !almost_equal(grading.value(QStringLiteral("amount")).toDouble(), 100.0) ||
      layer_adjustment_controller.colorMixerModel() == QStringLiteral("HSV") ||
      layer_adjustment_controller.hslChannel() == QStringLiteral("Blues") ||
      layer_adjustment_controller.blackAndWhiteEnabled() ||
      layer_adjustment_controller.colorReplacementEnabled() ||
      layer_adjustment_controller.skinToneProtectionEnabled() ||
      layer_adjustment_controller.colorHarmonyEnabled()) {
    return fail(
        "advanced layer-local color edits should persist on the layer with clamps/defaults and not mutate global color state");
  }
  layer_adjustment_controller.addTextLayer(QStringLiteral("Label"), QStringLiteral("Label"));
  if (layer_adjustment_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("exposure"),
          0.25) ||
      layer_adjustment_controller.setPhotoLayerAdjustmentValue(
          1,
          QStringLiteral("unsupported"),
          12.0)) {
    return fail("layer-local adjustment edits should reject non-adjustment layers and unsupported keys");
  }
  if (layer_adjustment_controller.resetPhotoLayerAdjustmentGroup(0, QStringLiteral("whiteBalance")) ||
      layer_adjustment_controller.resetPhotoLayerAdjustmentGroup(1, QStringLiteral("unsupported"))) {
    return fail("layer-local reset should reject non-adjustment layers and unsupported reset groups");
  }
  readable_layer = layer_ops_controller.photoLayers().at(0).toMap();
  const auto layer_audit_label = readable_layer.value(QStringLiteral("layerAuditLabel")).toString();
  if (!almost_equal(readable_layer.value(QStringLiteral("opacity")).toDouble(), 0.42) ||
      readable_layer.value(QStringLiteral("opacityPercent")).toInt() != 42 ||
      !almost_equal(readable_layer.value(QStringLiteral("maskOpacity")).toDouble(), 0.64) ||
      !almost_equal(readable_layer.value(QStringLiteral("maskDensity")).toDouble(), 0.72) ||
      readable_layer.value(QStringLiteral("maskOpacityPercent")).toInt() != 64 ||
      readable_layer.value(QStringLiteral("maskDensityPercent")).toInt() != 72 ||
      readable_layer.value(QStringLiteral("blendMode")).toString() != QStringLiteral("multiply") ||
      !readable_layer.value(QStringLiteral("clippingMask")).toBool() ||
      !readable_layer.value(QStringLiteral("hasLayerMask")).toBool() ||
      readable_layer.value(QStringLiteral("layerMaskLabel")).toString() != QStringLiteral("Layer mask") ||
      readable_layer.value(QStringLiteral("dependencyLabel")).toString() !=
          QStringLiteral("Clips to layer below") ||
      !layer_audit_label.contains(QStringLiteral("42%")) ||
      !layer_audit_label.contains(QStringLiteral("multiply")) ||
      !layer_audit_label.contains(QStringLiteral("clips below")) ||
      !layer_audit_label.contains(QStringLiteral("mask"))) {
    return fail("photo layer rows should expose opacity, blend, clipping, and layer mask metadata");
  }
  const auto layer_stack_render_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(
          layer_ops_controller);
  if (layer_stack_render_state.layers.size() != 2 ||
      layer_stack_render_state.layers.at(0).toMap().value(QStringLiteral("blendMode")).toString() !=
          QStringLiteral("multiply") ||
      !almost_equal(
          layer_stack_render_state.layers.at(0).toMap().value(QStringLiteral("opacity")).toDouble(),
          0.42) ||
      !almost_equal(
          layer_stack_render_state.layers.at(0).toMap().value(QStringLiteral("maskOpacity")).toDouble(),
          0.64) ||
      !almost_equal(
          layer_stack_render_state.layers.at(0).toMap().value(QStringLiteral("maskDensity")).toDouble(),
          0.72)) {
    return fail("draft render requests should carry live layer opacity, mask gate, and blend metadata");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          layer_ops_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          layer_ops_controller)) {
    return fail("photo layer blend and opacity changes should preview at draft quality without scheduling full renders");
  }
  if (!layer_ops_controller.setPhotoLayerEnabled(0, false) ||
      layer_ops_controller.photoLayers().at(0).toMap().value(QStringLiteral("enabled")).toBool()) {
    return fail("photo layers should support hide/show operations after direct layer controls");
  }
  if (!layer_ops_controller.setPhotoLayerSolo(0, true) ||
      !layer_ops_controller.photoLayers().at(0).toMap().value(QStringLiteral("solo")).toBool()) {
    return fail("photo layers should support solo operations");
  }
  readable_layer = layer_ops_controller.photoLayers().at(0).toMap();
  if (!readable_layer.value(QStringLiteral("layerAuditLabel")).toString().contains(QStringLiteral("hidden")) ||
      !readable_layer.value(QStringLiteral("layerAuditLabel")).toString().contains(QStringLiteral("solo"))) {
    return fail("photo layer audit metadata should reflect hidden and solo layer states");
  }
  if (!layer_ops_controller.photoLayerSoloActive() || !layer_ops_controller.clearPhotoLayerSolo() ||
      layer_ops_controller.photoLayerSoloActive()) {
    return fail("photo layer isolation should expose and clear active solo state");
  }
  if (!layer_ops_controller.isolatePhotoLayer(1) ||
      layer_ops_controller.photoLayers().at(0).toMap().value(QStringLiteral("solo")).toBool() ||
      !layer_ops_controller.photoLayers().at(1).toMap().value(QStringLiteral("solo")).toBool()) {
    return fail("photo layer isolate should solo exactly one layer at a time");
  }
  const auto isolated_layer_render_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(
          layer_ops_controller);
  auto isolated_render_state_solo_count = 0;
  for (const auto& layer_value : isolated_layer_render_state.layers) {
    if (layer_value.toMap().value(QStringLiteral("solo")).toBool()) {
      ++isolated_render_state_solo_count;
    }
  }
  if (isolated_render_state_solo_count != 1) {
    return fail("photo layer isolate should mark exactly one layer for render-state isolation");
  }
  photo_editor::desktop::EditState synthetic_solo_render_state;
  synthetic_solo_render_state.layers = QVariantList{
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("solo-red")},
          {QStringLiteral("name"), QStringLiteral("Solo Red")},
          {QStringLiteral("kind"), QStringLiteral("fill")},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("solo"), true},
          {QStringLiteral("fillColor"), QStringLiteral("#ff0000")},
      },
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("skipped-green")},
          {QStringLiteral("name"), QStringLiteral("Skipped Green")},
          {QStringLiteral("kind"), QStringLiteral("fill")},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("solo"), false},
          {QStringLiteral("fillColor"), QStringLiteral("#00ff00")},
      },
  };
  QImage solo_render_input(1, 1, QImage::Format_ARGB32);
  solo_render_input.fill(QColor::fromRgb(0, 0, 0));
  const auto solo_render_output = photo_editor::desktop::render_output_image(
      solo_render_input, synthetic_solo_render_state, QColorSpace::SRgb);
  const auto solo_render_pixel = solo_render_output.pixelColor(0, 0);
  if (!(solo_render_pixel.red() > solo_render_pixel.green() &&
        solo_render_pixel.red() > solo_render_pixel.blue())) {
    return fail("photo layer isolate should constrain rendered pixels to the isolated layer");
  }
  if (!layer_ops_controller.clearPhotoLayerSolo()) {
    return fail("photo layer isolate workflow should return to the full stack");
  }
  if (!layer_ops_controller.savePhotoLayerVisibilityState(QStringLiteral("Hidden Headline")) ||
      layer_ops_controller.photoLayerVisibilityStateCount() != 1) {
    return fail("photo layer stack should save named visibility states");
  }
  const auto visibility_state = layer_ops_controller.photoLayerVisibilityStates().at(0).toMap();
  if (visibility_state.value(QStringLiteral("name")).toString() != QStringLiteral("Hidden Headline") ||
      visibility_state.value(QStringLiteral("visibleCount")).toInt() != 1 ||
      !visibility_state.value(QStringLiteral("visibilitySummary")).toString().contains(QStringLiteral("1/2"))) {
    return fail("photo layer visibility states should summarize saved layer visibility");
  }
  if (!layer_ops_controller.setPhotoLayerEnabled(0, true) ||
      !layer_ops_controller.isolatePhotoLayer(1) ||
      !layer_ops_controller.applyPhotoLayerVisibilityState(0)) {
    return fail("photo layer stack should restore a saved visibility state");
  }
  if (layer_ops_controller.photoLayers().at(0).toMap().value(QStringLiteral("enabled")).toBool() ||
      layer_ops_controller.photoLayerSoloActive()) {
    return fail("saved layer visibility state should restore enabled and solo flags by layer id");
  }
  if (!layer_ops_controller.deletePhotoLayerVisibilityState(0) ||
      layer_ops_controller.photoLayerVisibilityStateCount() != 0) {
    return fail("photo layer visibility states should be removable");
  }
  if (!layer_ops_controller.setPhotoLayerTransform(0, 11, 13, 1.5, 12.0)) {
    return fail("photo layers should support non-destructive transform metadata edits");
  }
  const auto transformed_layer = layer_ops_controller.photoLayers().at(0).toMap();
  if (transformed_layer.value(QStringLiteral("x")).toInt() != 11 ||
      transformed_layer.value(QStringLiteral("y")).toInt() != 13 ||
      !almost_equal(transformed_layer.value(QStringLiteral("scale")).toDouble(), 1.5) ||
      !almost_equal(transformed_layer.value(QStringLiteral("rotation")).toDouble(), 12.0)) {
    return fail("photo layer transforms should be normalized into controller state");
  }
  if (!layer_ops_controller.setPhotoLayerLocked(0, true) ||
      layer_ops_controller.renamePhotoLayer(0, QStringLiteral("Blocked Rename")) ||
      layer_ops_controller.setPhotoLayerOpacity(0, 0.75) ||
      layer_ops_controller.setPhotoLayerBlendMode(0, QStringLiteral("screen")) ||
      layer_ops_controller.setPhotoLayerClippingMask(0, false) ||
      layer_ops_controller.setPhotoLayerMaskEnabled(0, false) ||
      layer_ops_controller.setPhotoLayerTransform(0, 4, 4, 2.0, 45.0) ||
      layer_ops_controller.deletePhotoLayer(0)) {
    return fail("locked photo layers should block destructive stack operations");
  }
  if (!layer_ops_controller.setPhotoLayerLocked(0, false) ||
      !layer_ops_controller.duplicatePhotoLayer(0) ||
      layer_ops_controller.photoLayerCount() != 3) {
    return fail("photo layers should support duplicate operations");
  }
  const auto duplicated_name = layer_ops_controller.photoLayers().at(1).toMap().value(QStringLiteral("name")).toString();
  if (!duplicated_name.endsWith(QStringLiteral("Copy"))) {
    return fail("duplicated photo layers should receive copy metadata");
  }
  photo_editor::desktop::EditorPreviewController duplicate_mask_policy_controller;
  duplicate_mask_policy_controller.loadAsset(
      QStringLiteral("duplicate-mask-policy-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  duplicate_mask_policy_controller.addMask(QStringLiteral("linear"));
  if (duplicate_mask_policy_controller.photoLayerCount() != 1) {
    return fail("mask creation should create one linked layer for duplicate policy tests");
  }
  const auto source_masked_layer =
      duplicate_mask_policy_controller.photoLayers().at(0).toMap();
  if (source_masked_layer.value(QStringLiteral("selectionMaskSetId")).toString().isEmpty()) {
    return fail("duplicate policy source layer should have a mask link");
  }
  if (!duplicate_mask_policy_controller.duplicatePhotoLayerWithMasks(0) ||
      duplicate_mask_policy_controller.photoLayerCount() != 2) {
    return fail("photo layers should duplicate with mask links on request");
  }
  if (duplicate_mask_policy_controller.undoLabel() !=
      QStringLiteral("Duplicate layer with masks")) {
    return fail("duplicate with masks should expose a policy-specific undo label");
  }
  duplicate_mask_policy_controller.undo();
  if (duplicate_mask_policy_controller.photoLayerCount() != 1 ||
      duplicate_mask_policy_controller.redoLabel() !=
          QStringLiteral("Duplicate layer with masks")) {
    return fail("undoing duplicate with masks should restore count and preserve redo label");
  }
  duplicate_mask_policy_controller.redo();
  if (duplicate_mask_policy_controller.photoLayerCount() != 2 ||
      duplicate_mask_policy_controller.undoLabel() !=
          QStringLiteral("Duplicate layer with masks")) {
    return fail("redoing duplicate with masks should restore count and undo label");
  }
  const auto copied_masked_layer =
      duplicate_mask_policy_controller.photoLayers().at(1).toMap();
  if (copied_masked_layer.value(QStringLiteral("selectionMaskSetId")).toString() !=
          source_masked_layer.value(QStringLiteral("selectionMaskSetId")).toString() ||
      copied_masked_layer.value(QStringLiteral("duplicateMaskPolicy")).toString() !=
          QStringLiteral("with-masks")) {
    return fail("duplicate with masks should preserve mask link metadata");
  }
  if (!duplicate_mask_policy_controller.duplicatePhotoLayerWithoutMasks(0) ||
      duplicate_mask_policy_controller.photoLayerCount() != 3) {
    return fail("photo layers should duplicate without mask links on request");
  }
  if (duplicate_mask_policy_controller.undoLabel() !=
      QStringLiteral("Duplicate layer without masks")) {
    return fail("duplicate without masks should expose a policy-specific undo label");
  }
  duplicate_mask_policy_controller.undo();
  if (duplicate_mask_policy_controller.photoLayerCount() != 2 ||
      duplicate_mask_policy_controller.redoLabel() !=
          QStringLiteral("Duplicate layer without masks")) {
    return fail("undoing duplicate without masks should restore count and preserve redo label");
  }
  duplicate_mask_policy_controller.redo();
  if (duplicate_mask_policy_controller.photoLayerCount() != 3 ||
      duplicate_mask_policy_controller.undoLabel() !=
          QStringLiteral("Duplicate layer without masks")) {
    return fail("redoing duplicate without masks should restore count and undo label");
  }
  const auto copied_unmasked_layer =
      duplicate_mask_policy_controller.photoLayers().at(1).toMap();
  if (!copied_unmasked_layer.value(QStringLiteral("selectionMaskSetId")).toString().isEmpty() ||
      copied_unmasked_layer.contains(QStringLiteral("selectionMaskSetIds")) ||
      copied_unmasked_layer.value(QStringLiteral("selectionScoped")).toBool() ||
      copied_unmasked_layer.value(QStringLiteral("hasLayerMask")).toBool() ||
      copied_unmasked_layer.value(QStringLiteral("duplicateMaskPolicy")).toString() !=
          QStringLiteral("without-masks")) {
    return fail("duplicate without masks should remove layer mask link metadata");
  }
  if (!layer_ops_controller.movePhotoLayer(1, 1) ||
      layer_ops_controller.photoLayers().at(2).toMap().value(QStringLiteral("name")).toString() != duplicated_name) {
    return fail("photo layers should support reorder operations");
  }
  if (!layer_ops_controller.deletePhotoLayer(2) || layer_ops_controller.photoLayerCount() != 2) {
    return fail("photo layers should support delete operations");
  }

  photo_editor::desktop::EditorPreviewController layer_mask_history_controller;
  layer_mask_history_controller.loadAsset(
      QStringLiteral("layer-mask-history-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  layer_mask_history_controller.addMask(QStringLiteral("linear"));
  if (layer_mask_history_controller.maskCount() != 1 ||
      layer_mask_history_controller.photoLayerCount() != 1 ||
      !layer_mask_history_controller.canUndo()) {
    return fail("mask creation should create a linked layer and enter undo history");
  }
  layer_mask_history_controller.undo();
  if (layer_mask_history_controller.maskCount() != 0 ||
      layer_mask_history_controller.photoLayerCount() != 0 ||
      !layer_mask_history_controller.canRedo()) {
    return fail("undo should remove a newly created mask and linked adjustment layer");
  }
  layer_mask_history_controller.redo();
  if (layer_mask_history_controller.maskCount() != 1 ||
      layer_mask_history_controller.photoLayerCount() != 1) {
    return fail("redo should restore a newly created mask and linked adjustment layer");
  }
  layer_mask_history_controller.setActiveMaskExposure(0.46);
  if (!almost_equal(layer_mask_history_controller.activeMaskExposure(), 0.46)) {
    return fail("mask edit history proof should edit the active mask");
  }
  layer_mask_history_controller.undo();
  if (!almost_equal(layer_mask_history_controller.activeMaskExposure(), 0.0) ||
      !layer_mask_history_controller.canRedo()) {
    return fail("undo should restore the previous active mask edit");
  }
  layer_mask_history_controller.redo();
  if (!almost_equal(layer_mask_history_controller.activeMaskExposure(), 0.46)) {
    return fail("redo should restore the active mask edit");
  }
  if (!layer_mask_history_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("vibrance"),
          37.0)) {
    return fail("layer-local color history proof should edit the linked adjustment layer");
  }
  layer_mask_history_controller.undo();
  auto history_layer = layer_mask_history_controller.photoLayers().at(0).toMap();
  if (!almost_equal(history_layer.value(QStringLiteral("vibrance")).toDouble(), 0.0) ||
      !layer_mask_history_controller.canRedo()) {
    return fail("undo should restore the previous layer-local color edit");
  }
  layer_mask_history_controller.redo();
  history_layer = layer_mask_history_controller.photoLayers().at(0).toMap();
  if (!almost_equal(history_layer.value(QStringLiteral("vibrance")).toDouble(), 37.0)) {
    return fail("redo should restore the layer-local color edit");
  }
  if (!layer_mask_history_controller.addFillLayer(QStringLiteral("#303840"), QStringLiteral("History Fill")) ||
      layer_mask_history_controller.photoLayerCount() != 2) {
    return fail("layer reorder/delete history proof should prepare a second layer");
  }
  const auto layer_order_ids = [](const photo_editor::desktop::EditorPreviewController& controller) {
    QStringList ids;
    const auto layers = controller.photoLayers();
    for (const auto& layer_value : layers) {
      ids.append(layer_value.toMap().value(QStringLiteral("id")).toString());
    }
    return ids;
  };
  const auto order_before_move = layer_order_ids(layer_mask_history_controller);
  if (!layer_mask_history_controller.movePhotoLayer(0, 1)) {
    return fail("layer reorder history proof should move a layer");
  }
  const auto order_after_move = layer_order_ids(layer_mask_history_controller);
  if (order_after_move == order_before_move) {
    return fail("layer reorder should change layer order before undo");
  }
  layer_mask_history_controller.undo();
  if (layer_order_ids(layer_mask_history_controller) != order_before_move ||
      !layer_mask_history_controller.canRedo()) {
    return fail("undo should restore layer order before a reorder");
  }
  layer_mask_history_controller.redo();
  if (layer_order_ids(layer_mask_history_controller) != order_after_move) {
    return fail("redo should restore layer order after a reorder");
  }
  const auto count_before_delete = layer_mask_history_controller.photoLayerCount();
  const auto order_before_delete = layer_order_ids(layer_mask_history_controller);
  if (!layer_mask_history_controller.deletePhotoLayer(1) ||
      layer_mask_history_controller.photoLayerCount() != count_before_delete - 1) {
    return fail("layer deletion history proof should delete one layer");
  }
  layer_mask_history_controller.undo();
  if (layer_mask_history_controller.photoLayerCount() != count_before_delete ||
      layer_order_ids(layer_mask_history_controller) != order_before_delete ||
      !layer_mask_history_controller.canRedo()) {
    return fail("undo should restore a deleted layer in its previous order");
  }
  layer_mask_history_controller.redo();
  if (layer_mask_history_controller.photoLayerCount() != count_before_delete - 1) {
    return fail("redo should delete the same layer again");
  }

  photo_editor::desktop::EditorPreviewController layer_geometry_controller;
  layer_geometry_controller.loadAsset(
      QStringLiteral("layer-geometry-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  layer_geometry_controller.addTextLayer(QStringLiteral("A"), QStringLiteral("Layer A"));
  layer_geometry_controller.addTextLayer(QStringLiteral("B"), QStringLiteral("Layer B"));
  layer_geometry_controller.addTextLayer(QStringLiteral("C"), QStringLiteral("Layer C"));
  if (!layer_geometry_controller.setPhotoLayerTransform(0, 300, 20, 1.0, 0.0) ||
      !layer_geometry_controller.setPhotoLayerTransform(1, 100, 70, 1.0, 0.0) ||
      !layer_geometry_controller.setPhotoLayerTransform(2, 500, 30, 1.0, 0.0)) {
    return fail("photo layer geometry setup should support transform positioning");
  }
  const auto initial_bounds = layer_geometry_controller.photoLayerBounds(0);
  if (!almost_equal(initial_bounds.value(QStringLiteral("x")).toDouble(), 300.0) ||
      !almost_equal(initial_bounds.value(QStringLiteral("width")).toDouble(), 512.0) ||
      !almost_equal(initial_bounds.value(QStringLiteral("height")).toDouble(), 128.0)) {
    return fail("photo layer bounds should expose transformed layer geometry");
  }
  if (!layer_geometry_controller.snapPhotoLayerToCanvas(0, QStringLiteral("bottom-right"), 1000, 800)) {
    return fail("photo layers should snap to canvas anchors");
  }
  auto snapped_layer = layer_geometry_controller.photoLayers().at(0).toMap();
  if (snapped_layer.value(QStringLiteral("x")).toInt() != 488 ||
      snapped_layer.value(QStringLiteral("y")).toInt() != 672) {
    return fail("canvas snapping should update layer position from layer bounds");
  }
  QVariantList geometry_indices;
  geometry_indices << 0 << 1 << 2;
  if (!layer_geometry_controller.alignPhotoLayers(
          geometry_indices,
          QStringLiteral("vertical-center"),
          1000,
          800)) {
    return fail("photo layers should align against a canvas reference");
  }
  for (int index = 0; index < 3; ++index) {
    if (layer_geometry_controller.photoLayers().at(index).toMap().value(QStringLiteral("y")).toInt() != 336) {
      return fail("vertical-center alignment should place selected layers on the canvas center line");
    }
  }
  layer_geometry_controller.setPhotoLayerTransform(0, 0, 0, 1.0, 0.0);
  layer_geometry_controller.setPhotoLayerTransform(1, 50, 0, 1.0, 0.0);
  layer_geometry_controller.setPhotoLayerTransform(2, 400, 0, 1.0, 0.0);
  if (!layer_geometry_controller.distributePhotoLayers(geometry_indices, QStringLiteral("horizontal")) ||
      layer_geometry_controller.photoLayers().at(1).toMap().value(QStringLiteral("x")).toInt() != 200) {
    return fail("photo layers should distribute selected layer centers along an axis");
  }

  QImage flattened_source(8, 4, QImage::Format_ARGB32);
  flattened_source.fill(QColor::fromRgb(24, 48, 220));
  const auto flattened_source_path = runtime_root.filePath(QStringLiteral("flattened-source.png"));
  if (!flattened_source.save(flattened_source_path)) {
    return fail("flattened export source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController flattened_export_controller;
  flattened_export_controller.loadAsset(
      QStringLiteral("flattened-export-asset"),
      flattened_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!flattened_export_controller.addFillLayer(QStringLiteral("#00ff00"), QStringLiteral("Green Overlay")) ||
      flattened_export_controller.photoLayerCount() != 1) {
    return fail("flattened export setup should create a visible fill layer");
  }
  const auto layer_export_path = runtime_root.filePath(QStringLiteral("fill-layer-export.png"));
  if (!flattened_export_controller.exportPhotoLayer(0, layer_export_path)) {
    return fail("photo layers should export individual layer rasters");
  }
  const QImage layer_export(layer_export_path);
  if (layer_export.isNull() || layer_export.size() != flattened_source.size()) {
    return fail("exported fill layer rasters should use the source canvas dimensions");
  }
  const auto layer_sample = layer_export.pixelColor(4, 2);
  if (!(layer_sample.green() > layer_sample.blue() && layer_sample.green() > layer_sample.red())) {
    return fail("exported fill layer rasters should preserve layer color");
  }
  const auto flattened_export_path = runtime_root.filePath(QStringLiteral("flattened-photo-export.png"));
  if (!flattened_export_controller.exportFlattenedPhoto(flattened_export_path)) {
    return fail("photo layer stacks should export as flattened rendered photos");
  }
  const QImage flattened_export(flattened_export_path);
  if (flattened_export.isNull() || flattened_export.size() != flattened_source.size()) {
    return fail("flattened photo export should preserve rendered image dimensions");
  }
  const auto flattened_sample = flattened_export.pixelColor(4, 2);
  if (!(flattened_sample.green() > flattened_sample.blue() &&
        flattened_sample.green() > flattened_sample.red())) {
    return fail("flattened photo export should composite visible photo layers into the output image");
  }

  photo_editor::desktop::EditorPreviewController layer_order_composite_controller;
  layer_order_composite_controller.loadAsset(
      QStringLiteral("layer-order-composite-asset"),
      flattened_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!layer_order_composite_controller.addFillLayer(
          QStringLiteral("#00ff00"),
          QStringLiteral("Order Green")) ||
      !layer_order_composite_controller.addFillLayer(
          QStringLiteral("#ff0000"),
          QStringLiteral("Order Red")) ||
      layer_order_composite_controller.photoLayerCount() != 2) {
    return fail("layer order composite proof should create two opaque fill layers");
  }
  const auto layer_order_before_composite = layer_order_ids(layer_order_composite_controller);
  const auto export_order_sample = [&](const QString& file_name) {
    const auto path = runtime_root.filePath(file_name);
    if (!layer_order_composite_controller.exportFlattenedPhoto(path)) {
      return QColor();
    }
    const QImage image(path);
    return image.isNull() ? QColor() : image.pixelColor(4, 2);
  };
  const auto order_before_sample =
      export_order_sample(QStringLiteral("layer-order-before.png"));
  if (!(order_before_sample.green() > order_before_sample.red() &&
        order_before_sample.green() > order_before_sample.blue())) {
    return fail("layer order composite proof should render the initial top green layer before reorder");
  }
  if (!layer_order_composite_controller.movePhotoLayer(0, 1)) {
    return fail("layer order composite proof should reorder the top fill layer");
  }
  const auto layer_order_after_composite = layer_order_ids(layer_order_composite_controller);
  const auto order_after_sample =
      export_order_sample(QStringLiteral("layer-order-after.png"));
  if (layer_order_after_composite == layer_order_before_composite ||
      !(order_after_sample.red() > order_after_sample.green() &&
        order_after_sample.red() > order_after_sample.blue())) {
    return fail("layer order changes should alter the flattened composite output");
  }
  layer_order_composite_controller.undo();
  const auto order_undo_sample =
      export_order_sample(QStringLiteral("layer-order-undo.png"));
  if (layer_order_ids(layer_order_composite_controller) != layer_order_before_composite ||
      !(order_undo_sample.green() > order_undo_sample.red() &&
        order_undo_sample.green() > order_undo_sample.blue()) ||
      !layer_order_composite_controller.canRedo()) {
    return fail("undo should restore exact layer order and flattened composite output");
  }
  layer_order_composite_controller.redo();
  const auto order_redo_sample =
      export_order_sample(QStringLiteral("layer-order-redo.png"));
  if (layer_order_ids(layer_order_composite_controller) != layer_order_after_composite ||
      !(order_redo_sample.red() > order_redo_sample.green() &&
        order_redo_sample.red() > order_redo_sample.blue())) {
    return fail("redo should restore reordered layer order and flattened composite output");
  }

  QImage adjustment_blend_source(8, 4, QImage::Format_ARGB32);
  adjustment_blend_source.fill(QColor::fromRgb(64, 64, 64));
  const auto adjustment_blend_source_path =
      runtime_root.filePath(QStringLiteral("adjustment-blend-source.png"));
  if (!adjustment_blend_source.save(adjustment_blend_source_path)) {
    return fail("adjustment blend source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController adjustment_blend_controller;
  adjustment_blend_controller.loadAsset(
      QStringLiteral("adjustment-blend-asset"),
      adjustment_blend_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  adjustment_blend_controller.setExposure(1.0);
  if (!adjustment_blend_controller.addAdjustmentLayer(QStringLiteral("Screen Grade")) ||
      adjustment_blend_controller.photoLayerCount() != 1) {
    return fail("adjustment blend proof should create a captured adjustment layer");
  }
  if (adjustment_blend_controller.photoLayers()
          .at(0)
          .toMap()
          .value(QStringLiteral("blendMode"))
          .toString() != QStringLiteral("normal")) {
    return fail("new adjustment layers should default to normal blend mode");
  }
  const auto export_adjustment_blend_sample = [&](const QString& file_name) {
    const auto path = runtime_root.filePath(file_name);
    if (!adjustment_blend_controller.exportFlattenedPhoto(path)) {
      return QColor();
    }
    const QImage image(path);
    return image.isNull() ? QColor() : image.pixelColor(4, 2);
  };
  const auto normal_adjustment_sample =
      export_adjustment_blend_sample(QStringLiteral("adjustment-blend-normal.png"));
  if (!adjustment_blend_controller.setPhotoLayerBlendMode(0, QStringLiteral("screen"))) {
    return fail("adjustment layers should accept meaningful non-normal blend modes");
  }
  const auto screen_adjustment_sample =
      export_adjustment_blend_sample(QStringLiteral("adjustment-blend-screen.png"));
  if (!(screen_adjustment_sample.red() > normal_adjustment_sample.red() &&
        screen_adjustment_sample.green() > normal_adjustment_sample.green() &&
        screen_adjustment_sample.blue() > normal_adjustment_sample.blue())) {
    return fail("adjustment layer blend modes should alter the flattened composite output");
  }
  if (!adjustment_blend_controller.setPhotoLayerBlendMode(0, QStringLiteral("soft light")) ||
      adjustment_blend_controller.photoLayers()
              .at(0)
              .toMap()
              .value(QStringLiteral("blendMode"))
              .toString() != QStringLiteral("soft-light")) {
    return fail("adjustment layer blend mode normalization should match renderer blend names");
  }
  photo_editor::desktop::EditorPreviewController group_blend_controller;
  group_blend_controller.loadAsset(
      QStringLiteral("group-blend-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!group_blend_controller.addLayerGroup(QStringLiteral("Group")) ||
      group_blend_controller.setPhotoLayerBlendMode(0, QStringLiteral("multiply")) ||
      group_blend_controller.photoLayers()
              .at(0)
              .toMap()
              .value(QStringLiteral("blendMode"))
              .toString() != QStringLiteral("normal")) {
    return fail("unsupported layer kinds should keep non-normal blend modes disabled");
  }

  photo_editor::desktop::EditorPreviewController panel_reset_controller;
  panel_reset_controller.loadAsset(
      QStringLiteral("panel-reset-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  panel_reset_controller.setTemperature(18.0);
  panel_reset_controller.setTint(-9.0);
  panel_reset_controller.setExposure(0.65);
  panel_reset_controller.setHighlights(-22.0);
  panel_reset_controller.setVibrance(31.0);
  panel_reset_controller.setTexture(44.0);
  if (!panel_reset_controller.resetAdjustmentGroup(QStringLiteral("whiteBalance")) ||
      !almost_equal(panel_reset_controller.temperature(), 0.0) ||
      !almost_equal(panel_reset_controller.tint(), 0.0) ||
      !almost_equal(panel_reset_controller.exposure(), 0.65) ||
      !almost_equal(panel_reset_controller.vibrance(), 31.0) ||
      !almost_equal(panel_reset_controller.texture(), 44.0)) {
    return fail("white balance panel reset should reset only temperature and tint");
  }
  panel_reset_controller.undo();
  if (!almost_equal(panel_reset_controller.temperature(), 18.0) ||
      !almost_equal(panel_reset_controller.tint(), -9.0) ||
      !almost_equal(panel_reset_controller.exposure(), 0.65)) {
    return fail("panel reset should be undoable as a single edit state");
  }
  if (!panel_reset_controller.resetAdjustmentGroup(QStringLiteral("light")) ||
      !almost_equal(panel_reset_controller.exposure(), 0.0) ||
      !almost_equal(panel_reset_controller.highlights(), 0.0) ||
      !almost_equal(panel_reset_controller.temperature(), 18.0) ||
      !almost_equal(panel_reset_controller.vibrance(), 31.0) ||
      !almost_equal(panel_reset_controller.texture(), 44.0)) {
    return fail("light panel reset should not reset white balance, color, or presence controls");
  }
  if (!panel_reset_controller.resetAdjustmentGroup(QStringLiteral("color")) ||
      !almost_equal(panel_reset_controller.vibrance(), 0.0) ||
      !almost_equal(panel_reset_controller.saturation(), 0.0) ||
      !almost_equal(panel_reset_controller.temperature(), 18.0) ||
      !almost_equal(panel_reset_controller.texture(), 44.0)) {
    return fail("color panel reset should only reset basic color controls");
  }
  if (panel_reset_controller.resetAdjustmentGroup(QStringLiteral("missing-panel"))) {
    return fail("unknown panel reset groups should be rejected");
  }

  photo_editor::desktop::EditorPreviewController wb_residual_controller;
  wb_residual_controller.loadAsset(
      QStringLiteral("wb-residual-reset-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  const auto neutral_wb_recipe = wb_residual_controller.currentRecipeJson();
  wb_residual_controller.setTemperature(21.0);
  wb_residual_controller.setTint(-13.0);
  if (!wb_residual_controller.resetAdjustmentGroup(QStringLiteral("whiteBalance")) ||
      !almost_equal(wb_residual_controller.temperature(), 0.0) ||
      !almost_equal(wb_residual_controller.tint(), 0.0) ||
      wb_residual_controller.currentRecipeJson() != neutral_wb_recipe) {
    return fail("white balance reset should serialize back to the exact neutral recipe without residual tint");
  }

  photo_editor::desktop::EditorPreviewController global_reset_controller;
  global_reset_controller.loadAsset(
      QStringLiteral("global-reset-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  global_reset_controller.setTemperature(17.0);
  global_reset_controller.setTint(-8.0);
  global_reset_controller.setExposure(0.55);
  global_reset_controller.setContrast(12.0);
  global_reset_controller.setVibrance(29.0);
  global_reset_controller.addMask(QStringLiteral("linear"));
  global_reset_controller.resetAdjustments();
  const auto global_reset_recipe =
      QJsonDocument::fromJson(global_reset_controller.currentRecipeJson().toUtf8()).object();
  const auto global_reset_adjustments =
      global_reset_recipe.value(QStringLiteral("globalAdjustments")).toObject();
  if (!almost_equal(global_reset_controller.temperature(), 0.0) ||
      !almost_equal(global_reset_controller.tint(), 0.0) ||
      !almost_equal(global_reset_controller.exposure(), 0.0) ||
      !almost_equal(global_reset_controller.contrast(), 0.0) ||
      !almost_equal(global_reset_controller.vibrance(), 0.0) ||
      global_reset_controller.maskCount() != 0 ||
      global_reset_controller.photoLayerCount() != 0 ||
      !almost_equal(global_reset_adjustments.value(QStringLiteral("temperature")).toDouble(), 0.0) ||
      !almost_equal(global_reset_adjustments.value(QStringLiteral("tint")).toDouble(), 0.0) ||
      !almost_equal(global_reset_adjustments.value(QStringLiteral("exposure")).toDouble(), 0.0) ||
      !almost_equal(global_reset_adjustments.value(QStringLiteral("contrast")).toDouble(), 0.0) ||
      !almost_equal(global_reset_adjustments.value(QStringLiteral("vibrance")).toDouble(), 0.0) ||
      !global_reset_recipe.value(QStringLiteral("maskSets")).toArray().isEmpty() ||
      !global_reset_recipe.value(QStringLiteral("maskStack")).toArray().isEmpty() ||
      !global_reset_recipe.value(QStringLiteral("photoLayers")).toArray().isEmpty()) {
    return fail("reset all should return affected global adjustments, masks, layers, and serialized recipe fields to neutral state");
  }

  photo_editor::desktop::EditorPreviewController order_a_controller;
  order_a_controller.loadAsset(
      QStringLiteral("adjustment-order-a"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  order_a_controller.setTemperature(12.0);
  order_a_controller.setExposure(0.42);
  order_a_controller.setVibrance(18.0);
  photo_editor::desktop::EditorPreviewController order_b_controller;
  order_b_controller.loadAsset(
      QStringLiteral("adjustment-order-b"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  order_b_controller.setVibrance(18.0);
  order_b_controller.setExposure(0.42);
  order_b_controller.setTemperature(12.0);
  if (order_a_controller.currentRecipeJson() != order_b_controller.currentRecipeJson()) {
    return fail("adjustment recipe serialization should be deterministic regardless of edit order");
  }

  QImage reset_visual_source(96, 64, QImage::Format_ARGB32);
  reset_visual_source.setColorSpace(photo_editor::desktop::preview_color_space());
  for (int y = 0; y < reset_visual_source.height(); ++y) {
    for (int x = 0; x < reset_visual_source.width(); ++x) {
      reset_visual_source.setPixelColor(
          x,
          y,
          QColor(
              24 + (x * 180) / std::max(1, reset_visual_source.width() - 1),
              38 + (y * 160) / std::max(1, reset_visual_source.height() - 1),
              72 + ((x + y) * 82) /
                       std::max(1, reset_visual_source.width() + reset_visual_source.height() - 2),
              255));
    }
  }
  const auto reset_visual_path = runtime_root.filePath(QStringLiteral("reset-visual-source.png"));
  if (!reset_visual_source.save(reset_visual_path)) {
    return fail("reset visual source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController reset_visual_controller;
  photo_editor::desktop::TestImageSink reset_visual_sink;
  reset_visual_controller.setImageProvider(&reset_visual_sink);
  reset_visual_controller.loadAsset(
      QStringLiteral("reset-visual-asset"),
      reset_visual_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(reset_visual_controller, 4000)) {
    return fail("reset visual controller should render the original preview");
  }
  const auto original_reset_preview = reset_visual_sink.image(QStringLiteral("edited"));
  reset_visual_controller.setTemperature(26.0);
  reset_visual_controller.setTint(-18.0);
  reset_visual_controller.setExposure(0.35);
  if (!wait_for_preview_idle(reset_visual_controller, 4000)) {
    return fail("reset visual controller should render the edited preview");
  }
  const auto edited_reset_preview = reset_visual_sink.image(QStringLiteral("edited"));
  reset_visual_controller.resetAdjustments();
  if (!wait_for_preview_idle(reset_visual_controller, 4000)) {
    return fail("reset visual controller should render the reset preview");
  }
  const auto neutral_reset_preview = reset_visual_sink.image(QStringLiteral("edited"));
  if (original_reset_preview.isNull() || edited_reset_preview.isNull() ||
      neutral_reset_preview.isNull() ||
      max_channel_delta(original_reset_preview, edited_reset_preview) <= 1 ||
      max_channel_delta(original_reset_preview, neutral_reset_preview) > 1) {
    return fail("reset preview pixels should return to the original visual state after neutral reset");
  }

  photo_editor::desktop::EditorPreviewController transaction_controller;
  transaction_controller.loadAsset(
      QStringLiteral("transaction-asset"),
      reset_visual_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(transaction_controller, 4000)) {
    return fail("transaction controller should finish its initial preview before testing live drag previews");
  }
  const auto transaction_generation_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(transaction_controller);
  photo_editor::desktop::EditorPreviewControllerTestAccess::begin_edit_transaction(transaction_controller);
  transaction_controller.setExposure(0.5);
  transaction_controller.setContrast(10.0);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(transaction_controller) !=
      transaction_generation_before) {
    return fail("edit transactions should defer final render generation invalidation while controls are changing");
  }
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::
          edit_transaction_interactive_preview_timer_active(transaction_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              edit_transaction_interactive_preview_request_count(transaction_controller) != 2) {
    return fail("edit transactions should queue live interactive previews while coalescing final slider commit work");
  }
  photo_editor::desktop::EditorPreviewControllerTestAccess::end_edit_transaction(transaction_controller);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(transaction_controller) !=
      transaction_generation_before + 1) {
    return fail("edit transactions should collapse multiple control changes into one render request");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::
          edit_transaction_interactive_preview_timer_active(transaction_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              edit_transaction_interactive_preview_request_count(transaction_controller) != 0) {
    return fail("committing an edit transaction should cancel stale draft preview work before the exact final render");
  }
  if (!almost_equal(transaction_controller.exposure(), 0.5) ||
      !almost_equal(transaction_controller.contrast(), 10.0)) {
    return fail("edit transactions should preserve every control value changed inside the transaction");
  }
  if (!transaction_controller.canUndo()) {
    return fail("edit transactions should create one undo step for the whole drag");
  }
  transaction_controller.undo();
  if (!almost_equal(transaction_controller.exposure(), 0.0) ||
      !almost_equal(transaction_controller.contrast(), 0.0)) {
    return fail("one undo should restore the pre-transaction state, not an intermediate slider value");
  }
  if (!transaction_controller.canRedo()) {
    return fail("transaction undo should keep the coalesced edit redoable");
  }
  transaction_controller.redo();
  if (!almost_equal(transaction_controller.exposure(), 0.5) ||
      !almost_equal(transaction_controller.contrast(), 10.0)) {
    return fail("redo should restore every value from the coalesced edit transaction");
  }

  controller.setExposure(0.75);
  controller.setContrast(12.0);

  if (!controller.canUndo()) {
    return fail("changing editor controls should enable undo");
  }

  controller.undo();
  if (!almost_equal(controller.exposure(), 0.75) || !almost_equal(controller.contrast(), 0.0)) {
    return fail("undo should restore the previous edit state");
  }
  if (!controller.canRedo()) {
    return fail("undo should enable redo");
  }

  controller.undo();
  if (!almost_equal(controller.exposure(), 0.0) || !almost_equal(controller.contrast(), 0.0)) {
    return fail("multiple undo steps should walk back through the edit history");
  }

  controller.redo();
  if (!almost_equal(controller.exposure(), 0.75) || !almost_equal(controller.contrast(), 0.0)) {
    return fail("redo should restore the most recently undone state");
  }

  controller.redo();
  if (!almost_equal(controller.exposure(), 0.75) || !almost_equal(controller.contrast(), 12.0)) {
    return fail("redo should restore the latest edit state");
  }
  if (controller.canRedo()) {
    return fail("redo history should be exhausted after replaying every state");
  }

  controller.loadAsset(
      QStringLiteral("asset-b"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  controller.setExposure(-0.5);

  controller.loadAsset(
      QStringLiteral("asset-a"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());

  if (!almost_equal(controller.exposure(), 0.75) || !almost_equal(controller.contrast(), 12.0)) {
    return fail("reloading an asset should restore its current edit state");
  }

  controller.undo();
  if (!almost_equal(controller.exposure(), 0.75) || !almost_equal(controller.contrast(), 0.0)) {
    return fail("each asset should preserve its own undo stack");
  }

  controller.loadAsset(
      QStringLiteral("asset-b"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());

  if (!almost_equal(controller.exposure(), -0.5)) {
    return fail("switching assets should preserve independent edit states");
  }
  if (!controller.canUndo()) {
    return fail("switching back to a previously edited asset should restore its undo history");
  }

  photo_editor::desktop::EditorPreviewController curve_controller;
  curve_controller.loadAsset(
      QStringLiteral("curve-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  curve_controller.setCurveChannel(QStringLiteral("Master"));
  curve_controller.setActiveCurvePoints(QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.12}},
      QVariantMap{{QStringLiteral("x"), 0.45}, {QStringLiteral("y"), 0.58}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 0.91}},
  });

  const auto curve_points = curve_controller.activeCurvePoints();
  if (curve_points.size() != 3) {
    return fail("custom tone curves should preserve endpoint controls");
  }
  if (!almost_equal(curve_point_coordinate(curve_points[0], "x"), 0.0) ||
      !almost_equal(curve_point_coordinate(curve_points[0], "y"), 0.12) ||
      !almost_equal(curve_point_coordinate(curve_points[1], "x"), 0.45) ||
      !almost_equal(curve_point_coordinate(curve_points[1], "y"), 0.58) ||
      !almost_equal(curve_point_coordinate(curve_points[2], "x"), 1.0) ||
      !almost_equal(curve_point_coordinate(curve_points[2], "y"), 0.91)) {
    return fail("tone curve endpoints should remain editable for black and white point adjustment");
  }
  curve_controller.setCurveChannel(QStringLiteral("Luma"));
  curve_controller.setActiveCurvePoints(QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.68}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  });
  const auto luma_curve_points = curve_controller.activeCurvePoints();
  if (curve_controller.curveChannel() != QStringLiteral("Luma") ||
      luma_curve_points.size() != 3 ||
      !almost_equal(curve_point_coordinate(luma_curve_points[1], "y"), 0.68)) {
    return fail("luma curve channel should preserve independent point controls");
  }
  if (!curve_controller.curvePresetNames().contains(QStringLiteral("Filmic S"))) {
    return fail("built-in curve presets should be exposed to the UI");
  }
  curve_controller.setCurveChannel(QStringLiteral("RGB"));
  curve_controller.setCurveShadows(22.0);
  if (curve_controller.activeCurveMode() != QStringLiteral("Parametric")) {
    return fail("RGB curve sliders should expose parametric curve mode");
  }
  if (!curve_controller.applyCurvePreset(QStringLiteral("Matte Lift"))) {
    return fail("built-in curve presets should apply to the active curve channel");
  }
  const auto matte_curve_points = curve_controller.activeCurvePoints();
  if (matte_curve_points.size() != 5 ||
      !almost_equal(curve_point_coordinate(matte_curve_points[0], "y"), 0.08) ||
      !almost_equal(curve_controller.curveShadows(), 0.0) ||
      curve_controller.activeCurveMode() != QStringLiteral("Point")) {
    return fail("curve presets should install point curves and clear RGB parametric controls");
  }
  curve_controller.setActiveCurvePoints(QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.45}, {QStringLiteral("y"), 0.54}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  });
  const auto before_curve_preview = curve_controller.activeCurvePoints();
  const auto filmic_preview_points = curve_controller.curvePresetPreviewPoints(QStringLiteral("Filmic S"));
  if (filmic_preview_points.size() != 5 || curve_controller.activeCurvePoints() != before_curve_preview) {
    return fail("curve preset preview points should be readable without mutating the active curve");
  }
  curve_controller.setCurveChannel(QStringLiteral("Luma"));
  if (!curve_controller.applyCurvePreset(QStringLiteral("Highlight Roll-off"))) {
    return fail("built-in curve presets should apply to luma curves");
  }
  const auto rolloff_curve_points = curve_controller.activeCurvePoints();
  if (rolloff_curve_points.size() != 4 ||
      !almost_equal(curve_point_coordinate(rolloff_curve_points[3], "y"), 0.92)) {
    return fail("curve presets should preserve endpoint edits for non-RGB channels");
  }
  if (!curve_controller.presetNames().contains(QStringLiteral("Clean Portrait")) ||
      !curve_controller.presetNames().contains(QStringLiteral("Landscape Pop")) ||
      !curve_controller.presetNames().contains(QStringLiteral("Night Sky Recovery")) ||
      !curve_controller.presetNames().contains(QStringLiteral("Aerial Clean Color")) ||
      !curve_controller.presetNames().contains(QStringLiteral("RAW Highlight Recovery")) ||
      !curve_controller.presetNames().contains(QStringLiteral("Natural HDR Balance")) ||
      !curve_controller.presetPackNames().contains(QStringLiteral("Portrait Essentials")) ||
      !curve_controller.presetPackNames().contains(QStringLiteral("Landscape Core")) ||
      !curve_controller.presetPackNames().contains(QStringLiteral("Low Light")) ||
      !curve_controller.presetPackNames().contains(QStringLiteral("Drone Core")) ||
      !curve_controller.presetPackNames().contains(QStringLiteral("RAW Recovery")) ||
      !curve_controller.presetPackNames().contains(QStringLiteral("Dynamic Range")) ||
      curve_controller.presetPackName(QStringLiteral("Clean Portrait")) != QStringLiteral("Portrait Essentials")) {
    return fail("built-in preset packs should be exposed to the UI");
  }
  if (curve_controller.presetRecipeJson(QStringLiteral("Landscape Pop")).isEmpty()) {
    return fail("built-in preset packs should expose recipe payloads");
  }
  const auto render_built_in_preset = [&](const QString& name, const QImage& source) {
    const auto preset_json = curve_controller.presetRecipeJson(name);
    const auto preset_state = photo_editor::desktop::parse_edit_recipe_json(preset_json);
    return photo_editor::desktop::render_output_image_with_backend(
        source,
        preset_state,
        photo_editor::desktop::preview_color_space(),
        QImage::Format_ARGB32,
        photo_editor::desktop::RenderBackendPreference::CpuOnly);
  };
  const struct {
    const char* name;
    QImage source;
    std::uint64_t expected_hash;
  } built_in_preset_renders[] = {
      {"Clean Portrait", built_in_preset_portrait_source_image(), 4967665468636011849ULL},
      {"Landscape Pop", built_in_preset_landscape_source_image(), 17877839669455383664ULL},
      {"Clean Black and White", built_in_preset_portrait_source_image(), 16607149064084717141ULL},
  };
  for (const auto& preset_render : built_in_preset_renders) {
    const auto rendered =
        render_built_in_preset(QString::fromLatin1(preset_render.name), preset_render.source);
    if (rendered.isNull()) {
      return fail("built-in preset registry render should produce an image");
    }
    const auto actual_hash = image_hash(rendered);
    if (actual_hash != preset_render.expected_hash) {
      std::cerr << "built-in preset render hash mismatch for " << preset_render.name
                << ": expected " << preset_render.expected_hash
                << ", actual " << actual_hash << '\n';
      return fail("built-in preset registry renders should stay locked to known output hashes");
    }
  }
  if (!curve_controller.presetCategoryNames().contains(QStringLiteral("Portrait")) ||
      !curve_controller.presetCategoryNames().contains(QStringLiteral("Landscape")) ||
      !curve_controller.presetCategoryNames().contains(QStringLiteral("Black and White")) ||
      !curve_controller.presetCategoryNames().contains(QStringLiteral("Night")) ||
      !curve_controller.presetCategoryNames().contains(QStringLiteral("Drone")) ||
      !curve_controller.presetCategoryNames().contains(QStringLiteral("RAW Recovery")) ||
      !curve_controller.presetCategoryNames().contains(QStringLiteral("HDR")) ||
      curve_controller.presetCategoryName(QStringLiteral("Landscape Pop")) != QStringLiteral("Landscape")) {
    return fail("built-in presets should expose categories for browsing");
  }
  const auto raw_recovery_groups = curve_controller.presetAdjustmentGroups(QStringLiteral("RAW Highlight Recovery"));
  if (!raw_recovery_groups.contains(QStringLiteral("raw")) ||
      !raw_recovery_groups.contains(QStringLiteral("tone")) ||
      curve_controller.presetCategoryName(QStringLiteral("Natural HDR Balance")) != QStringLiteral("HDR")) {
    return fail("built-in presets should cover RAW recovery and high-dynamic-range scenes");
  }
  const auto landscape_groups = curve_controller.presetAdjustmentGroups(QStringLiteral("Landscape Pop"));
  if (landscape_groups.size() != 3 ||
      !landscape_groups.contains(QStringLiteral("tone")) ||
      !landscape_groups.contains(QStringLiteral("color")) ||
      !landscape_groups.contains(QStringLiteral("detail"))) {
    return fail("built-in presets should declare their touched adjustment groups");
  }
  if (!curve_controller.searchPresetNames(QStringLiteral("landscape")).contains(QStringLiteral("Landscape Pop")) ||
      !curve_controller.searchPresetNames(QStringLiteral("Landscape Core"))
           .contains(QStringLiteral("Golden Hour Landscape"))) {
    return fail("preset search should match names, packs, and categories");
  }
  if (!curve_controller.presetThumbnailSource(QStringLiteral("Landscape Pop"))
           .startsWith(QStringLiteral("data:image/png;base64,")) ||
      curve_controller.presetThumbnailSource(QStringLiteral("Landscape Pop")) ==
          curve_controller.presetThumbnailSource(QStringLiteral("Clean Black and White"))) {
    return fail("preset thumbnails should be generated as distinct PNG data URLs");
  }
  const auto landscape_thumbnail =
      decode_png_data_url(curve_controller.presetThumbnailSource(QStringLiteral("Landscape Pop")));
  const auto expected_landscape_thumbnail =
      photo_editor::desktop::render_output_image_with_backend(
          preset_thumbnail_fixture_source_image(),
          photo_editor::desktop::parse_edit_recipe_json(
              curve_controller.presetRecipeJson(QStringLiteral("Landscape Pop"))),
          photo_editor::desktop::preview_color_space(),
          QImage::Format_ARGB32,
          photo_editor::desktop::RenderBackendPreference::CpuOnly);
  if (landscape_thumbnail.isNull() ||
      landscape_thumbnail.size() != expected_landscape_thumbnail.size() ||
      max_channel_delta(landscape_thumbnail, expected_landscape_thumbnail) > 0) {
    return fail("preset thumbnails should be rendered from the same color pipeline as applied presets");
  }
  curve_controller.setPresetFavorite(QStringLiteral("Landscape Pop"), true);
  if (!curve_controller.isPresetFavorite(QStringLiteral("Landscape Pop")) ||
      !curve_controller.favoritePresetNames().contains(QStringLiteral("Landscape Pop"))) {
    return fail("preset favorites should track built-in presets");
  }
  if (!curve_controller.applyPreset(QStringLiteral("Landscape Pop"))) {
    return fail("built-in preset packs should apply like user presets");
  }
  if (!almost_equal(curve_controller.texture(), 18.0) ||
      !almost_equal(curve_controller.clarity(), 20.0) ||
      !almost_equal(curve_controller.dehaze(), 14.0) ||
      !almost_equal(curve_controller.vibrance(), 22.0)) {
    return fail("built-in landscape preset should apply its tone and detail recipe");
  }
  if (!curve_controller.canUndo() ||
      !curve_controller.status().contains(QStringLiteral("Applied preset 'Landscape Pop'"))) {
    return fail("preset application should be recorded through status and undo history");
  }
  if (curve_controller.deletePreset(QStringLiteral("Landscape Pop"))) {
    return fail("built-in preset packs should not be deleted as user presets");
  }
  photo_editor::desktop::EditorPreviewController layer_preset_controller;
  layer_preset_controller.loadAsset(
      QStringLiteral("layer-local-preset-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!layer_preset_controller.addAdjustmentLayer(QStringLiteral("Layer Preset Target"))) {
    return fail("layer-local preset proof should create an adjustment layer target");
  }
  layer_preset_controller.setExposure(-0.31);
  layer_preset_controller.setTemperature(17.0);
  layer_preset_controller.setTexture(6.0);
  const auto layer_preset_global_recipe_before =
      QJsonDocument::fromJson(
          photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json_for_state(
              layer_preset_controller,
              photo_editor::desktop::EditorPreviewControllerTestAccess::
                  interactive_render_state(layer_preset_controller))
              .toUtf8())
          .object()
          .value(QStringLiteral("globalAdjustments"))
          .toObject();
  if (layer_preset_global_recipe_before.isEmpty() ||
      !layer_preset_controller.applyPresetToPhotoLayer(
          0,
          QStringLiteral("Landscape Pop"),
          1.0)) {
    return fail("built-in presets should apply directly to a selected adjustment layer");
  }
  const auto layer_preset_global_recipe_after =
      QJsonDocument::fromJson(
          photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json_for_state(
              layer_preset_controller,
              photo_editor::desktop::EditorPreviewControllerTestAccess::
                  interactive_render_state(layer_preset_controller))
              .toUtf8())
          .object()
          .value(QStringLiteral("globalAdjustments"))
          .toObject();
  const auto layer_preset_layer = layer_preset_controller.photoLayers().at(0).toMap();
  if (!almost_equal(layer_preset_layer.value(QStringLiteral("texture")).toDouble(), 18.0) ||
      !almost_equal(layer_preset_layer.value(QStringLiteral("clarity")).toDouble(), 20.0) ||
      !almost_equal(layer_preset_layer.value(QStringLiteral("dehaze")).toDouble(), 14.0) ||
      !almost_equal(layer_preset_layer.value(QStringLiteral("vibrance")).toDouble(), 22.0) ||
      !almost_equal(layer_preset_controller.exposure(), -0.31) ||
      !almost_equal(layer_preset_controller.temperature(), 17.0) ||
      !almost_equal(layer_preset_controller.texture(), 6.0) ||
      layer_preset_global_recipe_after != layer_preset_global_recipe_before ||
      !layer_preset_controller.status().contains(QStringLiteral("Applied preset 'Landscape Pop' to layer"))) {
    return fail("layer-local preset application should update only the layer recipe and leave Base Image/global recipe unchanged");
  }
  photo_editor::desktop::EditorPreviewController adaptive_controller;
  adaptive_controller.loadAsset(
      QStringLiteral("adaptive-preset-asset"),
      QString(),
      QString(),
      QStringLiteral("Canon EOS R5"),
      QString(),
      QStringLiteral("RF 28-70mm F2L USM"),
      QString());
  if (!adaptive_controller.isPresetAdaptive(QStringLiteral("Camera Aware Portrait")) ||
      adaptive_controller.isPresetAdaptive(QStringLiteral("Clean Portrait"))) {
    return fail("adaptive presets should declare adaptive metadata rules");
  }
  if (!adaptive_controller.applyPreset(QStringLiteral("Camera Aware Portrait")) ||
      !almost_equal(adaptive_controller.exposure(), 0.18) ||
      !almost_equal(adaptive_controller.temperature(), 2.0) ||
      !almost_equal(adaptive_controller.texture(), -10.0) ||
      !almost_equal(adaptive_controller.clarity(), -4.0)) {
    return fail("adaptive presets should vary by camera and lens metadata");
  }
  photo_editor::desktop::EditorPreviewController changed_controls_controller;
  changed_controls_controller.loadAsset(
      QStringLiteral("changed-controls-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  const auto changed_controls =
      changed_controls_controller.presetChangedControls(QStringLiteral("Landscape Pop"));
  if (!changed_controls.contains(QStringLiteral("Texture")) ||
      !changed_controls.contains(QStringLiteral("Clarity")) ||
      !changed_controls.contains(QStringLiteral("Vibrance"))) {
    return fail("preset changed-control preview should list controls before applying");
  }
  changed_controls_controller.setPresetIntensity(0.0);
  if (!changed_controls_controller.presetChangedControls(QStringLiteral("Landscape Pop")).isEmpty()) {
    return fail("preset changed-control preview should respect preset intensity");
  }
  changed_controls_controller.setCompareAdjustmentGroups(
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("unsupported")});
  changed_controls_controller.setCompareMode(true);
  const auto selected_compare_edits =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_compare_adjustment_groups(
          changed_controls_controller);
  if (!selected_compare_edits.contains(QStringLiteral("tone")) ||
      !selected_compare_edits.contains(QStringLiteral("color")) ||
      selected_compare_edits.contains(QStringLiteral("unsupported"))) {
    return fail("compare selected edits should carry normalized adjustment groups into render requests");
  }
  photo_editor::desktop::EditorPreviewController local_compare_controller;
  local_compare_controller.loadAsset(
      QStringLiteral("local-compare-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  local_compare_controller.addMask(QStringLiteral("linear"));
  local_compare_controller.setActiveMaskExposure(0.44);
  local_compare_controller.setActiveMaskTemperature(18.0);
  local_compare_controller.setActiveMaskVibrance(26.0);
  local_compare_controller.setActiveMaskRemoveStrength(63.0);
  local_compare_controller.setActiveMaskHealTextureBlend(0.24);
  local_compare_controller.setActiveMaskHealColorBlend(0.82);
  local_compare_controller.setMaskCompareAdjustmentGroups(
      {QStringLiteral("tone"),
       QStringLiteral("presence"),
       QStringLiteral("effects"),
       QStringLiteral("unsupported")});
  local_compare_controller.setCompareMode(true);
  const auto selected_local_compare_edits =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          interactive_mask_compare_adjustment_groups(local_compare_controller);
  if (!selected_local_compare_edits.contains(QStringLiteral("tone")) ||
      !selected_local_compare_edits.contains(QStringLiteral("presence")) ||
      !selected_local_compare_edits.contains(QStringLiteral("effects")) ||
      selected_local_compare_edits.contains(QStringLiteral("unsupported"))) {
    return fail("local mask compare groups should carry normalized adjustment groups into render requests");
  }
  const auto local_compare_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(
          local_compare_controller);
  const auto local_compare_mask = local_compare_state.mask_stack.value(0).toMap();
  if (!almost_equal(local_compare_controller.activeMaskExposure(), 0.44) ||
      !almost_equal(local_compare_controller.activeMaskTemperature(), 18.0) ||
      !almost_equal(local_compare_controller.activeMaskVibrance(), 26.0) ||
      !almost_equal(local_compare_controller.activeMaskRemoveStrength(), 63.0) ||
      !almost_equal(local_compare_mask.value(QStringLiteral("exposure")).toDouble(), 0.44) ||
      !almost_equal(local_compare_mask.value(QStringLiteral("temperature")).toDouble(), 18.0) ||
      !almost_equal(local_compare_mask.value(QStringLiteral("vibrance")).toDouble(), 26.0) ||
      !almost_equal(local_compare_mask.value(QStringLiteral("removeStrength")).toDouble(), 63.0)) {
    return fail("local mask before/after groups should not mutate active mask adjustment values");
  }
  photo_editor::desktop::EditorPreviewController intensity_controller;
  intensity_controller.loadAsset(
      QStringLiteral("preset-intensity-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!intensity_controller.applyPresetWithIntensity(QStringLiteral("Landscape Pop"), 0.5) ||
      !almost_equal(intensity_controller.texture(), 9.0) ||
      !almost_equal(intensity_controller.clarity(), 10.0) ||
      !almost_equal(intensity_controller.dehaze(), 7.0) ||
      !almost_equal(intensity_controller.vibrance(), 11.0)) {
    return fail("preset intensity should scale preset adjustment deltas");
  }
  intensity_controller.resetAdjustments();
  intensity_controller.setPresetIntensity(0.25);
  if (!almost_equal(intensity_controller.presetIntensity(), 0.25) ||
      !intensity_controller.applyPreset(QStringLiteral("Landscape Pop")) ||
      !almost_equal(intensity_controller.texture(), 4.5) ||
      !almost_equal(intensity_controller.clarity(), 5.0)) {
    return fail("preset intensity property should drive regular preset application");
  }
  intensity_controller.resetAdjustments();
  if (!intensity_controller.applyPresetWithIntensity(
          QStringLiteral("Landscape Pop"),
          0.5,
          {QStringLiteral("detail")}) ||
      !almost_equal(intensity_controller.texture(), 9.0) ||
      !almost_equal(intensity_controller.contrast(), 0.0)) {
    return fail("preset intensity should respect partial adjustment groups");
  }
  intensity_controller.resetAdjustments();
  const auto preview_recipe_json = intensity_controller.presetPreviewRecipeJson(
      QStringLiteral("Landscape Pop"),
      {QStringLiteral("detail")},
      0.5);
  const auto preview_recipe_state = photo_editor::desktop::parse_edit_recipe_json(preview_recipe_json);
  if (preview_recipe_json.isEmpty() ||
      !almost_equal(preview_recipe_state.texture, 9.0) ||
      !almost_equal(preview_recipe_state.clarity, 10.0) ||
      !almost_equal(intensity_controller.texture(), 0.0) ||
      !almost_equal(intensity_controller.clarity(), 0.0)) {
    return fail("preset preview recipe should honor intensity and groups without mutating the current recipe");
  }
  photo_editor::desktop::EditorPreviewController partial_preset_controller;
  partial_preset_controller.loadAsset(
      QStringLiteral("partial-preset-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  partial_preset_controller.setExposure(0.64);
  partial_preset_controller.setContrast(12.0);
  if (!partial_preset_controller.savePreset(QStringLiteral("Tone Only"), true)) {
    return fail("saving a preset should record declared adjustment groups");
  }
  const auto tone_only_groups = partial_preset_controller.presetAdjustmentGroups(QStringLiteral("Tone Only"));
  if (tone_only_groups.size() != 1 || !tone_only_groups.contains(QStringLiteral("tone"))) {
    return fail("user presets should declare exactly the groups they touch");
  }
  partial_preset_controller.setExposure(0.0);
  partial_preset_controller.setContrast(0.0);
  partial_preset_controller.setTemperature(18.0);
  if (!partial_preset_controller.applyPreset(QStringLiteral("Tone Only")) ||
      !almost_equal(partial_preset_controller.exposure(), 0.64) ||
      !almost_equal(partial_preset_controller.contrast(), 12.0) ||
      !almost_equal(partial_preset_controller.temperature(), 18.0)) {
    return fail("preset declarations should be the default scope when applying presets");
  }
  photo_editor::desktop::EditorPreviewController safety_controller;
  safety_controller.loadAsset(
      QStringLiteral("preset-safety-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  safety_controller.setCropPreset(QStringLiteral("4x5"));
  safety_controller.addMask(QStringLiteral("linear"));
  safety_controller.setOutputColorSpace(QStringLiteral("Display P3"));
  if (!safety_controller.savePreset(QStringLiteral("Crop Mask Output"), true)) {
    return fail("saving a safety preset should work");
  }
  const auto safety_warnings = safety_controller.presetSafetyWarnings(QStringLiteral("Crop Mask Output"));
  const auto has_warning = [&safety_warnings](const QString& text) {
    return std::any_of(safety_warnings.cbegin(), safety_warnings.cend(), [&text](const QString& warning) {
      return warning.contains(text);
    });
  };
  if (!has_warning(QStringLiteral("crop")) ||
      !has_warning(QStringLiteral("masks")) ||
      !has_warning(QStringLiteral("output"))) {
    return fail("preset safety checks should warn for crop, masks, and output settings");
  }
  if (!safety_controller.presetSafetyWarnings(
                            QStringLiteral("Crop Mask Output"),
                            {QStringLiteral("tone")})
           .isEmpty()) {
    return fail("preset safety checks should respect explicit adjustment groups");
  }
  curve_controller.setColorMixerModel(QStringLiteral("HSV"));
  if (curve_controller.colorMixerModel() != QStringLiteral("HSV")) {
    return fail("color mixer model should expose HSV selection");
  }
  curve_controller.setColorMixerModel(QStringLiteral("LCH"));
  if (curve_controller.colorMixerModel() != QStringLiteral("LCH")) {
    return fail("color mixer model should expose LCH selection");
  }
  curve_controller.setColorMixerModel(QStringLiteral("OKLab"));
  if (curve_controller.colorMixerModel() != QStringLiteral("OKLab")) {
    return fail("color mixer model should expose OKLab selection");
  }
  curve_controller.setHslChannel(QStringLiteral("Reds"));
  curve_controller.setSelectiveCyan(24.0);
  curve_controller.setSelectiveMagenta(-12.0);
  curve_controller.setSelectiveYellow(9.0);
  curve_controller.setSelectiveBlack(15.0);
  if (!almost_equal(curve_controller.selectiveCyan(), 24.0) ||
      !almost_equal(curve_controller.selectiveMagenta(), -12.0) ||
      !almost_equal(curve_controller.selectiveYellow(), 9.0) ||
      !almost_equal(curve_controller.selectiveBlack(), 15.0)) {
    return fail("selective color controls should preserve CMYK edits for the active channel");
  }
  curve_controller.setHslChannel(QStringLiteral("Blues"));
  if (!almost_equal(curve_controller.selectiveCyan(), 0.0)) {
    return fail("selective color controls should be stored per color channel");
  }
  curve_controller.setHslChannel(QStringLiteral("Reds"));
  if (!almost_equal(curve_controller.selectiveBlack(), 15.0)) {
    return fail("selective color controls should restore the previous channel values");
  }
  curve_controller.setChannelMixerRedFromGreen(34.0);
  curve_controller.setChannelMixerGreenFromBlue(-16.0);
  curve_controller.setChannelMixerBlueFromRed(21.0);
  curve_controller.setChannelMixerRedFromRed(118.0);
  if (!almost_equal(curve_controller.channelMixerRedFromGreen(), 34.0) ||
      !almost_equal(curve_controller.channelMixerGreenFromBlue(), -16.0) ||
      !almost_equal(curve_controller.channelMixerBlueFromRed(), 21.0) ||
      !almost_equal(curve_controller.channelMixerRedFromRed(), 118.0)) {
    return fail("channel mixer controls should expose the RGB contribution matrix");
  }
  if (!curve_controller.resetChannelMixerRow(QStringLiteral("red")) ||
      !almost_equal(curve_controller.channelMixerRedFromRed(), 100.0) ||
      !almost_equal(curve_controller.channelMixerRedFromGreen(), 0.0) ||
      !almost_equal(curve_controller.channelMixerRedFromBlue(), 0.0) ||
      !almost_equal(curve_controller.channelMixerGreenFromBlue(), -16.0)) {
    return fail("channel mixer row reset should restore one output row without changing the others");
  }
  if (!curve_controller.applyChannelMixerPreset(QStringLiteral("Monochrome")) ||
      !almost_equal(curve_controller.channelMixerRedFromRed(), 21.26) ||
      !almost_equal(curve_controller.channelMixerRedFromGreen(), 71.52) ||
      !almost_equal(curve_controller.channelMixerBlueFromBlue(), 7.22)) {
    return fail("channel mixer monochrome preset should apply a luminance matrix");
  }
  if (!curve_controller.resetChannelMixer() ||
      !almost_equal(curve_controller.channelMixerRedFromRed(), 100.0) ||
      !almost_equal(curve_controller.channelMixerGreenFromGreen(), 100.0) ||
      !almost_equal(curve_controller.channelMixerBlueFromBlue(), 100.0) ||
      !almost_equal(curve_controller.channelMixerRedFromGreen(), 0.0)) {
    return fail("channel mixer reset should restore the identity matrix");
  }
  curve_controller.setBlackAndWhiteEnabled(true);
  curve_controller.setBlackAndWhiteReds(28.0);
  curve_controller.setBlackAndWhiteBlues(-22.0);
  if (!curve_controller.blackAndWhiteEnabled() ||
      !almost_equal(curve_controller.blackAndWhiteReds(), 28.0) ||
      !almost_equal(curve_controller.blackAndWhiteBlues(), -22.0)) {
    return fail("black-and-white mixer controls should expose monochrome channel weights");
  }
  curve_controller.setColorReplacementEnabled(true);
  curve_controller.setColorReplacementSourceHue(0.02);
  curve_controller.setColorReplacementTargetHue(0.36);
  curve_controller.setColorReplacementRange(48.0);
  curve_controller.setColorReplacementSoftness(63.0);
  curve_controller.setColorReplacementStrength(82.0);
  curve_controller.setColorReplacementPreserveLuminance(false);
  curve_controller.setColorReplacementPreserveSaturation(false);
  if (!curve_controller.colorReplacementEnabled() ||
      !almost_equal(curve_controller.colorReplacementSourceHue(), 0.02) ||
      !almost_equal(curve_controller.colorReplacementTargetHue(), 0.36) ||
      !almost_equal(curve_controller.colorReplacementRange(), 48.0) ||
      !almost_equal(curve_controller.colorReplacementSoftness(), 63.0) ||
      !almost_equal(curve_controller.colorReplacementStrength(), 82.0) ||
      curve_controller.colorReplacementPreserveLuminance() ||
      curve_controller.colorReplacementPreserveSaturation()) {
    return fail("color replacement controls should expose source hue, target hue, range, softness, strength, and preservation toggles");
  }
  curve_controller.setSkinToneProtectionEnabled(true);
  curve_controller.setSkinToneLineHue(0.09);
  curve_controller.setSkinToneProtectionRange(44.0);
  curve_controller.setSkinToneProtectionStrength(91.0);
  if (!curve_controller.skinToneProtectionEnabled() ||
      !almost_equal(curve_controller.skinToneLineHue(), 0.09) ||
      !almost_equal(curve_controller.skinToneLineAngle(), 32.4) ||
      !almost_equal(curve_controller.skinToneProtectionRange(), 44.0) ||
      !almost_equal(curve_controller.skinToneProtectionStrength(), 91.0)) {
    return fail("skin tone controls should expose line angle, protected range, and protection strength");
  }
  curve_controller.setColorHarmonyEnabled(true);
  curve_controller.setColorHarmonyMode(QStringLiteral("Triadic"));
  curve_controller.setColorHarmonyBaseHue(0.04);
  curve_controller.setColorHarmonyStrength(68.0);
  if (!curve_controller.colorHarmonyEnabled() ||
      curve_controller.colorHarmonyMode() != QStringLiteral("Triadic") ||
      !almost_equal(curve_controller.colorHarmonyBaseHue(), 0.04) ||
      !almost_equal(curve_controller.colorHarmonyStrength(), 68.0)) {
    return fail("color harmony controls should expose mode, base hue, and strength");
  }

  controller.addMask(QStringLiteral("linear"));
  if (controller.maskCount() != 1 || !controller.hasActiveMask() ||
      controller.activeMaskKind() != QStringLiteral("linear")) {
    return fail("adding a mask should create and select the requested stack entry");
  }
  const auto created_linear_mask = controller.maskStack().at(0).toMap();
  if (created_linear_mask.value(QStringLiteral("id")).toString().isEmpty() ||
      !created_linear_mask.contains(QStringLiteral("parentId")) ||
      created_linear_mask.value(QStringLiteral("maskSetId")).toString().isEmpty() ||
      created_linear_mask.value(QStringLiteral("operation")).toString() != QStringLiteral("add") ||
      created_linear_mask.value(QStringLiteral("toolRole")).toString() != QStringLiteral("selection") ||
      created_linear_mask.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
      created_linear_mask.value(QStringLiteral("sourceSchemaVersion")).toInt() != 1) {
    return fail("new mask sources should carry stable identity, set, operation, role, and version fields");
  }
  if (controller.photoLayerCount() != 1) {
    return fail("creating a selection source should automatically create an adjustment layer");
  }
  const auto auto_selection_layer = controller.photoLayers().at(0).toMap();
  if (auto_selection_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("adjustment") ||
      !auto_selection_layer.value(QStringLiteral("selectionScoped")).toBool() ||
      auto_selection_layer.value(QStringLiteral("selectionMaskSetId")).toString() !=
          created_linear_mask.value(QStringLiteral("maskSetId")).toString() ||
      auto_selection_layer.value(QStringLiteral("displayName")).toString() !=
          QStringLiteral("Adjustment Layer 1") ||
      !auto_selection_layer.value(QStringLiteral("displaySubtitle"))
           .toString()
           .contains(created_linear_mask.value(QStringLiteral("maskSetName")).toString()) ||
      !almost_equal(auto_selection_layer.value(QStringLiteral("exposure")).toDouble(), 0.0) ||
      !almost_equal(auto_selection_layer.value(QStringLiteral("temperature")).toDouble(), 0.0) ||
      !almost_equal(auto_selection_layer.value(QStringLiteral("tint")).toDouble(), 0.0)) {
    return fail("auto-created selection adjustment layer should be neutral and linked to the mask set");
  }
  controller.undo();
  if (controller.maskCount() != 0 || controller.photoLayerCount() != 0) {
    return fail("undo should remove the auto-created adjustment layer with its selection source");
  }
  controller.redo();
  if (controller.maskCount() != 1 || controller.photoLayerCount() != 1 ||
      !controller.hasActiveMask()) {
    return fail("redo should restore the selection source and auto-created adjustment layer together");
  }
  photo_editor::desktop::EditorPreviewController independent_selection_layer_controller;
  independent_selection_layer_controller.addMask(QStringLiteral("linear"));
  independent_selection_layer_controller.addMask(QStringLiteral("radial"));
  if (independent_selection_layer_controller.maskCount() != 2 ||
      independent_selection_layer_controller.photoLayerCount() != 1) {
    return fail("adding a source to an active selection set should reuse the existing adjustment layer");
  }
  auto second_selection_mask =
      independent_selection_layer_controller.maskStack()
          .at(independent_selection_layer_controller.activeMaskIndex())
          .toMap();
  second_selection_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("independent-radial-set"));
  second_selection_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Independent Radial"));
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          independent_selection_layer_controller,
          second_selection_mask) ||
      independent_selection_layer_controller.maskCount() != 2 ||
      independent_selection_layer_controller.photoLayerCount() != 2) {
    return fail("creating an independent selection set should create its own adjustment layer");
  }
  second_selection_mask =
      independent_selection_layer_controller.maskStack()
          .at(independent_selection_layer_controller.activeMaskIndex())
          .toMap();
  const auto second_selection_layer =
      independent_selection_layer_controller.photoLayers().at(0).toMap();
  const auto first_selection_layer =
      independent_selection_layer_controller.photoLayers().at(1).toMap();
  if (second_selection_layer.value(QStringLiteral("selectionMaskSetId")).toString() !=
          second_selection_mask.value(QStringLiteral("maskSetId")).toString() ||
      second_selection_layer.value(QStringLiteral("selectionMaskSetId")).toString() ==
          first_selection_layer.value(QStringLiteral("selectionMaskSetId")).toString()) {
    return fail("auto-created adjustment layers should be linked per mask set, not globally shared");
  }
  second_selection_mask.insert(QStringLiteral("operation"), QStringLiteral("subtract"));
  second_selection_mask.insert(QStringLiteral("invert"), true);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          independent_selection_layer_controller,
          second_selection_mask)) {
    return fail("test setup should be able to mark the second mask as subtract/inverted");
  }
  second_selection_mask =
      independent_selection_layer_controller.maskStack()
          .at(independent_selection_layer_controller.activeMaskIndex())
          .toMap();
  auto multi_mask_adjustment_layer = second_selection_layer;
  QVariantList combined_mask_set_ids;
  combined_mask_set_ids.append(second_selection_mask.value(QStringLiteral("maskSetId")).toString());
  combined_mask_set_ids.append(first_selection_layer.value(QStringLiteral("selectionMaskSetId")).toString());
  QVariantList combined_mask_set_names;
  combined_mask_set_names.append(second_selection_mask.value(QStringLiteral("maskSetName")).toString());
  combined_mask_set_names.append(first_selection_layer.value(QStringLiteral("selectionMaskSetName")).toString());
  multi_mask_adjustment_layer.insert(QStringLiteral("selectionMaskSetIds"), combined_mask_set_ids);
  multi_mask_adjustment_layer.insert(QStringLiteral("selectionMaskSetNames"), combined_mask_set_names);
  multi_mask_adjustment_layer.insert(
      QStringLiteral("selectionMaskSetId"),
      combined_mask_set_ids.constFirst().toString());
  multi_mask_adjustment_layer.insert(
      QStringLiteral("selectionMaskSetName"),
      combined_mask_set_names.constFirst().toString());
  QVariantList multi_mask_layers;
  multi_mask_layers.append(multi_mask_adjustment_layer);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::apply_photo_layer_stack_change(
          independent_selection_layer_controller,
          multi_mask_layers)) {
    return fail("multi-mask adjustment layer setup should apply through the normal layer stack path");
  }
  const auto normalized_multi_mask_layer =
      independent_selection_layer_controller.photoLayers().constFirst().toMap();
  const auto normalized_multi_mask_ids =
      normalized_multi_mask_layer.value(QStringLiteral("selectionMaskSetIds")).toList();
  const auto normalized_multi_mask_names =
      normalized_multi_mask_layer.value(QStringLiteral("selectionMaskSetNames")).toList();
  const auto multi_mask_signature =
      normalized_multi_mask_layer.value(QStringLiteral("selectionMaskSetSignature")).toString();
  if (independent_selection_layer_controller.photoLayerCount() != 1 ||
      normalized_multi_mask_layer.value(QStringLiteral("selectionMaskSetCount")).toInt() != 2 ||
      normalized_multi_mask_ids.size() != 2 ||
      normalized_multi_mask_names.size() != 2 ||
      !normalized_multi_mask_ids.contains(combined_mask_set_ids.constFirst()) ||
      !normalized_multi_mask_ids.contains(combined_mask_set_ids.constLast()) ||
      !multi_mask_signature.contains(combined_mask_set_ids.constFirst().toString()) ||
      !multi_mask_signature.contains(combined_mask_set_ids.constLast().toString()) ||
      !normalized_multi_mask_layer.value(QStringLiteral("displaySubtitle"))
           .toString()
           .contains(QStringLiteral("2 masks")) ||
      !normalized_multi_mask_layer.value(QStringLiteral("layerAuditLabel"))
           .toString()
           .contains(QStringLiteral("2 masks")) ||
      !normalized_multi_mask_layer.value(QStringLiteral("thumbnailInvalidationMaskId"))
           .toString()
           .contains(QStringLiteral("ids:"))) {
    return fail("adjustment layers should preserve multiple linked mask-set ids, names, count, and cache identity");
  }
  auto found_add_combine = false;
  auto found_subtract_invert_combine = false;
  for (const auto& value : independent_selection_layer_controller.maskSets()) {
    const auto set = value.toMap();
    const auto set_id = set.value(QStringLiteral("id")).toString();
    if (set_id == first_selection_layer.value(QStringLiteral("selectionMaskSetId")).toString()) {
      found_add_combine =
          set.value(QStringLiteral("combineModeSummary")).toString().contains(QStringLiteral("Add")) &&
          set.value(QStringLiteral("combineModeContract")).toString().contains(QStringLiteral("modes:add"));
    }
    if (set_id == second_selection_mask.value(QStringLiteral("maskSetId")).toString()) {
      found_subtract_invert_combine =
          set.value(QStringLiteral("combineModeSummary")).toString().contains(QStringLiteral("Subtract")) &&
          set.value(QStringLiteral("combineModeSummary")).toString().contains(QStringLiteral("Inverted")) &&
          set.value(QStringLiteral("combineModeContract")).toString().contains(QStringLiteral("modes:subtract")) &&
          set.value(QStringLiteral("combineModeContract")).toString().contains(QStringLiteral("inverted:true"));
    }
  }
  if (!found_add_combine || !found_subtract_invert_combine) {
    return fail("mask sets should expose explicit combine mode and invert metadata for layer children");
  }
  if (!independent_selection_layer_controller.moveMaskSetInPhotoLayer(
          0,
          first_selection_layer.value(QStringLiteral("selectionMaskSetId")).toString(),
          0)) {
    return fail("adjustment layers should allow mask sets to be reordered inside the layer");
  }
  const auto reordered_multi_mask_layer =
      independent_selection_layer_controller.photoLayers().constFirst().toMap();
  const auto reordered_multi_mask_ids =
      reordered_multi_mask_layer.value(QStringLiteral("selectionMaskSetIds")).toList();
  if (reordered_multi_mask_ids.size() != 2 ||
      reordered_multi_mask_ids.constFirst().toString() !=
          first_selection_layer.value(QStringLiteral("selectionMaskSetId")).toString() ||
      reordered_multi_mask_ids.constLast().toString() !=
          second_selection_mask.value(QStringLiteral("maskSetId")).toString() ||
      reordered_multi_mask_layer.value(QStringLiteral("selectionMaskSetId")).toString() !=
          first_selection_layer.value(QStringLiteral("selectionMaskSetId")).toString() ||
      !reordered_multi_mask_layer.value(QStringLiteral("selectionMaskSetSignature"))
           .toString()
           .contains(reordered_multi_mask_ids.constFirst().toString()) ||
      !reordered_multi_mask_layer.value(QStringLiteral("thumbnailInvalidationMaskId"))
           .toString()
           .contains(reordered_multi_mask_ids.constLast().toString())) {
    return fail("reordering layer masks should update ordered ids, legacy first id, signature, and thumbnail identity");
  }
  if (independent_selection_layer_controller.moveMaskSetInPhotoLayer(
          0,
          first_selection_layer.value(QStringLiteral("selectionMaskSetId")).toString(),
          0)) {
    return fail("reordering a layer mask to its current position should be a no-op");
  }

  photo_editor::desktop::EditorPreviewController selection_brush_layer_controller;
  selection_brush_layer_controller.loadAsset(
      QStringLiteral("selection-brush-layer-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  selection_brush_layer_controller.startSelectionBrush(false);
  if (selection_brush_layer_controller.maskCount() != 1 ||
      selection_brush_layer_controller.photoLayerCount() != 1 ||
      selection_brush_layer_controller.photoLayers()
              .at(0)
              .toMap()
              .value(QStringLiteral("kind"))
              .toString() != QStringLiteral("adjustment")) {
    return fail("starting a selection brush should also create the adjustment layer");
  }
  selection_brush_layer_controller.deletePhotoLayer(0);
  if (selection_brush_layer_controller.photoLayerCount() != 0 ||
      selection_brush_layer_controller.ensureActiveSelectionAdjustmentLayer() != 0 ||
      selection_brush_layer_controller.photoLayerCount() != 1) {
    return fail("legacy selection masks should be able to create a missing linked adjustment layer on demand");
  }

  photo_editor::desktop::EditorPreviewController retouch_brush_layer_controller;
  retouch_brush_layer_controller.loadAsset(
      QStringLiteral("retouch-brush-layer-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  retouch_brush_layer_controller.startRemoveBrush();
  if (retouch_brush_layer_controller.maskCount() != 1 ||
      retouch_brush_layer_controller.photoLayerCount() != 0) {
    return fail("retouch masks should not create adjustment layers");
  }

  controller.setActiveMaskName(QStringLiteral("Subject Sweep"));
  controller.setActiveMaskExposure(0.75);
  controller.setActiveMaskContrast(18.0);
  controller.setActiveMaskHighlights(-22.0);
  controller.setActiveMaskShadows(31.0);
  controller.setActiveMaskWhites(12.0);
  controller.setActiveMaskBlacks(-14.0);
  controller.setActiveMaskPosition(0.61);
  controller.setActiveMaskRotation(14.0);
  if (!almost_equal(controller.linearMaskExposure(), 0.75) ||
      !almost_equal(controller.linearMaskPosition(), 0.61)) {
    return fail("active mask edits should synchronize legacy compatibility fields");
  }
  if (!almost_equal(controller.activeMaskContrast(), 18.0) ||
      !almost_equal(controller.activeMaskHighlights(), -22.0) ||
      !almost_equal(controller.activeMaskShadows(), 31.0) ||
      !almost_equal(controller.activeMaskWhites(), 12.0) ||
      !almost_equal(controller.activeMaskBlacks(), -14.0)) {
    return fail("selection masks should expose local tone controls");
  }
  controller.setActiveMaskRefineLuminanceMin(0.18);
  controller.setActiveMaskRefineLuminanceMax(0.82);
  controller.setActiveMaskRefineHueCenter(1.16);
  controller.setActiveMaskRefineHueRange(0.22);
  controller.setActiveMaskRefineSaturationMin(0.25);
  controller.setActiveMaskRefineSaturationMax(0.88);
  controller.setActiveMaskRefineDepthMin(0.12);
  controller.setActiveMaskRefineDepthMax(0.76);
  controller.setActiveMaskRefineEdgeConfidenceMin(0.31);
  controller.setActiveMaskRefineRangeFeather(0.07);
  if (!almost_equal(controller.activeMaskRefineLuminanceMin(), 0.18) ||
      !almost_equal(controller.activeMaskRefineLuminanceMax(), 0.82) ||
      !almost_equal(controller.activeMaskRefineHueCenter(), 0.16) ||
      !almost_equal(controller.activeMaskRefineHueRange(), 0.22) ||
      !almost_equal(controller.activeMaskRefineSaturationMin(), 0.25) ||
      !almost_equal(controller.activeMaskRefineSaturationMax(), 0.88) ||
      !almost_equal(controller.activeMaskRefineDepthMin(), 0.12) ||
      !almost_equal(controller.activeMaskRefineDepthMax(), 0.76) ||
      !almost_equal(controller.activeMaskRefineEdgeConfidenceMin(), 0.31) ||
      !almost_equal(controller.activeMaskRefineRangeFeather(), 0.07)) {
    return fail("selection masks should expose range refinement controls");
  }

  auto cached_brush_mask =
      photo_editor::desktop::default_mask_definition(QStringLiteral("brush"), 1);
  cached_brush_mask.insert(QStringLiteral("id"), QStringLiteral("subject-brush-source"));
  cached_brush_mask.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
  cached_brush_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  cached_brush_mask.insert(QStringLiteral("brushPreset"), QStringLiteral("Soft Round"));
  cached_brush_mask.insert(QStringLiteral("brushSize"), 0.045);
  cached_brush_mask.insert(QStringLiteral("brushFlow"), 0.72);
  cached_brush_mask.insert(QStringLiteral("brushHardness"), 0.34);
  cached_brush_mask.insert(QStringLiteral("brushSpacing"), 0.5);
  cached_brush_mask.insert(QStringLiteral("brushAutoMask"), true);
  cached_brush_mask.insert(QStringLiteral("brushAutoMaskTolerance"), 0.24);
  cached_brush_mask.insert(QStringLiteral("selectionFeather"), 9.0);
  cached_brush_mask.insert(QStringLiteral("selectionSmooth"), 11.0);
  cached_brush_mask.insert(QStringLiteral("refineLuminanceMin"), 0.12);
  cached_brush_mask.insert(QStringLiteral("refineLuminanceMax"), 0.91);
  cached_brush_mask.insert(QStringLiteral("segmentationData"), QByteArray(4, char(0x55)));
  cached_brush_mask.insert(QStringLiteral("segmentationWidth"), 2);
  cached_brush_mask.insert(QStringLiteral("segmentationHeight"), 2);
  QVariantMap cache_brush_point;
  cache_brush_point.insert(QStringLiteral("x"), 0.42);
  cache_brush_point.insert(QStringLiteral("y"), 0.38);
  cache_brush_point.insert(QStringLiteral("size"), 0.045);
  cache_brush_point.insert(QStringLiteral("flow"), 0.72);
  cache_brush_point.insert(QStringLiteral("pressure"), 0.8);
  cache_brush_point.insert(QStringLiteral("tiltX"), 0.0);
  cache_brush_point.insert(QStringLiteral("tiltY"), 0.0);
  cache_brush_point.insert(QStringLiteral("erase"), false);
  cached_brush_mask.insert(QStringLiteral("brushPoints"), QVariantList{cache_brush_point});

  const QRectF cache_crop(0.05, 0.10, 0.82, 0.73);
  const QSize cache_raster_size(1024, 768);
  const QRect cache_tile_rect(256, 128, 256, 256);
  const auto base_mask_tile_key =
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_tile_cache_key(
          cached_brush_mask,
          QStringLiteral("source-rev-a"),
          cache_crop,
          cache_raster_size,
          cache_tile_rect);
  if (base_mask_tile_key.isEmpty()) {
    return fail("mask tile cache key should be available for a valid mask source and tile");
  }
  const auto key_for_mask = [&](const QVariantMap& mask,
                                const QString& source_revision,
                                const QRectF& crop_rect,
                                const QSize& raster_size,
                                const QRect& tile_rect) {
    return photo_editor::desktop::EditorPreviewControllerTestAccess::mask_tile_cache_key(
        mask,
        source_revision,
        crop_rect,
        raster_size,
        tile_rect);
  };
  if (key_for_mask(
          cached_brush_mask,
          QStringLiteral("source-rev-b"),
          cache_crop,
          cache_raster_size,
          cache_tile_rect) == base_mask_tile_key ||
      key_for_mask(
          cached_brush_mask,
          QStringLiteral("source-rev-a"),
          QRectF(0.06, 0.10, 0.82, 0.73),
          cache_raster_size,
          cache_tile_rect) == base_mask_tile_key ||
      key_for_mask(
          cached_brush_mask,
          QStringLiteral("source-rev-a"),
          cache_crop,
          QSize(2048, 1536),
          cache_tile_rect) == base_mask_tile_key ||
      key_for_mask(
          cached_brush_mask,
          QStringLiteral("source-rev-a"),
          cache_crop,
          cache_raster_size,
          QRect(512, 128, 256, 256)) == base_mask_tile_key) {
    return fail("mask tile cache key should include source revision, crop, raster size, and tile rect");
  }
  auto changed_refine_mask = cached_brush_mask;
  changed_refine_mask.insert(QStringLiteral("selectionFeather"), 12.0);
  auto changed_source_mask = cached_brush_mask;
  changed_source_mask.insert(QStringLiteral("id"), QStringLiteral("subject-brush-source-copy"));
  auto changed_data_mask = cached_brush_mask;
  changed_data_mask.insert(QStringLiteral("segmentationData"), QByteArray(4, char(0x22)));
  auto unrelated_adjustment_mask = cached_brush_mask;
  unrelated_adjustment_mask.insert(QStringLiteral("exposure"), 1.25);
  if (key_for_mask(
          changed_refine_mask,
          QStringLiteral("source-rev-a"),
          cache_crop,
          cache_raster_size,
          cache_tile_rect) == base_mask_tile_key ||
      key_for_mask(
          changed_source_mask,
          QStringLiteral("source-rev-a"),
          cache_crop,
          cache_raster_size,
          cache_tile_rect) == base_mask_tile_key ||
      key_for_mask(
          changed_data_mask,
          QStringLiteral("source-rev-a"),
          cache_crop,
          cache_raster_size,
          cache_tile_rect) == base_mask_tile_key) {
    return fail("mask tile cache key should include source id, refine settings, and raster data hash");
  }
  if (key_for_mask(
          unrelated_adjustment_mask,
          QStringLiteral("source-rev-a"),
          cache_crop,
          cache_raster_size,
          cache_tile_rect) != base_mask_tile_key) {
    return fail("mask tile cache key should not depend on unrelated local adjustment sliders");
  }

  photo_editor::desktop::EditorPreviewControllerTestAccess::clear_mask_tile_cache();
  const QByteArray first_tile_data(16 * 16, char(0x7f));
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::insert_mask_tile(
          base_mask_tile_key,
          first_tile_data,
          QSize(16, 16)) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_tile_cache_entries() != 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_tile_cache_bytes() !=
          first_tile_data.size()) {
    return fail("mask tile cache should store valid matte tiles and account memory");
  }
  QByteArray found_tile_data;
  QSize found_tile_size;
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::find_mask_tile(
          base_mask_tile_key,
          &found_tile_data,
          &found_tile_size) ||
      found_tile_data != first_tile_data || found_tile_size != QSize(16, 16)) {
    return fail("mask tile cache should return matte tile data and dimensions");
  }
  const QByteArray updated_tile_data(16 * 16, char(0x44));
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::insert_mask_tile(
          base_mask_tile_key,
          updated_tile_data,
          QSize(16, 16)) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_tile_cache_entries() != 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_tile_cache_bytes() !=
          updated_tile_data.size() ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::find_mask_tile(
          base_mask_tile_key,
          &found_tile_data,
          &found_tile_size) ||
      found_tile_data != updated_tile_data) {
    return fail("mask tile cache should replace existing tiles without double-counting memory");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::insert_mask_tile(
          QStringLiteral("invalid-mask-tile"),
          QByteArray(12, char(0x11)),
          QSize(4, 4)) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_tile_cache_entries() != 1) {
    return fail("mask tile cache should reject malformed matte tile payloads");
  }
  photo_editor::desktop::EditorPreviewControllerTestAccess::clear_mask_tile_cache();

  controller.addMask(QStringLiteral("radial"));
  controller.setActiveMaskOperation(QStringLiteral("subtract"));
  controller.setActiveMaskName(QStringLiteral("Radial Cut"));
  controller.setActiveMaskCenterX(0.43);
  controller.setActiveMaskCenterY(0.57);
  controller.setActiveMaskRadiusX(0.18);
  controller.setActiveMaskRadiusY(0.22);
  if (controller.maskCount() != 2 ||
      controller.activeMaskOperation() != QStringLiteral("subtract")) {
    return fail("stack masks should retain per-entry operations");
  }
  controller.setActiveMaskIndex(0);
  if (controller.activeMaskName() != QStringLiteral("Subject Sweep")) {
    return fail("active mask access should refresh when switching the selected mask");
  }
  controller.setActiveMaskIndex(1);
  if (controller.activeMaskName() != QStringLiteral("Radial Cut")) {
    return fail("active mask cache should not return stale data after switching masks");
  }
  controller.setActiveMaskSolo(true);
  controller.setActiveMaskLocked(true);
  controller.setActiveMaskName(QStringLiteral("Locked Rename"));
  controller.setActiveMaskEnabled(false);
  if (!controller.activeMaskSolo() || !controller.activeMaskLocked() ||
      controller.activeMaskName() != QStringLiteral("Radial Cut") ||
      !controller.activeMaskEnabled() ||
      !controller.activeMaskEditDisabledReason().contains(QStringLiteral("Locked selection source"))) {
    return fail("locked masks should keep solo state while rejecting ordinary edits");
  }
  controller.removeActiveMask();
  if (controller.maskCount() != 2) {
    return fail("locked masks should not be deleted");
  }
  controller.setActiveMaskLocked(false);
  controller.setActiveMaskSolo(false);
  if (controller.activeMaskLocked() || controller.activeMaskSolo() ||
      !controller.activeMaskEditDisabledReason().isEmpty()) {
    return fail("mask lock and solo toggles should be reversible");
  }

  controller.moveActiveMask(-1);
  if (controller.activeMaskIndex() != 0 ||
      controller.maskStack().at(0).toMap().value(QStringLiteral("kind")).toString() !=
          QStringLiteral("radial")) {
    return fail("moving the active mask should reorder the stack");
  }

  controller.duplicateActiveMask();
  const auto original_radial = controller.maskStack().at(0).toMap();
  const auto duplicate_radial = controller.maskStack().at(1).toMap();
  if (controller.maskCount() != 3 ||
      duplicate_radial.value(QStringLiteral("kind")).toString() != QStringLiteral("radial") ||
      duplicate_radial.value(QStringLiteral("id")).toString() ==
          original_radial.value(QStringLiteral("id")).toString() ||
      duplicate_radial.value(QStringLiteral("name")).toString() ==
          original_radial.value(QStringLiteral("name")).toString() ||
      !duplicate_radial.value(QStringLiteral("name")).toString().endsWith(QStringLiteral("Copy"))) {
    return fail("duplicating the active mask should insert a cloned entry with a new id and name");
  }
  auto found_radial_badge = false;
  for (const auto& set_value : controller.maskSets()) {
    const auto sources = set_value.toMap().value(QStringLiteral("sources")).toList();
    for (const auto& source_value : sources) {
      const auto source = source_value.toMap();
      if (source.value(QStringLiteral("flatIndex")).toInt() == controller.activeMaskIndex()) {
        found_radial_badge =
            source.value(QStringLiteral("sourceBadge")).toString() == QStringLiteral("Radial") &&
            source.value(QStringLiteral("operationBadge")).toString() == QStringLiteral("Subtract") &&
            source.value(QStringLiteral("sourceAuditLabel")).toString().contains(QStringLiteral("Radial")) &&
            source.value(QStringLiteral("sourceAuditLabel")).toString().contains(QStringLiteral("Subtract"));
      }
    }
  }
  if (!found_radial_badge ||
      !controller.maskStackAuditSignature().contains(QStringLiteral("sources:3")) ||
      !controller.maskStackAuditSignature().contains(QStringLiteral("invalid:0")) ||
      !controller.maskStackAuditSignature().contains(QStringLiteral("active:1"))) {
    return fail("mask stack rows should expose source badges, operation badges, and audit metadata");
  }

  controller.removeActiveMask();
  if (controller.maskCount() != 2) {
    return fail("removing the active mask should shrink the stack");
  }

  controller.duplicateActiveMaskInverted();
  if (controller.maskCount() != 3 || !controller.activeMaskSelectionInverted() ||
      controller.maskStack().at(2).toMap().value(QStringLiteral("id")).toString() ==
          controller.maskStack().at(1).toMap().value(QStringLiteral("id")).toString() ||
      !controller.maskStackAuditSignature().contains(QStringLiteral("sources:3"))) {
    return fail("duplicate and invert should create a uniquely identified inverted copy");
  }
  controller.removeActiveMask();
  if (controller.maskCount() != 2) {
    return fail("removing a duplicate-and-invert mask should restore the stack size");
  }

  photo_editor::desktop::EditorPreviewController shape_mask_controller;
  shape_mask_controller.addMask(QStringLiteral("rectangle"));
  shape_mask_controller.setActiveMaskCenterX(0.41);
  shape_mask_controller.setActiveMaskCenterY(0.62);
  shape_mask_controller.setActiveMaskRadiusX(0.29);
  shape_mask_controller.setActiveMaskRadiusY(0.17);
  if (shape_mask_controller.activeMaskKind() != QStringLiteral("rectangle") ||
      !almost_equal(shape_mask_controller.activeMaskCenterX(), 0.41) ||
      !almost_equal(shape_mask_controller.activeMaskRadiusY(), 0.17) ||
      shape_mask_controller.activeMaskOutlinePaths().isEmpty()) {
    return fail("rectangle selection sources should expose editable center/radius geometry and outlines");
  }
  auto found_rectangle_badge = false;
  for (const auto& set_value : shape_mask_controller.maskSets()) {
    for (const auto& source_value : set_value.toMap().value(QStringLiteral("sources")).toList()) {
      found_rectangle_badge = found_rectangle_badge ||
                              source_value.toMap().value(QStringLiteral("sourceBadge")).toString() ==
                                  QStringLiteral("Rectangle");
    }
  }
  shape_mask_controller.addMask(QStringLiteral("ellipse"));
  shape_mask_controller.setActiveMaskOperation(QStringLiteral("subtract"));
  shape_mask_controller.setActiveMaskCenterX(0.57);
  shape_mask_controller.setActiveMaskCenterY(0.39);
  shape_mask_controller.setActiveMaskRadiusX(0.23);
  shape_mask_controller.setActiveMaskRadiusY(0.31);
  if (!found_rectangle_badge || shape_mask_controller.activeMaskKind() != QStringLiteral("ellipse") ||
      shape_mask_controller.activeMaskOperation() != QStringLiteral("subtract") ||
      !almost_equal(shape_mask_controller.activeMaskCenterY(), 0.39) ||
      !almost_equal(shape_mask_controller.activeMaskRadiusX(), 0.23) ||
      shape_mask_controller.activeMaskOutlinePaths().isEmpty()) {
    return fail("ellipse selection sources should expose badges, editable geometry, operations, and outlines");
  }

  const auto inherited_set_id =
      controller.maskStack().at(controller.activeMaskIndex()).toMap().value(QStringLiteral("maskSetId")).toString();
  controller.addMask(QStringLiteral("radial"));
  const auto inherited_source_index = controller.activeMaskIndex();
  if (controller.maskCount() != 3 ||
      controller.maskStack().at(inherited_source_index).toMap().value(QStringLiteral("maskSetId")).toString() !=
          inherited_set_id) {
    return fail("new selection sources should inherit the active mask set for set-level operations");
  }
  if (!controller.setMaskSourceEnabled(inherited_source_index, false) ||
      controller.maskStack().at(inherited_source_index).toMap().value(QStringLiteral("enabled")).toBool()) {
    return fail("source visibility toggles should hide one source component");
  }
  auto inherited_set_enabled = false;
  for (const auto& set_value : controller.maskSets()) {
    const auto set = set_value.toMap();
    if (set.value(QStringLiteral("id")).toString() == inherited_set_id) {
      inherited_set_enabled = set.value(QStringLiteral("enabled")).toBool();
    }
  }
  if (!inherited_set_enabled || !controller.setMaskSourceEnabled(inherited_source_index, true)) {
    return fail("hiding one source should keep the mask set visible while other sources remain enabled");
  }
  if (!controller.setMaskSourceSolo(inherited_source_index, true) ||
      !controller.maskStack().at(inherited_source_index).toMap().value(QStringLiteral("solo")).toBool() ||
      controller.maskStack().at(1).toMap().value(QStringLiteral("solo")).toBool()) {
    return fail("source solo should isolate one source component and clear sibling solo state");
  }
  if (!controller.setMaskSourceSolo(inherited_source_index, false) ||
      controller.maskStack().at(inherited_source_index).toMap().value(QStringLiteral("solo")).toBool()) {
    return fail("source solo should be clearable at the component level");
  }
  if (!controller.setMaskSetEnabled(inherited_set_id, false) ||
      controller.maskStack().at(1).toMap().value(QStringLiteral("enabled")).toBool() ||
      controller.maskStack().at(inherited_source_index).toMap().value(QStringLiteral("enabled")).toBool()) {
    return fail("mask-set visibility should hide every source in the set");
  }
  auto inherited_set_visible_after_hide = true;
  for (const auto& set_value : controller.maskSets()) {
    const auto set = set_value.toMap();
    if (set.value(QStringLiteral("id")).toString() == inherited_set_id) {
      inherited_set_visible_after_hide = set.value(QStringLiteral("enabled")).toBool();
    }
  }
  if (inherited_set_visible_after_hide || !controller.setMaskSetEnabled(inherited_set_id, true)) {
    return fail("mask-set visibility should expose the aggregate hidden state and restore sources");
  }
  if (!controller.setMaskSetSolo(inherited_set_id, true)) {
    return fail("mask-set solo should isolate every source in the set and clear other sets");
  }
  auto target_set_sources = 0;
  auto target_set_solo_sources = 0;
  auto other_set_solo_sources = 0;
  for (const auto& mask_value : controller.maskStack()) {
    const auto mask = mask_value.toMap();
    const auto is_target_set = mask.value(QStringLiteral("maskSetId")).toString() == inherited_set_id;
    const auto is_solo = mask.value(QStringLiteral("solo")).toBool();
    if (is_target_set) {
      ++target_set_sources;
      if (is_solo) {
        ++target_set_solo_sources;
      }
    } else if (is_solo) {
      ++other_set_solo_sources;
    }
  }
  if (target_set_sources < 2 || target_set_solo_sources != target_set_sources ||
      other_set_solo_sources != 0) {
    return fail("mask-set solo should isolate every source in the set and clear other sets");
  }
  if (!controller.setMaskSetSolo(inherited_set_id, false) ||
      controller.maskStack().at(1).toMap().value(QStringLiteral("solo")).toBool() ||
      controller.maskStack().at(inherited_source_index).toMap().value(QStringLiteral("solo")).toBool()) {
    return fail("mask-set solo should be clearable at the set level");
  }

  photo_editor::desktop::EditorPreviewController reorder_controller;
  reorder_controller.addMask(QStringLiteral("linear"));
  reorder_controller.setActiveMaskName(QStringLiteral("Set A Linear"));
  reorder_controller.setActiveMaskExposure(0.42);
  reorder_controller.setActiveMaskTemperature(8.0);
  const auto reorder_set_a_id =
      reorder_controller.maskStack().at(0).toMap().value(QStringLiteral("maskSetId")).toString();
  reorder_controller.addMask(QStringLiteral("radial"));
  reorder_controller.setActiveMaskName(QStringLiteral("Set A Radial"));
  reorder_controller.deselectActiveMask();
  reorder_controller.addMask(QStringLiteral("ellipse"));
  reorder_controller.setActiveMaskName(QStringLiteral("Set B Ellipse"));
  reorder_controller.setActiveMaskExposure(-0.7);
  reorder_controller.setActiveMaskTemperature(-12.0);
  const auto reorder_set_b_id =
      reorder_controller.maskStack().at(2).toMap().value(QStringLiteral("maskSetId")).toString();
  if (reorder_set_a_id.isEmpty() || reorder_set_b_id.isEmpty() ||
      reorder_set_a_id == reorder_set_b_id || reorder_controller.maskSetCount() != 2) {
    return fail("reorder fixture should create two independent mask sets");
  }
  if (!reorder_controller.moveMaskSource(0, reorder_set_a_id, 1) ||
      reorder_controller.activeMaskIndex() != 1 ||
      reorder_controller.maskStack().at(0).toMap().value(QStringLiteral("name")).toString() !=
          QStringLiteral("Set A Radial") ||
      reorder_controller.maskStack().at(1).toMap().value(QStringLiteral("name")).toString() !=
          QStringLiteral("Set A Linear")) {
    return fail("drag-style reorder should move a source within its mask set");
  }
  if (!reorder_controller.moveMaskSource(1, reorder_set_b_id, 1) ||
      reorder_controller.activeMaskIndex() != 2 ||
      reorder_controller.maskStack().at(2).toMap().value(QStringLiteral("maskSetId")).toString() !=
          reorder_set_b_id ||
      reorder_controller.maskStack().at(2).toMap().value(QStringLiteral("maskSetName")).toString() !=
          reorder_controller.maskStack().at(1).toMap().value(QStringLiteral("maskSetName")).toString() ||
      !almost_equal(reorder_controller.maskStack().at(2).toMap().value(QStringLiteral("exposure")).toDouble(), -0.7) ||
      !almost_equal(reorder_controller.maskStack().at(2).toMap().value(QStringLiteral("temperature")).toDouble(), -12.0)) {
    return fail("drag-style reorder should move a source between mask sets and inherit target set adjustments");
  }
  auto found_reordered_set_b = false;
  for (const auto& set_value : reorder_controller.maskSets()) {
    const auto set = set_value.toMap();
    if (set.value(QStringLiteral("id")).toString() != reorder_set_b_id) {
      continue;
    }
    const auto sources = set.value(QStringLiteral("sources")).toList();
    found_reordered_set_b =
        sources.size() == 2 &&
        sources.at(0).toMap().value(QStringLiteral("name")).toString() ==
            QStringLiteral("Set B Ellipse") &&
        sources.at(1).toMap().value(QStringLiteral("name")).toString() ==
            QStringLiteral("Set A Linear");
  }
  if (!found_reordered_set_b) {
    return fail("grouped mask sets should expose reordered source order after cross-set moves");
  }
  const auto typed_reorder_sources = reorder_controller.typedMaskSources();
  if (typed_reorder_sources.size() != 3) {
    return fail("typed mask source view should expose every flat mask source");
  }
  const auto typed_reordered_source = typed_reorder_sources.at(2).toMap();
  const auto typed_reordered_metadata =
      typed_reordered_source.value(QStringLiteral("metadata")).toMap();
  const auto typed_reordered_adjustments =
      typed_reordered_source.value(QStringLiteral("adjustments")).toMap();
  const auto typed_reordered_geometry =
      typed_reordered_source.value(QStringLiteral("geometry")).toMap();
  if (typed_reordered_source.value(QStringLiteral("modelSchemaVersion")).toInt() != 1 ||
      typed_reordered_source.value(QStringLiteral("flatIndex")).toInt() != 2 ||
      typed_reordered_metadata.value(QStringLiteral("name")).toString() !=
          QStringLiteral("Set A Linear") ||
      typed_reordered_metadata.value(QStringLiteral("maskSetId")).toString() != reorder_set_b_id ||
      typed_reordered_metadata.contains(QStringLiteral("exposure")) ||
      !almost_equal(typed_reordered_adjustments.value(QStringLiteral("exposure")).toDouble(), -0.7) ||
      !almost_equal(typed_reordered_adjustments.value(QStringLiteral("temperature")).toDouble(), -12.0) ||
      !almost_equal(typed_reordered_geometry.value(QStringLiteral("position")).toDouble(), 0.5) ||
      typed_reordered_geometry.contains(QStringLiteral("maskSetId"))) {
    return fail("typed mask source view should separate metadata, adjustments, and geometry after cross-set moves");
  }
  reorder_controller.setActiveMaskIndex(0);
  reorder_controller.setActiveMaskLocked(true);
  if (reorder_controller.moveMaskSource(0, reorder_set_b_id, 0)) {
    return fail("locked selection sources should reject drag-style reorder");
  }

  const auto mask_count_before_set_copy = controller.maskCount();
  const auto layer_count_before_set_copy = controller.photoLayerCount();
  auto original_set_source_count = 0;
  QStringList original_set_source_ids;
  for (const auto& mask_value : controller.maskStack()) {
    const auto mask = mask_value.toMap();
    if (mask.value(QStringLiteral("maskSetId")).toString() == inherited_set_id) {
      ++original_set_source_count;
      original_set_source_ids.push_back(mask.value(QStringLiteral("id")).toString());
    }
  }
  if (!controller.duplicateActiveMaskSet() ||
      controller.maskCount() != mask_count_before_set_copy + original_set_source_count ||
      controller.photoLayerCount() != layer_count_before_set_copy + 1) {
    return fail("copying a whole mask set should duplicate every source component and create its adjustment layer");
  }
  const auto copied_set_id =
      controller.maskStack().at(controller.activeMaskIndex()).toMap().value(QStringLiteral("maskSetId")).toString();
  const auto copied_set_layer = controller.photoLayers().at(0).toMap();
  if (copied_set_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("adjustment") ||
      !copied_set_layer.value(QStringLiteral("selectionScoped")).toBool() ||
      copied_set_layer.value(QStringLiteral("selectionMaskSetId")).toString() != copied_set_id ||
      !almost_equal(copied_set_layer.value(QStringLiteral("exposure")).toDouble(), 0.0) ||
      !almost_equal(copied_set_layer.value(QStringLiteral("temperature")).toDouble(), 0.0)) {
    return fail("copied mask sets should get a neutral linked adjustment layer for their new set id");
  }
  auto copied_set_source_count = 0;
  auto copied_set_sources_have_fresh_ids = true;
  auto copied_set_sources_reset_solo = true;
  for (const auto& mask_value : controller.maskStack()) {
    const auto mask = mask_value.toMap();
    if (mask.value(QStringLiteral("maskSetId")).toString() != copied_set_id) {
      continue;
    }
    ++copied_set_source_count;
    copied_set_sources_have_fresh_ids =
        copied_set_sources_have_fresh_ids &&
        !original_set_source_ids.contains(mask.value(QStringLiteral("id")).toString());
    copied_set_sources_reset_solo =
        copied_set_sources_reset_solo && !mask.value(QStringLiteral("solo")).toBool();
  }
  if (copied_set_id == inherited_set_id || copied_set_source_count != original_set_source_count ||
      !copied_set_sources_have_fresh_ids || !copied_set_sources_reset_solo) {
    return fail("copying a whole mask set should create a distinct set with fresh non-solo sources");
  }
  for (auto copied_index = 0; copied_index < copied_set_source_count; ++copied_index) {
    controller.removeActiveMask();
  }
  if (controller.maskCount() != mask_count_before_set_copy) {
    return fail("copied mask set sources should be removable as a block before brush stack tests");
  }
  controller.removeActiveMask();
  if (controller.maskCount() != 2 || controller.activeMaskIndex() != 1) {
    return fail("temporary inherited source should be removable before brush stack tests");
  }

  controller.addMask(QStringLiteral("brush"));
  if (controller.activeMaskKind() != QStringLiteral("brush")) {
    return fail("adding a brush mask should select the brush entry");
  }
  const auto built_in_brush_presets = controller.brushPresetNames();
  const QStringList required_brush_presets = {
      QStringLiteral("Soft Add"),    QStringLiteral("Hard Edge"), QStringLiteral("Hair Refine"),
      QStringLiteral("Skin Soft"),   QStringLiteral("Sky Protect"), QStringLiteral("Object Erase"),
      QStringLiteral("Clone Stamp"), QStringLiteral("Heal"),      QStringLiteral("Remove")};
  const auto all_required_presets_available =
      std::all_of(required_brush_presets.cbegin(), required_brush_presets.cend(), [&](const auto& preset) {
        return built_in_brush_presets.contains(preset);
      });
  if (!built_in_brush_presets.contains(QStringLiteral("Edge Aware")) ||
      !all_required_presets_available ||
      controller.activeMaskBrushPreset() != QStringLiteral("Soft Round")) {
    return fail("brush masks should expose built-in brush presets");
  }
  if (!controller.applyActiveMaskBrushPreset(QStringLiteral("Edge Aware")) ||
      controller.activeMaskBrushPreset() != QStringLiteral("Edge Aware") ||
      !almost_equal(controller.activeMaskBrushSize(), 0.026) ||
      !almost_equal(controller.activeMaskBrushHardness(), 0.66) ||
      !almost_equal(controller.activeMaskBrushSpacing(), 0.42) ||
      !almost_equal(controller.activeMaskBrushSmoothing(), 0.34) ||
      !controller.activeMaskBrushAutoMask() ||
      controller.activeMaskBrushProtectEdges() ||
      !almost_equal(controller.activeMaskBrushAutoMaskTolerance(), 0.14)) {
    return fail("applying a brush preset should update brush shape and auto-mask settings");
  }
  if (!controller.applyActiveMaskBrushPreset(QStringLiteral("Hair Refine")) ||
      !controller.activeMaskBrushAutoMask() ||
      !controller.activeMaskBrushProtectEdges() ||
      !almost_equal(controller.activeMaskBrushAutoMaskTolerance(), 0.10)) {
    return fail("hair refine brush preset should enable protected edge gating");
  }
  controller.setActiveMaskBrushSize(0.14);
  controller.setActiveMaskBrushFlow(0.72);
  controller.setActiveMaskBrushHardness(0.61);
  controller.setActiveMaskBrushSpacing(0.32);
  controller.setActiveMaskBrushSmoothing(0.28);
  controller.setActiveMaskBrushPressureSize(0.84);
  controller.setActiveMaskBrushPressureFlow(0.52);
  controller.setActiveMaskBrushPressureOpacity(0.36);
  controller.setActiveMaskBrushBuildup(false);
  controller.setActiveMaskBrushEraseMode(true);
  controller.setActiveMaskBrushAutoMask(true);
  controller.setActiveMaskBrushAutoMaskTolerance(0.11);
  controller.setActiveMaskBrushProtectEdges(true);
  if (!controller.activeMaskBrushAutoMask() || !controller.activeMaskBrushProtectEdges()) {
    return fail("Protect Edges should be an explicit Edge Aware brush option");
  }
  controller.setActiveMaskBrushAutoMaskContinuousSampling(true);
  const auto brush_final_x = static_cast<int>(std::lround(
      controller.activeMaskBrushSizeNormalized() * 2.0 *
      static_cast<double>(std::max(1, controller.sourceImageWidth() - 1))));
  const auto brush_final_y = static_cast<int>(std::lround(
      controller.activeMaskBrushSizeNormalized() * 2.0 *
      static_cast<double>(std::max(1, controller.sourceImageHeight() - 1))));
  const auto brush_size_summary = controller.activeMaskBrushSizeSummary();
  const auto brush_unit_contract_valid =
      almost_equal(controller.activeMaskBrushSizeNormalized(), 0.14) &&
      controller.activeMaskBrushPreviewDiameterPx() == 0 &&
      controller.activeMaskBrushFinalDiameterXPx() == 0 &&
      controller.activeMaskBrushFinalDiameterYPx() == 0 &&
      brush_size_summary.contains(QStringLiteral("Preview -- px")) &&
      brush_size_summary.contains(QStringLiteral("Stored 14% radius")) &&
      brush_size_summary.contains(QStringLiteral("Final -- px"));
  if (!brush_unit_contract_valid) {
    std::cerr << "brush unit summary=\"" << brush_size_summary.toStdString()
              << "\" normalized=" << controller.activeMaskBrushSizeNormalized()
              << " previewPx=" << controller.activeMaskBrushPreviewDiameterPx()
              << " finalPx=(" << controller.activeMaskBrushFinalDiameterXPx()
              << ',' << controller.activeMaskBrushFinalDiameterYPx()
              << ") expected=(" << std::max(1, brush_final_x)
              << ',' << std::max(1, brush_final_y) << ") source=("
              << controller.sourceImageWidth() << ','
              << controller.sourceImageHeight() << ")\n";
    return fail("brush masks should expose explicit normalized, preview-pixel, and final-pixel size units");
  }
  if (!almost_equal(controller.activeMaskBrushHardness(), 0.61) ||
      !almost_equal(controller.activeMaskBrushSpacing(), 0.32) ||
      !almost_equal(controller.activeMaskBrushSmoothing(), 0.28) ||
      !almost_equal(controller.activeMaskBrushPressureSize(), 0.84) ||
      !almost_equal(controller.activeMaskBrushPressureFlow(), 0.52) ||
      !almost_equal(controller.activeMaskBrushPressureOpacity(), 0.36) ||
      controller.activeMaskBrushBuildup() ||
      !controller.activeMaskBrushEraseMode() ||
      !controller.activeMaskBrushAutoMask() ||
      !almost_equal(controller.activeMaskBrushAutoMaskTolerance(), 0.11) ||
      !controller.activeMaskBrushAutoMaskContinuousSampling() ||
      !controller.activeMaskBrushProtectEdges() ||
      controller.activeMaskBrushPreset() != QStringLiteral("Custom")) {
    return fail("brush masks should expose editable hardness, spacing, smoothing, buildup, erase mode, auto-mask, auto-mask sampling mode, and preset controls");
  }
  if (controller.saveActiveMaskBrushPreset(QStringLiteral("Hair Refine"), true) ||
      !controller.saveActiveMaskBrushPreset(QStringLiteral("Boundary Detail"), false) ||
      !controller.isCustomBrushPreset(QStringLiteral("Boundary Detail")) ||
      !controller.brushPresetNames().contains(QStringLiteral("Boundary Detail")) ||
      controller.saveActiveMaskBrushPreset(QStringLiteral("Boundary Detail"), false)) {
    return fail("custom brush presets should save named brush settings without overwriting built-ins");
  }
  controller.setActiveMaskBrushSize(0.03);
  controller.setActiveMaskBrushFlow(0.21);
  controller.setActiveMaskBrushProtectEdges(false);
  if (!controller.applyActiveMaskBrushPreset(QStringLiteral("Boundary Detail")) ||
      controller.activeMaskBrushPreset() != QStringLiteral("Boundary Detail") ||
      !almost_equal(controller.activeMaskBrushSize(), 0.14) ||
      !almost_equal(controller.activeMaskBrushFlow(), 0.72) ||
      !controller.activeMaskBrushProtectEdges()) {
    return fail("applying a custom brush preset should restore saved brush settings");
  }
  if (!controller.deleteCustomBrushPreset(QStringLiteral("Boundary Detail")) ||
      controller.isCustomBrushPreset(QStringLiteral("Boundary Detail")) ||
      controller.brushPresetNames().contains(QStringLiteral("Boundary Detail")) ||
      controller.applyActiveMaskBrushPreset(QStringLiteral("Boundary Detail"))) {
    return fail("custom brush presets should be deletable from the brush preset list");
  }
  if (!controller.saveActiveMaskBrushPreset(QStringLiteral("Boundary Detail"), true) ||
      !controller.saveActiveMaskBrushPreset(QStringLiteral("Hair Boundary"), true) ||
      !controller.resetCustomBrushPresets() ||
      controller.isCustomBrushPreset(QStringLiteral("Boundary Detail")) ||
      controller.isCustomBrushPreset(QStringLiteral("Hair Boundary"))) {
    return fail("custom brush preset reset should clear saved custom presets");
  }
  if (!controller.applyActiveMaskBrushPreset(QStringLiteral("Object Erase")) ||
      controller.activeMaskBrushPreset() != QStringLiteral("Object Erase") ||
      !controller.activeMaskBrushEraseMode() ||
      !controller.activeMaskBrushAutoMask()) {
    return fail("object erase preset should be available and switch the active brush to erase mode");
  }
  controller.setActiveMaskBrushSize(0.14);
  controller.setActiveMaskBrushFlow(0.72);
  controller.setActiveMaskBrushHardness(0.61);
  controller.setActiveMaskBrushSpacing(0.32);
  controller.setActiveMaskBrushSmoothing(0.28);
  controller.setActiveMaskBrushPressureSize(1.0);
  controller.setActiveMaskBrushPressureFlow(0.5);
  controller.setActiveMaskBrushPressureOpacity(1.0);
  controller.setActiveMaskBrushEraseMode(true);
  controller.setActiveMaskBrushAutoMask(true);
  controller.setActiveMaskBrushAutoMaskTolerance(0.11);
  controller.setActiveMaskBrushAutoMaskContinuousSampling(false);
  controller.setActiveMaskBrushProtectEdges(true);
  controller.setActiveMaskBrushBuildup(true);
  controller.addBrushPointWithTablet(0.35, 0.44, 0.5, 0.6, -0.2);
  controller.addBrushPoint(0.41, 0.51);
  if (controller.activeMaskBrushPointCount() < 2) {
    return fail("brush masks should track painted samples");
  }
  const auto pressure_point =
      controller.maskStack().at(controller.activeMaskIndex()).toMap()
          .value(QStringLiteral("brushPoints")).toList().first().toMap();
  if (!almost_equal(pressure_point.value(QStringLiteral("pressure")).toDouble(), 0.5) ||
      !almost_equal(pressure_point.value(QStringLiteral("tiltX")).toDouble(), 0.6) ||
      !almost_equal(pressure_point.value(QStringLiteral("tiltY")).toDouble(), -0.2) ||
      !almost_equal(pressure_point.value(QStringLiteral("opacity")).toDouble(), 0.5) ||
      !pressure_point.value(QStringLiteral("erase")).toBool() ||
      pressure_point.value(QStringLiteral("size")).toDouble() >= 0.14 ||
      !almost_equal(pressure_point.value(QStringLiteral("flow")).toDouble(), 0.54)) {
    return fail("brush points should store and apply stylus pressure curves, tilt, and erase mode");
  }
  const auto pressure_stroke_settings =
      controller.maskStack().at(controller.activeMaskIndex()).toMap()
          .value(QStringLiteral("brushStrokes")).toList().first().toMap()
          .value(QStringLiteral("settings")).toMap();
  if (!almost_equal(pressure_stroke_settings.value(QStringLiteral("pressureSize")).toDouble(), 1.0) ||
      !almost_equal(pressure_stroke_settings.value(QStringLiteral("pressureFlow")).toDouble(), 0.5) ||
      !almost_equal(pressure_stroke_settings.value(QStringLiteral("pressureOpacity")).toDouble(), 1.0) ||
      !pressure_stroke_settings.value(QStringLiteral("protectEdges")).toBool() ||
      pressure_stroke_settings.value(QStringLiteral("autoMaskContinuousSampling")).toBool()) {
    return fail("brush stroke history should preserve pressure and protected-edge auto-mask settings");
  }
  controller.setActiveMaskBrushEraseMode(false);
  controller.clearActiveBrushPoints();
  if (controller.activeMaskBrushPointCount() != 0) {
    return fail("clearing brush points should reset the active brush mask");
  }
  controller.addBrushPointWithTabletEraser(0.45, 0.45, 0.64, 0.20, -0.10, true);
  const auto eraser_point =
      controller.maskStack().at(controller.activeMaskIndex()).toMap()
          .value(QStringLiteral("brushPoints")).toList().first().toMap();
  const auto eraser_stroke =
      controller.maskStack().at(controller.activeMaskIndex()).toMap()
          .value(QStringLiteral("brushStrokes")).toList().first().toMap();
  if (!eraser_point.value(QStringLiteral("erase")).toBool() ||
      eraser_stroke.value(QStringLiteral("operation")).toString() != QStringLiteral("erase") ||
      eraser_stroke.value(QStringLiteral("samples")).toList().first().toMap().value(QStringLiteral("erase")).toBool() != true ||
      !almost_equal(eraser_point.value(QStringLiteral("pressure")).toDouble(), 0.64) ||
      !almost_equal(eraser_point.value(QStringLiteral("tiltX")).toDouble(), 0.20) ||
      !almost_equal(eraser_point.value(QStringLiteral("tiltY")).toDouble(), -0.10)) {
    return fail("tablet eraser strokes should preserve pressure, tilt, and explicit erase intent");
  }
  controller.clearActiveBrushPoints();
  controller.setActiveMaskBrushSize(0.06);
  controller.setActiveMaskBrushSpacing(0.10);
  controller.addBrushPoint(0.10, 0.20);
  controller.addBrushPoint(0.90, 0.20);
  const auto dense_spacing_point_count = controller.activeMaskBrushPointCount();
  controller.clearActiveBrushPoints();
  controller.setActiveMaskBrushSpacing(2.0);
  controller.addBrushPoint(0.10, 0.20);
  controller.addBrushPoint(0.90, 0.20);
  if (dense_spacing_point_count <= controller.activeMaskBrushPointCount()) {
    return fail("lower brush spacing should sample more brush dabs along a stroke");
  }
  controller.clearActiveBrushPoints();
  controller.setActiveMaskBrushSpacing(2.0);
  controller.setActiveMaskBrushSmoothing(1.0);
  controller.beginActiveBrushStroke();
  controller.addBrushPoint(0.10, 0.40);
  controller.addBrushPoint(0.90, 0.40);
  const auto smoothed_live_points = controller.activeMaskLiveBrushPoints();
  if (controller.activeMaskLiveBrushPreviewBackend() != QStringLiteral("shared-mask-raster") &&
      controller.activeMaskLiveBrushPreviewBackend() != QStringLiteral("metal") &&
      controller.activeMaskLiveBrushPreviewBackend() != QStringLiteral("cpu")) {
    return fail("live brush preview should report the shared mask raster path or an active compositor backend");
  }
  if (controller.activeMaskLiveBrushPreviewElapsedMs() < 0 ||
      !controller.activeMaskLiveBrushPreviewWithinLatencyBudget()) {
    return fail("live brush preview should measure and meet the live-stroke latency budget");
  }
  if (controller.activeMaskLiveBrushPreviewBackend() == QStringLiteral("metal")) {
    if (controller.activeMaskLiveBrushPreviewFallbackReason().contains(QStringLiteral("CPU image fallback"))) {
      return fail("Metal live brush preview should not report a CPU fallback reason");
    }
  } else if (controller.activeMaskLiveBrushPreviewBackend() == QStringLiteral("shared-mask-raster")) {
    if (!controller.activeMaskLiveBrushPreviewFallbackReason().contains(
            QStringLiteral("shared-brush-raster-live-preview")) ||
        controller.activeMaskLiveBrushPreviewSource().isEmpty() ||
        controller.activeMaskLiveBrushPreviewNativeAvailable()) {
      return fail("shared live brush preview should publish through the same mask raster path as committed brush mattes");
    }
  } else if (!controller.activeMaskLiveBrushPreviewFallbackReason().contains(QStringLiteral("CPU image fallback")) &&
             !controller.activeMaskLiveBrushPreviewFallbackReason().contains(QStringLiteral("native-live-preview")) &&
             controller.activeMaskLiveBrushPreviewFallbackReason().isEmpty()) {
    return fail("non-native live brush preview should expose the CPU image fallback path");
  }
  if (smoothed_live_points.size() < 4) {
    return fail("stroke smoothing should keep damped intermediate samples before the live endpoint");
  }
  const auto smoothed_intermediate_point =
      smoothed_live_points.at(smoothed_live_points.size() - 2).toMap();
  const auto smoothed_live_endpoint = smoothed_live_points.last().toMap();
  if (!(smoothed_intermediate_point.value(QStringLiteral("x")).toDouble() < 0.50) ||
      !almost_equal(smoothed_live_endpoint.value(QStringLiteral("x")).toDouble(), 0.90) ||
      !almost_equal(smoothed_live_endpoint.value(QStringLiteral("y")).toDouble(), 0.40)) {
    return fail("stroke smoothing should damp intermediate samples without lagging behind the cursor");
  }
  const auto smoothed_committed_count_before_release =
      controller.maskStack().at(controller.activeMaskIndex()).toMap()
          .value(QStringLiteral("brushPoints"))
          .toList()
          .size();
  controller.endActiveBrushStroke();
  if (controller.brushStrokeActive() ||
      controller.activeMaskLiveBrushPoints().isEmpty() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::pending_brush_commit_count(controller) <= 0 ||
      controller.maskStack().at(controller.activeMaskIndex()).toMap()
              .value(QStringLiteral("brushPoints"))
              .toList()
              .size() != smoothed_committed_count_before_release) {
    return fail("ending a brush stroke should keep pending visual feedback and defer the committed raster work");
  }
  if (!wait_for_brush_commit(controller)) {
    return fail("deferred brush stroke commit should finish on the event loop");
  }
  const auto brush_undo_label_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  const auto expected_brush_undo_label =
      QStringLiteral("%1 stroke")
          .arg(brush_undo_label_mask.value(QStringLiteral("name")).toString().trimmed());
  if (expected_brush_undo_label.trimmed() == QStringLiteral("stroke") ||
      controller.undoLabel() != expected_brush_undo_label) {
    return fail("committed brush strokes should expose a mask-specific undo label");
  }
  if (!controller.activeMaskLiveBrushPoints().isEmpty()) {
    return fail("deferred brush stroke commit should clear pending visual feedback after publishing");
  }
  const auto deferred_brush_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(controller);
  const auto traced_brush_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  if (!deferred_brush_telemetry.value(QStringLiteral("commit_deferred")).toBool() ||
      !deferred_brush_telemetry.value(QStringLiteral("async_worker")).toBool() ||
      deferred_brush_telemetry.value(QStringLiteral("raster_worker_thread")).toString().isEmpty() ||
      deferred_brush_telemetry.value(QStringLiteral("mask_source_id")).toString() !=
          traced_brush_mask.value(QStringLiteral("id")).toString() ||
      deferred_brush_telemetry.value(QStringLiteral("mask_set_id")).toString() !=
          traced_brush_mask.value(QStringLiteral("maskSetId")).toString() ||
      !deferred_brush_telemetry.value(QStringLiteral("mask_source_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !deferred_brush_telemetry.value(QStringLiteral("mask_alpha_trace_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !deferred_brush_telemetry.value(QStringLiteral("mask_alpha_trace_signature"))
           .toString()
           .contains(traced_brush_mask.value(QStringLiteral("id")).toString()) ||
      deferred_brush_telemetry.value(QStringLiteral("mask_source_point_count")).toInt() <= 0 ||
      deferred_brush_telemetry.value(QStringLiteral("brushAlphaSourceContract")).toString() !=
          QStringLiteral("brush-strokes-plus-alpha-tile-cache-v1") ||
      deferred_brush_telemetry.value(QStringLiteral("brushAlphaSourceOfTruth")).toString() !=
          QStringLiteral("brushStrokes") ||
      deferred_brush_telemetry.value(QStringLiteral("brushAlphaTileCacheKind")).toString() !=
          QStringLiteral("mask-alpha-tile-cache") ||
      !deferred_brush_telemetry.value(QStringLiteral("brushAlphaTileCacheEnabled")).toBool() ||
      !deferred_brush_telemetry.value(QStringLiteral("brushAlphaSourceSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:"))) {
    return fail("deferred brush stroke commits should expose queued async worker and mask alpha trace telemetry");
  }
  const auto smoothed_committed_endpoint =
      controller.maskStack().at(controller.activeMaskIndex()).toMap()
          .value(QStringLiteral("brushPoints")).toList().last().toMap();
  if (!almost_equal(smoothed_committed_endpoint.value(QStringLiteral("x")).toDouble(), 0.90) ||
      !almost_equal(smoothed_committed_endpoint.value(QStringLiteral("y")).toDouble(), 0.40)) {
    return fail("ending a smoothed stroke should commit the same cursor endpoint shown live");
  }
  const auto smoothed_penultimate_x =
      smoothed_intermediate_point.value(QStringLiteral("x")).toDouble();
  controller.clearActiveBrushPoints();
  controller.setActiveMaskBrushSpacing(2.0);
  controller.setActiveMaskBrushSmoothing(0.0);
  controller.beginActiveBrushStroke();
  controller.addBrushPoint(0.10, 0.40);
  controller.addBrushPoint(0.90, 0.40);
  const auto unsmoothed_live_points = controller.activeMaskLiveBrushPoints();
  (void)finish_brush_stroke(controller);
  if (unsmoothed_live_points.size() <= smoothed_live_points.size() ||
      unsmoothed_live_points.at(unsmoothed_live_points.size() - 2)
              .toMap()
              .value(QStringLiteral("x"))
              .toDouble() <= smoothed_penultimate_x ||
      !almost_equal(
          unsmoothed_live_points.last().toMap().value(QStringLiteral("x")).toDouble(),
          0.90)) {
    return fail("setting brush smoothing to zero should disable damping and preserve the direct sampled path");
  }
  controller.clearActiveBrushPoints();

  controller.setActiveMaskBrushSize(0.08);
  controller.setActiveMaskBrushFlow(0.72);
  controller.setActiveMaskBrushSpacing(0.34);
  controller.setActiveMaskBrushSmoothing(0.26);
  controller.setActiveMaskBrushEraseMode(false);
  controller.setActiveMaskBrushAutoMask(false);
  const auto deterministic_pressure_stroke = [&controller, &finish_brush_stroke]() {
    controller.clearActiveBrushPoints();
    controller.beginActiveBrushStroke();
    controller.addBrushPointWithTablet(0.18, 0.30, 0.42, 0.10, -0.15);
    controller.addBrushPointWithTablet(0.46, 0.52, 0.78, 0.20, -0.05);
    controller.addBrushPointWithTablet(0.74, 0.68, 0.55, -0.10, 0.12);
    (void)finish_brush_stroke(controller);
    return controller.maskStack().at(controller.activeMaskIndex()).toMap();
  };
  const auto deterministic_stroke_a = deterministic_pressure_stroke();
  const auto deterministic_stroke_b = deterministic_pressure_stroke();
  if (deterministic_stroke_a.value(QStringLiteral("brushPoints")).toList() !=
          deterministic_stroke_b.value(QStringLiteral("brushPoints")).toList() ||
      deterministic_stroke_a.value(QStringLiteral("segmentationData")).toByteArray() !=
          deterministic_stroke_b.value(QStringLiteral("segmentationData")).toByteArray() ||
      deterministic_stroke_a.value(QStringLiteral("segmentationWidth")).toInt() !=
          deterministic_stroke_b.value(QStringLiteral("segmentationWidth")).toInt() ||
      deterministic_stroke_a.value(QStringLiteral("segmentationHeight")).toInt() !=
          deterministic_stroke_b.value(QStringLiteral("segmentationHeight")).toInt()) {
    return fail("replaying the same brush path and pressure samples should produce identical samples and raster matte bytes");
  }
  controller.clearActiveBrushPoints();

  controller.setActiveMaskBrushSize(0.06);
  controller.setActiveMaskBrushFlow(0.72);
  controller.setActiveMaskBrushSpacing(0.50);
  controller.setActiveMaskBrushSmoothing(0.0);
  const auto brush_point_coordinate_signature = [](const QVariantMap& mask) {
    QStringList parts;
    const auto points = mask.value(QStringLiteral("brushPoints")).toList();
    parts.reserve(points.size());
    for (const auto& point_value : points) {
      const auto point = point_value.toMap();
      parts.push_back(QStringLiteral("%1,%2,%3,%4")
                          .arg(static_cast<int>(
                              std::llround(point.value(QStringLiteral("x")).toDouble() * 1000000.0)))
                          .arg(static_cast<int>(
                              std::llround(point.value(QStringLiteral("y")).toDouble() * 1000000.0)))
                          .arg(static_cast<int>(std::llround(
                              point.value(QStringLiteral("size")).toDouble() * 1000000.0)))
                          .arg(static_cast<int>(std::llround(
                              point.value(QStringLiteral("flow")).toDouble() * 1000000.0))));
    }
    return parts.join(QStringLiteral("|"));
  };
  const auto paint_event_rate_stroke = [&controller, &finish_brush_stroke](
                                           const QVector<QPointF>& events) {
    controller.clearActiveBrushPoints();
    controller.beginActiveBrushStroke();
    for (const auto& event : events) {
      controller.addBrushPoint(event.x(), event.y());
    }
    (void)finish_brush_stroke(controller);
    return controller.maskStack().at(controller.activeMaskIndex()).toMap();
  };
  const auto coarse_event_rate_stroke =
      paint_event_rate_stroke({QPointF(0.10, 0.62), QPointF(0.90, 0.62)});
  const auto dense_event_rate_stroke = paint_event_rate_stroke(
      {QPointF(0.10, 0.62), QPointF(0.30, 0.62), QPointF(0.50, 0.62), QPointF(0.70, 0.62),
       QPointF(0.90, 0.62)});
  if (brush_point_coordinate_signature(coarse_event_rate_stroke) !=
          brush_point_coordinate_signature(dense_event_rate_stroke) ||
      coarse_event_rate_stroke.value(QStringLiteral("segmentationData")).toByteArray() !=
          dense_event_rate_stroke.value(QStringLiteral("segmentationData")).toByteArray() ||
      coarse_event_rate_stroke.value(QStringLiteral("brushPoints")).toList().size() <= 2) {
    return fail("committed brush stroke interpolation should be stable across input event rates");
  }
  controller.clearActiveBrushPoints();

  controller.setActiveMaskBrushSize(0.35);
  controller.setActiveMaskBrushSpacing(2.0);
  controller.setActiveMaskBrushSmoothing(0.0);
  const auto maximum_brush_gap = [](const QVariantMap& mask) {
    auto maximum_gap = 0.0;
    const auto points = mask.value(QStringLiteral("brushPoints")).toList();
    for (auto index = 1; index < points.size(); ++index) {
      const auto previous = points.at(index - 1).toMap();
      const auto current = points.at(index).toMap();
      maximum_gap = std::max(
          maximum_gap,
          std::hypot(
              current.value(QStringLiteral("x")).toDouble() -
                  previous.value(QStringLiteral("x")).toDouble(),
              current.value(QStringLiteral("y")).toDouble() -
                  previous.value(QStringLiteral("y")).toDouble()));
    }
    return maximum_gap;
  };
  const auto high_spacing_fast_stroke =
      paint_event_rate_stroke({QPointF(0.05, 0.72), QPointF(0.95, 0.72)});
  if (maximum_brush_gap(high_spacing_fast_stroke) > 0.050001) {
    return fail("fast brush strokes should cap committed dab spacing to avoid visible gaps");
  }
  controller.clearActiveBrushPoints();

  QImage auto_mask_source(32, 8, QImage::Format_ARGB32);
  for (int y = 0; y < auto_mask_source.height(); ++y) {
    for (int x = 0; x < auto_mask_source.width(); ++x) {
      auto_mask_source.setPixelColor(
          x,
          y,
          x < auto_mask_source.width() / 2 ? QColor::fromRgb(28, 28, 28)
                                           : QColor::fromRgb(235, 235, 235));
    }
  }
  const auto auto_mask_path = runtime_root.filePath(QStringLiteral("brush-auto-mask.png"));
  if (!auto_mask_source.save(auto_mask_path)) {
    return fail("auto-mask source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController auto_mask_controller;
  auto_mask_controller.loadAsset(
      QStringLiteral("brush-auto-mask"),
      auto_mask_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  auto_mask_controller.addMask(QStringLiteral("brush"));
  auto_mask_controller.setActiveMaskBrushSize(0.45);
  auto_mask_controller.setActiveMaskBrushFlow(1.0);
  auto_mask_controller.setActiveMaskBrushSpacing(0.20);
  auto_mask_controller.setActiveMaskBrushSmoothing(0.0);
  auto_mask_controller.setActiveMaskBrushAutoMask(true);
  auto_mask_controller.setActiveMaskBrushAutoMaskTolerance(0.05);
  if (auto_mask_controller.activeMaskBrushAutoMaskContinuousSampling()) {
    return fail("brush auto-mask should default to first-dab sampling");
  }
  auto_mask_controller.beginActiveBrushStroke();
  auto_mask_controller.addBrushPoint(0.35, 0.5);
  auto_mask_controller.addBrushPoint(0.70, 0.5);
  (void)finish_brush_stroke(auto_mask_controller);
  const auto auto_mask = auto_mask_controller.maskStack().at(auto_mask_controller.activeMaskIndex()).toMap();
  const auto anchored_auto_mask_points = auto_mask.value(QStringLiteral("brushPoints")).toList();
  if (anchored_auto_mask_points.size() < 2) {
    return fail("anchored brush auto-mask stroke should commit multiple dab samples");
  }
  const auto anchored_anchor_x =
      anchored_auto_mask_points.first().toMap().value(QStringLiteral("autoMaskAnchorX")).toDouble();
  const auto anchored_anchor_y =
      anchored_auto_mask_points.first().toMap().value(QStringLiteral("autoMaskAnchorY")).toDouble();
  for (const auto& point_value : anchored_auto_mask_points) {
    const auto point = point_value.toMap();
    if (!almost_equal(point.value(QStringLiteral("autoMaskAnchorX")).toDouble(), anchored_anchor_x) ||
        !almost_equal(point.value(QStringLiteral("autoMaskAnchorY")).toDouble(), anchored_anchor_y)) {
      return fail("first-dab brush auto-mask should keep a stable color sample anchor across the stroke");
    }
  }
  const auto auto_mask_data = auto_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto auto_mask_width = auto_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto auto_mask_height = auto_mask.value(QStringLiteral("segmentationHeight")).toInt();
  const auto anchored_auto_mask_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          auto_mask_controller);
  if (mask_value_at(auto_mask_data, auto_mask_width, auto_mask_height, 0.35, 0.5) < 128 ||
      mask_value_at(auto_mask_data, auto_mask_width, auto_mask_height, 0.70, 0.5) > 48) {
    return fail("first-dab brush auto-mask should stop painted coverage at a strong image edge");
  }
  if (anchored_auto_mask_telemetry.value(QStringLiteral("auto_mask_candidate_pixels")).toLongLong() <=
          anchored_auto_mask_telemetry.value(QStringLiteral("auto_mask_edge_evaluation_pixels")).toLongLong() ||
      anchored_auto_mask_telemetry.value(QStringLiteral("auto_mask_edge_evaluation_pixels")).toLongLong() <=
          0) {
    return fail("brush auto-mask should skip expensive edge-barrier evaluation for pixels rejected by color tolerance");
  }

  QImage bounded_auto_mask_source(2400, 1200, QImage::Format_ARGB32);
  for (int y = 0; y < bounded_auto_mask_source.height(); ++y) {
    for (int x = 0; x < bounded_auto_mask_source.width(); ++x) {
      bounded_auto_mask_source.setPixelColor(
          x,
          y,
          x < bounded_auto_mask_source.width() / 2 ? QColor::fromRgb(30, 34, 38)
                                                   : QColor::fromRgb(226, 231, 236));
    }
  }
  const auto bounded_auto_mask_path =
      runtime_root.filePath(QStringLiteral("brush-auto-mask-bounded-decode.png"));
  if (!bounded_auto_mask_source.save(bounded_auto_mask_path)) {
    return fail("bounded auto-mask source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController bounded_auto_mask_controller;
  bounded_auto_mask_controller.loadAsset(
      QStringLiteral("brush-auto-mask-bounded-decode"),
      bounded_auto_mask_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  bounded_auto_mask_controller.addMask(QStringLiteral("brush"));
  bounded_auto_mask_controller.setActiveMaskBrushSize(0.04);
  bounded_auto_mask_controller.setActiveMaskBrushFlow(1.0);
  bounded_auto_mask_controller.setActiveMaskBrushSpacing(0.20);
  bounded_auto_mask_controller.setActiveMaskBrushSmoothing(0.0);
  bounded_auto_mask_controller.setActiveMaskBrushAutoMask(true);
  bounded_auto_mask_controller.setActiveMaskBrushAutoMaskTolerance(0.05);
  bounded_auto_mask_controller.beginActiveBrushStroke();
  bounded_auto_mask_controller.addBrushPoint(0.22, 0.50);
  bounded_auto_mask_controller.addBrushPoint(0.30, 0.50);
  if (!finish_brush_stroke(bounded_auto_mask_controller)) {
    return fail("bounded auto-mask brush commit should finish");
  }
  const auto bounded_auto_mask =
      bounded_auto_mask_controller.maskStack()
          .at(bounded_auto_mask_controller.activeMaskIndex())
          .toMap();
  const auto bounded_auto_mask_data =
      bounded_auto_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto bounded_auto_mask_width =
      bounded_auto_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto bounded_auto_mask_height =
      bounded_auto_mask.value(QStringLiteral("segmentationHeight")).toInt();
  const auto bounded_auto_mask_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          bounded_auto_mask_controller);
  if (bounded_auto_mask_telemetry.value(QStringLiteral("source_decode_edge")).toInt() >= 1024 ||
      !bounded_auto_mask_telemetry.value(QStringLiteral("source_decode_bounded")).toBool() ||
      std::max(bounded_auto_mask_width, bounded_auto_mask_height) >= 1024 ||
      mask_value_at(
          bounded_auto_mask_data,
          bounded_auto_mask_width,
          bounded_auto_mask_height,
          0.24,
          0.50) < 128 ||
      mask_value_at(
          bounded_auto_mask_data,
          bounded_auto_mask_width,
          bounded_auto_mask_height,
          0.75,
          0.50) > 24) {
    return fail("small brush auto-mask commits should use a bounded source decode without losing edge-aware mask coverage");
  }
  bounded_auto_mask_controller.beginActiveBrushStroke();
  bounded_auto_mask_controller.addBrushPoint(0.34, 0.50);
  if (!finish_brush_stroke(bounded_auto_mask_controller)) {
    return fail("bounded auto-mask incremental brush commit should finish");
  }
  const auto bounded_incremental_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          bounded_auto_mask_controller);
  if (!bounded_incremental_telemetry.value(QStringLiteral("incremental")).toBool() ||
      bounded_incremental_telemetry.value(QStringLiteral("source_decode_edge")).toInt() !=
          std::max(bounded_auto_mask_width, bounded_auto_mask_height)) {
    return fail("bounded auto-mask follow-up strokes should keep the established raster size and stay incremental");
  }

  auto_mask_controller.clearActiveBrushPoints();
  auto_mask_controller.setActiveMaskBrushAutoMaskContinuousSampling(true);
  auto_mask_controller.beginActiveBrushStroke();
  auto_mask_controller.addBrushPoint(0.35, 0.5);
  auto_mask_controller.addBrushPoint(0.70, 0.5);
  (void)finish_brush_stroke(auto_mask_controller);
  const auto continuous_auto_mask =
      auto_mask_controller.maskStack().at(auto_mask_controller.activeMaskIndex()).toMap();
  const auto continuous_points =
      continuous_auto_mask.value(QStringLiteral("brushPoints")).toList();
  const auto continuous_first_point = continuous_points.first().toMap();
  const auto continuous_last_point = continuous_points.last().toMap();
  if (!auto_mask_controller.activeMaskBrushAutoMaskContinuousSampling() ||
      !almost_equal(
          continuous_first_point.value(QStringLiteral("autoMaskAnchorX")).toDouble(),
          continuous_first_point.value(QStringLiteral("x")).toDouble()) ||
      !almost_equal(
          continuous_last_point.value(QStringLiteral("autoMaskAnchorX")).toDouble(),
          continuous_last_point.value(QStringLiteral("x")).toDouble())) {
    return fail("continuous brush auto-mask should sample from each dab center");
  }
  const auto continuous_data =
      continuous_auto_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto continuous_width =
      continuous_auto_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto continuous_height =
      continuous_auto_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(continuous_data, continuous_width, continuous_height, 0.70, 0.5) < 128) {
    return fail("continuous brush auto-mask should allow the stroke to pick up a new center sample");
  }

  QImage subtle_edge_source(32, 8, QImage::Format_ARGB32);
  for (int y = 0; y < subtle_edge_source.height(); ++y) {
    for (int x = 0; x < subtle_edge_source.width(); ++x) {
      const auto value = x < subtle_edge_source.width() / 2 ? 100 : 150;
      subtle_edge_source.setPixelColor(x, y, QColor::fromRgb(value, value, value));
    }
  }
  const auto subtle_edge_path = runtime_root.filePath(QStringLiteral("brush-auto-mask-subtle-edge.png"));
  if (!subtle_edge_source.save(subtle_edge_path)) {
    return fail("subtle auto-mask source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController subtle_edge_controller;
  subtle_edge_controller.loadAsset(
      QStringLiteral("brush-auto-mask-subtle-edge"),
      subtle_edge_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  subtle_edge_controller.addMask(QStringLiteral("brush"));
  subtle_edge_controller.setActiveMaskBrushSize(0.45);
  subtle_edge_controller.setActiveMaskBrushFlow(1.0);
  subtle_edge_controller.setActiveMaskBrushSpacing(0.20);
  subtle_edge_controller.setActiveMaskBrushSmoothing(0.0);
  subtle_edge_controller.setActiveMaskBrushAutoMask(true);
  subtle_edge_controller.setActiveMaskBrushAutoMaskTolerance(0.25);
  subtle_edge_controller.beginActiveBrushStroke();
  subtle_edge_controller.addBrushPoint(0.35, 0.5);
  const auto uncommitted_preview_mask =
      subtle_edge_controller.maskStack().at(subtle_edge_controller.activeMaskIndex()).toMap();
  if (!uncommitted_preview_mask.value(QStringLiteral("brushPoints")).toList().isEmpty()) {
    return fail("brush auto-mask live preview should not commit brush points before stroke end");
  }
  if (subtle_edge_controller.activeMaskLiveBrushPoints().isEmpty()) {
    return fail("brush auto-mask live preview should expose uncommitted live points");
  }
  if (subtle_edge_controller.activeMaskLiveBrushPreviewBackend() != QStringLiteral("cpu-auto-mask")) {
    return fail("brush auto-mask live preview should use the tolerance-filtered CPU rasterizer");
  }
  if (subtle_edge_controller.activeMaskLiveBrushPreviewNativeAvailable()) {
    return fail("brush auto-mask live preview should route through the image fallback, not native compositor");
  }
  const auto preview_revision_before_tolerance_drag =
      subtle_edge_controller.activeMaskLiveBrushPreviewRevision();
  subtle_edge_controller.setActiveMaskBrushAutoMaskTolerance(0.32);
  if (subtle_edge_controller.activeMaskLiveBrushPreviewRevision() <=
          preview_revision_before_tolerance_drag ||
      subtle_edge_controller.activeMaskLiveBrushPreviewBackend() != QStringLiteral("cpu-auto-mask")) {
    return fail("brush auto-mask tolerance changes should rebuild the uncommitted live preview");
  }
  subtle_edge_controller.setActiveMaskBrushAutoMaskTolerance(0.25);
  const auto preview_revision_before_protect_edges =
      subtle_edge_controller.activeMaskLiveBrushPreviewRevision();
  subtle_edge_controller.setActiveMaskBrushProtectEdges(true);
  if (!subtle_edge_controller.activeMaskBrushAutoMask() ||
      !subtle_edge_controller.activeMaskBrushProtectEdges() ||
      subtle_edge_controller.activeMaskLiveBrushPreviewRevision() <=
          preview_revision_before_protect_edges) {
    return fail("Protect Edges should enable Edge Aware and rebuild the uncommitted live preview");
  }
  subtle_edge_controller.addBrushPoint(0.70, 0.5);
  (void)finish_brush_stroke(subtle_edge_controller);
  const auto subtle_edge_mask =
      subtle_edge_controller.maskStack().at(subtle_edge_controller.activeMaskIndex()).toMap();
  const auto subtle_edge_data = subtle_edge_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto subtle_edge_width = subtle_edge_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto subtle_edge_height = subtle_edge_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(subtle_edge_data, subtle_edge_width, subtle_edge_height, 0.35, 0.5) < 128 ||
      mask_value_at(subtle_edge_data, subtle_edge_width, subtle_edge_height, 0.70, 0.5) > 80) {
    return fail("brush auto-mask should use perceptual luminance and local edge gating on subtle hard edges");
  }
  subtle_edge_controller.clearActiveBrushPoints();
  subtle_edge_controller.setActiveMaskBrushAutoMaskContinuousSampling(true);
  subtle_edge_controller.beginActiveBrushStroke();
  subtle_edge_controller.addBrushPoint(0.35, 0.5);
  subtle_edge_controller.addBrushPoint(0.70, 0.5);
  (void)finish_brush_stroke(subtle_edge_controller);
  const auto subtle_continuous_mask =
      subtle_edge_controller.maskStack().at(subtle_edge_controller.activeMaskIndex()).toMap();
  if (mask_value_at(
          subtle_continuous_mask.value(QStringLiteral("segmentationData")).toByteArray(),
          subtle_continuous_mask.value(QStringLiteral("segmentationWidth")).toInt(),
          subtle_continuous_mask.value(QStringLiteral("segmentationHeight")).toInt(),
          0.70,
          0.5) < 128) {
    return fail("continuous brush auto-mask sampling should still permit painting across a new sampled side of a subtle edge");
  }

  QImage weak_edge_source(32, 8, QImage::Format_ARGB32);
  for (int y = 0; y < weak_edge_source.height(); ++y) {
    for (int x = 0; x < weak_edge_source.width(); ++x) {
      const auto value = x < weak_edge_source.width() / 2 ? 100 : 116;
      weak_edge_source.setPixelColor(x, y, QColor::fromRgb(value, value, value));
    }
  }
  const auto weak_edge_path = runtime_root.filePath(QStringLiteral("brush-protect-edges-weak-edge.png"));
  if (!weak_edge_source.save(weak_edge_path)) {
    return fail("weak-edge auto-mask source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController weak_edge_controller;
  weak_edge_controller.loadAsset(
      QStringLiteral("brush-protect-edges-weak-edge"),
      weak_edge_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  weak_edge_controller.addMask(QStringLiteral("brush"));
  weak_edge_controller.setActiveMaskBrushSize(0.45);
  weak_edge_controller.setActiveMaskBrushFlow(1.0);
  weak_edge_controller.setActiveMaskBrushSpacing(0.20);
  weak_edge_controller.setActiveMaskBrushSmoothing(0.0);
  weak_edge_controller.setActiveMaskBrushAutoMask(true);
  weak_edge_controller.setActiveMaskBrushAutoMaskTolerance(0.30);
  weak_edge_controller.beginActiveBrushStroke();
  weak_edge_controller.addBrushPoint(0.35, 0.5);
  weak_edge_controller.addBrushPoint(0.70, 0.5);
  (void)finish_brush_stroke(weak_edge_controller);
  const auto weak_edge_mask =
      weak_edge_controller.maskStack().at(weak_edge_controller.activeMaskIndex()).toMap();
  if (mask_value_at(
          weak_edge_mask.value(QStringLiteral("segmentationData")).toByteArray(),
          weak_edge_mask.value(QStringLiteral("segmentationWidth")).toInt(),
          weak_edge_mask.value(QStringLiteral("segmentationHeight")).toInt(),
          0.70,
          0.5) < 128) {
    return fail("broad Edge Aware tolerance should still show why Protect Edges is a distinct option");
  }
  weak_edge_controller.clearActiveBrushPoints();
  weak_edge_controller.setActiveMaskBrushProtectEdges(true);
  weak_edge_controller.beginActiveBrushStroke();
  weak_edge_controller.addBrushPoint(0.35, 0.5);
  weak_edge_controller.addBrushPoint(0.70, 0.5);
  (void)finish_brush_stroke(weak_edge_controller);
  const auto protected_weak_edge_mask =
      weak_edge_controller.maskStack().at(weak_edge_controller.activeMaskIndex()).toMap();
  if (mask_value_at(
          protected_weak_edge_mask.value(QStringLiteral("segmentationData")).toByteArray(),
          protected_weak_edge_mask.value(QStringLiteral("segmentationWidth")).toInt(),
          protected_weak_edge_mask.value(QStringLiteral("segmentationHeight")).toInt(),
          0.70,
          0.5) > 96) {
    return fail("Protect Edges should tighten weak hair/sky/building boundary gating");
  }

  controller.addMask(QStringLiteral("luminance"));
  controller.setActiveMaskLuminanceMin(0.64);
  controller.setActiveMaskLuminanceMax(0.91);
  controller.setActiveMaskRangeFeather(0.06);
  if (!almost_equal(controller.activeMaskLuminanceMin(), 0.64) ||
      !almost_equal(controller.activeMaskLuminanceMax(), 0.91) ||
      !almost_equal(controller.activeMaskRangeFeather(), 0.06)) {
    return fail("luminance range masks should expose editable constraint controls");
  }

  QImage luminance_range_source(10, 10, QImage::Format_ARGB32);
  for (int y = 0; y < luminance_range_source.height(); ++y) {
    for (int x = 0; x < luminance_range_source.width(); ++x) {
      const auto value = static_cast<int>(std::lround((static_cast<double>(x) / 9.0) * 255.0));
      luminance_range_source.setPixelColor(x, y, QColor(value, value, value, 255));
    }
  }
  const auto luminance_range_path = runtime_root.filePath(QStringLiteral("luminance-range-source.png"));
  if (!luminance_range_source.save(luminance_range_path)) {
    return fail("luminance range source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController luminance_range_controller;
  luminance_range_controller.loadAsset(
      QStringLiteral("luminance-range-source"),
      luminance_range_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  luminance_range_controller.addMask(QStringLiteral("luminance"));
  if (!luminance_range_controller.beginActiveLuminanceRangeSample(0.18, 0.20) ||
      !luminance_range_controller.updateActiveLuminanceRangeSample(0.42, 0.60)) {
    return fail("luminance range sampling should expose a drag preview lifecycle");
  }
  const auto luminance_preview = luminance_range_controller.activeMaskLuminanceSamplePreview();
  if (!luminance_preview.value(QStringLiteral("active")).toBool() ||
      luminance_preview.value(QStringLiteral("width")).toDouble() <= 0.20 ||
      luminance_preview.value(QStringLiteral("height")).toDouble() <= 0.30) {
    return fail("luminance range sampling should publish the dragged patch preview bounds");
  }
  if (!luminance_range_controller.commitActiveLuminanceRangeSample(0.42, 0.60) ||
      !luminance_range_controller.activeMaskLuminanceSamplePreview().isEmpty()) {
    return fail("luminance range sampling should commit and clear the transient preview");
  }
  auto luminance_samples = luminance_range_controller.activeMaskLuminanceSamples();
  if (luminance_samples.size() != 1 ||
      luminance_range_controller.activeMaskLuminanceMin() > 0.20 ||
      luminance_range_controller.activeMaskLuminanceMax() < 0.42 ||
      luminance_samples.first().toMap().value(QStringLiteral("pixelCount")).toInt() <= 0) {
    return fail("luminance range sampling should measure and store the dragged patch constraints");
  }
  for (int index = 0; index < 5; ++index) {
    const auto start = 0.04 * static_cast<double>(index);
    if (!luminance_range_controller.beginActiveLuminanceRangeSample(start, 0.10) ||
        !luminance_range_controller.commitActiveLuminanceRangeSample(start + 0.02, 0.18)) {
      return fail("luminance range sampling should support repeated patch samples");
    }
  }
  luminance_samples = luminance_range_controller.activeMaskLuminanceSamples();
  if (luminance_samples.size() != 5) {
    return fail("luminance range sampling should retain the five most recent patches");
  }
  if (!luminance_range_controller.removeActiveLuminanceRangeSample(1) ||
      luminance_range_controller.activeMaskLuminanceSamples().size() != 4 ||
      luminance_range_controller.removeActiveLuminanceRangeSample(99)) {
    return fail("luminance range sampling should allow valid sample removal and reject invalid removal");
  }

  controller.addMask(QStringLiteral("color"));
  controller.setActiveMaskTemperature(14.0);
  controller.setActiveMaskTint(-11.0);
  controller.setActiveMaskVibrance(26.0);
  controller.setActiveMaskSaturation(-9.0);
  controller.setActiveMaskHueCenter(0.12);
  controller.setActiveMaskHueRange(0.18);
  controller.setActiveMaskSaturationMin(0.24);
  controller.setActiveMaskSaturationMax(0.77);
  controller.setActiveMaskRemoveStrength(63.0);
  if (!almost_equal(controller.activeMaskTemperature(), 14.0) ||
      !almost_equal(controller.activeMaskTint(), -11.0) ||
      !almost_equal(controller.activeMaskVibrance(), 26.0) ||
      !almost_equal(controller.activeMaskSaturation(), -9.0)) {
    return fail("selection masks should expose local color controls");
  }
  if (!almost_equal(controller.activeMaskHueCenter(), 0.12) ||
      !almost_equal(controller.activeMaskHueRange(), 0.18) ||
      !almost_equal(controller.activeMaskSaturationMin(), 0.24) ||
      !almost_equal(controller.activeMaskSaturationMax(), 0.77) ||
      !almost_equal(controller.activeMaskRemoveStrength(), 63.0)) {
    return fail("color range masks should expose editable hue and saturation constraints");
  }

  controller.addMask(QStringLiteral("depth"));
  controller.setActiveMaskDepthMin(0.28);
  controller.setActiveMaskDepthMax(0.74);
  controller.setActiveMaskRangeFeather(0.09);
  if (controller.activeMaskKind() != QStringLiteral("depth") ||
      !almost_equal(controller.activeMaskDepthMin(), 0.28) ||
      !almost_equal(controller.activeMaskDepthMax(), 0.74) ||
      !almost_equal(controller.activeMaskRangeFeather(), 0.09)) {
    return fail("depth range masks should expose editable depth constraints");
  }
  if (controller.activeMaskDepthSamplingAvailable() ||
      controller.activeMaskDepthSamplingDisabledReason().isEmpty() ||
      controller.sampleActiveDepthRangeAt(0.5, 0.5)) {
    return fail("depth range sampling should be disabled with a reason when no depth metadata exists");
  }

  photo_editor::desktop::EditRecipeState depth_recipe_state;
  QVariantMap sampled_depth_mask;
  sampled_depth_mask.insert(QStringLiteral("kind"), QStringLiteral("depth"));
  sampled_depth_mask.insert(QStringLiteral("name"), QStringLiteral("Depth Payload"));
  sampled_depth_mask.insert(QStringLiteral("depthMin"), 0.2);
  sampled_depth_mask.insert(QStringLiteral("depthMax"), 0.8);
  sampled_depth_mask.insert(QStringLiteral("rangeFeather"), 0.1);
  sampled_depth_mask.insert(QStringLiteral("depthData"), QByteArray::fromHex("001080ff"));
  sampled_depth_mask.insert(QStringLiteral("depthWidth"), 2);
  sampled_depth_mask.insert(QStringLiteral("depthHeight"), 2);
  depth_recipe_state.mask_stack = {sampled_depth_mask};
  depth_recipe_state.active_mask_index = 0;
  photo_editor::desktop::EditorPreviewController depth_range_controller;
  depth_range_controller.loadAsset(
      QStringLiteral("depth-range-source"),
      luminance_range_path,
      QString(),
      QString(),
      QString(),
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(depth_recipe_state));
  depth_range_controller.setActiveMaskIndex(0);
  if (depth_range_controller.activeMaskKind() != QStringLiteral("depth") ||
      !depth_range_controller.activeMaskDepthSamplingAvailable() ||
      !depth_range_controller.activeMaskDepthSamplingDisabledReason().isEmpty()) {
    return fail("depth range sampling should be available when a valid depth payload exists");
  }
  if (!depth_range_controller.sampleActiveDepthRangeAt(1.0, 1.0) ||
      depth_range_controller.activeMaskDepthSamples().size() != 1 ||
      depth_range_controller.activeMaskDepthMin() > 0.1 ||
      depth_range_controller.activeMaskDepthMax() < 0.9) {
    return fail("depth range sampling should sample the active depth payload and update constraints");
  }
  for (int index = 0; index < 5; ++index) {
    if (!depth_range_controller.sampleActiveDepthRangeAt(index % 2 == 0 ? 0.0 : 1.0, 0.0)) {
      return fail("depth range sampling should support repeated samples from the depth payload");
    }
  }
  if (depth_range_controller.activeMaskDepthSamples().size() != 5 ||
      !depth_range_controller.removeActiveDepthRangeSample(0) ||
      depth_range_controller.activeMaskDepthSamples().size() != 4 ||
      depth_range_controller.removeActiveDepthRangeSample(99)) {
    return fail("depth range sampling should retain five samples and support removal");
  }

  photo_editor::desktop::EditRecipeState full_resolution_alpha_state;
  QVariantList full_resolution_alpha_stack;
  const QString full_resolution_set_id = QStringLiteral("full-resolution-alpha-set");
  const auto append_full_resolution_mask = [&](QVariantMap mask, const QString& id) {
    mask.insert(QStringLiteral("id"), id);
    mask.insert(QStringLiteral("maskSetId"), full_resolution_set_id);
    mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Full Resolution Alpha"));
    mask.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
    mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
    full_resolution_alpha_stack.append(mask);
  };
  auto full_resolution_brush =
      photo_editor::desktop::default_mask_definition(QStringLiteral("brush"), 1);
  QVariantMap full_resolution_brush_point = normalized_point(0.44, 0.52);
  full_resolution_brush_point.insert(QStringLiteral("size"), 0.08);
  full_resolution_brush_point.insert(QStringLiteral("flow"), 0.9);
  full_resolution_brush_point.insert(QStringLiteral("pressure"), 0.7);
  full_resolution_brush.insert(
      QStringLiteral("brushPoints"),
      QVariantList{full_resolution_brush_point});
  append_full_resolution_mask(
      full_resolution_brush,
      QStringLiteral("full-resolution-brush"));
  for (const auto& kind :
       {QStringLiteral("linear"),
        QStringLiteral("radial"),
        QStringLiteral("rectangle"),
        QStringLiteral("ellipse"),
        QStringLiteral("luminance"),
        QStringLiteral("color"),
        QStringLiteral("depth")}) {
    append_full_resolution_mask(
        photo_editor::desktop::default_mask_definition(
            kind,
            static_cast<int>(full_resolution_alpha_stack.size()) + 1),
        QStringLiteral("full-resolution-%1").arg(kind));
  }
  auto full_resolution_polygon =
      photo_editor::desktop::default_mask_definition(
          QStringLiteral("polygon"),
          static_cast<int>(full_resolution_alpha_stack.size()) + 1);
  full_resolution_polygon.insert(
      QStringLiteral("polygonPoints"),
      QVariantList{
          normalized_point(0.22, 0.20),
          normalized_point(0.74, 0.28),
          normalized_point(0.62, 0.74),
          normalized_point(0.28, 0.68),
      });
  append_full_resolution_mask(
      full_resolution_polygon,
      QStringLiteral("full-resolution-polygon"));
  auto full_resolution_lasso =
      photo_editor::desktop::default_mask_definition(
          QStringLiteral("lasso"),
          static_cast<int>(full_resolution_alpha_stack.size()) + 1);
  const QVariantList full_resolution_lasso_points{
      normalized_point(0.18, 0.24),
      normalized_point(0.36, 0.18),
      normalized_point(0.68, 0.30),
      normalized_point(0.58, 0.72),
      normalized_point(0.24, 0.66),
  };
  full_resolution_lasso.insert(QStringLiteral("polygonPoints"), full_resolution_lasso_points);
  full_resolution_lasso.insert(QStringLiteral("lassoRawPoints"), full_resolution_lasso_points);
  full_resolution_lasso.insert(QStringLiteral("lassoClosed"), true);
  full_resolution_lasso.insert(QStringLiteral("lassoSimplified"), true);
  full_resolution_lasso.insert(QStringLiteral("lassoSmoothed"), true);
  full_resolution_lasso.insert(QStringLiteral("lassoRawPointCount"), full_resolution_lasso_points.size());
  append_full_resolution_mask(
      full_resolution_lasso,
      QStringLiteral("full-resolution-lasso"));
  auto full_resolution_object =
      photo_editor::desktop::default_mask_definition(
          QStringLiteral("object"),
          static_cast<int>(full_resolution_alpha_stack.size()) + 1);
  full_resolution_object.insert(QStringLiteral("segmentationData"), QByteArray(64, char(0xff)));
  full_resolution_object.insert(QStringLiteral("segmentationWidth"), 8);
  full_resolution_object.insert(QStringLiteral("segmentationHeight"), 8);
  QVariantMap full_resolution_generated_metadata;
  full_resolution_generated_metadata.insert(QStringLiteral("modelName"), QStringLiteral("test"));
  full_resolution_generated_metadata.insert(QStringLiteral("modelVersion"), QStringLiteral("1"));
  full_resolution_generated_metadata.insert(QStringLiteral("promptType"), QStringLiteral("object"));
  full_resolution_generated_metadata.insert(
      QStringLiteral("sourceImageVersion"),
      QStringLiteral("source-revision"));
  full_resolution_object.insert(
      QStringLiteral("generatedMaskMetadata"),
      full_resolution_generated_metadata);
  append_full_resolution_mask(
      full_resolution_object,
      QStringLiteral("full-resolution-object"));
  full_resolution_alpha_state.mask_stack = full_resolution_alpha_stack;
  full_resolution_alpha_state.active_mask_index = 0;
  photo_editor::desktop::EditorPreviewController full_resolution_alpha_controller;
  full_resolution_alpha_controller.loadAsset(
      QStringLiteral("full-resolution-alpha-contract"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(full_resolution_alpha_state));
  const auto source_by_id = [&](const QString& id) {
    for (const auto& set_value : full_resolution_alpha_controller.maskSets()) {
      const auto sources = set_value.toMap().value(QStringLiteral("sources")).toList();
      for (const auto& source_value : sources) {
        const auto source = source_value.toMap();
        if (source.value(QStringLiteral("id")).toString() == id) {
          return source;
        }
      }
    }
    return QVariantMap{};
  };
  if (!mask_source_has_full_resolution_alpha_contract(
          source_by_id(QStringLiteral("full-resolution-brush")),
          QStringLiteral("brush-stroke-replay"))) {
    return fail("brush mask sources should expose deterministic full-resolution stroke replay");
  }
  for (const auto& kind :
       {QStringLiteral("linear"),
        QStringLiteral("radial"),
        QStringLiteral("rectangle"),
        QStringLiteral("ellipse"),
        QStringLiteral("luminance"),
        QStringLiteral("color"),
        QStringLiteral("depth"),
        QStringLiteral("polygon"),
        QStringLiteral("lasso")}) {
    if (!mask_source_has_full_resolution_alpha_contract(
            source_by_id(QStringLiteral("full-resolution-%1").arg(kind)),
            QStringLiteral("procedural-geometry"))) {
      return fail("procedural mask sources should expose deterministic full-resolution geometry");
    }
  }
  if (!mask_source_has_full_resolution_alpha_contract(
          source_by_id(QStringLiteral("full-resolution-object")),
          QStringLiteral("raster-payload"))) {
    return fail("generated raster mask sources should expose full-resolution raster payload availability");
  }

  const auto grouped_source_for_mask = [&](const QString& mask_id) {
    for (const auto& set_value : controller.maskSets()) {
      const auto sources = set_value.toMap().value(QStringLiteral("sources")).toList();
      for (const auto& source_value : sources) {
        const auto source = source_value.toMap();
        if (source.value(QStringLiteral("id")).toString() == mask_id) {
          return source;
        }
      }
    }
    return QVariantMap{};
  };

  const auto lasso_stack_count_before = controller.maskStack().size();
  controller.beginCoalescedEdit();
  controller.startLassoSelection(false);
  controller.appendActivePolygonPoint(0.18, 0.18);
  controller.appendActivePolygonPoint(0.34, 0.19);
  controller.appendActivePolygonPoint(0.50, 0.20);
  controller.appendActivePolygonPoint(0.70, 0.22);
  controller.appendActivePolygonPoint(0.64, 0.72);
  controller.appendActivePolygonPoint(0.38, 0.76);
  controller.appendActivePolygonPoint(0.18, 0.70);
  const auto open_lasso_count = controller.activeMaskPolygonPointCount();
  if (controller.activeMaskKind() != QStringLiteral("lasso") ||
      controller.activeMaskOperation() != QStringLiteral("add") ||
      open_lasso_count < 6) {
    return fail("lasso selections should capture a freehand additive path while drawing");
  }
  if (!controller.closeActiveLassoPath() ||
      controller.activeMaskKind() != QStringLiteral("lasso") ||
      controller.activeMaskPolygonPointCount() == open_lasso_count) {
    return fail("lasso selections should close by simplifying and smoothing the freehand path");
  }
  const auto closed_lasso = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  if (!closed_lasso.value(QStringLiteral("lassoClosed")).toBool() ||
      !closed_lasso.value(QStringLiteral("lassoSimplified")).toBool() ||
      !closed_lasso.value(QStringLiteral("lassoSmoothed")).toBool() ||
      !closed_lasso.value(QStringLiteral("lassoFreehandPath")).toBool() ||
       closed_lasso.value(QStringLiteral("lassoRawPointCount")).toInt() != open_lasso_count ||
       closed_lasso.value(QStringLiteral("lassoSimplifiedPointCount")).toInt() !=
           controller.activeMaskPolygonPointCount()) {
    return fail("closed lasso selections should preserve raw freehand metadata and expose the committed path count");
  }
  const auto closed_lasso_signature =
      closed_lasso.value(QStringLiteral("shapeAlphaSourceSignature")).toString();
  if (closed_lasso.value(QStringLiteral("shapeAlphaSourceContract")).toString() !=
          QStringLiteral("editable-path-plus-alpha-tile-cache-v1") ||
      closed_lasso.value(QStringLiteral("shapeAlphaSourceOfTruth")).toString() !=
          QStringLiteral("lassoRawPoints+polygonPoints") ||
      closed_lasso.value(QStringLiteral("shapeAlphaPointCount")).toInt() !=
          controller.activeMaskPolygonPointCount() ||
      closed_lasso.value(QStringLiteral("shapeAlphaRawPointCount")).toInt() != open_lasso_count ||
      !closed_lasso.value(QStringLiteral("shapeAlphaClosed")).toBool() ||
      !closed_lasso.value(QStringLiteral("shapeAlphaEditable")).toBool() ||
      closed_lasso.value(QStringLiteral("shapeAlphaTileCacheKind")).toString() !=
          QStringLiteral("mask-alpha-tile-cache") ||
      !closed_lasso.value(QStringLiteral("shapeAlphaTileCacheEnabled")).toBool() ||
      closed_lasso.value(QStringLiteral("shapeAlphaTileCacheTileSize")).toInt() != 256 ||
      !closed_lasso_signature.startsWith(QStringLiteral("sha1:"))) {
    return fail("closed lasso selections should expose editable path plus alpha tile cache metadata");
  }
  const auto grouped_lasso_source =
      grouped_source_for_mask(closed_lasso.value(QStringLiteral("id")).toString());
  if (grouped_lasso_source.value(QStringLiteral("shapeAlphaSourceSignature")).toString() !=
          closed_lasso_signature ||
      grouped_lasso_source.value(QStringLiteral("shapeAlphaSourceContract")).toString() !=
          QStringLiteral("editable-path-plus-alpha-tile-cache-v1") ||
      grouped_lasso_source.value(QStringLiteral("shapeAlphaSourceOfTruth")).toString() !=
          QStringLiteral("lassoRawPoints+polygonPoints") ||
      grouped_lasso_source.value(QStringLiteral("shapeAlphaPointCount")).toInt() !=
          controller.activeMaskPolygonPointCount() ||
      grouped_lasso_source.value(QStringLiteral("shapeAlphaRawPointCount")).toInt() !=
          open_lasso_count ||
      !grouped_lasso_source.value(QStringLiteral("shapeAlphaClosed")).toBool() ||
      !grouped_lasso_source.value(QStringLiteral("shapeAlphaEditable")).toBool() ||
      grouped_lasso_source.value(QStringLiteral("shapeAlphaTileCacheKind")).toString() !=
          QStringLiteral("mask-alpha-tile-cache") ||
      !grouped_lasso_source.value(QStringLiteral("shapeAlphaTileCacheEnabled")).toBool() ||
      grouped_lasso_source.value(QStringLiteral("shapeAlphaTileCacheTileSize")).toInt() !=
          256) {
    return fail("grouped lasso sources should preserve editable path cache metadata");
  }
  const auto lasso_bounds = outline_bounds(controller.activeMaskOutlinePaths());
  const auto expected_lasso_bounds = QRectF(QPointF(0.18, 0.18), QPointF(0.70, 0.76));
  if (!outline_bounds_match(lasso_bounds, expected_lasso_bounds, 0.035)) {
    const auto detail =
        outline_bounds_detail(QStringLiteral("lasso"), lasso_bounds, expected_lasso_bounds).toUtf8();
    return fail(detail.constData());
  }
  controller.endCoalescedEdit();
  if (!controller.canUndo()) {
    return fail("lasso creation should be undoable after its coalesced draw transaction");
  }
  controller.undo();
  if (controller.maskStack().size() != lasso_stack_count_before) {
    return fail("one undo should remove the entire coalesced lasso creation");
  }
  if (!controller.canRedo()) {
    return fail("lasso creation should be redoable after a coalesced undo");
  }
  controller.redo();
  if (controller.activeMaskKind() != QStringLiteral("lasso") ||
      !controller.maskStack().at(controller.activeMaskIndex()).toMap().value(QStringLiteral("lassoClosed")).toBool() ||
      controller.activeMaskPolygonPointCount() !=
          closed_lasso.value(QStringLiteral("lassoSimplifiedPointCount")).toInt() ||
      controller.maskStack()
              .at(controller.activeMaskIndex())
              .toMap()
              .value(QStringLiteral("shapeAlphaSourceSignature"))
              .toString() != closed_lasso_signature) {
    return fail("redo should restore the committed lasso path from one coalesced history entry");
  }
  controller.startLassoSelection(true);
  controller.appendActivePolygonPoint(0.20, 0.20);
  controller.appendActivePolygonPoint(0.70, 0.22);
  controller.appendActivePolygonPoint(0.62, 0.70);
  if (controller.activeMaskKind() != QStringLiteral("lasso") ||
      controller.activeMaskOperation() != QStringLiteral("subtract") ||
      !controller.closeActiveLassoPath()) {
    return fail("lasso subtract should use the same close-on-release path as additive lasso");
  }

  const auto ai_mask_path = runtime_root.filePath(QStringLiteral("ai-mask-source.png"));
  QImage ai_mask_source(24, 24, QImage::Format_ARGB32);
  ai_mask_source.fill(QColor(160, 190, 230));
  if (!ai_mask_source.save(ai_mask_path)) {
    return fail("AI mask source image should be writable");
  }
  const auto ai_mask_second_path = runtime_root.filePath(QStringLiteral("ai-mask-source-second.png"));
  if (!ai_mask_source.save(ai_mask_second_path)) {
    return fail("second AI mask source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController ai_mask_controller;
  ai_mask_controller.loadAsset(
      QStringLiteral("ai-mask-source"),
      ai_mask_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());

  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::ensure_smart_select_instance_map(
          ai_mask_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_instance_map_key(
          ai_mask_controller).isEmpty() ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_instance_map_size(
          ai_mask_controller).isValid() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_instance_map_shared_cache_hit(
          ai_mask_controller)) {
    return fail("smart selection should build an initial foreground instance map cache entry");
  }
  const auto smart_select_instance_cache_key =
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_instance_map_key(
          ai_mask_controller);
  if (!smart_select_instance_cache_key.contains(QStringLiteral("model:foreground-instance-map-v1")) ||
      !smart_select_instance_cache_key.contains(QStringLiteral("edge:1024"))) {
    return fail("smart selection instance map cache keys should include model version and preview resolution");
  }
  const auto smart_select_instance_cache_size =
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_instance_map_size(
          ai_mask_controller);
  photo_editor::desktop::EditorPreviewController cached_instance_map_controller;
  cached_instance_map_controller.loadAsset(
      QStringLiteral("ai-mask-source-cached"),
      ai_mask_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::ensure_smart_select_instance_map(
          cached_instance_map_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_instance_map_shared_cache_hit(
          cached_instance_map_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_instance_map_key(
          cached_instance_map_controller) != smart_select_instance_cache_key ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_instance_map_size(
          cached_instance_map_controller) != smart_select_instance_cache_size) {
    return fail("smart selection should reuse cached instance maps across controllers for the same source state");
  }
  cached_instance_map_controller.setCropZoom(1.08);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::ensure_smart_select_instance_map(
          cached_instance_map_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_instance_map_key(
          cached_instance_map_controller) == smart_select_instance_cache_key ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_instance_map_shared_cache_hit(
          cached_instance_map_controller)) {
    return fail("smart selection instance map cache keys should include crop state");
  }
  const auto smart_select_crop_cache_key =
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_instance_map_key(
          cached_instance_map_controller);
  cached_instance_map_controller.setTransformRotation(2.0);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::ensure_smart_select_instance_map(
          cached_instance_map_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_instance_map_key(
          cached_instance_map_controller) == smart_select_crop_cache_key) {
    return fail("smart selection instance map cache keys should include rotation state");
  }
  photo_editor::desktop::EditorPreviewController second_source_instance_map_controller;
  second_source_instance_map_controller.loadAsset(
      QStringLiteral("ai-mask-source-second"),
      ai_mask_second_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::ensure_smart_select_instance_map(
          second_source_instance_map_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_instance_map_key(
          second_source_instance_map_controller) == smart_select_instance_cache_key) {
    return fail("smart selection instance map cache keys should include source file identity");
  }

  ai_mask_controller.addAIMask(QStringLiteral("sky"));
  auto active_mask = ai_mask_controller.maskStack().at(ai_mask_controller.activeMaskIndex()).toMap();
  if (ai_mask_controller.activeMaskKind() != QStringLiteral("sky") ||
      active_mask.value(QStringLiteral("segmentationData")).toByteArray().isEmpty()) {
    return fail("AI sky masks should create a semantic raster mask");
  }
  if (active_mask.value(QStringLiteral("confidenceData")).toByteArray().isEmpty() ||
      active_mask.value(QStringLiteral("confidenceWidth")).toInt() !=
          active_mask.value(QStringLiteral("segmentationWidth")).toInt() ||
      active_mask.value(QStringLiteral("confidenceHeight")).toInt() !=
          active_mask.value(QStringLiteral("segmentationHeight")).toInt() ||
      active_mask.value(QStringLiteral("confidenceMapSchemaVersion")).toInt() != 1) {
    return fail("generated masks should preserve confidence maps when the runtime returns them");
  }
  if (ai_mask_controller.staleGeneratedMaskCount() != 0 ||
      active_mask.value(QStringLiteral("generatedMaskMetadata")).toMap().isEmpty()) {
    return fail("new generated masks should be stamped with current source/crop/model metadata");
  }
  if (ai_mask_controller.activeGeneratedMaskQualityScore() <= 0 ||
      ai_mask_controller.activeGeneratedMaskQualitySummary().isEmpty() ||
      ai_mask_controller.activeGeneratedMaskQualityFlags().contains(QStringLiteral("empty"))) {
    return fail("generated masks should expose a non-empty quality score and summary");
  }
  const auto generated_metadata = active_mask.value(QStringLiteral("generatedMaskMetadata")).toMap();
  const auto generated_dependencies = generated_metadata.value(QStringLiteral("dependencies")).toMap();
  if (active_mask.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
      active_mask.value(QStringLiteral("sourceSchemaVersion")).toInt() != 1 ||
      active_mask.value(QStringLiteral("rasterSchemaVersion")).toInt() != 1 ||
      generated_metadata.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
      generated_metadata.value(QStringLiteral("rasterSchemaVersion")).toInt() != 1 ||
      generated_metadata.value(QStringLiteral("generatedOutputSchemaVersion")).toInt() != 1 ||
      generated_metadata.value(QStringLiteral("modelName")).toString().isEmpty() ||
      generated_metadata.value(QStringLiteral("modelVersion")).toString() != QStringLiteral("semantic-mask-v1") ||
      generated_metadata.value(QStringLiteral("promptType")).toString() != QStringLiteral("semantic:sky") ||
      !generated_metadata.value(QStringLiteral("confidenceMapAvailable")).toBool() ||
      generated_metadata.value(QStringLiteral("confidenceMapSchemaVersion")).toInt() != 1 ||
      generated_metadata.value(QStringLiteral("confidenceResolution")).toString() != QStringLiteral("8x8") ||
      generated_metadata.value(QStringLiteral("sourceResolution")).toString() != QStringLiteral("24x24") ||
      generated_metadata.value(QStringLiteral("sourceImageVersion")).toString().isEmpty() ||
      generated_dependencies.value(QStringLiteral("promptType")).toString() != QStringLiteral("semantic:sky") ||
      generated_dependencies.value(QStringLiteral("sourceImageVersion")).toString().isEmpty() ||
      generated_dependencies.value(QStringLiteral("modelVersion")).toString() != QStringLiteral("semantic-mask-v1")) {
    return fail("generated masks should expose schema, model, prompt, source, and dependency metadata");
  }
  if (active_mask.value(QStringLiteral("generatedAlphaSourceContract")).toString() !=
          QStringLiteral("generated-alpha-plus-metadata-v1") ||
      active_mask.value(QStringLiteral("generatedAlphaTargetType")).toString() != QStringLiteral("sky") ||
      active_mask.value(QStringLiteral("generatedAlphaPromptType")).toString() != QStringLiteral("semantic:sky") ||
      active_mask.value(QStringLiteral("generatedAlphaModelVersion")).toString() !=
          QStringLiteral("semantic-mask-v1") ||
      active_mask.value(QStringLiteral("generatedAlphaSourceImageVersion")).toString().isEmpty() ||
      active_mask.value(QStringLiteral("generatedAlphaWidth")).toInt() !=
          active_mask.value(QStringLiteral("segmentationWidth")).toInt() ||
      active_mask.value(QStringLiteral("generatedAlphaHeight")).toInt() !=
          active_mask.value(QStringLiteral("segmentationHeight")).toInt() ||
      !almost_equal(active_mask.value(QStringLiteral("generatedAlphaDownscaleFactor")).toDouble(), 3.0) ||
      !active_mask.value(QStringLiteral("generatedAlphaConfidenceAvailable")).toBool() ||
      active_mask.value(QStringLiteral("generatedAlphaConfidenceWidth")).toInt() != 8 ||
      active_mask.value(QStringLiteral("generatedAlphaConfidenceHeight")).toInt() != 8 ||
      !active_mask.value(QStringLiteral("generatedAlphaRefinementStateSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !active_mask.value(QStringLiteral("generatedAlphaSourceSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      generated_metadata.value(QStringLiteral("generatedAlphaSourceSignature")).toString() !=
          active_mask.value(QStringLiteral("generatedAlphaSourceSignature")).toString()) {
    return fail("generated masks should expose a canonical alpha plus generation metadata contract");
  }
  QVariantMap grouped_generated_source;
  for (const auto& set_value : ai_mask_controller.maskSets()) {
    const auto sources = set_value.toMap().value(QStringLiteral("sources")).toList();
    for (const auto& source_value : sources) {
      const auto source = source_value.toMap();
      if (source.value(QStringLiteral("id")).toString() ==
          active_mask.value(QStringLiteral("id")).toString()) {
        grouped_generated_source = source;
      }
    }
  }
  if (grouped_generated_source.value(QStringLiteral("generatedAlphaSourceContract")).toString() !=
          QStringLiteral("generated-alpha-plus-metadata-v1") ||
      grouped_generated_source.value(QStringLiteral("generatedAlphaTargetType")).toString() !=
          QStringLiteral("sky") ||
      grouped_generated_source.value(QStringLiteral("generatedAlphaPromptType")).toString() !=
          QStringLiteral("semantic:sky") ||
      grouped_generated_source.value(QStringLiteral("generatedAlphaModelVersion")).toString() !=
          QStringLiteral("semantic-mask-v1") ||
      grouped_generated_source.value(QStringLiteral("generatedAlphaConfidenceAvailable")).toBool() !=
          true ||
      grouped_generated_source.value(QStringLiteral("generatedAlphaConfidenceWidth")).toInt() != 8 ||
      grouped_generated_source.value(QStringLiteral("generatedAlphaConfidenceHeight")).toInt() != 8 ||
      grouped_generated_source.value(QStringLiteral("generatedAlphaRefinementStateSignature"))
              .toString() !=
          active_mask.value(QStringLiteral("generatedAlphaRefinementStateSignature")).toString() ||
      grouped_generated_source.value(QStringLiteral("generatedAlphaSourceSignature")).toString() !=
          active_mask.value(QStringLiteral("generatedAlphaSourceSignature")).toString()) {
    return fail("grouped generated mask sources should preserve alpha, model, prompt, confidence, and refinement metadata");
  }
  ai_mask_controller.setCropZoom(1.12);
  ai_mask_controller.setTransformRotation(3.0);
  if (ai_mask_controller.staleGeneratedMaskCount() != 1 ||
      !ai_mask_controller.staleGeneratedMaskSummary().contains(QStringLiteral("source dimensions")) ||
      !ai_mask_controller.staleGeneratedMaskSummary().contains(QStringLiteral("Sky"))) {
    return fail("generated masks should become stale after crop or rotation changes");
  }
  if (!ai_mask_controller.activeGeneratedMaskQualityFlags().contains(QStringLiteral("stale")) ||
      ai_mask_controller.activeGeneratedMaskQualityScore() >= 100) {
    return fail("generated mask quality should include stale state in its score and flags");
  }
  const auto stale_active_index = ai_mask_controller.activeMaskIndex();
  if (ai_mask_controller.updateStaleGeneratedMasks() != 1 ||
      ai_mask_controller.staleGeneratedMaskCount() != 0 ||
      ai_mask_controller.activeMaskIndex() != stale_active_index ||
      !ai_mask_controller.status().contains(QStringLiteral("Updated 1 generated selection"))) {
    return fail("Update AI Masks should regenerate stale generated selections in place");
  }
  if (ai_mask_controller.activeGeneratedMaskQualityFlags().contains(QStringLiteral("stale"))) {
    return fail("generated mask quality should clear stale warnings after update");
  }
  ai_mask_controller.addAIMask(QStringLiteral("background"));
  active_mask = ai_mask_controller.maskStack().at(ai_mask_controller.activeMaskIndex()).toMap();
  if (ai_mask_controller.activeMaskKind() != QStringLiteral("background") ||
      active_mask.value(QStringLiteral("segmentationWidth")).toInt() <= 0) {
    return fail("AI background masks should create an inverted semantic raster mask");
  }
  auto known_alpha_mask =
      photo_editor::desktop::default_mask_definition(QStringLiteral("object"), 1);
  known_alpha_mask.insert(QStringLiteral("id"), QStringLiteral("known-alpha-outline"));
  known_alpha_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("known-alpha-set"));
  known_alpha_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Known Alpha"));
  known_alpha_mask.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
  known_alpha_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  known_alpha_mask.insert(QStringLiteral("selectionExpand"), 0.0);
  known_alpha_mask.insert(QStringLiteral("selectionSmooth"), 0.0);
  known_alpha_mask.insert(QStringLiteral("selectionFineEdge"), 0.0);
  known_alpha_mask.insert(QStringLiteral("selectionEdgeContrast"), 0.0);
  known_alpha_mask.insert(QStringLiteral("selectionDecontaminate"), 0.0);
  known_alpha_mask.insert(QStringLiteral("maskSetSelectionExpand"), 0.0);
  known_alpha_mask.insert(QStringLiteral("maskSetSelectionSmooth"), 0.0);
  known_alpha_mask.insert(QStringLiteral("maskSetSelectionFineEdge"), 0.0);
  known_alpha_mask.insert(QStringLiteral("maskSetSelectionEdgeContrast"), 0.0);
  known_alpha_mask.insert(QStringLiteral("maskSetSelectionDecontaminate"), 0.0);
  known_alpha_mask.insert(QStringLiteral("maskSetSelectionInvert"), false);
  constexpr auto known_alpha_width = 16;
  constexpr auto known_alpha_height = 16;
  QByteArray known_alpha_data(known_alpha_width * known_alpha_height, char(0));
  for (auto y = 4; y <= 10; ++y) {
    for (auto x = 3; x <= 8; ++x) {
      known_alpha_data[y * known_alpha_width + x] = char(0xff);
    }
  }
  known_alpha_mask.insert(QStringLiteral("segmentationData"), known_alpha_data);
  known_alpha_mask.insert(QStringLiteral("segmentationWidth"), known_alpha_width);
  known_alpha_mask.insert(QStringLiteral("segmentationHeight"), known_alpha_height);
  photo_editor::desktop::EditorPreviewController known_alpha_outline_controller;
  known_alpha_outline_controller.loadAsset(
      QStringLiteral("known-alpha-outline"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  known_alpha_outline_controller.addMask(QStringLiteral("linear"));
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          known_alpha_outline_controller,
          known_alpha_mask)) {
    return fail("known alpha outline test should install a raster source mask");
  }
  known_alpha_outline_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  const QRectF known_alpha_expected_bounds(
      QPointF(3.0 / known_alpha_width, 4.0 / known_alpha_height),
      QPointF(9.0 / known_alpha_width, 11.0 / known_alpha_height));
  const auto known_alpha_traced_bounds =
      outline_bounds(known_alpha_outline_controller.activeMaskOutlinePaths());
  if (!outline_bounds_match(
          known_alpha_traced_bounds,
          known_alpha_expected_bounds,
          1.0 / static_cast<double>(known_alpha_width))) {
    const auto detail = outline_bounds_detail(
                            QStringLiteral("known alpha"),
                            known_alpha_traced_bounds,
                            known_alpha_expected_bounds)
                            .toUtf8();
    return fail(detail.constData());
  }
  const auto known_alpha_outline_trace_fields =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_fields(
          known_alpha_outline_controller);
  const auto known_alpha_public_outline_trace = known_alpha_outline_controller.activeMaskOutlineTrace();
  if (known_alpha_outline_trace_fields.value(QStringLiteral("outline_trace_source")).toString() !=
          QStringLiteral("direct-mask") ||
      known_alpha_public_outline_trace.value(QStringLiteral("outline_trace_source")).toString() !=
          QStringLiteral("cache-hit") ||
      known_alpha_outline_trace_fields.value(QStringLiteral("mask_alpha_provider")).toString() !=
          QStringLiteral("raster-payload") ||
      known_alpha_public_outline_trace.value(QStringLiteral("mask_alpha_provider")).toString() !=
          QStringLiteral("raster-payload") ||
      known_alpha_outline_trace_fields.value(QStringLiteral("mask_alpha_raster_width")).toInt() !=
          known_alpha_width ||
      known_alpha_public_outline_trace.value(QStringLiteral("outline_trace_tolerance_pixels")).toDouble() !=
          1.5 ||
      known_alpha_outline_trace_fields.value(QStringLiteral("mask_alpha_raster_height")).toInt() !=
          known_alpha_height) {
    return fail("known alpha outline tracing should publish raster alpha trace metadata publicly");
  }
  const auto assert_generated_semantic_outline_bounds = [&](const QString& kind) -> QString {
    photo_editor::desktop::EditorPreviewController semantic_outline_controller;
    semantic_outline_controller.loadAsset(
        QStringLiteral("semantic-outline-") + kind,
        ai_mask_path,
        QString(),
        QString(),
        QString(),
        QString(),
        QString());
    semantic_outline_controller.setMaskPreviewMode(QStringLiteral("overlay"));
    semantic_outline_controller.addAIMask(kind);
    if (semantic_outline_controller.maskStack().isEmpty() ||
        semantic_outline_controller.activeMaskKind() != kind) {
      return QStringLiteral("generated %1 mask should be created before outline matching").arg(kind);
    }
    auto semantic_mask =
        semantic_outline_controller.maskStack()
            .at(semantic_outline_controller.activeMaskIndex())
            .toMap();
    semantic_mask.insert(QStringLiteral("selectionExpand"), 0.0);
    semantic_mask.insert(QStringLiteral("selectionSmooth"), 0.0);
    semantic_mask.insert(QStringLiteral("selectionFineEdge"), 0.0);
    semantic_mask.insert(QStringLiteral("selectionEdgeContrast"), 0.0);
    semantic_mask.insert(QStringLiteral("selectionDecontaminate"), 0.0);
    semantic_mask.insert(QStringLiteral("maskSetSelectionExpand"), 0.0);
    semantic_mask.insert(QStringLiteral("maskSetSelectionSmooth"), 0.0);
    semantic_mask.insert(QStringLiteral("maskSetSelectionFineEdge"), 0.0);
    semantic_mask.insert(QStringLiteral("maskSetSelectionEdgeContrast"), 0.0);
    semantic_mask.insert(QStringLiteral("maskSetSelectionDecontaminate"), 0.0);
    semantic_mask.insert(QStringLiteral("maskSetSelectionInvert"), false);
    if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
            semantic_outline_controller,
            semantic_mask)) {
      return QStringLiteral("generated %1 mask should accept neutral outline test refinement").arg(kind);
    }

    semantic_mask =
        semantic_outline_controller.maskStack()
            .at(semantic_outline_controller.activeMaskIndex())
            .toMap();
    const auto alpha_bounds = alpha_mask_bounds(
        semantic_mask.value(QStringLiteral("segmentationData")).toByteArray(),
        semantic_mask.value(QStringLiteral("segmentationWidth")).toInt(),
        semantic_mask.value(QStringLiteral("segmentationHeight")).toInt());
    const auto traced_bounds = outline_bounds(semantic_outline_controller.activeMaskOutlinePaths());
    const auto tolerance = outline_tolerance_for_image_pixels(
        semantic_mask.value(QStringLiteral("segmentationWidth")).toInt(),
        semantic_mask.value(QStringLiteral("segmentationHeight")).toInt());
    const auto semantic_outline_trace_fields =
        photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_fields(
            semantic_outline_controller);
    if (!outline_bounds_match(traced_bounds, alpha_bounds, tolerance)) {
      return outline_bounds_detail(QStringLiteral("generated ") + kind, traced_bounds, alpha_bounds);
    }
    if (std::abs(
            semantic_outline_trace_fields.value(QStringLiteral("outline_trace_tolerance_normalized")).toDouble() -
            tolerance) > 1e-9 ||
        semantic_outline_trace_fields.value(QStringLiteral("outline_trace_tolerance_pixels")).toDouble() !=
            1.5) {
      return QStringLiteral("generated %1 outline tracing should publish the one-image-pixel tolerance contract")
          .arg(kind);
    }
    return {};
  };
  for (const auto& kind : {QStringLiteral("subject"), QStringLiteral("sky"), QStringLiteral("background")}) {
    const auto outline_failure = assert_generated_semantic_outline_bounds(kind);
    if (!outline_failure.isEmpty()) {
      return fail(outline_failure.toUtf8().constData());
    }
  }

  photo_editor::desktop::EditRecipeState generated_quality_state;
  QVariantMap generated_quality_metadata;
  generated_quality_metadata.insert(QStringLiteral("schemaVersion"), 1);
  generated_quality_metadata.insert(QStringLiteral("kind"), QStringLiteral("object"));
  generated_quality_metadata.insert(QStringLiteral("promptType"), QStringLiteral("semantic:object"));
  generated_quality_metadata.insert(QStringLiteral("runtime"), QStringLiteral("Quality Test"));
  generated_quality_metadata.insert(QStringLiteral("modelVersion"), QStringLiteral("quality-test-v1"));
  auto huge_low_confidence_mask =
      photo_editor::desktop::default_mask_definition(QStringLiteral("object"), 1);
  huge_low_confidence_mask.insert(QStringLiteral("name"), QStringLiteral("Huge Low Confidence"));
  huge_low_confidence_mask.insert(QStringLiteral("generatedMaskMetadata"), generated_quality_metadata);
  huge_low_confidence_mask.insert(QStringLiteral("segmentationData"), QByteArray(16, char(255)));
  huge_low_confidence_mask.insert(QStringLiteral("segmentationWidth"), 4);
  huge_low_confidence_mask.insert(QStringLiteral("segmentationHeight"), 4);
  huge_low_confidence_mask.insert(QStringLiteral("confidenceData"), QByteArray(16, char(60)));
  huge_low_confidence_mask.insert(QStringLiteral("confidenceWidth"), 4);
  huge_low_confidence_mask.insert(QStringLiteral("confidenceHeight"), 4);
  huge_low_confidence_mask.insert(QStringLiteral("confidenceMapSchemaVersion"), 1);
  auto empty_generated_mask =
      photo_editor::desktop::default_mask_definition(QStringLiteral("object"), 2);
  empty_generated_mask.insert(QStringLiteral("name"), QStringLiteral("Empty Generated"));
  empty_generated_mask.insert(QStringLiteral("generatedMaskMetadata"), generated_quality_metadata);
  empty_generated_mask.insert(QStringLiteral("segmentationData"), QByteArray(16, char(0)));
  empty_generated_mask.insert(QStringLiteral("segmentationWidth"), 4);
  empty_generated_mask.insert(QStringLiteral("segmentationHeight"), 4);
  generated_quality_state.mask_stack = {huge_low_confidence_mask, empty_generated_mask};
  generated_quality_state.active_mask_index = 0;
  photo_editor::desktop::EditorPreviewController generated_quality_controller;
  generated_quality_controller.loadAsset(
      QStringLiteral("generated-quality-source"),
      ai_mask_path,
      QString(),
      QString(),
      QString(),
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(generated_quality_state));
  generated_quality_controller.setActiveMaskIndex(0);
  const auto huge_quality_flags = generated_quality_controller.activeGeneratedMaskQualityFlags();
  if (generated_quality_controller.activeGeneratedMaskQualityScore() >= 70 ||
      !huge_quality_flags.contains(QStringLiteral("huge")) ||
      !huge_quality_flags.contains(QStringLiteral("clipped")) ||
      !huge_quality_flags.contains(QStringLiteral("low-confidence"))) {
    return fail("generated mask quality should flag huge, clipped, and low-confidence mattes");
  }
  generated_quality_controller.setActiveMaskIndex(1);
  if (generated_quality_controller.activeGeneratedMaskQualityScore() != 0 ||
      !generated_quality_controller.activeGeneratedMaskQualityFlags().contains(QStringLiteral("empty")) ||
      !generated_quality_controller.activeGeneratedMaskQualitySummary().contains(QStringLiteral("empty"))) {
    return fail("generated mask quality should mark empty generated mattes as unusable");
  }

  const auto sampling_parity_path =
      runtime_root.filePath(QStringLiteral("mask-sampling-parity-source.png"));
  QImage sampling_parity_source(64, 64, QImage::Format_ARGB32);
  sampling_parity_source.setColorSpace(photo_editor::desktop::preview_color_space());
  sampling_parity_source.fill(QColor::fromRgb(64, 64, 64));
  if (!sampling_parity_source.save(sampling_parity_path)) {
    return fail("mask sampling parity source should be writable");
  }
  photo_editor::desktop::EditRecipeState sampling_parity_state;
  auto sampling_parity_mask =
      photo_editor::desktop::default_mask_definition(QStringLiteral("object"), 1);
  sampling_parity_mask.insert(QStringLiteral("name"), QStringLiteral("Sampling Parity"));
  sampling_parity_mask.insert(QStringLiteral("exposure"), 1.0);
  sampling_parity_mask.insert(QStringLiteral("selectionGrow"), 50.0);
  sampling_parity_mask.insert(QStringLiteral("selectionSmooth"), 0.0);
  sampling_parity_mask.insert(QStringLiteral("selectionFeather"), 0.0);
  QByteArray sampling_parity_matte(64, char(0));
  sampling_parity_matte[2 * 8 + 2] = char(255);
  sampling_parity_mask.insert(QStringLiteral("segmentationData"), sampling_parity_matte);
  sampling_parity_mask.insert(QStringLiteral("segmentationWidth"), 8);
  sampling_parity_mask.insert(QStringLiteral("segmentationHeight"), 8);
  sampling_parity_state.mask_stack = {sampling_parity_mask};
  sampling_parity_state.active_mask_index = 0;
  photo_editor::desktop::EditorPreviewController sampling_parity_controller;
  sampling_parity_controller.loadAsset(
      QStringLiteral("mask-sampling-parity"),
      sampling_parity_path,
      QString(),
      QString(),
      QString(),
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(sampling_parity_state));
  if (!wait_for_preview_idle(sampling_parity_controller)) {
    return fail("sampling parity controller should finish loading before thumbnail checks");
  }
  sampling_parity_controller.setActiveMaskIndex(0);
  const auto sampling_parity_sets = sampling_parity_controller.maskSets();
  if (sampling_parity_sets.size() != 1) {
    return fail("sampling parity mask should surface as one grouped mask set");
  }
  const auto sampling_parity_thumbnail = decode_png_data_url(
      sampling_parity_sets.at(0).toMap().value(QStringLiteral("thumbnailSource")).toString());
  if (sampling_parity_thumbnail.isNull()) {
    return fail("sampling parity mask should produce a decodable thumbnail");
  }
  const auto grown_thumbnail_sample = sampling_parity_thumbnail.pixelColor(
      sampling_parity_thumbnail.width() * 6 / 7,
      sampling_parity_thumbnail.height() * 2 / 7);
  const auto ungrown_corner_sample = sampling_parity_thumbnail.pixelColor(
      sampling_parity_thumbnail.width() - 2,
      sampling_parity_thumbnail.height() - 2);
  auto sampling_thumbnail_max_alpha = 0;
  auto sampling_thumbnail_min_x = sampling_parity_thumbnail.width();
  auto sampling_thumbnail_max_x = -1;
  auto sampling_thumbnail_min_y = sampling_parity_thumbnail.height();
  auto sampling_thumbnail_max_y = -1;
  for (int y = 0; y < sampling_parity_thumbnail.height(); ++y) {
    for (int x = 0; x < sampling_parity_thumbnail.width(); ++x) {
      const auto alpha = sampling_parity_thumbnail.pixelColor(x, y).alpha();
      sampling_thumbnail_max_alpha = std::max(sampling_thumbnail_max_alpha, alpha);
      if (alpha > 0) {
        sampling_thumbnail_min_x = std::min(sampling_thumbnail_min_x, x);
        sampling_thumbnail_max_x = std::max(sampling_thumbnail_max_x, x);
        sampling_thumbnail_min_y = std::min(sampling_thumbnail_min_y, y);
        sampling_thumbnail_max_y = std::max(sampling_thumbnail_max_y, y);
      }
    }
  }
  const auto sampling_parity_render = photo_editor::desktop::render_output_image(
      sampling_parity_source,
      sampling_parity_state,
      QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32,
      nullptr,
      nullptr,
      photo_editor::desktop::RenderMaskOptions{});
  if (sampling_parity_render.isNull()) {
    return fail("sampling parity render should produce an image");
  }
  const auto grown_render_sample = sampling_parity_render.pixelColor(54, 18);
  const auto ungrown_render_sample = sampling_parity_render.pixelColor(62, 62);
  const auto thumbnail_grew_beyond_source_cell =
      sampling_thumbnail_max_alpha > 0 &&
      sampling_thumbnail_min_x < sampling_parity_thumbnail.width() / 7 &&
      sampling_thumbnail_max_x > sampling_parity_thumbnail.width() * 3 / 7 &&
      ungrown_corner_sample.alpha() == 0;
  if (!thumbnail_grew_beyond_source_cell ||
      grown_render_sample.red() <= ungrown_render_sample.red() + 8) {
    std::cerr << "sampling parity thumbnail grown=("
              << grown_thumbnail_sample.red() << ','
              << grown_thumbnail_sample.green() << ','
              << grown_thumbnail_sample.blue() << ','
              << grown_thumbnail_sample.alpha() << ") corner=("
              << ungrown_corner_sample.red() << ','
              << ungrown_corner_sample.green() << ','
              << ungrown_corner_sample.blue() << ','
              << ungrown_corner_sample.alpha() << ") render grown=("
              << grown_render_sample.red() << ','
              << grown_render_sample.green() << ','
              << grown_render_sample.blue() << ") corner=("
              << ungrown_render_sample.red() << ','
              << ungrown_render_sample.green() << ','
              << ungrown_render_sample.blue() << ") thumbnail alpha bbox=("
              << sampling_thumbnail_min_x << ','
              << sampling_thumbnail_min_y << ")-("
              << sampling_thumbnail_max_x << ','
              << sampling_thumbnail_max_y << ") max="
              << sampling_thumbnail_max_alpha << '\n';
    return fail("preview thumbnails and rendered output should use the same raster matte grow sampling rules");
  }

  photo_editor::desktop::EditRecipeState generated_cleanup_state;
  auto cleanup_mask = photo_editor::desktop::default_mask_definition(QStringLiteral("object"), 1);
  cleanup_mask.insert(QStringLiteral("name"), QStringLiteral("Cleanup Generated"));
  cleanup_mask.insert(QStringLiteral("generatedMaskMetadata"), generated_quality_metadata);
  QByteArray cleanup_data(36, char(0));
  for (int y = 1; y <= 3; ++y) {
    for (int x = 1; x <= 3; ++x) {
      cleanup_data[y * 6 + x] = char(255);
    }
  }
  cleanup_data[2 * 6 + 2] = char(0);
  cleanup_data[5 * 6 + 5] = char(255);
  cleanup_mask.insert(QStringLiteral("segmentationData"), cleanup_data);
  cleanup_mask.insert(QStringLiteral("segmentationWidth"), 6);
  cleanup_mask.insert(QStringLiteral("segmentationHeight"), 6);
  cleanup_mask.insert(QStringLiteral("generatedMaskHoleFillThreshold"), 0.0);
  cleanup_mask.insert(QStringLiteral("generatedMaskIslandRemovalThreshold"), 0.0);
  generated_cleanup_state.mask_stack = {cleanup_mask};
  generated_cleanup_state.active_mask_index = 0;
  photo_editor::desktop::EditorPreviewController generated_cleanup_controller;
  generated_cleanup_controller.loadAsset(
      QStringLiteral("generated-cleanup-source"),
      ai_mask_path,
      QString(),
      QString(),
      QString(),
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(generated_cleanup_state));
  generated_cleanup_controller.setActiveMaskIndex(0);
  auto uncleaned_mask =
      generated_cleanup_controller.maskStack().at(generated_cleanup_controller.activeMaskIndex()).toMap();
  auto uncleaned_data = uncleaned_mask.value(QStringLiteral("segmentationData")).toByteArray();
  if (static_cast<unsigned char>(uncleaned_data.at(2 * 6 + 2)) != 0U ||
      static_cast<unsigned char>(uncleaned_data.at(5 * 6 + 5)) == 0U) {
    return fail("generated cleanup threshold reset should rebuild from the original matte");
  }
  generated_cleanup_controller.setActiveGeneratedMaskHoleFillThreshold(0.05);
  generated_cleanup_controller.setActiveGeneratedMaskIslandRemovalThreshold(0.05);
  auto cleaned_mask =
      generated_cleanup_controller.maskStack().at(generated_cleanup_controller.activeMaskIndex()).toMap();
  auto cleaned_data = cleaned_mask.value(QStringLiteral("segmentationData")).toByteArray();
  if (static_cast<unsigned char>(cleaned_data.at(2 * 6 + 2)) < 200U) {
    return fail("generated mask cleanup should fill small enclosed holes");
  }
  if (static_cast<unsigned char>(cleaned_data.at(5 * 6 + 5)) != 0U) {
    return fail("generated mask cleanup should remove isolated islands");
  }
  if (cleaned_mask.value(QStringLiteral("generatedMaskHolesFilled")).toInt() != 1 ||
      cleaned_mask.value(QStringLiteral("generatedMaskIslandsRemoved")).toInt() != 1) {
    return fail("generated mask cleanup should report filled holes and removed islands");
  }
  if (!generated_cleanup_controller.activeGeneratedMaskCleanupSummary().contains(QStringLiteral("5.00"))) {
    return fail("generated mask cleanup should expose visible threshold percentages");
  }

  const auto landscape_semantic_stack_count = ai_mask_controller.maskStack().size();
  const auto landscape_semantic_active_kind = ai_mask_controller.activeMaskKind();
  for (const auto& landscape_kind : {
           QStringLiteral("water"),
           QStringLiteral("vegetation"),
           QStringLiteral("mountain"),
           QStringLiteral("ground"),
           QStringLiteral("architecture"),
           QStringLiteral("snow"),
       }) {
    ai_mask_controller.addAIMask(landscape_kind);
    if (ai_mask_controller.maskStack().size() != landscape_semantic_stack_count ||
        ai_mask_controller.activeMaskKind() != landscape_semantic_active_kind ||
        !ai_mask_controller.status().contains(QStringLiteral("landscape semantic masks are disabled")) ||
        !ai_mask_controller.status().contains(QStringLiteral("real landscape segmentation model"))) {
      return fail("unsupported landscape semantic masks should not fall back to foreground mattes");
    }
  }
  const auto generated_refine_target =
      ai_mask_controller.maskStack().at(ai_mask_controller.activeMaskIndex()).toMap();
  const auto generated_refine_target_id =
      generated_refine_target.value(QStringLiteral("id")).toString();
  const auto generated_refine_target_name =
      generated_refine_target.value(QStringLiteral("name")).toString();
  const auto generated_refine_target_set_id =
      generated_refine_target.value(QStringLiteral("maskSetId")).toString();
  const auto generated_refine_stack_count = ai_mask_controller.maskStack().size();
  const auto generated_refine_sets_before = ai_mask_controller.maskSets();
  const auto generated_refine_set_count = generated_refine_sets_before.size();
  const auto generated_refine_source_count =
      generated_refine_sets_before
          .at(generated_refine_set_count - 1)
          .toMap()
          .value(QStringLiteral("sources"))
          .toList()
          .size();
  ai_mask_controller.startAIMaskRefineBrush(false);
  const auto generated_refine_brush =
      ai_mask_controller.maskStack().at(ai_mask_controller.activeMaskIndex()).toMap();
  const auto generated_refine_sources =
      ai_mask_controller.maskSets()
          .at(generated_refine_set_count - 1)
          .toMap()
          .value(QStringLiteral("sources"))
          .toList();
  const auto generated_refine_source =
      generated_refine_sources.isEmpty() ? QVariantMap{} : generated_refine_sources.constLast().toMap();
  if (ai_mask_controller.activeMaskKind() != QStringLiteral("brush") ||
      ai_mask_controller.activeMaskOperation() != QStringLiteral("add") ||
      ai_mask_controller.activeMaskBrushPreset() != QStringLiteral("Edge Aware") ||
      !ai_mask_controller.activeMaskBrushAutoMask() ||
      !almost_equal(ai_mask_controller.activeMaskBrushSize(), 0.026) ||
      ai_mask_controller.maskStack().size() != generated_refine_stack_count + 1 ||
      ai_mask_controller.maskSets().size() != generated_refine_set_count ||
      !generated_refine_brush.value(QStringLiteral("brushRefineMode")).toBool() ||
      generated_refine_brush.value(QStringLiteral("parentId")).toString() !=
          generated_refine_target_id ||
      !generated_refine_brush.value(QStringLiteral("refineBrushRecalculate")).toBool() ||
      !almost_equal(generated_refine_brush.value(QStringLiteral("selectionFineEdge")).toDouble(), 62.0) ||
      !almost_equal(generated_refine_brush.value(QStringLiteral("selectionEdgeContrast")).toDouble(), 18.0) ||
      !almost_equal(generated_refine_brush.value(QStringLiteral("selectionFeather")).toDouble(), 10.0) ||
      !almost_equal(generated_refine_brush.value(QStringLiteral("refineEdgeConfidenceMin")).toDouble(), 0.08) ||
      generated_refine_brush.value(QStringLiteral("brushRefineTargetId")).toString() !=
          generated_refine_target_id ||
      generated_refine_brush.value(QStringLiteral("brushRefineTargetName")).toString() !=
          generated_refine_target_name ||
      generated_refine_brush.value(QStringLiteral("maskSetId")).toString() !=
          generated_refine_target_set_id ||
      generated_refine_sources.size() != generated_refine_source_count + 1 ||
      !generated_refine_source.value(QStringLiteral("dependencyLabel"))
           .toString()
           .contains(generated_refine_target_name) ||
      !generated_refine_source.value(QStringLiteral("sourceAuditLabel"))
           .toString()
           .contains(QStringLiteral("brush refine")) ||
      !generated_refine_source.value(QStringLiteral("sourceAuditLabel"))
           .toString()
           .contains(QStringLiteral("edge recalculation")) ||
      !ai_mask_controller.status().contains(QStringLiteral("Brush Refine Add"))) {
    return fail("AI mask refine brush should add a target-linked edge-recalculation component in the same selection set");
  }
  const auto people_part_stack_count = ai_mask_controller.maskStack().size();
  const auto people_part_active_kind = ai_mask_controller.activeMaskKind();
  for (const auto& people_part_kind : {
           QStringLiteral("person"),
           QStringLiteral("people"),
           QStringLiteral("face"),
           QStringLiteral("hair"),
           QStringLiteral("clothing"),
           QStringLiteral("facial hair"),
           QStringLiteral("lips"),
           QStringLiteral("teeth"),
           QStringLiteral("eyes"),
           QStringLiteral("skin"),
       }) {
    ai_mask_controller.addAIMask(people_part_kind);
    if (ai_mask_controller.maskStack().size() != people_part_stack_count ||
        ai_mask_controller.activeMaskKind() != people_part_active_kind ||
        !ai_mask_controller.status().contains(QStringLiteral("people-part semantic masks are disabled")) ||
        !ai_mask_controller.status().contains(QStringLiteral("real person-part segmentation model"))) {
      return fail("unsupported people-part semantic masks should not fall back to foreground mattes");
    }
  }

  qunsetenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION");
  photo_editor::desktop::EditorPreviewController unavailable_mask_controller;
  unavailable_mask_controller.loadAsset(
      QStringLiteral("ai-mask-unavailable"),
      ai_mask_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  unavailable_mask_controller.addAIMask(QStringLiteral("sky"));
  const auto unavailable_mask_status = unavailable_mask_controller.status();
  qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
  if (!unavailable_mask_status.contains(QStringLiteral("Generated masking is unavailable")) ||
      !unavailable_mask_status.contains(QStringLiteral("sky")) ||
      !unavailable_mask_status.contains(QStringLiteral("Vision semantic masking"))) {
    return fail("generated mask failures should explain the unavailable model/runtime and target");
  }

  const auto alpha_mask_path = runtime_root.filePath(QStringLiteral("imported-alpha-mask.png"));
  QImage alpha_mask_source(4, 2, QImage::Format_ARGB32);
  alpha_mask_source.fill(QColor(255, 255, 255, 0));
  alpha_mask_source.setPixelColor(1, 0, QColor(255, 255, 255, 128));
  alpha_mask_source.setPixelColor(2, 0, QColor(255, 255, 255, 255));
  if (!alpha_mask_source.save(alpha_mask_path)) {
    return fail("alpha mask source image should be writable");
  }
  if (!ai_mask_controller.addAlphaImageMask(QUrl::fromLocalFile(alpha_mask_path).toString()) ||
      ai_mask_controller.activeMaskKind() != QStringLiteral("alpha")) {
    return fail("alpha image masks should import from local image files");
  }
  active_mask = ai_mask_controller.maskStack().at(ai_mask_controller.activeMaskIndex()).toMap();
  if (active_mask.value(QStringLiteral("segmentationData")).toByteArray().size() != 8 ||
      active_mask.value(QStringLiteral("segmentationWidth")).toInt() != 4 ||
      active_mask.value(QStringLiteral("segmentationHeight")).toInt() != 2 ||
      active_mask.value(QStringLiteral("alphaSourcePath")).toString() != alpha_mask_path) {
    return fail("alpha image masks should store imported matte dimensions and source path");
  }
  photo_editor::desktop::EditorPreviewController alpha_import_controller;
  alpha_import_controller.loadAsset(
      QStringLiteral("alpha-import-source"),
      ai_mask_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!alpha_import_controller.beginAlphaImageMaskImport(QUrl::fromLocalFile(alpha_mask_path).toString()) ||
      !alpha_import_controller.alphaMaskImportPreviewActive()) {
    return fail("alpha image masks should support a preview-before-commit import state");
  }
  auto alpha_preview = alpha_import_controller.alphaMaskImportPreview();
  if (alpha_preview.value(QStringLiteral("width")).toInt() != 4 ||
      alpha_preview.value(QStringLiteral("height")).toInt() != 2 ||
      alpha_preview.value(QStringLiteral("selectedPixelCount")).toInt() != 2) {
    return fail("alpha import preview should expose source dimensions and selected pixel count");
  }
  alpha_import_controller.setAlphaMaskImportThreshold(0.75);
  alpha_preview = alpha_import_controller.alphaMaskImportPreview();
  if (alpha_preview.value(QStringLiteral("selectedPixelCount")).toInt() != 1) {
    return fail("alpha import threshold should update the pending preview before commit");
  }
  alpha_import_controller.setAlphaMaskImportInvert(true);
  alpha_preview = alpha_import_controller.alphaMaskImportPreview();
  if (alpha_preview.value(QStringLiteral("selectedPixelCount")).toInt() != 7) {
    return fail("alpha import invert should update the pending preview before commit");
  }
  alpha_import_controller.setAlphaMaskImportResampleMode(QStringLiteral("Match Source"));
  alpha_preview = alpha_import_controller.alphaMaskImportPreview();
  if (alpha_preview.value(QStringLiteral("width")).toInt() != 24 ||
      alpha_preview.value(QStringLiteral("height")).toInt() != 24 ||
      alpha_import_controller.alphaMaskImportResampleMode() != QStringLiteral("Match Source")) {
    return fail("alpha import resampling should preview against the active source dimensions");
  }
  if (!alpha_import_controller.commitAlphaImageMaskImport() ||
      alpha_import_controller.alphaMaskImportPreviewActive() ||
      alpha_import_controller.activeMaskKind() != QStringLiteral("alpha")) {
    return fail("alpha import preview should commit and clear pending state");
  }
  const auto committed_alpha =
      alpha_import_controller.maskStack().at(alpha_import_controller.activeMaskIndex()).toMap();
  if (!committed_alpha.value(QStringLiteral("alphaImportInvert")).toBool() ||
      !almost_equal(committed_alpha.value(QStringLiteral("alphaImportThreshold")).toDouble(), 0.75) ||
      committed_alpha.value(QStringLiteral("alphaImportResampleMode")).toString() != QStringLiteral("Match Source") ||
      committed_alpha.value(QStringLiteral("segmentationWidth")).toInt() != 24 ||
      committed_alpha.value(QStringLiteral("segmentationHeight")).toInt() != 24) {
    return fail("alpha import commit should preserve invert, threshold, resample, and matte dimensions");
  }

  controller.setSharpening(28.0);
  controller.setSharpenRadius(2.4);
  controller.setSharpenDetail(63.0);
  controller.setSharpenMasking(41.0);
  controller.setLuminanceNr(19.0);
  controller.setColorNr(24.0);
  controller.setLensDistortion(-16.0);
  controller.setLensVignette(22.0);
  controller.setChromaticAberration(37.0);
  controller.setPerspectiveVertical(-29.0);
  controller.setPerspectiveHorizontal(18.0);
  if (!almost_equal(controller.sharpening(), 28.0) ||
      !almost_equal(controller.sharpenRadius(), 2.4) ||
      !almost_equal(controller.sharpenDetail(), 63.0) ||
      !almost_equal(controller.sharpenMasking(), 41.0) ||
      !almost_equal(controller.luminanceNr(), 19.0) ||
      !almost_equal(controller.colorNr(), 24.0) ||
      !almost_equal(controller.lensDistortion(), -16.0) ||
      !almost_equal(controller.lensVignette(), 22.0) ||
      !almost_equal(controller.chromaticAberration(), 37.0) ||
      !almost_equal(controller.perspectiveVertical(), -29.0) ||
      !almost_equal(controller.perspectiveHorizontal(), 18.0)) {
    return fail("detail, optics, and perspective controls should retain editable preview state");
  }

  controller.setUprightMode(QStringLiteral("Guided"));
  controller.setUprightVerticalGuideAngle(12.0);
  controller.setUprightHorizontalGuideAngle(-8.0);
  if (controller.uprightMode() != QStringLiteral("Guided") ||
      !almost_equal(controller.uprightVerticalGuideAngle(), 12.0) ||
      !almost_equal(controller.uprightHorizontalGuideAngle(), -8.0) ||
      !almost_equal(controller.perspectiveVertical(), -30.0) ||
      !almost_equal(controller.perspectiveHorizontal(), 20.0)) {
    return fail("guided upright should persist guide angles and derive perspective corrections");
  }

  controller.setTransformRotation(4.5);
  controller.setTransformScale(1.35);
  controller.setTransformSkewX(-22.0);
  controller.setTransformSkewY(14.0);
  controller.setContentAwareCropFill(true);
  if (!almost_equal(controller.transformRotation(), 4.5) ||
      !almost_equal(controller.transformScale(), 1.35) ||
      !almost_equal(controller.transformSkewX(), -22.0) ||
      !almost_equal(controller.transformSkewY(), 14.0) ||
      !controller.contentAwareCropFill()) {
    return fail("transform controls should retain rotate, scale, skew, and crop-fill preview state");
  }

  const auto crop_precision_path = runtime_root.filePath(QStringLiteral("crop-transform-precision.png"));
  QImage crop_precision_source(32, 20, QImage::Format_ARGB32);
  crop_precision_source.fill(QColor::fromRgb(112, 128, 144));
  if (!crop_precision_source.save(crop_precision_path)) {
    return fail("crop precision source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController crop_precision_controller;
  crop_precision_controller.loadAsset(
      QStringLiteral("crop-transform-precision"),
      crop_precision_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  crop_precision_controller.updateCropRect(0.12, 0.14, 0.52, 0.62);
  crop_precision_controller.setPerspectiveVertical(-18.0);
  crop_precision_controller.setPerspectiveHorizontal(12.0);
  crop_precision_controller.setTransformRotation(4.5);
  crop_precision_controller.setTransformScale(1.35);
  crop_precision_controller.setTransformSkewX(-22.0);
  crop_precision_controller.setTransformSkewY(14.0);
  crop_precision_controller.setLiquifyRadius(0.42);
  crop_precision_controller.setLiquifyStrength(36.0);
  crop_precision_controller.setContentAwareCropFill(true);
  crop_precision_controller.resetCropRect();
  if (!almost_equal(crop_precision_controller.cropRectX(), 0.0) ||
      !almost_equal(crop_precision_controller.cropRectY(), 0.0) ||
      !almost_equal(crop_precision_controller.cropRectWidth(), 1.0) ||
      !almost_equal(crop_precision_controller.cropRectHeight(), 1.0) ||
      !almost_equal(crop_precision_controller.transformRotation(), 4.5) ||
      !almost_equal(crop_precision_controller.transformScale(), 1.35) ||
      !almost_equal(crop_precision_controller.transformSkewX(), -22.0) ||
      !almost_equal(crop_precision_controller.transformSkewY(), 14.0) ||
      !almost_equal(crop_precision_controller.perspectiveVertical(), -18.0) ||
      !almost_equal(crop_precision_controller.perspectiveHorizontal(), 12.0) ||
      !almost_equal(crop_precision_controller.liquifyRadius(), 0.42) ||
      !almost_equal(crop_precision_controller.liquifyStrength(), 36.0) ||
      !crop_precision_controller.contentAwareCropFill()) {
    return fail("reset crop should preserve transform, perspective, liquify, and crop-fill state");
  }
  crop_precision_controller.updateCropRect(0.10, 0.20, 0.50, 0.60);
  if (!crop_precision_controller.resetTransform() ||
      !almost_equal(crop_precision_controller.cropRectX(), 0.10) ||
      !almost_equal(crop_precision_controller.cropRectY(), 0.20) ||
      !almost_equal(crop_precision_controller.cropRectWidth(), 0.50) ||
      !almost_equal(crop_precision_controller.cropRectHeight(), 0.60) ||
      !almost_equal(crop_precision_controller.transformRotation(), 0.0) ||
      !almost_equal(crop_precision_controller.transformScale(), 1.0) ||
      !almost_equal(crop_precision_controller.transformSkewX(), 0.0) ||
      !almost_equal(crop_precision_controller.transformSkewY(), 0.0) ||
      !almost_equal(crop_precision_controller.perspectiveVertical(), 0.0) ||
      !almost_equal(crop_precision_controller.perspectiveHorizontal(), 0.0) ||
      !almost_equal(crop_precision_controller.liquifyRadius(), 0.25) ||
      !almost_equal(crop_precision_controller.liquifyStrength(), 0.0) ||
      crop_precision_controller.contentAwareCropFill()) {
    return fail("reset transform should preserve explicit crop geometry while clearing transform state");
  }
  if (crop_precision_controller.resetTransform()) {
    return fail("reset transform should report false when transform state is already neutral");
  }

  controller.deselectActiveMask();
  if (controller.hasActiveMask() || controller.activeMaskIndex() != -1) {
    return fail("deselect should clear the active mask focus without deleting masks");
  }

  controller.setTemperature(22.0);
  if (!controller.copyCurrentSettings() || !controller.hasCopiedSettings()) {
    return fail("copying settings should populate the edit clipboard");
  }

  controller.setExposure(0.0);
  controller.setTemperature(0.0);
  if (!controller.pasteCopiedSettings({QStringLiteral("color")})) {
    return fail("pasting copied settings should support partial adjustment groups");
  }
  if (!almost_equal(controller.exposure(), 0.0)) {
    std::cerr << "exposure after color paste: " << controller.exposure() << '\n';
    return fail("partial paste should preserve excluded tone controls");
  }
  if (!almost_equal(controller.temperature(), 22.0)) {
    std::cerr << "temperature after color paste: " << controller.temperature() << '\n';
    return fail("partial paste should apply included color controls");
  }

  controller.setRawExposure(1.25);
  controller.setRawBoost(0.55);
  controller.setRawShadowBoost(1.4);
  controller.setRawChromaNoiseReduction(0.62);
  controller.setRawMoireReduction(0.37);
  controller.setRawHotDeadPixelCleanup(0.41);
  controller.setRawFalseColorSuppression(0.29);
  controller.setRawDemosaicQuality(QStringLiteral("Full"));
  controller.setRawCameraMatchingLook(QStringLiteral("Portrait"));
  controller.setRawWhiteBalanceOverride(true);
  controller.setRawWhiteBalanceTemperature(6400.0);
  controller.setRawWhiteBalanceTint(-12.0);
  controller.setRawBlackLevel(0.016);
  controller.setRawWhiteLevel(1.16);
  controller.setRawCalibrationHue(-11.0);
  controller.setRawCalibrationSaturation(21.0);
  controller.setRawRedPrimaryHue(7.0);
  controller.setRawRedPrimarySaturation(18.0);
  controller.setRawGreenPrimaryHue(-9.0);
  controller.setRawGreenPrimarySaturation(-12.0);
  controller.setRawBluePrimaryHue(14.0);
  controller.setRawBluePrimarySaturation(16.0);
  if (!controller.copyCurrentSettings()) {
    return fail("copying settings should retain RAW decode controls");
  }

  controller.setRawExposure(0.0);
  controller.setRawBoost(0.0);
  controller.setRawShadowBoost(1.0);
  controller.setRawChromaNoiseReduction(0.0);
  controller.setRawMoireReduction(0.0);
  controller.setRawHotDeadPixelCleanup(0.0);
  controller.setRawFalseColorSuppression(0.0);
  controller.setRawDemosaicQuality(QStringLiteral("Balanced"));
  controller.setRawCameraMatchingLook(QStringLiteral("Standard"));
  controller.setRawWhiteBalanceOverride(false);
  controller.setRawWhiteBalanceTemperature(5200.0);
  controller.setRawWhiteBalanceTint(0.0);
  controller.setRawBlackLevel(0.0);
  controller.setRawWhiteLevel(1.0);
  controller.setRawCalibrationHue(0.0);
  controller.setRawCalibrationSaturation(0.0);
  controller.setRawRedPrimaryHue(0.0);
  controller.setRawRedPrimarySaturation(0.0);
  controller.setRawGreenPrimaryHue(0.0);
  controller.setRawGreenPrimarySaturation(0.0);
  controller.setRawBluePrimaryHue(0.0);
  controller.setRawBluePrimarySaturation(0.0);
  controller.setExposure(-0.25);
  if (!controller.pasteCopiedSettings({QStringLiteral("raw")})) {
    return fail("pasting copied settings should support RAW-only adjustment groups");
  }
  if (!almost_equal(controller.rawExposure(), 1.25) ||
      !almost_equal(controller.rawBoost(), 0.55) ||
      !almost_equal(controller.rawShadowBoost(), 1.4) ||
      !almost_equal(controller.rawChromaNoiseReduction(), 0.62) ||
      !almost_equal(controller.rawMoireReduction(), 0.37) ||
      !almost_equal(controller.rawHotDeadPixelCleanup(), 0.41) ||
      !almost_equal(controller.rawFalseColorSuppression(), 0.29) ||
      controller.rawDemosaicQuality() != QStringLiteral("Full") ||
      controller.rawCameraMatchingLook() != QStringLiteral("Portrait") ||
      !controller.rawWhiteBalanceOverride() ||
      !almost_equal(controller.rawWhiteBalanceTemperature(), 6400.0) ||
      !almost_equal(controller.rawWhiteBalanceTint(), -12.0) ||
      !almost_equal(controller.rawBlackLevel(), 0.016) ||
      !almost_equal(controller.rawWhiteLevel(), 1.16) ||
      !almost_equal(controller.rawCalibrationHue(), -11.0) ||
      !almost_equal(controller.rawCalibrationSaturation(), 21.0) ||
      !almost_equal(controller.rawRedPrimaryHue(), 7.0) ||
      !almost_equal(controller.rawRedPrimarySaturation(), 18.0) ||
      !almost_equal(controller.rawGreenPrimaryHue(), -9.0) ||
      !almost_equal(controller.rawGreenPrimarySaturation(), -12.0) ||
      !almost_equal(controller.rawBluePrimaryHue(), 14.0) ||
      !almost_equal(controller.rawBluePrimarySaturation(), 16.0) ||
      !almost_equal(controller.exposure(), -0.25)) {
    return fail("raw-only paste should restore RAW decode controls without touching tone controls");
  }

  controller.setTexture(18.0);
  controller.setDehaze(11.0);
  controller.setRawDetail(0.72);
  controller.setRawChromaNoiseReduction(0.48);
  controller.setRawMoireReduction(0.26);
  controller.setRawHotDeadPixelCleanup(0.34);
  controller.setRawFalseColorSuppression(0.39);
  controller.setRawCalibrationHue(13.0);
  controller.setRawCalibrationSaturation(-17.0);
  controller.setRawRedPrimaryHue(-6.0);
  controller.setRawRedPrimarySaturation(22.0);
  controller.setRawGreenPrimaryHue(8.0);
  controller.setRawGreenPrimarySaturation(-11.0);
  controller.setRawBluePrimaryHue(10.0);
  controller.setRawBluePrimarySaturation(15.0);
  controller.setRawDemosaicQuality(QStringLiteral("Preview"));
  controller.setRawCameraMatchingLook(QStringLiteral("Neutral"));
  controller.setRawLensCorrection(false);
  if (!controller.savePreset(QStringLiteral("Portrait Base"), true) ||
      !controller.presetNames().contains(QStringLiteral("Portrait Base"))) {
    return fail("saving a preset should persist it in the controller");
  }
  if (!controller.savePresetToPack(QStringLiteral("Client Warmth"), QStringLiteral("Client Pack"), true) ||
      !controller.presetNames().contains(QStringLiteral("Client Warmth")) ||
      !controller.presetPackNames().contains(QStringLiteral("Client Pack")) ||
      controller.presetPackName(QStringLiteral("Client Warmth")) != QStringLiteral("Client Pack") ||
      controller.presetCategoryName(QStringLiteral("Client Warmth")) != QStringLiteral("Client Pack") ||
      !controller.searchPresetNames(QStringLiteral("Client Pack")).contains(QStringLiteral("Client Warmth")) ||
      !controller.presetThumbnailSource(QStringLiteral("Client Warmth"))
           .startsWith(QStringLiteral("data:image/png;base64,"))) {
    return fail("saving a user preset should support custom preset packs");
  }
  if (!controller.savePresetToPack(QStringLiteral("Client Organized"), QStringLiteral("Working Pack"), true) ||
      !controller.isUserPreset(QStringLiteral("Client Organized"))) {
    return fail("saved presets should be identified as user-editable presets");
  }
  controller.setPresetFavorite(QStringLiteral("Client Organized"), true);
  if (!controller.renamePreset(QStringLiteral("Client Organized"), QStringLiteral("Client Organized Final")) ||
      controller.presetNames().contains(QStringLiteral("Client Organized")) ||
      !controller.presetNames().contains(QStringLiteral("Client Organized Final")) ||
      !controller.isPresetFavorite(QStringLiteral("Client Organized Final")) ||
      controller.renamePreset(QStringLiteral("Client Organized Final"), QStringLiteral("Portrait Base")) ||
      controller.renamePreset(QStringLiteral("Landscape Pop"), QStringLiteral("Landscape Copy"))) {
    return fail("user presets should be renameable without clobbering other presets or built-ins");
  }
  if (!controller.movePresetToPack(QStringLiteral("Client Organized Final"), QStringLiteral("Delivered Pack")) ||
      controller.presetPackName(QStringLiteral("Client Organized Final")) != QStringLiteral("Delivered Pack") ||
      controller.presetCategoryName(QStringLiteral("Client Organized Final")) != QStringLiteral("Delivered Pack") ||
      !controller.searchPresetNames(QStringLiteral("Delivered Pack")).contains(QStringLiteral("Client Organized Final")) ||
      controller.movePresetToPack(QStringLiteral("Landscape Pop"), QStringLiteral("Delivered Pack"))) {
    return fail("user presets should be movable between organizational packs");
  }
  if (!controller.deletePreset(QStringLiteral("Client Organized Final")) ||
      controller.presetNames().contains(QStringLiteral("Client Organized Final"))) {
    return fail("organized user presets should remain deletable after rename and move");
  }
  const auto exported_pack_path = runtime_root.filePath(QStringLiteral("client-pack.photoeditor-presets.json"));
  if (!controller.exportPresetPack(exported_pack_path, QStringLiteral("Client Pack")) ||
      !QFileInfo::exists(exported_pack_path)) {
    return fail("user preset packs should export to a portable preset pack file");
  }
  if (!controller.deletePreset(QStringLiteral("Client Warmth")) ||
      controller.presetNames().contains(QStringLiteral("Client Warmth"))) {
    return fail("user presets should be removable before pack import");
  }
  if (!controller.importPresetPack(exported_pack_path, true) ||
      !controller.presetNames().contains(QStringLiteral("Client Warmth")) ||
      controller.presetPackName(QStringLiteral("Client Warmth")) != QStringLiteral("Client Pack") ||
      controller.presetCategoryName(QStringLiteral("Client Warmth")) != QStringLiteral("Client Pack")) {
    return fail("user preset packs should import from exported preset pack files");
  }
  controller.setPresetFavorite(QStringLiteral("Client Warmth"), true);
  if (!controller.isPresetFavorite(QStringLiteral("Client Warmth")) ||
      !controller.favoritePresetNames().contains(QStringLiteral("Client Warmth"))) {
    return fail("user preset favorites should be saved with preset metadata");
  }
  const auto sync_folder_path = runtime_root.filePath(QStringLiteral("preset-sync"));
  if (!controller.presetSyncManifestJson().contains(QStringLiteral("photo-editor-preset-sync")) ||
      !controller.exportPresetSyncBundle(sync_folder_path)) {
    return fail("preset sync hooks should export a portable sync manifest");
  }
  if (!controller.deletePreset(QStringLiteral("Client Warmth")) ||
      controller.presetNames().contains(QStringLiteral("Client Warmth"))) {
    return fail("sync import test should be able to remove the local preset first");
  }
  if (!controller.importPresetSyncBundle(sync_folder_path, true) ||
      controller.presetPackName(QStringLiteral("Client Warmth")) != QStringLiteral("Client Pack") ||
      controller.presetCategoryName(QStringLiteral("Client Warmth")) != QStringLiteral("Client Pack") ||
      !controller.isPresetFavorite(QStringLiteral("Client Warmth"))) {
    return fail("preset sync hooks should restore presets and metadata from a sync folder");
  }

  controller.setTexture(0.0);
  controller.setDehaze(0.0);
  controller.setRawDetail(0.0);
  controller.setRawChromaNoiseReduction(0.0);
  controller.setRawMoireReduction(0.0);
  controller.setRawHotDeadPixelCleanup(0.0);
  controller.setRawFalseColorSuppression(0.0);
  controller.setRawCalibrationHue(0.0);
  controller.setRawCalibrationSaturation(0.0);
  controller.setRawRedPrimaryHue(0.0);
  controller.setRawRedPrimarySaturation(0.0);
  controller.setRawGreenPrimaryHue(0.0);
  controller.setRawGreenPrimarySaturation(0.0);
  controller.setRawBluePrimaryHue(0.0);
  controller.setRawBluePrimarySaturation(0.0);
  controller.setRawDemosaicQuality(QStringLiteral("Balanced"));
  controller.setRawCameraMatchingLook(QStringLiteral("Standard"));
  controller.setRawLensCorrection(true);
  controller.setExposure(-0.5);
  controller.setTemperature(0.0);
  if (!controller.applyPreset(QStringLiteral("Portrait Base"), {QStringLiteral("detail")})) {
    return fail("preset application should support partial adjustment groups");
  }
  if (!almost_equal(controller.texture(), 18.0) || !almost_equal(controller.dehaze(), 11.0)) {
    return fail("detail-only preset application should restore the saved detail controls");
  }
  if (!almost_equal(controller.temperature(), 0.0)) {
    return fail("partial preset application should not touch excluded color controls");
  }

  if (!controller.applyPreset(QStringLiteral("Portrait Base"), {QStringLiteral("raw")})) {
    return fail("preset application should support RAW-only adjustment groups");
  }
  if (!almost_equal(controller.rawDetail(), 0.72) ||
      !almost_equal(controller.rawChromaNoiseReduction(), 0.48) ||
      !almost_equal(controller.rawMoireReduction(), 0.26) ||
      !almost_equal(controller.rawHotDeadPixelCleanup(), 0.34) ||
      !almost_equal(controller.rawFalseColorSuppression(), 0.39) ||
      !almost_equal(controller.rawCalibrationHue(), 13.0) ||
      !almost_equal(controller.rawCalibrationSaturation(), -17.0) ||
      !almost_equal(controller.rawRedPrimaryHue(), -6.0) ||
      !almost_equal(controller.rawRedPrimarySaturation(), 22.0) ||
      !almost_equal(controller.rawGreenPrimaryHue(), 8.0) ||
      !almost_equal(controller.rawGreenPrimarySaturation(), -11.0) ||
      !almost_equal(controller.rawBluePrimaryHue(), 10.0) ||
      !almost_equal(controller.rawBluePrimarySaturation(), 15.0) ||
      controller.rawDemosaicQuality() != QStringLiteral("Preview") ||
      controller.rawCameraMatchingLook() != QStringLiteral("Neutral") ||
      controller.rawLensCorrection() ||
      !almost_equal(controller.texture(), 18.0) ||
      !almost_equal(controller.dehaze(), 11.0)) {
    return fail("raw-only preset application should restore RAW decode controls without touching detail controls");
  }

  controller.setPreviewTargetEdge(1400);
  const auto reference_cache_before_exposure =
      photo_editor::desktop::EditorPreviewControllerTestAccess::cache_path_for_selection(
          controller,
          false,
          false);
  controller.setExposure(0.4);
  const auto reference_cache_after_exposure =
      photo_editor::desktop::EditorPreviewControllerTestAccess::cache_path_for_selection(
          controller,
          false,
          false);
  if (reference_cache_before_exposure != reference_cache_after_exposure) {
    return fail("reference preview cache should stay stable across non-RAW, non-crop adjustment changes");
  }

  const auto edited_cache_before_crop =
      photo_editor::desktop::EditorPreviewControllerTestAccess::cache_path_for_selection(
          controller,
          true,
          false);
  controller.setCropPreset(QStringLiteral("4x5"));
  const auto reference_cache_after_crop =
      photo_editor::desktop::EditorPreviewControllerTestAccess::cache_path_for_selection(
          controller,
          false,
          false);
  const auto edited_cache_after_crop =
      photo_editor::desktop::EditorPreviewControllerTestAccess::cache_path_for_selection(
          controller,
          true,
          false);
  if (reference_cache_after_crop == reference_cache_after_exposure ||
      edited_cache_after_crop == edited_cache_before_crop) {
    return fail("preview cache paths should still change when crop-dependent content changes");
  }
  controller.setCropPreset(QStringLiteral("1:1"));
  if (!almost_equal(controller.cropAspectRatio(), 1.0)) {
    return fail("square aspect presets should support 1:1 ratio labels");
  }
  controller.setCropPreset(QStringLiteral("9:16"));
  if (!almost_equal(controller.cropAspectRatio(), 9.0 / 16.0)) {
    return fail("portrait aspect presets should parse colon-delimited ratio labels");
  }
  const auto available_crop_presets = controller.cropAspectPresets();
  if (!available_crop_presets.contains(QStringLiteral("9:16")) ||
      !available_crop_presets.contains(QStringLiteral("5:4"))) {
    return fail("crop aspect presets should expose common landscape and portrait ratios");
  }
  if (!available_crop_presets.contains(QStringLiteral("8x10 in")) ||
      !available_crop_presets.contains(QStringLiteral("A4 Landscape"))) {
    return fail("crop aspect presets should expose common print sizes");
  }
  photo_editor::desktop::EditRecipeState portrait_crop_state;
  portrait_crop_state.crop_preset = QStringLiteral("9:16");
  const auto portrait_crop_rect =
      photo_editor::desktop::crop_rect_for_image(QSize(1600, 1000), portrait_crop_state);
  if (portrait_crop_rect.width() < 560 || portrait_crop_rect.width() > 565 ||
      portrait_crop_rect.height() != 1000) {
    return fail("render crop utilities should apply expanded aspect presets consistently");
  }
  controller.setCropPreset(QStringLiteral("8x10 in"));
  if (!almost_equal(controller.cropAspectRatio(), 0.8)) {
    return fail("print crop presets should parse inch-based portrait ratios");
  }
  controller.setCropPreset(QStringLiteral("A4 Landscape"));
  if (!almost_equal(controller.cropAspectRatio(), 297.0 / 210.0)) {
    return fail("print crop presets should parse ISO paper landscape ratios");
  }
  photo_editor::desktop::EditRecipeState print_crop_state;
  print_crop_state.crop_preset = QStringLiteral("8x10 in");
  const auto print_crop_rect =
      photo_editor::desktop::crop_rect_for_image(QSize(1600, 1000), print_crop_state);
  if (print_crop_rect.width() != 800 || print_crop_rect.height() != 1000) {
    return fail("render crop utilities should apply print size crops consistently");
  }
  if (!controller.addCustomCropAspectPreset(QStringLiteral("2.39:1")) ||
      controller.addCustomCropAspectPreset(QStringLiteral("2.39x1"))) {
    return fail("custom crop aspect presets should be addable without duplicate aliases");
  }
  if (!controller.cropAspectPresets().contains(QStringLiteral("2.39:1")) ||
      !controller.customCropAspectPresets().contains(QStringLiteral("2.39:1"))) {
    return fail("custom crop aspect presets should appear in the merged crop preset list");
  }
  controller.setCropPreset(QStringLiteral("2.39:1"));
  if (!almost_equal(controller.cropAspectRatio(), 2.39)) {
    return fail("custom crop aspect presets should drive crop aspect ratio parsing");
  }
  const auto crop_preset_bundle_path = runtime_root.filePath(QStringLiteral("crop-presets.json"));
  if (!controller.exportCustomCropAspectPresets(crop_preset_bundle_path)) {
    return fail("custom crop aspect presets should export to a reusable preset bundle");
  }
  photo_editor::desktop::EditorPreviewController persisted_crop_controller;
  if (!persisted_crop_controller.customCropAspectPresets().contains(QStringLiteral("2.39:1")) ||
      !persisted_crop_controller.removeCustomCropAspectPreset(QStringLiteral("2.39x1"))) {
    return fail("custom crop aspect presets should persist and be removable by ratio alias");
  }
  photo_editor::desktop::EditorPreviewController removed_crop_controller;
  if (removed_crop_controller.customCropAspectPresets().contains(QStringLiteral("2.39:1"))) {
    return fail("removed custom crop aspect presets should not reload from disk");
  }
  if (removed_crop_controller.importCustomCropAspectPresets(crop_preset_bundle_path) != 1 ||
      !removed_crop_controller.customCropAspectPresets().contains(QStringLiteral("2.39:1")) ||
      removed_crop_controller.importCustomCropAspectPresets(crop_preset_bundle_path) != 0) {
    return fail("custom crop aspect preset imports should restore exported presets without duplicates");
  }
  if (!removed_crop_controller.addCustomCropAspectPreset(QStringLiteral("7:5")) ||
      removed_crop_controller.importCustomCropAspectPresets(crop_preset_bundle_path, true) != 1 ||
      !removed_crop_controller.customCropAspectPresets().contains(QStringLiteral("2.39:1")) ||
      removed_crop_controller.customCropAspectPresets().contains(QStringLiteral("7:5"))) {
    return fail("custom crop aspect preset imports should optionally replace existing custom presets");
  }
  photo_editor::desktop::EditorPreviewController horizon_controller;
  horizon_controller.loadAsset(
      QStringLiteral("horizon-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  constexpr auto kPi = 3.14159265358979323846;
  const auto horizon_angle = std::atan2(0.2, 0.8) * 180.0 / kPi;
  if (!horizon_controller.straightenFromHorizon(0.1, 0.3, 0.9, 0.5) ||
      !almost_equal(horizon_controller.transformRotation(), -horizon_angle, 1e-5) ||
      !almost_equal(horizon_controller.uprightHorizontalGuideAngle(), horizon_angle, 1e-5)) {
    return fail("straighten from horizon should convert a marked horizon line into a rotation correction");
  }
  horizon_controller.undo();
  if (!almost_equal(horizon_controller.transformRotation(), 0.0) ||
      !almost_equal(horizon_controller.uprightHorizontalGuideAngle(), 0.0)) {
    return fail("straighten from horizon should be undoable");
  }
  if (!horizon_controller.straightenFromHorizon(0.9, 0.5, 0.1, 0.3) ||
      !almost_equal(horizon_controller.transformRotation(), -horizon_angle, 1e-5)) {
    return fail("straighten from horizon should treat reversed guide endpoints consistently");
  }
  const auto rotation_before_invalid_horizon = horizon_controller.transformRotation();
  if (horizon_controller.straightenFromHorizon(0.4, 0.4, 0.4, 0.4) ||
      !almost_equal(horizon_controller.transformRotation(), rotation_before_invalid_horizon)) {
    return fail("straighten from horizon should reject zero-length horizon guides");
  }

  const auto wb_histogram_path = runtime_root.filePath(QStringLiteral("wb-histogram-source.png"));
  QImage wb_histogram_source(48, 48, QImage::Format_ARGB32);
  wb_histogram_source.fill(QColor(122, 122, 122));
  for (int y = 0; y < wb_histogram_source.height(); ++y) {
    for (int x = 0; x < 8; ++x) {
      wb_histogram_source.setPixelColor(x, y, Qt::black);
    }
    for (int x = wb_histogram_source.width() - 8; x < wb_histogram_source.width(); ++x) {
      wb_histogram_source.setPixelColor(x, y, Qt::white);
    }
  }
  for (int y = 20; y <= 28; ++y) {
    for (int x = 20; x <= 28; ++x) {
      wb_histogram_source.setPixelColor(x, y, QColor(230, 210, 180));
    }
  }
  wb_histogram_source.setPixelColor(24, 24, QColor(90, 120, 255));
  if (!wb_histogram_source.save(wb_histogram_path)) {
    return fail("wb histogram source image should be writable");
  }

  controller.loadAsset(
      QStringLiteral("asset-wb-histogram"),
      wb_histogram_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(controller)) {
    return fail("wb histogram preview should finish rendering");
  }
  if (!wait_for_histogram_idle(controller)) {
    return fail("wb histogram preview should finish histogram update");
  }
  if (controller.histogramSampleCount() <= 0 ||
      controller.histogramShadowClipCount() <= 0 ||
      controller.histogramHighlightClipCount() <= 0) {
    return fail("preview histogram should report sample count and clipping diagnostics");
  }

  QImage preview_clip_thresholds(6, 1, QImage::Format_ARGB32);
  preview_clip_thresholds.setPixelColor(0, 0, QColor(0, 12, 12));
  preview_clip_thresholds.setPixelColor(1, 0, QColor(1, 12, 12));
  preview_clip_thresholds.setPixelColor(2, 0, QColor(254, 254, 254));
  preview_clip_thresholds.setPixelColor(3, 0, QColor(255, 12, 12));
  preview_clip_thresholds.setPixelColor(4, 0, QColor(12, 0, 255));
  preview_clip_thresholds.setPixelColor(5, 0, QColor(255, 255, 255));
  photo_editor::desktop::EditorPreviewControllerTestAccess::compute_histogram(
      controller,
      preview_clip_thresholds);
  const auto red_clip_counts = controller.histogramRedCounts();
  if (controller.histogramSampleCount() != 6 || controller.histogramShadowClipCount() != 2 ||
      controller.histogramHighlightClipCount() != 3 || red_clip_counts.size() != 256 ||
      red_clip_counts.at(0).toInt() != 1 || red_clip_counts.at(1).toInt() != 1 ||
      red_clip_counts.at(254).toInt() != 1 || red_clip_counts.at(255).toInt() != 2) {
    return fail(
        "preview clipping thresholds should count only exact 8-bit channel endpoints 0 and 255");
  }

  QImage raw_clip_thresholds(5, 1, QImage::Format_RGBA64);
  raw_clip_thresholds.setPixelColor(0, 0, QColor::fromRgbF(0.0, 0.5, 0.5));
  raw_clip_thresholds.setPixelColor(1, 0, QColor::fromRgbF(2.0F / 65535.0F, 0.5F, 0.5F));
  raw_clip_thresholds.setPixelColor(2, 0, QColor::fromRgbF(1.0, 0.5, 0.5));
  raw_clip_thresholds.setPixelColor(
      3,
      0,
      QColor::fromRgbF(1.0F - 2.0F / 65535.0F, 0.5F, 0.5F));
  raw_clip_thresholds.setPixelColor(4, 0, QColor::fromRgbF(0.0, 1.0, 0.5));
  photo_editor::desktop::EditorPreviewControllerTestAccess::update_raw_clipping_warnings(
      controller,
      raw_clip_thresholds);
  if (controller.rawClipSampleCount() != 5 || controller.rawShadowClipCount() != 2 ||
      controller.rawHighlightClipCount() != 2) {
    return fail(
        "raw clipping thresholds should count only 16-bit endpoint-equivalent shadow and highlight samples");
  }
  controller.setHighlightReconstructionMode(QStringLiteral("Camera Default"));
  if (controller.highlightReconstructionMode() != QStringLiteral("Camera Default") ||
      !controller.rawHighlightRecovery()) {
    return fail("controller should expose camera-default highlight reconstruction mode");
  }
  controller.setRawHighlightRecovery(false);
  if (controller.highlightReconstructionMode() != QStringLiteral("Disabled")) {
    return fail("legacy RAW highlight recovery toggle should select the disabled reconstruction mode");
  }
  controller.setRawHighlightRecovery(true);
  if (controller.highlightReconstructionMode() != QStringLiteral("Enabled")) {
    return fail("legacy RAW highlight recovery toggle should restore the enabled reconstruction mode");
  }

  photo_editor::desktop::RawDecodeSettings preview_demosaic_settings;
  preview_demosaic_settings.demosaic_quality = QStringLiteral("Preview");
  photo_editor::desktop::RawDecodeSettings balanced_demosaic_settings;
  balanced_demosaic_settings.demosaic_quality = QStringLiteral("Balanced");
  photo_editor::desktop::RawDecodeSettings full_demosaic_settings;
  full_demosaic_settings.demosaic_quality = QStringLiteral("Full");
  full_demosaic_settings.chroma_noise_reduction_amount = 1.8;
  full_demosaic_settings.moire_reduction_amount = -0.5;
  full_demosaic_settings.hot_dead_pixel_cleanup_amount = 1.2;
  full_demosaic_settings.false_color_suppression_amount = -0.8;
  if (!photo_editor::desktop::raw_decode_uses_draft_demosaic(preview_demosaic_settings, false) ||
      !photo_editor::desktop::raw_decode_uses_draft_demosaic(balanced_demosaic_settings, true) ||
      photo_editor::desktop::raw_decode_uses_draft_demosaic(full_demosaic_settings, true) ||
      !almost_equal(
          photo_editor::desktop::raw_decode_chroma_noise_reduction_amount(full_demosaic_settings),
          1.0) ||
      !almost_equal(
          photo_editor::desktop::raw_decode_moire_reduction_amount(full_demosaic_settings),
          0.0) ||
      !almost_equal(
          photo_editor::desktop::raw_decode_hot_dead_pixel_cleanup_amount(full_demosaic_settings),
          1.0) ||
      !almost_equal(
          photo_editor::desktop::raw_decode_false_color_suppression_amount(full_demosaic_settings),
          0.0)) {
    return fail("RAW demosaic quality modes should select draft/full decode behavior");
  }

  QImage hot_pixel_test(5, 5, QImage::Format_RGBA64);
  hot_pixel_test.fill(QColor::fromRgbF(0.22F, 0.22F, 0.22F));
  hot_pixel_test.setPixelColor(2, 2, QColor::fromRgbF(1.0F, 1.0F, 1.0F));
  if (!photo_editor::desktop::apply_raw_hot_dead_pixel_cleanup(&hot_pixel_test, 1.0) ||
      hot_pixel_test.pixelColor(2, 2).redF() > 0.35) {
    return fail("RAW hot/dead pixel cleanup should repair isolated bright pixels");
  }

  QImage dead_pixel_test(5, 5, QImage::Format_RGBA64);
  dead_pixel_test.fill(QColor::fromRgbF(0.72F, 0.72F, 0.72F));
  dead_pixel_test.setPixelColor(2, 2, QColor::fromRgbF(0.0F, 0.0F, 0.0F));
  if (!photo_editor::desktop::apply_raw_hot_dead_pixel_cleanup(&dead_pixel_test, 1.0) ||
      dead_pixel_test.pixelColor(2, 2).redF() < 0.55) {
    return fail("RAW hot/dead pixel cleanup should repair isolated dark pixels");
  }

  QImage normal_pixel_test(5, 5, QImage::Format_RGBA64);
  normal_pixel_test.fill(QColor::fromRgbF(0.22F, 0.22F, 0.22F));
  normal_pixel_test.setPixelColor(2, 2, QColor::fromRgbF(0.24F, 0.24F, 0.24F));
  if (photo_editor::desktop::apply_raw_hot_dead_pixel_cleanup(&normal_pixel_test, 1.0) ||
      normal_pixel_test.pixelColor(2, 2).redF() < 0.23) {
    return fail("RAW hot/dead pixel cleanup should preserve normal local texture");
  }

  QImage false_color_test(5, 5, QImage::Format_RGBA64);
  false_color_test.fill(QColor::fromRgbF(0.48F, 0.48F, 0.48F));
  false_color_test.setPixelColor(2, 2, QColor::fromRgbF(0.95F, 0.05F, 0.05F));
  if (!photo_editor::desktop::apply_raw_false_color_suppression(&false_color_test, 1.0) ||
      false_color_test.pixelColor(2, 2).saturationF() > 0.25F) {
    return fail("RAW false-color suppression should neutralize isolated chroma outliers");
  }

  QImage neutral_detail_test(5, 5, QImage::Format_RGBA64);
  neutral_detail_test.fill(QColor::fromRgbF(0.48F, 0.48F, 0.48F));
  neutral_detail_test.setPixelColor(2, 2, QColor::fromRgbF(0.55F, 0.55F, 0.55F));
  if (photo_editor::desktop::apply_raw_false_color_suppression(&neutral_detail_test, 1.0) ||
      neutral_detail_test.pixelColor(2, 2).redF() < 0.54F) {
    return fail("RAW false-color suppression should preserve neutral luminance detail");
  }

  photo_editor::desktop::EditorPreviewControllerTestAccess::set_raw_as_shot_white_balance(
      controller,
      7350.0,
      -21.0);
  if (!almost_equal(controller.rawAsShotWhiteBalanceTemperature(), 7350.0) ||
      !almost_equal(controller.rawAsShotWhiteBalanceTint(), -21.0)) {
    return fail("controller should expose decoded RAW as-shot white balance metadata");
  }
  controller.setRawWhiteBalanceOverride(false);
  controller.setRawWhiteBalanceTemperature(5200.0);
  controller.setRawWhiteBalanceTint(0.0);
  controller.useAsShotRawWhiteBalance();
  if (!controller.rawWhiteBalanceOverride() ||
      !almost_equal(controller.rawWhiteBalanceTemperature(), 7350.0) ||
      !almost_equal(controller.rawWhiteBalanceTint(), -21.0)) {
    return fail("as-shot RAW white balance action should enable the override and copy decoded metadata");
  }

  QImage raw_clipping_source(12, 10, QImage::Format_ARGB32);
  raw_clipping_source.fill(QColor(128, 128, 128));
  raw_clipping_source.setPixelColor(1, 1, Qt::black);
  raw_clipping_source.setPixelColor(2, 2, Qt::white);
  photo_editor::desktop::EditorPreviewControllerTestAccess::update_raw_clipping_warnings(
      controller,
      raw_clipping_source);
  if (controller.rawClipSampleCount() != raw_clipping_source.width() * raw_clipping_source.height() ||
      controller.rawShadowClipCount() != 1 ||
      controller.rawHighlightClipCount() != 1) {
    return fail("RAW clipping warnings should report source shadow and highlight clipping separately");
  }

  controller.setWhiteBalanceSampling(true);
  controller.sampleWhiteBalanceAt(0.5, 0.5);
  if (controller.whiteBalanceSampling()) {
    return fail("white balance sampling should exit sampling mode after applying a patch sample");
  }
  if (!(controller.temperature() < -1.0)) {
    return fail("white balance sampling should average a local patch instead of following a noisy center pixel outlier");
  }

  const auto color_sampling_path = runtime_root.filePath(QStringLiteral("color-tool-hue-sampling.png"));
  QImage color_sampling_source(48, 36, QImage::Format_ARGB32);
  color_sampling_source.fill(QColor(30, 34, 38));
  for (int y = 4; y < 24; ++y) {
    for (int x = 4; x < 18; ++x) {
      color_sampling_source.setPixelColor(x, y, QColor(224, 42, 36));
    }
    for (int x = 30; x < 44; ++x) {
      color_sampling_source.setPixelColor(x, y, QColor(48, 190, 64));
    }
  }
  for (int y = 26; y < 34; ++y) {
    for (int x = 18; x < 31; ++x) {
      color_sampling_source.setPixelColor(x, y, QColor(190, 120, 86));
    }
  }
  if (!color_sampling_source.save(color_sampling_path)) {
    return fail("color tool sampling source image should be writable");
  }
  controller.loadAsset(
      QStringLiteral("asset-color-tool-sampling"),
      color_sampling_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  controller.setWhiteBalanceSampling(true);
  controller.setColorToolSamplingTarget(QStringLiteral("color-replace-source"));
  if (controller.whiteBalanceSampling() ||
      controller.colorToolSamplingTarget() != QStringLiteral("color-replace-source")) {
    return fail("color tool sampling should cancel white balance sampling and expose the active target");
  }
  controller.sampleColorToolAt(0.22, 0.38);
  if (!controller.colorToolSamplingTarget().isEmpty() ||
      !controller.colorReplacementEnabled() ||
      controller.colorReplacementStrength() < 69.0 ||
      !(controller.colorReplacementSourceHue() < 0.04 || controller.colorReplacementSourceHue() > 0.96)) {
    return fail("color replace source sampler should pick red hue, enable the tool, seed strength, and exit sampling mode");
  }
  controller.setColorToolSamplingTarget(QStringLiteral("color-replace-target"));
  controller.sampleColorToolAt(0.78, 0.38);
  if (!controller.colorToolSamplingTarget().isEmpty() ||
      !almost_equal(controller.colorReplacementTargetHue(), 1.0 / 3.0, 0.04)) {
    return fail("color replace target sampler should pick the sampled green hue");
  }
  controller.setSkinToneProtectionStrength(0.0);
  controller.setColorToolSamplingTarget(QStringLiteral("skin-protection"));
  controller.sampleColorToolAt(0.50, 0.84);
  if (!controller.colorToolSamplingTarget().isEmpty() ||
      !controller.skinToneProtectionEnabled() ||
      controller.skinToneProtectionStrength() < 64.0 ||
      !almost_equal(controller.skinToneLineHue(), 0.055, 0.05)) {
    return fail("skin protection sampler should pick a warm skin hue, enable protection, seed strength, and exit sampling mode");
  }
  if (!controller.colorToolMaskPreviewSource().isEmpty()) {
    return fail("color tool mask preview should stay hidden until explicitly enabled");
  }
  controller.setColorToolMaskPreviewEnabled(true);
  const auto color_tool_mask_source = controller.colorToolMaskPreviewSource();
  if (!controller.colorToolMaskPreviewEnabled() ||
      !color_tool_mask_source.startsWith(QStringLiteral("data:image/png;base64,"))) {
    return fail("color tool mask preview should publish affected pixels as a PNG data URL");
  }
  controller.setColorReplacementSourceHue(1.0 / 3.0);
  const auto updated_color_tool_mask_source = controller.colorToolMaskPreviewSource();
  if (updated_color_tool_mask_source.isEmpty() ||
      updated_color_tool_mask_source == color_tool_mask_source) {
    return fail("color tool mask preview should refresh when the selected source hue changes");
  }
  controller.setColorToolMaskPreviewEnabled(false);
  if (!controller.colorToolMaskPreviewSource().isEmpty()) {
    return fail("color tool mask preview should clear when disabled");
  }
  controller.addMask(QStringLiteral("color"));
  if (!controller.sampleActiveColorRangeAt(0.22, 0.38) ||
      !controller.sampleActiveColorRangeAt(0.78, 0.38) ||
      !controller.sampleActiveColorRangeAt(0.50, 0.84) ||
      !controller.sampleActiveColorRangeAt(0.23, 0.39) ||
      !controller.sampleActiveColorRangeAt(0.79, 0.39) ||
      !controller.sampleActiveColorRangeAt(0.51, 0.83) ||
      controller.activeMaskColorSamples().size() != 5 ||
      controller.activeMaskHueRange() <= 0.04 ||
      controller.activeMaskSaturationMin() >= controller.activeMaskSaturationMax()) {
    return fail("active color range masks should append sampled swatches, cap at five, and derive range constraints");
  }
  const auto capped_color_samples = controller.activeMaskColorSamples();
  if (capped_color_samples.first().toMap().value(QStringLiteral("hex")).toString() != QStringLiteral("#30BE40")) {
    return fail("adding a sixth color range sample should keep the five most recent swatches");
  }
  if (!controller.removeActiveColorRangeSample(1) ||
      controller.activeMaskColorSamples().size() != 4 ||
      controller.removeActiveColorRangeSample(99)) {
    return fail("active color range samples should be removable by index and reject invalid removals");
  }
  controller.setSkinToneProtectionEnabled(false);
  controller.setSkinToneProtectionStrength(0.0);
  controller.setSkinToneLineHue(0.08);
  controller.setSkinToneProtectionRange(30.0);
  controller.setColorReplacementEnabled(false);
  const auto auto_skin_detected = controller.autoDetectSkinProtection();
  const auto auto_skin_mask_source = controller.colorToolMaskPreviewSource();
  if (!auto_skin_detected || !controller.skinToneProtectionEnabled() ||
      controller.skinToneProtectionStrength() < 69.0 || !controller.colorToolMaskPreviewEnabled() ||
      !almost_equal(controller.skinToneLineHue(), 0.055, 0.07) ||
      controller.skinToneProtectionRange() < 18.0 || controller.skinToneProtectionRange() > 62.0 ||
      !auto_skin_mask_source.startsWith(QStringLiteral("data:image/png;base64,"))) {
    const auto failure_detail =
        QStringLiteral(
            "auto skin protection should detect a warm skin cluster, enable protection, and publish a mask preview "
            "(detected=%1 enabled=%2 strength=%3 mask=%4 hue=%5 range=%6 status=%7)")
            .arg(auto_skin_detected)
            .arg(controller.skinToneProtectionEnabled())
            .arg(controller.skinToneProtectionStrength())
            .arg(auto_skin_mask_source.startsWith(QStringLiteral("data:image/png;base64,")))
            .arg(controller.skinToneLineHue())
            .arg(controller.skinToneProtectionRange())
            .arg(controller.status())
            .toUtf8();
    return fail(failure_detail.constData());
  }

  photo_editor::desktop::TestImageSink image_sink;
  controller.setImageProvider(&image_sink);

  const auto color_space_path = runtime_root.filePath(QStringLiteral("preview-color-space.png"));
  QImage color_space_source(24, 16, QImage::Format_ARGB32);
  for (int y = 0; y < color_space_source.height(); ++y) {
    for (int x = 0; x < color_space_source.width(); ++x) {
      color_space_source.setPixelColor(
          x,
          y,
          QColor(
              32 + ((x * 200) / std::max(1, color_space_source.width() - 1)),
              48 + ((y * 160) / std::max(1, color_space_source.height() - 1)),
              96));
    }
  }
  color_space_source.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
  if (!color_space_source.save(color_space_path)) {
    return fail("preview color space source image should be writable");
  }

  controller.loadAsset(
      QStringLiteral("asset-color-space"),
      color_space_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(controller)) {
    return fail("preview color space render should finish rendering");
  }

  const auto preview_image = image_sink.image(QStringLiteral("edited"));
  const auto expected_srgb = QColorSpace(QColorSpace::SRgb);
  if (preview_image.isNull()) {
    return fail("image sink should receive the rendered in-memory preview");
  }
  if (!preview_image.colorSpace().isValid() || preview_image.colorSpace() != expected_srgb) {
    return fail("rendered preview should normalize decoded images into the sRGB working space");
  }

  controller.setOutputColorSpace(QStringLiteral("Display P3"));
  const auto output_profile_intent =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_edit_intent(controller);
  if (output_profile_intent.operation != QStringLiteral("output_color_space") ||
      output_profile_intent.dirty_domain != QStringLiteral("display_transform") ||
      output_profile_intent.render_reason != QStringLiteral("output_profile_change") ||
      !output_profile_intent.schedule_full_resolution) {
    return fail("output color space edits should emit a typed display-transform edit intent");
  }
  const auto output_profile_graph =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_render_graph_resolution(controller);
  if (output_profile_graph.dirty_domain != QStringLiteral("display_transform") ||
      output_profile_graph.dirty_stages !=
           QStringList({
               QStringLiteral("display_transform"),
               QStringLiteral("overlay_composite"),
               QStringLiteral("publish_texture"),
               QStringLiteral("encode_disk"),
               QStringLiteral("histogram_scope"),
          }) ||
      !output_profile_graph.full_invalidation) {
    return fail("output color space edits should resolve only display and publication dirty stages");
  }
  if (!wait_for_preview_idle(controller)) {
    return fail("display-p3 preview render should finish rendering");
  }
  const auto display_p3_preview_image = image_sink.image(QStringLiteral("edited"));
  const auto expected_display_p3 = QColorSpace(QColorSpace::DisplayP3);
  if (controller.outputColorSpace() != QStringLiteral("Display P3")) {
    return fail("controller should expose the selected output color space");
  }
  if (display_p3_preview_image.isNull() ||
      !display_p3_preview_image.colorSpace().isValid() ||
      display_p3_preview_image.colorSpace() != expected_display_p3) {
    return fail("rendered preview should apply the selected output color space transform");
  }
  controller.setInputColorProfile(QStringLiteral("/profiles/Canon EOS R5 Adobe Standard.dcp"));
  if (controller.inputColorProfile() != QStringLiteral("Camera DCP: Canon EOS R5 Adobe Standard.dcp")) {
    return fail("controller should expose normalized DCP camera input profiles");
  }
  const auto dcp_recipe = QJsonDocument::fromJson(controller.currentRecipeJson().toUtf8()).object();
  if (dcp_recipe.value(QStringLiteral("colorPipeline"))
          .toObject()
          .value(QStringLiteral("inputProfile"))
          .toString() != QStringLiteral("Camera DCP: Canon EOS R5 Adobe Standard.dcp")) {
    return fail("controller recipes should persist DCP camera input profiles");
  }
  controller.setInputColorProfile(QStringLiteral("ICC: Manufacturer Camera Profile"));
  if (controller.inputColorProfile() != QStringLiteral("Camera ICC: Manufacturer Camera Profile")) {
    return fail("controller should expose normalized ICC camera input profiles");
  }
  controller.setRawCameraMatchingLook(QStringLiteral("Vivid"));
  if (controller.rawCameraMatchingLook() != QStringLiteral("Vivid")) {
    return fail("controller should expose selected camera matching looks");
  }
  const auto look_recipe = QJsonDocument::fromJson(controller.currentRecipeJson().toUtf8()).object();
  if (look_recipe.value(QStringLiteral("rawAdjustments"))
          .toObject()
          .value(QStringLiteral("cameraMatchingLook"))
          .toString() != QStringLiteral("Vivid")) {
    return fail("controller recipes should persist camera matching looks");
  }
  controller.setRawCameraMatchingLook(QStringLiteral("Standard"));
  controller.setInputColorProfile(QStringLiteral("Embedded"));
  controller.setMonitorColorProfile(QStringLiteral("sRGB"));
  if (!wait_for_preview_idle(controller)) {
    return fail("monitor-profile preview render should finish rendering");
  }
  const auto monitor_srgb_preview_image = image_sink.image(QStringLiteral("edited"));
  if (controller.outputColorSpace() != QStringLiteral("Display P3") ||
      controller.monitorColorProfile() != QStringLiteral("sRGB")) {
    return fail("monitor profile selection should not replace the export output profile");
  }
  if (monitor_srgb_preview_image.isNull() ||
      !monitor_srgb_preview_image.colorSpace().isValid() ||
      monitor_srgb_preview_image.colorSpace() != expected_srgb) {
    return fail("rendered preview should target the selected monitor profile");
  }

  const auto p3_boundary_path = runtime_root.filePath(QStringLiteral("preview-display-p3-boundary.png"));
  QImage p3_boundary_source(18, 12, QImage::Format_ARGB32);
  p3_boundary_source.fill(QColor(0, 255, 0));
  p3_boundary_source.setColorSpace(expected_display_p3);
  if (!p3_boundary_source.save(p3_boundary_path)) {
    return fail("Display P3 boundary source image should be writable");
  }
  controller.loadAsset(
      QStringLiteral("asset-display-p3-boundary"),
      p3_boundary_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  controller.setOutputColorSpace(QStringLiteral("Display P3"));
  controller.setMonitorColorProfile(QStringLiteral("Display P3"));
  controller.setExposure(0.0);
  controller.setTemperature(0.0);
  controller.setTint(0.0);
  controller.setSaturation(1.0);
  if (!wait_for_preview_idle(controller)) {
    return fail("Display P3 boundary preview render should finish rendering");
  }
  const auto p3_boundary_preview = image_sink.image(QStringLiteral("edited"));
  if (p3_boundary_preview.isNull() || p3_boundary_preview.colorSpace() != expected_display_p3) {
    return fail("Display P3 boundary preview should keep the selected output profile");
  }
  const auto p3_boundary_argb = p3_boundary_preview.convertToFormat(QImage::Format_ARGB32);
  const auto p3_boundary_sample =
      p3_boundary_argb.pixel(p3_boundary_argb.width() / 2, p3_boundary_argb.height() / 2);
  if (qGreen(p3_boundary_sample) < 248 ||
      qRed(p3_boundary_sample) > 96 ||
      qBlue(p3_boundary_sample) > 96 ||
      qGreen(p3_boundary_sample) - qRed(p3_boundary_sample) < 150 ||
      qGreen(p3_boundary_sample) - qBlue(p3_boundary_sample) < 150) {
    return fail("edited Display P3 previews should not be routed through a clipped sRGB image");
  }
  controller.setSoftProofProfile(QStringLiteral("sRGB"));
  controller.setSoftProofEnabled(true);
  if (!wait_for_preview_idle(controller)) {
    return fail("soft-proof preview render should finish rendering");
  }
  const auto soft_proof_preview = image_sink.image(QStringLiteral("edited"));
  if (!controller.softProofEnabled() || controller.softProofProfile() != QStringLiteral("sRGB")) {
    return fail("controller should expose the selected soft proof profile");
  }
  if (soft_proof_preview.isNull() || soft_proof_preview.colorSpace() != expected_display_p3) {
    return fail("soft proof preview should still target the monitor display profile");
  }
  const auto soft_proof_argb = soft_proof_preview.convertToFormat(QImage::Format_ARGB32);
  const auto soft_proof_sample =
      soft_proof_argb.pixel(soft_proof_argb.width() / 2, soft_proof_argb.height() / 2);
  if (qRed(soft_proof_sample) <= qRed(p3_boundary_sample) + 32 ||
      qBlue(soft_proof_sample) <= qBlue(p3_boundary_sample) + 8) {
    return fail("soft proofing through sRGB should simulate the proof profile before monitor display");
  }
  controller.setSoftProofEnabled(false);
  if (!wait_for_preview_idle(controller)) {
    return fail("soft-proof disable render should finish before HDR parity fixture");
  }

  photo_editor::desktop::EditorPreviewController hdr_controller;
  photo_editor::desktop::TestImageSink hdr_image_sink;
  const auto hdr_fixture_metal_disabled =
      qEnvironmentVariableIsSet("PHOTO_EDITOR_DISABLE_METAL_PREVIEW");
  qputenv("PHOTO_EDITOR_DISABLE_METAL_PREVIEW", QByteArrayLiteral("1"));
  hdr_controller.setImageProvider(&hdr_image_sink);
  hdr_controller.setOutputColorSpace(QStringLiteral("sRGB"));
  hdr_controller.setMonitorColorProfile(QStringLiteral("sRGB"));
  const auto hdr_ramp_path = runtime_root.filePath(QStringLiteral("preview-hdr-ramp.tiff"));
  QImage hdr_ramp_source(512, 24, QImage::Format_RGBA64);
  hdr_ramp_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < hdr_ramp_source.height(); ++y) {
    for (int x = 0; x < hdr_ramp_source.width(); ++x) {
      const auto t = static_cast<double>(x) / static_cast<double>(std::max(1, hdr_ramp_source.width() - 1));
      const auto value = 0.05 + t * 0.75;
      hdr_ramp_source.setPixelColor(
          x,
          y,
          QColor::fromRgbF(
              static_cast<float>(value),
              static_cast<float>(value),
              static_cast<float>(value),
              1.0F));
    }
  }
  if (!hdr_ramp_source.save(hdr_ramp_path)) {
    return fail("16-bit preview ramp source should be writable");
  }

  hdr_controller.loadAsset(
      QStringLiteral("asset-hdr-preview-ramp"),
      hdr_ramp_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(hdr_controller)) {
    return fail("16-bit preview ramp asset load should finish rendering before strong edits");
  }
  hdr_image_sink.clearImage(QStringLiteral("edited"));
  photo_editor::desktop::EditorPreviewControllerTestAccess::begin_edit_transaction(hdr_controller);
  hdr_controller.setExposure(0.25);
  hdr_controller.setContrast(0.0);
  hdr_controller.setBrightness(0.18);
  hdr_controller.setHdrLocalToneMapping(0.0);
  hdr_controller.setFilmicShoulder(0.0);
  hdr_controller.setFilmicToe(0.0);
  hdr_controller.setAdaptiveContrast(0.0);
  hdr_controller.setLevelsBlackPoint(0.0);
  hdr_controller.setLevelsGamma(0.0);
  hdr_controller.setLevelsWhitePoint(0.0);
  hdr_controller.setZoneDeepShadows(0.0);
  hdr_controller.setZoneShadows(0.0);
  hdr_controller.setZoneMidtones(0.0);
  hdr_controller.setZoneLights(0.0);
  hdr_controller.setZoneHighlights(0.0);
  photo_editor::desktop::EditorPreviewControllerTestAccess::end_edit_transaction(hdr_controller);
  if (!almost_equal(hdr_controller.brightness(), 0.18)) {
    return fail("brightness control should be exposed on the preview controller");
  }
  if (!almost_equal(hdr_controller.hdrLocalToneMapping(), 0.0)) {
    return fail("HDR local tone mapping control should be exposed on the preview controller");
  }
  if (!almost_equal(hdr_controller.filmicShoulder(), 0.0) ||
      !almost_equal(hdr_controller.filmicToe(), 0.0)) {
    return fail("filmic shoulder and toe controls should be exposed on the preview controller");
  }
  if (!almost_equal(hdr_controller.adaptiveContrast(), 0.0)) {
    return fail("adaptive contrast control should be exposed on the preview controller");
  }
  if (!almost_equal(hdr_controller.levelsBlackPoint(), 0.0) ||
      !almost_equal(hdr_controller.levelsGamma(), 0.0) ||
      !almost_equal(hdr_controller.levelsWhitePoint(), 0.0)) {
    return fail("levels controls should be exposed on the preview controller");
  }
  if (!almost_equal(hdr_controller.zoneDeepShadows(), 0.0) ||
      !almost_equal(hdr_controller.zoneShadows(), 0.0) ||
      !almost_equal(hdr_controller.zoneMidtones(), 0.0) ||
      !almost_equal(hdr_controller.zoneLights(), 0.0) ||
      !almost_equal(hdr_controller.zoneHighlights(), 0.0)) {
    return fail("zone controls should be exposed on the preview controller");
  }
  if (!wait_for_preview_idle(hdr_controller)) {
    return fail("16-bit preview ramp render should finish rendering");
  }
  auto hdr_render_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(hdr_controller);
  const auto hdr_uncached_ramp_path =
      runtime_root.filePath(QStringLiteral("preview-hdr-ramp-uncached.tiff"));
  QFile::remove(hdr_uncached_ramp_path);
  if (!QFile::copy(hdr_ramp_path, hdr_uncached_ramp_path)) {
    return fail("16-bit preview ramp uncached source should be writable");
  }
  photo_editor::desktop::EditorPreviewControllerTestAccess::clear_preview_working_caches();
  hdr_render_request.file_path = hdr_uncached_ramp_path;
  hdr_render_request.generation = 1;
  hdr_render_request.latest_generation =
      std::make_shared<std::atomic<int>>(hdr_render_request.generation);
  // This assertion is about native 16-bit decode plus the uncached render path.
  // Cache-hit behavior is exercised by the dedicated cache tests above.
  hdr_render_request.full_invalidation = true;
  hdr_render_request.compare_mode = false;
  const auto hdr_render_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(hdr_render_request);
  if (!hdr_render_result.success || hdr_render_result.cancelled) {
    return fail("16-bit preview ramp synchronous render should complete");
  }
  const auto hdr_preview_image = hdr_render_result.preview_image;
  if (hdr_preview_image.isNull()) {
    return fail("16-bit preview ramp should produce an in-memory preview image");
  }

  QImage hdr_linear_decode;
  if (!photo_editor::desktop::decode_native_linear_image(hdr_uncached_ramp_path, &hdr_linear_decode, 0)) {
    return fail("16-bit preview ramp should decode through the native linear path");
  }
  if (hdr_linear_decode.size() != hdr_preview_image.size()) {
    return fail("16-bit preview render should preserve the uncached source dimensions");
  }
  if (hdr_render_result.decode_backend != QStringLiteral("native") ||
      hdr_render_result.decoded_cache_hit ||
      hdr_render_result.linear_cache_hit ||
      hdr_render_result.adjusted_cache_hit ||
      hdr_render_result.rendered_cache_hit) {
    return fail("16-bit preview ramp should exercise the uncached native render path");
  }
  const auto hdr_render_span = max_channel_span(hdr_preview_image);
  if (hdr_render_span < 24) {
    return fail("16-bit preview ramp should render visible tonal variation after edits");
  }
  if (!hdr_fixture_metal_disabled) {
    qunsetenv("PHOTO_EDITOR_DISABLE_METAL_PREVIEW");
  }

  QImage hdr_chroma_source(4, 1, QImage::Format_RGBA16FPx4);
  if (hdr_chroma_source.isNull()) {
    return fail("half-float chroma source should allocate");
  }
  hdr_chroma_source.setColorSpace(photo_editor::desktop::working_color_space());
  auto* hdr_chroma_scanline = reinterpret_cast<QRgbaFloat16*>(hdr_chroma_source.scanLine(0));
  hdr_chroma_scanline[0] = QRgbaFloat16{qfloat16(0.95F), qfloat16(0.52F), qfloat16(0.24F), qfloat16(1.0F)};
  hdr_chroma_scanline[1] = QRgbaFloat16{qfloat16(1.05F), qfloat16(0.52F), qfloat16(0.24F), qfloat16(1.0F)};
  hdr_chroma_scanline[2] = QRgbaFloat16{qfloat16(1.20F), qfloat16(0.52F), qfloat16(0.24F), qfloat16(1.0F)};
  hdr_chroma_scanline[3] = QRgbaFloat16{qfloat16(1.35F), qfloat16(0.52F), qfloat16(0.24F), qfloat16(1.0F)};

  photo_editor::desktop::RenderEditState hdr_chroma_state;
  hdr_chroma_state.exposure = 1.0;
  hdr_chroma_state.contrast = 10.0;

  auto hdr_chroma_preview = hdr_chroma_source;
  photo_editor::desktop::apply_color_adjustments(&hdr_chroma_preview, hdr_chroma_state);
  if (hdr_chroma_preview.isNull()) {
    return fail("desktop render helper should render over-range chroma previews");
  }

  auto hdr_chroma_expected = photo_editor::desktop::working_image_from_qimage(hdr_chroma_source);
  if (hdr_chroma_expected.isNull()) {
    return fail("half-float chroma source should convert to a working image");
  }
  photo_editor::pipeline::ColorAdjustmentState hdr_chroma_pipeline_state;
  hdr_chroma_pipeline_state.exposure = 1.0;
  hdr_chroma_pipeline_state.contrast = 10.0;
  photo_editor::pipeline::apply_color_adjustments(
      &hdr_chroma_expected.pixels,
      hdr_chroma_expected.width,
      hdr_chroma_expected.height,
      hdr_chroma_pipeline_state);
  const auto hdr_chroma_expected_preview = photo_editor::desktop::qimage_from_working_image(
      hdr_chroma_expected,
      QColorSpace(QColorSpace::SRgb));
  if (hdr_chroma_expected_preview.isNull()) {
    return fail("desktop render helper reference image should be valid");
  }
  if (max_channel_delta(hdr_chroma_preview, hdr_chroma_expected_preview) > 3) {
    return fail("desktop render helper should match the direct path for over-range chroma highlights");
  }

  const auto smart_select_path = runtime_root.filePath(QStringLiteral("smart-select-source.png"));
  QImage smart_select_source(96, 96, QImage::Format_ARGB32);
  smart_select_source.fill(Qt::white);
  for (int y = 38; y <= 57; ++y) {
    for (int x = 10; x <= 25; ++x) {
      smart_select_source.setPixelColor(x, y, QColor(214, 78, 78));
    }
    for (int x = 40; x <= 55; ++x) {
      smart_select_source.setPixelColor(x, y, QColor(74, 118, 212));
    }
    for (int x = 70; x <= 85; ++x) {
      smart_select_source.setPixelColor(x, y, QColor(78, 176, 112));
    }
  }
  if (!smart_select_source.save(smart_select_path)) {
    return fail("smart select source image should be writable");
  }

  const auto tiny_object_path = runtime_root.filePath(QStringLiteral("tiny-object-source.png"));
  QImage tiny_object_source(96, 96, QImage::Format_ARGB32);
  tiny_object_source.fill(Qt::white);
  for (int y = 46; y <= 51; ++y) {
    for (int x = 14; x <= 19; ++x) {
      tiny_object_source.setPixelColor(x, y, QColor(214, 78, 78));
    }
    for (int x = 45; x <= 50; ++x) {
      tiny_object_source.setPixelColor(x, y, QColor(74, 118, 212));
    }
    for (int x = 76; x <= 81; ++x) {
      tiny_object_source.setPixelColor(x, y, QColor(78, 176, 112));
    }
  }
  if (!tiny_object_source.save(tiny_object_path)) {
    return fail("tiny object source image should be writable");
  }

  controller.loadAsset(
      QStringLiteral("asset-smart"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  const auto smart_select_point = [](double x, double y) {
    QVariantMap point;
    point.insert(QStringLiteral("x"), x);
    point.insert(QStringLiteral("y"), y);
    return point;
  };
  const auto wait_for_smart_preview = [](photo_editor::desktop::EditorPreviewController& preview_controller,
                                          int timeout_ms = 500) {
    auto elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
      if (preview_controller.smartSelectPreviewActive()) {
        return true;
      }
      QThread::msleep(25);
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
      elapsed_ms += 25;
    }
    return preview_controller.smartSelectPreviewActive();
  };
  const auto wait_for_smart_commit = [](photo_editor::desktop::EditorPreviewController& preview_controller,
                                        int expected_mask_count,
                                        int timeout_ms = 4000) {
    auto elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
      if (!preview_controller.processing() &&
          preview_controller.maskCount() >= expected_mask_count) {
        return true;
      }
      QThread::msleep(25);
      elapsed_ms += 25;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return !preview_controller.processing() &&
           preview_controller.maskCount() >= expected_mask_count;
  };
  photo_editor::desktop::EditorPreviewController superseded_smart_commit_controller;
  superseded_smart_commit_controller.loadAsset(
      QStringLiteral("asset-superseded-smart-commit"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(superseded_smart_commit_controller)) {
    return fail("superseded smart selection controller should finish its initial preview");
  }
  const auto superseded_smart_generation_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_selection_job_generation(
          superseded_smart_commit_controller);
  const auto superseded_smart_cancelled_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_selection_cancelled_count(
          superseded_smart_commit_controller);
  superseded_smart_commit_controller.smartSelectObjectAt(0.18, 0.5, true);
  if (!superseded_smart_commit_controller.processing() ||
      superseded_smart_commit_controller.maskCount() != 0) {
    return fail("first smart selection commit should stay pending before stale replacement");
  }
  superseded_smart_commit_controller.smartSelectObjectAt(0.48, 0.5, true);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::smart_selection_job_generation(
          superseded_smart_commit_controller) != superseded_smart_generation_before + 2) {
    return fail("a newer smart selection commit should supersede the in-flight generation immediately");
  }
  if (!wait_for_smart_commit(superseded_smart_commit_controller, 1)) {
    return fail("superseded smart selection commit should finish with the newest target");
  }
  const auto superseded_smart_mask =
      superseded_smart_commit_controller.maskStack().at(0).toMap();
  const auto superseded_smart_data =
      superseded_smart_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto superseded_smart_width =
      superseded_smart_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto superseded_smart_height =
      superseded_smart_mask.value(QStringLiteral("segmentationHeight")).toInt();
  const auto superseded_smart_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          superseded_smart_commit_controller);
  if (superseded_smart_commit_controller.maskCount() != 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_selection_cancelled_count(
          superseded_smart_commit_controller) <= superseded_smart_cancelled_before ||
      mask_value_at(
          superseded_smart_data,
          superseded_smart_width,
          superseded_smart_height,
          0.48,
          0.5) < 128 ||
      mask_value_at(
          superseded_smart_data,
          superseded_smart_width,
          superseded_smart_height,
          0.18,
          0.5) > 24 ||
      superseded_smart_telemetry.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("smart_selection") ||
      !superseded_smart_telemetry.value(QStringLiteral("async_job_id"))
           .toString()
           .startsWith(QStringLiteral("smart_selection-%1-")
                           .arg(superseded_smart_generation_before + 2)) ||
      superseded_smart_telemetry.value(QStringLiteral("async_job_kind")).toString() !=
          QStringLiteral("smart_selection") ||
      superseded_smart_telemetry.value(QStringLiteral("async_job_stage")).toString() !=
          QStringLiteral("async_commit_publish") ||
      superseded_smart_telemetry.value(QStringLiteral("async_job_asset_id")).toString() !=
          QStringLiteral("asset-superseded-smart-commit") ||
      superseded_smart_telemetry.value(QStringLiteral("async_job_file_path")).toString() !=
          smart_select_path ||
      !superseded_smart_telemetry.value(QStringLiteral("async_job_state_signature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      superseded_smart_telemetry.value(QStringLiteral("job_generation")).toInt() !=
          superseded_smart_generation_before + 2 ||
      superseded_smart_telemetry.value(QStringLiteral("cancelled")).toBool()) {
    return fail("superseded smart selection commits should cancel stale results and publish only the newest async job target");
  }

  photo_editor::desktop::EditorPreviewController stale_mask_change_smart_commit_controller;
  stale_mask_change_smart_commit_controller.loadAsset(
      QStringLiteral("asset-stale-mask-change-smart-commit"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(stale_mask_change_smart_commit_controller)) {
    return fail("stale mask-change smart commit controller should finish its initial preview");
  }
  const auto stale_mask_change_cancelled_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_selection_cancelled_count(
          stale_mask_change_smart_commit_controller);
  stale_mask_change_smart_commit_controller.smartSelectObjectAt(0.18, 0.5, true);
  const auto stale_mask_change_request_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_selection_job_generation(
          stale_mask_change_smart_commit_controller);
  if (!stale_mask_change_smart_commit_controller.processing() ||
      stale_mask_change_smart_commit_controller.maskCount() != 0) {
    return fail("smart selection commit should be pending before a mask-stack change invalidates it");
  }
  stale_mask_change_smart_commit_controller.startSelectionBrush(false);
  const auto stale_mask_change_revision_after_brush =
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_work_state_revision(
          stale_mask_change_smart_commit_controller);
  auto stale_mask_change_elapsed_ms = 0;
  while (stale_mask_change_elapsed_ms < 2000 &&
         photo_editor::desktop::EditorPreviewControllerTestAccess::smart_selection_cancelled_count(
             stale_mask_change_smart_commit_controller) == stale_mask_change_cancelled_before) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    QThread::msleep(25);
    stale_mask_change_elapsed_ms += 25;
  }
  const auto stale_mask_change_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          stale_mask_change_smart_commit_controller);
  if (stale_mask_change_smart_commit_controller.maskCount() != 1 ||
      stale_mask_change_smart_commit_controller.activeMaskKind() != QStringLiteral("brush") ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_work_state_revision(
          stale_mask_change_smart_commit_controller) != stale_mask_change_revision_after_brush ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_selection_cancelled_count(
          stale_mask_change_smart_commit_controller) != stale_mask_change_cancelled_before + 1 ||
      stale_mask_change_telemetry.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("smart_selection") ||
      !stale_mask_change_telemetry.value(QStringLiteral("cancelled")).toBool() ||
      stale_mask_change_telemetry.value(QStringLiteral("cancellation_reason")).toString() !=
          QStringLiteral("stale_result") ||
      stale_mask_change_telemetry.value(QStringLiteral("stage")).toString() !=
          QStringLiteral("async_commit_publish") ||
      stale_mask_change_telemetry.value(QStringLiteral("job_generation")).toInt() !=
          stale_mask_change_request_generation ||
      stale_mask_change_telemetry.value(QStringLiteral("async_job_kind")).toString() !=
          QStringLiteral("smart_selection") ||
      stale_mask_change_telemetry.value(QStringLiteral("async_job_stage")).toString() !=
          QStringLiteral("async_commit_publish") ||
      stale_mask_change_telemetry.value(QStringLiteral("async_job_asset_id")).toString() !=
          QStringLiteral("asset-stale-mask-change-smart-commit") ||
      stale_mask_change_telemetry.value(QStringLiteral("async_job_file_path")).toString() !=
          smart_select_path ||
      !stale_mask_change_telemetry.value(QStringLiteral("async_job_state_signature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !stale_mask_change_smart_commit_controller.status().contains(QStringLiteral("canceled"))) {
    return fail("smart selection commits should reject stale async results after the active mask stack changes");
  }

  controller.updateSmartSelectPreview(0.18, 0.5);
  const auto smart_outline_hits_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_outline_cache_hit_count(controller);
  const auto smart_outline_misses_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_outline_cache_miss_count(controller);
  const auto smart_preview_outline = controller.smartSelectPreviewOutlinePoints();
  const auto smart_preview_outline_second = controller.smartSelectPreviewOutlinePoints();
  const auto smart_preview_pipeline_signature =
      controller.smartSelectPreviewPipelineSignature();
  if (!controller.smartSelectPreviewActive() ||
      !controller.smartSelectPreviewSource().isEmpty() ||
      !smart_preview_pipeline_signature.contains(QStringLiteral("smart-selection-hover")) ||
      !smart_preview_pipeline_signature.contains(QStringLiteral("precommit:true")) ||
      !smart_preview_pipeline_signature.contains(QStringLiteral("preview-edge:1024")) ||
      !smart_preview_pipeline_signature.contains(QStringLiteral("debounce-ms:45")) ||
      !smart_preview_pipeline_signature.contains(QStringLiteral("instance-map:loaded")) ||
      !smart_preview_pipeline_signature.contains(QStringLiteral("shared-cache-hit:false")) ||
      !smart_preview_pipeline_signature.contains(QStringLiteral("outline-only:true")) ||
      !smart_preview_pipeline_signature.contains(QStringLiteral("raster-source:false")) ||
      !smart_preview_pipeline_signature.contains(QStringLiteral("candidate-count:1")) ||
      smart_preview_outline.size() < 4 ||
      smart_preview_outline.size() > 96 ||
      smart_preview_outline_second != smart_preview_outline ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              smart_select_preview_outline_cache_miss_count(controller) !=
          smart_outline_misses_before + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              smart_select_preview_outline_cache_hit_count(controller) !=
           smart_outline_hits_before + 1) {
    return fail("smart select should expose a source-free clean realtime hover outline before committing the selection");
  }
  const auto smart_preview_trace_mask =
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_preview_mask(
          controller);
  if (smart_preview_trace_mask.value(QStringLiteral("mask_source_hash")).toString().isEmpty() ||
      !smart_preview_trace_mask.value(QStringLiteral("mask_alpha_trace_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !smart_preview_trace_mask.value(QStringLiteral("mask_alpha_trace_signature"))
           .toString()
           .contains(QStringLiteral("raster:")) ||
      smart_preview_trace_mask.value(QStringLiteral("mask_alpha_raster_width")).toInt() <= 0 ||
      smart_preview_trace_mask.value(QStringLiteral("mask_alpha_raster_height")).toInt() <= 0) {
    return fail("smart select hover preview should carry exact alpha/source trace metadata before commit");
  }
  const auto smart_preview_overlay = controller.smartSelectPreviewSource();
  controller.setMaskPreviewMode(QStringLiteral("white-on-black"));
  if (controller.maskPreviewMode() != QStringLiteral("white-on-black") ||
      controller.smartSelectPreviewSource().isEmpty() ||
      controller.smartSelectPreviewSource() == smart_preview_overlay ||
      controller.smartSelectPreviewOutlinePoints().size() < 4) {
    return fail("smart select preview should follow the selected mask preview mode");
  }
  const auto smart_outline_misses_after_mode_change =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_outline_cache_miss_count(controller);
  controller.updateSmartSelectPreview(4.0 / 7.0, 1.0 / 7.0);
  const auto first_candidate_preview = controller.smartSelectPreviewSource();
  const auto first_candidate_outline = controller.smartSelectPreviewOutlinePoints();
  if (!controller.smartSelectCanCycleCandidates() ||
      controller.smartSelectCandidateCount() < 2 ||
      controller.smartSelectCandidateIndex() != 0 ||
      controller.smartSelectCandidateHudText() !=
          QStringLiteral("Candidate 1 / %1").arg(controller.smartSelectCandidateCount()) ||
      first_candidate_preview.isEmpty() ||
      first_candidate_outline.size() < 4 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              smart_select_preview_outline_cache_miss_count(controller) !=
          smart_outline_misses_after_mode_change + 1) {
    return fail("smart select should expose multiple candidate objects near ambiguous clicks");
  }
  const auto first_candidate_trace_mask =
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_preview_mask(
          controller);
  const auto first_candidate_trace_hash =
      first_candidate_trace_mask.value(QStringLiteral("mask_alpha_trace_hash")).toString();
  if (!first_candidate_trace_hash.startsWith(QStringLiteral("sha1:")) ||
      first_candidate_trace_mask.value(QStringLiteral("mask_alpha_raster_width")).toInt() <= 0 ||
      first_candidate_trace_mask.value(QStringLiteral("mask_alpha_raster_height")).toInt() <= 0) {
    return fail("smart select first candidate preview should expose traceable alpha identity");
  }
  controller.cycleSmartSelectCandidate(1);
  const auto second_candidate_trace_mask =
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_preview_mask(
          controller);
  const auto second_candidate_trace_hash =
      second_candidate_trace_mask.value(QStringLiteral("mask_alpha_trace_hash")).toString();
  if (controller.smartSelectCandidateIndex() != 1 ||
      controller.smartSelectPreviewSource().isEmpty() ||
      controller.smartSelectPreviewSource() == first_candidate_preview ||
      !second_candidate_trace_hash.startsWith(QStringLiteral("sha1:")) ||
      second_candidate_trace_hash == first_candidate_trace_hash) {
    return fail("smart select candidate cycling should update the active preview candidate and trace identity");
  }
  controller.cycleSmartSelectCandidate(-1);
  if (controller.smartSelectCandidateIndex() != 0) {
    return fail("smart select candidate cycling should wrap backward to the previous candidate");
  }
  controller.setMaskPreviewMode(QStringLiteral("overlay"));
  const auto smart_generation_before_commit =
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_selection_job_generation(
          controller);
  const auto smart_cancelled_before_commit =
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_selection_cancelled_count(
          controller);
  const auto smart_state_revision_before_commit =
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_work_state_revision(controller);
  controller.smartSelectObjectAt(0.18, 0.5, true);
  if (!controller.processing() || controller.maskCount() != 0) {
    return fail("smart select commit should enter an async pending state before publishing a mask");
  }
  if (!wait_for_smart_commit(controller, 1)) {
    return fail("async smart select commit should finish and publish the first object mask");
  }
  const auto smart_commit_preview_empty = controller.activeMaskPreviewSource().isEmpty();
  const auto smart_commit_outline_paths = controller.activeMaskOutlinePaths();
  const auto smart_commit_outline_empty = smart_commit_outline_paths.isEmpty();
  const auto smart_commit_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  const auto smart_commit_mask_data =
      smart_commit_mask.value(QStringLiteral("segmentationData")).toByteArray();
  auto smart_commit_selected_pixels = 0;
  for (const auto value : smart_commit_mask_data) {
    if (static_cast<unsigned char>(value) > 0) {
      ++smart_commit_selected_pixels;
    }
  }
  if (controller.maskCount() != 1 ||
      controller.activeMaskKind() != QStringLiteral("object") ||
      controller.activeMaskToolRole() != QStringLiteral("selection") ||
      !smart_commit_preview_empty ||
      smart_commit_outline_empty ||
      !controller.activeMaskCommitted()) {
    const auto failure_detail =
        QStringLiteral(
            "smart select should create an object selection mask with an outline and no raster overlay source in normal overlay mode "
            "(count=%1 kind=%2 role=%3 previewEmpty=%4 outlineEmpty=%5 committed=%6 seg=%7x%8 bytes=%9 selected=%10)")
            .arg(controller.maskCount())
            .arg(controller.activeMaskKind())
            .arg(controller.activeMaskToolRole())
            .arg(smart_commit_preview_empty)
            .arg(smart_commit_outline_empty)
            .arg(controller.activeMaskCommitted())
            .arg(smart_commit_mask.value(QStringLiteral("segmentationWidth")).toInt())
            .arg(smart_commit_mask.value(QStringLiteral("segmentationHeight")).toInt())
            .arg(smart_commit_mask_data.size())
            .arg(smart_commit_selected_pixels)
            .toUtf8();
    return fail(failure_detail.constData());
  }
  const auto smart_commit_outline_path_count =
      !smart_commit_outline_paths.isEmpty() &&
              smart_commit_outline_paths.first().metaType().id() == QMetaType::QVariantMap
          ? 1
          : smart_commit_outline_paths.size();
  if (smart_commit_outline_path_count != 1) {
    const auto detail =
        QStringLiteral("normal smart/object mask overlay should use one clean exterior display outline, not noisy interior alpha contours (paths=%1)")
            .arg(smart_commit_outline_path_count)
            .toUtf8();
    return fail(detail.constData());
  }
  if (controller.photoLayerCount() != 1 ||
      controller.photoLayers().at(0).toMap().value(QStringLiteral("kind")).toString() !=
          QStringLiteral("adjustment") ||
      !controller.photoLayers().at(0).toMap().value(QStringLiteral("selectionScoped")).toBool() ||
      controller.photoLayers().at(0).toMap().value(QStringLiteral("displayName")).toString() !=
          QStringLiteral("Adjustment Layer 1")) {
    return fail("smart select should create a selection-scoped adjustment layer with the object mask");
  }
  const auto smart_select_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          controller);
  const auto smart_select_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  const auto smart_select_generated_metadata =
      smart_select_mask.value(QStringLiteral("generatedMaskMetadata")).toMap();
  if (smart_select_mask.value(QStringLiteral("generatedAlphaSourceContract")).toString() !=
          QStringLiteral("generated-alpha-plus-metadata-v1") ||
      smart_select_mask.value(QStringLiteral("generatedAlphaTargetType")).toString() !=
          QStringLiteral("object") ||
      smart_select_mask.value(QStringLiteral("generatedAlphaPromptType")).toString() !=
          QStringLiteral("semantic:object") ||
      smart_select_mask.value(QStringLiteral("generatedAlphaModelName")).toString().isEmpty() ||
      smart_select_mask.value(QStringLiteral("generatedAlphaSourceImageVersion")).toString().isEmpty() ||
      smart_select_mask.value(QStringLiteral("generatedAlphaWidth")).toInt() !=
          smart_select_mask.value(QStringLiteral("segmentationWidth")).toInt() ||
      smart_select_mask.value(QStringLiteral("generatedAlphaHeight")).toInt() !=
          smart_select_mask.value(QStringLiteral("segmentationHeight")).toInt() ||
      smart_select_mask.value(QStringLiteral("generatedAlphaDownscaleFactor")).toDouble() <= 0.0 ||
      !smart_select_mask.value(QStringLiteral("generatedAlphaSourceSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      smart_select_generated_metadata.value(QStringLiteral("generatedAlphaSourceContract")).toString() !=
          QStringLiteral("generated-alpha-plus-metadata-v1") ||
      smart_select_generated_metadata.value(QStringLiteral("generatedAlphaSourceSignature")).toString() !=
          smart_select_mask.value(QStringLiteral("generatedAlphaSourceSignature")).toString() ||
      smart_select_generated_metadata.value(QStringLiteral("smartSelectionPrompts")).toList().isEmpty()) {
    return fail("committed smart selections should expose alpha, generation, prompt, and refinement provenance");
  }
  if (smart_select_telemetry.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("smart_selection") ||
      smart_select_telemetry.value(QStringLiteral("smart_selection_ms")).toLongLong() < 0 ||
      smart_select_telemetry.value(QStringLiteral("generated_mask_feedback_budget_ms")).toLongLong() != 500 ||
      !smart_select_telemetry.contains(QStringLiteral("generated_mask_feedback_cached")) ||
      !smart_select_telemetry.contains(QStringLiteral("generated_mask_feedback_within_latency_budget")) ||
      !smart_select_telemetry.value(QStringLiteral("async_worker")).toBool() ||
      smart_select_telemetry.value(QStringLiteral("smart_selection_worker_thread")).toString().isEmpty() ||
      smart_select_telemetry.value(QStringLiteral("prompt_count")).toInt() <= 0 ||
      !smart_select_telemetry.value(QStringLiteral("success")).toBool() ||
      smart_select_telemetry.value(QStringLiteral("cancelled")).toBool() ||
      smart_select_telemetry.value(QStringLiteral("job_generation")).toInt() <=
          smart_generation_before_commit ||
      smart_select_telemetry.value(QStringLiteral("async_job_kind")).toString() !=
          QStringLiteral("smart_selection") ||
      smart_select_telemetry.value(QStringLiteral("async_job_stage")).toString() !=
          QStringLiteral("async_commit_publish") ||
      smart_select_telemetry.value(QStringLiteral("async_job_asset_id")).toString() !=
          QStringLiteral("asset-smart") ||
      smart_select_telemetry.value(QStringLiteral("async_job_file_path")).toString() !=
          smart_select_path ||
      !smart_select_telemetry.value(QStringLiteral("async_job_id"))
           .toString()
           .startsWith(QStringLiteral("smart_selection-")) ||
      !smart_select_telemetry.value(QStringLiteral("async_job_state_signature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      smart_select_telemetry.value(QStringLiteral("state_revision")).toInt() !=
          smart_state_revision_before_commit ||
      !smart_select_telemetry.contains(QStringLiteral("cache_hit")) ||
      !smart_select_telemetry.contains(QStringLiteral("fallback_reason")) ||
      smart_select_telemetry.value(QStringLiteral("mask_source_id")).toString() !=
          smart_select_mask.value(QStringLiteral("id")).toString() ||
      smart_select_telemetry.value(QStringLiteral("mask_set_id")).toString() !=
          smart_select_mask.value(QStringLiteral("maskSetId")).toString() ||
      smart_select_telemetry.value(QStringLiteral("mask_source_kind")).toString() !=
          QStringLiteral("object") ||
      !smart_select_telemetry.value(QStringLiteral("mask_source_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !smart_select_telemetry.value(QStringLiteral("mask_alpha_trace_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !smart_select_telemetry.value(QStringLiteral("mask_alpha_trace_signature"))
           .toString()
           .contains(smart_select_mask.value(QStringLiteral("id")).toString()) ||
      smart_select_telemetry.value(QStringLiteral("mask_alpha_raster_width")).toInt() <= 0 ||
      smart_select_telemetry.value(QStringLiteral("mask_alpha_raster_height")).toInt() <= 0 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_selection_cancelled_count(
          controller) != smart_cancelled_before_commit) {
    return fail("smart selection should expose timing, generation, cancellation, cache-hit, success, fallback, and mask trace telemetry");
  }
  controller.setMaskPreviewMode(QStringLiteral("white-on-black"));
  const auto object_preview_white_on_black = controller.activeMaskPreviewSource();
  controller.setMaskPreviewMode(QStringLiteral("holes"));
  const auto object_preview_holes = controller.activeMaskPreviewSource();
  controller.setMaskPreviewMode(QStringLiteral("halos"));
  if (controller.maskPreviewMode() != QStringLiteral("white-on-black") ||
      object_preview_holes.isEmpty() ||
      controller.activeMaskPreviewSource().isEmpty() ||
      controller.activeMaskPreviewSource() == object_preview_holes) {
    return fail("mask preview should expose explicit holes and halos matte inspection aliases");
  }
  controller.setMaskPreviewMode(QStringLiteral("overlay"));
  if (!controller.activeMaskPreviewSource().isEmpty() ||
      controller.activeMaskOutlinePaths().isEmpty() ||
      controller.maskPreviewDisplayMode() != QStringLiteral("overlay") ||
      !almost_equal(controller.maskPreviewOverlayOpacity(), 0.58)) {
    return fail("normal overlay mode should use clean outline/canvas rendering without a raster preview source");
  }
  const auto display_modes = controller.maskDisplayModeNames();
  const auto required_display_modes = QStringList{
      QStringLiteral("overlay"),
      QStringLiteral("outline"),
      QStringLiteral("marching-ants"),
      QStringLiteral("grayscale-alpha"),
      QStringLiteral("isolated-mask"),
      QStringLiteral("hidden"),
  };
  if (display_modes.contains(QStringLiteral("rubylith")) ||
      display_modes.size() != required_display_modes.size()) {
    return fail("mask preview display mode names should not advertise legacy aliases");
  }
  for (const auto& display_mode : required_display_modes) {
    if (!display_modes.contains(display_mode)) {
      return fail("mask preview should expose predictable display mode names");
    }
  }
  controller.setMaskPreviewMode(QStringLiteral("rubylith"));
  if (controller.maskPreviewMode() != QStringLiteral("overlay") ||
      controller.maskPreviewDisplayMode() != QStringLiteral("overlay") ||
      !controller.activeMaskPreviewSource().isEmpty() ||
      controller.activeMaskOutlinePaths().isEmpty()) {
    return fail("mask preview should accept rubylith as a legacy single-color overlay alias");
  }
  controller.setMaskPreviewMode(QStringLiteral("outline"));
  if (controller.maskPreviewMode() != QStringLiteral("outline") ||
      controller.maskPreviewDisplayMode() != QStringLiteral("outline") ||
      controller.activeMaskOutlinePaths().isEmpty() ||
      !controller.activeMaskPreviewSource().isEmpty()) {
    return fail("mask preview outline mode should be a solid boundary mode without a raster matte source");
  }
  controller.setMaskPreviewMode(QStringLiteral("color overlay on b&w"));
  const auto normalized_color_on_bw_mode = controller.maskPreviewMode();
  const auto object_preview_color_on_bw = controller.activeMaskPreviewSource();
  controller.setMaskPreviewMode(QStringLiteral("image on bw"));
  const auto normalized_image_on_bw_mode = controller.maskPreviewMode();
  const auto object_preview_image_on_bw = controller.activeMaskPreviewSource();
  controller.setMaskPreviewMode(QStringLiteral("isolated-mask"));
  const auto normalized_on_black_mode = controller.maskPreviewMode();
  const auto display_on_black_mode = controller.maskPreviewDisplayMode();
  const auto object_preview_on_black = controller.activeMaskPreviewSource();
  controller.setMaskPreviewMode(QStringLiteral("image-on-white"));
  if (controller.maskPreviewMode() != QStringLiteral("on-white") ||
      normalized_color_on_bw_mode != QStringLiteral("color-on-bw") ||
      normalized_image_on_bw_mode != QStringLiteral("image-on-bw") ||
      normalized_on_black_mode != QStringLiteral("on-black") ||
      display_on_black_mode != QStringLiteral("isolated-mask") ||
      object_preview_color_on_bw.isEmpty() ||
      object_preview_image_on_bw.isEmpty() ||
      object_preview_on_black.isEmpty() ||
      controller.activeMaskPreviewSource().isEmpty()) {
    return fail("mask preview should support Color Overlay, Color on B&W, Image on B&W, Isolated Mask, and On White modes");
  }
  controller.setMaskPreviewMode(QStringLiteral("grayscale-alpha"));
  if (controller.maskPreviewMode() != QStringLiteral("grayscale") ||
      controller.maskPreviewDisplayMode() != QStringLiteral("grayscale-alpha") ||
      controller.activeMaskPreviewSource().isEmpty()) {
    return fail("mask preview should expose grayscale alpha as the predictable display name for matte inspection");
  }
  controller.setMaskPreviewMode(QStringLiteral("on-white"));
  controller.setMaskPreviewOverlayOpacity(0.37);
  controller.setMaskPreviewOverlayColor(QStringLiteral(""));
  const auto automatic_overlay_preview = controller.activeMaskPreviewSource();
  controller.setMaskPreviewOverlayColor(QStringLiteral("#3366cc"));
  const auto custom_overlay_preview = controller.activeMaskPreviewSource();
  controller.setMaskPreviewOverlayColor(QStringLiteral(""));
  controller.setMaskPreviewOverlayOpacity(2.0);
  controller.setMaskPreviewOverlayColor(QStringLiteral("not-a-color"));
  if (!almost_equal(controller.maskPreviewOverlayOpacity(), 1.0) ||
      controller.maskPreviewOverlayColor() != QString() ||
      custom_overlay_preview.isEmpty() ||
      automatic_overlay_preview.isEmpty() ||
      custom_overlay_preview == automatic_overlay_preview) {
    return fail("mask preview should expose clamped overlay opacity and a resettable custom overlay color");
  }
  if (controller.maskPreviewShowAll()) {
    return fail("mask preview should default to active-mask-only display");
  }
  controller.setMaskPreviewShowAll(true);
  if (!controller.maskPreviewShowAll()) {
    return fail("mask preview should expose an explicit show-all-masks toggle");
  }
  controller.setMaskPreviewShowAll(false);
  if (controller.maskPreviewShowAll()) {
    return fail("mask preview show-all toggle should be reversible");
  }
  const auto preview_before_temporary_hide = controller.activeMaskPreviewSource();
  controller.setMaskPreviewTemporarilyHidden(true);
  if (!controller.maskPreviewTemporarilyHidden() ||
      controller.maskPreviewMode() != QStringLiteral("on-white") ||
      !controller.activeMaskPreviewSource().isEmpty()) {
    return fail("temporary mask preview hide should suppress preview output without changing the selected preview mode");
  }
  controller.setMaskPreviewTemporarilyHidden(false);
  if (controller.maskPreviewTemporarilyHidden() ||
      controller.maskPreviewMode() != QStringLiteral("on-white") ||
      controller.activeMaskPreviewSource().isEmpty() ||
      controller.activeMaskPreviewSource() != preview_before_temporary_hide) {
    return fail("temporary mask preview hide should restore the previous preview source when released");
  }
  controller.setMaskPreviewMode(QStringLiteral("marching ants"));
  if (controller.maskPreviewMode() != QStringLiteral("marching-ants") ||
      controller.maskPreviewDisplayMode() != QStringLiteral("marching-ants") ||
      controller.activeMaskOutlinePaths().isEmpty() ||
      !controller.activeMaskPreviewSource().isEmpty()) {
    return fail("mask preview should support marching ants as an outline-only mode");
  }
  controller.setMaskPreviewMode(QStringLiteral("hidden"));
  if (controller.maskPreviewMode() != QStringLiteral("none") ||
      controller.maskPreviewDisplayMode() != QStringLiteral("hidden") ||
      !controller.activeMaskPreviewSource().isEmpty()) {
    return fail("mask preview should expose hidden as the predictable display name for disabling previews");
  }
  controller.setMaskPreviewMode(QStringLiteral("edge-view"));
  const auto object_preview_edge = controller.activeMaskPreviewSource();
  controller.setMaskPreviewMode(QStringLiteral("affected-pixels"));
  if (controller.maskPreviewMode() != QStringLiteral("overlay") ||
      !object_preview_edge.isEmpty() ||
      !controller.activeMaskPreviewSource().isEmpty() ||
      controller.activeMaskOutlinePaths().isEmpty()) {
    return fail("mask preview should normalize edge and affected diagnostics to clean overlay mode without raster matte sources");
  }
  auto smart_mask = controller.maskStack().at(0).toMap();
  if (smart_mask.value(QStringLiteral("name")).toString() != QStringLiteral("Smart Selection 1")) {
    return fail("smart select should label newly created smart selections");
  }
  if (smart_mask.value(QStringLiteral("matteCleanupSchemaVersion")).toInt() != 1 ||
      smart_mask.value(QStringLiteral("matteCleanupProfile")).toString() != QStringLiteral("object-hard-edge") ||
      !smart_mask.value(QStringLiteral("matteCleanupDefaultsApplied")).toBool() ||
      !almost_equal(smart_mask.value(QStringLiteral("selectionSmooth")).toDouble(), 3.0) ||
      !almost_equal(smart_mask.value(QStringLiteral("selectionDespeckle")).toDouble(), 6.0) ||
      !almost_equal(smart_mask.value(QStringLiteral("selectionFillHoles")).toDouble(), 8.0) ||
      !almost_equal(smart_mask.value(QStringLiteral("selectionRemoveIslands")).toDouble(), 10.0) ||
      !almost_equal(smart_mask.value(QStringLiteral("generatedMaskHoleFillThreshold")).toDouble(), 0.0060) ||
      !almost_equal(smart_mask.value(QStringLiteral("generatedMaskIslandRemovalThreshold")).toDouble(), 0.0025)) {
    return fail("smart object selections should inherit object matte cleanup defaults");
  }
  const auto object_aware_metadata =
      smart_mask.value(QStringLiteral("generatedMaskMetadata")).toMap();
  if (!object_aware_metadata.value(QStringLiteral("objectAwareRefinement")).toBool() ||
      object_aware_metadata.value(QStringLiteral("objectAwareRefinementMode")).toString() !=
          QStringLiteral("object-soft-boundary") ||
      object_aware_metadata.value(QStringLiteral("objectAwareRefinementRuntime")).toString().isEmpty() ||
      !object_aware_metadata.value(QStringLiteral("objectAwareDefaultsApplied")).toBool() ||
      !almost_equal(smart_mask.value(QStringLiteral("selectionFineEdge")).toDouble(), 42.0) ||
      !almost_equal(smart_mask.value(QStringLiteral("selectionFeather")).toDouble(), 8.0)) {
    return fail("runtime-backed object selections should get object-aware soft-boundary refinement defaults");
  }
  if (!object_aware_metadata.value(QStringLiteral("colorAwareRefinement")).toBool() ||
      object_aware_metadata.value(QStringLiteral("colorAwareRefinementMode")).toString() !=
          QStringLiteral("high-contrast-hard-edge") ||
      object_aware_metadata.value(QStringLiteral("colorAwareRefinementRuntime")).toString().isEmpty() ||
      !object_aware_metadata.value(QStringLiteral("colorAwareDefaultsApplied")).toBool() ||
      !almost_equal(smart_mask.value(QStringLiteral("selectionEdgeContrast")).toDouble(), 24.0)) {
    return fail("high-contrast object selections should get color-aware hard-edge refinement defaults");
  }
  if (controller.maskSetCount() != 1 ||
      controller.maskSets().at(0).toMap().value(QStringLiteral("sources")).toList().size() != 1) {
    return fail("selection masks should surface as grouped mask sets");
  }
  const auto first_mask_set = controller.maskSets().at(0).toMap();
  const auto first_mask_set_thumbnail =
      first_mask_set.value(QStringLiteral("thumbnailSource")).toString();
  const auto first_mask_source_thumbnail =
      first_mask_set.value(QStringLiteral("sources")).toList().at(0).toMap()
          .value(QStringLiteral("thumbnailSource"))
          .toString();
  if (!first_mask_set_thumbnail.startsWith(QStringLiteral("data:image/png;base64,")) ||
      !first_mask_source_thumbnail.startsWith(QStringLiteral("data:image/png;base64,"))) {
    return fail("mask sets and sources should expose real matte thumbnails");
  }
  auto merged = smart_mask.value(QStringLiteral("segmentationData")).toByteArray();
  auto merged_width = smart_mask.value(QStringLiteral("segmentationWidth")).toInt();
  auto merged_height = smart_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(merged, merged_width, merged_height, 0.18, 0.5) < 128 ||
      mask_value_at(merged, merged_width, merged_height, 0.48, 0.5) > 24 ||
      mask_value_at(merged, merged_width, merged_height, 0.02, 0.02) > 24) {
    return fail("smart select should create a localized mask around the clicked region");
  }

  controller.smartSelectObjectAt(0.48, 0.5, false);
  if (!wait_for_smart_commit(controller, 1)) {
    return fail("async smart select should finish when expanding the active object mask");
  }
  if (controller.maskCount() != 1) {
    return fail("smart select should expand the active object mask instead of adding a new one");
  }
  smart_mask = controller.maskStack().at(0).toMap();
  merged = smart_mask.value(QStringLiteral("segmentationData")).toByteArray();
  merged_width = smart_mask.value(QStringLiteral("segmentationWidth")).toInt();
  merged_height = smart_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(merged, merged_width, merged_height, 0.18, 0.5) < 128 ||
      mask_value_at(merged, merged_width, merged_height, 0.48, 0.5) < 128 ||
      mask_value_at(merged, merged_width, merged_height, 0.80, 0.5) > 24) {
    return fail("smart select should union repeated object picks into one selection");
  }
  if (controller.maskSets().at(0).toMap().value(QStringLiteral("thumbnailSource")).toString() ==
      first_mask_set_thumbnail) {
    return fail("mask set thumbnails should reflect matte changes after mask edits");
  }
  const auto union_outline_paths = controller.activeMaskOutlinePaths();
  const auto union_outline_points = controller.activeMaskOutlinePoints();
  auto union_selected_pixels = 0;
  for (const auto value : merged) {
    if (static_cast<unsigned char>(value) > 0) {
      ++union_selected_pixels;
    }
  }
  if (union_outline_points.size() < 4) {
    const auto detail =
        QStringLiteral(
            "smart select should expose a traced outline for the active object selection "
            "(paths=%1 points=%2 seg=%3x%4 bytes=%5 selected=%6 expand=%7 edgeContrast=%8)")
            .arg(union_outline_paths.size())
            .arg(union_outline_points.size())
            .arg(merged_width)
            .arg(merged_height)
            .arg(merged.size())
            .arg(union_selected_pixels)
            .arg(smart_mask.value(QStringLiteral("selectionExpand")).toDouble())
            .arg(smart_mask.value(QStringLiteral("selectionEdgeContrast")).toDouble())
            .toUtf8();
    return fail(detail.constData());
  }
  auto neutral_object_outline_mask = smart_mask;
  neutral_object_outline_mask.insert(QStringLiteral("selectionExpand"), 0.0);
  neutral_object_outline_mask.insert(QStringLiteral("selectionSmooth"), 0.0);
  neutral_object_outline_mask.insert(QStringLiteral("selectionFineEdge"), 0.0);
  neutral_object_outline_mask.insert(QStringLiteral("selectionEdgeContrast"), 0.0);
  neutral_object_outline_mask.insert(QStringLiteral("selectionDecontaminate"), 0.0);
  neutral_object_outline_mask.insert(QStringLiteral("maskSetSelectionExpand"), 0.0);
  neutral_object_outline_mask.insert(QStringLiteral("maskSetSelectionSmooth"), 0.0);
  neutral_object_outline_mask.insert(QStringLiteral("maskSetSelectionFineEdge"), 0.0);
  neutral_object_outline_mask.insert(QStringLiteral("maskSetSelectionEdgeContrast"), 0.0);
  neutral_object_outline_mask.insert(QStringLiteral("maskSetSelectionDecontaminate"), 0.0);
  neutral_object_outline_mask.insert(QStringLiteral("maskSetSelectionInvert"), false);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          controller,
          neutral_object_outline_mask)) {
    return fail("smart object selection should accept neutral refinement for exact outline bounds proof");
  }
  const auto object_alpha_bounds = alpha_mask_bounds(merged, merged_width, merged_height);
  const auto object_outline_bounds = outline_bounds(controller.activeMaskOutlinePaths());
  const auto object_outline_tolerance =
      outline_tolerance_for_image_pixels(merged_width, merged_height);
  const auto object_outline_trace_fields =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_fields(
          controller);
  if (!outline_bounds_match(object_outline_bounds, object_alpha_bounds, object_outline_tolerance)) {
    const auto detail =
        outline_bounds_detail(QStringLiteral("generated object"), object_outline_bounds, object_alpha_bounds)
            .toUtf8();
    return fail(detail.constData());
  }
  if (std::abs(
          object_outline_trace_fields.value(QStringLiteral("outline_trace_tolerance_normalized")).toDouble() -
          object_outline_tolerance) > 1e-9 ||
      object_outline_trace_fields.value(QStringLiteral("outline_trace_tolerance_pixels")).toDouble() !=
          1.5) {
    return fail("generated object outline tracing should publish the same one-image-pixel tolerance used by bounds tests");
  }

  const auto stale_smart_select_path =
      runtime_root.filePath(QStringLiteral("stale-smart-preview-source.png"));
  QFile::remove(stale_smart_select_path);
  if (!QFile::copy(smart_select_path, stale_smart_select_path)) {
    return fail("stale smart preview source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController stale_preview_state_controller;
  stale_preview_state_controller.loadAsset(
      QStringLiteral("asset-stale-smart-preview-state"),
      stale_smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(stale_preview_state_controller)) {
    return fail("stale smart preview state controller should finish its initial preview");
  }
  const auto stale_preview_generation_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_preview_generation(
          stale_preview_state_controller);
  const auto stale_preview_cancelled_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_selection_cancelled_count(
          stale_preview_state_controller);
  stale_preview_state_controller.scheduleSmartSelectPreview(0.18, 0.5);
  const auto stale_preview_request_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_preview_generation(
          stale_preview_state_controller);
  stale_preview_state_controller.setExposure(0.35);
  auto stale_preview_elapsed_ms = 0;
  while (stale_preview_elapsed_ms < 2000 &&
         photo_editor::desktop::EditorPreviewControllerTestAccess::smart_selection_cancelled_count(
             stale_preview_state_controller) == stale_preview_cancelled_before) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    QThread::msleep(25);
    stale_preview_elapsed_ms += 25;
  }
  const auto stale_preview_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          stale_preview_state_controller);
  if (stale_preview_request_generation != stale_preview_generation_before + 1 ||
      stale_preview_state_controller.smartSelectPreviewActive() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_selection_cancelled_count(
          stale_preview_state_controller) != stale_preview_cancelled_before + 1 ||
      stale_preview_telemetry.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("smart_selection_preview") ||
      !stale_preview_telemetry.value(QStringLiteral("cancelled")).toBool() ||
      stale_preview_telemetry.value(QStringLiteral("cancellation_reason")).toString() !=
          QStringLiteral("stale_result") ||
      stale_preview_telemetry.value(QStringLiteral("stage")).toString() !=
          QStringLiteral("preview_publish") ||
      stale_preview_telemetry.value(QStringLiteral("job_generation")).toInt() !=
          stale_preview_request_generation ||
      stale_preview_telemetry.value(QStringLiteral("async_job_kind")).toString() !=
          QStringLiteral("smart_selection_preview") ||
      stale_preview_telemetry.value(QStringLiteral("async_job_stage")).toString() !=
          QStringLiteral("preview_publish") ||
      !stale_preview_telemetry.value(QStringLiteral("async_job_id"))
           .toString()
           .startsWith(QStringLiteral("smart_selection_preview-%1-")
                           .arg(stale_preview_request_generation)) ||
      stale_preview_telemetry.value(QStringLiteral("async_job_asset_id")).toString() !=
          QStringLiteral("asset-stale-smart-preview-state") ||
      stale_preview_telemetry.value(QStringLiteral("async_job_file_path")).toString() !=
          stale_smart_select_path ||
      !stale_preview_telemetry.value(QStringLiteral("async_job_state_signature"))
           .toString()
           .startsWith(QStringLiteral("sha1:"))) {
    return fail("smart preview publish should reject stale results after the edit state changes");
  }

  qunsetenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION");
  photo_editor::desktop::EditorPreviewController scheduled_preview_controller;
  scheduled_preview_controller.loadAsset(
      QStringLiteral("asset-scheduled-preview"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  scheduled_preview_controller.scheduleSmartSelectPreview(0.18, 0.5);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::thumbnail_refresh_deferred(
          scheduled_preview_controller)) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("pending smart hover preview should keep thumbnail refresh below selection preview priority");
  }

  photo_editor::desktop::EditorPreviewController thumbnail_smart_hover_controller;
  thumbnail_smart_hover_controller.loadAsset(
      QStringLiteral("asset-thumbnail-smart-hover-queue"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(thumbnail_smart_hover_controller)) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("smart-hover thumbnail queue proof should start after the initial preview settles");
  }
  thumbnail_smart_hover_controller.startSelectionBrush(false);
  const auto smart_hover_cached_layers = thumbnail_smart_hover_controller.photoLayers();
  if (smart_hover_cached_layers.isEmpty() ||
      smart_hover_cached_layers.constFirst()
          .toMap()
          .value(QStringLiteral("thumbnailSource"))
          .toString()
          .isEmpty()) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("smart-hover thumbnail queue proof should start with a cached layer thumbnail");
  }
  const auto smart_hover_cached_layer_thumbnail =
      smart_hover_cached_layers.constFirst()
          .toMap()
          .value(QStringLiteral("thumbnailSource"))
          .toString();
  const auto smart_hover_layer_refresh_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_thumbnail_refresh_count(thumbnail_smart_hover_controller);
  const auto smart_hover_deferred_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_thumbnail_refresh_deferred_count(thumbnail_smart_hover_controller);
  const auto smart_hover_queue_scheduled_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_scheduled_count(thumbnail_smart_hover_controller);
  const auto smart_hover_queue_dispatched_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_dispatched_count(thumbnail_smart_hover_controller);
  const auto smart_hover_deferred_state = [&]() {
    return QStringLiteral(
               "deferred=%1 queuePending=%2 queueTimer=%3 queueScheduled=%4 queuePreempted=%5 "
               "queueDispatched=%6 layerTimer=%7 smartPending=%8 smartTimer=%9 smartRunning=%10 "
               "interactivePending=%11 interactiveTimer=%12 interactiveRunning=%13 fullPending=%14 "
               "fullTimer=%15 fullRunning=%16 processing=%17")
        .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::thumbnail_refresh_deferred(
                 thumbnail_smart_hover_controller))
        .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                 thumbnail_refresh_queue_pending(thumbnail_smart_hover_controller))
        .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                 thumbnail_refresh_queue_timer_active(thumbnail_smart_hover_controller))
        .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                 thumbnail_refresh_queue_scheduled_count(thumbnail_smart_hover_controller))
        .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                 thumbnail_refresh_queue_preempted_count(thumbnail_smart_hover_controller))
        .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                 thumbnail_refresh_queue_dispatched_count(thumbnail_smart_hover_controller))
        .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                 photo_layer_adjustment_render_timer_active(thumbnail_smart_hover_controller))
        .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                 smart_select_preview_request_pending(thumbnail_smart_hover_controller))
        .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                 smart_select_preview_timer_active(thumbnail_smart_hover_controller))
        .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                 smart_select_preview_running(thumbnail_smart_hover_controller))
        .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                 interactive_render_pending(thumbnail_smart_hover_controller))
        .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                 interactive_render_timer_active(thumbnail_smart_hover_controller))
        .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                 interactive_render_running(thumbnail_smart_hover_controller))
        .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
            thumbnail_smart_hover_controller))
        .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
            thumbnail_smart_hover_controller))
        .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_running(
            thumbnail_smart_hover_controller))
        .arg(thumbnail_smart_hover_controller.processing());
  };
  if (!thumbnail_smart_hover_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("exposure"),
          0.24)) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("smart-hover thumbnail queue proof should dirty the layer thumbnail cache");
  }
  thumbnail_smart_hover_controller.scheduleSmartSelectPreview(0.22, 0.5);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::thumbnail_refresh_deferred(
          thumbnail_smart_hover_controller)) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("pending smart hover should defer dirty thumbnail refresh work");
  }
  const auto smart_hover_layers_during = thumbnail_smart_hover_controller.photoLayers();
  const auto smart_hover_layer_during =
      smart_hover_layers_during.isEmpty() ? QVariantMap{} : smart_hover_layers_during.constFirst().toMap();
  if (smart_hover_layers_during.isEmpty() ||
      smart_hover_layer_during.value(QStringLiteral("thumbnailSource")).toString() !=
          smart_hover_cached_layer_thumbnail ||
      smart_hover_layer_during.value(QStringLiteral("thumbnailStale")).toBool() != true ||
      smart_hover_layer_during.value(QStringLiteral("thumbnailProgressive")).toBool() != true ||
      smart_hover_layer_during.value(QStringLiteral("thumbnailProgressiveReason")).toString() !=
          QStringLiteral("thumbnail_refresh_deferred") ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_count(thumbnail_smart_hover_controller) !=
          smart_hover_layer_refresh_before ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_deferred_count(thumbnail_smart_hover_controller) !=
          smart_hover_deferred_before + 1 ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_pending(thumbnail_smart_hover_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_refresh_queue_scheduled_count(thumbnail_smart_hover_controller) !=
          smart_hover_queue_scheduled_before + 1) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("smart hover should keep dirty thumbnails stale/progressive and queued below selection preview priority");
  }
  if (!wait_for_smart_preview(thumbnail_smart_hover_controller)) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("smart-hover thumbnail queue proof should publish the hover preview");
  }
  if (!wait_for_thumbnail_queue_dispatch(
          thumbnail_smart_hover_controller,
          smart_hover_queue_dispatched_before + 1)) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    const auto failure_message =
        QStringLiteral("smart-hover thumbnail queue should dispatch after the hover preview settles: %1")
            .arg(smart_hover_deferred_state());
    return fail(qPrintable(failure_message));
  }
  QVariantList smart_hover_layers_after;
  QVariantMap smart_hover_layer_after;
  auto smart_hover_fresh_thumbnail_published = false;
  for (auto elapsed_ms = 0; elapsed_ms < 2000; elapsed_ms += 25) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    smart_hover_layers_after = thumbnail_smart_hover_controller.photoLayers();
    smart_hover_layer_after =
        smart_hover_layers_after.isEmpty() ? QVariantMap{} : smart_hover_layers_after.constFirst().toMap();
    smart_hover_fresh_thumbnail_published =
        !smart_hover_layers_after.isEmpty() &&
        !smart_hover_layer_after.value(QStringLiteral("thumbnailSource")).toString().isEmpty() &&
        !smart_hover_layer_after.value(QStringLiteral("thumbnailStale")).toBool() &&
        !smart_hover_layer_after.value(QStringLiteral("thumbnailProgressive")).toBool() &&
        photo_editor::desktop::EditorPreviewControllerTestAccess::
                photo_layer_thumbnail_refresh_count(thumbnail_smart_hover_controller) >=
            smart_hover_layer_refresh_before + 1;
    if (smart_hover_fresh_thumbnail_published) {
      break;
    }
    QThread::msleep(25);
  }
  if (smart_hover_layers_after.isEmpty() ||
      smart_hover_layer_after.value(QStringLiteral("thumbnailSource")).toString().isEmpty() ||
      smart_hover_layer_after.value(QStringLiteral("thumbnailStale")).toBool() ||
      smart_hover_layer_after.value(QStringLiteral("thumbnailProgressive")).toBool() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_count(thumbnail_smart_hover_controller) <
          smart_hover_layer_refresh_before + 1) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("smart-hover thumbnail queue should publish fresh layer thumbnails after dispatch");
  }

  const auto scheduled_generation_after_first_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_preview_generation(
          scheduled_preview_controller);
  const auto scheduled_duplicate_skips_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_duplicate_skip_count(scheduled_preview_controller);
  scheduled_preview_controller.scheduleSmartSelectPreview(0.18, 0.5);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::
              smart_select_preview_generation(scheduled_preview_controller) !=
          scheduled_generation_after_first_request ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              smart_select_preview_duplicate_skip_count(scheduled_preview_controller) !=
          scheduled_duplicate_skips_before + 1) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("scheduled smart select preview should skip duplicate pending hover requests");
  }
  if (!wait_for_smart_preview(scheduled_preview_controller) ||
      !scheduled_preview_controller.smartSelectPreviewSource().isEmpty() ||
      scheduled_preview_controller.smartSelectPreviewOutlinePoints().size() < 4 ||
      scheduled_preview_controller.smartSelectCandidateHudText() != QStringLiteral("Candidate 1 / 1")) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("scheduled smart select preview should resolve through the debounced fallback path");
  }
  const auto scheduled_generation_after_preview =
      photo_editor::desktop::EditorPreviewControllerTestAccess::smart_select_preview_generation(
          scheduled_preview_controller);
  const auto scheduled_duplicate_skips_after_preview =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          smart_select_preview_duplicate_skip_count(scheduled_preview_controller);
  scheduled_preview_controller.scheduleSmartSelectPreview(0.18, 0.5);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::
              smart_select_preview_generation(scheduled_preview_controller) !=
          scheduled_generation_after_preview ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              smart_select_preview_duplicate_skip_count(scheduled_preview_controller) !=
          scheduled_duplicate_skips_after_preview + 1) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("scheduled smart select preview should skip duplicate visible hover previews");
  }
  if (!scheduled_preview_controller.refineSmartSelectPreviewWithPrompt(0.48, 0.5, true) ||
      !scheduled_preview_controller.smartSelectPreviewActive() ||
      scheduled_preview_controller.maskCount() != 0 ||
      scheduled_preview_controller.photoLayerCount() != 0) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("smart select preview/refine should remain preview-only until the user accepts it");
  }
  if (scheduled_preview_controller.acceptSmartSelectPreview(QStringLiteral("selection"), true) !=
          QStringLiteral("selection-ai") ||
      scheduled_preview_controller.smartSelectPreviewActive() ||
      scheduled_preview_controller.maskCount() != 1 ||
      scheduled_preview_controller.photoLayerCount() != 1 ||
      !scheduled_preview_controller.selectionFailureReason().isEmpty()) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("smart select previews should refine with prompts and accept into a committed selection");
  }
  const auto accepted_preview_metadata =
      scheduled_preview_controller.maskStack().at(0).toMap()
          .value(QStringLiteral("generatedMaskMetadata"))
          .toMap();
  if (accepted_preview_metadata.value(QStringLiteral("negativePromptCount")).toInt() != 1 ||
      accepted_preview_metadata.value(QStringLiteral("smartSelectionPrompts")).toList().size() < 2) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("accepted smart select previews should preserve refine prompt metadata");
  }
  photo_editor::desktop::EditorPreviewController pending_commit_controller;
  pending_commit_controller.loadAsset(
      QStringLiteral("asset-pending-smart-commit"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!pending_commit_controller.beginSmartSelectStroke(0.18, 0.5, 600.0, 400.0) ||
      pending_commit_controller.commitSmartSelectStroke(QStringLiteral("selection"), true) !=
          QStringLiteral("pending") ||
      pending_commit_controller.smartSelectStrokeActive() ||
      pending_commit_controller.maskCount() != 0) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("smart selection stroke release should not block while waiting for an unfinished preview");
  }
  auto pending_commit_elapsed_ms = 0;
  while (pending_commit_elapsed_ms < 2000 && pending_commit_controller.maskCount() == 0) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    QThread::msleep(25);
    pending_commit_elapsed_ms += 25;
  }
  if (pending_commit_controller.maskCount() != 1 ||
      pending_commit_controller.activeMaskKind() != QStringLiteral("object") ||
      !pending_commit_controller.activeMaskCommitted() ||
      pending_commit_controller.smartSelectPreviewActive()) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("pending smart selection commit should apply automatically after the async preview resolves");
  }
  photo_editor::desktop::EditorPreviewController rejected_preview_controller;
  rejected_preview_controller.loadAsset(
      QStringLiteral("asset-rejected-preview"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  rejected_preview_controller.updateSmartSelectPreview(0.18, 0.5);
  rejected_preview_controller.rejectSmartSelectPreview();
  if (rejected_preview_controller.smartSelectPreviewActive() ||
      rejected_preview_controller.maskCount() != 0 ||
      rejected_preview_controller.photoLayerCount() != 0 ||
      rejected_preview_controller.selectionFailureReason() != QStringLiteral("cancelled") ||
      !rejected_preview_controller.status().contains(QStringLiteral("canceled"))) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("smart select previews should reject without committing a selection");
  }
  photo_editor::desktop::EditorPreviewController pending_reject_controller;
  pending_reject_controller.loadAsset(
      QStringLiteral("asset-pending-rejected-preview"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  pending_reject_controller.scheduleSmartSelectPreview(0.18, 0.5);
  pending_reject_controller.rejectSmartSelectPreview();
  if (!pending_reject_controller.status().contains(QStringLiteral("canceled"))) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("smart select preview rejection should report pending request cancellation");
  }
  QThread::msleep(160);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  if (pending_reject_controller.smartSelectPreviewActive() ||
      pending_reject_controller.maskCount() != 0 ||
      pending_reject_controller.photoLayerCount() != 0 ||
      pending_reject_controller.selectionFailureReason() != QStringLiteral("cancelled")) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("smart select preview rejection should cancel pending debounced requests");
  }
  photo_editor::desktop::EditorPreviewController smart_refine_controller;
  smart_refine_controller.loadAsset(
      QStringLiteral("asset-smart-refine"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  smart_refine_controller.startPolygonSelection();
  smart_refine_controller.appendActivePolygonPoint(0.10, 0.35);
  smart_refine_controller.appendActivePolygonPoint(0.28, 0.35);
  smart_refine_controller.appendActivePolygonPoint(0.28, 0.65);
  smart_refine_controller.appendActivePolygonPoint(0.10, 0.65);
  const auto smart_refine_set_id =
      smart_refine_controller.maskStack().at(0).toMap().value(QStringLiteral("maskSetId")).toString();
  if (!smart_refine_controller.refineActiveMaskWithSmartSelect() ||
      !smart_refine_controller.smartSelectPreviewActive() ||
      smart_refine_controller.acceptSmartSelectPreview(QStringLiteral("selection"), false) !=
          QStringLiteral("selection-ai") ||
      smart_refine_controller.maskCount() != 2) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("Smart Refine should preview and accept an object source from an active manual selection");
  }
  const auto smart_refine_source =
      smart_refine_controller.maskStack().at(1).toMap();
  if (smart_refine_source.value(QStringLiteral("kind")).toString() != QStringLiteral("object") ||
      smart_refine_source.value(QStringLiteral("maskSetId")).toString() != smart_refine_set_id ||
      smart_refine_source.value(QStringLiteral("generatedMaskMetadata")).toMap()
              .value(QStringLiteral("boxPromptCount")).toInt() != 1) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("accepted Smart Refine source should stay in the active selection set with box prompt metadata");
  }

  photo_editor::desktop::EditorPreviewController fallback_tiny_object_controller;
  fallback_tiny_object_controller.loadAsset(
      QStringLiteral("asset-fallback-tiny-object"),
      tiny_object_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  fallback_tiny_object_controller.smartSelectObjectAt(0.18, 0.5, true);
  if (!wait_for_smart_commit(fallback_tiny_object_controller, 1)) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("async single-click object fallback should finish without the instance-map path");
  }
  if (fallback_tiny_object_controller.maskCount() != 1 ||
      fallback_tiny_object_controller.activeMaskKind() != QStringLiteral("object")) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("single-click object picks should still work without the instance-map path");
  }
  const auto fallback_tiny_object_mask =
      fallback_tiny_object_controller.maskStack().at(0).toMap();
  const auto fallback_tiny_metadata =
      fallback_tiny_object_mask.value(QStringLiteral("generatedMaskMetadata")).toMap();
  if (fallback_tiny_metadata.value(QStringLiteral("runtime")).toString() !=
          QStringLiteral("Local Smart Select") ||
      fallback_tiny_metadata.value(QStringLiteral("modelName")).toString() !=
          QStringLiteral("Local Smart Select")) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("local object-pick fallback should be labeled Local Smart Select on the committed mask");
  }
  const auto fallback_tiny_object_data =
      fallback_tiny_object_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto fallback_tiny_object_width =
      fallback_tiny_object_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto fallback_tiny_object_height =
      fallback_tiny_object_mask.value(QStringLiteral("segmentationHeight")).toInt();
  const auto fallback_tiny_center =
      mask_value_at(
          fallback_tiny_object_data,
          fallback_tiny_object_width,
          fallback_tiny_object_height,
          0.18,
          0.5);
  const auto fallback_tiny_nearby =
      mask_value_at(
          fallback_tiny_object_data,
          fallback_tiny_object_width,
          fallback_tiny_object_height,
          0.28,
          0.5);
  const auto fallback_tiny_other =
      mask_value_at(
          fallback_tiny_object_data,
          fallback_tiny_object_width,
          fallback_tiny_object_height,
          0.48,
          0.5);
  if (fallback_tiny_center < 128 || fallback_tiny_nearby > 64 || fallback_tiny_other > 24) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("single-click object fallback should prefer the tighter localized object mask");
  }

  photo_editor::desktop::EditorPreviewController mixed_prompt_controller;
  mixed_prompt_controller.loadAsset(
      QStringLiteral("asset-mixed-smart-prompts"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  QVariantList mixed_prompts;
  mixed_prompts.append(QVariantMap{
      {QStringLiteral("type"), QStringLiteral("positive-point")},
      {QStringLiteral("x"), 0.18},
      {QStringLiteral("y"), 0.5},
  });
  mixed_prompts.append(QVariantMap{
      {QStringLiteral("type"), QStringLiteral("negative-point")},
      {QStringLiteral("x"), 0.48},
      {QStringLiteral("y"), 0.5},
  });
  mixed_prompts.append(QVariantMap{
      {QStringLiteral("type"), QStringLiteral("box")},
      {QStringLiteral("left"), 0.11},
      {QStringLiteral("top"), 0.38},
      {QStringLiteral("right"), 0.25},
      {QStringLiteral("bottom"), 0.62},
  });
  mixed_prompts.append(QVariantMap{
      {QStringLiteral("type"), QStringLiteral("brush")},
      {QStringLiteral("x"), 0.13},
      {QStringLiteral("y"), 0.44},
  });
  mixed_prompts.append(QVariantMap{
      {QStringLiteral("type"), QStringLiteral("brush")},
      {QStringLiteral("x"), 0.23},
      {QStringLiteral("y"), 0.56},
  });
  if (mixed_prompt_controller.pickMaskPath(mixed_prompts, QStringLiteral("selection"), true) !=
          QStringLiteral("pending") ||
      !wait_for_smart_commit(mixed_prompt_controller, 1) ||
      mixed_prompt_controller.maskCount() != 1) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("smart selection should accept mixed positive, negative, box, and brush prompts");
  }
  const auto mixed_prompt_mask = mixed_prompt_controller.maskStack().at(0).toMap();
  const auto mixed_prompt_metadata =
      mixed_prompt_mask.value(QStringLiteral("generatedMaskMetadata")).toMap();
  const auto mixed_prompt_types =
      mixed_prompt_metadata.value(QStringLiteral("smartSelectionPromptTypes")).toStringList();
  if (mixed_prompt_metadata.value(QStringLiteral("smartSelectionPromptSchemaVersion")).toInt() != 1 ||
      mixed_prompt_metadata.value(QStringLiteral("positivePromptCount")).toInt() != 1 ||
      mixed_prompt_metadata.value(QStringLiteral("negativePromptCount")).toInt() != 1 ||
      mixed_prompt_metadata.value(QStringLiteral("boxPromptCount")).toInt() != 1 ||
      mixed_prompt_metadata.value(QStringLiteral("brushPromptCount")).toInt() != 2 ||
      !mixed_prompt_types.contains(QStringLiteral("positive-point")) ||
      !mixed_prompt_types.contains(QStringLiteral("negative-point")) ||
      !mixed_prompt_types.contains(QStringLiteral("box")) ||
      !mixed_prompt_types.contains(QStringLiteral("brush")) ||
      mixed_prompt_metadata.value(QStringLiteral("smartSelectionPrompts")).toList().size() != 5) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("committed smart selections should preserve mixed prompt provenance metadata");
  }
  qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");

  photo_editor::desktop::EditorPreviewController controller_backed_stroke_controller;
  controller_backed_stroke_controller.loadAsset(
      QStringLiteral("asset-controller-backed-stroke"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!controller_backed_stroke_controller.beginSmartSelectStroke(0.18, 0.5, 600.0, 400.0) ||
      !controller_backed_stroke_controller.smartSelectStrokeActive() ||
      controller_backed_stroke_controller.smartSelectStrokePoints().size() != 1 ||
      controller_backed_stroke_controller.updateSmartSelectStroke(0.185, 0.5, 600.0, 400.0) ||
      controller_backed_stroke_controller.smartSelectStrokePoints().size() != 1 ||
      !controller_backed_stroke_controller.updateSmartSelectStroke(0.48, 0.5, 600.0, 400.0) ||
      controller_backed_stroke_controller.smartSelectStrokePoints().size() != 2) {
    return fail("controller-backed smart select stroke state should store points once and apply the drag spacing threshold");
  }
  const auto controller_backed_prompts =
      controller_backed_stroke_controller.smartSelectStrokePromptPayload();
  const auto controller_backed_box = controller_backed_prompts.value(0).toMap();
  if (controller_backed_prompts.size() != 3 ||
      controller_backed_box.value(QStringLiteral("type")).toString() != QStringLiteral("box") ||
      std::abs(controller_backed_box.value(QStringLiteral("left")).toDouble() - 0.16) > 1e-6 ||
      std::abs(controller_backed_box.value(QStringLiteral("top")).toDouble() - 0.47) > 1e-6 ||
      std::abs(controller_backed_box.value(QStringLiteral("right")).toDouble() - 0.50) > 1e-6 ||
      std::abs(controller_backed_box.value(QStringLiteral("bottom")).toDouble() - 0.53) > 1e-6 ||
      controller_backed_prompts.value(1).toMap().value(QStringLiteral("type")).toString() !=
          QStringLiteral("positive-point") ||
      controller_backed_prompts.value(2).toMap().value(QStringLiteral("type")).toString() !=
          QStringLiteral("positive-point")) {
    return fail("controller-backed smart select strokes should convert to the same box plus positive prompt payload QML used to build");
  }
  const auto controller_backed_pick =
      controller_backed_stroke_controller.commitSmartSelectStroke(QStringLiteral("selection"), true);
  if ((controller_backed_pick != QStringLiteral("selection-ai") &&
       controller_backed_pick != QStringLiteral("pending")) ||
      (controller_backed_pick == QStringLiteral("pending") &&
       !wait_for_smart_commit(controller_backed_stroke_controller, 1)) ||
      controller_backed_stroke_controller.smartSelectStrokeActive() ||
      !controller_backed_stroke_controller.smartSelectStrokePoints().isEmpty() ||
      controller_backed_stroke_controller.maskCount() != 1 ||
      controller_backed_stroke_controller.activeMaskKind() != QStringLiteral("object")) {
    return fail("controller-backed smart select stroke commit should create one object source and clear transient stroke state");
  }

  photo_editor::desktop::EditorPreviewController stroke_controller;
  stroke_controller.loadAsset(
      QStringLiteral("asset-stroke"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  const QVariantList stroke_points{
      smart_select_point(0.18, 0.5),
      smart_select_point(0.48, 0.5),
  };
  stroke_controller.updateSmartSelectPreviewPath(stroke_points);
  if (!stroke_controller.smartSelectPreviewActive() ||
      stroke_controller.smartSelectPreviewOutlinePoints().size() < 4) {
    return fail("smart select strokes should preview the combined selection footprint before commit");
  }
  const auto stroke_pick =
      stroke_controller.pickMaskPath(stroke_points, QStringLiteral("selection"), true);
  if (stroke_pick != QStringLiteral("pending") ||
      !wait_for_smart_commit(stroke_controller, 1) ||
      stroke_controller.maskCount() != 1 ||
      stroke_controller.activeMaskKind() != QStringLiteral("object")) {
    return fail("smart select strokes should create one object source from a dragged quick-selection pass");
  }
  const auto stroke_mask = stroke_controller.maskStack().at(0).toMap();
  const auto stroke_data = stroke_mask.value(
      QStringLiteral("segmentationData")).toByteArray();
  const auto stroke_width = stroke_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto stroke_height = stroke_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(stroke_data, stroke_width, stroke_height, 0.18, 0.5) < 128 ||
      mask_value_at(stroke_data, stroke_width, stroke_height, 0.48, 0.5) < 128 ||
      mask_value_at(stroke_data, stroke_width, stroke_height, 0.80, 0.5) > 24) {
    return fail("smart select strokes should union every object candidate touched by the drag path");
  }

  photo_editor::desktop::EditorPreviewController no_prompt_controller;
  no_prompt_controller.loadAsset(
      QStringLiteral("asset-no-prompt"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  const auto no_prompt_pick =
      no_prompt_controller.pickMaskPath(QVariantList{}, QStringLiteral("selection"), true);
  if (no_prompt_pick != QStringLiteral("selection-missed") ||
      !no_prompt_controller.status().contains(QStringLiteral("No object found")) ||
      no_prompt_controller.selectionFailureReason() != QStringLiteral("no-target") ||
      !no_prompt_controller.status().contains(QStringLiteral("Click or drag")) ||
      !no_prompt_controller.maskActionRetryAvailable() ||
      no_prompt_controller.maskActionRetryLabel() != QStringLiteral("Retry Selection") ||
      !no_prompt_controller.maskActionRetryDetail().contains(QStringLiteral("No object found")) ||
      no_prompt_controller.retryLastMaskAction()) {
    return fail("empty smart selection prompts should produce an actionable no-object status");
  }

  photo_editor::desktop::EditorPreviewController sparse_stroke_controller;
  sparse_stroke_controller.loadAsset(
      QStringLiteral("asset-sparse-stroke"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  const QVariantList sparse_stroke_points{
      smart_select_point(0.18, 0.5),
      smart_select_point(0.80, 0.5),
  };
  const auto sparse_stroke_pick =
      sparse_stroke_controller.pickMaskPath(sparse_stroke_points, QStringLiteral("selection"), true);
  if (sparse_stroke_pick != QStringLiteral("pending") ||
      !wait_for_smart_commit(sparse_stroke_controller, 1) ||
      sparse_stroke_controller.maskCount() != 1 ||
      sparse_stroke_controller.activeMaskKind() != QStringLiteral("object")) {
    return fail("smart select drags should still create one object source from sparse drag samples");
  }
  const auto sparse_stroke_mask = sparse_stroke_controller.maskStack().at(0).toMap();
  const auto sparse_stroke_data =
      sparse_stroke_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto sparse_stroke_width =
      sparse_stroke_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto sparse_stroke_height =
      sparse_stroke_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(sparse_stroke_data, sparse_stroke_width, sparse_stroke_height, 0.18, 0.5) < 128 ||
      mask_value_at(sparse_stroke_data, sparse_stroke_width, sparse_stroke_height, 0.48, 0.5) < 128 ||
      mask_value_at(sparse_stroke_data, sparse_stroke_width, sparse_stroke_height, 0.80, 0.5) < 128) {
    return fail("smart select drags should sample along the full path so intermediate objects are not skipped");
  }

  controller.smartSelectObjectAt(0.80, 0.5, true);
  if (!wait_for_smart_commit(controller, 2)) {
    return fail("async new smart selection should finish and create a separate object mask");
  }
  if (controller.maskCount() != 2 ||
      controller.activeMaskIndex() != 1) {
    return fail("starting a new smart selection should create a separate object mask");
  }
  controller.clearSmartSelectPreview();
  if (controller.smartSelectPreviewActive()) {
    return fail("smart select preview should clear when the hover state is reset");
  }

  photo_editor::desktop::EditorPreviewController subtract_controller;
  subtract_controller.loadAsset(
      QStringLiteral("asset-subtract"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  subtract_controller.smartSelectAddAt(0.18, 0.5, true);
  if (!wait_for_smart_commit(subtract_controller, 1)) {
    return fail("async smart select add should finish before subtracting");
  }
  subtract_controller.smartSelectSubtractAt(0.48, 0.5, false);
  if (!wait_for_smart_commit(subtract_controller, 2)) {
    return fail("async smart select subtract should finish inside the current mask set");
  }
  if (subtract_controller.maskCount() != 2 ||
      subtract_controller.activeMaskOperation() != QStringLiteral("subtract") ||
      subtract_controller.activeMaskToolRole() != QStringLiteral("selection") ||
      subtract_controller.maskSetCount() != 1 ||
      subtract_controller.maskSets().at(0).toMap().value(QStringLiteral("sources")).toList().size() != 2) {
    return fail("smart select should support positive and negative object clicks inside the current mask set");
  }
  const auto subtract_mask = subtract_controller.maskStack().at(1).toMap();
  if (mask_value_at(
          subtract_mask.value(QStringLiteral("segmentationData")).toByteArray(),
          subtract_mask.value(QStringLiteral("segmentationWidth")).toInt(),
          subtract_mask.value(QStringLiteral("segmentationHeight")).toInt(),
          0.48,
          0.5) < 128) {
    return fail("subtract smart select should rasterize the subtracted region");
  }
  subtract_controller.setActiveMaskIndex(0);
  subtract_controller.setMaskPreviewMode(QStringLiteral("black-on-white"));
  const auto subtract_set_preview_a = subtract_controller.activeMaskPreviewSource();
  subtract_controller.setActiveMaskIndex(1);
  const auto subtract_set_preview_b = subtract_controller.activeMaskPreviewSource();
  if (subtract_set_preview_a.isEmpty() ||
      subtract_set_preview_b.isEmpty() ||
      subtract_set_preview_a != subtract_set_preview_b) {
    return fail("active mask preview should show the full set result, not the selected source only");
  }

  photo_editor::desktop::EditorPreviewController tiny_object_controller;
  tiny_object_controller.loadAsset(
      QStringLiteral("asset-tiny-object"),
      tiny_object_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  tiny_object_controller.smartSelectObjectAt(0.18, 0.5, true);
  if (!wait_for_smart_commit(tiny_object_controller, 1)) {
    return fail("async single-click object pick should finish for tiny targets");
  }
  if (tiny_object_controller.maskCount() != 1 ||
      tiny_object_controller.activeMaskKind() != QStringLiteral("object")) {
    return fail("single-click object picks should still create an object selection for tiny targets");
  }
  const auto tiny_object_mask = tiny_object_controller.maskStack().at(0).toMap();
  const auto tiny_object_data =
      tiny_object_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto tiny_object_width =
      tiny_object_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto tiny_object_height =
      tiny_object_mask.value(QStringLiteral("segmentationHeight")).toInt();
  const auto tiny_center =
      mask_value_at(tiny_object_data, tiny_object_width, tiny_object_height, 0.18, 0.5);
  const auto tiny_nearby =
      mask_value_at(tiny_object_data, tiny_object_width, tiny_object_height, 0.28, 0.5);
  const auto tiny_other =
      mask_value_at(tiny_object_data, tiny_object_width, tiny_object_height, 0.48, 0.5);
  if (tiny_center < 128 || tiny_nearby > 64 || tiny_other > 24) {
    return fail("single-click object picks should prefer the tighter prompt-refined mask for tiny objects");
  }

  photo_editor::desktop::EditorPreviewController range_controller;
  range_controller.loadAsset(
      QStringLiteral("asset-range"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  range_controller.addMask(QStringLiteral("luminance"));
  range_controller.setActiveMaskLuminanceMin(0.1);
  range_controller.setActiveMaskLuminanceMax(0.85);
  if (range_controller.activeMaskKind() != QStringLiteral("luminance") ||
      !range_controller.activeMaskPreviewSource().isEmpty()) {
    return fail("luminance range masks should keep normal overlay mode source-free");
  }
  range_controller.addMask(QStringLiteral("color"));
  range_controller.setActiveMaskHueCenter(0.0);
  range_controller.setActiveMaskHueRange(0.12);
  range_controller.setActiveMaskSaturationMin(0.2);
  range_controller.setActiveMaskSaturationMax(1.0);
  if (range_controller.activeMaskKind() != QStringLiteral("color") ||
      !range_controller.activeMaskPreviewSource().isEmpty()) {
    return fail("color range masks should keep normal overlay mode source-free");
  }
  range_controller.setMaskPreviewMode(QStringLiteral("black-on-white"));
  const auto range_neutral_preview = range_controller.activeMaskPreviewSource();
  if (range_controller.maskPreviewMode() != QStringLiteral("black-on-white") ||
      range_neutral_preview.isEmpty()) {
    return fail("range mask previews should support black-on-white matte inspection");
  }
  range_controller.setMaskPreviewMode(QStringLiteral("white-on-black"));
  if (range_controller.maskPreviewMode() != QStringLiteral("white-on-black") ||
      range_controller.activeMaskPreviewSource().isEmpty() ||
      range_controller.activeMaskPreviewSource() == range_neutral_preview) {
    return fail("range mask previews should support the dedicated neutral preview modes");
  }

  photo_editor::desktop::EditorPreviewController missed_pick_controller;
  missed_pick_controller.loadAsset(
      QStringLiteral("asset-missed-pick"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  const auto missed_selection =
      missed_pick_controller.pickMaskAt(0.02, 0.02, QStringLiteral("selection"), false);
  if (missed_selection != QStringLiteral("pending") ||
      !wait_for_smart_commit(missed_pick_controller, 1) ||
      missed_pick_controller.maskCount() != 1 ||
      !missed_pick_controller.hasActiveMask() ||
      missed_pick_controller.activeMaskKind() != QStringLiteral("ellipse")) {
    return fail("missed smart selection should create an editable rough fallback mask");
  }
  const auto missed_fallback_mask = missed_pick_controller.maskStack().at(0).toMap();
  const auto missed_fallback_metadata =
      missed_fallback_mask.value(QStringLiteral("generatedMaskMetadata")).toMap();
  if (!missed_fallback_metadata.value(QStringLiteral("smartFallback")).toBool() ||
      missed_fallback_metadata.value(QStringLiteral("smartFallbackRuntime")).toString().isEmpty() ||
      missed_fallback_metadata.value(QStringLiteral("smartFallbackReason")).toString().isEmpty() ||
      missed_fallback_mask.value(QStringLiteral("centerX")).toDouble() <= 0.0 ||
      missed_fallback_mask.value(QStringLiteral("radiusX")).toDouble() < 0.05 ||
      missed_fallback_metadata.value(QStringLiteral("smartSelectionPrompts")).toList().isEmpty()) {
    return fail("rough smart selection fallback should be editable and preserve miss provenance");
  }

  const auto missed_remove =
      missed_pick_controller.pickMaskAt(0.02, 0.02, QStringLiteral("remove"), false);
  if (missed_remove != QStringLiteral("pending") ||
      !wait_for_preview_idle(missed_pick_controller, 4000) ||
      missed_pick_controller.maskCount() != 1 ||
      !missed_pick_controller.hasActiveMask()) {
    return fail("missed object remove should not create an implicit remove brush fallback");
  }

  photo_editor::desktop::EditorPreviewController remove_controller;
  remove_controller.loadAsset(
      QStringLiteral("asset-remove"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  const auto mask_memory_policy = remove_controller.maskMemoryPolicySignature();
  if (!mask_memory_policy.contains(QStringLiteral("foreground-mattes:67108864")) ||
      !mask_memory_policy.contains(QStringLiteral("mask-tiles:134217728")) ||
      !mask_memory_policy.contains(QStringLiteral("mask-thumbnails:524288")) ||
      !mask_memory_policy.contains(QStringLiteral("inpaint-patch:100663296")) ||
      !mask_memory_policy.contains(QStringLiteral("inpaint-candidates:33554432:5")) ||
      !mask_memory_policy.contains(QStringLiteral("embeddings:0"))) {
    return fail("mask memory policy should expose bounded generated matte, thumbnail, inpaint, and embedding caches");
  }
  remove_controller.objectRemoveAt(0.18, 0.5, true);
  if (!wait_for_smart_commit(remove_controller, 1)) {
    return fail("async object remove should finish and stage a retouch object mask");
  }
  if (remove_controller.maskCount() != 1 ||
      remove_controller.activeMaskKind() != QStringLiteral("object") ||
      remove_controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      remove_controller.activeMaskRetouchMode() != QStringLiteral("remove") ||
      remove_controller.activeMaskCommitted()) {
    return fail("object remove should create a staged retouch object mask");
  }
  const auto staged_object_remove_mask = remove_controller.maskStack().at(0).toMap();
  if (!staged_object_remove_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() ||
      staged_object_remove_mask.value(QStringLiteral("inpaintPatchWidth")).toInt() > 0 ||
      staged_object_remove_mask.value(QStringLiteral("inpaintCandidateCount")).toInt() > 0) {
    return fail("object remove selection creation should not generate an inpaint patch");
  }
  const auto staged_object_remove_metadata =
      staged_object_remove_mask.value(QStringLiteral("generatedMaskMetadata")).toMap();
  if (staged_object_remove_metadata.value(QStringLiteral("objectRemoveAssisted")).toBool() != true ||
      staged_object_remove_metadata.value(QStringLiteral("objectRemoveAssistMode")).toString() !=
          QStringLiteral("object-plus-shadow-reflection") ||
      staged_object_remove_metadata.value(QStringLiteral("objectRemoveExpandedPixelCount")).toInt() <=
          staged_object_remove_metadata.value(QStringLiteral("objectRemoveOriginalPixelCount")).toInt() ||
      staged_object_remove_metadata.value(QStringLiteral("objectRemoveExpansionPixels")).toInt() <= 0 ||
      staged_object_remove_metadata.value(QStringLiteral("objectRemoveShadowReflectionPixels")).toInt() <= 0 ||
      staged_object_remove_metadata.value(QStringLiteral("smartSelectionPrompts")).toList().isEmpty()) {
    return fail("object-detect remove should expand the target matte for likely edge, shadow, and reflection pixels");
  }
  remove_controller.startRemoveBrush();
  if (remove_controller.activeMaskKind() != QStringLiteral("brush") ||
      remove_controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      remove_controller.activeMaskRetouchMode() != QStringLiteral("remove") ||
      remove_controller.activeMaskCommitted()) {
    return fail("remove brush should join the staged retouch workflow");
  }
  const auto staged_brush_remove_mask = remove_controller.maskStack().at(1).toMap();
  if (!staged_brush_remove_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() ||
      staged_brush_remove_mask.value(QStringLiteral("inpaintPatchWidth")).toInt() > 0 ||
      staged_brush_remove_mask.value(QStringLiteral("inpaintCandidateCount")).toInt() > 0 ||
      remove_controller.retouchWorkflowStateSignature().indexOf(QStringLiteral("remove:draft")) < 0) {
    return fail("remove brush creation should create only an editable draft selection");
  }
  remove_controller.beginActiveBrushStroke();
  remove_controller.addBrushPoint(0.22, 0.52);
  (void)finish_brush_stroke(remove_controller);
  if (!remove_controller.activeMaskPreviewSource().isEmpty() ||
      remove_controller.activeMaskOutlinePaths().isEmpty()) {
    return fail("remove brush should show a source-free clean outline before the retouch is applied");
  }
  const auto highlighted_brush_remove_mask = remove_controller.maskStack().at(1).toMap();
  if (!highlighted_brush_remove_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() ||
      highlighted_brush_remove_mask.value(QStringLiteral("inpaintPatchWidth")).toInt() > 0 ||
      highlighted_brush_remove_mask.value(QStringLiteral("inpaintCandidateCount")).toInt() > 0) {
    return fail("painting a remove selection should preview the matte without generating an inpaint patch");
  }
  qputenv("PHOTO_EDITOR_TEST_FAKE_AI_MODELS", "1");
  const auto inpaint_generation_before_apply =
      photo_editor::desktop::EditorPreviewControllerTestAccess::inpaint_job_generation(
          remove_controller);
  const auto inpaint_cancelled_before_apply =
      photo_editor::desktop::EditorPreviewControllerTestAccess::inpaint_cancelled_count(
          remove_controller);
  const auto inpaint_state_revision_before_apply =
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_work_state_revision(
          remove_controller);
  remove_controller.applyActiveRetouch();
  if (!wait_for_preview_idle(remove_controller, 6000)) {
    return fail("async remove inpaint apply should finish before committed masks are inspected");
  }
  if (!remove_controller.maskStack().at(0).toMap().value(QStringLiteral("committed")).toBool() ||
      !remove_controller.maskStack().at(1).toMap().value(QStringLiteral("committed")).toBool()) {
    return fail("applying a retouch should commit every pending retouch mask in the session");
  }
  const auto first_committed_inpaint_mask = remove_controller.maskStack().at(0).toMap();
  const auto second_committed_inpaint_mask = remove_controller.maskStack().at(1).toMap();
  if (first_committed_inpaint_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() ||
      first_committed_inpaint_mask.value(QStringLiteral("inpaintPatchWidth")).toInt() <= 0 ||
      second_committed_inpaint_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() ||
      first_committed_inpaint_mask.value(QStringLiteral("inpaintPatchPayloadBytes")).toLongLong() <= 0 ||
      first_committed_inpaint_mask.value(QStringLiteral("inpaintCandidatePayloadBytes")).toLongLong() <= 0 ||
      first_committed_inpaint_mask.value(QStringLiteral("inpaintCandidatePayloadBytes")).toLongLong() > 33554432 ||
      first_committed_inpaint_mask.value(QStringLiteral("inpaintCandidateCount")).toInt() > 5 ||
      first_committed_inpaint_mask.value(QStringLiteral("inpaintCandidateEvictedCount")).toInt() < 0) {
    return fail("applying a remove retouch with AI inpaint available should bake patch data into committed masks");
  }
  const auto inpaint_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          remove_controller);
  if (inpaint_telemetry.value(QStringLiteral("operation")).toString() != QStringLiteral("inpaint") ||
      inpaint_telemetry.value(QStringLiteral("inpaint_ms")).toLongLong() < 0 ||
      inpaint_telemetry.value(QStringLiteral("retouch_mode")).toString() != QStringLiteral("remove") ||
      !inpaint_telemetry.value(QStringLiteral("success")).toBool() ||
      inpaint_telemetry.value(QStringLiteral("cancelled")).toBool() ||
      inpaint_telemetry.value(QStringLiteral("job_generation")).toInt() <=
          inpaint_generation_before_apply ||
      inpaint_telemetry.value(QStringLiteral("state_revision")).toInt() !=
          inpaint_state_revision_before_apply ||
      inpaint_telemetry.value(QStringLiteral("async_job_kind")).toString() !=
          QStringLiteral("inpaint") ||
      inpaint_telemetry.value(QStringLiteral("async_job_stage")).toString() !=
          QStringLiteral("apply") ||
      inpaint_telemetry.value(QStringLiteral("inpaint_execution_mode")).toString() !=
          QStringLiteral("async_worker") ||
      inpaint_telemetry.value(QStringLiteral("inpaint_decode_edge")).toInt() != 1024 ||
      inpaint_telemetry.value(QStringLiteral("inpaint_worker_thread")).toString().isEmpty() ||
      inpaint_telemetry.value(QStringLiteral("inpaint_worker_elapsed_ms")).toLongLong() < 0 ||
      !inpaint_telemetry.value(QStringLiteral("async_job_id"))
           .toString()
           .startsWith(QStringLiteral("inpaint-")) ||
      inpaint_telemetry.value(QStringLiteral("async_job_asset_id")).toString() !=
          inpaint_telemetry.value(QStringLiteral("asset_id")).toString() ||
      !inpaint_telemetry.value(QStringLiteral("async_job_state_signature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      inpaint_telemetry.value(QStringLiteral("memory_policy_signature")).toString() !=
          remove_controller.maskMemoryPolicySignature() ||
      inpaint_telemetry.value(QStringLiteral("inpaint_patch_payload_bytes")).toLongLong() <= 0 ||
      inpaint_telemetry.value(QStringLiteral("inpaint_candidate_payload_bytes")).toLongLong() <= 0 ||
      inpaint_telemetry.value(QStringLiteral("inpaint_candidate_payload_bytes")).toLongLong() > 67108864 ||
      inpaint_telemetry.value(QStringLiteral("inpaint_candidate_evicted_count")).toInt() < 0 ||
      inpaint_telemetry.value(QStringLiteral("source_mask_width")).toInt() <= 0 ||
      inpaint_telemetry.value(QStringLiteral("source_mask_pixels")).toInt() <= 0 ||
      inpaint_telemetry.value(QStringLiteral("patch_width")).toInt() <= 0 ||
      !inpaint_telemetry.contains(QStringLiteral("fallback_reason")) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::inpaint_cancelled_count(
          remove_controller) != inpaint_cancelled_before_apply) {
    return fail("inpaint apply should expose timing, generation, cancellation, source-mask, patch, success, and fallback telemetry");
  }
  if (remove_controller.retouchWorkflowStateSignature().indexOf(QStringLiteral("remove:applied")) < 0) {
    return fail("remove workflow should move from draft selection to applied patch only after Apply");
  }
  qunsetenv("PHOTO_EDITOR_TEST_FAKE_AI_MODELS");

  QImage inpaint_texture_source(96, 96, QImage::Format_ARGB32);
  for (int y = 0; y < inpaint_texture_source.height(); ++y) {
    for (int x = 0; x < inpaint_texture_source.width(); ++x) {
      const auto grain = ((x * 17 + y * 29) % 11) - 5;
      inpaint_texture_source.setPixelColor(
          x,
          y,
          QColor::fromRgb(
              std::clamp(110 + x + grain, 0, 255),
              std::clamp(116 + y + grain, 0, 255),
              std::clamp(128 + grain, 0, 255)));
    }
  }
  const auto inpaint_texture_path = runtime_root.filePath(QStringLiteral("cpu-inpaint-texture.png"));
  if (!inpaint_texture_source.save(inpaint_texture_path)) {
    return fail("CPU inpaint texture test image should be writable");
  }

  photo_editor::desktop::EditorPreviewController remove_refine_controller;
  remove_refine_controller.loadAsset(
      QStringLiteral("asset-remove-refine"),
      inpaint_texture_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  remove_refine_controller.startRemoveBrush();
  remove_refine_controller.setActiveMaskBrushSize(0.22);
  remove_refine_controller.addBrushPoint(0.40, 0.50);
  const auto remove_refine_base_mask = remove_refine_controller.maskStack().at(0).toMap();
  if (remove_refine_base_mask.value(QStringLiteral("operation")).toString() != QStringLiteral("add") ||
      selected_pixel_count(remove_refine_base_mask.value(QStringLiteral("segmentationData")).toByteArray()) <= 0) {
    return fail("remove refine should start with an additive pending selection matte");
  }
  remove_refine_controller.startRemoveRefineBrush(false);
  if (remove_refine_controller.activeMaskOperation() != QStringLiteral("add") ||
      remove_refine_controller.activeMaskRetouchMode() != QStringLiteral("remove")) {
    return fail("remove add refine should create an additive pending remove source");
  }
  remove_refine_controller.setActiveMaskBrushSize(0.18);
  remove_refine_controller.addBrushPoint(0.66, 0.50);
  const auto remove_refine_add_mask =
      remove_refine_controller.maskStack().at(remove_refine_controller.activeMaskIndex()).toMap();
  const auto remove_refine_additive_count =
      selected_pixel_count(remove_refine_base_mask.value(QStringLiteral("segmentationData")).toByteArray()) +
      selected_pixel_count(remove_refine_add_mask.value(QStringLiteral("segmentationData")).toByteArray());
  remove_refine_controller.startRemoveRefineBrush(true);
  if (remove_refine_controller.activeMaskOperation() != QStringLiteral("subtract") ||
      remove_refine_controller.activeMaskRetouchMode() != QStringLiteral("remove")) {
    return fail("remove subtract refine should create a subtractive pending remove source");
  }
  remove_refine_controller.setActiveMaskBrushSize(0.16);
  remove_refine_controller.addBrushPoint(0.40, 0.50);
  const auto remove_refine_subtract_mask =
      remove_refine_controller.maskStack().at(remove_refine_controller.activeMaskIndex()).toMap();
  if (selected_pixel_count(remove_refine_subtract_mask.value(QStringLiteral("segmentationData")).toByteArray()) <= 0) {
    return fail("remove subtract refine should paint a matte before Apply");
  }
  qputenv("PHOTO_EDITOR_TEST_FAKE_AI_MODELS", "1");
  remove_refine_controller.applyActiveRetouch();
  if (!wait_for_preview_idle(remove_refine_controller, 6000)) {
    return fail("async refined remove inpaint apply should finish before inspecting the patch");
  }
  qunsetenv("PHOTO_EDITOR_TEST_FAKE_AI_MODELS");
  const auto refined_remove_patch_mask = remove_refine_controller.maskStack().at(0).toMap();
  const auto refined_remove_source_count =
      selected_pixel_count(refined_remove_patch_mask.value(QStringLiteral("inpaintSourceMaskData")).toByteArray());
  if (refined_remove_source_count <= 0 ||
      refined_remove_source_count >= remove_refine_additive_count ||
      refined_remove_patch_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty()) {
    return fail("remove apply should generate the inpaint patch from the add/subtract refined matte");
  }

  photo_editor::desktop::EditorPreviewController fallback_inpaint_controller;
  fallback_inpaint_controller.loadAsset(
      QStringLiteral("asset-cpu-inpaint"),
      inpaint_texture_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  fallback_inpaint_controller.setInpaintPrompt(QStringLiteral("preserve fine grain texture"));
  fallback_inpaint_controller.setInpaintSeed(42);
  fallback_inpaint_controller.regenerateInpaintSeed();
  if (fallback_inpaint_controller.inpaintSeed() != 43) {
    return fail("regenerating inpaint should advance the seed control");
  }
  fallback_inpaint_controller.setInpaintBlendAmount(0.42);
  fallback_inpaint_controller.setInpaintQualityMode(QStringLiteral("final"));
  fallback_inpaint_controller.startRemoveBrush();
  fallback_inpaint_controller.beginActiveBrushStroke();
  fallback_inpaint_controller.addBrushPoint(0.48, 0.5);
  (void)finish_brush_stroke(fallback_inpaint_controller);
  fallback_inpaint_controller.applyActiveRetouch();
  if (!wait_for_preview_idle(fallback_inpaint_controller, 6000)) {
    return fail("async final-quality fallback inpaint apply should finish before inspecting the patch");
  }
  const auto fallback_mask = fallback_inpaint_controller.maskStack().at(0).toMap();
  const auto fallback_patch_image =
      QImage::fromData(fallback_mask.value(QStringLiteral("inpaintPatchData")).toByteArray(), "PNG");
  const auto fallback_candidates_for_color = fallback_mask.value(QStringLiteral("inpaintCandidates")).toList();
  if (fallback_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() ||
      fallback_mask.value(QStringLiteral("inpaintPatchColorSpace")).toString() != QStringLiteral("sRGB") ||
      fallback_patch_image.isNull() || !fallback_patch_image.colorSpace().isValid() ||
      fallback_patch_image.colorSpace() != QColorSpace(QColorSpace::SRgb) ||
      !fallback_mask.value(QStringLiteral("inpaintFallback")).toBool() ||
      fallback_mask.value(QStringLiteral("inpaintModelName")).toString() !=
          QStringLiteral("cpu-diffusion-inpaint-v1") ||
      fallback_mask.value(QStringLiteral("inpaintPrompt")).toString() !=
          QStringLiteral("preserve fine grain texture") ||
      std::abs(fallback_mask.value(QStringLiteral("inpaintSeamFeather")).toDouble() - 4.0) > 1e-6 ||
      std::abs(fallback_mask.value(QStringLiteral("inpaintColorMatch")).toDouble() - 0.75) > 1e-6 ||
      fallback_mask.value(QStringLiteral("inpaintFinalRequired")).toBool() ||
      fallback_mask.value(QStringLiteral("inpaintSeed")).toInt() != 43 ||
      std::abs(fallback_mask.value(QStringLiteral("inpaintBlendAmount")).toDouble() - 0.42) > 1e-6 ||
      fallback_mask.value(QStringLiteral("inpaintQualityMode")).toString() != QStringLiteral("final") ||
      fallback_mask.value(QStringLiteral("inpaintSourceMaskData")).toByteArray().isEmpty() ||
      fallback_mask.value(QStringLiteral("inpaintSourceMaskWidth")).toInt() !=
          inpaint_texture_source.width() ||
      fallback_mask.value(QStringLiteral("inpaintSourceMaskHeight")).toInt() !=
          inpaint_texture_source.height() ||
      fallback_mask.value(QStringLiteral("inpaintSourceBoundsWidth")).toInt() <= 0 ||
      fallback_mask.value(QStringLiteral("inpaintSourceBoundsHeight")).toInt() <= 0 ||
      fallback_mask.value(QStringLiteral("inpaintCandidateCount")).toInt() < 3 ||
      fallback_mask.value(QStringLiteral("inpaintCandidateCount")).toInt() > 5 ||
      fallback_mask.value(QStringLiteral("inpaintPatchPayloadBytes")).toLongLong() <= 0 ||
      fallback_mask.value(QStringLiteral("inpaintCandidatePayloadBytes")).toLongLong() <= 0 ||
      fallback_mask.value(QStringLiteral("inpaintCandidatePayloadBytes")).toLongLong() > 33554432 ||
      fallback_mask.value(QStringLiteral("inpaintCandidateEvictedCount")).toInt() < 0 ||
      fallback_candidates_for_color.size() < 3 ||
      fallback_candidates_for_color.at(0).toMap().value(QStringLiteral("patchColorSpace")).toString() !=
          QStringLiteral("sRGB") ||
      fallback_mask.value(QStringLiteral("inpaintSelectedVariationIndex")).toInt() != 0 ||
      fallback_mask.value(QStringLiteral("inpaintSelectedVariationSeed")).toInt() != 43 ||
      fallback_mask.value(QStringLiteral("inpaintSelectedVariationModelName")).toString() !=
          QStringLiteral("cpu-diffusion-inpaint-v1") ||
      fallback_mask.value(QStringLiteral("inpaintRegionKind")).toString().isEmpty() ||
      !fallback_mask.value(QStringLiteral("inpaintEdgeContinuation")).toBool() ||
      !fallback_mask.value(QStringLiteral("inpaintTextureSynthesis")).toBool() ||
      fallback_mask.value(QStringLiteral("inpaintTextureStrength")).toDouble() <= 0.0 ||
      fallback_mask.value(QStringLiteral("inpaintRegionCoverage")).toDouble() <= 0.0 ||
      fallback_mask.value(QStringLiteral("inpaintRegionWidth")).toInt() <= 0 ||
      fallback_mask.value(QStringLiteral("inpaintRegionHeight")).toInt() <= 0) {
    std::cerr << "fallback inpaint patch bytes="
              << fallback_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().size()
              << " fallback=" << fallback_mask.value(QStringLiteral("inpaintFallback")).toBool()
              << " model="
              << fallback_mask.value(QStringLiteral("inpaintModelName")).toString().toStdString()
              << " prompt="
              << fallback_mask.value(QStringLiteral("inpaintPrompt")).toString().toStdString()
              << " seed=" << fallback_mask.value(QStringLiteral("inpaintSeed")).toInt()
              << " blend=" << fallback_mask.value(QStringLiteral("inpaintBlendAmount")).toDouble()
              << " quality="
              << fallback_mask.value(QStringLiteral("inpaintQualityMode")).toString().toStdString()
              << " sourceMask="
              << fallback_mask.value(QStringLiteral("inpaintSourceMaskData")).toByteArray().size()
              << " candidates=" << fallback_mask.value(QStringLiteral("inpaintCandidateCount")).toInt()
              << " region="
              << fallback_mask.value(QStringLiteral("inpaintRegionKind")).toString().toStdString()
              << " edge=" << fallback_mask.value(QStringLiteral("inpaintEdgeContinuation")).toBool()
              << " texture=" << fallback_mask.value(QStringLiteral("inpaintTextureSynthesis")).toBool()
              << " textureStrength="
              << fallback_mask.value(QStringLiteral("inpaintTextureStrength")).toDouble()
              << " coverage="
              << fallback_mask.value(QStringLiteral("inpaintRegionCoverage")).toDouble()
              << " status=" << fallback_inpaint_controller.status().toStdString()
              << "\n";
    return fail("remove retouch should bake a deterministic CPU inpaint patch when the AI model is unavailable");
  }
  if (fallback_inpaint_controller.maskActionRetryAvailable()) {
    return fail("successful fallback inpaint patches should not leave a retry action active");
  }
  if (fallback_inpaint_controller.activeInpaintFinalizationAvailable()) {
    return fail("final-quality fallback inpaint patches should not require finalization");
  }
  if (!fallback_inpaint_controller.status().contains(QStringLiteral("Local CPU Fill patch baked")) ||
      fallback_inpaint_controller.status().contains(QStringLiteral("LaMa inpaint patch baked")) ||
      fallback_inpaint_controller.status().contains(QStringLiteral("CPU inpaint patch baked"))) {
    std::cerr << "fallback inpaint status="
              << fallback_inpaint_controller.status().toStdString()
              << " fallback="
              << fallback_mask.value(QStringLiteral("inpaintFallback")).toBool()
              << " model="
              << fallback_mask.value(QStringLiteral("inpaintModelName")).toString().toStdString()
              << "\n";
    return fail("successful fallback inpaint status should clearly say Local CPU Fill, not LaMa or generic CPU inpaint");
  }

  photo_editor::desktop::EditorPreviewController missing_inpaint_controller;
  missing_inpaint_controller.loadAsset(
      QStringLiteral("asset-missing-inpaint"),
      runtime_root.filePath(QStringLiteral("missing-inpaint-source.png")),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  missing_inpaint_controller.startRemoveBrush();
  missing_inpaint_controller.beginActiveBrushStroke();
  missing_inpaint_controller.addBrushPoint(0.5, 0.5);
  (void)finish_brush_stroke(missing_inpaint_controller);
  const auto missing_seed_before = missing_inpaint_controller.inpaintSeed();
  missing_inpaint_controller.applyActiveRetouch();
  if (!wait_for_preview_idle(missing_inpaint_controller, 6000)) {
    return fail("failed async inpaint apply should settle before exposing retry state");
  }
  if (!missing_inpaint_controller.maskActionRetryAvailable() ||
      missing_inpaint_controller.maskActionRetryLabel() != QStringLiteral("Retry Inpaint") ||
      !missing_inpaint_controller.maskActionRetryDetail().contains(QStringLiteral("Unable to decode")) ||
      !missing_inpaint_controller.retryLastMaskAction()) {
    return fail("failed inpaint should expose and dispatch a one-click retry action");
  }
  if (!wait_for_preview_idle(missing_inpaint_controller, 6000) ||
      missing_inpaint_controller.inpaintSeed() != missing_seed_before + 1 ||
      !missing_inpaint_controller.maskActionRetryAvailable()) {
    return fail("failed inpaint should expose and dispatch a one-click retry action");
  }
  if (fallback_inpaint_controller.retouchBeforeAfterEnabled()) {
    return fail("retouch before/after should default to the after view");
  }
  const auto committed_retouch_before_compare =
      fallback_inpaint_controller.maskStack().at(0).toMap();
  const auto compare_patch_data =
      committed_retouch_before_compare.value(QStringLiteral("inpaintPatchData")).toByteArray();
  const auto compare_source_mask =
      committed_retouch_before_compare.value(QStringLiteral("inpaintSourceMaskData")).toByteArray();
  const auto compare_candidates =
      committed_retouch_before_compare.value(QStringLiteral("inpaintCandidates")).toList();
  const auto compare_selected_seed =
      committed_retouch_before_compare.value(QStringLiteral("inpaintSelectedVariationSeed")).toInt();
  fallback_inpaint_controller.setRetouchBeforeAfterEnabled(true);
  if (!fallback_inpaint_controller.retouchBeforeAfterEnabled()) {
    return fail("retouch before/after toggle should enter the before view");
  }
  const auto before_request_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(
          fallback_inpaint_controller);
  if (before_request_state.mask_stack.isEmpty() ||
      before_request_state.mask_stack.at(0)
           .toMap()
           .value(QStringLiteral("enabled"), true)
           .toBool()) {
    return fail("retouch before view should bypass retouch and inpaint masks in preview render state");
  }
  const auto before_request_mask = before_request_state.mask_stack.at(0).toMap();
  if (before_request_mask.value(QStringLiteral("inpaintPatchData")).toByteArray() !=
          compare_patch_data ||
      before_request_mask.value(QStringLiteral("inpaintSourceMaskData")).toByteArray() !=
          compare_source_mask ||
      before_request_mask.value(QStringLiteral("inpaintCandidates")).toList().size() !=
          compare_candidates.size() ||
      before_request_mask.value(QStringLiteral("inpaintSelectedVariationSeed")).toInt() !=
          compare_selected_seed) {
    return fail("retouch before view should preserve patch payload while bypassing it for preview");
  }
  const auto committed_retouch_during_compare =
      fallback_inpaint_controller.maskStack().at(0).toMap();
  if (!committed_retouch_during_compare.value(QStringLiteral("enabled")).toBool() ||
      committed_retouch_during_compare.value(QStringLiteral("inpaintPatchData")).toByteArray() !=
          compare_patch_data ||
      committed_retouch_during_compare.value(QStringLiteral("inpaintSourceMaskData")).toByteArray() !=
          compare_source_mask ||
      committed_retouch_during_compare.value(QStringLiteral("inpaintSelectedVariationSeed")).toInt() !=
          compare_selected_seed) {
    return fail("retouch before view should not mutate the committed retouch recipe data");
  }
  fallback_inpaint_controller.setRetouchBeforeAfterEnabled(false);
  const auto after_request_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(
          fallback_inpaint_controller);
  if (after_request_state.mask_stack.isEmpty() ||
      !after_request_state.mask_stack.at(0).toMap().value(QStringLiteral("enabled")).toBool()) {
    return fail("retouch after view should restore retouch masks in preview render state");
  }
  const auto after_request_mask = after_request_state.mask_stack.at(0).toMap();
  const auto committed_retouch_after_compare =
      fallback_inpaint_controller.maskStack().at(0).toMap();
  if (after_request_mask.value(QStringLiteral("inpaintPatchData")).toByteArray() !=
          compare_patch_data ||
      committed_retouch_after_compare.value(QStringLiteral("inpaintPatchData")).toByteArray() !=
          compare_patch_data ||
      committed_retouch_after_compare.value(QStringLiteral("inpaintSourceMaskData")).toByteArray() !=
          compare_source_mask ||
      committed_retouch_after_compare.value(QStringLiteral("inpaintSelectedVariationSeed")).toInt() !=
          compare_selected_seed) {
    return fail("retouch after view should leave the committed retouch payload unchanged");
  }
  const auto patch_bounds = fallback_inpaint_controller.activeInpaintPatchBounds();
  if (!patch_bounds.isValid() || patch_bounds.width() <= 0.0 || patch_bounds.height() <= 0.0 ||
      patch_bounds.x() < 0.0 || patch_bounds.y() < 0.0 ||
      patch_bounds.right() > 1.0 || patch_bounds.bottom() > 1.0) {
    return fail("inpaint patch diagnostics should expose normalized active patch bounds");
  }
  if (fallback_inpaint_controller.inpaintPatchDiagnosticsEnabled()) {
    return fail("inpaint patch diagnostics should default off");
  }
  const auto had_patch_diagnostics_env =
      qEnvironmentVariableIsSet("PHOTO_EDITOR_ENABLE_PATCH_DIAGNOSTICS");
  const auto patch_diagnostics_env = qgetenv("PHOTO_EDITOR_ENABLE_PATCH_DIAGNOSTICS");
  const auto had_dev_observability_env =
      qEnvironmentVariableIsSet("PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY");
  const auto dev_observability_env = qgetenv("PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY");
  qunsetenv("PHOTO_EDITOR_ENABLE_PATCH_DIAGNOSTICS");
  qunsetenv("PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY");
  if (fallback_inpaint_controller.inpaintPatchDiagnosticsAvailable()) {
    return fail("inpaint patch diagnostics should be hidden outside developer/debug sessions");
  }
  fallback_inpaint_controller.setInpaintPatchDiagnosticsEnabled(true);
  if (fallback_inpaint_controller.inpaintPatchDiagnosticsEnabled()) {
    return fail("inpaint patch diagnostics should not enable without developer/debug opt-in");
  }
  qputenv("PHOTO_EDITOR_ENABLE_PATCH_DIAGNOSTICS", "1");
  if (!fallback_inpaint_controller.inpaintPatchDiagnosticsAvailable()) {
    return fail("explicit developer patch diagnostics opt-in should expose patch diagnostics");
  }
  fallback_inpaint_controller.setInpaintPatchDiagnosticsEnabled(true);
  if (!fallback_inpaint_controller.inpaintPatchDiagnosticsEnabled()) {
    return fail("developer-gated inpaint patch diagnostics toggle should enable visible patch bounds");
  }
  fallback_inpaint_controller.setInpaintPatchDiagnosticsEnabled(false);
  if (had_patch_diagnostics_env) {
    qputenv("PHOTO_EDITOR_ENABLE_PATCH_DIAGNOSTICS", patch_diagnostics_env);
  } else {
    qunsetenv("PHOTO_EDITOR_ENABLE_PATCH_DIAGNOSTICS");
  }
  if (had_dev_observability_env) {
    qputenv("PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY", dev_observability_env);
  } else {
    qunsetenv("PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY");
  }
  const auto committed_inpaint_patch =
      fallback_inpaint_controller.maskStack().at(0).toMap().value(QStringLiteral("inpaintPatchData")).toByteArray();
  fallback_inpaint_controller.undo();
  if (!fallback_inpaint_controller.canRedo() ||
      fallback_inpaint_controller.maskStack().isEmpty() ||
      fallback_inpaint_controller.maskStack().at(0).toMap().value(QStringLiteral("committed")).toBool() ||
      !fallback_inpaint_controller.maskStack()
           .at(0)
           .toMap()
           .value(QStringLiteral("inpaintPatchData"))
           .toByteArray()
           .isEmpty()) {
    return fail("undo should restore a pending retouch mask before the baked inpaint operation");
  }
  fallback_inpaint_controller.redo();
  const auto redone_inpaint_mask = fallback_inpaint_controller.maskStack().at(0).toMap();
  if (!redone_inpaint_mask.value(QStringLiteral("committed")).toBool() ||
      redone_inpaint_mask.value(QStringLiteral("inpaintPatchData")).toByteArray() !=
          committed_inpaint_patch ||
      redone_inpaint_mask.value(QStringLiteral("inpaintSourceMaskData")).toByteArray().isEmpty() ||
      redone_inpaint_mask.value(QStringLiteral("inpaintCandidates")).toList().size() < 3 ||
      redone_inpaint_mask.value(QStringLiteral("inpaintPatchPayloadBytes")).toLongLong() <= 0 ||
      redone_inpaint_mask.value(QStringLiteral("inpaintCandidatePayloadBytes")).toLongLong() <= 0 ||
      redone_inpaint_mask.value(QStringLiteral("inpaintCandidatePayloadBytes")).toLongLong() > 33554432 ||
      redone_inpaint_mask.value(QStringLiteral("inpaintQualityMode")).toString() !=
          QStringLiteral("final")) {
    return fail("redo should restore the full baked inpaint operation payload");
  }
  const auto patch_before_regenerate =
      redone_inpaint_mask.value(QStringLiteral("inpaintPatchData")).toByteArray();
  const auto seed_before_regenerate = redone_inpaint_mask.value(QStringLiteral("inpaintSeed")).toInt();
  if (!fallback_inpaint_controller.activeInpaintRegenerationAvailable() ||
      !fallback_inpaint_controller.regenerateActiveInpaintPatch()) {
    return fail("committed inpaint retouch patches should be directly regeneratable from their stored source mask");
  }
  if (!wait_for_preview_idle(fallback_inpaint_controller, 6000)) {
    return fail("async inpaint regeneration should finish before inspecting the regenerated patch");
  }
  const auto regenerated_inpaint_mask = fallback_inpaint_controller.maskStack().at(0).toMap();
  if (!regenerated_inpaint_mask.value(QStringLiteral("committed")).toBool() ||
      regenerated_inpaint_mask.value(QStringLiteral("inpaintSeed")).toInt() !=
          seed_before_regenerate + 1 ||
      fallback_inpaint_controller.inpaintSeed() != seed_before_regenerate + 1 ||
      regenerated_inpaint_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() ||
      regenerated_inpaint_mask.value(QStringLiteral("inpaintPatchData")).toByteArray() ==
          patch_before_regenerate ||
      regenerated_inpaint_mask.value(QStringLiteral("inpaintCandidates")).toList().size() < 3 ||
      regenerated_inpaint_mask.value(QStringLiteral("inpaintPatchPayloadBytes")).toLongLong() <= 0 ||
      regenerated_inpaint_mask.value(QStringLiteral("inpaintCandidatePayloadBytes")).toLongLong() <= 0 ||
      regenerated_inpaint_mask.value(QStringLiteral("inpaintCandidatePayloadBytes")).toLongLong() > 33554432 ||
      regenerated_inpaint_mask.value(QStringLiteral("inpaintCandidateEvictedCount")).toInt() < 0 ||
      regenerated_inpaint_mask.value(QStringLiteral("inpaintCandidateIndex")).toInt() != 0 ||
      regenerated_inpaint_mask.value(QStringLiteral("inpaintSourceBoundsWidth")).toInt() <= 0 ||
      regenerated_inpaint_mask.value(QStringLiteral("inpaintSelectedVariationIndex")).toInt() != 0 ||
      regenerated_inpaint_mask.value(QStringLiteral("inpaintSelectedVariationSeed")).toInt() !=
          seed_before_regenerate + 1 ||
      !fallback_inpaint_controller.status().contains(QStringLiteral("Regenerated remove retouch patch"))) {
    return fail("Regenerate should rebuild the active committed inpaint patch instead of only advancing the global seed");
  }
  if (!fallback_inpaint_controller.activeInpaintVariationPickerVisible() ||
      fallback_inpaint_controller.activeInpaintCandidateCount() < 3 ||
      fallback_inpaint_controller.activeInpaintCandidates().size() < 3 ||
      fallback_inpaint_controller.activeInpaintCandidateIndex() != 0) {
    return fail("committed inpaint retouch masks with generated candidates should expose a variation picker state");
  }
  const auto fallback_candidates = fallback_inpaint_controller.activeInpaintCandidates();
  const auto fallback_candidate_one = fallback_candidates.at(1).toMap();
  const auto fallback_candidate_one_patch =
      fallback_candidate_one.value(QStringLiteral("patchData")).toByteArray();
  fallback_inpaint_controller.setActiveInpaintCandidateIndex(1);
  const auto selected_candidate_mask = fallback_inpaint_controller.maskStack().at(0).toMap();
  const auto variation_select_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          fallback_inpaint_controller);
  if (fallback_inpaint_controller.activeInpaintCandidateIndex() != 1 ||
      selected_candidate_mask.value(QStringLiteral("inpaintPatchData")).toByteArray() !=
          fallback_candidate_one_patch ||
      selected_candidate_mask.value(QStringLiteral("inpaintPatchColorSpace")).toString() !=
          fallback_candidate_one.value(QStringLiteral("patchColorSpace")).toString() ||
      selected_candidate_mask.value(QStringLiteral("inpaintSeed")).toInt() !=
          fallback_candidate_one.value(QStringLiteral("seed")).toInt() ||
      selected_candidate_mask.value(QStringLiteral("inpaintSelectedVariationIndex")).toInt() != 1 ||
      selected_candidate_mask.value(QStringLiteral("inpaintSelectedVariationSeed")).toInt() !=
          fallback_candidate_one.value(QStringLiteral("seed")).toInt()) {
    return fail("selecting an inpaint variation should swap the active patch, index, and seed");
  }
  if (variation_select_telemetry.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("inpaint_variation_select") ||
      variation_select_telemetry.value(QStringLiteral("success")).toBool() != true ||
      variation_select_telemetry.value(QStringLiteral("cancelled")).toBool() ||
      variation_select_telemetry.value(QStringLiteral("async_job_kind")).toString() !=
          QStringLiteral("inpaint_variation") ||
      variation_select_telemetry.value(QStringLiteral("async_job_stage")).toString() !=
          QStringLiteral("select_candidate") ||
      !variation_select_telemetry.value(QStringLiteral("async_job_id"))
           .toString()
           .startsWith(QStringLiteral("inpaint_variation-")) ||
      !variation_select_telemetry.value(QStringLiteral("async_job_state_signature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      variation_select_telemetry.value(QStringLiteral("previous_candidate_index")).toInt() != 0 ||
      variation_select_telemetry.value(QStringLiteral("candidate_index")).toInt() != 1 ||
      variation_select_telemetry.value(QStringLiteral("candidate_count")).toInt() <
          fallback_inpaint_controller.activeInpaintCandidateCount() ||
      variation_select_telemetry.value(QStringLiteral("inpaint_patch_payload_bytes")).toLongLong() <= 0 ||
      variation_select_telemetry.value(QStringLiteral("source_mask_width")).toInt() <= 0 ||
      variation_select_telemetry.value(QStringLiteral("source_mask_pixels")).toInt() <= 0 ||
      variation_select_telemetry.value(QStringLiteral("memory_policy_signature")).toString() !=
          fallback_inpaint_controller.maskMemoryPolicySignature()) {
    return fail("inpaint variation selection should expose candidate job identity and payload telemetry");
  }
  if (!wait_for_preview_idle(fallback_inpaint_controller, 6000)) {
    return fail("inpaint variation selection preview should settle before regenerating the selected variation");
  }
  const auto variation_patch_before_regenerate =
      selected_candidate_mask.value(QStringLiteral("inpaintPatchData")).toByteArray();
  const auto variation_seed_before_regenerate =
      selected_candidate_mask.value(QStringLiteral("inpaintSeed")).toInt();
  if (!fallback_inpaint_controller.regenerateActiveInpaintPatch()) {
    return fail("selected inpaint variations should be regeneratable from their stored source mask");
  }
  if (!wait_for_preview_idle(fallback_inpaint_controller, 6000)) {
    return fail("async selected-variation regeneration should finish before inspecting the regenerated patch");
  }
  const auto regenerated_variation_mask = fallback_inpaint_controller.maskStack().at(0).toMap();
  const auto variation_regenerate_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          fallback_inpaint_controller);
  if (fallback_inpaint_controller.activeInpaintCandidateIndex() != 1 ||
      regenerated_variation_mask.value(QStringLiteral("inpaintCandidateIndex")).toInt() != 1 ||
      regenerated_variation_mask.value(QStringLiteral("inpaintSelectedVariationIndex")).toInt() != 1 ||
      regenerated_variation_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() ||
      regenerated_variation_mask.value(QStringLiteral("inpaintPatchData")).toByteArray() ==
          variation_patch_before_regenerate ||
      regenerated_variation_mask.value(QStringLiteral("inpaintSeed")).toInt() <=
          variation_seed_before_regenerate ||
      fallback_inpaint_controller.inpaintSeed() !=
          regenerated_variation_mask.value(QStringLiteral("inpaintSeed")).toInt() ||
      !fallback_inpaint_controller.status().contains(QStringLiteral("variation 2"))) {
    return fail("regenerating a selected inpaint variation should keep that variation active with a fresh patch");
  }
  if (variation_regenerate_telemetry.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("inpaint_regenerate") ||
      variation_regenerate_telemetry.value(QStringLiteral("success")).toBool() != true ||
      variation_regenerate_telemetry.value(QStringLiteral("async_job_kind")).toString() !=
          QStringLiteral("inpaint_regenerate") ||
      variation_regenerate_telemetry.value(QStringLiteral("target_candidate_index")).toInt() != 1 ||
      variation_regenerate_telemetry.value(QStringLiteral("selected_candidate_index")).toInt() != 1 ||
      variation_regenerate_telemetry.value(QStringLiteral("variation_specific_regenerate")).toBool() !=
          true ||
      variation_regenerate_telemetry.value(QStringLiteral("seed")).toInt() !=
          regenerated_variation_mask.value(QStringLiteral("inpaintSeed")).toInt() ||
      variation_regenerate_telemetry.value(QStringLiteral("inpaint_worker_elapsed_ms")).toLongLong() <= 0) {
    return fail("selected-variation regeneration should report the preserved candidate index and worker telemetry");
  }
  fallback_inpaint_controller.setActiveInpaintCandidateIndex(999);
  if (fallback_inpaint_controller.activeInpaintCandidateIndex() !=
          fallback_inpaint_controller.activeInpaintCandidateCount() - 1 ||
      fallback_inpaint_controller.maskStack()
              .at(0)
              .toMap()
              .value(QStringLiteral("inpaintSelectedVariationIndex"))
              .toInt() != fallback_inpaint_controller.activeInpaintCandidateCount() - 1) {
    return fail("inpaint variation selection should clamp to available generated candidates");
  }
  if (!fallback_inpaint_controller.reopenActiveRetouchSelection()) {
    return fail("applied retouch patches should reopen their stored selection for refinement");
  }
  const auto reopened_retouch_mask = fallback_inpaint_controller.maskStack().at(0).toMap();
  if (fallback_inpaint_controller.activeMaskCommitted() ||
      !reopened_retouch_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() ||
      reopened_retouch_mask.value(QStringLiteral("segmentationData")).toByteArray().isEmpty() ||
      fallback_inpaint_controller.inpaintSeed() <= 0 ||
      !fallback_inpaint_controller.status().contains(QStringLiteral("selection reopened"))) {
    return fail("reopening a retouch patch should clear baked patch data and restore an editable selection");
  }
  if (!fallback_inpaint_controller.resetActiveRetouchSelection()) {
    return fail("reopened retouch selections should support reset without leaving the retouch tool");
  }
  const auto reset_retouch_mask = fallback_inpaint_controller.maskStack().at(0).toMap();
  if (fallback_inpaint_controller.activeMaskRetouchMode() != QStringLiteral("remove") ||
      fallback_inpaint_controller.activeMaskCommitted() ||
      !reset_retouch_mask.value(QStringLiteral("segmentationData")).toByteArray().isEmpty() ||
      !reset_retouch_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() ||
      !reset_retouch_mask.value(QStringLiteral("brushPoints")).toList().isEmpty()) {
    return fail("reset retouch selection should clear matte and patch payload while preserving the active mode");
  }

  photo_editor::desktop::EditorPreviewController delete_patch_controller;
  delete_patch_controller.loadAsset(
      QStringLiteral("asset-delete-inpaint"),
      inpaint_texture_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  delete_patch_controller.startRemoveBrush();
  delete_patch_controller.beginActiveBrushStroke();
  delete_patch_controller.addBrushPoint(0.48, 0.5);
  (void)finish_brush_stroke(delete_patch_controller);
  delete_patch_controller.applyActiveRetouch();
  if (!wait_for_preview_idle(delete_patch_controller, 6000)) {
    return fail("delete patch setup should wait for async remove inpaint apply");
  }
  if (delete_patch_controller.maskCount() != 1 || !delete_patch_controller.activeMaskCommitted()) {
    return fail("delete patch coverage should start from an applied retouch patch");
  }
  if (!delete_patch_controller.deleteActiveRetouchPatch() ||
      delete_patch_controller.maskCount() != 0 ||
      delete_patch_controller.hasActiveMask() ||
      !delete_patch_controller.status().contains(QStringLiteral("patch deleted"))) {
    return fail("Delete Patch should remove the applied retouch edit from the recipe");
  }

  const auto committed_count = remove_controller.maskCount();
  remove_controller.objectRemoveAt(0.85, 0.5, true);
  if (!wait_for_smart_commit(remove_controller, committed_count + 1) ||
      remove_controller.maskCount() != committed_count + 1 ||
      remove_controller.activeMaskCommitted()) {
    return fail("starting another object remove should stage a fresh pending retouch");
  }
  remove_controller.cancelActiveRetouch();
  if (remove_controller.maskCount() != committed_count) {
    return fail("cancel should drop pending retouch masks without removing committed edits");
  }

  QImage dust_source(96, 96, QImage::Format_ARGB32);
  dust_source.fill(QColor::fromRgb(180, 184, 188));
  const auto paint_dust = [&](int center_x, int center_y) {
    for (int y = center_y - 1; y <= center_y + 1; ++y) {
      for (int x = center_x - 1; x <= center_x + 1; ++x) {
        dust_source.setPixelColor(x, y, QColor::fromRgb(82, 84, 86));
      }
    }
  };
  paint_dust(30, 32);
  paint_dust(70, 62);
  const auto dust_path = runtime_root.filePath(QStringLiteral("dust-source.png"));
  if (!dust_source.save(dust_path)) {
    return fail("dust removal test image should be writable");
  }
  photo_editor::desktop::EditorPreviewController dust_controller;
  const auto dust_camera = QStringLiteral("DustCam 1");
  const auto dust_lens = QStringLiteral("Dust 50mm");
  dust_controller.loadAsset(
      QStringLiteral("dust-removal"),
      dust_path,
      QString(),
      dust_camera,
      QString(),
      dust_lens,
      QString());
  dust_controller.removeDustSpots();
  if (dust_controller.maskCount() != 1 ||
      dust_controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      dust_controller.activeMaskRetouchMode() != QStringLiteral("dust") ||
      dust_controller.activeMaskCommitted() ||
      !almost_equal(dust_controller.activeMaskRemoveStrength(), 74.0)) {
    return fail("dust removal should create a staged retouch mask for review");
  }
  if (!dust_controller.status().contains(QStringLiteral("staged")) ||
      !dust_controller.activeMaskPreviewSource().isEmpty() ||
      dust_controller.activeMaskOutlinePaths().isEmpty()) {
    return fail("dust removal should communicate review state and expose a source-free clean outline before commit");
  }
  const auto dust_mask = dust_controller.maskStack().at(0).toMap();
  const auto dust_data = dust_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto dust_width = dust_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto dust_height = dust_mask.value(QStringLiteral("segmentationHeight")).toInt();
  const auto first_dust_value = mask_value_at(dust_data, dust_width, dust_height, 30.0 / 95.0, 32.0 / 95.0);
  const auto second_dust_value = mask_value_at(dust_data, dust_width, dust_height, 70.0 / 95.0, 62.0 / 95.0);
  const auto background_dust_value = mask_value_at(dust_data, dust_width, dust_height, 0.10, 0.10);
  if (first_dust_value < 128 || second_dust_value < 128 || background_dust_value > 24) {
    auto max_dust_value = 0;
    auto max_dust_x = 0;
    auto max_dust_y = 0;
    for (int y = 0; y < dust_height; ++y) {
      for (int x = 0; x < dust_width; ++x) {
        const auto value = static_cast<int>(static_cast<unsigned char>(dust_data.at(y * dust_width + x)));
        if (value > max_dust_value) {
          max_dust_value = value;
          max_dust_x = x;
          max_dust_y = y;
        }
      }
    }
    std::cerr << "dust mask values first=" << static_cast<int>(first_dust_value)
              << " second=" << static_cast<int>(second_dust_value)
              << " background=" << static_cast<int>(background_dust_value)
              << " max=" << max_dust_value << " at " << max_dust_x << "," << max_dust_y << '\n';
    return fail("dust removal should target small dark spots without filling smooth background");
  }
  dust_controller.saveActiveDustMap();
  if (!dust_controller.hasSensorDustMap()) {
    return fail("saving dust removal should create a reusable sensor dust map for the camera and lens");
  }
  dust_controller.applyActiveRetouch();
  if (!dust_controller.activeMaskCommitted()) {
    return fail("dust removal should commit only after the review apply action");
  }
  photo_editor::desktop::EditorPreviewController dust_cancel_controller;
  dust_cancel_controller.loadAsset(
      QStringLiteral("dust-removal-cancel"),
      dust_path,
      QString(),
      dust_camera,
      QString(),
      dust_lens,
      QString());
  dust_cancel_controller.removeDustSpots();
  dust_cancel_controller.cancelActiveRetouch();
  if (dust_cancel_controller.maskCount() != 0 || dust_cancel_controller.hasActiveMask()) {
    return fail("dust removal review should discard the staged retouch before commit");
  }

  QImage clean_sensor_source(96, 96, QImage::Format_ARGB32);
  clean_sensor_source.fill(QColor::fromRgb(180, 184, 188));
  const auto clean_sensor_path = runtime_root.filePath(QStringLiteral("sensor-dust-clean-source.png"));
  if (!clean_sensor_source.save(clean_sensor_path)) {
    return fail("sensor dust map target image should be writable");
  }
  photo_editor::desktop::EditorPreviewController sensor_map_controller;
  sensor_map_controller.loadAsset(
      QStringLiteral("sensor-dust-map"),
      clean_sensor_path,
      QString(),
      dust_camera,
      QString(),
      dust_lens,
      QString());
  if (!sensor_map_controller.hasSensorDustMap()) {
    return fail("sensor dust maps should be discovered for matching camera and lens metadata");
  }
  sensor_map_controller.applySensorDustMap();
  if (sensor_map_controller.maskCount() != 1 ||
      sensor_map_controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      sensor_map_controller.activeMaskRetouchMode() != QStringLiteral("dust") ||
      sensor_map_controller.activeMaskCommitted()) {
    return fail("applying a sensor dust map should stage a dust retouch mask for review");
  }
  const auto sensor_map_mask = sensor_map_controller.maskStack().at(0).toMap();
  const auto sensor_map_data = sensor_map_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto sensor_map_width = sensor_map_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto sensor_map_height = sensor_map_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(sensor_map_data, sensor_map_width, sensor_map_height, 30.0 / 95.0, 32.0 / 95.0) < 128 ||
      mask_value_at(sensor_map_data, sensor_map_width, sensor_map_height, 70.0 / 95.0, 62.0 / 95.0) < 128 ||
      mask_value_at(sensor_map_data, sensor_map_width, sensor_map_height, 0.10, 0.10) > 24) {
    return fail("sensor dust maps should reapply the saved matte at the same sensor coordinates");
  }
  sensor_map_controller.applyActiveRetouch();
  if (!sensor_map_controller.activeMaskCommitted()) {
    return fail("sensor dust map review should commit after apply");
  }

  QImage red_eye_source(96, 96, QImage::Format_ARGB32);
  red_eye_source.fill(QColor::fromRgb(74, 64, 58));
  const auto paint_red_eye = [&](int center_x, int center_y) {
    for (int y = center_y - 1; y <= center_y + 1; ++y) {
      for (int x = center_x - 1; x <= center_x + 1; ++x) {
        red_eye_source.setPixelColor(x, y, QColor::fromRgb(235, 24, 20));
      }
    }
  };
  paint_red_eye(34, 42);
  paint_red_eye(62, 42);
  const auto red_eye_path = runtime_root.filePath(QStringLiteral("red-eye-source.png"));
  if (!red_eye_source.save(red_eye_path)) {
    return fail("red-eye removal test image should be writable");
  }
  photo_editor::desktop::EditorPreviewController red_eye_controller;
  red_eye_controller.loadAsset(
      QStringLiteral("red-eye-removal"),
      red_eye_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  red_eye_controller.removeRedEye();
  if (red_eye_controller.maskCount() != 1 ||
      red_eye_controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      red_eye_controller.activeMaskRetouchMode() != QStringLiteral("red-eye") ||
      red_eye_controller.activeMaskCommitted()) {
    return fail("red-eye removal should create a staged retouch mask for review");
  }
  if (!red_eye_controller.status().contains(QStringLiteral("staged")) ||
      !red_eye_controller.activeMaskPreviewSource().isEmpty() ||
      red_eye_controller.activeMaskOutlinePaths().isEmpty()) {
    return fail("red-eye removal should communicate review state and expose a source-free clean outline before commit");
  }
  const auto red_eye_mask = red_eye_controller.maskStack().at(0).toMap();
  const auto red_eye_data = red_eye_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto red_eye_width = red_eye_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto red_eye_height = red_eye_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(red_eye_data, red_eye_width, red_eye_height, 34.0 / 95.0, 42.0 / 95.0) < 128 ||
      mask_value_at(red_eye_data, red_eye_width, red_eye_height, 62.0 / 95.0, 42.0 / 95.0) < 128 ||
      mask_value_at(red_eye_data, red_eye_width, red_eye_height, 0.10, 0.10) > 24) {
    return fail("red-eye removal should target red pupil highlights without masking neutral skin");
  }
  red_eye_controller.applyActiveRetouch();
  if (!red_eye_controller.activeMaskCommitted()) {
    return fail("red-eye removal should commit only after the review apply action");
  }
  photo_editor::desktop::EditorPreviewController red_eye_cancel_controller;
  red_eye_cancel_controller.loadAsset(
      QStringLiteral("red-eye-removal-cancel"),
      red_eye_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  red_eye_cancel_controller.removeRedEye();
  red_eye_cancel_controller.cancelActiveRetouch();
  if (red_eye_cancel_controller.maskCount() != 0 || red_eye_cancel_controller.hasActiveMask()) {
    return fail("red-eye removal review should discard the staged retouch before commit");
  }

  QImage skin_source(96, 96, QImage::Format_ARGB32);
  skin_source.fill(QColor::fromRgb(64, 72, 82));
  for (int y = 20; y <= 72; ++y) {
    for (int x = 18; x <= 78; ++x) {
      skin_source.setPixelColor(x, y, QColor::fromRgb(210, 152, 124));
    }
  }
  skin_source.setPixelColor(48, 46, QColor::fromRgb(118, 72, 62));
  const auto skin_path = runtime_root.filePath(QStringLiteral("skin-cleanup-source.png"));
  if (!skin_source.save(skin_path)) {
    return fail("skin cleanup test image should be writable");
  }
  photo_editor::desktop::EditorPreviewController skin_controller;
  skin_controller.loadAsset(
      QStringLiteral("skin-cleanup"),
      skin_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  skin_controller.cleanupSkin();
  if (skin_controller.maskCount() != 1 ||
      skin_controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      skin_controller.activeMaskRetouchMode() != QStringLiteral("skin-cleanup") ||
      !skin_controller.activeMaskCommitted() ||
      !almost_equal(skin_controller.activeMaskRemoveStrength(), 46.0)) {
    return fail("skin cleanup should create a committed skin retouch mask");
  }
  const auto skin_mask = skin_controller.maskStack().at(0).toMap();
  const auto skin_data = skin_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto skin_width = skin_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto skin_height = skin_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(skin_data, skin_width, skin_height, 48.0 / 95.0, 46.0 / 95.0) < 128 ||
      mask_value_at(skin_data, skin_width, skin_height, 0.08, 0.08) > 24) {
    return fail("skin cleanup should mask skin-toned regions without selecting neutral background");
  }

  photo_editor::desktop::EditorPreviewController frequency_controller;
  frequency_controller.loadAsset(
      QStringLiteral("frequency-smoothing"),
      skin_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  frequency_controller.smoothSkinFrequency();
  if (frequency_controller.maskCount() != 1 ||
      frequency_controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      frequency_controller.activeMaskRetouchMode() != QStringLiteral("frequency-smoothing") ||
      !frequency_controller.activeMaskCommitted() ||
      !almost_equal(frequency_controller.activeMaskRemoveStrength(), 58.0)) {
    return fail("frequency smoothing should create a committed skin smoothing retouch mask");
  }

  photo_editor::desktop::EditorPreviewController retouch_list_controller;
  retouch_list_controller.loadAsset(
      QStringLiteral("retouch-operation-list"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  retouch_list_controller.startRemoveBrush();
  retouch_list_controller.startHealBrush();
  if (retouch_list_controller.retouchCount() != 2) {
    return fail("retouch operation list should expose staged retouch operations");
  }
  const auto initial_retouch_list = retouch_list_controller.retouchStack();
  const auto staged_remove_row = initial_retouch_list.at(0).toMap();
  const auto staged_heal_row = initial_retouch_list.at(1).toMap();
  if (staged_remove_row.value(QStringLiteral("retouchOrder")).toInt() != 1 ||
      staged_remove_row.value(QStringLiteral("retouchOrderLabel")).toString() != QStringLiteral("#1") ||
      staged_heal_row.value(QStringLiteral("retouchOrder")).toInt() != 2 ||
      staged_heal_row.value(QStringLiteral("retouchOrderLabel")).toString() != QStringLiteral("#2") ||
      staged_heal_row.value(QStringLiteral("retouchModeLabel")).toString() != QStringLiteral("Heal") ||
      staged_heal_row.value(QStringLiteral("retouchStatus")).toString() != QStringLiteral("staged") ||
      staged_heal_row.value(QStringLiteral("retouchPointCount")).toInt() != 0 ||
      !staged_heal_row.value(QStringLiteral("retouchAuditLabel")).toString().startsWith(QStringLiteral("#2 · Heal")) ||
      !staged_heal_row.value(QStringLiteral("retouchAuditLabel")).toString().contains(QStringLiteral("no strokes"))) {
    return fail("retouch operation list should expose deterministic order labels, audit labels, status, and stroke counts");
  }
  retouch_list_controller.setActiveMaskName(QStringLiteral("Blemish pass"));
  if (retouch_list_controller.activeMaskName() != QStringLiteral("Blemish pass")) {
    return fail("retouch operation list should support renaming the active operation");
  }
  retouch_list_controller.setActiveMaskEnabled(false);
  if (retouch_list_controller.maskStack().at(retouch_list_controller.activeMaskIndex()).toMap()
          .value(QStringLiteral("enabled")).toBool()) {
    return fail("retouch operation list should support disabling an operation");
  }
  retouch_list_controller.moveActiveMask(-1);
  if (retouch_list_controller.activeMaskIndex() != 0 ||
      retouch_list_controller.maskStack().at(0).toMap().value(QStringLiteral("name")).toString() !=
          QStringLiteral("Blemish pass") ||
      retouch_list_controller.maskStack().at(0).toMap().value(QStringLiteral("retouchOrder")).toInt() != 1 ||
      retouch_list_controller.maskStack().at(1).toMap().value(QStringLiteral("retouchOrder")).toInt() != 2 ||
      retouch_list_controller.retouchStack().at(0).toMap().value(QStringLiteral("retouchAuditLabel")).toString()
          .startsWith(QStringLiteral("#1 · Heal")) == false) {
    return fail("retouch operation list should support reordering operations and compact retouch order");
  }
  retouch_list_controller.removeActiveMask();
  if (retouch_list_controller.retouchCount() != 1 ||
      retouch_list_controller.maskStack().at(0).toMap().value(QStringLiteral("retouchMode")).toString() !=
          QStringLiteral("remove") ||
      retouch_list_controller.maskStack().at(0).toMap().value(QStringLiteral("retouchOrder")).toInt() != 1) {
    return fail("retouch operation list should support deleting operations and compact remaining retouch order");
  }

  controller.setMaskPreviewMode(QStringLiteral("overlay"));
  controller.startSelectionBrush(false);
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskOperation() != QStringLiteral("add") ||
      controller.activeMaskToolRole() != QStringLiteral("selection") ||
      !controller.activeMaskCommitted()) {
    return fail("refine add should create an additive brush mask");
  }
  controller.beginActiveBrushStroke();
  if (!controller.brushStrokeActive()) {
    return fail("beginning a brush stroke should expose active stroke state for the live canvas overlay");
  }
  controller.addBrushPoint(0.34, 0.48);
  controller.addBrushPoint(0.88, 0.82);
  if (controller.activeMaskLiveBrushPoints().size() <= 2) {
    return fail("active brush strokes should expose interpolated live brush samples before commit");
  }
  (void)finish_brush_stroke(controller);
  if (controller.brushStrokeActive()) {
    return fail("ending a brush stroke should clear the active stroke state");
  }
  if (!controller.activeMaskLiveBrushPoints().isEmpty()) {
    return fail("ending a brush stroke should clear the transient live brush sample buffer");
  }
  if (controller.activeMaskBrushPointCount() <= 2) {
    return fail("selection brush strokes should interpolate samples along long drags");
  }
  if (!controller.activeMaskPreviewSource().isEmpty() ||
      controller.activeMaskOutlinePaths().isEmpty()) {
    return fail("selection brush should keep normal overlay source-free while exposing a clean outline");
  }
  const auto brush_outline_bounds = outline_bounds(controller.activeMaskOutlinePaths());
  const auto expected_brush_bounds = QRectF(QPointF(0.31, 0.45), QPointF(0.91, 0.85));
  if (!outline_bounds_match(brush_outline_bounds, expected_brush_bounds, 0.06)) {
    const auto detail =
        outline_bounds_detail(QStringLiteral("brush stroke"), brush_outline_bounds, expected_brush_bounds)
            .toUtf8();
    return fail(detail.constData());
  }
  photo_editor::desktop::EditorPreviewController outline_quality_controller;
  outline_quality_controller.loadAsset(
      QStringLiteral("outline-quality-ladder-proof"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  outline_quality_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  outline_quality_controller.startSelectionBrush(false);
  outline_quality_controller.beginActiveBrushStroke();
  outline_quality_controller.addBrushPoint(0.24, 0.34);
  outline_quality_controller.addBrushPoint(0.58, 0.64);
  if (outline_quality_controller.activeMaskOutlineQualityTier() != QStringLiteral("live-vector")) {
    return fail("outline quality ladder should report live-vector while a brush stroke is actively drawing");
  }
  outline_quality_controller.endActiveBrushStroke();
  if (outline_quality_controller.activeMaskOutlineQualityTier() != QStringLiteral("preview-alpha")) {
    return fail("outline quality ladder should keep a preview-alpha tier visible while brush raster commit is pending");
  }
  if (!wait_for_brush_commit(outline_quality_controller) ||
      outline_quality_controller.activeMaskOutlineQualityTier() != QStringLiteral("settled-alpha")) {
    return fail("outline quality ladder should promote to settled-alpha after the brush raster commit publishes");
  }
  outline_quality_controller.startPolygonSelection();
  outline_quality_controller.appendActivePolygonPoint(0.20, 0.20);
  outline_quality_controller.appendActivePolygonPoint(0.70, 0.24);
  outline_quality_controller.appendActivePolygonPoint(0.52, 0.76);
  if (outline_quality_controller.activeMaskOutlineQualityTier() != QStringLiteral("vector-path")) {
    return fail("outline quality ladder should report vector-path for editable polygon/lasso geometry");
  }
  outline_quality_controller.setMaskPreviewMode(QStringLiteral("none"));
  if (outline_quality_controller.activeMaskOutlineQualityTier() != QStringLiteral("hidden")) {
    return fail("outline quality ladder should report hidden when mask preview is disabled");
  }
  photo_editor::desktop::EditorPreviewController stale_outline_controller;
  stale_outline_controller.loadAsset(
      QStringLiteral("stale-outline-proof"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  stale_outline_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  stale_outline_controller.startSelectionBrush(false);
  stale_outline_controller.beginActiveBrushStroke();
  stale_outline_controller.addBrushPoint(0.30, 0.42);
  stale_outline_controller.addBrushPoint(0.70, 0.62);
  (void)finish_brush_stroke(stale_outline_controller);
  const auto stroke_replayed_outline = stale_outline_controller.activeMaskOutlinePaths();
  auto stale_outline_mask =
      stale_outline_controller.maskStack().at(stale_outline_controller.activeMaskIndex()).toMap();
  const auto stale_outline_width =
      stale_outline_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto stale_outline_height =
      stale_outline_mask.value(QStringLiteral("segmentationHeight")).toInt();
  stale_outline_mask.insert(
      QStringLiteral("segmentationData"),
      QByteArray(stale_outline_width * stale_outline_height, '\0'));
  if (stroke_replayed_outline.isEmpty() ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          stale_outline_controller,
          stale_outline_mask) ||
      stale_outline_controller.activeMaskOutlinePaths() != stroke_replayed_outline) {
    return fail("brush outlines should replay committed stroke geometry instead of trusting stale stored segmentation data");
  }
  photo_editor::desktop::EditorPreviewController empty_alpha_outline_controller;
  empty_alpha_outline_controller.loadAsset(
      QStringLiteral("empty-alpha-outline-proof"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  empty_alpha_outline_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  empty_alpha_outline_controller.startSelectionBrush(false);
  auto empty_alpha_mask =
      empty_alpha_outline_controller.maskStack()
          .at(empty_alpha_outline_controller.activeMaskIndex())
          .toMap();
  empty_alpha_mask.insert(QStringLiteral("kind"), QStringLiteral("object"));
  empty_alpha_mask.insert(QStringLiteral("segmentationWidth"), 32);
  empty_alpha_mask.insert(QStringLiteral("segmentationHeight"), 32);
  empty_alpha_mask.insert(QStringLiteral("segmentationData"), QByteArray(32 * 32, '\0'));
  empty_alpha_mask.insert(QStringLiteral("brushPoints"), QVariantList{});
  empty_alpha_mask.insert(QStringLiteral("brushStrokes"), QVariantList{});
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          empty_alpha_outline_controller,
          empty_alpha_mask) ||
      !empty_alpha_outline_controller.activeMaskOutlinePaths().isEmpty()) {
    return fail("empty raster alpha should not fall back to a fake convex hull or bounding-box outline");
  }
  photo_editor::desktop::EditorPreviewController composite_outline_controller;
  composite_outline_controller.loadAsset(
      QStringLiteral("composite-outline-proof"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  composite_outline_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  composite_outline_controller.startSelectionBrush(false);
  composite_outline_controller.beginActiveBrushStroke();
  composite_outline_controller.addBrushPoint(0.30, 0.42);
  composite_outline_controller.addBrushPoint(0.44, 0.50);
  (void)finish_brush_stroke(composite_outline_controller);
  composite_outline_controller.startSelectionBrush(true);
  composite_outline_controller.beginActiveBrushStroke();
  composite_outline_controller.addBrushPoint(0.38, 0.46);
  composite_outline_controller.addBrushPoint(0.50, 0.52);
  (void)finish_brush_stroke(composite_outline_controller);
  const auto composite_fast_keys_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_fast_key_count(
          composite_outline_controller);
  const auto composite_full_keys_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_full_key_count(
          composite_outline_controller);
  const auto composite_outline_paths = composite_outline_controller.activeMaskOutlinePaths();
  if (composite_outline_paths.isEmpty() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_fast_key_count(
          composite_outline_controller) != composite_fast_keys_before ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_full_key_count(
          composite_outline_controller) != composite_full_keys_before + 1) {
    return fail("multi-source mask-set outlines should be traced from composed alpha instead of a single brush source");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_source(
          composite_outline_controller) != QStringLiteral("composite-alpha") ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_budget_ms(
          composite_outline_controller) != 33 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_elapsed_ms(
          composite_outline_controller) < 0 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_path_count(
          composite_outline_controller) != composite_outline_paths.size() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              active_mask_outline_trace_within_latency_budget(composite_outline_controller) !=
          (photo_editor::desktop::EditorPreviewControllerTestAccess::
               active_mask_outline_trace_elapsed_ms(composite_outline_controller) <=
           photo_editor::desktop::EditorPreviewControllerTestAccess::
               active_mask_outline_trace_budget_ms(composite_outline_controller))) {
    return fail("composite outline tracing should expose source, budget, elapsed time, and path-count telemetry");
  }
  const auto composite_outline_trace_fields =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_fields(
          composite_outline_controller);
  const auto composite_public_outline_trace = composite_outline_controller.activeMaskOutlineTrace();
  if (composite_outline_trace_fields.value(QStringLiteral("outline_trace_source")).toString() !=
          QStringLiteral("composite-alpha") ||
      composite_public_outline_trace.value(QStringLiteral("outline_trace_source")).toString() !=
          QStringLiteral("cache-hit") ||
      composite_outline_trace_fields.value(QStringLiteral("mask_source_kind")).toString() !=
          QStringLiteral("composite") ||
      composite_public_outline_trace.value(QStringLiteral("mask_source_kind")).toString() !=
          QStringLiteral("composite") ||
      composite_outline_trace_fields.value(QStringLiteral("mask_source_count")).toInt() < 2 ||
      composite_outline_trace_fields
              .value(QStringLiteral("mask_alpha_provider_contract"))
              .toString() != QStringLiteral("shared-mask-alpha-provider-v1") ||
      composite_public_outline_trace
              .value(QStringLiteral("mask_alpha_provider_contract"))
              .toString() != QStringLiteral("shared-mask-alpha-provider-v1") ||
      composite_outline_trace_fields.value(QStringLiteral("mask_alpha_provider")).toString() !=
          QStringLiteral("composite") ||
      composite_outline_trace_fields.value(QStringLiteral("outline_coordinate_space")).toString() !=
          QStringLiteral("image-normalized") ||
      composite_outline_trace_fields.value(QStringLiteral("outline_coordinate_contract")).toString() !=
          QStringLiteral("image-normalized|crop-aware|points-clamped-0-1:v1") ||
      composite_outline_trace_fields.value(QStringLiteral("outline_alpha_source_contract")).toString() !=
          QStringLiteral("same-final-alpha-as-layer-mask:v1") ||
      composite_outline_trace_fields.value(QStringLiteral("outline_alpha_source_provider")).toString() !=
          QStringLiteral("composite") ||
      !composite_outline_trace_fields
           .value(QStringLiteral("mask_alpha_provider_signature"))
           .toString()
           .contains(QStringLiteral("provider:composite")) ||
      !composite_outline_trace_fields.value(QStringLiteral("mask_source_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !composite_outline_trace_fields.value(QStringLiteral("mask_alpha_trace_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !composite_outline_trace_fields.value(QStringLiteral("mask_alpha_trace_signature"))
           .toString()
           .contains(QStringLiteral("outline-composite")) ||
      !composite_outline_trace_fields.value(QStringLiteral("mask_alpha_trace_signature"))
           .toString()
           .contains(QStringLiteral("provider:composite")) ||
      composite_outline_trace_fields.value(QStringLiteral("outline_trace_path_count")).toInt() !=
          composite_outline_paths.size()) {
    return fail("composite outline tracing should carry exact mask alpha/source trace hashes and signatures");
  }
  photo_editor::desktop::EditorPreviewController outline_cache_controller;
  outline_cache_controller.loadAsset(
      QStringLiteral("outline-cache-proof"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  outline_cache_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  outline_cache_controller.startSelectionBrush(false);
  outline_cache_controller.addBrushPoint(0.32, 0.44);
  const auto outline_cache_hits_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_cache_hit_count(
          outline_cache_controller);
  const auto outline_cache_misses_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_cache_miss_count(
          outline_cache_controller);
  const auto outline_cache_fast_keys_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_fast_key_count(
          outline_cache_controller);
  const auto outline_cache_full_keys_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_full_key_count(
          outline_cache_controller);
  const auto outline_cache_first = outline_cache_controller.activeMaskOutlinePaths();
  const auto outline_cache_first_trace_fields =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_fields(
          outline_cache_controller);
  const auto outline_cache_second = outline_cache_controller.activeMaskOutlinePaths();
  const auto outline_cache_second_trace_fields =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_fields(
          outline_cache_controller);
  if (outline_cache_first.isEmpty() ||
      outline_cache_second != outline_cache_first ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_cache_miss_count(
          outline_cache_controller) != outline_cache_misses_before + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_cache_hit_count(
          outline_cache_controller) != outline_cache_hits_before + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_fast_key_count(
          outline_cache_controller) != outline_cache_fast_keys_before ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_full_key_count(
          outline_cache_controller) != outline_cache_full_keys_before + 1 ||
      outline_cache_first_trace_fields.value(QStringLiteral("outline_trace_source")).toString() !=
          QStringLiteral("composite-alpha") ||
      outline_cache_first_trace_fields.value(QStringLiteral("mask_alpha_provider")).toString() !=
          QStringLiteral("brush-stroke-replay")) {
    return fail("active brush outline paths should use rebuilt composite alpha and cache repeated unchanged overlay paints");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_source(
          outline_cache_controller) != QStringLiteral("cache-hit") ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_budget_ms(
          outline_cache_controller) != 33 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_elapsed_ms(
          outline_cache_controller) < 0 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              active_mask_outline_trace_path_count(outline_cache_controller) !=
          outline_cache_first.size() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              active_mask_outline_trace_within_latency_budget(outline_cache_controller) !=
          (photo_editor::desktop::EditorPreviewControllerTestAccess::
               active_mask_outline_trace_elapsed_ms(outline_cache_controller) <=
           photo_editor::desktop::EditorPreviewControllerTestAccess::
               active_mask_outline_trace_budget_ms(outline_cache_controller))) {
    return fail("active mask outline cache hits should expose source, budget, elapsed time, and path-count telemetry");
  }
  if (!outline_cache_first_trace_fields.value(QStringLiteral("mask_alpha_trace_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      outline_cache_second_trace_fields.value(QStringLiteral("outline_trace_source")).toString() !=
          QStringLiteral("cache-hit") ||
      outline_cache_second_trace_fields.value(QStringLiteral("mask_alpha_trace_hash")).toString() !=
          outline_cache_first_trace_fields.value(QStringLiteral("mask_alpha_trace_hash")).toString() ||
      outline_cache_second_trace_fields.value(QStringLiteral("mask_alpha_trace_signature")).toString() !=
          outline_cache_first_trace_fields.value(QStringLiteral("mask_alpha_trace_signature")).toString()) {
    return fail("active mask outline cache hits should preserve exact mask alpha trace hashes from the cached outline");
  }
  const auto outline_cache_misses_after_hit =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_cache_miss_count(
          outline_cache_controller);
  outline_cache_controller.addBrushPoint(0.54, 0.58);
  const auto outline_cache_after_edit = outline_cache_controller.activeMaskOutlinePaths();
  if (outline_cache_after_edit.isEmpty() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_cache_miss_count(
          outline_cache_controller) != outline_cache_misses_after_hit + 1) {
    return fail("active mask outline cache should miss after the active mask changes");
  }
  const auto outline_cache_misses_after_mask_edit =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_cache_miss_count(
          outline_cache_controller);
  const auto outline_cache_hits_after_mask_edit =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_cache_hit_count(
          outline_cache_controller);
  const auto outline_cache_full_keys_after_mask_edit =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_full_key_count(
          outline_cache_controller);
  outline_cache_controller.setExposure(0.42);
  const auto outline_cache_after_global_edit = outline_cache_controller.activeMaskOutlinePaths();
  const auto outline_cache_after_global_edit_trace_fields =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_fields(
          outline_cache_controller);
  if (outline_cache_after_global_edit != outline_cache_after_edit ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_cache_miss_count(
          outline_cache_controller) != outline_cache_misses_after_mask_edit + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_cache_hit_count(
          outline_cache_controller) != outline_cache_hits_after_mask_edit ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_full_key_count(
          outline_cache_controller) != outline_cache_full_keys_after_mask_edit + 1 ||
      outline_cache_after_global_edit_trace_fields
              .value(QStringLiteral("outline_trace_source"))
              .toString() != QStringLiteral("composite-alpha") ||
      outline_cache_after_global_edit_trace_fields.value(QStringLiteral("mask_alpha_provider")).toString() !=
          QStringLiteral("brush-stroke-replay")) {
    return fail("normal overlay brush outlines should remain stable across unrelated global edits and recompute from brush-stroke alpha when needed");
  }
  const auto skipped_preview_source_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_preview_source_skip_count(
          outline_cache_controller);
  if (!outline_cache_controller.activeMaskPreviewSource().isEmpty() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_preview_source_skip_count(
          outline_cache_controller) != skipped_preview_source_before + 1) {
    return fail("normal overlay mode should count skipped active matte preview source generation");
  }
  const auto skipped_preview_source_after_overlay =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_preview_source_skip_count(
          outline_cache_controller);
  outline_cache_controller.setMaskPreviewMode(QStringLiteral("black-on-white"));
  const auto matte_lru_hits_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_preview_lru_hit_count(
          outline_cache_controller);
  const auto matte_lru_misses_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_preview_lru_miss_count(
          outline_cache_controller);
  const auto black_on_white_preview = outline_cache_controller.activeMaskPreviewSource();
  if (black_on_white_preview.isEmpty() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_preview_source_skip_count(
          outline_cache_controller) != skipped_preview_source_after_overlay) {
    return fail("explicit matte inspection mode should generate a preview source without counting as skipped");
  }
  const auto repeated_black_on_white_preview = outline_cache_controller.activeMaskPreviewSource();
  if (repeated_black_on_white_preview != black_on_white_preview ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_preview_lru_miss_count(
          outline_cache_controller) != matte_lru_misses_before + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_preview_lru_hit_count(
          outline_cache_controller) != matte_lru_hits_before + 1) {
    return fail("active matte preview source should hit the cache when an inspection mode is requested twice");
  }
  outline_cache_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  if (!outline_cache_controller.activeMaskPreviewSource().isEmpty()) {
    return fail("normal overlay mode should still keep active matte preview source empty after an inspection cache fill");
  }
  outline_cache_controller.setMaskPreviewMode(QStringLiteral("black-on-white"));
  if (outline_cache_controller.activeMaskPreviewSource() != black_on_white_preview ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_preview_lru_hit_count(
          outline_cache_controller) != matte_lru_hits_before + 2) {
    return fail("active matte preview LRU should survive toggling back from source-free overlay mode");
  }
  const auto matte_lru_evictions_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_preview_lru_eviction_count(
          outline_cache_controller);
  const QStringList inspection_modes{
      QStringLiteral("white-on-black"),
      QStringLiteral("grayscale"),
      QStringLiteral("on-black"),
      QStringLiteral("on-white"),
      QStringLiteral("color-on-bw"),
  };
  for (const auto& inspection_mode : inspection_modes) {
    outline_cache_controller.setMaskPreviewMode(inspection_mode);
    if (outline_cache_controller.activeMaskPreviewSource().isEmpty()) {
      return fail("active matte preview LRU proof should generate all explicit inspection preview modes");
    }
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_preview_lru_cache_size(
          outline_cache_controller) > 4 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_preview_lru_eviction_count(
          outline_cache_controller) <= matte_lru_evictions_before) {
    return fail("active matte preview LRU should stay bounded and evict old inspection sources");
  }
  photo_editor::desktop::EditorPreviewController thumbnail_defer_controller;
  thumbnail_defer_controller.loadAsset(
      QStringLiteral("thumbnail-defer-proof"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  thumbnail_defer_controller.setMaskPreviewMode(QStringLiteral("overlay"));
  thumbnail_defer_controller.startSelectionBrush(false);
  const auto photo_layers_before_brush = thumbnail_defer_controller.photoLayers();
  if (photo_layers_before_brush.isEmpty()) {
    return fail("selection brush should create an adjustment layer before thumbnail defer proof");
  }
  const auto photo_layer_before_brush = photo_layers_before_brush.constFirst().toMap();
  const auto photo_layer_thumbnail_before_brush =
      photo_layer_before_brush.value(QStringLiteral("thumbnailSource")).toString();
  if (photo_layer_thumbnail_before_brush.isEmpty()) {
    return fail("selection brush adjustment layer should have a cached thumbnail before live brush proof");
  }
  const auto mask_sets_before_brush = thumbnail_defer_controller.maskSets();
  const auto mask_set_before_brush =
      mask_sets_before_brush.isEmpty() ? QVariantMap{} : mask_sets_before_brush.constFirst().toMap();
  if (mask_set_before_brush.value(QStringLiteral("thumbnailSource")).toString().isEmpty()) {
    return fail("selection brush mask set should have a cached thumbnail before live brush proof");
  }
  const auto photo_layer_thumbnail_refresh_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_thumbnail_refresh_count(thumbnail_defer_controller);
  const auto photo_layer_thumbnail_deferred_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_thumbnail_refresh_deferred_count(thumbnail_defer_controller);
  const auto thumbnail_queue_scheduled_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_scheduled_count(thumbnail_defer_controller);
  const auto thumbnail_queue_dispatched_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_dispatched_count(thumbnail_defer_controller);
  const auto thumbnail_queue_cancelled_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_cancelled_count(thumbnail_defer_controller);
  const auto thumbnail_queue_worker_started_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_worker_started_count(thumbnail_defer_controller);
  const auto thumbnail_queue_worker_completed_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_worker_completed_count(thumbnail_defer_controller);
  const auto thumbnail_queue_stale_result_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_stale_result_count(thumbnail_defer_controller);
  thumbnail_defer_controller.beginActiveBrushStroke();
  thumbnail_defer_controller.addBrushPoint(0.30, 0.45);
  thumbnail_defer_controller.addBrushPoint(0.70, 0.55);
  thumbnail_defer_controller.setActiveMaskDensity(0.82);
  if (!thumbnail_defer_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("exposure"),
          0.35)) {
    return fail("layer thumbnail defer proof should dirty the adjustment layer during the live brush stroke");
  }
  const auto photo_layers_during_brush = thumbnail_defer_controller.photoLayers();
  const auto photo_layers_during_brush_second = thumbnail_defer_controller.photoLayers();
  const auto photo_layer_during_brush =
      photo_layers_during_brush.isEmpty() ? QVariantMap{} : photo_layers_during_brush.constFirst().toMap();
  const auto thumbnail_queue_generation_during_brush =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_pending_generation(thumbnail_defer_controller);
  if (photo_layers_during_brush.isEmpty() ||
      photo_layers_during_brush_second != photo_layers_during_brush ||
      photo_layer_during_brush.value(QStringLiteral("thumbnailSource")).toString() !=
          photo_layer_thumbnail_before_brush ||
      photo_layer_during_brush.value(QStringLiteral("thumbnailStale")).toBool() != true ||
      photo_layer_during_brush.value(QStringLiteral("thumbnailProgressive")).toBool() != true ||
      photo_layer_during_brush.value(QStringLiteral("thumbnailProgressiveReason")).toString() !=
          QStringLiteral("thumbnail_refresh_deferred") ||
      !photo_layer_during_brush.value(QStringLiteral("thumbnailProgressiveSignature"))
           .toString()
           .contains(QStringLiteral("progressive:true")) ||
      photo_layer_during_brush.value(QStringLiteral("thumbnailStaleDetected")).toBool() != true ||
      photo_layer_during_brush.value(QStringLiteral("thumbnailStaleMismatch")).toBool() != true ||
      photo_layer_during_brush.value(QStringLiteral("thumbnailStaleDetectionReason")).toString() !=
          QStringLiteral("thumbnail_refresh_deferred") ||
      photo_layer_during_brush.value(QStringLiteral("thumbnailStalePreviousRecipeHash")).toString().isEmpty() ||
      photo_layer_during_brush.value(QStringLiteral("thumbnailStaleCurrentRecipeHash")).toString().isEmpty() ||
      photo_layer_during_brush.value(QStringLiteral("thumbnailStalePreviousRecipeHash")).toString() ==
          photo_layer_during_brush.value(QStringLiteral("thumbnailStaleCurrentRecipeHash")).toString() ||
      photo_layer_during_brush.value(QStringLiteral("thumbnailStalePreviousInvalidationKey")).toString() !=
          photo_layer_during_brush.value(QStringLiteral("thumbnailProgressivePreviousInvalidationKey")).toString() ||
      !photo_layer_during_brush.value(QStringLiteral("thumbnailStaleDetectionSignature"))
           .toString()
           .contains(QStringLiteral("mismatch:true")) ||
      !photo_layer_during_brush.value(QStringLiteral("thumbnailQueuePending")).toBool() ||
      photo_layer_during_brush.value(QStringLiteral("thumbnailQueueJobId")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-%1").arg(thumbnail_queue_generation_during_brush) ||
      photo_layer_during_brush.value(QStringLiteral("thumbnailQueueJobKind")).toString() !=
          QStringLiteral("thumbnail_refresh") ||
      photo_layer_during_brush.value(QStringLiteral("thumbnailQueuePriority")).toString() !=
          QStringLiteral("low") ||
      photo_layer_during_brush.value(QStringLiteral("thumbnailQueuePriorityRank")).toInt() != 90 ||
      photo_layer_during_brush.value(QStringLiteral("thumbnailQueueReason")).toString() !=
          QStringLiteral("photo_layer_thumbnail_refresh_deferred") ||
      photo_layer_during_brush.value(QStringLiteral("thumbnailQueueGeneration")).toInt() !=
          thumbnail_queue_generation_during_brush ||
      !photo_layer_during_brush.value(QStringLiteral("thumbnailQueueDeferBlockerSignature"))
           .toString()
           .contains(QStringLiteral("live_brush_stroke")) ||
      !photo_layer_during_brush.value(QStringLiteral("thumbnailQueueContract"))
           .toString()
           .contains(QStringLiteral("thumbnail-refresh-queue:v1|priority:low|pending:true")) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_count(thumbnail_defer_controller) !=
          photo_layer_thumbnail_refresh_count_before ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_deferred_count(thumbnail_defer_controller) !=
          photo_layer_thumbnail_deferred_count_before + 1 ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_pending(thumbnail_defer_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_timer_active(thumbnail_defer_controller) ||
      thumbnail_queue_generation_during_brush <= 0 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_refresh_queue_cancelled_count(thumbnail_defer_controller) !=
          thumbnail_queue_cancelled_count_before ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_refresh_queue_scheduled_count(thumbnail_defer_controller) !=
          thumbnail_queue_scheduled_count_before + 1) {
    std::cerr << "photo layer thumbnail defer diagnostics:"
              << " rows=" << photo_layers_during_brush.size()
              << " repeated_equal=" << (photo_layers_during_brush_second == photo_layers_during_brush)
              << " same_thumbnail="
              << (photo_layer_during_brush.value(QStringLiteral("thumbnailSource")).toString() ==
                  photo_layer_thumbnail_before_brush)
              << " stale=" << photo_layer_during_brush.value(QStringLiteral("thumbnailStale")).toBool()
              << " progressive="
              << photo_layer_during_brush.value(QStringLiteral("thumbnailProgressive")).toBool()
              << " reason="
              << photo_layer_during_brush.value(QStringLiteral("thumbnailProgressiveReason"))
                     .toString()
                     .toStdString()
              << " queue_pending_field="
              << photo_layer_during_brush.value(QStringLiteral("thumbnailQueuePending")).toBool()
              << " queue_job="
              << photo_layer_during_brush.value(QStringLiteral("thumbnailQueueJobId"))
                     .toString()
                     .toStdString()
              << " queue_reason="
              << photo_layer_during_brush.value(QStringLiteral("thumbnailQueueReason"))
                     .toString()
                     .toStdString()
              << " queue_generation_field="
              << photo_layer_during_brush.value(QStringLiteral("thumbnailQueueGeneration")).toInt()
              << " queue_generation=" << thumbnail_queue_generation_during_brush
              << " queue_blockers="
              << photo_layer_during_brush
                     .value(QStringLiteral("thumbnailQueueDeferBlockerSignature"))
                     .toString()
                     .toStdString()
              << " refresh_count="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     photo_layer_thumbnail_refresh_count(thumbnail_defer_controller)
              << " deferred_count="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     photo_layer_thumbnail_refresh_deferred_count(thumbnail_defer_controller)
              << " queue_pending="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     thumbnail_refresh_queue_pending(thumbnail_defer_controller)
              << " timer_active="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     thumbnail_refresh_queue_timer_active(thumbnail_defer_controller)
              << " queue_cancelled="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     thumbnail_refresh_queue_cancelled_count(thumbnail_defer_controller)
              << " queue_scheduled="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     thumbnail_refresh_queue_scheduled_count(thumbnail_defer_controller)
              << '\n';
    return fail("photo layer thumbnails should reuse cached layer thumbnails and queue a low-priority refresh during live brush strokes");
  }
  thumbnail_defer_controller.setActiveMaskDensity(0.79);
  const auto photo_layers_after_queue_replacement = thumbnail_defer_controller.photoLayers();
  const auto photo_layer_after_queue_replacement =
      photo_layers_after_queue_replacement.isEmpty()
          ? QVariantMap{}
          : photo_layers_after_queue_replacement.constFirst().toMap();
  if (photo_layers_after_queue_replacement.isEmpty() ||
      photo_layer_after_queue_replacement.value(QStringLiteral("thumbnailSource")).toString() !=
          photo_layer_thumbnail_before_brush ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_pending(thumbnail_defer_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_refresh_queue_pending_generation(thumbnail_defer_controller) <=
          thumbnail_queue_generation_during_brush ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_refresh_queue_cancelled_count(thumbnail_defer_controller) !=
          thumbnail_queue_cancelled_count_before + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_refresh_queue_cancelled_reason(thumbnail_defer_controller) !=
          QStringLiteral("superseded_pending_thumbnail_refresh")) {
    return fail("new thumbnail edits should cancel and replace stale queued thumbnail generations");
  }
  const auto thumbnail_refresh_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_thumbnail_refresh_count(
          thumbnail_defer_controller);
  const auto thumbnail_deferred_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_thumbnail_refresh_deferred_count(
          thumbnail_defer_controller);
  const auto group_rebuild_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_group_cache_rebuild_count(
          thumbnail_defer_controller);
  const auto mask_sets_during_brush = thumbnail_defer_controller.maskSets();
  const auto mask_sets_during_brush_second = thumbnail_defer_controller.maskSets();
  const auto mask_set_during_brush =
      mask_sets_during_brush.isEmpty() ? QVariantMap{} : mask_sets_during_brush.constFirst().toMap();
  const auto mask_queue_generation_during_brush =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_pending_generation(thumbnail_defer_controller);
  if (mask_sets_during_brush.isEmpty() ||
      mask_sets_during_brush_second != mask_sets_during_brush ||
      mask_set_during_brush.value(QStringLiteral("thumbnailStale")).toBool() != true ||
      mask_set_during_brush.value(QStringLiteral("thumbnailProgressive")).toBool() != true ||
      mask_set_during_brush.value(QStringLiteral("thumbnailProgressiveReason")).toString() !=
          QStringLiteral("thumbnail_refresh_deferred") ||
      mask_set_during_brush.value(QStringLiteral("thumbnailStaleDetected")).toBool() != true ||
      mask_set_during_brush.value(QStringLiteral("thumbnailStaleMismatch")).toBool() != true ||
      mask_set_during_brush.value(QStringLiteral("thumbnailStaleDetectionReason")).toString() !=
          QStringLiteral("thumbnail_refresh_deferred") ||
      mask_set_during_brush.value(QStringLiteral("thumbnailStalePreviousRecipeHash")).toString().isEmpty() ||
      mask_set_during_brush.value(QStringLiteral("thumbnailStaleCurrentRecipeHash")).toString().isEmpty() ||
      mask_set_during_brush.value(QStringLiteral("thumbnailStalePreviousRecipeHash")).toString() ==
          mask_set_during_brush.value(QStringLiteral("thumbnailStaleCurrentRecipeHash")).toString() ||
      mask_set_during_brush.value(QStringLiteral("thumbnailStalePreviousInvalidationKey")).toString() !=
          mask_set_during_brush.value(QStringLiteral("thumbnailProgressivePreviousInvalidationKey")).toString() ||
      !mask_set_during_brush.value(QStringLiteral("thumbnailStaleDetectionSignature"))
           .toString()
           .contains(QStringLiteral("mismatch:true")) ||
      !mask_set_during_brush.value(QStringLiteral("thumbnailQueuePending")).toBool() ||
      mask_set_during_brush.value(QStringLiteral("thumbnailQueueJobId")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-%1").arg(mask_queue_generation_during_brush) ||
      mask_set_during_brush.value(QStringLiteral("thumbnailQueueJobKind")).toString() !=
          QStringLiteral("thumbnail_refresh") ||
      mask_set_during_brush.value(QStringLiteral("thumbnailQueuePriority")).toString() !=
          QStringLiteral("low") ||
      mask_set_during_brush.value(QStringLiteral("thumbnailQueueReason")).toString() !=
          QStringLiteral("mask_thumbnail_refresh_deferred") ||
      !mask_set_during_brush.value(QStringLiteral("thumbnailQueueDeferBlockerSignature"))
           .toString()
           .contains(QStringLiteral("live_brush_stroke")) ||
      !mask_set_during_brush.value(QStringLiteral("thumbnailQueueContract"))
           .toString()
           .contains(QStringLiteral("blockers:")) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_group_cache_rebuild_count(
          thumbnail_defer_controller) != group_rebuild_count_before + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_thumbnail_refresh_count(
          thumbnail_defer_controller) != thumbnail_refresh_count_before ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_thumbnail_refresh_deferred_count(
          thumbnail_defer_controller) != thumbnail_deferred_count_before + 1) {
    return fail("mask set metadata should cache during live brush strokes while thumbnail generation is deferred");
  }
  (void)finish_brush_stroke(thumbnail_defer_controller);
  if (!wait_for_thumbnail_queue_dispatch(
          thumbnail_defer_controller,
          thumbnail_queue_dispatched_count_before + 1)) {
    return fail("queued thumbnail refresh should dispatch after deferred brush work settles");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_refresh_queue_worker_started_count(thumbnail_defer_controller) !=
          thumbnail_queue_worker_started_count_before + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_refresh_queue_worker_completed_count(thumbnail_defer_controller) !=
          thumbnail_queue_worker_completed_count_before + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_worker_running(thumbnail_defer_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
           thumbnail_refresh_queue_active_job_id(thumbnail_defer_controller)
           .isEmpty() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_refresh_queue_stale_result_count(thumbnail_defer_controller) !=
          thumbnail_queue_stale_result_count_before) {
    return fail("queued thumbnail refresh should complete through a current async thumbnail queue worker");
  }
  const auto photo_layers_after_brush = thumbnail_defer_controller.photoLayers();
  const auto photo_layer_after_brush =
      photo_layers_after_brush.isEmpty() ? QVariantMap{} : photo_layers_after_brush.constFirst().toMap();
  if (photo_layers_after_brush.isEmpty() ||
      photo_layer_after_brush.value(QStringLiteral("thumbnailSource")).toString().isEmpty() ||
      photo_layer_after_brush.value(QStringLiteral("thumbnailStale")).toBool() ||
      photo_layer_after_brush.value(QStringLiteral("thumbnailProgressive")).toBool() ||
      photo_layer_after_brush.value(QStringLiteral("thumbnailRenderQueuedWorkerJobId")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-%1").arg(mask_queue_generation_during_brush) ||
      photo_layer_after_brush.value(QStringLiteral("thumbnailRenderQueuedWorkerContract")).toString() !=
          QStringLiteral("thumbnail-render-consumed-queue-worker:v1") ||
      photo_layer_after_brush.value(QStringLiteral("thumbnailRenderQueuedWorkerLayerDirty")).toBool() != true ||
      photo_layer_after_brush.value(QStringLiteral("thumbnailRenderQueuedWorkerMaskDirty")).toBool() != true ||
      !photo_layer_after_brush.value(QStringLiteral("thumbnailRenderQueuedWorkerStateSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_count(thumbnail_defer_controller) !=
          photo_layer_thumbnail_refresh_count_before + 1 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_deferred_count(thumbnail_defer_controller) !=
          photo_layer_thumbnail_deferred_count_before + 2) {
    std::cerr << "photo layer post-queue worker diagnostics:"
              << " rows=" << photo_layers_after_brush.size()
              << " source_empty="
              << photo_layer_after_brush.value(QStringLiteral("thumbnailSource")).toString().isEmpty()
              << " stale=" << photo_layer_after_brush.value(QStringLiteral("thumbnailStale")).toBool()
              << " progressive="
              << photo_layer_after_brush.value(QStringLiteral("thumbnailProgressive")).toBool()
              << " queued_worker="
              << photo_layer_after_brush.value(QStringLiteral("thumbnailRenderQueuedWorkerJobId"))
                     .toString()
                     .toStdString()
              << " expected_worker="
              << QStringLiteral("thumbnail-refresh-queue-%1")
                     .arg(mask_queue_generation_during_brush)
                     .toStdString()
              << " worker_contract="
              << photo_layer_after_brush.value(QStringLiteral("thumbnailRenderQueuedWorkerContract"))
                     .toString()
                     .toStdString()
              << " layer_dirty="
              << photo_layer_after_brush
                     .value(QStringLiteral("thumbnailRenderQueuedWorkerLayerDirty"))
                     .toBool()
              << " mask_dirty="
              << photo_layer_after_brush
                     .value(QStringLiteral("thumbnailRenderQueuedWorkerMaskDirty"))
                     .toBool()
              << " state_sig="
              << photo_layer_after_brush
                     .value(QStringLiteral("thumbnailRenderQueuedWorkerStateSignature"))
                     .toString()
                     .toStdString()
              << " refresh_count="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     photo_layer_thumbnail_refresh_count(thumbnail_defer_controller)
              << " deferred_count="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::
                     photo_layer_thumbnail_refresh_deferred_count(thumbnail_defer_controller)
              << '\n';
    return fail("photo layer thumbnails should rebuild once after deferred brush work settles");
  }
  const auto mask_sets_after_brush = thumbnail_defer_controller.maskSets();
  const auto thumbnail_set_after_brush =
      mask_sets_after_brush.isEmpty() ? QVariantMap{} : mask_sets_after_brush.constFirst().toMap();
  const auto thumbnail_sources_after_brush =
      thumbnail_set_after_brush.value(QStringLiteral("sources")).toList();
  const auto thumbnail_source_after_brush =
      thumbnail_sources_after_brush.isEmpty() ? QVariantMap{} : thumbnail_sources_after_brush.constFirst().toMap();
  if (mask_sets_after_brush.isEmpty() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_thumbnail_cache_dirty(
          thumbnail_defer_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_thumbnail_refresh_count(
          thumbnail_defer_controller) != thumbnail_refresh_count_before + 1 ||
      thumbnail_set_after_brush.value(QStringLiteral("thumbnailSource")).toString().isEmpty() ||
      thumbnail_set_after_brush.value(QStringLiteral("async_job_kind")).toString() !=
          QStringLiteral("mask_thumbnail") ||
      thumbnail_set_after_brush.value(QStringLiteral("async_job_stage")).toString() !=
          QStringLiteral("thumbnail") ||
      !thumbnail_set_after_brush.value(QStringLiteral("async_job_id"))
           .toString()
           .startsWith(QStringLiteral("mask_thumbnail-")) ||
      !thumbnail_set_after_brush.value(QStringLiteral("thumbnailJobStateSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      thumbnail_set_after_brush.value(QStringLiteral("thumbnailRenderQueuedWorkerJobId")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-%1").arg(mask_queue_generation_during_brush) ||
      thumbnail_set_after_brush.value(QStringLiteral("thumbnailRenderQueuedWorkerContract")).toString() !=
          QStringLiteral("thumbnail-render-consumed-queue-worker:v1") ||
      thumbnail_set_after_brush.value(QStringLiteral("thumbnailRenderQueuedWorkerMaskEntryCount")).toInt() <= 0 ||
      thumbnail_set_after_brush.value(QStringLiteral("thumbnailCacheHit")).toBool() != true ||
      thumbnail_set_after_brush.value(QStringLiteral("thumbnailCacheSource")).toString() !=
          QStringLiteral("entry-cache") ||
      thumbnail_set_after_brush.value(QStringLiteral("thumbnailQueueWorkerRendered")).toBool() != true ||
      thumbnail_set_after_brush.value(QStringLiteral("thumbnailQueueWorkerRenderJobId")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-%1").arg(mask_queue_generation_during_brush) ||
      thumbnail_set_after_brush.value(QStringLiteral("thumbnailQueueWorkerRenderSource")).toString() !=
          QStringLiteral("mask-set") ||
      thumbnail_set_after_brush.value(QStringLiteral("thumbnailQueueWorkerRenderMaskEntryCount")).toInt() <= 0 ||
      thumbnail_set_after_brush.value(QStringLiteral("thumbnailQueueWorkerRenderContract")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-worker-entry:v1") ||
      thumbnail_source_after_brush.value(QStringLiteral("thumbnailRenderQueuedWorkerJobId")).toString() !=
          thumbnail_set_after_brush.value(QStringLiteral("thumbnailRenderQueuedWorkerJobId")).toString() ||
      thumbnail_source_after_brush.value(QStringLiteral("thumbnailCacheHit")).toBool() != true ||
      thumbnail_source_after_brush.value(QStringLiteral("thumbnailQueueWorkerRendered")).toBool() != true ||
      thumbnail_source_after_brush.value(QStringLiteral("thumbnailQueueWorkerRenderSource")).toString() !=
          QStringLiteral("mask-source") ||
      thumbnail_source_after_brush.value(QStringLiteral("thumbnailQueueWorkerRenderMaskEntryCount")).toInt() <= 0 ||
      thumbnail_set_after_brush.value(QStringLiteral("thumbnail_render_budget_ms")).toLongLong() != 100 ||
      thumbnail_set_after_brush.value(QStringLiteral("thumbnail_render_elapsed_ms")).toLongLong() < 0 ||
      !thumbnail_set_after_brush.contains(QStringLiteral("thumbnail_render_within_latency_budget")) ||
      thumbnail_set_after_brush.value(QStringLiteral("mask_source_kind")).toString() !=
          QStringLiteral("composite") ||
      !thumbnail_set_after_brush.value(QStringLiteral("mask_source_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !thumbnail_set_after_brush.value(QStringLiteral("mask_alpha_trace_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !thumbnail_set_after_brush.value(QStringLiteral("mask_alpha_trace_signature"))
           .toString()
           .contains(QStringLiteral("mask-thumbnail-composite")) ||
      thumbnail_set_after_brush.value(QStringLiteral("mask_alpha_provider_contract")).toString() !=
          QStringLiteral("shared-mask-alpha-provider-v1") ||
      thumbnail_set_after_brush.value(QStringLiteral("mask_alpha_provider")).toString() !=
          QStringLiteral("composite") ||
      !thumbnail_set_after_brush
           .value(QStringLiteral("mask_alpha_provider_signature"))
           .toString()
           .contains(QStringLiteral("provider:composite")) ||
      thumbnail_set_after_brush
              .value(QStringLiteral("mask_alpha_full_resolution_available"))
              .toBool() != true ||
      !thumbnail_source_after_brush.value(QStringLiteral("mask_source_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !thumbnail_source_after_brush.value(QStringLiteral("mask_alpha_trace_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      thumbnail_source_after_brush.value(QStringLiteral("mask_alpha_provider")).toString() !=
          QStringLiteral("brush-stroke-replay") ||
      thumbnail_source_after_brush.value(QStringLiteral("mask_alpha_provider_contract")).toString() !=
          QStringLiteral("shared-mask-alpha-provider-v1") ||
      !thumbnail_source_after_brush
           .value(QStringLiteral("mask_alpha_provider_signature"))
           .toString()
           .contains(QStringLiteral("provider:brush-stroke-replay")) ||
      thumbnail_source_after_brush
              .value(QStringLiteral("mask_alpha_full_resolution_available"))
              .toBool() != true ||
      thumbnail_source_after_brush.value(QStringLiteral("async_job_id")).toString() !=
          thumbnail_set_after_brush.value(QStringLiteral("async_job_id")).toString()) {
    return fail("mask thumbnails should refresh once after deferred brush rasterization settles with current thumbnail job metadata");
  }
  photo_editor::desktop::EditorPreviewController thumbnail_worker_fill_controller;
  thumbnail_worker_fill_controller.loadAsset(
      QStringLiteral("thumbnail-worker-fill-proof"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!thumbnail_worker_fill_controller.addFillLayer(
          QStringLiteral("#224466"),
          QStringLiteral("Worker Fill"))) {
    return fail("thumbnail queue worker proof should create a fill layer");
  }
  const auto worker_fill_layers_before = thumbnail_worker_fill_controller.photoLayers();
  const auto worker_fill_layer_before =
      worker_fill_layers_before.isEmpty()
          ? QVariantMap{}
          : worker_fill_layers_before.constFirst().toMap();
  if (worker_fill_layer_before.value(QStringLiteral("kind")).toString() != QStringLiteral("fill") ||
      worker_fill_layer_before.value(QStringLiteral("thumbnailSource")).toString().isEmpty()) {
    return fail("thumbnail queue worker proof should warm the initial fill-layer thumbnail");
  }
  const auto worker_fill_store_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::thumbnail_entry_cache_store_count(
          thumbnail_worker_fill_controller);
  const auto worker_fill_hit_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::thumbnail_entry_cache_hit_count(
          thumbnail_worker_fill_controller);
  const auto worker_fill_queue_dispatched_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_dispatched_count(thumbnail_worker_fill_controller);
  thumbnail_worker_fill_controller.startSelectionBrush(false);
  thumbnail_worker_fill_controller.beginActiveBrushStroke();
  thumbnail_worker_fill_controller.addBrushPoint(0.25, 0.25);
  thumbnail_worker_fill_controller.addBrushPoint(0.35, 0.32);
  if (!thumbnail_worker_fill_controller.setPhotoLayerOpacity(1, 0.64)) {
    return fail("thumbnail queue worker proof should dirty the fill layer while a brush stroke defers thumbnails");
  }
  const auto worker_fill_layers_deferred = thumbnail_worker_fill_controller.photoLayers();
  const auto worker_fill_queue_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_pending_generation(thumbnail_worker_fill_controller);
  if (worker_fill_layers_deferred.isEmpty() ||
      worker_fill_queue_generation <= 0 ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::thumbnail_refresh_queue_pending(
          thumbnail_worker_fill_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::thumbnail_refresh_deferred(
          thumbnail_worker_fill_controller)) {
    return fail("thumbnail queue worker proof should queue fill-layer thumbnail work behind the live brush stroke");
  }
  (void)finish_brush_stroke(thumbnail_worker_fill_controller);
  if (!wait_for_thumbnail_queue_dispatch(
          thumbnail_worker_fill_controller,
          worker_fill_queue_dispatched_before + 1)) {
    return fail("thumbnail queue worker proof should dispatch after the live brush stroke settles");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::thumbnail_entry_cache_store_count(
          thumbnail_worker_fill_controller) <= worker_fill_store_count_before) {
    return fail("thumbnail queue worker should store at least one rendered non-adjustment layer entry before the model getter rerenders");
  }
  {
    auto elapsed_ms = 0;
    while (elapsed_ms < 3000 &&
           photo_editor::desktop::EditorPreviewControllerTestAccess::thumbnail_refresh_deferred(
               thumbnail_worker_fill_controller)) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
      QThread::msleep(25);
      elapsed_ms += 25;
    }
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::thumbnail_refresh_deferred(
          thumbnail_worker_fill_controller)) {
    return fail("thumbnail queue worker proof should wait for higher-priority preview blockers before reading final layer thumbnails");
  }
  const auto worker_fill_layers_after = thumbnail_worker_fill_controller.photoLayers();
  QVariantMap worker_fill_layer_after;
  for (const auto& value : worker_fill_layers_after) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("fill")) {
      worker_fill_layer_after = layer;
      break;
    }
  }
  if (worker_fill_layer_after.isEmpty() ||
      worker_fill_layer_after.value(QStringLiteral("thumbnailSource")).toString().isEmpty() ||
      worker_fill_layer_after.value(QStringLiteral("thumbnailCacheHit")).toBool() != true ||
      worker_fill_layer_after.value(QStringLiteral("thumbnailCacheSource")).toString() !=
          QStringLiteral("entry-cache") ||
      worker_fill_layer_after.value(QStringLiteral("thumbnailQueueWorkerRendered")).toBool() != true ||
      worker_fill_layer_after.value(QStringLiteral("thumbnailQueueWorkerRenderJobId")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-%1").arg(worker_fill_queue_generation) ||
      worker_fill_layer_after.value(QStringLiteral("thumbnailQueueWorkerRenderSource")).toString() !=
          QStringLiteral("non-adjustment-photo-layer") ||
      worker_fill_layer_after.value(QStringLiteral("thumbnailQueueWorkerRenderContract")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-worker-entry:v1") ||
      worker_fill_layer_after.value(QStringLiteral("thumbnailQueueWorkerServiceContract")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-worker-service|cancellable|entry-cache:v1") ||
      worker_fill_layer_after.value(QStringLiteral("thumbnailQueueWorkerRenderEntryCount")).toInt() <= 0 ||
      worker_fill_layer_after.value(QStringLiteral("thumbnailRenderQueuedWorkerJobId")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-%1").arg(worker_fill_queue_generation) ||
      worker_fill_layer_after.value(QStringLiteral("thumbnailRenderQueuedWorkerEntryCount")).toInt() <= 0 ||
      worker_fill_layer_after.value(QStringLiteral("thumbnailRenderQueuedWorkerServiceContract")).toString() !=
          QStringLiteral("thumbnail-refresh-queue-worker-service|cancellable|entry-cache:v1") ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::thumbnail_entry_cache_hit_count(
          thumbnail_worker_fill_controller) <= worker_fill_hit_count_before) {
    std::cerr << "thumbnail queue worker fill diagnostics:"
              << " layers=" << worker_fill_layers_after.size()
              << " source_empty="
              << worker_fill_layer_after.value(QStringLiteral("thumbnailSource")).toString().isEmpty()
              << " cache_hit="
              << worker_fill_layer_after.value(QStringLiteral("thumbnailCacheHit")).toBool()
              << " cache_source="
              << worker_fill_layer_after.value(QStringLiteral("thumbnailCacheSource"))
                     .toString()
                     .toStdString()
              << " worker_rendered="
              << worker_fill_layer_after.value(QStringLiteral("thumbnailQueueWorkerRendered")).toBool()
              << " worker_job="
              << worker_fill_layer_after.value(QStringLiteral("thumbnailQueueWorkerRenderJobId"))
                     .toString()
                     .toStdString()
              << " expected_job="
              << QStringLiteral("thumbnail-refresh-queue-%1")
                     .arg(worker_fill_queue_generation)
                     .toStdString()
              << " worker_source="
              << worker_fill_layer_after.value(QStringLiteral("thumbnailQueueWorkerRenderSource"))
                     .toString()
                     .toStdString()
              << " worker_contract="
              << worker_fill_layer_after.value(QStringLiteral("thumbnailQueueWorkerRenderContract"))
                     .toString()
                     .toStdString()
              << " worker_count="
              << worker_fill_layer_after.value(QStringLiteral("thumbnailQueueWorkerRenderEntryCount")).toInt()
              << " consumed_worker_count="
              << worker_fill_layer_after.value(QStringLiteral("thumbnailRenderQueuedWorkerEntryCount")).toInt()
              << " cache_hits="
              << photo_editor::desktop::EditorPreviewControllerTestAccess::thumbnail_entry_cache_hit_count(
                     thumbnail_worker_fill_controller)
              << " cache_hits_before=" << worker_fill_hit_count_before
              << '\n';
    return fail("non-adjustment layer thumbnails should be rendered by the low-priority thumbnail queue worker and consumed from the entry cache");
  }
  const auto photo_layer_thumbnail_refresh_count_after_brush =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_thumbnail_refresh_count(thumbnail_defer_controller);
  const auto photo_layer_thumbnail_deferred_count_after_brush =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_thumbnail_refresh_deferred_count(thumbnail_defer_controller);
  const auto photo_layer_thumbnail_after_brush =
      photo_layers_after_brush.constFirst()
          .toMap()
          .value(QStringLiteral("thumbnailSource"))
          .toString();
  const auto thumbnail_queue_scheduled_count_after_brush =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_scheduled_count(thumbnail_defer_controller);
  const auto thumbnail_queue_dispatched_count_after_brush =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_dispatched_count(thumbnail_defer_controller);
  if (!thumbnail_defer_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("contrast"),
          12.0) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_adjustment_render_timer_active(thumbnail_defer_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::thumbnail_refresh_deferred(
          thumbnail_defer_controller)) {
    return fail("active layer adjustment debounce should keep thumbnail refresh below slider priority");
  }
  const auto photo_layers_during_slider =
      thumbnail_defer_controller.photoLayers();
  const auto photo_layer_during_slider =
      photo_layers_during_slider.isEmpty() ? QVariantMap{} : photo_layers_during_slider.constFirst().toMap();
  if (photo_layers_during_slider.isEmpty() ||
      photo_layer_during_slider.value(QStringLiteral("thumbnailSource")).toString() !=
          photo_layer_thumbnail_after_brush ||
      photo_layer_during_slider.value(QStringLiteral("thumbnailStale")).toBool() != true ||
      photo_layer_during_slider.value(QStringLiteral("thumbnailProgressive")).toBool() != true ||
      photo_layer_during_slider.value(QStringLiteral("thumbnailProgressiveReason")).toString() !=
          QStringLiteral("thumbnail_refresh_deferred") ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_count(thumbnail_defer_controller) !=
          photo_layer_thumbnail_refresh_count_after_brush ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_deferred_count(thumbnail_defer_controller) !=
          photo_layer_thumbnail_deferred_count_after_brush + 1 ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_pending(thumbnail_defer_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_timer_active(thumbnail_defer_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_refresh_queue_scheduled_count(thumbnail_defer_controller) !=
          thumbnail_queue_scheduled_count_after_brush + 1) {
    return fail("photo layer thumbnails should reuse cached layer thumbnails and queue a low-priority refresh during active slider debounce");
  }
  if (!wait_for_preview_idle(thumbnail_defer_controller)) {
    return fail("thumbnail slider priority proof should settle after the debounced layer render");
  }
  if (!wait_for_thumbnail_queue_dispatch(
          thumbnail_defer_controller,
          thumbnail_queue_dispatched_count_after_brush + 1)) {
    return fail("queued thumbnail refresh should dispatch after active slider debounce settles");
  }
  const auto photo_layers_after_slider = thumbnail_defer_controller.photoLayers();
  const auto photo_layer_after_slider =
      photo_layers_after_slider.isEmpty() ? QVariantMap{} : photo_layers_after_slider.constFirst().toMap();
  if (photo_layers_after_slider.isEmpty() ||
      photo_layer_after_slider.value(QStringLiteral("thumbnailSource")).toString().isEmpty() ||
      photo_layer_after_slider.value(QStringLiteral("thumbnailStale")).toBool() ||
      photo_layer_after_slider.value(QStringLiteral("thumbnailProgressive")).toBool() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_count(thumbnail_defer_controller) !=
          photo_layer_thumbnail_refresh_count_after_brush + 1) {
    return fail("photo layer thumbnails should rebuild once after active slider debounce settles");
  }
  const auto photo_layer_thumbnail_after_slider =
      photo_layer_after_slider.value(QStringLiteral("thumbnailSource")).toString();
  const auto photo_layer_thumbnail_refresh_count_after_slider =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_thumbnail_refresh_count(thumbnail_defer_controller);
  const auto photo_layer_thumbnail_deferred_count_after_slider =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_thumbnail_refresh_deferred_count(thumbnail_defer_controller);
  const auto thumbnail_queue_scheduled_count_after_slider =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_scheduled_count(thumbnail_defer_controller);
  const auto thumbnail_queue_dispatched_count_after_slider =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_dispatched_count(thumbnail_defer_controller);
  if (!thumbnail_defer_controller.setPhotoLayerAdjustmentValue(
          0,
          QStringLiteral("highlights"),
          -18.0)) {
    return fail("main-preview thumbnail priority proof should dirty the adjustment layer thumbnail");
  }
  if (!wait_for_preview_idle(thumbnail_defer_controller)) {
    return fail("main-preview thumbnail priority proof should settle the layer edit before render-worker proof");
  }
  photo_editor::desktop::EditorPreviewControllerTestAccess::request_render(
      thumbnail_defer_controller,
      true,
      QStringLiteral("thumbnail_main_preview_preemption"));
  const auto thumbnail_main_preview_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(
          thumbnail_defer_controller);
  photo_editor::desktop::EditorPreviewControllerTestAccess::start_render_stage(
      thumbnail_defer_controller,
      true,
      thumbnail_main_preview_request);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_running(
          thumbnail_defer_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::thumbnail_refresh_deferred(
          thumbnail_defer_controller)) {
    return fail("running main preview render should keep thumbnail refresh below preview publish priority");
  }
  const auto photo_layers_during_main_preview = thumbnail_defer_controller.photoLayers();
  const auto photo_layer_during_main_preview =
      photo_layers_during_main_preview.isEmpty()
          ? QVariantMap{}
          : photo_layers_during_main_preview.constFirst().toMap();
  if (photo_layers_during_main_preview.isEmpty() ||
      photo_layer_during_main_preview.value(QStringLiteral("thumbnailSource")).toString() !=
          photo_layer_thumbnail_after_slider ||
      photo_layer_during_main_preview.value(QStringLiteral("thumbnailStale")).toBool() != true ||
      photo_layer_during_main_preview.value(QStringLiteral("thumbnailProgressive")).toBool() != true ||
      photo_layer_during_main_preview.value(QStringLiteral("thumbnailProgressiveReason")).toString() !=
          QStringLiteral("thumbnail_refresh_deferred") ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_count(thumbnail_defer_controller) !=
          photo_layer_thumbnail_refresh_count_after_slider ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_deferred_count(thumbnail_defer_controller) !=
          photo_layer_thumbnail_deferred_count_after_slider + 1 ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::thumbnail_refresh_queue_pending(
          thumbnail_defer_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_refresh_queue_scheduled_count(thumbnail_defer_controller) !=
          thumbnail_queue_scheduled_count_after_slider + 1) {
    return fail("photo layer thumbnails should reuse cached thumbnails and queue refresh behind a running main preview render");
  }
  if (!wait_for_preview_idle(thumbnail_defer_controller)) {
    return fail("main-preview thumbnail priority proof should settle after the preview render worker");
  }
  if (!wait_for_thumbnail_queue_dispatch(
          thumbnail_defer_controller,
          thumbnail_queue_dispatched_count_after_slider + 1)) {
    return fail("queued thumbnail refresh should dispatch after running main preview render settles");
  }
  const auto photo_layers_after_main_preview = thumbnail_defer_controller.photoLayers();
  const auto photo_layer_after_main_preview =
      photo_layers_after_main_preview.isEmpty()
          ? QVariantMap{}
          : photo_layers_after_main_preview.constFirst().toMap();
  if (photo_layers_after_main_preview.isEmpty() ||
      photo_layer_after_main_preview.value(QStringLiteral("thumbnailSource")).toString().isEmpty() ||
      photo_layer_after_main_preview.value(QStringLiteral("thumbnailStale")).toBool() ||
      photo_layer_after_main_preview.value(QStringLiteral("thumbnailProgressive")).toBool() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_thumbnail_refresh_count(thumbnail_defer_controller) !=
           photo_layer_thumbnail_refresh_count_after_slider + 1) {
    return fail("photo layer thumbnails should rebuild once after the main preview render worker settles");
  }
  const auto thumbnail_retry_queue_scheduled_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_scheduled_count(thumbnail_defer_controller);
  const auto thumbnail_retry_queue_dispatched_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          thumbnail_refresh_queue_dispatched_count(thumbnail_defer_controller);
  if (!thumbnail_defer_controller.retryThumbnailRender() ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::mask_thumbnail_cache_dirty(
          thumbnail_defer_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::photo_layer_thumbnail_cache_dirty(
          thumbnail_defer_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::thumbnail_refresh_queue_pending(
          thumbnail_defer_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::thumbnail_refresh_queue_timer_active(
          thumbnail_defer_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              thumbnail_refresh_queue_scheduled_count(thumbnail_defer_controller) !=
          thumbnail_retry_queue_scheduled_before + 1) {
    return fail("thumbnail retry should dirty mask and layer thumbnail caches and queue a non-blocking refresh");
  }
  if (!wait_for_preview_idle(thumbnail_defer_controller) ||
      !wait_for_thumbnail_queue_dispatch(
          thumbnail_defer_controller,
          thumbnail_retry_queue_dispatched_before + 1)) {
    return fail("thumbnail retry should settle through the existing preview and thumbnail queues");
  }
  const auto retried_photo_layers = thumbnail_defer_controller.photoLayers();
  const auto retried_mask_sets = thumbnail_defer_controller.maskSets();
  if (retried_photo_layers.isEmpty() || retried_mask_sets.isEmpty() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::mask_thumbnail_cache_dirty(
          thumbnail_defer_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::photo_layer_thumbnail_cache_dirty(
          thumbnail_defer_controller)) {
    return fail("thumbnail retry should rebuild and clear thumbnail dirty state after the queued refresh is consumed");
  }
  const auto selection_brush_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  const auto selection_brush_strokes =
      selection_brush_mask.value(QStringLiteral("brushStrokes")).toList();
  const auto selection_brush_data =
      selection_brush_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto selection_brush_width =
      selection_brush_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto selection_brush_height =
      selection_brush_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(selection_brush_data, selection_brush_width, selection_brush_height, 0.34, 0.48) < 96 ||
      mask_value_at(selection_brush_data, selection_brush_width, selection_brush_height, 0.61, 0.65) < 96 ||
      mask_value_at(selection_brush_data, selection_brush_width, selection_brush_height, 0.08, 0.08) > 24) {
    return fail("selection brush should paint a continuous raster mask along the dragged path");
  }
  if (selection_brush_strokes.size() != 1 ||
      selection_brush_strokes.first().toMap().value(QStringLiteral("createdAtOrder")).toInt() != 1 ||
      selection_brush_strokes.first().toMap().value(QStringLiteral("operation")).toString() !=
          QStringLiteral("add") ||
      selection_brush_strokes.first()
              .toMap()
              .value(QStringLiteral("samples"))
              .toList()
              .size() != controller.activeMaskBrushPointCount() ||
      selection_brush_strokes.first()
              .toMap()
              .value(QStringLiteral("settings"))
              .toMap()
              .value(QStringLiteral("preset"))
              .toString() != QStringLiteral("Selection Brush")) {
    return fail("selection brush should commit a stroke object with samples, settings, operation, and order");
  }
  auto full_res_rebuild_probe = selection_brush_mask;
  full_res_rebuild_probe.insert(QStringLiteral("segmentationData"), QByteArray(4 * 4, '\0'));
  full_res_rebuild_probe.insert(QStringLiteral("segmentationWidth"), 4);
  full_res_rebuild_probe.insert(QStringLiteral("segmentationHeight"), 4);
  const auto full_res_rebuilt_mask =
      photo_editor::desktop::EditorPreviewControllerTestAccess::scaled_mask(
          full_res_rebuild_probe,
          QSize(192, 128),
          QStringLiteral("full-resolution-render"));
  const auto full_res_rebuilt_data =
      full_res_rebuilt_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto full_res_rebuilt_width =
      full_res_rebuilt_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto full_res_rebuilt_height =
      full_res_rebuilt_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (!full_res_rebuilt_mask.value(QStringLiteral("scaledMatteRebuiltFromBrushStrokes")).toBool() ||
      full_res_rebuilt_mask.value(QStringLiteral("scaledMattePurpose")).toString() !=
          QStringLiteral("full-resolution-render") ||
      full_res_rebuilt_width != 192 ||
      full_res_rebuilt_height != 128 ||
      full_res_rebuilt_mask.value(QStringLiteral("scaled_matte_rebuild_ms")).toLongLong() < 0 ||
      full_res_rebuilt_mask.value(QStringLiteral("scaled_matte_rebuild_pixels")).toLongLong() !=
          192 * 128 ||
      full_res_rebuilt_mask.value(QStringLiteral("full_resolution_matte_rebuild_ms")).toLongLong() < 0 ||
      full_res_rebuilt_mask.value(QStringLiteral("full_resolution_matte_rebuild_pixels")).toLongLong() !=
          192 * 128 ||
      selected_pixel_count(full_res_rebuilt_data) <= 0 ||
      mask_value_at(full_res_rebuilt_data, full_res_rebuilt_width, full_res_rebuilt_height, 0.34, 0.48) < 96 ||
      mask_value_at(full_res_rebuilt_data, full_res_rebuilt_width, full_res_rebuilt_height, 0.61, 0.65) < 96 ||
      mask_value_at(full_res_rebuilt_data, full_res_rebuilt_width, full_res_rebuilt_height, 0.08, 0.08) > 24) {
    return fail("full-resolution brush masks should replay committed stroke geometry at the target render size instead of scaling stale preview mattes");
  }
  const auto grouped_mask_sets = controller.maskSets();
  if (grouped_mask_sets.size() != 2 ||
      grouped_mask_sets.at(1).toMap().value(QStringLiteral("sources")).toList().size() != 2) {
    return fail("refine brushes should join the active selection mask set instead of creating a new set");
  }
  const auto refine_set = grouped_mask_sets.at(1).toMap();
  const auto refine_sources = refine_set.value(QStringLiteral("sources")).toList();
  if (!refine_set.value(QStringLiteral("thumbnailSource")).toString().startsWith(
          QStringLiteral("data:image/png;base64,")) ||
      !refine_sources.at(0).toMap()
           .value(QStringLiteral("thumbnailSource"))
           .toString()
           .startsWith(QStringLiteral("data:image/png;base64,")) ||
      !refine_sources.at(1).toMap()
           .value(QStringLiteral("thumbnailSource"))
           .toString()
           .startsWith(QStringLiteral("data:image/png;base64,"))) {
    return fail("grouped refine masks should expose matte thumbnails for the set and each source");
  }

  controller.beginActiveBrushStroke();
  controller.addBrushPoint(0.18, 0.22);
  (void)finish_brush_stroke(controller);
  controller.beginActiveBrushStroke();
  controller.addBrushPoint(0.82, 0.78);
  (void)finish_brush_stroke(controller);
  const auto separated_stroke_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  if (separated_stroke_mask.value(QStringLiteral("brushStrokes")).toList().size() != 3) {
    return fail("separate committed brush strokes should remain separate stroke history objects");
  }
  const auto separated_stroke_data =
      separated_stroke_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto separated_stroke_width =
      separated_stroke_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto separated_stroke_height =
      separated_stroke_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(separated_stroke_data, separated_stroke_width, separated_stroke_height, 0.5, 0.5) > 24) {
    return fail("separate brush strokes should not interpolate a bridge through the gap between strokes");
  }
  const auto coalesced_strokes_before =
      separated_stroke_mask.value(QStringLiteral("brushStrokes")).toList().size();
  controller.beginActiveBrushStroke();
  controller.addBrushPoint(0.16, 0.18);
  controller.endActiveBrushStroke();
  controller.beginActiveBrushStroke();
  controller.addBrushPoint(0.84, 0.22);
  controller.endActiveBrushStroke();
  if (!wait_for_brush_commit(controller)) {
    return fail("queued short brush strokes should finish their coalesced raster commit");
  }
  const auto coalesced_stroke_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  const auto coalesced_strokes =
      coalesced_stroke_mask.value(QStringLiteral("brushStrokes")).toList();
  const auto coalesced_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          controller);
  const auto coalesced_data =
      coalesced_stroke_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto coalesced_width =
      coalesced_stroke_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto coalesced_height =
      coalesced_stroke_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (coalesced_strokes.size() != coalesced_strokes_before + 2 ||
      coalesced_telemetry.value(QStringLiteral("coalesced_stroke_count")).toInt() != 2 ||
      coalesced_telemetry.value(QStringLiteral("coalesced_point_count")).toInt() < 2 ||
      coalesced_telemetry.value(QStringLiteral("async_job_kind")).toString() !=
          QStringLiteral("brush_rasterization") ||
      coalesced_telemetry.value(QStringLiteral("async_job_stage")).toString() !=
          QStringLiteral("interactive") ||
      !coalesced_telemetry.value(QStringLiteral("async_job_id"))
           .toString()
           .startsWith(QStringLiteral("brush_rasterization-")) ||
      coalesced_telemetry.value(QStringLiteral("async_job_asset_id")).toString() !=
          coalesced_telemetry.value(QStringLiteral("asset_id")).toString() ||
      coalesced_telemetry.value(QStringLiteral("async_job_file_path")).toString().isEmpty() ||
      !coalesced_telemetry.value(QStringLiteral("async_job_state_signature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      mask_value_at(coalesced_data, coalesced_width, coalesced_height, 0.50, 0.20) > 24) {
    return fail("queued short brush strokes should coalesce into one identified raster job without merging stroke history");
  }
  controller.setActiveMaskBrushSize(0.12);
  controller.setActiveMaskBrushSpacing(0.10);
  controller.setActiveMaskBrushAutoMask(true);
  controller.setActiveMaskBrushProtectEdges(true);
  if (!wait_for_preview_idle(controller)) {
    return fail("controller should settle before stale brush cancellation probe");
  }
  const auto stale_cancel_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::brush_raster_cancelled_count(
          controller);
  const auto stale_cancel_strokes_before =
      coalesced_stroke_mask.value(QStringLiteral("brushStrokes")).toList().size();
  controller.beginActiveBrushStroke();
  for (auto index = 0; index < 160; ++index) {
    const auto t = static_cast<double>(index) / 159.0;
    controller.addBrushPoint(0.12 + 0.76 * t, 0.72 - 0.42 * t);
  }
  controller.endActiveBrushStroke();
  auto stale_probe_running = false;
  for (auto elapsed_ms = 0; elapsed_ms < 2000; elapsed_ms += 2) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
    if (photo_editor::desktop::EditorPreviewControllerTestAccess::brush_raster_commit_running(
            controller)) {
      stale_probe_running = true;
      break;
    }
    QThread::msleep(2);
  }
  if (!stale_probe_running) {
    return fail("heavy brush stroke should start an in-flight raster worker for stale cancellation probe");
  }
  controller.beginActiveBrushStroke();
  controller.addBrushPoint(0.20, 0.78);
  controller.endActiveBrushStroke();
  if (!wait_for_brush_commit(controller, 8000)) {
    return fail("superseded brush raster commit should cancel and replay queued strokes");
  }
  const auto stale_cancel_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  const auto stale_cancel_strokes =
      stale_cancel_mask.value(QStringLiteral("brushStrokes")).toList();
  const auto stale_cancel_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          controller);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::brush_raster_cancelled_count(
          controller) <= stale_cancel_count_before ||
      stale_cancel_strokes.size() != stale_cancel_strokes_before + 2 ||
      stale_cancel_telemetry.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("brush_rasterization") ||
      stale_cancel_telemetry.value(QStringLiteral("async_job_kind")).toString() !=
          QStringLiteral("brush_rasterization") ||
      stale_cancel_telemetry.value(QStringLiteral("async_job_stage")).toString() !=
          QStringLiteral("interactive") ||
      !stale_cancel_telemetry.value(QStringLiteral("async_job_id"))
           .toString()
           .startsWith(QStringLiteral("brush_rasterization-")) ||
      stale_cancel_telemetry.value(QStringLiteral("async_job_asset_id")).toString() !=
          stale_cancel_telemetry.value(QStringLiteral("asset_id")).toString() ||
      !stale_cancel_telemetry.value(QStringLiteral("async_job_state_signature"))
           .toString()
           .startsWith(QStringLiteral("sha1:"))) {
    return fail("newer compatible brush strokes should cancel stale in-flight raster work and publish only an identified current brush job");
  }
  controller.setActiveMaskBrushAutoMask(false);
  controller.setActiveMaskBrushProtectEdges(false);
  controller.setActiveMaskBrushSize(0.08);
  controller.setActiveMaskBrushSpacing(0.75);
  const auto brush_points_before_undo_probe = controller.activeMaskBrushPointCount();
  controller.beginActiveBrushStroke();
  controller.addBrushPoint(0.48, 0.18);
  controller.addBrushPoint(0.52, 0.20);
  controller.addBrushPoint(0.56, 0.22);
  (void)finish_brush_stroke(controller);
  const auto brush_points_after_grouped_stroke = controller.activeMaskBrushPointCount();
  if (brush_points_after_grouped_stroke <= brush_points_before_undo_probe || !controller.canUndo()) {
    return fail("a completed brush stroke should add committed brush samples and one undo entry");
  }
  controller.undo();
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskBrushPointCount() != brush_points_before_undo_probe) {
    return fail("one undo should remove every sample from the last completed brush stroke without removing the active mask");
  }
  if (!controller.canRedo()) {
    return fail("undoing a grouped brush stroke should create one redo step");
  }
  controller.redo();
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskBrushPointCount() != brush_points_after_grouped_stroke) {
    return fail("redo should restore every sample from the grouped brush stroke in one step");
  }

  controller.startSelectionBrush(true);
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskOperation() != QStringLiteral("subtract") ||
      controller.activeMaskToolRole() != QStringLiteral("selection") ||
      !controller.activeMaskCommitted()) {
    return fail("refine subtract should create a subtractive brush mask");
  }
  if (controller.activeMaskOutlinePoints().isEmpty()) {
    return fail("switching to an erase brush should keep outlining the current selection set");
  }
  const auto grouped_mask_sets_after_subtract = controller.maskSets();
  if (grouped_mask_sets_after_subtract.size() != 2 ||
      grouped_mask_sets_after_subtract.at(1).toMap().value(QStringLiteral("sources")).toList().size() != 3) {
    return fail("grouped selection mask views should refresh after stack edits");
  }

  photo_editor::desktop::EditRecipeState dependency_state;
  QVariantMap reusable_mask;
  reusable_mask.insert(QStringLiteral("id"), QStringLiteral("linear-base"));
  reusable_mask.insert(QStringLiteral("name"), QStringLiteral("Reusable Base"));
  reusable_mask.insert(QStringLiteral("kind"), QStringLiteral("linear"));
  reusable_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("dependency-set"));
  reusable_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Dependency Set"));
  reusable_mask.insert(QStringLiteral("outputId"), QStringLiteral("base-alpha"));
  reusable_mask.insert(QStringLiteral("reusableOutput"), true);
  QVariantMap dependent_mask;
  dependent_mask.insert(QStringLiteral("id"), QStringLiteral("radial-child"));
  dependent_mask.insert(QStringLiteral("name"), QStringLiteral("Dependent Radial"));
  dependent_mask.insert(QStringLiteral("kind"), QStringLiteral("radial"));
  dependent_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("dependency-set"));
  dependent_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Dependency Set"));
  dependent_mask.insert(QStringLiteral("parentId"), QStringLiteral("linear-base"));
  dependent_mask.insert(QStringLiteral("operation"), QStringLiteral("subtract"));
  dependency_state.mask_stack = {reusable_mask, dependent_mask};
  photo_editor::desktop::EditorPreviewController dependency_controller;
  dependency_controller.loadAsset(
      QStringLiteral("mask-dependency-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(dependency_state));
  const auto dependency_outputs = dependency_controller.reusableMaskOutputs();
  const auto dependency_sources =
      dependency_controller.maskSets().at(0).toMap().value(QStringLiteral("sources")).toList();
  if (dependency_outputs.size() != 1 ||
      dependency_outputs.at(0).toMap().value(QStringLiteral("dependencyLabel")).toString() !=
          QStringLiteral("Reusable matte: base-alpha") ||
      !dependency_sources.at(0)
           .toMap()
           .value(QStringLiteral("dependencyLabel"))
           .toString()
           .contains(QStringLiteral("Reusable output: base-alpha")) ||
      dependency_sources.at(1).toMap().value(QStringLiteral("dependencyLabel")).toString() !=
          QStringLiteral("Depends on Reusable Base")) {
    return fail("mask sets should expose reusable outputs and source dependency labels");
  }

  controller.startSelectionEdgeBrush(false);
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskOperation() != QStringLiteral("add") ||
      controller.activeMaskToolRole() != QStringLiteral("selection") ||
      !controller.activeMaskCommitted() ||
      !almost_equal(controller.activeMaskBrushSize(), 0.026) ||
      !almost_equal(controller.activeMaskFeather(), 0.34)) {
    return fail("edge add should create a smaller direct-paint brush for boundary cleanup");
  }

  controller.startSelectionEdgeBrush(true);
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskOperation() != QStringLiteral("subtract") ||
      controller.activeMaskToolRole() != QStringLiteral("selection") ||
      !controller.activeMaskCommitted() ||
      !almost_equal(controller.activeMaskBrushSize(), 0.026) ||
      !almost_equal(controller.activeMaskFeather(), 0.34)) {
    return fail("edge erase should create a smaller subtractive brush for boundary cleanup");
  }

  if (controller.retouchWorkflowStateSignature() !=
      QStringLiteral(
          "remove:inactive|heal:inactive|clone:inactive|patch:inactive|dust:inactive|red-eye:inactive")) {
    return fail("retouch workflows should start with every primary mode inactive");
  }

  controller.startRemoveBrush();
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      controller.activeMaskCommitted() ||
      !almost_equal(controller.activeMaskRemoveStrength(), 100.0)) {
    return fail("remove brush should create a brush mask configured for object removal");
  }
  if (controller.retouchWorkflowStateSignature() !=
      QStringLiteral(
          "remove:draft|heal:inactive|clone:inactive|patch:inactive|dust:inactive|red-eye:inactive")) {
    return fail("remove retouch workflow should enter draft state");
  }
  if (controller.retouchCount() != 1 ||
      controller.retouchStack().size() != 1) {
    return fail("retouch grouped views should refresh after stack edits");
  }

  controller.startHealBrush();
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      controller.activeMaskCommitted() ||
      controller.activeMaskRetouchMode() != QStringLiteral("heal") ||
      controller.activeMaskRemoveStrength() <= 0.0 ||
      !almost_equal(controller.activeMaskHealTextureBlend(), 0.68) ||
      !almost_equal(controller.activeMaskHealColorBlend(), 0.46)) {
    return fail("heal brush should create a dedicated retouch brush");
  }
  controller.setActiveMaskHealTextureBlend(0.24);
  controller.setActiveMaskHealColorBlend(0.82);
  const auto heal_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  if (!almost_equal(controller.activeMaskHealTextureBlend(), 0.24) ||
      !almost_equal(controller.activeMaskHealColorBlend(), 0.82) ||
      !almost_equal(heal_mask.value(QStringLiteral("healTextureBlend")).toDouble(), 0.24) ||
      !almost_equal(heal_mask.value(QStringLiteral("healColorBlend")).toDouble(), 0.82)) {
    return fail("heal brush should expose separate texture and color blend controls");
  }
  if (controller.retouchWorkflowStateSignature() !=
      QStringLiteral("remove:draft|heal:draft|clone:inactive|patch:inactive|dust:inactive|red-eye:inactive")) {
    return fail("heal retouch workflow should have a separate draft state");
  }

  controller.startPatchBrush();
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      controller.activeMaskCommitted() ||
      controller.activeMaskRetouchMode() != QStringLiteral("patch") ||
      !almost_equal(controller.activeMaskRemoveStrength(), 88.0) ||
      !controller.activeMaskBrushAutoMask() ||
      !controller.activeMaskPatchSourceSet() ||
      !almost_equal(controller.activeMaskPatchSourceX(), 0.62) ||
      !almost_equal(controller.activeMaskPatchSourceY(), 0.50) ||
      !almost_equal(controller.activeMaskPatchSourceWidth(), 0.18) ||
      !almost_equal(controller.activeMaskPatchSourceHeight(), 0.18)) {
    return fail("patch brush should create an auto-masked retouch brush");
  }
  if (!controller.setActiveMaskPatchSourceRegion(0.12, 0.18, 0.24, 0.16) ||
      !controller.activeMaskPatchSourceSet() ||
      !almost_equal(controller.activeMaskPatchSourceX(), 0.12) ||
      !almost_equal(controller.activeMaskPatchSourceY(), 0.18) ||
      !almost_equal(controller.activeMaskPatchSourceWidth(), 0.24) ||
      !almost_equal(controller.activeMaskPatchSourceHeight(), 0.16)) {
    return fail("patch brush should expose a draggable source region before applying");
  }
  if (controller.retouchWorkflowStateSignature() !=
      QStringLiteral("remove:draft|heal:draft|clone:inactive|patch:draft|dust:inactive|red-eye:inactive")) {
    return fail("patch retouch workflow should have a separate draft state");
  }
  controller.beginActiveBrushStroke();
  controller.addBrushPoint(0.42, 0.52);
  (void)finish_brush_stroke(controller);
  qputenv("PHOTO_EDITOR_TEST_FAKE_AI_MODELS", "1");
  controller.applyActiveRetouch();
  if (!wait_for_preview_idle(controller, 6000)) {
    return fail("async patch inpaint apply should finish before inspecting the patch");
  }
  qunsetenv("PHOTO_EDITOR_TEST_FAKE_AI_MODELS");
  const auto patch_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  if (!patch_mask.value(QStringLiteral("committed")).toBool() ||
      patch_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() ||
      patch_mask.value(QStringLiteral("inpaintModelName")).toString() !=
          QStringLiteral("source-region-patch-v1") ||
      patch_mask.value(QStringLiteral("inpaintQualityMode")).toString() != QStringLiteral("preview") ||
      std::abs(patch_mask.value(QStringLiteral("inpaintSeamFeather")).toDouble() - 4.0) > 1e-6 ||
      std::abs(patch_mask.value(QStringLiteral("inpaintColorMatch")).toDouble() - 0.75) > 1e-6 ||
      !patch_mask.value(QStringLiteral("inpaintFinalRequired")).toBool() ||
      !patch_mask.value(QStringLiteral("patchSourceSet")).toBool() ||
      !almost_equal(patch_mask.value(QStringLiteral("patchSourceX")).toDouble(), 0.12) ||
      !almost_equal(patch_mask.value(QStringLiteral("patchSourceY")).toDouble(), 0.18) ||
      !almost_equal(patch_mask.value(QStringLiteral("patchSourceWidth")).toDouble(), 0.24) ||
      !almost_equal(patch_mask.value(QStringLiteral("patchSourceHeight")).toDouble(), 0.16)) {
    return fail("patch brush should bake an inpaint patch when applied");
  }
  if (controller.retouchWorkflowStateSignature() !=
      QStringLiteral("remove:draft|heal:draft|clone:inactive|patch:applied|dust:inactive|red-eye:inactive")) {
    return fail("applied patch workflow should not collapse the other retouch states");
  }
  if (!controller.status().contains(QStringLiteral("Source-region Patch baked from the selected source area")) ||
      controller.status().contains(QStringLiteral("LaMa Model Inpaint patch baked")) ||
      controller.status().contains(QStringLiteral("Local CPU Fill patch baked"))) {
    return fail("source-region patch status should not be mislabeled as model inpaint or CPU fill");
  }
  if (!controller.activeInpaintFinalizationAvailable() ||
      !controller.status().contains(QStringLiteral("Final-resolution inpaint must be queued separately"))) {
    return fail("preview-quality patch apply should expose explicit final-resolution inpaint queueing");
  }
  if (controller.inpaintProgressActive() ||
      controller.inpaintProgressSignature() != QStringLiteral("idle|progress:0.000|label:none")) {
    return fail("inpaint progress should be idle before a final-resolution job starts");
  }
  if (!controller.queueActiveFinalInpaintPatch()) {
    return fail("preview-quality patch should queue a final-resolution inpaint pass");
  }
  if (!wait_for_preview_idle(controller, 6000)) {
    return fail("async final-resolution inpaint queue should finish before inspecting the promoted patch");
  }
  const auto finalized_patch_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  const auto final_inpaint_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(controller);
  if (finalized_patch_mask.value(QStringLiteral("inpaintQualityMode")).toString() != QStringLiteral("final") ||
      finalized_patch_mask.value(QStringLiteral("inpaintFinalRequired")).toBool() ||
      controller.activeInpaintFinalizationAvailable() ||
      !controller.status().contains(QStringLiteral("Queued final-resolution inpaint and baked patch retouch")) ||
      controller.inpaintProgressActive() ||
      controller.inpaintProgressValue() < 0.99 ||
      controller.inpaintProgressLabel() != QStringLiteral("Final inpaint complete") ||
      controller.inpaintProgressSignature() !=
          QStringLiteral("idle|progress:1.000|label:Final inpaint complete") ||
      finalized_patch_mask.value(QStringLiteral("inpaintPatchPayloadBytes")).toLongLong() <= 0 ||
      finalized_patch_mask.value(QStringLiteral("inpaintCandidatePayloadBytes")).toLongLong() <= 0 ||
      finalized_patch_mask.value(QStringLiteral("inpaintCandidatePayloadBytes")).toLongLong() > 33554432 ||
      final_inpaint_telemetry.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("inpaint_regenerate") ||
      final_inpaint_telemetry.value(QStringLiteral("quality_mode")).toString() != QStringLiteral("final") ||
      final_inpaint_telemetry.value(QStringLiteral("async_job_kind")).toString() !=
          QStringLiteral("inpaint_regenerate") ||
      final_inpaint_telemetry.value(QStringLiteral("async_job_stage")).toString() !=
          QStringLiteral("regenerate") ||
      final_inpaint_telemetry.value(QStringLiteral("inpaint_execution_mode")).toString() !=
          QStringLiteral("async_worker") ||
      final_inpaint_telemetry.value(QStringLiteral("inpaint_decode_edge")).toInt() != 0 ||
      final_inpaint_telemetry.value(QStringLiteral("inpaint_worker_thread")).toString().isEmpty() ||
      final_inpaint_telemetry.value(QStringLiteral("inpaint_worker_elapsed_ms")).toLongLong() < 0 ||
      !final_inpaint_telemetry.value(QStringLiteral("async_job_id"))
           .toString()
           .startsWith(QStringLiteral("inpaint_regenerate-")) ||
      final_inpaint_telemetry.value(QStringLiteral("async_job_asset_id")).toString() !=
          final_inpaint_telemetry.value(QStringLiteral("asset_id")).toString() ||
      !final_inpaint_telemetry.value(QStringLiteral("async_job_state_signature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      final_inpaint_telemetry.value(QStringLiteral("memory_policy_signature")).toString() !=
          controller.maskMemoryPolicySignature() ||
      final_inpaint_telemetry.value(QStringLiteral("inpaint_patch_payload_bytes")).toLongLong() <= 0 ||
      final_inpaint_telemetry.value(QStringLiteral("inpaint_candidate_payload_bytes")).toLongLong() <= 0 ||
      final_inpaint_telemetry.value(QStringLiteral("inpaint_candidate_payload_bytes")).toLongLong() > 33554432 ||
      final_inpaint_telemetry.value(QStringLiteral("progress_label")).toString() !=
          QStringLiteral("Updating retouch patch") ||
      final_inpaint_telemetry.value(QStringLiteral("progress_value")).toDouble() < 0.90) {
    return fail("final-resolution inpaint queue should promote the active patch and expose progressive status");
  }

  controller.startCloneBrush();
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      controller.activeMaskCommitted() ||
      controller.activeMaskRetouchMode() != QStringLiteral("clone") ||
      controller.activeMaskCloneSourceSet() ||
      !controller.activeMaskCloneAligned()) {
    return fail("clone stamp should start without a source point");
  }
  if (controller.retouchWorkflowStateSignature() !=
      QStringLiteral(
          "remove:draft|heal:draft|clone:source-required|patch:applied|dust:inactive|red-eye:inactive")) {
    return fail("clone workflow should require a source before painting");
  }
  controller.setActiveMaskCloneSource(0.22, 0.34);
  if (!controller.activeMaskCloneSourceSet() ||
      !almost_equal(controller.activeMaskCloneSourceX(), 0.22) ||
      !almost_equal(controller.activeMaskCloneSourceY(), 0.34)) {
    return fail("clone stamp should store the chosen source point");
  }
  if (controller.retouchWorkflowStateSignature() !=
      QStringLiteral("remove:draft|heal:draft|clone:draft|patch:applied|dust:inactive|red-eye:inactive")) {
    return fail("clone workflow should move from source-required to draft once a source is set");
  }
  controller.setActiveMaskCloneScale(1.1);
  controller.setActiveMaskCloneRotation(6.0);
  controller.setActiveMaskCloneAligned(false);
  if (!controller.saveActiveCloneSource() ||
      controller.activeMaskCloneSourceCount() != 1 ||
      controller.activeMaskCloneSourceIndex() != 0) {
    return fail("clone stamp should save the current source into a reusable source panel");
  }
  controller.setActiveMaskCloneSource(0.44, 0.46);
  controller.setActiveMaskCloneScale(1.6);
  controller.setActiveMaskCloneRotation(-12.0);
  controller.setActiveMaskCloneAligned(true);
  if (!controller.saveActiveCloneSource() ||
      controller.activeMaskCloneSourceCount() != 2 ||
      controller.activeMaskCloneSourceIndex() != 1 ||
      !controller.selectActiveCloneSource(0) ||
      !almost_equal(controller.activeMaskCloneSourceX(), 0.22) ||
      !almost_equal(controller.activeMaskCloneSourceY(), 0.34) ||
      !almost_equal(controller.activeMaskCloneScale(), 1.1) ||
      !almost_equal(controller.activeMaskCloneRotation(), 6.0) ||
      controller.activeMaskCloneAligned() ||
      !controller.deleteActiveCloneSource(1) ||
      controller.activeMaskCloneSourceCount() != 1 ||
      controller.activeMaskCloneSourceIndex() != 0) {
    return fail("clone stamp should select and delete multiple saved source entries");
  }
  controller.setActiveMaskCloneScale(1.4);
  controller.setActiveMaskCloneRotation(18.0);
  controller.setActiveMaskCloneAligned(false);
  if (!almost_equal(controller.activeMaskCloneScale(), 1.4) ||
      !almost_equal(controller.activeMaskCloneRotation(), 18.0) ||
      controller.activeMaskCloneAligned()) {
    return fail("clone stamp should expose scale, rotation, and aligned-mode controls");
  }
  controller.addBrushPoint(0.48, 0.52);
  const auto clone_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  if (!almost_equal(clone_mask.value(QStringLiteral("cloneOffsetX")).toDouble(), -0.26) ||
      !almost_equal(clone_mask.value(QStringLiteral("cloneOffsetY")).toDouble(), -0.18) ||
      !almost_equal(clone_mask.value(QStringLiteral("cloneScale")).toDouble(), 1.4) ||
      !almost_equal(clone_mask.value(QStringLiteral("cloneRotation")).toDouble(), 18.0) ||
      clone_mask.value(QStringLiteral("cloneAligned"), true).toBool()) {
    return fail("clone stamp should derive a stable source offset from the first painted point");
  }
  controller.beginActiveBrushStroke();
  controller.addBrushPoint(0.58, 0.66);
  (void)finish_brush_stroke(controller);
  const auto non_aligned_clone_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  if (!almost_equal(non_aligned_clone_mask.value(QStringLiteral("cloneOffsetX")).toDouble(), -0.36) ||
      !almost_equal(non_aligned_clone_mask.value(QStringLiteral("cloneOffsetY")).toDouble(), -0.32)) {
    return fail("non-aligned clone stamp should restart source offset from the current stroke target");
  }

  photo_editor::desktop::EditorPreviewController generated_retouch_controller;
  generated_retouch_controller.startRemoveBrush();
  auto generated_retouch_mask =
      generated_retouch_controller.maskStack().at(generated_retouch_controller.activeMaskIndex()).toMap();
  generated_retouch_mask.insert(QStringLiteral("kind"), QStringLiteral("object"));
  generated_retouch_mask.insert(QStringLiteral("name"), QStringLiteral("Dust Removal"));
  generated_retouch_mask.insert(QStringLiteral("retouchMode"), QStringLiteral("dust"));
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          generated_retouch_controller,
          generated_retouch_mask) ||
      generated_retouch_controller.retouchWorkflowStateSignature() !=
          QStringLiteral(
              "remove:inactive|heal:inactive|clone:inactive|patch:inactive|dust:review|red-eye:inactive")) {
    return fail("dust retouch workflow should be a generated review state");
  }
  generated_retouch_mask.insert(QStringLiteral("name"), QStringLiteral("Red-Eye Removal"));
  generated_retouch_mask.insert(QStringLiteral("retouchMode"), QStringLiteral("red-eye"));
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          generated_retouch_controller,
          generated_retouch_mask) ||
      generated_retouch_controller.retouchWorkflowStateSignature() !=
          QStringLiteral(
              "remove:inactive|heal:inactive|clone:inactive|patch:inactive|dust:inactive|red-eye:review")) {
    return fail("red-eye retouch workflow should be a generated review state");
  }

  controller.setActiveMaskSelectionExpand(22.0);
  if (!almost_equal(controller.activeMaskSelectionExpand(), 22.0)) {
    return fail("selection refine controls should expose expand and contract on the active mask");
  }
  controller.setActiveMaskSelectionSmooth(31.0);
  if (!almost_equal(controller.activeMaskSelectionSmooth(), 31.0)) {
    return fail("selection refine controls should expose smoothing on the active mask");
  }
  controller.setActiveMaskSelectionFineEdge(73.0);
  if (!almost_equal(controller.activeMaskSelectionFineEdge(), 73.0)) {
    return fail("selection refine controls should expose fine-edge recovery on the active mask");
  }
  controller.setActiveMaskSelectionEdgeContrast(64.0);
  if (!almost_equal(controller.activeMaskSelectionEdgeContrast(), 64.0)) {
    return fail("selection refine controls should expose edge contrast on the active mask");
  }
  controller.setActiveMaskSelectionDecontaminate(42.0);
  if (!almost_equal(controller.activeMaskSelectionDecontaminate(), 42.0)) {
    return fail("selection refine controls should expose edge decontamination on the active mask");
  }
  controller.setActiveMaskSelectionInverted(true);
  if (!controller.activeMaskSelectionInverted()) {
    return fail("selection refine controls should allow inverting the active selection");
  }

  const auto refinement_preview_source_path =
      runtime_root.filePath(QStringLiteral("refinement-preview-source.png"));
  if (!preset_thumbnail_fixture_source_image().save(refinement_preview_source_path)) {
    return fail("refinement preview fixture image should be writable");
  }
  photo_editor::desktop::EditorPreviewController refinement_preview_controller;
  refinement_preview_controller.loadAsset(
      QStringLiteral("asset-refinement-preview"),
      refinement_preview_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  refinement_preview_controller.startSelectionBrush();
  refinement_preview_controller.setActiveMaskBrushSize(0.24);
  refinement_preview_controller.addBrushPoint(0.42, 0.50);
  refinement_preview_controller.setActiveMaskSelectionInverted(true);
  refinement_preview_controller.setActiveMaskSelectionExpand(64.0);
  refinement_preview_controller.setActiveMaskSelectionSmooth(72.0);
  refinement_preview_controller.setActiveMaskSelectionFineEdge(66.0);
  refinement_preview_controller.setActiveMaskSelectionEdgeContrast(80.0);
  refinement_preview_controller.setActiveMaskSelectionDecontaminate(35.0);
  refinement_preview_controller.setMaskPreviewMode(QStringLiteral("on-white"));
  refinement_preview_controller.setMaskRefinementPreviewMode(QStringLiteral("refined"));
  const auto refined_selection_preview = refinement_preview_controller.activeMaskPreviewSource();
  const auto refined_preview_job =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          latest_refine_edge_preview_job(refinement_preview_controller);
  refinement_preview_controller.setMaskRefinementPreviewMode(QStringLiteral("show original"));
  const auto original_selection_preview =
      refinement_preview_controller.activeMaskPreviewSource();
  const auto original_preview_job =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          latest_refine_edge_preview_job(refinement_preview_controller);
  if (refinement_preview_controller.maskRefinementPreviewMode() != QStringLiteral("original") ||
      refined_selection_preview.isEmpty() ||
      original_selection_preview.isEmpty() ||
      refined_selection_preview == original_selection_preview ||
      refined_preview_job.job_kind != QStringLiteral("refine_edge_preview") ||
      refined_preview_job.stage != QStringLiteral("active_preview_cache") ||
      refined_preview_job.job_id.isEmpty() ||
      !refined_preview_job.job_id.startsWith(QStringLiteral("refine_edge_preview-")) ||
      refined_preview_job.asset_id != QStringLiteral("asset-refinement-preview") ||
      refined_preview_job.file_path != refinement_preview_source_path ||
      refined_preview_job.generation !=
          photo_editor::desktop::EditorPreviewControllerTestAccess::
              mask_work_state_revision(refinement_preview_controller) ||
      refined_preview_job.state_revision != refined_preview_job.generation ||
      refined_preview_job.state_signature.isEmpty() ||
      original_preview_job.job_kind != QStringLiteral("refine_edge_preview") ||
      original_preview_job.stage != QStringLiteral("active_preview_cache") ||
      original_preview_job.job_id == refined_preview_job.job_id ||
      !almost_equal(refinement_preview_controller.activeMaskSelectionExpand(), 64.0) ||
      !almost_equal(refinement_preview_controller.activeMaskSelectionSmooth(), 72.0) ||
      !almost_equal(refinement_preview_controller.activeMaskSelectionFineEdge(), 66.0) ||
      !almost_equal(refinement_preview_controller.activeMaskSelectionEdgeContrast(), 80.0) ||
      !almost_equal(refinement_preview_controller.activeMaskSelectionDecontaminate(), 35.0) ||
      !refinement_preview_controller.activeMaskSelectionInverted()) {
    return fail("selection refinement preview should compare original vs refined without mutating refine fields");
  }
  refinement_preview_controller.setMaskRefinementPreviewMode(QStringLiteral("refined"));
  if (refinement_preview_controller.maskRefinementPreviewMode() != QStringLiteral("refined") ||
      refinement_preview_controller.activeMaskPreviewSource() != refined_selection_preview ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              latest_refine_edge_preview_job(refinement_preview_controller)
                  .job_id != refined_preview_job.job_id) {
    return fail("selection refinement preview should return to the refined selection preview");
  }
  refinement_preview_controller.setMaskPreviewMode(QStringLiteral("refine edge"));
  const auto refinement_edge_trace = refinement_preview_controller.activeMaskOutlineTrace();
  if (refinement_preview_controller.maskPreviewMode() != QStringLiteral("overlay") ||
      !refinement_preview_controller.activeMaskPreviewSource().isEmpty() ||
      refinement_preview_controller.activeMaskOutlinePaths().isEmpty() ||
      refinement_edge_trace.value(QStringLiteral("operation")).toString() != QStringLiteral("refine_edge") ||
      refinement_edge_trace.value(QStringLiteral("stage")).toString() != QStringLiteral("refine_edge") ||
      refinement_edge_trace.value(QStringLiteral("elapsed_ms")).toLongLong() < 0 ||
      refinement_edge_trace.value(QStringLiteral("refine_edge_worker_elapsed_ms")).toLongLong() < 0 ||
      refinement_edge_trace.value(QStringLiteral("source_event")).toString() !=
          QStringLiteral("active_mask_outline_trace")) {
    return fail("selection refinement edge preview requests should normalize to source-free clean overlay rendering with standard refine-edge timing telemetry");
  }

  photo_editor::desktop::EditorPreviewController no_refinement_delta_controller;
  no_refinement_delta_controller.loadAsset(
      QStringLiteral("asset-no-refinement-delta-preview"),
      refinement_preview_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  no_refinement_delta_controller.startSelectionBrush();
  no_refinement_delta_controller.setActiveMaskBrushSize(0.24);
  no_refinement_delta_controller.addBrushPoint(0.42, 0.50);
  no_refinement_delta_controller.setMaskPreviewMode(QStringLiteral("refinement-edge"));
  if (no_refinement_delta_controller.maskPreviewMode() != QStringLiteral("overlay") ||
      !no_refinement_delta_controller.activeMaskPreviewSource().isEmpty() ||
      no_refinement_delta_controller.activeMaskOutlinePaths().isEmpty()) {
    return fail("stale refinement edge preview state should remain source-free by falling back to clean overlay rendering");
  }

  QImage high_quality_refinement_source(1200, 800, QImage::Format_ARGB32);
  high_quality_refinement_source.setColorSpace(photo_editor::desktop::preview_color_space());
  for (int y = 0; y < high_quality_refinement_source.height(); ++y) {
    for (int x = 0; x < high_quality_refinement_source.width(); ++x) {
      high_quality_refinement_source.setPixelColor(
          x,
          y,
          QColor(24 + (x % 80), 52 + (y % 110), 96 + ((x + y) % 90), 255));
    }
  }
  const auto high_quality_refinement_source_path =
      runtime_root.filePath(QStringLiteral("high-quality-refinement-preview-source.png"));
  if (!high_quality_refinement_source.save(high_quality_refinement_source_path)) {
    return fail("high-quality refinement preview fixture image should be writable");
  }
  photo_editor::desktop::EditorPreviewController high_quality_preview_controller;
  high_quality_preview_controller.loadAsset(
      QStringLiteral("asset-high-quality-refinement-preview"),
      high_quality_refinement_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  high_quality_preview_controller.startSelectionBrush();
  high_quality_preview_controller.setActiveMaskBrushSize(0.22);
  high_quality_preview_controller.addBrushPoint(0.48, 0.52);
  high_quality_preview_controller.setActiveMaskSelectionFineEdge(62.0);
  high_quality_preview_controller.setActiveMaskSelectionEdgeContrast(70.0);
  high_quality_preview_controller.setMaskPreviewMode(QStringLiteral("on-white"));
  const auto normal_quality_tier = high_quality_preview_controller.activeMaskPreviewQualityTier();
  const auto normal_quality_preview_image =
      decode_png_data_url(high_quality_preview_controller.activeMaskPreviewSource());
  high_quality_preview_controller.setMaskRefinementHighQualityPreview(true);
  const auto high_quality_tier = high_quality_preview_controller.activeMaskPreviewQualityTier();
  const auto high_quality_preview_image =
      decode_png_data_url(high_quality_preview_controller.activeMaskPreviewSource());
  const auto full_preview_generation_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_full_preview_generation(
          high_quality_preview_controller);
  high_quality_preview_controller.setMaskFullResolutionPreview(true);
  const auto full_quality_tier = high_quality_preview_controller.activeMaskPreviewQualityTier();
  const auto full_resolution_preview_image =
      decode_png_data_url(high_quality_preview_controller.activeMaskPreviewSource());
  if (!high_quality_preview_controller.maskRefinementHighQualityPreview() ||
      !high_quality_preview_controller.maskFullResolutionPreview() ||
      normal_quality_tier != QStringLiteral("draft") ||
      high_quality_tier != QStringLiteral("high") ||
      full_quality_tier != QStringLiteral("full") ||
      normal_quality_preview_image.isNull() ||
      high_quality_preview_image.isNull() ||
      full_resolution_preview_image.isNull() ||
      std::max(normal_quality_preview_image.width(), normal_quality_preview_image.height()) > 512 ||
      std::max(high_quality_preview_image.width(), high_quality_preview_image.height()) <= 512 ||
      full_resolution_preview_image.size() != high_quality_refinement_source.size() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_full_preview_generation(
          high_quality_preview_controller) != full_preview_generation_before) {
    const auto failure_detail =
        QStringLiteral(
            "selection refinement preview quality should support normal, high, and full-resolution active matte sources "
            "without scheduling a full image render (normal=%1x%2 high=%3x%4 full=%5x%6 expected-full=%7x%8)")
            .arg(normal_quality_preview_image.width())
            .arg(normal_quality_preview_image.height())
            .arg(high_quality_preview_image.width())
            .arg(high_quality_preview_image.height())
            .arg(full_resolution_preview_image.width())
            .arg(full_resolution_preview_image.height())
            .arg(high_quality_refinement_source.width())
            .arg(high_quality_refinement_source.height())
            .toUtf8();
    return fail(failure_detail.constData());
  }

  photo_editor::desktop::EditorPreviewController raster_outline_controller;
  raster_outline_controller.loadAsset(
      QStringLiteral("asset-raster-backed-outline-preview"),
      refinement_preview_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  raster_outline_controller.startSelectionBrush();
  raster_outline_controller.setActiveMaskBrushSize(0.18);
  raster_outline_controller.addBrushPoint(0.44, 0.52);
  const auto raster_outline_before_expand = raster_outline_controller.activeMaskOutlinePaths();
  raster_outline_controller.setActiveMaskSelectionExpand(55.0);
  const auto raster_outline_after_expand = raster_outline_controller.activeMaskOutlinePaths();
  if (raster_outline_controller.activeMaskKind() != QStringLiteral("brush") ||
      raster_outline_controller.activeMaskToolRole() != QStringLiteral("selection") ||
      raster_outline_before_expand.isEmpty() ||
      raster_outline_after_expand.isEmpty() ||
      raster_outline_before_expand == raster_outline_after_expand) {
    return fail("asset-backed active selection outlines should be traced from the raster matte even for a single source");
  }
  photo_editor::desktop::EditorPreviewController stale_low_res_outline_controller;
  stale_low_res_outline_controller.loadAsset(
      QStringLiteral("asset-stale-low-res-brush-outline"),
      high_quality_refinement_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  stale_low_res_outline_controller.startSelectionBrush();
  stale_low_res_outline_controller.setActiveMaskBrushSize(0.18);
  stale_low_res_outline_controller.addBrushPoint(0.46, 0.54);
  stale_low_res_outline_controller.setMaskFullResolutionPreview(true);
  auto stale_low_res_brush_mask =
      stale_low_res_outline_controller.maskStack()
          .at(stale_low_res_outline_controller.activeMaskIndex())
          .toMap();
  stale_low_res_brush_mask.insert(QStringLiteral("segmentationData"), QByteArray(2 * 2, char(0xff)));
  stale_low_res_brush_mask.insert(QStringLiteral("segmentationWidth"), 2);
  stale_low_res_brush_mask.insert(QStringLiteral("segmentationHeight"), 2);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          stale_low_res_outline_controller,
          stale_low_res_brush_mask)) {
    return fail("test setup should be able to poison a brush mask with stale low-resolution matte data");
  }
  const auto stale_low_res_outline_paths =
      stale_low_res_outline_controller.activeMaskOutlinePaths();
  const auto stale_low_res_outline_trace =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_mask_outline_trace_fields(
          stale_low_res_outline_controller);
  if (stale_low_res_outline_paths.isEmpty() ||
      stale_low_res_outline_controller.activeMaskOutlineQualityTier() !=
          QStringLiteral("settled-alpha") ||
      stale_low_res_outline_trace.value(QStringLiteral("outline_trace_source")).toString() !=
          QStringLiteral("composite-alpha") ||
      stale_low_res_outline_trace.value(QStringLiteral("mask_alpha_provider")).toString() !=
          QStringLiteral("brush-stroke-replay") ||
      stale_low_res_outline_trace.value(QStringLiteral("outline_coordinate_space")).toString() !=
          QStringLiteral("image-normalized") ||
      stale_low_res_outline_trace.value(QStringLiteral("outline_alpha_source_contract")).toString() !=
          QStringLiteral("same-final-alpha-as-layer-mask:v1") ||
      stale_low_res_outline_trace.value(QStringLiteral("outline_alpha_source_provider")).toString() !=
          QStringLiteral("brush-stroke-replay") ||
      stale_low_res_outline_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt() !=
          high_quality_refinement_source.width() ||
      stale_low_res_outline_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt() !=
          high_quality_refinement_source.height() ||
      stale_low_res_outline_trace.value(QStringLiteral("mask_alpha_trace_signature"))
          .toString()
          .contains(QStringLiteral("raster:2x2"))) {
    const auto failure_detail =
        QStringLiteral(
            "active brush outlines should ignore stale low-resolution segmentation mattes and trace rebuilt brush-stroke alpha at the current preview raster "
            "(paths=%1 tier=%2 source=%3 provider=%4 raster=%5x%6 expected=%7x%8 signature=%9)")
            .arg(stale_low_res_outline_paths.size())
            .arg(stale_low_res_outline_controller.activeMaskOutlineQualityTier())
            .arg(stale_low_res_outline_trace.value(QStringLiteral("outline_trace_source")).toString())
            .arg(stale_low_res_outline_trace.value(QStringLiteral("mask_alpha_provider")).toString())
            .arg(stale_low_res_outline_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt())
            .arg(stale_low_res_outline_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt())
            .arg(high_quality_refinement_source.width())
            .arg(high_quality_refinement_source.height())
            .arg(stale_low_res_outline_trace.value(QStringLiteral("mask_alpha_trace_signature")).toString())
            .toUtf8();
    return fail(failure_detail.constData());
  }

  controller.startSelectionBrush();
  controller.setActiveMaskExposure(0.44);
  controller.setActiveMaskTemperature(18.0);
  controller.setActiveMaskOpacity(0.66);
  controller.setActiveMaskDensity(0.74);
  controller.setActiveMaskFeather(0.72);
  controller.setActiveMaskBrushSize(0.13);
  controller.setActiveMaskBrushFlow(0.61);
  controller.setActiveMaskSelectionInverted(true);
  controller.setActiveMaskSelectionExpand(19.0);
  controller.setActiveMaskSelectionSmooth(27.0);
  controller.setActiveMaskSelectionFineEdge(48.0);
  controller.setActiveMaskSelectionEdgeContrast(39.0);
  controller.setActiveMaskSelectionDecontaminate(22.0);
  auto reset_refine_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  reset_refine_mask.insert(QStringLiteral("selectionGrow"), 34.0);
  reset_refine_mask.insert(QStringLiteral("selectionShrink"), 12.0);
  reset_refine_mask.insert(QStringLiteral("selectionFeather"), 46.0);
  reset_refine_mask.insert(QStringLiteral("selectionBlur"), 5.0);
  reset_refine_mask.insert(QStringLiteral("selectionHarden"), 58.0);
  reset_refine_mask.insert(QStringLiteral("selectionDespeckle"), 33.0);
  reset_refine_mask.insert(QStringLiteral("selectionFillHoles"), 44.0);
  reset_refine_mask.insert(QStringLiteral("selectionRemoveIslands"), 55.0);
  reset_refine_mask.insert(QStringLiteral("maskSetSelectionInvert"), true);
  reset_refine_mask.insert(QStringLiteral("maskSetSelectionExpand"), 7.0);
  reset_refine_mask.insert(QStringLiteral("maskSetSelectionSmooth"), 8.0);
  reset_refine_mask.insert(QStringLiteral("maskSetSelectionFineEdge"), 9.0);
  reset_refine_mask.insert(QStringLiteral("maskSetSelectionEdgeContrast"), 10.0);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          controller,
          reset_refine_mask) ||
      !controller.resetActiveMaskRefinement()) {
    return fail("reset refinement should apply to an editable selection source");
  }
  const auto reset_refine_result = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  if (controller.activeMaskSelectionInverted() ||
      !almost_equal(controller.activeMaskSelectionExpand(), 0.0) ||
      !almost_equal(controller.activeMaskSelectionSmooth(), 0.0) ||
      !almost_equal(controller.activeMaskSelectionFineEdge(), 0.0) ||
      !almost_equal(controller.activeMaskSelectionEdgeContrast(), 0.0) ||
      !almost_equal(controller.activeMaskSelectionDecontaminate(), 0.0) ||
      !almost_equal(reset_refine_result.value(QStringLiteral("selectionGrow")).toDouble(), 0.0) ||
      !almost_equal(reset_refine_result.value(QStringLiteral("selectionShrink")).toDouble(), 0.0) ||
      !almost_equal(reset_refine_result.value(QStringLiteral("selectionFeather")).toDouble(), 0.0) ||
      !almost_equal(reset_refine_result.value(QStringLiteral("selectionBlur")).toDouble(), 0.0) ||
      !almost_equal(reset_refine_result.value(QStringLiteral("selectionHarden")).toDouble(), 0.0) ||
      !almost_equal(reset_refine_result.value(QStringLiteral("selectionDespeckle")).toDouble(), 0.0) ||
      !almost_equal(reset_refine_result.value(QStringLiteral("selectionFillHoles")).toDouble(), 0.0) ||
      !almost_equal(reset_refine_result.value(QStringLiteral("selectionRemoveIslands")).toDouble(), 0.0) ||
      reset_refine_result.value(QStringLiteral("maskSetSelectionInvert")).toBool() ||
      !almost_equal(reset_refine_result.value(QStringLiteral("maskSetSelectionExpand")).toDouble(), 0.0) ||
      !almost_equal(reset_refine_result.value(QStringLiteral("maskSetSelectionSmooth")).toDouble(), 0.0) ||
      !almost_equal(reset_refine_result.value(QStringLiteral("maskSetSelectionFineEdge")).toDouble(), 0.0) ||
      !almost_equal(reset_refine_result.value(QStringLiteral("maskSetSelectionEdgeContrast")).toDouble(), 0.0)) {
    return fail("reset refinement should clear source and mask-set matte refinement fields");
  }
  if (!almost_equal(controller.activeMaskExposure(), 0.44) ||
      !almost_equal(controller.activeMaskTemperature(), 18.0) ||
      !almost_equal(controller.activeMaskOpacity(), 0.66) ||
      !almost_equal(controller.activeMaskDensity(), 0.74) ||
      !almost_equal(controller.activeMaskFeather(), 0.72) ||
      !almost_equal(controller.activeMaskBrushSize(), 0.13) ||
      !almost_equal(controller.activeMaskBrushFlow(), 0.61)) {
    return fail("reset refinement should preserve source geometry and local adjustments");
  }
  controller.removeActiveMask();

  const auto polygon_stack_count_before = controller.maskStack().size();
  controller.beginCoalescedEdit();
  controller.startPolygonSelection();
  controller.appendActivePolygonPoint(0.15, 0.15);
  controller.appendActivePolygonPoint(0.82, 0.18);
  controller.appendActivePolygonPoint(0.76, 0.84);
  if (controller.activeMaskKind() != QStringLiteral("polygon") ||
      controller.activeMaskPolygonPointCount() != 3 ||
      controller.activeMaskOutlinePoints().size() != 3) {
    return fail("polygon selection should track drawn points and expose an outline");
  }
  const auto polygon_bounds = outline_bounds(controller.activeMaskOutlinePaths());
  const auto expected_polygon_bounds = QRectF(QPointF(0.15, 0.15), QPointF(0.82, 0.84));
  if (!outline_bounds_match(polygon_bounds, expected_polygon_bounds, 1e-6)) {
    const auto detail =
        outline_bounds_detail(QStringLiteral("polygon"), polygon_bounds, expected_polygon_bounds).toUtf8();
    return fail(detail.constData());
  }
  const auto initial_polygon = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  const auto initial_polygon_signature =
      initial_polygon.value(QStringLiteral("shapeAlphaSourceSignature")).toString();
  if (initial_polygon.value(QStringLiteral("shapeAlphaSourceContract")).toString() !=
          QStringLiteral("editable-path-plus-alpha-tile-cache-v1") ||
      initial_polygon.value(QStringLiteral("shapeAlphaSourceOfTruth")).toString() !=
          QStringLiteral("polygonPoints") ||
      initial_polygon.value(QStringLiteral("shapeAlphaPointCount")).toInt() != 3 ||
      initial_polygon.value(QStringLiteral("shapeAlphaRawPointCount")).toInt() != 0 ||
      !initial_polygon.value(QStringLiteral("shapeAlphaClosed")).toBool() ||
      !initial_polygon.value(QStringLiteral("shapeAlphaEditable")).toBool() ||
      initial_polygon.value(QStringLiteral("shapeAlphaTileCacheKind")).toString() !=
          QStringLiteral("mask-alpha-tile-cache") ||
      !initial_polygon.value(QStringLiteral("shapeAlphaTileCacheEnabled")).toBool() ||
      initial_polygon.value(QStringLiteral("shapeAlphaTileCacheTileSize")).toInt() != 256 ||
      !initial_polygon_signature.startsWith(QStringLiteral("sha1:"))) {
    return fail("polygon selections should expose editable path plus alpha tile cache metadata");
  }
  const auto grouped_polygon_source =
      grouped_source_for_mask(initial_polygon.value(QStringLiteral("id")).toString());
  if (grouped_polygon_source.value(QStringLiteral("shapeAlphaSourceSignature")).toString() !=
          initial_polygon_signature ||
      grouped_polygon_source.value(QStringLiteral("shapeAlphaSourceContract")).toString() !=
          QStringLiteral("editable-path-plus-alpha-tile-cache-v1") ||
      grouped_polygon_source.value(QStringLiteral("shapeAlphaSourceOfTruth")).toString() !=
          QStringLiteral("polygonPoints") ||
      grouped_polygon_source.value(QStringLiteral("shapeAlphaPointCount")).toInt() != 3 ||
      grouped_polygon_source.value(QStringLiteral("shapeAlphaRawPointCount")).toInt() != 0 ||
      !grouped_polygon_source.value(QStringLiteral("shapeAlphaClosed")).toBool() ||
      !grouped_polygon_source.value(QStringLiteral("shapeAlphaEditable")).toBool() ||
      grouped_polygon_source.value(QStringLiteral("shapeAlphaTileCacheKind")).toString() !=
          QStringLiteral("mask-alpha-tile-cache") ||
      !grouped_polygon_source.value(QStringLiteral("shapeAlphaTileCacheEnabled")).toBool() ||
      grouped_polygon_source.value(QStringLiteral("shapeAlphaTileCacheTileSize")).toInt() !=
          256) {
    return fail("grouped polygon sources should preserve editable path cache metadata");
  }
  if (controller.activeMaskPolygonSelfIntersecting()) {
    return fail("simple polygon selections should not report self-intersection");
  }
  controller.endCoalescedEdit();
  if (!controller.canUndo()) {
    return fail("polygon creation should be undoable after its coalesced draw transaction");
  }
  controller.undo();
  if (controller.maskStack().size() != polygon_stack_count_before) {
    return fail("one undo should remove the entire coalesced polygon creation");
  }
  if (!controller.canRedo()) {
    return fail("polygon creation should be redoable after a coalesced undo");
  }
  controller.redo();
  if (controller.activeMaskKind() != QStringLiteral("polygon") ||
      controller.activeMaskPolygonPointCount() != 3 ||
      controller.activeMaskOutlinePoints().size() != 3 ||
      controller.maskStack()
              .at(controller.activeMaskIndex())
              .toMap()
              .value(QStringLiteral("shapeAlphaSourceSignature"))
              .toString() != initial_polygon_signature) {
    return fail("redo should restore the drawn polygon from one coalesced history entry");
  }
  controller.removeLastActivePolygonPoint();
  if (controller.activeMaskPolygonPointCount() != 2) {
    return fail("polygon selection should support removing the last point");
  }
  controller.clearActivePolygonPoints();
  if (controller.activeMaskPolygonPointCount() != 0) {
    return fail("polygon selection should support clearing all points");
  }
  controller.appendActivePolygonPoint(0.15, 0.15);
  controller.appendActivePolygonPoint(0.85, 0.85);
  controller.appendActivePolygonPoint(0.15, 0.85);
  controller.appendActivePolygonPoint(0.85, 0.15);
  if (!controller.activeMaskPolygonSelfIntersecting()) {
    return fail("bowtie polygon selections should report self-intersection");
  }
  controller.removeLastActivePolygonPoint();
  if (controller.activeMaskPolygonSelfIntersecting()) {
    return fail("polygon self-intersection should clear after removing the crossing point");
  }
  controller.clearActivePolygonPoints();
  controller.appendActivePolygonPoint(0.10, 0.10);
  controller.appendActivePolygonPoint(0.30, 0.101);
  controller.appendActivePolygonPoint(0.50, 0.10);
  controller.appendActivePolygonPoint(0.80, 0.10);
  controller.appendActivePolygonPoint(0.80, 0.80);
  controller.appendActivePolygonPoint(0.10, 0.80);
  const auto noisy_polygon_count = controller.activeMaskPolygonPointCount();
  const auto noisy_polygon_signature =
      controller.maskStack()
          .at(controller.activeMaskIndex())
          .toMap()
          .value(QStringLiteral("shapeAlphaSourceSignature"))
          .toString();
  if (!controller.simplifyActivePolygon(0.012) ||
      controller.activeMaskPolygonPointCount() >= noisy_polygon_count ||
      controller.activeMaskPolygonPointCount() < 3) {
    return fail("polygon simplify should remove redundant near-collinear points while preserving a valid polygon");
  }
  const auto simplified_polygon = controller.activeMaskPolygonPoints();
  const auto simplified_polygon_count = controller.activeMaskPolygonPointCount();
  if (!controller.smoothActivePolygon() ||
      controller.activeMaskPolygonPointCount() != simplified_polygon_count * 2 ||
      controller.activeMaskPolygonPoints().constFirst().toMap() ==
          simplified_polygon.constFirst().toMap()) {
    return fail("polygon smooth should apply a closed smoothing pass and keep the result editable");
  }
  const auto smoothed_polygon = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  const auto smoothed_polygon_signature =
      smoothed_polygon.value(QStringLiteral("shapeAlphaSourceSignature")).toString();
  if (smoothed_polygon.value(QStringLiteral("shapeAlphaPointCount")).toInt() !=
          controller.activeMaskPolygonPointCount() ||
      smoothed_polygon_signature == noisy_polygon_signature ||
      smoothed_polygon.value(QStringLiteral("shapeAlphaSourceContract")).toString() !=
          QStringLiteral("editable-path-plus-alpha-tile-cache-v1")) {
    return fail("polygon simplification and smoothing should update editable path cache identity");
  }
  const auto moved_polygon_count = controller.activeMaskPolygonPointCount();
  if (!controller.moveActivePolygonPoint(1, -0.25, 1.25) ||
      controller.activeMaskPolygonPointCount() != moved_polygon_count) {
    return fail("polygon vertices should be movable without changing the point count");
  }
  const auto moved_polygon_point = controller.activeMaskPolygonPoints().at(1).toMap();
  if (moved_polygon_point.value(QStringLiteral("x")).toDouble() != 0.0 ||
      moved_polygon_point.value(QStringLiteral("y")).toDouble() != 1.0 ||
      controller.moveActivePolygonPoint(-1, 0.5, 0.5) ||
      controller.moveActivePolygonPoint(moved_polygon_count, 0.5, 0.5)) {
    return fail("polygon vertex movement should clamp coordinates and reject invalid point indices");
  }
  const auto moved_polygon_signature =
      controller.maskStack()
          .at(controller.activeMaskIndex())
          .toMap()
          .value(QStringLiteral("shapeAlphaSourceSignature"))
          .toString();
  if (moved_polygon_signature == smoothed_polygon_signature) {
    return fail("moving a polygon vertex should update editable path cache identity");
  }
  if (!controller.insertActivePolygonPointAfter(1, 1.25, -0.25) ||
      controller.activeMaskPolygonPointCount() != moved_polygon_count + 1) {
    return fail("polygon edge insertion should add one point after the selected edge start");
  }
  const auto inserted_polygon_point = controller.activeMaskPolygonPoints().at(2).toMap();
  if (inserted_polygon_point.value(QStringLiteral("x")).toDouble() != 1.0 ||
      inserted_polygon_point.value(QStringLiteral("y")).toDouble() != 0.0 ||
      controller.insertActivePolygonPointAfter(-1, 0.5, 0.5) ||
      controller.insertActivePolygonPointAfter(controller.activeMaskPolygonPointCount(), 0.5, 0.5)) {
    return fail("polygon edge insertion should clamp coordinates and reject invalid edge indices");
  }
  const auto inserted_polygon = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  if (inserted_polygon.value(QStringLiteral("shapeAlphaPointCount")).toInt() !=
          controller.activeMaskPolygonPointCount() ||
      inserted_polygon.value(QStringLiteral("shapeAlphaSourceSignature")).toString() ==
          moved_polygon_signature) {
    return fail("inserting a polygon edge point should update editable path cache identity");
  }

  photo_editor::desktop::EditorPreviewController empty_mask_controller;
  empty_mask_controller.addMask(QStringLiteral("linear"));
  if (empty_mask_controller.emptyMaskCount() != 0) {
    return fail("linear selection sources should not be treated as empty masks");
  }
  empty_mask_controller.addMask(QStringLiteral("polygon"));
  if (empty_mask_controller.emptyMaskCount() != 1) {
    return fail("unfinished polygon selections should be counted as empty masks");
  }
  if (empty_mask_controller.deleteEmptyMasks() != 1 ||
      empty_mask_controller.maskCount() != 1 ||
      empty_mask_controller.emptyMaskCount() != 0 ||
      empty_mask_controller.activeMaskKind() != QStringLiteral("linear")) {
    return fail("Delete Empty Masks should remove invalid selection sources and keep valid masks active");
  }
  empty_mask_controller.addMask(QStringLiteral("brush"));
  if (empty_mask_controller.emptyMaskCount() != 1) {
    return fail("unpainted brush selection sources should be counted as empty masks");
  }
  empty_mask_controller.addBrushPoint(0.5, 0.5);
  if (empty_mask_controller.emptyMaskCount() != 0) {
    return fail("painted brush selection sources should stop being reported as empty masks");
  }
  empty_mask_controller.addMask(QStringLiteral("object"));
  if (empty_mask_controller.emptyMaskCount() != 1) {
    return fail("generated/object selection sources without a valid matte should be counted as empty masks");
  }

  photo_editor::desktop::EditorPreviewController validation_controller;
  validation_controller.addMask(QStringLiteral("linear"));
  auto impossible_selection = validation_controller.maskStack().at(0).toMap();
  impossible_selection.insert(QStringLiteral("retouchMode"), QStringLiteral("remove"));
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          validation_controller,
          impossible_selection) ||
      validation_controller.maskStack().at(0).toMap().value(QStringLiteral("retouchMode")).toString() ==
          QStringLiteral("remove")) {
    return fail("mask writes should reject selection sources that carry retouch modes");
  }
  auto impossible_retouch = validation_controller.maskStack().at(0).toMap();
  impossible_retouch.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
  impossible_retouch.insert(QStringLiteral("retouchMode"), QStringLiteral("remove"));
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::update_active_mask_definition(
          validation_controller,
          impossible_retouch)) {
    return fail("mask writes should reject retouch mode on adjustment-only geometry");
  }
  validation_controller.startRemoveBrush();
  const auto validation_active_mask =
      validation_controller.maskStack().at(validation_controller.activeMaskIndex()).toMap();
  if (validation_active_mask.value(QStringLiteral("retouchMode")).toString() != QStringLiteral("remove") ||
      validation_active_mask.value(QStringLiteral("toolRole")).toString() != QStringLiteral("retouch")) {
    return fail("mask validation should continue accepting valid retouch brush sources");
  }

  photo_editor::desktop::EditorPreviewController camera_default_controller;
  camera_default_controller.loadAsset(
      QStringLiteral("camera-default-source"),
      QString(),
      QString(),
      QStringLiteral("Canon EOS R5"),
      QString(),
      QString(),
      QString());
  camera_default_controller.setRawCalibrationHue(17.0);
  camera_default_controller.setRawRedPrimarySaturation(28.0);
  camera_default_controller.setRawCameraMatchingLook(QStringLiteral("Landscape"));
  camera_default_controller.setExposure(0.42);
  camera_default_controller.setCropPreset(QStringLiteral("4x5"));
  camera_default_controller.addMask(QStringLiteral("linear"));
  if (!camera_default_controller.saveCameraDefault() || !camera_default_controller.hasCameraDefault()) {
    return fail("saving camera defaults should persist defaults for the active camera");
  }

  camera_default_controller.loadAsset(
      QStringLiteral("camera-default-target"),
      QString(),
      QString(),
      QStringLiteral("Canon EOS R5"),
      QString(),
      QString(),
      QString());
  if (!almost_equal(camera_default_controller.rawCalibrationHue(), 17.0) ||
      !almost_equal(camera_default_controller.rawRedPrimarySaturation(), 28.0) ||
      camera_default_controller.rawCameraMatchingLook() != QStringLiteral("Landscape") ||
      !almost_equal(camera_default_controller.exposure(), 0.42) ||
      camera_default_controller.cropPreset() != QStringLiteral("Original") ||
      camera_default_controller.maskCount() != 0) {
    return fail("new assets from a camera should inherit sanitized per-camera defaults");
  }

  camera_default_controller.loadAsset(
      QStringLiteral("camera-default-other"),
      QString(),
      QString(),
      QStringLiteral("Nikon Z8"),
      QString(),
      QString(),
      QString());
  if (camera_default_controller.hasCameraDefault() ||
      !almost_equal(camera_default_controller.rawCalibrationHue(), 0.0) ||
      camera_default_controller.rawCameraMatchingLook() != QStringLiteral("Standard") ||
      !almost_equal(camera_default_controller.exposure(), 0.0)) {
    return fail("camera defaults should not bleed into other camera models");
  }

  photo_editor::desktop::EditorPreviewController reloaded_controller;
  if (!reloaded_controller.presetNames().contains(QStringLiteral("Portrait Base"))) {
    return fail("saved presets should reload from app data");
  }
  if (reloaded_controller.presetRecipeJson(QStringLiteral("Portrait Base")).isEmpty()) {
    return fail("reloaded presets should retain their recipe payload");
  }
  if (!reloaded_controller.presetPackNames().contains(QStringLiteral("Client Pack")) ||
      reloaded_controller.presetPackName(QStringLiteral("Client Warmth")) != QStringLiteral("Client Pack") ||
      reloaded_controller.presetCategoryName(QStringLiteral("Client Warmth")) != QStringLiteral("Client Pack")) {
    return fail("saved user preset packs should reload from app data");
  }
  if (!reloaded_controller.isPresetFavorite(QStringLiteral("Client Warmth")) ||
      !reloaded_controller.favoritePresetNames().contains(QStringLiteral("Client Warmth"))) {
    return fail("saved preset favorites should reload from app data");
  }
  reloaded_controller.loadAsset(
      QStringLiteral("camera-default-reloaded"),
      QString(),
      QString(),
      QStringLiteral("Canon EOS R5"),
      QString(),
      QString(),
      QString());
  if (!reloaded_controller.hasCameraDefault() ||
      !almost_equal(reloaded_controller.rawCalibrationHue(), 17.0) ||
      !almost_equal(reloaded_controller.rawRedPrimarySaturation(), 28.0) ||
      reloaded_controller.rawCameraMatchingLook() != QStringLiteral("Landscape")) {
    return fail("saved camera defaults should reload from app data");
  }
  if (!reloaded_controller.clearCameraDefault() || reloaded_controller.hasCameraDefault()) {
    return fail("camera defaults should be clearable for the active camera");
  }

  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  qunsetenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION");
  return EXIT_SUCCESS;
}
